#include "input.h"
#include <stdio.h>
#include <string.h>

#if defined(__PS3__) || defined(__PPU__)
#include <io/pad.h>

static uint32_t s_prev_btn_mask = 0;
#else
static uint32_t s_prev_btn_mask = 0;
#endif

int input_init(void) {
#if defined(__PS3__) || defined(__PPU__)
    ioPadInit(7);
    printf("[INPUT] PS3 ioPad initialized\n");
#endif
    s_prev_btn_mask = 0;
    return 0;
}

void input_shutdown(void) {
#if defined(__PS3__) || defined(__PPU__)
    ioPadEnd();
    printf("[INPUT] PS3 ioPad terminated\n");
#endif
}

void input_poll(controller_input_t *out_input) {
    if (!out_input) return;
    memset(out_input, 0, sizeof(controller_input_t));

#if defined(__PS3__) || defined(__PPU__)
    padInfo padinfo;
    padData paddata;

    if (ioPadGetInfo(&padinfo) != 0 || padinfo.status[0] == 0) {
        return;
    }

    if (ioPadGetData(0, &paddata) != 0 || paddata.len == 0) {
        return;
    }

    uint32_t curr_btn_mask = 0;
    if (paddata.BTN_CROSS)    curr_btn_mask |= (1 << 0);
    if (paddata.BTN_CIRCLE)   curr_btn_mask |= (1 << 1);
    if (paddata.BTN_SQUARE)   curr_btn_mask |= (1 << 2);
    if (paddata.BTN_TRIANGLE) curr_btn_mask |= (1 << 3);
    if (paddata.BTN_START)    curr_btn_mask |= (1 << 4);
    if (paddata.BTN_SELECT)   curr_btn_mask |= (1 << 5);
    if (paddata.BTN_UP)       curr_btn_mask |= (1 << 6);
    if (paddata.BTN_DOWN)     curr_btn_mask |= (1 << 7);
    if (paddata.BTN_LEFT)     curr_btn_mask |= (1 << 8);
    if (paddata.BTN_RIGHT)    curr_btn_mask |= (1 << 9);
    if (paddata.BTN_L1)       curr_btn_mask |= (1 << 10);
    if (paddata.BTN_R1)       curr_btn_mask |= (1 << 11);
    if (paddata.BTN_L2)       curr_btn_mask |= (1 << 12);
    if (paddata.BTN_R2)       curr_btn_mask |= (1 << 13);

    uint32_t pressed_mask = curr_btn_mask & (~s_prev_btn_mask);

    out_input->cross_pressed    = (pressed_mask & (1 << 0)) != 0;
    out_input->circle_pressed   = (pressed_mask & (1 << 1)) != 0;
    out_input->square_pressed   = (pressed_mask & (1 << 2)) != 0;
    out_input->triangle_pressed = (pressed_mask & (1 << 3)) != 0;
    out_input->start_pressed    = (pressed_mask & (1 << 4)) != 0;
    out_input->select_pressed   = (pressed_mask & (1 << 5)) != 0;
    out_input->up_pressed       = (pressed_mask & (1 << 6)) != 0;
    out_input->down_pressed     = (pressed_mask & (1 << 7)) != 0;
    out_input->left_pressed     = (pressed_mask & (1 << 8)) != 0;
    out_input->right_pressed    = (pressed_mask & (1 << 9)) != 0;
    out_input->l1_pressed       = (pressed_mask & (1 << 10)) != 0;
    out_input->r1_pressed       = (pressed_mask & (1 << 11)) != 0;

    out_input->l2_held = (curr_btn_mask & (1 << 12)) != 0;
    out_input->r2_held = (curr_btn_mask & (1 << 13)) != 0;
    out_input->up_held = (curr_btn_mask & (1 << 6)) != 0;
    out_input->down_held = (curr_btn_mask & (1 << 7)) != 0;

    s_prev_btn_mask = curr_btn_mask;
#else
    /* Mock / testing input: nothing held by default */
    (void)s_prev_btn_mask;
#endif
}
