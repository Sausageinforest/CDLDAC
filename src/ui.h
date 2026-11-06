#ifndef UI_H
#define UI_H

#include "metadata.h"
#include "audio_engine.h"
#include "visualizer.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    UI_MODE_STANDARD = 0,  /* Cover + Tracklist + Visualizer */
    UI_MODE_FOCUS          /* Large Cover + Fullscreen Visualizer */
} ui_screen_mode_t;

typedef struct {
    int screen_width;
    int screen_height;
    float scale_x;
    float scale_y;
    ui_screen_mode_t mode;
    int selected_track_index;
    int scroll_offset;
    bool is_720p;
    uint32_t current_cover_texture_id;
    uint32_t default_cover_texture_id;
} ui_state_t;

/* Initialize UI subsystem, load fonts and default cover */
int ui_init(void);

/* Shutdown UI subsystem and free resources */
void ui_shutdown(void);

/* Toggle between Standard and Focus screen modes */
void ui_toggle_mode(void);

/* Navigate tracklist cursor */
void ui_cursor_up(int track_count);
void ui_cursor_down(int track_count);
int ui_get_selected_track(void);
void ui_set_selected_track(int track);

/* Update cover art from memory buffer (JPEG or PNG) */
int ui_update_cover_art(const uint8_t *img_bytes, int img_size);

/* Render complete frame */
void ui_render(const album_metadata_t *meta,
               const audio_status_info_t *audio_status,
               const visualizer_t *vis,
               const char *status_message);

#endif /* UI_H */
