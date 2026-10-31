#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/time.h>

#include <sysmodule/sysmodule.h>
#include <net/net.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <io/pad.h>

#include "cdrom.h"

extern void sysProcessExit(int status);

#define DEFAULT_IP          "192.168.1.50"
#define DEFAULT_PORT        12345
#define UDP_CHUNK_SIZE      1176
#define SECTOR_TIME_USEC    13333

static uint8_t s_read_ahead_buffer[READ_AHEAD_BUFFER_SIZE] __attribute__((aligned(128)));
static char s_target_ip[64] = DEFAULT_IP;
static int  s_target_port   = DEFAULT_PORT;
static FILE *s_log_file     = NULL;

static void log_write(const char *fmt, ...) {
    if (!s_log_file) {
        s_log_file = fopen("/dev_usb000/cdldac.log", "w");
        if (!s_log_file) s_log_file = fopen("/dev_usb001/cdldac.log", "w");
        if (!s_log_file) s_log_file = fopen("/dev_hdd0/cdldac.log", "w");
    }
    if (s_log_file) {
        va_list args;
        va_start(args, fmt);
        vfprintf(s_log_file, fmt, args);
        va_end(args);
        fflush(s_log_file);
    }
}

static inline uint64_t get_time_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static void load_configuration(void) {
    const char *paths[] = {
        "/dev_usb000/config.txt",
        "/dev_usb001/config.txt",
        "/dev_hdd0/game/CDLDAC001/USRDIR/config.txt"
    };

    FILE *f = NULL;
    for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
        f = fopen(paths[i], "r");
        if (f) {
            log_write("[CONFIG] Found file at %s\n", paths[i]);
            break;
        }
    }
    if (!f) {
        log_write("[CONFIG] Not found, using default %s:%d\n", s_target_ip, s_target_port);
        return;
    }

    char line[128];
    if (fgets(line, sizeof(line), f)) {
        char ip_buf[64] = {0};
        int port = 0;
        if (sscanf(line, "IP=%63[^:]:%d", ip_buf, &port) == 2 ||
            sscanf(line, "%63[^:]:%d", ip_buf, &port) == 2 ||
            sscanf(line, "%63s %d", ip_buf, &port) == 2) {
            strncpy(s_target_ip, ip_buf, sizeof(s_target_ip) - 1);
            s_target_port = port;
        }
    }
    fclose(f);
    log_write("[CONFIG] Loaded target %s:%d\n", s_target_ip, s_target_port);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    log_write("=== PS3 CD-DA Streamer Starting ===\n");

    /* Загрузка системных модулей сети и ввода */
    sysModuleLoad(SYSMODULE_NET);
    sysModuleLoad(SYSMODULE_IO);

    load_configuration();

    /* Сеть */
    int net_init_res = netInitialize();
    log_write("[NET] netInitialize returned: %d\n", net_init_res);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    log_write("[NET] socket created: %d\n", sock);
    if (sock < 0) {
        log_write("[FATAL] socket creation failed!\n");
        sleep(5);
        return -1;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family      = AF_INET;
    dest_addr.sin_port        = htons(s_target_port);
    dest_addr.sin_addr.s_addr = inet_addr(s_target_ip);

    /* Геймпад */
    int pad_res = ioPadInit(7);
    log_write("[PAD] ioPadInit returned: %d\n", pad_res);

    /* Привод Blu-ray / CD */
    log_write("[CD] Initializing BDVD storage device (0x%llx)...\n", (unsigned long long)BDVD_DEV_ID);
    int cd_res = cdrom_init();
    log_write("[CD] cdrom_init returned: %d\n", cd_res);

    cd_toc_t toc;
    memset(&toc, 0, sizeof(toc));
    bool has_disc = false;

    if (cd_res == 0) {
        int toc_res = cdrom_read_toc(&toc);
        log_write("[CD] cdrom_read_toc returned: %d, total tracks: %d\n", toc_res, (int)toc.total_tracks);
        if (toc_res == 0 && toc.total_tracks > 0) {
            has_disc = true;
            log_write("[CD] Audio CD ready: %d tracks, first LBA: %u, leadout LBA: %u\n",
                      toc.total_tracks, toc.tracks[0].start_lba, toc.leadout_lba);
        }
    } else {
        log_write("[CD] WARNING: Could not open BDVD drive!\n");
    }

    int current_track_idx = 0;
    uint32_t current_lba  = has_disc ? toc.tracks[0].start_lba : 0;
    bool is_paused        = false;
    bool running          = true;

    uint32_t prev_buttons = 0;
    uint32_t buffered_lba_start = 0;
    uint32_t buffered_count     = 0;
    uint64_t next_send_time     = get_time_usec();
    uint64_t start_time         = next_send_time;
    uint32_t sectors_sent       = 0;

    log_write("[LOOP] Entering streaming loop...\n");

    while (running) {
        padInfo padinfo;
        memset(&padinfo, 0, sizeof(padinfo));
        ioPadGetInfo(&padinfo);

        if (padinfo.status[0]) {
            padData paddata;
            memset(&paddata, 0, sizeof(paddata));
            ioPadGetData(0, &paddata);

            uint32_t cur_buttons = 0;
            if (paddata.BTN_CROSS)    cur_buttons |= (1 << 0);
            if (paddata.BTN_START)    cur_buttons |= (1 << 1);
            if (paddata.BTN_CIRCLE)   cur_buttons |= (1 << 2);
            if (paddata.BTN_TRIANGLE) cur_buttons |= (1 << 3);
            if (paddata.BTN_RIGHT)    cur_buttons |= (1 << 4);
            if (paddata.BTN_R1)       cur_buttons |= (1 << 5);
            if (paddata.BTN_LEFT)     cur_buttons |= (1 << 6);
            if (paddata.BTN_L1)       cur_buttons |= (1 << 7);

            uint32_t pressed = cur_buttons & (~prev_buttons);
            prev_buttons = cur_buttons;

            /* Защита от случайного выхода в первые 3 секунды */
            if ((get_time_usec() - start_time) > 3000000ULL) {
                if (pressed & (1 << 2)) {
                    log_write("[PAD] Circle pressed: exiting to XMB...\n");
                    running = false;
                    break;
                }
            }

            if (pressed & ((1 << 0) | (1 << 1))) {
                is_paused = !is_paused;
                log_write("[PLAY] Play/Pause toggled: %s\n", is_paused ? "PAUSED" : "PLAYING");
                next_send_time = get_time_usec();
            }

            if (pressed & (1 << 3)) {
                log_write("[CD] Reloading TOC...\n");
                cdrom_close();
                if (cdrom_init() == 0 && cdrom_read_toc(&toc) == 0 && toc.total_tracks > 0) {
                    has_disc = true;
                    current_track_idx = 0;
                    current_lba = toc.tracks[0].start_lba;
                    buffered_count = 0;
                } else {
                    has_disc = false;
                    buffered_count = 0;
                }
            }

            if (has_disc && (pressed & ((1 << 4) | (1 << 5)))) {
                if (current_track_idx + 1 < toc.total_tracks) {
                    current_track_idx++;
                    current_lba = toc.tracks[current_track_idx].start_lba;
                    buffered_count = 0;
                    next_send_time = get_time_usec();
                }
            }

            if (has_disc && (pressed & ((1 << 6) | (1 << 7)))) {
                uint32_t elapsed_in_track = current_lba - toc.tracks[current_track_idx].start_lba;
                if (elapsed_in_track > (2 * 75) || current_track_idx == 0) {
                    current_lba = toc.tracks[current_track_idx].start_lba;
                } else {
                    current_track_idx--;
                    current_lba = toc.tracks[current_track_idx].start_lba;
                }
                buffered_count = 0;
                next_send_time = get_time_usec();
            }
        }

        if (has_disc && !is_paused) {
            uint32_t track_end_lba = (current_track_idx == toc.total_tracks - 1)
                                     ? toc.leadout_lba
                                     : toc.tracks[current_track_idx + 1].start_lba;

            if (current_lba >= track_end_lba) {
                if (current_track_idx + 1 < toc.total_tracks) {
                    current_track_idx++;
                    current_lba = toc.tracks[current_track_idx].start_lba;
                    buffered_count = 0;
                } else {
                    current_track_idx = 0;
                    current_lba = toc.tracks[0].start_lba;
                    is_paused = true;
                    buffered_count = 0;
                    continue;
                }
            }

            if (buffered_count == 0 ||
                current_lba < buffered_lba_start ||
                current_lba >= (buffered_lba_start + buffered_count)) {

                uint32_t sectors_to_read = READ_AHEAD_SECTORS;
                if (current_lba + sectors_to_read > toc.leadout_lba) {
                    sectors_to_read = toc.leadout_lba - current_lba;
                }

                if (sectors_to_read > 0) {
                    int read_res = cdrom_read_sectors(current_lba, sectors_to_read, s_read_ahead_buffer);
                    if (read_res == 0) {
                        buffered_lba_start = current_lba;
                        buffered_count     = sectors_to_read;
                    } else {
                        log_write("[CD] Read error at LBA %u (code: %d)\n", current_lba, read_res);
                        is_paused = true;
                        usleep(100000);
                        continue;
                    }
                }
            }

            uint32_t sector_offset = (current_lba - buffered_lba_start) * CDDA_SECTOR_SIZE;
            uint8_t *sector_ptr    = &s_read_ahead_buffer[sector_offset];

            sendto(sock, sector_ptr, UDP_CHUNK_SIZE, 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr));
            sendto(sock, sector_ptr + UDP_CHUNK_SIZE, UDP_CHUNK_SIZE, 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr));

            current_lba++;
            sectors_sent++;

            if (sectors_sent % 300 == 0) {
                log_write("[STREAM] Sent %u sectors (~%u sec of audio)\n", sectors_sent, sectors_sent / 75);
            }

            next_send_time += SECTOR_TIME_USEC;
            uint64_t now = get_time_usec();

            if (next_send_time > now) {
                usleep(next_send_time - now);
            } else if (now > next_send_time + 100000) {
                next_send_time = now;
            }
        } else {
            usleep(16000);
        }
    }

    log_write("[EXIT] Shutting down...\n");
    cdrom_close();
    close(sock);
    netDeinitialize();
    if (s_log_file) fclose(s_log_file);
    sysProcessExit(0);
    return 0;
}
