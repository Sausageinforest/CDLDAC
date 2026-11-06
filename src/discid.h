#ifndef DISCID_H
#define DISCID_H

#include "cd_drive.h"
#include <stddef.h>

#define DISCID_MB_LENGTH 28     /* MusicBrainz DiscID length without null terminator */
#define DISCID_FREEDB_LENGTH 8  /* FreeDB DiscID hex string length */

/* Calculate MusicBrainz DiscID from TOC (buffer size should be at least 32 bytes) */
int discid_calculate_musicbrainz(const cd_toc_t *toc, char *out_discid, size_t max_len);

/* Calculate FreeDB / CDDB DiscID from TOC (buffer size should be at least 16 bytes) */
int discid_calculate_freedb(const cd_toc_t *toc, char *out_freedb, size_t max_len);

/* Helper cryptographic hashes */
void sha1_hash(const uint8_t *data, size_t len, uint8_t digest[20]);
void md5_hash(const uint8_t *data, size_t len, uint8_t digest[16]);

#endif /* DISCID_H */
