#ifndef AUDIO_ENGINE_H
#define AUDIO_ENGINE_H

#include "cd_drive.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    AUDIO_STATE_STOPPED = 0,
    AUDIO_STATE_PLAYING,
    AUDIO_STATE_PAUSED,
    AUDIO_STATE_SEEKING
} audio_state_t;

typedef struct {
    audio_state_t state;
    int current_track;
    uint32_t current_lba;
    uint32_t track_start_lba;
    uint32_t track_end_lba;
    uint32_t track_elapsed_ms;
    uint32_t track_total_ms;
    float volume; /* 0.0f to 1.0f */
} audio_status_info_t;

/* Initialize audio engine and start PPU playback thread */
int audio_engine_init(const cd_toc_t *toc);

/* Shutdown audio engine and terminate thread */
void audio_engine_shutdown(void);

/* Update TOC reference (e.g. after disc change) */
void audio_engine_set_toc(const cd_toc_t *toc);

/* Playback controls */
int audio_play_track(int track_number);
void audio_pause(void);
void audio_resume(void);
void audio_toggle_pause(void);
void audio_stop(void);
void audio_seek(int delta_seconds);
void audio_set_volume(float volume);
float audio_get_volume(void);

/* Query current playback status */
void audio_get_status(audio_status_info_t *out_status);

/* Get recent audio samples (mono mix, normalized -1.0f to 1.0f) for FFT visualizer */
int audio_get_vis_samples(float *out_samples, int num_samples);

#endif /* AUDIO_ENGINE_H */
