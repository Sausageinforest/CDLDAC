#ifndef CD_DRIVE_H
#define CD_DRIVE_H

#include <stdint.h>
#include <stdbool.h>

#define CD_MAX_TRACKS 100
#define CD_SECTOR_SIZE_RAW 2352     /* Red Book CD-DA raw PCM sector size */
#define CD_FRAMES_PER_SEC 75
#define CD_PREGAP_FRAMES 150       /* 2 seconds pregap = 150 frames */

typedef enum {
    CD_STATUS_NO_DISC = 0,
    CD_STATUS_DISC_INSERTED,
    CD_STATUS_READY,
    CD_STATUS_ERROR
} cd_drive_status_t;

typedef struct {
    uint8_t track_num;
    uint32_t start_lba;
    uint32_t length_sectors;
    uint32_t duration_ms;
    bool is_audio;
} cd_track_info_t;

typedef struct {
    uint8_t first_track;
    uint8_t last_track;
    uint32_t leadout_lba;
    uint32_t total_sectors;
    uint32_t total_duration_ms;
    cd_track_info_t tracks[CD_MAX_TRACKS]; /* 1-indexed, tracks[1] .. tracks[last_track] */
} cd_toc_t;

/* Initialize CD drive subsystem */
int cd_drive_init(void);

/* Shutdown and close CD drive */
void cd_drive_shutdown(void);

/* Check drive status (handles disc insertion/ejection) */
cd_drive_status_t cd_drive_poll_status(void);

/* Read Table of Contents (TOC) from inserted disc */
int cd_drive_read_toc(cd_toc_t *out_toc);

/* Read raw CD-DA audio sectors (2352 bytes * sector_count) */
int cd_drive_read_audio_sectors(uint32_t lba, uint32_t sector_count, uint8_t *out_buffer);

/* Helper to convert MSF to LBA and LBA to MSF */
uint32_t cd_msf_to_lba(uint8_t m, uint8_t s, uint8_t f);
void cd_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f);

#endif /* CD_DRIVE_H */
