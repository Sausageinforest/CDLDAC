#include "audio_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <math.h>

#if defined(__PS3__) || defined(__PPU__)
#if defined(__has_include)
#if __has_include(<audio/audio.h>)
#include <audio/audio.h>
#define HAVE_PSL1GHT_AUDIO 1
#endif
#endif

#ifdef HAVE_PSL1GHT_AUDIO
static u32 s_audio_port_num = 0;
#endif
#endif

#define RING_BUFFER_SIZE (1024 * 1024)   /* 1 MB ring buffer (~5.9 seconds of audio) */
#define VIS_BUFFER_SIZE 2048
#define AUDIO_FRAMES_PER_BLOCK 512       /* 512 stereo frames = 2048 bytes */
#define AUDIO_BYTES_PER_BLOCK (AUDIO_FRAMES_PER_BLOCK * 4)

static uint8_t s_ring_buffer[RING_BUFFER_SIZE];
static size_t s_rb_read = 0;
static size_t s_rb_write = 0;
static size_t s_rb_used = 0;
static pthread_mutex_t s_rb_mutex = PTHREAD_MUTEX_INITIALIZER;

static float s_vis_buffer[VIS_BUFFER_SIZE];
static size_t s_vis_write_idx = 0;
static pthread_mutex_t s_vis_mutex = PTHREAD_MUTEX_INITIALIZER;

static cd_toc_t s_current_toc;
static audio_status_info_t s_status = {
    .state = AUDIO_STATE_STOPPED,
    .current_track = 1,
    .current_lba = 0,
    .track_start_lba = 0,
    .track_end_lba = 0,
    .track_elapsed_ms = 0,
    .track_total_ms = 0,
    .volume = 1.0f
};
static pthread_mutex_t s_status_mutex = PTHREAD_MUTEX_INITIALIZER;

static volatile bool s_engine_running = false;
static pthread_t s_thread_handle;

/* Ring buffer functions (thread safe) */
static void rb_clear(void) {
    pthread_mutex_lock(&s_rb_mutex);
    s_rb_read = 0;
    s_rb_write = 0;
    s_rb_used = 0;
    pthread_mutex_unlock(&s_rb_mutex);
}

static size_t rb_free_space(void) {
    pthread_mutex_lock(&s_rb_mutex);
    size_t free_sp = RING_BUFFER_SIZE - s_rb_used;
    pthread_mutex_unlock(&s_rb_mutex);
    return free_sp;
}

static size_t rb_push(const uint8_t *data, size_t len) {
    pthread_mutex_lock(&s_rb_mutex);
    size_t to_write = (len > (RING_BUFFER_SIZE - s_rb_used)) ? (RING_BUFFER_SIZE - s_rb_used) : len;
    for (size_t i = 0; i < to_write; i++) {
        s_ring_buffer[s_rb_write] = data[i];
        s_rb_write = (s_rb_write + 1) % RING_BUFFER_SIZE;
    }
    s_rb_used += to_write;
    pthread_mutex_unlock(&s_rb_mutex);
    return to_write;
}

static size_t rb_pop(uint8_t *dest, size_t len) {
    pthread_mutex_lock(&s_rb_mutex);
    size_t to_read = (len > s_rb_used) ? s_rb_used : len;
    for (size_t i = 0; i < to_read; i++) {
        dest[i] = s_ring_buffer[s_rb_read];
        s_rb_read = (s_rb_read + 1) % RING_BUFFER_SIZE;
    }
    s_rb_used -= to_read;
    pthread_mutex_unlock(&s_rb_mutex);
    return to_read;
}

/* Visualization buffer feeder */
static void vis_feed(const int16_t *stereo_samples, int num_frames) {
    pthread_mutex_lock(&s_vis_mutex);
    for (int i = 0; i < num_frames; i++) {
        float mono = ((float)stereo_samples[i * 2] + (float)stereo_samples[i * 2 + 1]) / 65536.0f;
        s_vis_buffer[s_vis_write_idx] = mono;
        s_vis_write_idx = (s_vis_write_idx + 1) % VIS_BUFFER_SIZE;
    }
    pthread_mutex_unlock(&s_vis_mutex);
}

int audio_get_vis_samples(float *out_samples, int num_samples) {
    if (!out_samples || num_samples <= 0) return -1;
    pthread_mutex_lock(&s_vis_mutex);
    size_t read_start = (s_vis_write_idx + VIS_BUFFER_SIZE - num_samples) % VIS_BUFFER_SIZE;
    for (int i = 0; i < num_samples; i++) {
        out_samples[i] = s_vis_buffer[(read_start + i) % VIS_BUFFER_SIZE];
    }
    pthread_mutex_unlock(&s_vis_mutex);
    return 0;
}

/* Audio streaming background thread */
static void *audio_thread_worker(void *arg) {
    (void)arg;
    uint8_t read_chunk[CD_SECTOR_SIZE_RAW * 4]; /* Read 4 sectors at once = 9408 bytes */
    uint8_t play_chunk[AUDIO_BYTES_PER_BLOCK];

#ifdef HAVE_PSL1GHT_AUDIO
    audioPortParam params;
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks = 8;
    params.attrib = 0;
    params.level = 1.0f;

    if (audioPortOpen(&params, &s_audio_port_num) == 0) {
        audioPortStart(s_audio_port_num);
        printf("[AUDIO] PS3 audio port opened and started\n");
    }
#endif

    while (s_engine_running) {
        audio_state_t cur_state;
        int track_num;
        uint32_t cur_lba, end_lba, start_lba;
        float vol;

        pthread_mutex_lock(&s_status_mutex);
        cur_state = s_status.state;
        track_num = s_status.current_track;
        cur_lba = s_status.current_lba;
        start_lba = s_status.track_start_lba;
        end_lba = s_status.track_end_lba;
        vol = s_status.volume;
        pthread_mutex_unlock(&s_status_mutex);

        if (cur_state == AUDIO_STATE_PLAYING) {
            /* 1. Drive reading: if ring buffer has space, fill it */
            if (rb_free_space() >= sizeof(read_chunk) && cur_lba < end_lba) {
                uint32_t sectors_to_read = 4;
                if (cur_lba + sectors_to_read > end_lba) {
                    sectors_to_read = end_lba - cur_lba;
                }

                if (cd_drive_read_audio_sectors(cur_lba, sectors_to_read, read_chunk) == 0) {
                    rb_push(read_chunk, sectors_to_read * CD_SECTOR_SIZE_RAW);
                    cur_lba += sectors_to_read;

                    pthread_mutex_lock(&s_status_mutex);
                    s_status.current_lba = cur_lba;
                    if (cur_lba >= start_lba) {
                        s_status.track_elapsed_ms = ((cur_lba - start_lba) * 1000) / CD_FRAMES_PER_SEC;
                    }
                    pthread_mutex_unlock(&s_status_mutex);
                }
            } else if (cur_lba >= end_lba) {
                /* End of track reached: advance to next track if available */
                if (track_num < s_current_toc.last_track) {
                    audio_play_track(track_num + 1);
                } else {
                    audio_stop();
                }
            }

            /* 2. Audio drain and playback */
            size_t bytes_popped = rb_pop(play_chunk, sizeof(play_chunk));
            if (bytes_popped > 0) {
                int16_t *samples = (int16_t *)play_chunk;
                int frames = bytes_popped / 4;

                /* Apply volume */
                if (vol < 0.99f) {
                    for (int i = 0; i < frames * 2; i++) {
                        int32_t val = (int32_t)((float)samples[i] * vol);
                        if (val > 32767) val = 32767;
                        if (val < -32768) val = -32768;
                        samples[i] = (int16_t)val;
                    }
                }

                /* Export to visualizer */
                vis_feed(samples, frames);

#ifdef HAVE_PSL1GHT_AUDIO
                /* Write to hardware port */
                cellAudioPortWrite(s_audio_port_num, play_chunk);
#else
                /* Simulated playback pace: 512 frames @ 44100Hz = ~11.6ms */
                usleep(11000);
#endif
            } else {
                usleep(2000); /* Buffer underrun wait */
            }
        } else {
            /* Paused or Stopped */
            usleep(20000);
        }
    }

#ifdef HAVE_PSL1GHT_AUDIO
    audioPortStop(s_audio_port_num);
    audioPortClose(s_audio_port_num);
#endif
    return NULL;
}

int audio_engine_init(const cd_toc_t *toc) {
    if (toc) {
        memcpy(&s_current_toc, toc, sizeof(cd_toc_t));
    }
    rb_clear();
    memset(s_vis_buffer, 0, sizeof(s_vis_buffer));

#ifdef HAVE_PSL1GHT_AUDIO
    audioInit();
#endif

    s_engine_running = true;
    if (pthread_create(&s_thread_handle, NULL, audio_thread_worker, NULL) != 0) {
        printf("[AUDIO] Failed to create audio thread\n");
        s_engine_running = false;
        return -1;
    }

    printf("[AUDIO] Audio engine initialized\n");
    return 0;
}

void audio_engine_shutdown(void) {
    if (!s_engine_running) return;
    s_engine_running = false;
    pthread_join(s_thread_handle, NULL);

#ifdef HAVE_PSL1GHT_AUDIO
    audioQuit();
#endif
    printf("[AUDIO] Audio engine shutdown\n");
}

void audio_engine_set_toc(const cd_toc_t *toc) {
    if (!toc) return;
    pthread_mutex_lock(&s_status_mutex);
    memcpy(&s_current_toc, toc, sizeof(cd_toc_t));
    pthread_mutex_unlock(&s_status_mutex);
}

int audio_play_track(int track_number) {
    if (track_number < s_current_toc.first_track || track_number > s_current_toc.last_track) {
        return -1;
    }

    rb_clear();

    pthread_mutex_lock(&s_status_mutex);
    s_status.current_track = track_number;
    s_status.track_start_lba = s_current_toc.tracks[track_number].start_lba;
    s_status.current_lba = s_status.track_start_lba;
    uint32_t next_lba = (track_number == s_current_toc.last_track)
                        ? s_current_toc.leadout_lba
                        : s_current_toc.tracks[track_number + 1].start_lba;
    s_status.track_end_lba = next_lba;
    s_status.track_elapsed_ms = 0;
    s_status.track_total_ms = s_current_toc.tracks[track_number].duration_ms;
    s_status.state = AUDIO_STATE_PLAYING;
    pthread_mutex_unlock(&s_status_mutex);

    printf("[AUDIO] Playing track %d (LBA: %u to %u)\n", track_number, s_status.track_start_lba, s_status.track_end_lba);
    return 0;
}

void audio_pause(void) {
    pthread_mutex_lock(&s_status_mutex);
    if (s_status.state == AUDIO_STATE_PLAYING) {
        s_status.state = AUDIO_STATE_PAUSED;
    }
    pthread_mutex_unlock(&s_status_mutex);
}

void audio_resume(void) {
    pthread_mutex_lock(&s_status_mutex);
    if (s_status.state == AUDIO_STATE_PAUSED) {
        s_status.state = AUDIO_STATE_PLAYING;
    }
    pthread_mutex_unlock(&s_status_mutex);
}

void audio_toggle_pause(void) {
    pthread_mutex_lock(&s_status_mutex);
    if (s_status.state == AUDIO_STATE_PLAYING) {
        s_status.state = AUDIO_STATE_PAUSED;
    } else if (s_status.state == AUDIO_STATE_PAUSED) {
        s_status.state = AUDIO_STATE_PLAYING;
    }
    pthread_mutex_unlock(&s_status_mutex);
}

void audio_stop(void) {
    pthread_mutex_lock(&s_status_mutex);
    s_status.state = AUDIO_STATE_STOPPED;
    s_status.track_elapsed_ms = 0;
    pthread_mutex_unlock(&s_status_mutex);
    rb_clear();
}

void audio_seek(int delta_seconds) {
    pthread_mutex_lock(&s_status_mutex);
    int32_t delta_lba = delta_seconds * CD_FRAMES_PER_SEC;
    int32_t target_lba = (int32_t)s_status.current_lba + delta_lba;

    if (target_lba < (int32_t)s_status.track_start_lba) {
        target_lba = s_status.track_start_lba;
    }
    if (target_lba >= (int32_t)s_status.track_end_lba) {
        target_lba = s_status.track_end_lba - 1;
    }

    s_status.current_lba = (uint32_t)target_lba;
    s_status.track_elapsed_ms = ((s_status.current_lba - s_status.track_start_lba) * 1000) / CD_FRAMES_PER_SEC;
    pthread_mutex_unlock(&s_status_mutex);

    rb_clear();
}

void audio_set_volume(float volume) {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    pthread_mutex_lock(&s_status_mutex);
    s_status.volume = volume;
    pthread_mutex_unlock(&s_status_mutex);
}

float audio_get_volume(void) {
    pthread_mutex_lock(&s_status_mutex);
    float v = s_status.volume;
    pthread_mutex_unlock(&s_status_mutex);
    return v;
}

void audio_get_status(audio_status_info_t *out_status) {
    if (!out_status) return;
    pthread_mutex_lock(&s_status_mutex);
    memcpy(out_status, &s_status, sizeof(audio_status_info_t));
    pthread_mutex_unlock(&s_status_mutex);
}
