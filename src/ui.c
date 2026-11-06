#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if defined(__PS3__) || defined(__PPU__)
#if defined(__has_include)
#if __has_include(<tiny3d.h>)
#include <tiny3d.h>
#endif
#endif

#ifndef TINY3D_QUADS
#define TINY3D_QUADS 3
#endif

#ifndef TINY3D_CLEAR_ALL
#define TINY3D_CLEAR_ALL 3
#endif

void tiny3d_Init(uint32_t flags) __attribute__((weak));
void tiny3d_Clear(uint32_t rgba, uint32_t flags) __attribute__((weak));
void tiny3d_Flip(void) __attribute__((weak));
void tiny3d_Polygon(int type) __attribute__((weak));
void tiny3d_End(void) __attribute__((weak));
void tiny3d_VertexPos(float x, float y, float z) __attribute__((weak));
void tiny3d_SetPolygonColor(uint32_t rgba) __attribute__((weak));

static int s_ps3_gfx_ready = 0;
#endif

static ui_state_t s_ui_state;
static uint8_t *s_default_cover_pixels = NULL;
static int s_default_cover_w = 0;
static int s_default_cover_h = 0;

static uint8_t *s_active_cover_pixels = NULL;
static int s_active_cover_w = 0;
static int s_active_cover_h = 0;

static void format_time(uint32_t ms, char *buf, size_t buf_sz) {
    uint32_t total_sec = ms / 1000;
    uint32_t minutes = total_sec / 60;
    uint32_t seconds = total_sec % 60;
    snprintf(buf, buf_sz, "%02u:%02u", minutes, seconds);
}

int ui_init(void) {
    memset(&s_ui_state, 0, sizeof(s_ui_state));
    s_ui_state.screen_width = 1920;
    s_ui_state.screen_height = 1080;
    s_ui_state.scale_x = 1.0f;
    s_ui_state.scale_y = 1.0f;
    s_ui_state.mode = UI_MODE_STANDARD;
    s_ui_state.selected_track_index = 1;
    s_ui_state.scroll_offset = 0;
    s_ui_state.is_720p = false;

#if defined(__PS3__) || defined(__PPU__)
    if (tiny3d_Init) {
        tiny3d_Init(1024 * 1024);
        s_ps3_gfx_ready = 1;
    }
#endif

    /* Load default cover image */
    int comp = 0;
    const char *cover_file = "/dev_hdd0/game/CDPL00001/USRDIR/assets/default_cover.png";
    FILE *f = fopen(cover_file, "rb");
    if (!f) {
        cover_file = "./assets/default_cover.png";
    } else {
        fclose(f);
    }

    s_default_cover_pixels = stbi_load(cover_file, &s_default_cover_w, &s_default_cover_h, &comp, 4);
    if (s_default_cover_pixels) {
        printf("[UI] Loaded default cover: %dx%d\n", s_default_cover_w, s_default_cover_h);
    } else {
        printf("[UI] Warning: Default cover not found at %s\n", cover_file);
    }

    return 0;
}

void ui_shutdown(void) {
    if (s_default_cover_pixels) {
        stbi_image_free(s_default_cover_pixels);
        s_default_cover_pixels = NULL;
    }
    if (s_active_cover_pixels) {
        stbi_image_free(s_active_cover_pixels);
        s_active_cover_pixels = NULL;
    }

#if defined(__PS3__) || defined(__PPU__)
    if (s_ps3_gfx_ready) {
        /* Release Tiny3D resources */
        s_ps3_gfx_ready = 0;
    }
#endif
}

void ui_toggle_mode(void) {
    if (s_ui_state.mode == UI_MODE_STANDARD) {
        s_ui_state.mode = UI_MODE_FOCUS;
    } else {
        s_ui_state.mode = UI_MODE_STANDARD;
    }
}

void ui_cursor_up(int track_count) {
    if (track_count <= 0) return;
    if (s_ui_state.selected_track_index > 1) {
        s_ui_state.selected_track_index--;
        if (s_ui_state.selected_track_index <= s_ui_state.scroll_offset) {
            s_ui_state.scroll_offset = s_ui_state.selected_track_index - 1;
        }
    }
}

void ui_cursor_down(int track_count) {
    if (track_count <= 0) return;
    if (s_ui_state.selected_track_index < track_count) {
        s_ui_state.selected_track_index++;
        if (s_ui_state.selected_track_index > s_ui_state.scroll_offset + 9) {
            s_ui_state.scroll_offset = s_ui_state.selected_track_index - 9;
        }
    }
}

int ui_get_selected_track(void) {
    return s_ui_state.selected_track_index;
}

void ui_set_selected_track(int track) {
    s_ui_state.selected_track_index = track;
}

int ui_update_cover_art(const uint8_t *img_bytes, int img_size) {
    if (!img_bytes || img_size <= 0) return -1;

    if (s_active_cover_pixels) {
        stbi_image_free(s_active_cover_pixels);
        s_active_cover_pixels = NULL;
    }

    int comp = 0;
    s_active_cover_pixels = stbi_load_from_memory(img_bytes, img_size, &s_active_cover_w, &s_active_cover_h, &comp, 4);
    if (s_active_cover_pixels) {
        printf("[UI] Active cover decoded successfully: %dx%d\n", s_active_cover_w, s_active_cover_h);
        return 0;
    }
    printf("[UI] Failed to decode cover image bytes\n");
    return -1;
}

/* Drawing primitives for Tiny3D / RSX hardware acceleration */
#if defined(__PS3__) || defined(__PPU__)

static void draw_rect(float x, float y, float w, float h, uint32_t color) {
    if (!tiny3d_Polygon || !tiny3d_SetPolygonColor || !tiny3d_VertexPos || !tiny3d_End) return;
    tiny3d_Polygon(TINY3D_QUADS);
    tiny3d_SetPolygonColor(color);
    tiny3d_VertexPos(x, y, 65535);
    tiny3d_VertexPos(x + w, y, 65535);
    tiny3d_VertexPos(x + w, y + h, 65535);
    tiny3d_VertexPos(x, y + h, 65535);
    tiny3d_End();
}

static void draw_rect_gradient(float x, float y, float w, float h, uint32_t col_top, uint32_t col_bot) {
    if (!tiny3d_Polygon || !tiny3d_SetPolygonColor || !tiny3d_VertexPos || !tiny3d_End) return;
    tiny3d_Polygon(TINY3D_QUADS);
    tiny3d_SetPolygonColor(col_top);
    tiny3d_VertexPos(x, y, 65535);
    tiny3d_VertexPos(x + w, y, 65535);
    tiny3d_SetPolygonColor(col_bot);
    tiny3d_VertexPos(x + w, y + h, 65535);
    tiny3d_VertexPos(x, y + h, 65535);
    tiny3d_End();
}

#endif

void ui_render(const album_metadata_t *meta,
               const audio_status_info_t *audio_status,
               const visualizer_t *vis,
               const char *status_message) {

    char cur_time_str[16] = "00:00";
    char tot_time_str[16] = "00:00";
    float progress = 0.0f;

    if (audio_status && audio_status->track_total_ms > 0) {
        format_time(audio_status->track_elapsed_ms, cur_time_str, sizeof(cur_time_str));
        format_time(audio_status->track_total_ms, tot_time_str, sizeof(tot_time_str));
        progress = (float)audio_status->track_elapsed_ms / (float)audio_status->track_total_ms;
        if (progress > 1.0f) progress = 1.0f;
    }

#if defined(__PS3__) || defined(__PPU__)
    if (!s_ps3_gfx_ready || !tiny3d_Clear) return;

    tiny3d_Clear(0xFF0D1117, TINY3D_CLEAR_ALL); /* Audiophile deep dark slate */

    float sx = s_ui_state.scale_x;
    float sy = s_ui_state.scale_y;

    /* Header Bar */
    draw_rect_gradient(0, 0, 1920 * sx, 70 * sy, 0xFF161B22, 0xFF0D1117);

    /* Render Status Message */
    if (status_message && strlen(status_message) > 0) {
        draw_rect(60 * sx, 20 * sy, 400 * sx, 30 * sy, 0x88238636);
    }

    if (s_ui_state.mode == UI_MODE_STANDARD) {
        /* Standard Layout: Left Cover + Right Tracklist + Bottom Visualizer */

        /* 1. Left Cover Box (X: 100, Y: 110, W: 500, H: 500) */
        draw_rect(96 * sx, 106 * sy, 508 * sx, 508 * sy, 0xFF30363D); /* Outer border */
        draw_rect(100 * sx, 110 * sy, 500 * sx, 500 * sy, 0xFF161B22); /* Inner background */

        /* Under Cover: Time and Playback State */
        draw_rect(100 * sx, 630 * sy, 500 * sx, 40 * sy, 0xFF21262D);

        /* 2. Right Tracklist Panel (X: 660, Y: 110, W: 1160, H: 560) */
        draw_rect(660 * sx, 110 * sy, 1160 * sx, 560 * sy, 0xCC161B22);

        /* Render up to 10 visible tracks */
        int start_t = s_ui_state.scroll_offset + 1;
        int max_t = meta ? meta->track_count : 0;

        for (int i = 0; i < 9 && (start_t + i) <= max_t; i++) {
            int trk_idx = start_t + i;
            float row_y = (240 + i * 44) * sy;

            if (trk_idx == s_ui_state.selected_track_index) {
                /* Cursor highlight */
                draw_rect(680 * sx, row_y, 1120 * sx, 38 * sy, 0xFF1F6FEB);
            } else if (audio_status && trk_idx == audio_status->current_track) {
                /* Active playing track */
                draw_rect(680 * sx, row_y, 1120 * sx, 38 * sy, 0xFF238636);
            }
        }

        /* 3. Bottom Progress Bar (X: 100, Y: 700, W: 1720, H: 10) */
        draw_rect(100 * sx, 700 * sy, 1720 * sx, 10 * sy, 0xFF30363D);
        if (progress > 0.0f) {
            draw_rect(100 * sx, 700 * sy, 1720 * sx * progress, 10 * sy, 0xFF58A6FF);
        }

        /* 4. Bottom FFT Spectrum Visualizer (X: 100, Y: 740, W: 1720, H: 260) */
        draw_rect(100 * sx, 740 * sy, 1720 * sx, 260 * sy, 0xAA0B0E14);

        if (vis) {
            float bar_w = 40.0f * sx;
            float bar_gap = 13.5f * sx;
            float base_y = 990.0f * sy;
            float max_bar_h = 230.0f * sy;

            for (int b = 0; b < VIS_NUM_BANDS; b++) {
                float bx = (115.0f * sx) + b * (bar_w + bar_gap);
                float val = vis->band_values[b];
                float bh = val * max_bar_h;
                if (bh < 2.0f) bh = 2.0f;

                /* Vertical gradient bar: Cyan -> Magenta */
                uint32_t top_col = 0xFFBC8CFF;
                uint32_t bot_col = 0xFF39D353;
                draw_rect_gradient(bx, base_y - bh, bar_w, bh, top_col, bot_col);

                /* Floating peak indicator cap */
                float peak_val = vis->peak_values[b];
                float peak_y = base_y - (peak_val * max_bar_h) - (4.0f * sy);
                if (peak_y < (base_y - max_bar_h)) peak_y = base_y - max_bar_h;
                draw_rect(bx, peak_y, bar_w, 4.0f * sy, 0xFFFFFFFF);
            }
        }
    } else {
        /* Focus Mode: Centered Large Cover + Full-Width Visualizer */
        float cx = (1920.0f * sx - 540.0f * sx) / 2.0f;
        draw_rect(cx - 4.0f, 96.0f * sy, 548.0f * sx, 548.0f * sy, 0xFF30363D);
        draw_rect(cx, 100.0f * sy, 540.0f * sx, 540.0f * sy, 0xFF161B22);

        /* Fullscreen Spectrum Analyzer Bars */
        if (vis) {
            float bar_w = 42.0f * sx;
            float bar_gap = 11.5f * sx;
            float base_y = 1030.0f * sy;
            float max_bar_h = 320.0f * sy;

            for (int b = 0; b < VIS_NUM_BANDS; b++) {
                float bx = (70.0f * sx) + b * (bar_w + bar_gap);
                float val = vis->band_values[b];
                float bh = val * max_bar_h;
                if (bh < 2.0f) bh = 2.0f;

                draw_rect_gradient(bx, base_y - bh, bar_w, bh, 0xFFFF7B72, 0xFF79C0FF);

                float peak_val = vis->peak_values[b];
                float peak_y = base_y - (peak_val * max_bar_h) - (4.0f * sy);
                draw_rect(bx, peak_y, bar_w, 4.0f * sy, 0xFFFFFFFF);
            }
        }
    }

    if (tiny3d_Flip) tiny3d_Flip();
#else
    /* Non-PS3 terminal / console simulation output */
    static int s_frame_counter = 0;
    if (++s_frame_counter % 60 == 0) {
        printf("\r[UI 1080p] Track %d | %s / %s [%3d%%] | State: %d | Title: %s",
               audio_status ? audio_status->current_track : 0,
               cur_time_str, tot_time_str,
               (int)(progress * 100.0f),
               audio_status ? audio_status->state : 0,
               meta ? meta->album_title : "No Album");
        fflush(stdout);
    }
#endif
}
