#ifndef VISUALIZER_H
#define VISUALIZER_H

#include <stdint.h>
#include <stdbool.h>

#define VIS_FFT_SIZE 1024
#define VIS_NUM_BANDS 32

typedef struct {
    float band_values[VIS_NUM_BANDS];   /* Smoothed current amplitude [0.0f .. 1.0f] */
    float peak_values[VIS_NUM_BANDS];   /* Peak indicator position [0.0f .. 1.0f] */
    int peak_hold[VIS_NUM_BANDS];       /* Frames to hold peak before decay */
    float falloff_rate;
    float attack_rate;
} visualizer_t;

/* Initialize visualizer subsystem */
void visualizer_init(visualizer_t *vis);

/* Update visualizer state with latest audio samples */
void visualizer_update(visualizer_t *vis, const float *samples, int num_samples);

/* Reset all visualizer bars and peaks to zero */
void visualizer_reset(visualizer_t *vis);

#endif /* VISUALIZER_H */
