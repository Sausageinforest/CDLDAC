#include "metadata.h"
#include "discid.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>

#if defined(__PS3__) || defined(__PPU__)
#include <net/net.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#define HTTP_USER_AGENT "PS3CDPlayer/1.0.0 (contact@homebrew.ps3)"
#define HTTP_RECV_BUF_SIZE 16384
#define HTTP_MAX_REDIRECTS 5

static const char *get_cache_dir(void) {
#if defined(__PS3__) || defined(__PPU__)
    return CACHE_BASE_PATH_PS3;
#else
    return CACHE_BASE_PATH_LOCAL;
#endif
}

static void ensure_cache_dir(void) {
    const char *dir = get_cache_dir();
    mkdir(dir, 0777);
}

int metadata_init(void) {
#if defined(__PS3__) || defined(__PPU__)
    netInitialize();
#endif
    ensure_cache_dir();
    return 0;
}

void metadata_shutdown(void) {
#if defined(__PS3__) || defined(__PPU__)
    netDeinitialize();
#endif
}

void metadata_cleanup(album_metadata_t *meta) {
    if (!meta) return;
    if (meta->cover_image_bytes) {
        free(meta->cover_image_bytes);
        meta->cover_image_bytes = NULL;
    }
    meta->cover_image_size = 0;
    meta->cover_loaded = false;
}

void metadata_set_fallback(const cd_toc_t *toc, const char *disc_id, album_metadata_t *out_meta) {
    if (!out_meta) return;
    metadata_cleanup(out_meta);

    memset(out_meta, 0, sizeof(album_metadata_t));
    if (disc_id) {
        strncpy(out_meta->disc_id, disc_id, sizeof(out_meta->disc_id) - 1);
    }
    if (toc) {
        discid_calculate_freedb(toc, out_meta->freedb_id, sizeof(out_meta->freedb_id));
    }

    snprintf(out_meta->album_title, sizeof(out_meta->album_title), "Audio CD");
    snprintf(out_meta->artist_name, sizeof(out_meta->artist_name), "Unknown Artist");
    snprintf(out_meta->release_year, sizeof(out_meta->release_year), "----");

    if (toc && toc->first_track >= 1 && toc->last_track >= toc->first_track) {
        out_meta->track_count = toc->last_track - toc->first_track + 1;
        for (int i = toc->first_track; i <= toc->last_track; i++) {
            album_track_t *trk = &out_meta->tracks[i];
            trk->number = i;
            snprintf(trk->title, sizeof(trk->title), "Track %02d", i);
            trk->duration_ms = toc->tracks[i].duration_ms;
        }
    } else {
        out_meta->track_count = 0;
    }
}

/* Parse a URL into host, port, path */
static int parse_url(const char *url, char *host, int max_host, int *port, char *path, int max_path, bool *is_https) {
    *is_https = false;
    *port = 80;

    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) {
        p += 7;
        *port = 80;
    } else if (strncmp(p, "https://", 8) == 0) {
        p += 8;
        *port = 443;
        *is_https = true;
    }

    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');

    if (colon && (!slash || colon < slash)) {
        int hlen = colon - p;
        if (hlen >= max_host) hlen = max_host - 1;
        strncpy(host, p, hlen);
        host[hlen] = '\0';
        *port = atoi(colon + 1);
    } else if (slash) {
        int hlen = slash - p;
        if (hlen >= max_host) hlen = max_host - 1;
        strncpy(host, p, hlen);
        host[hlen] = '\0';
    } else {
        strncpy(host, p, max_host - 1);
        host[max_host - 1] = '\0';
    }

    if (slash) {
        strncpy(path, slash, max_path - 1);
        path[max_path - 1] = '\0';
    } else {
        strncpy(path, "/", max_path - 1);
        path[max_path - 1] = '\0';
    }

    return 0;
}

/* Simple HTTP GET client */
static int http_get(const char *url, uint8_t **out_data, size_t *out_size, int redirect_count) {
    if (redirect_count > HTTP_MAX_REDIRECTS) {
        printf("[METADATA] Max HTTP redirects reached\n");
        return -1;
    }

    char host[256];
    int port = 80;
    char path[1024];
    bool is_https = false;

    parse_url(url, host, sizeof(host), &port, path, sizeof(path), &is_https);

    struct hostent *he = gethostbyname(host);
    if (!he || !he->h_addr_list[0]) {
        printf("[METADATA] DNS resolution failed for: %s\n", host);
        return -1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return -1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(port);
    memcpy(&saddr.sin_addr, he->h_addr_list[0], he->h_length);

    struct timeval tv;
    tv.tv_sec = 6;
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof(tv));

    if (connect(sock, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        close(sock);
        return -1;
    }

    char req[2048];
    int req_len = snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: %s\r\n"
        "Accept: */*\r\n"
        "Connection: close\r\n\r\n",
        path, host, HTTP_USER_AGENT);

    if (send(sock, req, req_len, 0) != req_len) {
        close(sock);
        return -1;
    }

    size_t cap = 65536;
    size_t total = 0;
    uint8_t *resp = malloc(cap);
    if (!resp) {
        close(sock);
        return -1;
    }

    uint8_t chunk[HTTP_RECV_BUF_SIZE];
    int n;
    while ((n = recv(sock, chunk, sizeof(chunk), 0)) > 0) {
        if (total + n > cap) {
            cap = (total + n) * 2;
            uint8_t *nresp = realloc(resp, cap);
            if (!nresp) {
                free(resp);
                close(sock);
                return -1;
            }
            resp = nresp;
        }
        memcpy(resp + total, chunk, n);
        total += n;
    }
    close(sock);

    if (total == 0) {
        free(resp);
        return -1;
    }

    /* Locate header end: \r\n\r\n */
    char *hdr_end = NULL;
    for (size_t i = 0; i + 3 < total; i++) {
        if (resp[i] == '\r' && resp[i+1] == '\n' && resp[i+2] == '\r' && resp[i+3] == '\n') {
            hdr_end = (char *)&resp[i];
            break;
        }
    }

    if (!hdr_end) {
        free(resp);
        return -1;
    }

    *hdr_end = '\0';
    char *body = hdr_end + 4;
    size_t body_len = total - (body - (char *)resp);

    /* Check HTTP Status Code */
    int status_code = 0;
    sscanf((char *)resp, "HTTP/%*s %d", &status_code);

    if (status_code == 301 || status_code == 302 || status_code == 303 || status_code == 307 || status_code == 308) {
        char *loc = strstr((char *)resp, "Location: ");
        if (!loc) loc = strstr((char *)resp, "location: ");
        if (loc) {
            loc += 10;
            char *loc_end = strstr(loc, "\r\n");
            if (loc_end) *loc_end = '\0';

            char next_url[1024];
            if (strncmp(loc, "http://", 7) == 0 || strncmp(loc, "https://", 8) == 0) {
                strncpy(next_url, loc, sizeof(next_url) - 1);
            } else {
                /* Relative redirect */
                snprintf(next_url, sizeof(next_url), "http://%s%s%s", host, loc[0] == '/' ? "" : "/", loc);
            }
            free(resp);
            return http_get(next_url, out_data, out_size, redirect_count + 1);
        }
    }

    if (status_code != 200) {
        printf("[METADATA] HTTP GET failed with status %d for %s\n", status_code, url);
        free(resp);
        return -1;
    }

    /* Allocate exact body buffer */
    uint8_t *result = malloc(body_len + 1);
    if (!result) {
        free(resp);
        return -1;
    }
    memcpy(result, body, body_len);
    result[body_len] = '\0';

    *out_data = result;
    *out_size = body_len;

    free(resp);
    return 0;
}

int metadata_fetch_disc(const cd_toc_t *toc, const char *disc_id, album_metadata_t *out_meta) {
    if (!out_meta) return -1;
    metadata_cleanup(out_meta);
    memset(out_meta, 0, sizeof(album_metadata_t));

    if (disc_id) strncpy(out_meta->disc_id, disc_id, sizeof(out_meta->disc_id) - 1);
    if (toc) discid_calculate_freedb(toc, out_meta->freedb_id, sizeof(out_meta->freedb_id));

    char url[512];
    snprintf(url, sizeof(url), "http://musicbrainz.org/ws/2/discid/%s?fmt=json&inc=recordings+artists", disc_id);

    uint8_t *json_data = NULL;
    size_t json_size = 0;
    int ret = http_get(url, &json_data, &json_size, 0);

    if (ret != 0 || !json_data) {
        printf("[METADATA] DiscID not found online or network unavailable. Falling back to TOC.\n");
        metadata_set_fallback(toc, disc_id, out_meta);
        return 0;
    }

    cJSON *root = cJSON_Parse((const char *)json_data);
    free(json_data);

    if (!root) {
        printf("[METADATA] JSON parse error\n");
        metadata_set_fallback(toc, disc_id, out_meta);
        return 0;
    }

    cJSON *releases = cJSON_GetObjectItem(root, "releases");
    if (!releases || !cJSON_IsArray(releases) || cJSON_GetArraySize(releases) == 0) {
        printf("[METADATA] No releases found in JSON response\n");
        cJSON_Delete(root);
        metadata_set_fallback(toc, disc_id, out_meta);
        return 0;
    }

    cJSON *rel = cJSON_GetArrayItem(releases, 0);

    cJSON *title = cJSON_GetObjectItem(rel, "title");
    if (title && cJSON_IsString(title)) {
        strncpy(out_meta->album_title, title->valuestring, sizeof(out_meta->album_title) - 1);
    } else {
        strncpy(out_meta->album_title, "Unknown Album", sizeof(out_meta->album_title) - 1);
    }

    cJSON *date = cJSON_GetObjectItem(rel, "date");
    if (date && cJSON_IsString(date)) {
        strncpy(out_meta->release_year, date->valuestring, sizeof(out_meta->release_year) - 1);
    } else {
        strncpy(out_meta->release_year, "----", sizeof(out_meta->release_year) - 1);
    }

    cJSON *mbid = cJSON_GetObjectItem(rel, "id");
    if (mbid && cJSON_IsString(mbid)) {
        strncpy(out_meta->release_mbid, mbid->valuestring, sizeof(out_meta->release_mbid) - 1);
    }

    cJSON *artists = cJSON_GetObjectItem(rel, "artist-credit");
    if (artists && cJSON_IsArray(artists) && cJSON_GetArraySize(artists) > 0) {
        cJSON *art0 = cJSON_GetArrayItem(artists, 0);
        cJSON *art_name = cJSON_GetObjectItem(art0, "name");
        if (art_name && cJSON_IsString(art_name)) {
            strncpy(out_meta->artist_name, art_name->valuestring, sizeof(out_meta->artist_name) - 1);
        }
    }
    if (strlen(out_meta->artist_name) == 0) {
        strncpy(out_meta->artist_name, "Unknown Artist", sizeof(out_meta->artist_name) - 1);
    }

    /* Extract Tracks */
    cJSON *media = cJSON_GetObjectItem(rel, "media");
    int count = 0;
    if (media && cJSON_IsArray(media) && cJSON_GetArraySize(media) > 0) {
        cJSON *med0 = cJSON_GetArrayItem(media, 0);
        cJSON *tracks = cJSON_GetObjectItem(med0, "tracks");
        if (tracks && cJSON_IsArray(tracks)) {
            int num_tracks = cJSON_GetArraySize(tracks);
            for (int i = 0; i < num_tracks && (i + 1) < CD_MAX_TRACKS; i++) {
                cJSON *titem = cJSON_GetArrayItem(tracks, i);
                album_track_t *dest = &out_meta->tracks[i + 1];
                dest->number = i + 1;

                cJSON *ttitle = cJSON_GetObjectItem(titem, "title");
                if (ttitle && cJSON_IsString(ttitle)) {
                    strncpy(dest->title, ttitle->valuestring, sizeof(dest->title) - 1);
                } else {
                    snprintf(dest->title, sizeof(dest->title), "Track %02d", i + 1);
                }

                cJSON *tlen = cJSON_GetObjectItem(titem, "length");
                if (tlen && cJSON_IsNumber(tlen)) {
                    dest->duration_ms = (uint32_t)tlen->valuedouble;
                } else if (toc && (i + 1) <= toc->last_track) {
                    dest->duration_ms = toc->tracks[i + 1].duration_ms;
                }
                count++;
            }
        }
    }

    out_meta->track_count = count;
    cJSON_Delete(root);

    /* Now load or download cover art */
    metadata_load_cover(out_meta);
    return 0;
}

int metadata_load_cover(album_metadata_t *meta) {
    if (!meta) return -1;

    snprintf(meta->cover_path, sizeof(meta->cover_path), "%s/%s.jpg", get_cache_dir(), meta->disc_id);

    /* 1. Check local cache file first */
    FILE *f = fopen(meta->cover_path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0) {
            meta->cover_image_bytes = malloc(sz);
            if (meta->cover_image_bytes && fread(meta->cover_image_bytes, 1, sz, f) == (size_t)sz) {
                meta->cover_image_size = (int)sz;
                meta->cover_loaded = true;
                fclose(f);
                printf("[METADATA] Cover art loaded from cache: %s (%ld bytes)\n", meta->cover_path, sz);
                return 0;
            }
            if (meta->cover_image_bytes) {
                free(meta->cover_image_bytes);
                meta->cover_image_bytes = NULL;
            }
        }
        fclose(f);
    }

    /* 2. Download from Cover Art Archive if MBID is available */
    if (strlen(meta->release_mbid) > 0) {
        char cover_url[512];
        snprintf(cover_url, sizeof(cover_url), "http://coverartarchive.org/release/%s/front-500", meta->release_mbid);

        uint8_t *img_data = NULL;
        size_t img_size = 0;
        int ret = http_get(cover_url, &img_data, &img_size, 0);
        if (ret == 0 && img_data && img_size > 0) {
            meta->cover_image_bytes = img_data;
            meta->cover_image_size = (int)img_size;
            meta->cover_loaded = true;

            /* Save to cache file */
            ensure_cache_dir();
            FILE *cf = fopen(meta->cover_path, "wb");
            if (cf) {
                fwrite(img_data, 1, img_size, cf);
                fclose(cf);
                printf("[METADATA] Cover art downloaded and cached to: %s (%zu bytes)\n", meta->cover_path, img_size);
            }
            return 0;
        }
    }

    meta->cover_loaded = false;
    return -1;
}
