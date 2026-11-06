#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <openssl/sha.h>
#include <openssl/aes.h>

static const uint8_t retail_pkg_aes_key[16] = {
    0x2E, 0x7B, 0x71, 0xD7, 0xC9, 0xC9, 0xA1, 0x4E,
    0xA3, 0x22, 0x1F, 0x18, 0x88, 0x28, 0xB8, 0xF8
};

typedef struct {
    uint32_t magic;
    uint32_t debugFlag;
    uint32_t infoOffset;
    uint32_t unknown1;
    uint32_t headSize;
    uint32_t itemCount;
    uint64_t packageSize;
    uint64_t dataOffset;
    uint64_t dataSize;
} pkg_header;

static inline uint64_t get_u64(const void* vd) {
    const uint8_t *d = (const uint8_t*)vd;
    return ((uint64_t)d[0]<<56) | ((uint64_t)d[1]<<48) | ((uint64_t)d[2]<<40) | ((uint64_t)d[3]<<32) |
           ((uint64_t)d[4]<<24) | ((uint64_t)d[5]<<16) | ((uint64_t)d[6]<<8)  | (uint64_t)d[7];
}

static inline void set_u64(void* vd, uint64_t v) {
    uint8_t *d = (uint8_t*)vd;
    d[0] = v>>56; d[1] = v>>48; d[2] = v>>40; d[3] = v>>32;
    d[4] = v>>24; d[5] = v>>16; d[6] = v>>8;  d[7] = v;
}

int finalize_file(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("Skipping %s (cannot open)\n", filename);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    long nlen = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (nlen < 0x140) {
        printf("Error: %s is too small\n", filename);
        fclose(f);
        return -1;
    }

    uint8_t *data = (uint8_t*)malloc(nlen);
    if (!data) {
        printf("Error: out of memory\n");
        fclose(f);
        return -1;
    }
    if (fread(data, 1, nlen, f) != (size_t)nlen) {
        printf("Error: failed reading %s\n", filename);
        free(data);
        fclose(f);
        return -1;
    }
    fclose(f);

    if (data[4] == 0x80) {
        printf("%s is already retail finalized (byte 4 == 0x80). Skipping.\n", filename);
        free(data);
        return 0;
    }

    pkg_header *header = (pkg_header *)data;
    uint64_t data_offset = get_u64(&(header->dataOffset));
    uint64_t data_size = get_u64(&(header->dataSize));
    printf("Finalizing %s (data_offset: %llu, data_size: %llu, nlen: %ld)...\n",
           filename, (unsigned long long)data_offset, (unsigned long long)data_size, nlen);

    if (data_offset >= (uint64_t)nlen) {
        printf("Error: invalid data offset\n");
        free(data);
        return -1;
    }

    // Decrypt debug
    uint8_t sha_crap[0x40];
    memset(sha_crap, 0, 0x40);
    memcpy(sha_crap, &data[0x60], 8);
    memcpy(sha_crap+0x8, &data[0x60], 8);
    memcpy(sha_crap+0x10, &data[0x68], 8);
    memcpy(sha_crap+0x18, &data[0x68], 8);

    uint64_t end_data = data_offset + data_size;
    if (end_data > (uint64_t)nlen) end_data = nlen;

    for (uint64_t dptr = data_offset; dptr < end_data; dptr += 0x10) {
        uint8_t hash[SHA_DIGEST_LENGTH];
        SHA1(sha_crap, 0x40, hash);
        size_t chunk = (end_data - dptr < 0x10) ? (end_data - dptr) : 0x10;
        for (size_t i = 0; i < chunk; i++) {
            data[dptr + i] ^= hash[i];
        }
        set_u64(sha_crap + 0x38, get_u64(sha_crap + 0x38) + 1);
    }

    // Re-encrypt retail AES CTR
    uint8_t pkg_iv[0x10];
    memcpy(pkg_iv, &data[0x70], 0x10);

    AES_KEY aes_key;
    AES_set_encrypt_key(retail_pkg_aes_key, 128, &aes_key);

    int num = 0;
    uint8_t ecount_buf[0x10];
    memset(ecount_buf, 0, 0x10);
    AES_ctr128_encrypt(&data[data_offset], &data[data_offset], end_data - data_offset, &aes_key, pkg_iv, ecount_buf, &num);

    // Set finalize flag
    data[4] = 0x80;
    if (data_offset + data_size < (uint64_t)nlen) {
        size_t zero_len = 0x60;
        if (data_offset + data_size + zero_len > (uint64_t)nlen)
            zero_len = nlen - (data_offset + data_size);
        memset(&data[data_offset + data_size], 0, zero_len);
    }

    // SHA1 at trailer
    if (nlen >= 0x20) {
        SHA1(data, nlen - 0x20, &data[nlen - 0x20]);
    }

    FILE *g = fopen(filename, "wb");
    if (!g) {
        printf("Error: cannot write %s\n", filename);
        free(data);
        return -1;
    }
    fwrite(data, 1, nlen, g);
    fclose(g);
    free(data);
    printf("Successfully finalized %s\n", filename);
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <file.pkg> ...\n", argv[0]);
        return 1;
    }
    for (int i = 1; i < argc; i++) {
        finalize_file(argv[i]);
    }
    return 0;
}
