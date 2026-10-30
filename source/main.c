#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>

#include <sys/process.h>
#include <sys/systime.h>
#include <net/net.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <io/pad.h>
#include <debug_font.h>

#include "cdrom.h"

#define DEFAULT_IP          "192.168.1.50"
#define DEFAULT_PORT        12345

#define UDP_CHUNK_SIZE      1176  /* 2352 / 2: кратно 4 байтам на сэмпл (16 бит * 2 канала) */
#define SECTOR_TIME_USEC    13333 /* 1000000 мкс / 75 секторов/с = 13333.33 мкс */

/* 128-байтно выровненный буфер упреждающего чтения (25 секторов) */
static uint8_t s_read_ahead_buffer[READ_AHEAD_BUFFER_SIZE] __attribute__((aligned(128)));

static char s_target_ip[64] = DEFAULT_IP;
static int  s_target_port   = DEFAULT_PORT;

/* Загрузка конфигурации из HDD или USB-накопителя */
static void load_configuration(void) {
    const char *paths[] = {
        "/dev_hdd0/game/CDLDAC001/USRDIR/config.txt",
        "/dev_usb000/config.txt",
        "/dev_usb001/config.txt"
    };

    FILE *f = NULL;
    for (size_t i = 0; i < sizeof(paths)/sizeof(paths[0]); i++) {
        f = fopen(paths[i], "r");
        if (f) {
            break;
        }
    }

    if (!f) {
        return;
    }

    char line[128];
    if (fgets(line, sizeof(line), f)) {
        char ip_buf[64] = {0};
        int port = 0;
        /* Поддержка форматов IP:PORT либо IP PORT */
        if (sscanf(line, "%63[^:]:%d", ip_buf, &port) == 2 ||
            sscanf(line, "%63s %d", ip_buf, &port) == 2) {
            strncpy(s_target_ip, ip_buf, sizeof(s_target_ip) - 1);
            s_target_port = port;
        }
    }
    fclose(f);
}

/* Отрисовка текстового дашборда на экран */
static void render_status(const char *drive_status,
                          int current_track,
                          int total_tracks,
                          uint32_t track_sec,
                          uint32_t track_total_sec,
                          bool is_paused) {
    DPRINTF("\033[2J\033[H"); /* Очистка экрана и сброс курсора */
    DPRINTF("=====================================================\n");
    DPRINTF("       PS3 Audio CD (CD-DA) -> LDAC UDP Streamer     \n");
    DPRINTF("=====================================================\n\n");
    DPRINTF(" Destination:     %s:%d\n", s_target_ip, s_target_port);
    DPRINTF(" Drive Status:    %s\n", drive_status);

    if (total_tracks > 0) {
        DPRINTF(" Track:           %d / %d\n", current_track, total_tracks);
        DPRINTF(" Track Time:      %02d:%02d / %02d:%02d\n",
                track_sec / 60, track_sec % 60,
                track_total_sec / 60, track_total_sec % 60);
        DPRINTF(" Playback State:  [%s]\n", is_paused ? "PAUSED" : "STREAMING");
    } else {
        DPRINTF(" Tracks:          No CD-DA tracks detected\n");
    }

    DPRINTF("\n-----------------------------------------------------\n");
    DPRINTF(" Controls:\n");
    DPRINTF("  [X] / [START]  - Play / Pause\n");
    DPRINTF("  [Right] / [R1] - Next Track\n");
    DPRINTF("  [Left]  / [L1] - Previous Track\n");
    DPRINTF("  [TRIANGLE]     - Rescan Disc / Reload TOC\n");
    DPRINTF("  [CIRCLE]       - Exit to XMB\n");
    DPRINTF("=====================================================\n");
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    init_debug_font();
    load_configuration();

    /* Инициализация сетевого стека */
    netInitialize();
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        DPRINTF("Error: Failed to create UDP socket!\n");
        sysUsleep(3000000);
        term_debug_font();
        return -1;
    }

    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family      = AF_INET;
    dest_addr.sin_port        = htons(s_target_port);
    dest_addr.sin_addr.s_addr = inet_addr(s_target_ip);

    /* Инициализация первого геймпада DualShock 3 */
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
    uint64_t last_ui_update = 0;

    /* Переменные для буфера упреждающего чтения */
    uint32_t buffered_lba_start = 0;
    uint32_t buffered_count     = 0;
    uint32_t next_send_time     = sysGetSystemTime();

    render_status(has_disc ? "Disc Mounted (CD-DA)" : "No Disc / Read Error",
                  has_disc ? (current_track_idx + 1) : 0,
                  has_disc ? toc.total_tracks : 0,
                  0,
                  has_disc ? toc.tracks[current_track_idx].duration_sec : 0,
                  is_paused);

    while (running) {
        /* Опрос геймпада */
        padInfo padinfo;
        padData paddata;
        ioPadGetInfo(&padinfo);

        if (padinfo.status[0]) {
            ioPadGetData(0, &paddata);
            uint32_t cur_buttons = (paddata.button[2] << 8) | paddata.button[3];
            uint32_t pressed = cur_buttons & (~prev_buttons);
            prev_buttons = cur_buttons;

            /* Circle: Выход в XMB */
            if (pressed & (PAD_CTRL_CIRCLE << 0)) {
                running = false;
                break;
            }

            /* Cross или Start: Пауза / Воспроизведение */
            if ((pressed & (PAD_CTRL_CROSS << 0)) || (pressed & (PAD_CTRL_START << 8))) {
                is_paused = !is_paused;
                next_send_time = sysGetSystemTime();
            }

            /* Triangle: Перечитать диск и обновить TOC */
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

            /* D-Pad Right или R1: Следующий трек */
            if (has_disc && ((pressed & (PAD_CTRL_RIGHT << 8)) || (pressed & (PAD_CTRL_R1 << 0)))) {
                if (current_track_idx + 1 < toc.total_tracks) {
                    current_track_idx++;
                    current_lba = toc.tracks[current_track_idx].start_lba;
                    buffered_count = 0;
                    next_send_time = sysGetSystemTime();
                }
            }

            /* D-Pad Left или L1: Предыдущий трек */
            if (has_disc && ((pressed & (PAD_CTRL_LEFT << 8)) || (pressed & (PAD_CTRL_L1 << 0)))) {
                uint32_t elapsed_in_track = current_lba - toc.tracks[current_track_idx].start_lba;
                /* Если от начала трека прошло больше 2 сек, прыгаем в начало текущего, иначе на предыдущий */
                if (elapsed_in_track > (2 * 75) || current_track_idx == 0) {
                    current_lba = toc.tracks[current_track_idx].start_lba;
                } else {
                    current_track_idx--;
                    current_lba = toc.tracks[current_track_idx].start_lba;
                }
                buffered_count = 0;
                next_send_time = sysGetSystemTime();
            }
        }

        /* Логика воспроизведения и стриминга */
        if (has_disc && !is_paused) {
            /* Проверка перехода за пределы текущего трека */
            uint32_t track_end_lba = (current_track_idx == toc.total_tracks - 1)
                                     ? toc.leadout_lba
                                     : toc.tracks[current_track_idx + 1].start_lba;

            if (current_lba >= track_end_lba) {
                if (current_track_idx + 1 < toc.total_tracks) {
                    current_track_idx++;
                    current_lba = toc.tracks[current_track_idx].start_lba;
                    buffered_count = 0;
                } else {
                    /* Конец диска: переход в режим паузы на первый трек */
                    current_track_idx = 0;
                    current_lba = toc.tracks[0].start_lba;
                    is_paused = true;
                    buffered_count = 0;
                    continue;
                }
            }

            /* Проверка необходимости пополнения упреждающего буфера */
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
                        /* Ошибка чтения привода: пауза */
                        is_paused = true;
                        sysUsleep(100000);
                        continue;
                    }
                }
            }

            /* Извлечение текущего сектора из буфера упреждающего чтения */
            uint32_t sector_offset = (current_lba - buffered_lba_start) * CDDA_SECTOR_SIZE;
            uint8_t *sector_ptr    = &s_read_ahead_buffer[sector_offset];

            /*
             * Отправка сектора 2352 байта двумя UDP пакетами по 1176 байт
             * (гарантированный размер без фрагментации в MTU 1500).
             */
            sendto(sock, sector_ptr, UDP_CHUNK_SIZE, 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr));

            sendto(sock, sector_ptr + UDP_CHUNK_SIZE, UDP_CHUNK_SIZE, 0,
                   (struct sockaddr *)&dest_addr, sizeof(dest_addr));

            current_lba++;

            /*
             * Прецизионный пейсинг (Pacing):
             * Шаг таймера ровно 13333 мкс на сектор (75 секторов/сек).
             */
            next_send_time += SECTOR_TIME_USEC;
            uint64_t now = sysGetSystemTime();

            if (next_send_time > now) {
                sysUsleep(next_send_time - now);
            } else if (now > next_send_time + 100000) {
                /* Защита от накопленного отставания таймера при задержке чтения привода */
                next_send_time = now;
            }
        } else {
            /* При паузе или отсутствии диска не нагружаем процессор */
            sysUsleep(16000);
        }

        /* Обновление интерфейса раз в 500 мс */
        uint64_t current_time = sysGetSystemTime();
        if (current_time - last_ui_update >= 500000) {
            last_ui_update = current_time;

            uint32_t track_sec = 0;
            uint32_t track_dur = 0;

            if (has_disc && current_track_idx < toc.total_tracks) {
                track_sec = (current_lba > toc.tracks[current_track_idx].start_lba)
                            ? (current_lba - toc.tracks[current_track_idx].start_lba) / 75
                            : 0;
                track_dur = toc.tracks[current_track_idx].duration_sec;
            }

            render_status(has_disc ? "Disc Mounted (CD-DA)" : "No Disc / Read Error",
                          has_disc ? (current_track_idx + 1) : 0,
                          has_disc ? toc.total_tracks : 0,
                          track_sec,
                          track_dur,
                          is_paused);
        }
    }

    /* Корректное освобождение ресурсов */
    cdrom_close();
    close(sock);
    netDeinitialize();
    term_debug_font();
    sysProcessExit(0);
    return 0;
}
