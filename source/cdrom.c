#include "cdrom.h"
#include <string.h>
#include <stdio.h>

#define SYS_STORAGE_OPEN                 600
#define SYS_STORAGE_CLOSE                601
#define SYS_STORAGE_SEND_DEVICE_COMMAND  616

#define SCSI_CMD_READ_TOC                0x43
#define SCSI_CMD_READ_CD                 0xBE

static uint32_t s_device_handle = 0;
static bool     s_is_open = false;
static uint8_t  s_toc_buffer[2048] __attribute__((aligned(128)));

static inline uint64_t lv2syscall1(uint64_t nr, uint64_t a1) {
    register uint64_t r11 __asm__("r11") = nr;
    register uint64_t r3  __asm__("r3")  = a1;
    __asm__ __volatile__(
        "sc 2"
        : "+r"(r3)
        : "r"(r11)
        : "r0", "r4", "r5", "r6", "r7", "r8", "r9", "r10", "r12", "lr", "ctr", "xer", "cr0", "cr1", "cr5", "cr6", "cr7", "memory"
    );
    return r3;
}

static inline uint64_t lv2syscall4(uint64_t nr, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4) {
    register uint64_t r11 __asm__("r11") = nr;
    register uint64_t r3  __asm__("r3")  = a1;
    register uint64_t r4  __asm__("r4")  = a2;
    register uint64_t r5  __asm__("r5")  = a3;
    register uint64_t r6  __asm__("r6")  = a4;
    __asm__ __volatile__(
        "sc 2"
        : "+r"(r3)
        : "r"(r11), "r"(r4), "r"(r5), "r"(r6)
        : "r0", "r7", "r8", "r9", "r10", "r12", "lr", "ctr", "xer", "cr0", "cr1", "cr5", "cr6", "cr7", "memory"
    );
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
    __asm__ __volatile__(
        "sc 2"
        : "+r"(r3)
        : "r"(r11), "r"(r4), "r"(r5), "r"(r6), "r"(r7), "r"(r8)
        : "r0", "r9", "r10", "r12", "lr", "ctr", "xer", "cr0", "cr1", "cr5", "cr6", "cr7", "memory"
    );
    return r3;
}

int cdrom_init(void) {
    if (s_is_open) return 0;
    uint32_t h = 0;
    int ret = (int)lv2syscall4(SYS_STORAGE_OPEN, BDVD_DEV_ID, 0, (uint64_t)(uintptr_t)&h, 0);
    if (ret == 0) {
        s_device_handle = h;
        s_is_open = true;
    }
    return ret;
}

void cdrom_close(void) {
    if (s_is_open) {
        lv2syscall1(SYS_STORAGE_CLOSE, (uint64_t)s_device_handle);
        s_device_handle = 0;
        s_is_open = false;
    }
}

int cdrom_read_toc(cd_toc_t *toc) {
    if (!s_is_open || !toc) return -1;
    memset(toc, 0, sizeof(cd_toc_t));
    memset(s_toc_buffer, 0, sizeof(s_toc_buffer));

    struct lv2_atapi_cmnd_block cmd __attribute__((aligned(16)));
    memset(&cmd, 0, sizeof(cmd));

    uint16_t alloc_len = sizeof(s_toc_buffer);
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
    cmd.proto      = 3;
    cmd.in_out     = 1;
    cmd.unknown    = 0;

    int ret = (int)lv2syscall6(SYS_STORAGE_SEND_DEVICE_COMMAND,
                               (uint64_t)s_device_handle, 0x01,
                               (uint64_t)(uintptr_t)&cmd, sizeof(cmd),
                               (uint64_t)(uintptr_t)s_toc_buffer, alloc_len);
    if (ret != 0) return ret;

    uint16_t toc_len = ((uint16_t)s_toc_buffer[0] << 8) | s_toc_buffer[1];
    if (toc_len < 2) return -2;

    toc->first_track = s_toc_buffer[2];
    toc->last_track  = s_toc_buffer[3];
    if (toc->last_track < toc->first_track || toc->last_track >= MAX_TRACKS) return -3;

    toc->total_tracks = (toc->last_track - toc->first_track) + 1;
    int num_descriptors = (toc_len - 2) / 8;
    int parsed_tracks = 0;

    for (int i = 0; i < num_descriptors; i++) {
        int offset = 4 + (i * 8);
        uint8_t control  = s_toc_buffer[offset + 1] & 0x0F;
        uint8_t track_no = s_toc_buffer[offset + 2];
        uint32_t lba = ((uint32_t)s_toc_buffer[offset + 4] << 24) |
                       ((uint32_t)s_toc_buffer[offset + 5] << 16) |
                       ((uint32_t)s_toc_buffer[offset + 6] << 8)  |
                       ((uint32_t)s_toc_buffer[offset + 7]);

        if (track_no == 0xAA) {
            toc->leadout_lba = lba;
        } else if (track_no >= toc->first_track && track_no <= toc->last_track) {
            int idx = track_no - toc->first_track;
            toc->tracks[idx].track_number = track_no;
            toc->tracks[idx].control      = control;
            toc->tracks[idx].start_lba    = lba;
            parsed_tracks++;
        }
    }

    if (parsed_tracks == 0 || toc->leadout_lba == 0) return -4;

    for (int i = 0; i < toc->total_tracks; i++) {
        uint32_t next_lba = (i == toc->total_tracks - 1)
                            ? toc->leadout_lba
                            : toc->tracks[i + 1].start_lba;
        toc->tracks[i].sector_count = (next_lba >= toc->tracks[i].start_lba)
                                      ? (next_lba - toc->tracks[i].start_lba) : 0;
        toc->tracks[i].duration_sec = toc->tracks[i].sector_count / 75;
    }

    toc->total_sectors      = toc->leadout_lba - toc->tracks[0].start_lba;
    toc->total_duration_sec = toc->total_sectors / 75;
    return 0;
}

int cdrom_read_sectors(uint32_t lba, uint32_t count, uint8_t *buffer) {
    if (!s_is_open || !buffer || count == 0) return -1;

    struct lv2_atapi_cmnd_block cmd __attribute__((aligned(16)));
    memset(&cmd, 0, sizeof(cmd));
    uint32_t total_bytes = count * CDDA_SECTOR_SIZE;

    cmd.pkt[0] = SCSI_CMD_READ_CD;
    cmd.pkt[1] = 0x04;
    cmd.pkt[2] = (uint8_t)((lba >> 24) & 0xFF);
    cmd.pkt[3] = (uint8_t)((lba >> 16) & 0xFF);
    cmd.pkt[4] = (uint8_t)((lba >> 8) & 0xFF);
    cmd.pkt[5] = (uint8_t)(lba & 0xFF);
    cmd.pkt[6] = (uint8_t)((count >> 16) & 0xFF);
    cmd.pkt[7] = (uint8_t)((count >> 8) & 0xFF);
    cmd.pkt[8] = (uint8_t)(count & 0xFF);
    cmd.pkt[9] = 0x10;
    cmd.pkt[10] = 0x00;
    cmd.pkt[11] = 0x00;

    cmd.pktlen     = 12;
    cmd.blocks     = count;
    cmd.block_size = CDDA_SECTOR_SIZE;
    cmd.proto      = 3;
    cmd.in_out     = 1;
    cmd.unknown    = 0;

    return (int)lv2syscall6(SYS_STORAGE_SEND_DEVICE_COMMAND,
                            (uint64_t)s_device_handle, 0x01,
                            (uint64_t)(uintptr_t)&cmd, sizeof(cmd),
                            (uint64_t)(uintptr_t)buffer, total_bytes);
}
