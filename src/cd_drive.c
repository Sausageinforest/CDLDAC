#include "cd_drive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(__PS3__) || defined(__PPU__)
#include <sys/storage.h>
#include <sys/errno.h>

#define PS3_STORAGE_DEV_BDVD 0x0101000000000001ULL
#define PS3_SCSI_CMD_EXECUTE 0x22

static s32 s_drive_fd = -1;
#else
static int s_drive_fd = -1;
static cd_toc_t s_mock_toc;
static bool s_mock_disc_present = false;
#endif

static cd_drive_status_t s_drive_status = CD_STATUS_NO_DISC;

uint32_t cd_msf_to_lba(uint8_t m, uint8_t s, uint8_t f) {
    uint32_t total_frames = ((uint32_t)m * 60 + (uint32_t)s) * CD_FRAMES_PER_SEC + (uint32_t)f;
    if (total_frames >= CD_PREGAP_FRAMES) {
        return total_frames - CD_PREGAP_FRAMES;
    }
    return 0;
}

void cd_lba_to_msf(uint32_t lba, uint8_t *m, uint8_t *s, uint8_t *f) {
    uint32_t total_frames = lba + CD_PREGAP_FRAMES;
    if (f) *f = total_frames % CD_FRAMES_PER_SEC;
    total_frames /= CD_FRAMES_PER_SEC;
    if (s) *s = total_frames % 60;
    if (m) *m = total_frames / 60;
}

int cd_drive_init(void) {
#if defined(__PS3__) || defined(__PPU__)
    s32 ret = sys_storage_open(PS3_STORAGE_DEV_BDVD, &s_drive_fd);
    if (ret != 0) {
        printf("[CD_DRIVE] Failed to open BDVD device: 0x%08X\n", ret);
        s_drive_fd = -1;
        s_drive_status = CD_STATUS_NO_DISC;
        return -1;
    }
    printf("[CD_DRIVE] Successfully opened BDVD device (fd: %d)\n", s_drive_fd);
    s_drive_status = CD_STATUS_READY;
    return 0;
#else
    s_drive_fd = 1;
    s_mock_disc_present = true;
    s_drive_status = CD_STATUS_READY;

    /* Initialize a default mock TOC for test / simulator environments */
    memset(&s_mock_toc, 0, sizeof(s_mock_toc));
    s_mock_toc.first_track = 1;
    s_mock_toc.last_track = 10;
    uint32_t cur_lba = 0;
    for (int i = 1; i <= 10; i++) {
        uint32_t trk_len = (180 + (i * 15)) * CD_FRAMES_PER_SEC; /* ~3-4 mins */
        s_mock_toc.tracks[i].track_num = i;
        s_mock_toc.tracks[i].start_lba = cur_lba;
        s_mock_toc.tracks[i].length_sectors = trk_len;
        s_mock_toc.tracks[i].duration_ms = (trk_len * 1000) / CD_FRAMES_PER_SEC;
        s_mock_toc.tracks[i].is_audio = true;
        cur_lba += trk_len;
    }
    s_mock_toc.leadout_lba = cur_lba;
    s_mock_toc.total_sectors = cur_lba;
    s_mock_toc.total_duration_ms = (cur_lba * 1000) / CD_FRAMES_PER_SEC;

    return 0;
#endif
}

void cd_drive_shutdown(void) {
#if defined(__PS3__) || defined(__PPU__)
    if (s_drive_fd >= 0) {
        sys_storage_close(s_drive_fd);
        s_drive_fd = -1;
    }
#else
    s_drive_fd = -1;
    s_mock_disc_present = false;
#endif
    s_drive_status = CD_STATUS_NO_DISC;
}

cd_drive_status_t cd_drive_poll_status(void) {
#if defined(__PS3__) || defined(__PPU__)
    if (s_drive_fd < 0) {
        if (cd_drive_init() == 0) {
            cd_toc_t test_toc;
            if (cd_drive_read_toc(&test_toc) == 0) {
                s_drive_status = CD_STATUS_READY;
            } else {
                s_drive_status = CD_STATUS_NO_DISC;
            }
        } else {
            s_drive_status = CD_STATUS_NO_DISC;
        }
    }
    return s_drive_status;
#else
    return s_mock_disc_present ? CD_STATUS_READY : CD_STATUS_NO_DISC;
#endif
}

int cd_drive_read_toc(cd_toc_t *out_toc) {
    if (!out_toc) return -1;
    memset(out_toc, 0, sizeof(cd_toc_t));

#if defined(__PS3__) || defined(__PPU__)
    if (s_drive_fd < 0) {
        if (cd_drive_init() != 0) return -1;
    }

    /* Buffer for SCSI READ TOC (Format 0, normal TOC) */
    uint8_t toc_buf[4 + 100 * 8] = {0};
    uint8_t cdb[10] = {0};
    cdb[0] = 0x43; /* SCSI READ TOC */
    cdb[1] = 0x00; /* LBA format */
    cdb[2] = 0x00; /* Format 0: Track descriptors */
    cdb[6] = 0x00; /* Start at track 0 */
    cdb[7] = (sizeof(toc_buf) >> 8) & 0xFF;
    cdb[8] = sizeof(toc_buf) & 0xFF;

    s32 ret = sys_storage_send_device_command(s_drive_fd, PS3_SCSI_CMD_EXECUTE,
                                              cdb, sizeof(cdb),
                                              toc_buf, sizeof(toc_buf));
    if (ret != 0) {
        printf("[CD_DRIVE] sys_storage_send_device_command (READ TOC) failed: 0x%08X\n", ret);
        s_drive_status = CD_STATUS_NO_DISC;
        return -1;
    }

    uint16_t toc_len = ((uint16_t)toc_buf[0] << 8) | toc_buf[1];
    uint8_t first_track = toc_buf[2];
    uint8_t last_track = toc_buf[3];

    if (first_track == 0 || last_track == 0 || first_track > last_track || last_track >= CD_MAX_TRACKS) {
        printf("[CD_DRIVE] Invalid TOC track range: %d to %d\n", first_track, last_track);
        return -1;
    }

    out_toc->first_track = first_track;
    out_toc->last_track = last_track;

    int num_descriptors = (toc_len - 2) / 8;
    for (int i = 0; i < num_descriptors; i++) {
        uint8_t *desc = &toc_buf[4 + i * 8];
        uint8_t ctrl_adr = desc[1];
        uint8_t track_num = desc[2];
        uint32_t lba = ((uint32_t)desc[4] << 24) |
                       ((uint32_t)desc[5] << 16) |
                       ((uint32_t)desc[6] << 8)  |
                       (uint32_t)desc[7];

        bool is_audio = ((ctrl_adr & 0x04) == 0);

        if (track_num == 0xAA) {
            /* Lead-out sector */
            out_toc->leadout_lba = lba;
        } else if (track_num >= first_track && track_num <= last_track) {
            out_toc->tracks[track_num].track_num = track_num;
            out_toc->tracks[track_num].start_lba = lba;
            out_toc->tracks[track_num].is_audio = is_audio;
        }
    }

    /* Calculate lengths and durations */
    for (int t = first_track; t <= last_track; t++) {
        uint32_t next_lba = (t == last_track) ? out_toc->leadout_lba : out_toc->tracks[t + 1].start_lba;
        if (next_lba > out_toc->tracks[t].start_lba) {
            out_toc->tracks[t].length_sectors = next_lba - out_toc->tracks[t].start_lba;
        } else {
            out_toc->tracks[t].length_sectors = 0;
        }
        out_toc->tracks[t].duration_ms = (out_toc->tracks[t].length_sectors * 1000) / CD_FRAMES_PER_SEC;
    }

    out_toc->total_sectors = out_toc->leadout_lba;
    out_toc->total_duration_ms = (out_toc->total_sectors * 1000) / CD_FRAMES_PER_SEC;

    s_drive_status = CD_STATUS_READY;
    return 0;
#else
    memcpy(out_toc, &s_mock_toc, sizeof(cd_toc_t));
    return 0;
#endif
}

int cd_drive_read_audio_sectors(uint32_t lba, uint32_t sector_count, uint8_t *out_buffer) {
    if (!out_buffer || sector_count == 0) return -1;

#if defined(__PS3__) || defined(__PPU__)
    if (s_drive_fd < 0) return -1;

    /* SCSI command 0xBE: READ CD */
    uint8_t cdb[12] = {0};
    cdb[0] = 0xBE;                  /* READ CD */
    cdb[1] = 0x04;                  /* Expected Sector Type: CD-DA (audio) */
    cdb[2] = (lba >> 24) & 0xFF;    /* Starting LBA */
    cdb[3] = (lba >> 16) & 0xFF;
    cdb[4] = (lba >> 8)  & 0xFF;
    cdb[5] = lba & 0xFF;
    cdb[6] = (sector_count >> 16) & 0xFF; /* Sector count (3 bytes) */
    cdb[7] = (sector_count >> 8)  & 0xFF;
    cdb[8] = sector_count & 0xFF;
    cdb[9] = 0x10;                  /* Header Code: User Data only (2352 bytes/sector) */

    uint32_t total_bytes = sector_count * CD_SECTOR_SIZE_RAW;
    s32 ret = sys_storage_send_device_command(s_drive_fd, PS3_SCSI_CMD_EXECUTE,
                                              cdb, sizeof(cdb),
                                              out_buffer, total_bytes);
    if (ret != 0) {
        /* Fallback: attempt direct block read if SCSI BE is restricted */
        u32 sectors_read = 0;
        ret = sys_storage_read(s_drive_fd, lba, sector_count, out_buffer, &sectors_read, 0);
        if (ret != 0) {
            printf("[CD_DRIVE] Audio sector read failed at LBA %u: 0x%08X\n", lba, ret);
            return -1;
        }
    }
    return 0;
#else
    /* Mock audio generator: 1 kHz stereo sine wave test tone */
    uint32_t total_samples = sector_count * (CD_SECTOR_SIZE_RAW / 4);
    int16_t *pcm16 = (int16_t *)out_buffer;
    for (uint32_t i = 0; i < total_samples; i++) {
        /* 44100 Hz sample rate, 440 Hz concert pitch A */
        double phase = (double)(lba * 588 + i) * 2.0 * 3.141592653589793 * 440.0 / 44100.0;
        int16_t sample = (int16_t)(sin(phase) * 16000.0);
        pcm16[i * 2] = sample;     /* Left */
        pcm16[i * 2 + 1] = sample; /* Right */
    }
    return 0;
#endif
}
