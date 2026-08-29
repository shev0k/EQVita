#define _XOPEN_SOURCE 700

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../app/media_browser.h"

static void test_supported_music_extensions(void)
{
    assert(eqvita_media_browser_is_supported_file("ux0:/music/song.ogg"));
    assert(eqvita_media_browser_is_supported_file("ux0:/music/song.OGG"));
    assert(eqvita_media_browser_is_supported_file("ux0:/music/song.mp3"));
    assert(eqvita_media_browser_is_supported_file("ux0:/music/song.WAV"));
    assert(!eqvita_media_browser_is_supported_file("ux0:/music/song.flac"));
    assert(!eqvita_media_browser_is_supported_file("ux0:/music/song.txt"));
    assert(!eqvita_media_browser_is_supported_file("ux0:/music/song"));
    assert(!eqvita_media_browser_is_supported_file(NULL));
    assert(eqvita_media_browser_is_supported_file_for_filter(
        "ux0:/configs/headphones.txt", EQVITA_MEDIA_FILTER_EQUALIZER_APO));
    assert(eqvita_media_browser_is_supported_file_for_filter(
        "ux0:/configs/HEADPHONES.TXT", EQVITA_MEDIA_FILTER_EQUALIZER_APO));
    assert(!eqvita_media_browser_is_supported_file_for_filter(
        "ux0:/configs/headphones.wav", EQVITA_MEDIA_FILTER_EQUALIZER_APO));
}

static void test_safe_path_join(void)
{
    char out[EQVITA_MEDIA_MAX_PATH];

    assert(eqvita_media_browser_join_path(out, sizeof(out), "ux0:", "music") == 0);
    assert(strcmp(out, "ux0:music") == 0);

    assert(eqvita_media_browser_join_path(out, sizeof(out), "ux0:music", "song.ogg") == 0);
    assert(strcmp(out, "ux0:music/song.ogg") == 0);

    assert(eqvita_media_browser_join_path(out, sizeof(out), "ux0:/music/", "song.ogg") == 0);
    assert(strcmp(out, "ux0:/music/song.ogg") == 0);

    assert(eqvita_media_browser_join_path(out, 8, "ux0:/music", "song.ogg") < 0);
    assert(eqvita_media_browser_join_path(NULL, sizeof(out), "ux0:/music", "song.ogg") < 0);
    assert(eqvita_media_browser_join_path(out, sizeof(out), NULL, "song.ogg") < 0);
    assert(eqvita_media_browser_join_path(out, sizeof(out), "ux0:/music", NULL) < 0);
}

static void test_file_name_from_path(void)
{
    assert(strcmp(eqvita_media_browser_file_name("ux0:music/song.ogg"), "song.ogg") == 0);
    assert(strcmp(eqvita_media_browser_file_name("uma0:music/song.mp3"), "song.mp3") == 0);
    assert(strcmp(eqvita_media_browser_file_name("song.wav"), "song.wav") == 0);
    assert(strcmp(eqvita_media_browser_file_name(NULL), "") == 0);
}

static void test_parent_path(void)
{
    char out[EQVITA_MEDIA_MAX_PATH];

    assert(eqvita_media_browser_parent_path(out, sizeof(out), "ux0:music/song.ogg") == 0);
    assert(strcmp(out, "ux0:music/") == 0);

    assert(eqvita_media_browser_parent_path(out, sizeof(out), "ux0:music/") == 0);
    assert(strcmp(out, "ux0:") == 0);

    assert(eqvita_media_browser_parent_path(out, sizeof(out), "ux0:") < 0);
    assert(eqvita_media_browser_parent_path(out, sizeof(out), "ux0:/") < 0);
}

static void test_root_path_detection(void)
{
    assert(eqvita_media_browser_is_root_path("ux0:"));
    assert(eqvita_media_browser_is_root_path("ux0:/"));
    assert(eqvita_media_browser_is_root_path("ur0:"));
    assert(!eqvita_media_browser_is_root_path("ux0:music/"));
    assert(!eqvita_media_browser_is_root_path("Storage"));
    assert(!eqvita_media_browser_is_root_path(NULL));
}

static void test_bounded_peq_paths(void)
{
    assert(eqvita_media_browser_paths_equal("ur0:data/eqvita/peq",
                                            "ur0:data/eqvita/peq/"));
    assert(eqvita_media_browser_paths_equal("ur0:data/eqvita/peq/sub/",
                                            "ur0:data/eqvita/peq/sub"));
    assert(!eqvita_media_browser_paths_equal("ur0:data/eqvita/peq",
                                             "ur0:data/eqvita/peq2"));
    assert(eqvita_media_browser_path_is_within("ur0:data/eqvita/peq",
                                               "ur0:data/eqvita/peq"));
    assert(eqvita_media_browser_path_is_within("ur0:data/eqvita/peq/sub/",
                                               "ur0:data/eqvita/peq"));
    assert(!eqvita_media_browser_path_is_within("ur0:data/eqvita/peq2",
                                                "ur0:data/eqvita/peq"));
    assert(!eqvita_media_browser_path_is_within("ur0:data/eqvita",
                                                "ur0:data/eqvita/peq"));
}

static void touch_file(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file != NULL);
    assert(fclose(file) == 0);
}

static void test_bounded_peq_listing(void)
{
    char root_template[] = "/tmp/eqvita-peq-browser-XXXXXX";
    char *root = mkdtemp(root_template);
    char txt_path[EQVITA_MEDIA_MAX_PATH];
    char upper_path[EQVITA_MEDIA_MAX_PATH];
    char ignored_path[EQVITA_MEDIA_MAX_PATH];
    char sub_path[EQVITA_MEDIA_MAX_PATH];
    eqvita_media_listing_t listing;
    int txt_count = 0;
    int directory_count = 0;

    assert(root != NULL);
    assert(eqvita_media_browser_join_path(txt_path, sizeof(txt_path), root, "speaker.txt") == 0);
    assert(eqvita_media_browser_join_path(upper_path, sizeof(upper_path), root, "Headphones.TXT") == 0);
    assert(eqvita_media_browser_join_path(ignored_path, sizeof(ignored_path), root, "ignore.wav") == 0);
    assert(eqvita_media_browser_join_path(sub_path, sizeof(sub_path), root, "sub") == 0);
    touch_file(txt_path);
    touch_file(upper_path);
    touch_file(ignored_path);
    assert(mkdir(sub_path, 0777) == 0);

    assert(eqvita_media_browser_read_dir_filtered_at_root(
               &listing, root, EQVITA_MEDIA_FILTER_EQUALIZER_APO, root) == 3);
    assert(eqvita_media_browser_paths_equal(listing.path, root));
    for (int i = 0; i < listing.count; ++i) {
        assert(listing.entries[i].kind != EQVITA_MEDIA_ENTRY_PARENT);
        if (listing.entries[i].kind == EQVITA_MEDIA_ENTRY_FILE) {
            txt_count++;
        } else if (listing.entries[i].kind == EQVITA_MEDIA_ENTRY_DIRECTORY) {
            directory_count++;
        }
    }
    assert(txt_count == 2);
    assert(directory_count == 1);
    assert(eqvita_media_browser_read_dir_filtered_at_root(
               &listing, "/tmp", EQVITA_MEDIA_FILTER_EQUALIZER_APO, root) < 0);

    assert(unlink(txt_path) == 0);
    assert(unlink(upper_path) == 0);
    assert(unlink(ignored_path) == 0);
    assert(rmdir(sub_path) == 0);
    assert(rmdir(root) == 0);
}

static void test_roots_include_music_shortcut(void)
{
    eqvita_media_listing_t listing;
    int found_ux0 = 0;
    int found_music = 0;

    assert(eqvita_media_browser_read_roots(&listing) >= 2);
    for (int i = 0; i < listing.count; ++i) {
        if (strcmp(listing.entries[i].name, "ux0:") == 0 &&
            strcmp(listing.entries[i].path, "ux0:") == 0) {
            found_ux0 = 1;
        }
        if (strcmp(listing.entries[i].name, "ux0:music/") == 0 &&
            strcmp(listing.entries[i].path, "ux0:music/") == 0) {
            found_music = 1;
        }
    }
    assert(found_ux0);
    assert(found_music);
}

int main(void)
{
    test_supported_music_extensions();
    test_safe_path_join();
    test_file_name_from_path();
    test_parent_path();
    test_root_path_detection();
    test_bounded_peq_paths();
    test_bounded_peq_listing();
    test_roots_include_music_shortcut();
    puts("media_browser tests passed");
    return 0;
}
