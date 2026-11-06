#ifndef METADATA_H
#define METADATA_H

#include "cd_drive.h"
#include <stdint.h>
#include <stdbool.h>

#define CACHE_BASE_PATH_PS3 "/dev_hdd0/game/CDPL00001/USRDIR/cache"
#define CACHE_BASE_PATH_LOCAL "./cache"

typedef struct {
    int number;
    char title[256];
    uint32_t duration_ms;
} album_track_t;

typedef struct {
    char disc_id[64];
    char freedb_id[16];
    char album_title[256];
    char artist_name[256];
    char release_year[16];
    char release_mbid[64];
    int track_count;
    album_track_t tracks[CD_MAX_TRACKS];
    bool cover_loaded;
    char cover_path[256];
    uint8_t *cover_image_bytes;
    int cover_image_size;
} album_metadata_t;

/* Initialize networking and metadata cache */
int metadata_init(void);

/* Shutdown networking */
void metadata_shutdown(void);

/* Fetch album metadata from MusicBrainz API (with automatic fallback to TOC) */
int metadata_fetch_disc(const cd_toc_t *toc, const char *disc_id, album_metadata_t *out_meta);

/* Download or load cached cover art for the release */
int metadata_load_cover(album_metadata_t *meta);

/* Set default fallback metadata if network or database search fails */
void metadata_set_fallback(const cd_toc_t *toc, const char *disc_id, album_metadata_t *out_meta);

/* Free allocated memory inside metadata structure */
void metadata_cleanup(album_metadata_t *meta);

#endif /* METADATA_H */
