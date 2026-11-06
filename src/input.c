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

    /* Convert pad buttons: bit 0..15 in paddata.button[2] and [3] */
    uint32_t curr_btn_mask = ((uint32_t)paddata.button[2] << 8) | paddata.button[3];
    uint32_t pressed_mask = curr_btn_mask & (~s_prev_btn_mask);

    out_input->cross_pressed    = (pressed_mask & PAD_CROSS) != 0;
    out_input->circle_pressed   = (pressed_mask & PAD_CIRCLE) != 0;
    out_input->square_pressed   = (pressed_mask & PAD_SQUARE) != 0;
    out_input->triangle_pressed = (pressed_mask & PAD_TRIANGLE) != 0;
    out_input->start_pressed    = (pressed_mask & PAD_START) != 0;
    out_input->select_pressed   = (pressed_mask & PAD_SELECT) != 0;
    out_input->up_pressed       = (pressed_mask & PAD_UP) != 0;
    out_input->down_pressed     = (pressed_mask & PAD_DOWN) != 0;
    out_input->left_pressed     = (pressed_mask & PAD_LEFT) != 0;
    out_input->right_pressed    = (pressed_mask & PAD_RIGHT) != 0;
    out_input->l1_pressed       = (pressed_mask & PAD_L1) != 0;
    out_input->r1_pressed       = (pressed_mask & PAD_R1) != 0;

    out_input->l2_held = (curr_btn_mask & PAD_L2) != 0;
    out_input->r2_held = (curr_btn_mask & PAD_R2) != 0;
    out_input->up_held = (curr_btn_mask & PAD_UP) != 0;
    out_input->down_held = (curr_btn_mask & PAD_DOWN) != 0;

    out_input->left_stick_x  = paddata.button[4] - 128;
    out_input->left_stick_y  = paddata.button[5] - 128;
    out_input->right_stick_x = paddata.button[6] - 128;
    out_input->right_stick_y = paddata.button[7] - 128;

    s_prev_btn_mask = curr_btn_mask;
#else
    /* Mock / testing input: nothing held by default */
    (void)s_prev_btn_mask;
#endif
}
