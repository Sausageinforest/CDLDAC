#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#include "cd_drive.h"
#include "discid.h"
#include "metadata.h"
#include "visualizer.h"
#include "cJSON.h"

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); \
        g_tests_passed++; \
    } else { \
        printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
        g_tests_failed++; \
    } \
} while(0)

/* Test 1: CD Drive Time/Sector conversions */
static void test_cd_conversions(void) {
    printf("Running Test: CD Time/Sector conversions...\n");

    /* MSF 00:02:00 should be LBA 0 (after 150 frames pregap) */
    uint32_t lba0 = cd_msf_to_lba(0, 2, 0);
    TEST_ASSERT(lba0 == 0, "cd_msf_to_lba(0, 2, 0) == 0");

    /* MSF 01:00:00 = 60 * 75 = 4500 frames -> LBA 4350 */
    uint32_t lba1 = cd_msf_to_lba(1, 0, 0);
    TEST_ASSERT(lba1 == 4350, "cd_msf_to_lba(1, 0, 0) == 4350");

    /* Convert back */
    uint8_t m = 0, s = 0, f = 0;
    cd_lba_to_msf(0, &m, &s, &f);
    TEST_ASSERT(m == 0 && s == 2 && f == 0, "cd_lba_to_msf(0) -> 00:02:00");

    cd_lba_to_msf(4350, &m, &s, &f);
    TEST_ASSERT(m == 1 && s == 0 && f == 0, "cd_lba_to_msf(4350) -> 01:00:00");
}

/* Test 2: DiscID MusicBrainz and FreeDB calculation */
static void test_discid(void) {
    printf("Running Test: DiscID Calculation...\n");

    cd_toc_t toc;
    memset(&toc, 0, sizeof(toc));
    toc.first_track = 1;
    toc.last_track = 3;
    toc.leadout_lba = 150000 - 150; /* so leadout_sector = 150000 */
    toc.tracks[1].start_lba = 150 - 150; /* sector 150 */
    toc.tracks[2].start_lba = 50000 - 150; /* sector 50000 */
    toc.tracks[3].start_lba = 100000 - 150; /* sector 100000 */

    char mb_discid[64] = {0};
    int ret = discid_calculate_musicbrainz(&toc, mb_discid, sizeof(mb_discid));
    TEST_ASSERT(ret == 0, "discid_calculate_musicbrainz returns 0");
    TEST_ASSERT(strlen(mb_discid) == 28, "MusicBrainz DiscID is exactly 28 characters");

    /* Verify against reference vector calculated earlier */
    TEST_ASSERT(strcmp(mb_discid, "gRPeB3HB_qVr.TcdavPTyRkCXqY-") == 0,
                "MusicBrainz DiscID matches expected hash gRPeB3HB_qVr.TcdavPTyRkCXqY-");

    char freedb_id[16] = {0};
    ret = discid_calculate_freedb(&toc, freedb_id, sizeof(freedb_id));
    TEST_ASSERT(ret == 0, "discid_calculate_freedb returns 0");
    TEST_ASSERT(strlen(freedb_id) == 8, "FreeDB DiscID is 8 characters");
    printf("    Generated FreeDB ID: %s\n", freedb_id);
}

/* Test 3: Metadata JSON Parsing */
static void test_json_parsing(void) {
    printf("Running Test: cJSON Metadata Parsing...\n");

    const char *sample_json =
        "{\"releases\":[{"
        "\"id\":\"8f468f36-8c7e-4fc1-9166-50664d267127\","
        "\"title\":\"Dummy\","
        "\"date\":\"1994-10-17\","
        "\"artist-credit\":[{\"name\":\"Portishead\"}],"
        "\"media\":[{\"tracks\":["
        "{\"number\":\"1\",\"title\":\"Mysterons\",\"length\":306200},"
        "{\"number\":\"2\",\"title\":\"Sour Times\",\"length\":254000}"
        "]}]"
        "}]}";

    cJSON *root = cJSON_Parse(sample_json);
    TEST_ASSERT(root != NULL, "cJSON_Parse successfully parsed sample payload");

    cJSON *rel = cJSON_GetArrayItem(cJSON_GetObjectItem(root, "releases"), 0);
    cJSON *title = cJSON_GetObjectItem(rel, "title");
    TEST_ASSERT(strcmp(title->valuestring, "Dummy") == 0, "Release title is 'Dummy'");

    cJSON *artist = cJSON_GetArrayItem(cJSON_GetObjectItem(rel, "artist-credit"), 0);
    cJSON *art_name = cJSON_GetObjectItem(artist, "name");
    TEST_ASSERT(strcmp(art_name->valuestring, "Portishead") == 0, "Artist is 'Portishead'");

    cJSON *tracks = cJSON_GetObjectItem(cJSON_GetArrayItem(cJSON_GetObjectItem(rel, "media"), 0), "tracks");
    TEST_ASSERT(cJSON_GetArraySize(tracks) == 2, "Found 2 tracks in JSON");

    cJSON_Delete(root);
}

/* Test 4: Visualizer FFT and Peak Decay */
static void test_visualizer(void) {
    printf("Running Test: Visualizer FFT & Frequency Bands...\n");

    visualizer_t vis;
    visualizer_init(&vis);

    /* Generate 1024 samples of a 1000 Hz sine wave */
    float samples[VIS_FFT_SIZE];
    for (int i = 0; i < VIS_FFT_SIZE; i++) {
        samples[i] = sinf(2.0f * (float)3.141592653589793 * 1000.0f * (float)i / 44100.0f);
    }

    visualizer_update(&vis, samples, VIS_FFT_SIZE);

    /* Check that at least one band is active */
    bool has_active_band = false;
    for (int b = 0; b < VIS_NUM_BANDS; b++) {
        if (vis.band_values[b] > 0.05f) {
            has_active_band = true;
            break;
        }
    }
    TEST_ASSERT(has_active_band, "FFT visualizer detected 1000Hz tone in frequency bands");

    /* Peak values should be >= band values */
    bool peaks_valid = true;
    for (int b = 0; b < VIS_NUM_BANDS; b++) {
        if (vis.peak_values[b] < vis.band_values[b]) {
            peaks_valid = false;
            break;
        }
    }
    TEST_ASSERT(peaks_valid, "Peak indicators are above or equal to band values");
}

int main(void) {
    printf("===================================================\n");
    printf("       PS3 AUDIOPHILE CD PLAYER TEST SUITE         \n");
    printf("===================================================\n");

    test_cd_conversions();
    test_discid();
    test_json_parsing();
    test_visualizer();

    printf("===================================================\n");
    printf("Results: %d Passed, %d Failed\n", g_tests_passed, g_tests_failed);
    printf("===================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
