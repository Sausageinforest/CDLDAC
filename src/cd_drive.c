#include "cd_drive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(__PS3__) || defined(__PPU__)
#define SYS_STORAGE_OPEN                 600
#define SYS_STORAGE_CLOSE                601
#define SYS_STORAGE_SEND_DEVICE_COMMAND  616
#define BDVD_DEV_ID                      0x0101000000000006ULL
#define SCSI_CMD_READ_TOC                0x43
#define SCSI_CMD_READ_CD                 0xBE

struct lv2_atapi_cmnd_block {
    uint8_t  pkt[32];
    uint32_t pktlen;
    uint32_t blocks;
    uint32_t block_size;
    uint32_t proto;
    uint32_t in_out;
    uint32_t unknown;
} __attribute__((packed, aligned(16)));

static inline uint64_t lv2syscall1(uint64_t nr, uint64_t a1) {
    register uint64_t r11 __asm__("r11") = nr;
    register uint64_t r3  __asm__("r3")  = a1;
    __asm__ __volatile__("sc 2" : "+r"(r3) : "r"(r11) : "r0", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r12", "lr", "ctr", "xer", "cr0", "cr1", "cr5", "cr6", "cr7", "memory");
    return r3;
}

static inline uint64_t lv2syscall4(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    register uint64_t r11 __asm__("r11") = nr;
    register uint64_t r3  __asm__("r3")  = a1;
    register uint64_t r4  __asm__("r4")  = a2;
    register uint64_t r5  __asm__("r5")  = a3;
    register uint64_t r6  __asm__("r6")  = a4;
    __asm__ __volatile__("sc 2" : "+r"(r3) : "r"(r11), "r"(r4), "r"(r5), "r"(r6) : "r0", "r7", "r8", "r9", "r10", "r12", "lr", "ctr", "xer", "cr0", "cr1", "cr5", "cr6", "cr7", "memory");
    return r3;
}

static inline uint64_t lv2syscall6(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
    register uint64_t r11 __asm__("r11") = nr;
    register uint64_t r3  __asm__("r3")  = a1;
    register uint64_t r4  __asm__("r4")  = a2;
    register uint64_t r5  __asm__("r5")  = a3;
    register uint64_t r6  __asm__("r6")  = a4;
    register uint64_t r7  __asm__("r7")  = a5;
    register uint64_t r8  __asm__("r8")  = a6;
    __asm__ __volatile__("sc 2" : "+r"(r3) : "r"(r11), "r"(r4), "r"(r5), "r"(r6), "r"(r7), "r"(r8) : "r0", "r9", "r10", "r12", "lr", "ctr", "xer", "cr0", "cr1", "cr5", "cr6", "cr7", "memory");
    return r3;
}

static uint32_t s_device_handle = 0;
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
    if (s_device_handle != 0) return 0;
    uint32_t handle = 0;
    int ret = (int)lv2syscall4(SYS_STORAGE_OPEN, BDVD_DEV_ID, 0, (uint64_t)(uintptr_t)&handle, 0);
    if (ret != 0) {
        printf("[CD_DRIVE] Failed to open BDVD device: 0x%08X\n", ret);
        s_device_handle = 0;
        s_drive_status = CD_STATUS_NO_DISC;
        return -1;
    }
    s_device_handle = handle;
    printf("[CD_DRIVE] Successfully opened BDVD device (handle: 0x%08X)\n", s_device_handle);
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
    if (s_device_handle != 0) {
        lv2syscall1(SYS_STORAGE_CLOSE, (uint64_t)s_device_handle);
        s_device_handle = 0;
    }
#else
    s_drive_fd = -1;
    s_mock_disc_present = false;
#endif
    s_drive_status = CD_STATUS_NO_DISC;
}

cd_drive_status_t cd_drive_poll_status(void) {
#if defined(__PS3__) || defined(__PPU__)
    if (s_device_handle == 0) {
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
    if (s_device_handle == 0) {
        if (cd_drive_init() != 0) return -1;
    }

    uint8_t toc_buf[4 + 100 * 8] __attribute__((aligned(16)));
    memset(toc_buf, 0, sizeof(toc_buf));

    struct lv2_atapi_cmnd_block cmd __attribute__((aligned(16)));
    memset(&cmd, 0, sizeof(cmd));

    uint16_t alloc_len = sizeof(toc_buf);
    cmd.pkt[0] = SCSI_CMD_READ_TOC;
    cmd.pkt[1] = 0x00;
    cmd.pkt[2] = 0x00;
    cmd.pkt[6] = 0x01;
    cmd.pkt[7] = (uint8_t)((alloc_len >> 8) & 0xFF);
    cmd.pkt[8] = (uint8_t)(alloc_len & 0xFF);
    cmd.pkt[9] = 0x00;

    cmd.pktlen     = 12;
    cmd.blocks     = 1;
    cmd.block_size = alloc_len;
    cmd.proto      = 3; /* PIO Data-In */
    cmd.in_out     = 1; /* Read */
    cmd.unknown    = 0;

    int ret = (int)lv2syscall6(SYS_STORAGE_SEND_DEVICE_COMMAND,
                               (uint64_t)s_device_handle, 0x01,
                               (uint64_t)(uintptr_t)&cmd, sizeof(cmd),
                               (uint64_t)(uintptr_t)toc_buf, alloc_len);
    if (ret != 0) {
        printf("[CD_DRIVE] lv2syscall6 READ_TOC failed: 0x%08X\n", ret);
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
    if (s_device_handle == 0) return -1;

    struct lv2_atapi_cmnd_block cmd __attribute__((aligned(16)));
    memset(&cmd, 0, sizeof(cmd));
    uint32_t total_bytes = sector_count * CD_SECTOR_SIZE_RAW;

    cmd.pkt[0] = SCSI_CMD_READ_CD;
    cmd.pkt[1] = 0x04; /* CD-DA */
    cmd.pkt[2] = (uint8_t)((lba >> 24) & 0xFF);
    cmd.pkt[3] = (uint8_t)((lba >> 16) & 0xFF);
    cmd.pkt[4] = (uint8_t)((lba >> 8)  & 0xFF);
    cmd.pkt[5] = (uint8_t)(lba & 0xFF);
    cmd.pkt[6] = (uint8_t)((sector_count >> 16) & 0xFF);
    cmd.pkt[7] = (uint8_t)((sector_count >> 8)  & 0xFF);
    cmd.pkt[8] = (uint8_t)(sector_count & 0xFF);
    cmd.pkt[9] = 0x10; /* User data only (2352 bytes) */
    cmd.pkt[10] = 0x00;
    cmd.pkt[11] = 0x00;

    cmd.pktlen     = 12;
    cmd.blocks     = sector_count;
    cmd.block_size = CD_SECTOR_SIZE_RAW;
    cmd.proto      = 3; /* PIO Data-In */
    cmd.in_out     = 1; /* Read */
    cmd.unknown    = 0;

    int ret = (int)lv2syscall6(SYS_STORAGE_SEND_DEVICE_COMMAND,
                               (uint64_t)s_device_handle, 0x01,
                               (uint64_t)(uintptr_t)&cmd, sizeof(cmd),
                               (uint64_t)(uintptr_t)out_buffer, total_bytes);
    if (ret != 0) {
        printf("[CD_DRIVE] Audio sector read failed at LBA %u: 0x%08X\n", lba, ret);
        return -1;
    }
    return 0;
#else
    /* Mock audio generator: 1 kHz stereo sine wave test tone */
    uint32_t total_samples = sector_count * (CD_SECTOR_SIZE_RAW / 4);
    int16_t *pcm16 = (int16_t *)out_buffer;
    for (uint32_t i = 0; i < total_samples; i++) {
        double phase = (double)(lba * 588 + i) * 2.0 * 3.141592653589793 * 440.0 / 44100.0;
        int16_t sample = (int16_t)(sin(phase) * 16000.0);
        pcm16[i * 2] = sample;     /* Left */
        pcm16[i * 2 + 1] = sample; /* Right */
    }
    return 0;
#endif
}
