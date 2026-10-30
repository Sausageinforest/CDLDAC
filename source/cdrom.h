#ifndef CDROM_H
#define CDROM_H

#include <stdint.h>
#include <stdbool.h>

#define BDVD_DEV_ID             0x0101000000000006ULL
#define CDDA_SECTOR_SIZE        2352
#define READ_AHEAD_SECTORS      25
#define READ_AHEAD_BUFFER_SIZE  (CDDA_SECTOR_SIZE * READ_AHEAD_SECTORS) /* 58800 bytes */
#define MAX_TRACKS              100

/* Структура командного блока ATAPI для syscall 616 ядра LV2 */
struct lv2_atapi_cmnd_block {
    uint8_t  pkt[32];      /* CDB-пакет команды (первые 12 байт для ATAPI) */
    uint32_t pktlen;       /* Длина пакета (12 для ATAPI) */
    uint32_t blocks;       /* Число передаваемых блоков */
    uint32_t block_size;   /* Размер одного блока в байтах */
    uint32_t proto;        /* Протокол: 3 = PIO Data-In, 0 = Non-data */
    uint32_t in_out;       /* Направление: 1 = DIR_READ (устройство -> RAM) */
    uint32_t unknown;      /* Зарезервировано ядра LV2 (0) */
} __attribute__((packed, aligned(16)));

/* Информация о треке из таблицы содержания диска (TOC) */
typedef struct {
    uint8_t  track_number;
    uint8_t  control;      /* 0x00 = Audio, 0x04 = Data */
    uint32_t start_lba;
    uint32_t sector_count;
    uint32_t duration_sec;
} cd_track_info_t;

/* Полная структура диска */
typedef struct {
    uint8_t         first_track;
    uint8_t         last_track;
    uint8_t         total_tracks;
    uint32_t        leadout_lba;
    uint32_t        total_sectors;
    uint32_t        total_duration_sec;
    cd_track_info_t tracks[MAX_TRACKS];
} cd_toc_t;

/* Инициализация, монтирование привода и чтение метаданных */
int  cdrom_init(void);
void cdrom_close(void);
int  cdrom_read_toc(cd_toc_t *toc);

/*
 * Чтение секторов CD-DA.
 * buffer должен быть выровнен по 128 байт.
 * count секторов (2352 * count байт).
 */
int  cdrom_read_sectors(uint32_t lba, uint32_t count, uint8_t *buffer);

#endif /* CDROM_H */
