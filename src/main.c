#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include "cd_drive.h"
#include "discid.h"
#include "metadata.h"
#include "audio_engine.h"
#include "visualizer.h"
#include "input.h"
#include "ui.h"

#if defined(__PS3__) || defined(__PPU__)
#include <sys/process.h>
#endif

static cd_toc_t g_toc;
static char g_disc_id[64] = {0};
static char g_freedb_id[16] = {0};
static album_metadata_t g_meta;
static visualizer_t g_vis;
static bool g_has_disc = false;
static char g_status_msg[128] = "Initializing PS3 CD Player...";

static void load_current_disc(void) {
    if (cd_drive_read_toc(&g_toc) != 0) {
        snprintf(g_status_msg, sizeof(g_status_msg), "Error reading TOC");
        g_has_disc = false;
        return;
    }

    g_has_disc = true;
    discid_calculate_musicbrainz(&g_toc, g_disc_id, sizeof(g_disc_id));
    discid_calculate_freedb(&g_toc, g_freedb_id, sizeof(g_freedb_id));

    printf("[MAIN] MusicBrainz DiscID: %s\n", g_disc_id);
    printf("[MAIN] FreeDB DiscID: %s\n", g_freedb_id);

    audio_engine_set_toc(&g_toc);

    snprintf(g_status_msg, sizeof(g_status_msg), "Fetching metadata: %s...", g_disc_id);
    metadata_fetch_disc(&g_toc, g_disc_id, &g_meta);

    if (g_meta.cover_loaded && g_meta.cover_image_bytes) {
        ui_update_cover_art(g_meta.cover_image_bytes, g_meta.cover_image_size);
    }

    snprintf(g_status_msg, sizeof(g_status_msg), "%s - %s", g_meta.artist_name, g_meta.album_title);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("===================================================\n");
    printf("   PS3 AUDIOPHILE CD PLAYER (TitleID: CDPL00001)   \n");
    printf("===================================================\n");

    /* 1. Subsystem Initialization */
    input_init();
    metadata_init();
    visualizer_init(&g_vis);
    ui_init();

    if (cd_drive_init() == 0) {
        printf("[MAIN] CD Drive initialized\n");
    }

    audio_engine_init(NULL);

    /* 2. Initial Disc Detection */
    cd_drive_status_t cd_st = cd_drive_poll_status();
    if (cd_st == CD_STATUS_READY) {
        load_current_disc();
        /* Auto-play track 1 on launch */
        audio_play_track(1);
        ui_set_selected_track(1);
    } else {
        snprintf(g_status_msg, sizeof(g_status_msg), "Please insert an Audio CD");
        metadata_set_fallback(NULL, NULL, &g_meta);
    }

    /* 3. Main Application Loop */
    bool app_running = true;
    controller_input_t input;
    audio_status_info_t audio_status;
    float vis_samples[VIS_FFT_SIZE];

    int frame_counter = 0;

    while (app_running) {
        /* Poll controller */
        input_poll(&input);

        /* Handle Controller Commands */
        if (input.start_pressed) {
            audio_toggle_pause();
        }

        if (input.cross_pressed) {
            int sel = ui_get_selected_track();
            audio_get_status(&audio_status);
            if (sel != audio_status.current_track || audio_status.state != AUDIO_STATE_PLAYING) {
                audio_play_track(sel);
            } else {
                audio_toggle_pause();
            }
        }

        if (input.up_pressed) {
            ui_cursor_up(g_meta.track_count);
        } else if (input.down_pressed) {
            ui_cursor_down(g_meta.track_count);
        }

        if (input.l1_pressed) {
            audio_get_status(&audio_status);
            if (audio_status.current_track > g_toc.first_track) {
                int prev = audio_status.current_track - 1;
                audio_play_track(prev);
                ui_set_selected_track(prev);
            }
        }

        if (input.r1_pressed) {
            audio_get_status(&audio_status);
            if (audio_status.current_track < g_toc.last_track) {
                int next = audio_status.current_track + 1;
                audio_play_track(next);
                ui_set_selected_track(next);
            }
        }

        /* Smooth Rewind / Fast-Forward */
        if (input.l2_held) {
            audio_seek(-2);
        } else if (input.r2_held) {
            audio_seek(+2);
        }

        /* Toggle Display Mode */
        if (input.triangle_pressed) {
            ui_toggle_mode();
        }

        /* Force Metadata Refresh */
        if (input.select_pressed && g_has_disc) {
            snprintf(g_status_msg, sizeof(g_status_msg), "Refreshing metadata...");
            metadata_fetch_disc(&g_toc, g_disc_id, &g_meta);
            if (g_meta.cover_loaded && g_meta.cover_image_bytes) {
                ui_update_cover_art(g_meta.cover_image_bytes, g_meta.cover_image_size);
            }
            snprintf(g_status_msg, sizeof(g_status_msg), "Metadata updated: %s", g_meta.album_title);
        }

        /* 4. Drive State Monitoring (Disc Inserted / Removed) */
        frame_counter++;
        if (frame_counter % 30 == 0) {
            cd_drive_status_t cur_drive_st = cd_drive_poll_status();
            if (cur_drive_st == CD_STATUS_READY && !g_has_disc) {
                /* New disc inserted */
                printf("[MAIN] Disc inserted\n");
                load_current_disc();
                audio_play_track(1);
                ui_set_selected_track(1);
            } else if (cur_drive_st != CD_STATUS_READY && g_has_disc) {
                /* Disc removed */
                printf("[MAIN] Disc removed\n");
                audio_stop();
                visualizer_reset(&g_vis);
                metadata_cleanup(&g_meta);
                metadata_set_fallback(NULL, NULL, &g_meta);
                g_has_disc = false;
                snprintf(g_status_msg, sizeof(g_status_msg), "Disc removed");
            }
        }

        /* 5. Update Audio Status & Visualizer */
        audio_get_status(&audio_status);
        if (audio_status.state == AUDIO_STATE_PLAYING) {
            audio_get_vis_samples(vis_samples, VIS_FFT_SIZE);
            visualizer_update(&g_vis, vis_samples, VIS_FFT_SIZE);
        } else {
            /* Decay bars to zero */
            memset(vis_samples, 0, sizeof(vis_samples));
            visualizer_update(&g_vis, vis_samples, VIS_FFT_SIZE);
        }

        /* 6. Render UI Frame */
        ui_render(&g_meta, &audio_status, &g_vis, g_status_msg);

        /* Frame pacing (60 FPS ~16.6ms) */
        usleep(16666);
    }

    /* 7. Graceful Termination */
    audio_engine_shutdown();
    cd_drive_shutdown();
    metadata_cleanup(&g_meta);
    metadata_shutdown();
    ui_shutdown();
    input_shutdown();

#if defined(__PS3__) || defined(__PPU__)
    sysProcessExit(0);
#endif

    return 0;
}
