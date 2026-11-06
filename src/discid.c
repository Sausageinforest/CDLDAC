#include "discid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Standalone SHA-1 Implementation (RFC 3174) --- */

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} SHA1_CTX;

#define SHA1_ROL(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))

static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    uint32_t w[80];

    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)buffer[i * 4] << 24) |
               ((uint32_t)buffer[i * 4 + 1] << 16) |
               ((uint32_t)buffer[i * 4 + 2] << 8) |
               ((uint32_t)buffer[i * 4 + 3]);
    }
    for (int i = 16; i < 80; i++) {
        w[i] = SHA1_ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = SHA1_ROL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = SHA1_ROL(b, 30);
        b = a;
        a = temp;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

static void sha1_init(SHA1_CTX *ctx) {
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count[0] = ctx->count[1] = 0;
}

static void sha1_update(SHA1_CTX *ctx, const uint8_t *data, size_t len) {
    uint32_t j = (ctx->count[0] >> 3) & 63;
    if ((ctx->count[0] += (uint32_t)(len << 3)) < (uint32_t)(len << 3)) {
        ctx->count[1]++;
    }
    ctx->count[1] += (uint32_t)(len >> 29);

    size_t i = 0;
    if ((j + len) > 63) {
        memcpy(&ctx->buffer[j], data, (i = 64 - j));
        sha1_transform(ctx->state, ctx->buffer);
        for (; i + 63 < len; i += 64) {
            sha1_transform(ctx->state, &data[i]);
        }
        j = 0;
    }
    memcpy(&ctx->buffer[j], &data[i], len - i);
}

static void sha1_final(uint8_t digest[20], SHA1_CTX *ctx) {
    uint8_t finalcount[8];
    for (int i = 0; i < 8; i++) {
        finalcount[i] = (uint8_t)((ctx->count[(i >= 4 ? 0 : 1)] >> ((3 - (i & 3)) * 8)) & 255);
    }
    uint8_t c = 0200;
    sha1_update(ctx, &c, 1);
    while ((ctx->count[0] & 504) != 448) {
        c = 0000;
        sha1_update(ctx, &c, 1);
    }
    sha1_update(ctx, finalcount, 8);
    for (int i = 0; i < 20; i++) {
        digest[i] = (uint8_t)((ctx->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
    }
}

void sha1_hash(const uint8_t *data, size_t len, uint8_t digest[20]) {
    SHA1_CTX ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(digest, &ctx);
}

/* --- Standalone MD5 Implementation (RFC 1321) --- */

typedef struct {
    uint32_t state[4];
    uint32_t count[2];
    uint8_t buffer[64];
} MD5_CTX;

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))
#define MD5_ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define MD5_FF(a, b, c, d, x, s, ac) { (a) += MD5_F((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = MD5_ROL((a), (s)); (a) += (b); }
#define MD5_GG(a, b, c, d, x, s, ac) { (a) += MD5_G((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = MD5_ROL((a), (s)); (a) += (b); }
#define MD5_HH(a, b, c, d, x, s, ac) { (a) += MD5_H((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = MD5_ROL((a), (s)); (a) += (b); }
#define MD5_II(a, b, c, d, x, s, ac) { (a) += MD5_I((b), (c), (d)) + (x) + (uint32_t)(ac); (a) = MD5_ROL((a), (s)); (a) += (b); }

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
    for (int i = 0; i < 16; i++) {
        x[i] = ((uint32_t)block[i*4]) | (((uint32_t)block[i*4+1]) << 8) |
               (((uint32_t)block[i*4+2]) << 16) | (((uint32_t)block[i*4+3]) << 24);
    }

    /* Round 1 */
    MD5_FF(a, b, c, d, x[ 0], 7, 0xd76aa478); MD5_FF(d, a, b, c, x[ 1], 12, 0xe8c7b756);
    MD5_FF(c, d, a, b, x[ 2], 17, 0x242070db); MD5_FF(b, c, d, a, x[ 3], 22, 0xc1bdceee);
    MD5_FF(a, b, c, d, x[ 4], 7, 0xf57c0faf); MD5_FF(d, a, b, c, x[ 5], 12, 0x4787c62a);
    MD5_FF(c, d, a, b, x[ 6], 17, 0xa8304613); MD5_FF(b, c, d, a, x[ 7], 22, 0xfd469501);
    MD5_FF(a, b, c, d, x[ 8], 7, 0x698098d8); MD5_FF(d, a, b, c, x[ 9], 12, 0x8b44f7af);
    MD5_FF(c, d, a, b, x[10], 17, 0xffff5bb1); MD5_FF(b, c, d, a, x[11], 22, 0x895cd7be);
    MD5_FF(a, b, c, d, x[12], 7, 0x6b901122); MD5_FF(d, a, b, c, x[13], 12, 0xfd987193);
    MD5_FF(c, d, a, b, x[14], 17, 0xa679438e); MD5_FF(b, c, d, a, x[15], 22, 0x49b40821);

    /* Round 2 */
    MD5_GG(a, b, c, d, x[ 1], 5, 0xf61e2562); MD5_GG(d, a, b, c, x[ 6], 9, 0xc040b340);
    MD5_GG(c, d, a, b, x[11], 14, 0x265e5a51); MD5_GG(b, c, d, a, x[ 0], 20, 0xe9b6c7aa);
    MD5_GG(a, b, c, d, x[ 5], 5, 0xd62f105d); MD5_GG(d, a, b, c, x[10], 9, 0x02441453);
    MD5_GG(c, d, a, b, x[15], 14, 0xd8a1e681); MD5_GG(b, c, d, a, x[ 4], 20, 0xe7d3fbc8);
    MD5_GG(a, b, c, d, x[ 9], 5, 0x21e1cde6); MD5_GG(d, a, b, c, x[14], 9, 0xc33707d6);
    MD5_GG(c, d, a, b, x[ 3], 14, 0xf4d50d87); MD5_GG(b, c, d, a, x[ 8], 20, 0x455a14ed);
    MD5_GG(a, b, c, d, x[13], 5, 0xa9e3e905); MD5_GG(d, a, b, c, x[ 2], 9, 0xfcefa3f8);
    MD5_GG(c, d, a, b, x[ 7], 14, 0x676f02d9); MD5_GG(b, c, d, a, x[12], 20, 0x8d2a4c8a);

    /* Round 3 */
    MD5_HH(a, b, c, d, x[ 5], 4, 0xfffa3942); MD5_HH(d, a, b, c, x[ 8], 11, 0x8771f681);
    MD5_HH(c, d, a, b, x[11], 16, 0x6d9d6122); MD5_HH(b, c, d, a, x[14], 23, 0xfde5380c);
    MD5_HH(a, b, c, d, x[ 1], 4, 0xa4beea44); MD5_HH(d, a, b, c, x[ 4], 11, 0x4bdecfa9);
    MD5_HH(c, d, a, b, x[ 7], 16, 0xf6bb4b60); MD5_HH(b, c, d, a, x[10], 23, 0xbebfbc70);
    MD5_HH(a, b, c, d, x[13], 4, 0x289b7ec6); MD5_HH(d, a, b, c, x[ 0], 11, 0xeaa127fa);
    MD5_HH(c, d, a, b, x[ 3], 16, 0xd4ef3085); MD5_HH(b, c, d, a, x[ 6], 23, 0x04881d05);
    MD5_HH(a, b, c, d, x[ 9], 4, 0xd9d4d039); MD5_HH(d, a, b, c, x[12], 11, 0xe6db99e5);
    MD5_HH(c, d, a, b, x[15], 16, 0x1fa27cf8); MD5_HH(b, c, d, a, x[ 2], 23, 0xc4ac5665);

    /* Round 4 */
    MD5_II(a, b, c, d, x[ 0], 6, 0xf4292244); MD5_II(d, a, b, c, x[ 7], 10, 0x432aff97);
    MD5_II(c, d, a, b, x[14], 15, 0xab9423a7); MD5_II(b, c, d, a, x[ 5], 21, 0xfc93a039);
    MD5_II(a, b, c, d, x[12], 6, 0x655b59c3); MD5_II(d, a, b, c, x[ 3], 10, 0x8f0ccc92);
    MD5_II(c, d, a, b, x[10], 15, 0xffeff47d); MD5_II(b, c, d, a, x[ 1], 21, 0x85845dd1);
    MD5_II(a, b, c, d, x[ 8], 6, 0x6fa87e4f); MD5_II(d, a, b, c, x[15], 10, 0xfe2ce6e0);
    MD5_II(c, d, a, b, x[ 6], 15, 0xa3014314); MD5_II(b, c, d, a, x[13], 21, 0x4e0811a1);
    MD5_II(a, b, c, d, x[ 4], 6, 0xf7537e82); MD5_II(d, a, b, c, x[11], 10, 0xbd3af235);
    MD5_II(c, d, a, b, x[ 2], 15, 0x2ad7d2bb); MD5_II(b, c, d, a, x[ 9], 21, 0xeb86d391);

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void md5_init(MD5_CTX *ctx) {
    ctx->count[0] = ctx->count[1] = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

static void md5_update(MD5_CTX *ctx, const uint8_t *input, size_t len) {
    uint32_t i = 0, index = (ctx->count[0] >> 3) & 0x3F;
    if ((ctx->count[0] += (uint32_t)(len << 3)) < (uint32_t)(len << 3)) ctx->count[1]++;
    ctx->count[1] += (uint32_t)(len >> 29);
    uint32_t partLen = 64 - index;
    if (len >= partLen) {
        memcpy(&ctx->buffer[index], input, partLen);
        md5_transform(ctx->state, ctx->buffer);
        for (i = partLen; i + 63 < len; i += 64) md5_transform(ctx->state, &input[i]);
        index = 0;
    }
    memcpy(&ctx->buffer[index], &input[i], len - i);
}

static void md5_final(uint8_t digest[16], MD5_CTX *ctx) {
    uint8_t bits[8];
    for (int i = 0; i < 4; i++) {
        bits[i] = (uint8_t)((ctx->count[0] >> (i * 8)) & 0xFF);
        bits[i + 4] = (uint8_t)((ctx->count[1] >> (i * 8)) & 0xFF);
    }
    uint32_t index = (ctx->count[0] >> 3) & 0x3F;
    uint32_t padLen = (index < 56) ? (56 - index) : (120 - index);
    static const uint8_t PADDING[64] = { 0x80 };
    md5_update(ctx, PADDING, padLen);
    md5_update(ctx, bits, 8);
    for (int i = 0; i < 4; i++) {
        digest[i*4]   = (uint8_t)(ctx->state[i] & 0xFF);
        digest[i*4+1] = (uint8_t)((ctx->state[i] >> 8) & 0xFF);
        digest[i*4+2] = (uint8_t)((ctx->state[i] >> 16) & 0xFF);
        digest[i*4+3] = (uint8_t)((ctx->state[i] >> 24) & 0xFF);
    }
}

void md5_hash(const uint8_t *data, size_t len, uint8_t digest[16]) {
    MD5_CTX ctx;
    md5_init(&ctx);
    md5_update(&ctx, data, len);
    md5_final(digest, &ctx);
}

/* --- Base64 URL-Safe Encoder (RFC 4648 with MusicBrainz substitutions: + -> ., / -> _, = -> -) --- */

static const char base64_mb_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";

static void base64_mb_encode(const uint8_t *src, size_t src_len, char *dst) {
    size_t i = 0, j = 0;
    while (i + 2 < src_len) {
        uint32_t triple = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8) | (uint32_t)src[i + 2];
        dst[j++] = base64_mb_table[(triple >> 18) & 0x3F];
        dst[j++] = base64_mb_table[(triple >> 12) & 0x3F];
        dst[j++] = base64_mb_table[(triple >> 6) & 0x3F];
        dst[j++] = base64_mb_table[triple & 0x3F];
        i += 3;
    }
    if (i < src_len) {
        if (src_len - i == 1) {
            uint32_t triple = (uint32_t)src[i] << 16;
            dst[j++] = base64_mb_table[(triple >> 18) & 0x3F];
            dst[j++] = base64_mb_table[(triple >> 12) & 0x3F];
            dst[j++] = '-';
            dst[j++] = '-';
        } else if (src_len - i == 2) {
            uint32_t triple = ((uint32_t)src[i] << 16) | ((uint32_t)src[i + 1] << 8);
            dst[j++] = base64_mb_table[(triple >> 18) & 0x3F];
            dst[j++] = base64_mb_table[(triple >> 12) & 0x3F];
            dst[j++] = base64_mb_table[(triple >> 6) & 0x3F];
            dst[j++] = '-';
        }
    }
    dst[j] = '\0';
}

/* --- MusicBrainz DiscID Calculation --- */

int discid_calculate_musicbrainz(const cd_toc_t *toc, char *out_discid, size_t max_len) {
    if (!toc || !out_discid || max_len <= DISCID_MB_LENGTH) return -1;

    if (toc->first_track < 1 || toc->last_track > 99 || toc->first_track > toc->last_track) {
        return -1;
    }

    /*
     * Build TOC string (804 characters):
     * 2 hex digits for first track (%02X)
     * 2 hex digits for last track (%02X)
     * 8 hex digits for leadout sector (%08X, track_offsets[0] = leadout_lba + 150)
     * 99 x 8 hex digits for track offsets (%08X, track_offsets[1..99] = track_lba + 150)
     */
    char toc_str[805];
    char *ptr = toc_str;

    ptr += sprintf(ptr, "%02X%02X", toc->first_track, toc->last_track);

    /* Leadout sector */
    uint32_t leadout_sector = toc->leadout_lba + CD_PREGAP_FRAMES;
    ptr += sprintf(ptr, "%08X", leadout_sector);

    /* Track offsets 1 to 99 */
    for (int i = 1; i < 100; i++) {
        if (i >= toc->first_track && i <= toc->last_track) {
            uint32_t track_sector = toc->tracks[i].start_lba + CD_PREGAP_FRAMES;
            ptr += sprintf(ptr, "%08X", track_sector);
        } else {
            ptr += sprintf(ptr, "%08X", 0);
        }
    }

    /* Calculate SHA-1 digest */
    uint8_t digest[20];
    sha1_hash((const uint8_t *)toc_str, 804, digest);

    /* Encode in Base64 URL-safe */
    base64_mb_encode(digest, 20, out_discid);

    return 0;
}

/* --- FreeDB / CDDB DiscID Calculation --- */

int discid_calculate_freedb(const cd_toc_t *toc, char *out_freedb, size_t max_len) {
    if (!toc || !out_freedb || max_len < 9) return -1;

    if (toc->first_track < 1 || toc->last_track > 99 || toc->first_track > toc->last_track) {
        return -1;
    }

    int sum_digits = 0;
    for (int i = toc->first_track; i <= toc->last_track; i++) {
        uint32_t sec = (toc->tracks[i].start_lba + CD_PREGAP_FRAMES) / CD_FRAMES_PER_SEC;
        while (sec > 0) {
            sum_digits += sec % 10;
            sec /= 10;
        }
    }

    uint32_t leadout_sec = (toc->leadout_lba + CD_PREGAP_FRAMES) / CD_FRAMES_PER_SEC;
    uint32_t first_sec = (toc->tracks[toc->first_track].start_lba + CD_PREGAP_FRAMES) / CD_FRAMES_PER_SEC;
    uint32_t total_sec = (leadout_sec >= first_sec) ? (leadout_sec - first_sec) : 0;

    uint32_t discid = ((uint32_t)(sum_digits % 0xFF) << 24) |
                      ((uint32_t)(total_sec & 0xFFFF) << 8) |
                      (uint32_t)(toc->last_track);

    snprintf(out_freedb, max_len, "%08x", discid);
    return 0;
}
