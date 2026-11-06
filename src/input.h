#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    /* Single press / trigger edges */
    bool cross_pressed;
    bool start_pressed;
    bool select_pressed;
    bool triangle_pressed;
    bool circle_pressed;
    bool square_pressed;
    bool up_pressed;
    bool down_pressed;
    bool left_pressed;
    bool right_pressed;
    bool l1_pressed;
    bool r1_pressed;

    /* Continuous held states (for seeking, etc.) */
    bool l2_held;
    bool r2_held;
    bool up_held;
    bool down_held;

    /* Analog sticks */
    int left_stick_x;
    int left_stick_y;
    int right_stick_x;
    int right_stick_y;
} controller_input_t;

/* Initialize controller subsystem */
int input_init(void);

/* Shutdown controller subsystem */
void input_shutdown(void);

/* Poll controller state (call once per frame) */
void input_poll(controller_input_t *out_input);

#endif /* INPUT_H */
