#include "logger.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>

#if defined(__PS3__) || defined(__PPU__)
#include <lv2/sysfs.h>
#endif

void log_write(const char *fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (len <= 0) return;

    /* Always output to stdout/console */
    fputs(buf, stdout);
    fflush(stdout);

    /* Flush immediately to USB drive and internal HDD */
    static const char * const s_log_paths[] = {
        "/dev_usb000/cdplayer.log",
        "/dev_usb001/cdplayer.log",
        "/dev_usb002/cdplayer.log",
        "/dev_usb003/cdplayer.log",
        "/dev_usb004/cdplayer.log",
        "/dev_usb005/cdplayer.log",
        "/dev_usb006/cdplayer.log",
        "/dev_usb007/cdplayer.log",
        "/dev_hdd0/tmp/cdplayer.log",
        "/dev_hdd0/game/CDPL00001/debug.log",
        "/dev_hdd0/game/CDPL00001/USRDIR/debug.log"
    };

    for (size_t i = 0; i < sizeof(s_log_paths) / sizeof(s_log_paths[0]); i++) {
#if defined(__PS3__) || defined(__PPU__)
        s32 fd = -1;
        if (sysFsOpen(s_log_paths[i], SYS_O_WRONLY | SYS_O_CREAT | SYS_O_APPEND, &fd, NULL, 0) == 0 && fd >= 0) {
            u64 written = 0;
            sysFsWrite(fd, buf, len, &written);
            sysFsClose(fd);
        }
#else
        FILE *f = fopen(s_log_paths[i], "a");
        if (f) {
            fwrite(buf, 1, len, f);
            fflush(f);
            fsync(fileno(f));
            fclose(f);
        }
#endif
    }
}
