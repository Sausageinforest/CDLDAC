#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/time.h>

#include <sys/process.h>
#include <net/net.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <io/pad.h>

#include "cdrom.h"

#define DEFAULT_IP          "192.168.1.50"
#define DEFAULT_PORT        12345

#define UDP_CHUNK_SIZE      1176
#define SECTOR_TIME_USEC    13333

static uint8_t s_read_ahead_buffer[READ_AHEAD_BUFFER_SIZE] __attribute__((aligned(128)));
static char s_target_ip[64] = DEFAULT_IP;
static int  s_target_port   = DEFAULT_PORT;

static inline uint64_t get_time_usec(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + tv.tv_usec;
}

static void load_configuration(void) {
    const char *paths[] = {
        "/dev_hdd0/game/CDLDAC001/USRDIR/config.txt",
        "/dev_usb000/config.txt",
        "/dev_usb001/config.txt"
    };

    FILE *f = NULL;
    for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
        f = fopen(paths[i], "r");
        if (f) break;
    }
    if (!f) return;

    char line[128];
    if (fgets(line, sizeof(line), f)) {
        char ip_buf[64] = {0};
        int port = 0;
        if (sscanf(line, "%63[^:]:%d", ip_buf, &port) == 2 ||
            sscanf(line, "%63s %d", ip_buf, &port) == 2) {
            strncpy(s_target_ip, ip_buf, sizeof(s_target_ip) - 1);
            s_target_port = port;
        }
    }
    fclose(f);
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    load_configuration();
    netInitialize();

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return -1;

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family      = AF_INET;
    dest_addr.sin_port        = htons(s_target_port);
    dest_addr.sin_addr.s_addr = inet_addr(s_target_ip);

    ioPadInit(7);

    cd_toc_t toc;
    bool has_disc = false;

    if (cdrom_init() == 0) {
        if (cdrom_read_toc(&toc) == 0 && toc.total_tracks > 0) {
            has_disc = true;
        }
    }

    int current_track_idx = 0;
    uint32_t current_lba  = has_disc ? toc.tracks[0].start_lba : 0;
    bool is_paused        = false;
    bool running          = true;

    uint32_t prev_buttons = 0;
    uint32_t buffered_lba_start = 0;
    uint32_t buffered_count     = 0;
    uint64_t next_send_time     = get_time_usec();

    printf("PS3 CD-DA Streamer started: %s:%d\n", s_target_ip, s_target_port);

    while (running) {
        padInfo padinfo;
        padData paddata;
        ioPadGetInfo(&padinfo);

        if (padinfo.status[0]) {
            ioPadGetData(0, &paddata);
            uint32_t cur_buttons = (paddata.button[2] << 8) | paddata.button[3];
            uint32_t pressed = cur_buttons & (~prev_buttons);
            prev_buttons = cur_buttons;

            if (pressed & (PAD_CTRL_CIRCLE << 0)) {
                running = false;
                break;
            }

            if ((pressed & (PAD_CTRL_CROSS << 0)) || (pressed & (PAD_CTRL_START << 8))) {
                is_paused = !is_paused;
                next_send_time = get_time_usec();
            }

            if (pressed & (PAD_CTRL_TRIANGLE << 0)) {
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

            if (has_disc && ((pressed & (PAD_CTRL_RIGHT << 8)) || (pressed & (PAD_CTRL_R1 << 0)))) {
                if (current_track_idx + 1 < toc.total_tracks) {
                    current_track_idx++;
                    current_lba = toc.tracks[current_track_idx].start_lba;
                    buffered_count = 0;
                    next_send_time = get_time_usec();
                }
            }

            if (has_disc && ((pressed & (PAD_CTRL_LEFT << 8)) || (pressed & (PAD_CTRL_L1 << 0)))) {
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

    cdrom_close();
    close(sock);
    netDeinitialize();
    sysProcessExit(0);
    return 0;
}
