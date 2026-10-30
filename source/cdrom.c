#include "cdrom.h"
#include <sys/lv2syscall.h>
#include <string.h>
#include <stdio.h>

#define SYS_STORAGE_OPEN                 600
#define SYS_STORAGE_CLOSE                601
#define SYS_STORAGE_SEND_DEVICE_COMMAND  616

#define SCSI_CMD_READ_TOC                0x43
#define SCSI_CMD_READ_CD                 0xBE

static uint32_t s_device_handle = 0;
static bool     s_is_open = false;

/* Временный буфер TOC, выровненный по 128 байт для сисколлов LV2 */
static uint8_t s_toc_buffer[2048] __attribute__((aligned(128)));

/* Обертка над системным вызовом открытия хранилища (sys_storage_open) */
static inline int sys_storage_open(uint64_t dev_id, uint32_t *handle) {
    uint32_t h = 0;
    int ret = (int)lv2syscall4(SYS_STORAGE_OPEN, dev_id, 0, (uint64_t)(uintptr_t)&h, 0);
    if (ret == 0 && handle) {
        *handle = h;
    }
    return ret;
}

/* Обертка над закрытием дескриптора (sys_storage_close) */
static inline int sys_storage_close(uint32_t handle) {
    return (int)lv2syscall1(SYS_STORAGE_CLOSE, (uint64_t)handle);
}

/* Обертка над отправкой ATAPI/SCSI пакета (sys_storage_send_device_command) */
static inline int sys_storage_send_device_command(uint32_t handle,
                                                  struct lv2_atapi_cmnd_block *cmd,
                                                  void *buffer,
                                                  uint64_t buffer_size) {
    return (int)lv2syscall6(SYS_STORAGE_SEND_DEVICE_COMMAND,
                            (uint64_t)handle,
                            0x01,                                   /* Command 0x01 = ATAPI packet */
                            (uint64_t)(uintptr_t)cmd,
                            (uint64_t)sizeof(struct lv2_atapi_cmnd_block),
                            (uint64_t)(uintptr_t)buffer,
                            buffer_size);
}

int cdrom_init(void) {
    if (s_is_open) {
        return 0;
    }

    int ret = sys_storage_open(BDVD_DEV_ID, &s_device_handle);
    if (ret != 0) {
        return ret;
    }

    s_is_open = true;
    return 0;
}

void cdrom_close(void) {
    if (s_is_open) {
        sys_storage_close(s_device_handle);
        s_device_handle = 0;
        s_is_open = false;
    }
}

/*
 * Чтение таблицы TOC (SCSI Opcode 0x43).
 * Формат: Format 0 (Formatted TOC), LBA addressing (MSF=0).
 */
int cdrom_read_toc(cd_toc_t *toc) {
    if (!s_is_open || !toc) {
        return -1;
    }

    memset(toc, 0, sizeof(cd_toc_t));
    memset(s_toc_buffer, 0, sizeof(s_toc_buffer));

    struct lv2_atapi_cmnd_block cmd __attribute__((aligned(16)));
    memset(&cmd, 0, sizeof(cmd));

    uint16_t alloc_len = sizeof(s_toc_buffer);

    /* Формирование CDB-пакета SCSI 0x43 (READ TOC) */
    cmd.pkt[0] = SCSI_CMD_READ_TOC;
    cmd.pkt[1] = 0x00;                           /* MSF = 0 (LBA addressing) */
    cmd.pkt[2] = 0x00;                           /* Format 0x00: Formatted TOC */
    cmd.pkt[6] = 0x01;                           /* Starting track number = 1 */
    cmd.pkt[7] = (uint8_t)((alloc_len >> 8) & 0xFF); /* Allocation length MSB */
    cmd.pkt[8] = (uint8_t)(alloc_len & 0xFF);        /* Allocation length LSB */
    cmd.pkt[9] = 0x00;                           /* Format flags (00b) */

    cmd.pktlen     = 12;
    cmd.blocks     = 1;
    cmd.block_size = alloc_len;
    cmd.proto      = 3;                          /* PIO Data-In */
    cmd.in_out     = 1;                          /* DIR_READ */
    cmd.unknown    = 0;

    int ret = sys_storage_send_device_command(s_device_handle, &cmd, s_toc_buffer, alloc_len);
    if (ret != 0) {
        return ret;
    }

    /*
     * Парсинг заголовка TOC:
     * Byte 0-1: Длина данных TOC (Big-Endian, исключая эти 2 байта)
     * Byte 2: Номер первого трека
     * Byte 3: Номер последнего трека
     */
    uint16_t toc_len = ((uint16_t)s_toc_buffer[0] << 8) | s_toc_buffer[1];
    if (toc_len < 2) {
        return -2;
    }

    toc->first_track = s_toc_buffer[2];
    toc->last_track  = s_toc_buffer[3];

    if (toc->last_track < toc->first_track || toc->last_track >= MAX_TRACKS) {
        return -3;
    }

    toc->total_tracks = (toc->last_track - toc->first_track) + 1;

    /*
     * Парсинг дескрипторов треков (по 8 байт каждый):
     * Byte 0: Зарезервировано
     * Byte 1: ADR (старшие 4 бита) / Control (младшие 4 бита: 0 = Audio, 4 = Data)
     * Byte 2: Номер трека (1..99, либо 0xAA для Lead-Out)
     * Byte 3: Зарезервировано
     * Byte 4..7: Стартовый LBA (32-бит Big-Endian)
     */
    int num_descriptors = (toc_len - 2) / 8;
    int parsed_tracks = 0;

    for (int i = 0; i < num_descriptors; i++) {
        int offset = 4 + (i * 8);
        uint8_t control  = s_toc_buffer[offset + 1] & 0x0F;
        uint8_t track_no = s_toc_buffer[offset + 2];

        /* Endian-безопасный сбор 32-битного LBA */
        uint32_t lba = ((uint32_t)s_toc_buffer[offset + 4] << 24) |
                       ((uint32_t)s_toc_buffer[offset + 5] << 16) |
                       ((uint32_t)s_toc_buffer[offset + 6] << 8)  |
                       ((uint32_t)s_toc_buffer[offset + 7]);

        if (track_no == 0xAA) {
            /* Lead-Out трек: отмечает конец записанной области диска */
            toc->leadout_lba = lba;
        } else if (track_no >= toc->first_track && track_no <= toc->last_track) {
            int idx = track_no - toc->first_track;
            toc->tracks[idx].track_number = track_no;
            toc->tracks[idx].control      = control;
            toc->tracks[idx].start_lba    = lba;
            parsed_tracks++;
        }
    }

    if (parsed_tracks == 0 || toc->leadout_lba == 0) {
        return -4;
    }

    /* Расчет границ каждого трека и длительности */
    for (int i = 0; i < toc->total_tracks; i++) {
        uint32_t next_lba = (i == toc->total_tracks - 1)
                            ? toc->leadout_lba
                            : toc->tracks[i + 1].start_lba;

        if (next_lba >= toc->tracks[i].start_lba) {
            toc->tracks[i].sector_count = next_lba - toc->tracks[i].start_lba;
        } else {
            toc->tracks[i].sector_count = 0;
        }
        toc->tracks[i].duration_sec = toc->tracks[i].sector_count / 75;
    }

    toc->total_sectors      = toc->leadout_lba - toc->tracks[0].start_lba;
    toc->total_duration_sec = toc->total_sectors / 75;

    return 0;
}

/*
 * Чтение сырых секторов CD-DA (SCSI Opcode 0xBE).
 * Expected Sector Type = 0x04 (CD-DA)
 * Flag = 0x10 (User Data only: 2352 байта)
 */
int cdrom_read_sectors(uint32_t lba, uint32_t count, uint8_t *buffer) {
    if (!s_is_open || !buffer || count == 0) {
        return -1;
    }

    struct lv2_atapi_cmnd_block cmd __attribute__((aligned(16)));
    memset(&cmd, 0, sizeof(cmd));

    uint32_t total_bytes = count * CDDA_SECTOR_SIZE;

    /* Сборка CDB 0xBE (READ CD) */
    cmd.pkt[0] = SCSI_CMD_READ_CD;
    cmd.pkt[1] = 0x04;                           /* Expected Sector Type: CD-DA (001b << 2 = 0x04) */
    cmd.pkt[2] = (uint8_t)((lba >> 24) & 0xFF);  /* Starting LBA MSB */
    cmd.pkt[3] = (uint8_t)((lba >> 16) & 0xFF);
    cmd.pkt[4] = (uint8_t)((lba >> 8) & 0xFF);
    cmd.pkt[5] = (uint8_t)(lba & 0xFF);          /* Starting LBA LSB */
    cmd.pkt[6] = (uint8_t)((count >> 16) & 0xFF);/* Sector Count MSB */
    cmd.pkt[7] = (uint8_t)((count >> 8) & 0xFF);
    cmd.pkt[8] = (uint8_t)(count & 0xFF);        /* Sector Count LSB */
    cmd.pkt[9] = 0x10;                           /* Header/Subheader/User Data: 0x10 = User Data Only (2352b) */
    cmd.pkt[10] = 0x00;                          /* Sub-channel selection: None */
    cmd.pkt[11] = 0x00;                          /* Reserved */

    cmd.pktlen     = 12;
    cmd.blocks     = count;
    cmd.block_size = CDDA_SECTOR_SIZE;
    cmd.proto      = 3;                          /* PIO Data-In */
    cmd.in_out     = 1;                          /* DIR_READ */
    cmd.unknown    = 0;

    return sys_storage_send_device_command(s_device_handle, &cmd, buffer, total_bytes);
}
