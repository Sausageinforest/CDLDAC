#include "visualizer.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

static float s_hann_window[VIS_FFT_SIZE];
static bool s_window_initialized = false;
static int s_band_bin_start[VIS_NUM_BANDS];
static int s_band_bin_end[VIS_NUM_BANDS];

static void init_tables(void) {
    if (s_window_initialized) return;

    for (int i = 0; i < VIS_FFT_SIZE; i++) {
        s_hann_window[i] = 0.5f * (1.0f - cosf((2.0f * (float)PI * (float)i) / (float)(VIS_FFT_SIZE - 1)));
    }

    /* Compute logarithmic band bin mappings (20 Hz to 20,000 Hz, sample rate 44100 Hz) */
    float bin_freq_step = 44100.0f / (float)VIS_FFT_SIZE; /* ~43.066 Hz per bin */
    float f_min = 20.0f;
    float f_max = 20000.0f;

    for (int b = 0; b < VIS_NUM_BANDS; b++) {
        float f1 = f_min * powf(f_max / f_min, (float)b / (float)VIS_NUM_BANDS);
        float f2 = f_min * powf(f_max / f_min, (float)(b + 1) / (float)VIS_NUM_BANDS);

        int bin1 = (int)(f1 / bin_freq_step);
        int bin2 = (int)(f2 / bin_freq_step);

        if (bin1 < 1) bin1 = 1;
        if (bin2 <= bin1) bin2 = bin1 + 1;
        if (bin2 > (VIS_FFT_SIZE / 2)) bin2 = (VIS_FFT_SIZE / 2);

        s_band_bin_start[b] = bin1;
        s_band_bin_end[b] = bin2;
    }

    s_window_initialized = true;
}

/* Bit-reversal permutation for in-place radix-2 FFT */
static void bit_reverse(float *real, float *imag, int n) {
    int j = 0;
    for (int i = 0; i < n - 1; i++) {
        if (i < j) {
            float tr = real[i]; real[i] = real[j]; real[j] = tr;
            float ti = imag[i]; imag[i] = imag[j]; imag[j] = ti;
        }
        int k = n >> 1;
        while (k <= j) {
            j -= k;
            k >>= 1;
        }
        j += k;
    }
}

/* Cooley-Tukey Radix-2 FFT */
static void fft_radix2(float *real, float *imag, int n) {
    bit_reverse(real, imag, n);

    for (int len = 2; len <= n; len <<= 1) {
        float angle = -2.0f * (float)PI / (float)len;
        float wlen_r = cosf(angle);
        float wlen_i = sinf(angle);

        for (int i = 0; i < n; i += len) {
            float w_r = 1.0f;
            float w_i = 0.0f;
            int half = len >> 1;

            for (int j = 0; j < half; j++) {
                int u = i + j;
                int v = i + j + half;

                float vr = real[v] * w_r - imag[v] * w_i;
                float vi = real[v] * w_i + imag[v] * w_r;

                real[v] = real[u] - vr;
                imag[v] = imag[u] - vi;
                real[u] += vr;
                imag[u] += vi;

                float next_wr = w_r * wlen_r - w_i * wlen_i;
                w_i = w_r * wlen_i + w_i * wlen_r;
                w_r = next_wr;
            }
        }
    }
}

void visualizer_init(visualizer_t *vis) {
    if (!vis) return;
    init_tables();
    visualizer_reset(vis);
    vis->falloff_rate = 0.88f;
    vis->attack_rate = 0.70f;
}

void visualizer_reset(visualizer_t *vis) {
    if (!vis) return;
    for (int i = 0; i < VIS_NUM_BANDS; i++) {
        vis->band_values[i] = 0.0f;
        vis->peak_values[i] = 0.0f;
        vis->peak_hold[i] = 0;
    }
}

void visualizer_update(visualizer_t *vis, const float *samples, int num_samples) {
    if (!vis || !samples || num_samples < VIS_FFT_SIZE) return;
    init_tables();

    float real[VIS_FFT_SIZE];
    float imag[VIS_FFT_SIZE];

    /* Apply Hann window */
    for (int i = 0; i < VIS_FFT_SIZE; i++) {
        real[i] = samples[i] * s_hann_window[i];
        imag[i] = 0.0f;
    }

    fft_radix2(real, imag, VIS_FFT_SIZE);

    /* Compute magnitudes and group into frequency bands */
    for (int b = 0; b < VIS_NUM_BANDS; b++) {
        int b_start = s_band_bin_start[b];
        int b_end = s_band_bin_end[b];

        float sum_mag = 0.0f;
        int count = 0;
        for (int k = b_start; k < b_end; k++) {
            float mag = sqrtf(real[k] * real[k] + imag[k] * imag[k]) / (float)VIS_FFT_SIZE;
            sum_mag += mag;
            count++;
        }
        float avg_mag = (count > 0) ? (sum_mag / (float)count) : 0.0f;

        /* Frequency-dependent equal loudness compensation (boost bass and treble slightly) */
        float freq_boost = 1.0f + 0.8f * ((float)b / (float)VIS_NUM_BANDS);
        avg_mag *= freq_boost;

        /* Convert to normalized logarithmic representation */
        float val = 0.0f;
        if (avg_mag > 0.0001f) {
            float db = 20.0f * log10f(avg_mag); /* typically -80dB to 0dB */
            val = (db + 65.0f) / 65.0f;         /* map -65dB..0dB to 0.0..1.0 */
            if (val < 0.0f) val = 0.0f;
            if (val > 1.0f) val = 1.0f;
        }

        /* Ballistics (Attack / Decay) */
        if (val > vis->band_values[b]) {
            vis->band_values[b] += (val - vis->band_values[b]) * vis->attack_rate;
        } else {
            vis->band_values[b] *= vis->falloff_rate;
        }

        /* Peak marker handling */
        if (vis->band_values[b] >= vis->peak_values[b]) {
            vis->peak_values[b] = vis->band_values[b];
            vis->peak_hold[b] = 10; /* hold peak for 10 frames (~160ms at 60fps) */
        } else {
            if (vis->peak_hold[b] > 0) {
                vis->peak_hold[b]--;
            } else {
                vis->peak_values[b] -= 0.025f;
                if (vis->peak_values[b] < 0.0f) vis->peak_values[b] = 0.0f;
            }
        }
    }
}
