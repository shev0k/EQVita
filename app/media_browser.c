#include "media_browser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef EQVITA_HOST_TESTS
#include <dirent.h>
#include <sys/stat.h>
#else
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#endif

#define BROWSER_READ_YIELD_ENTRIES 16

static int ascii_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int ext_equals(const char *ext, const char *want)
{
    if (!ext || !want) {
        return 0;
    }
    while (*ext && *want) {
        if (ascii_tolower((unsigned char)*ext) != ascii_tolower((unsigned char)*want)) {
            return 0;
        }
        ext++;
        want++;
    }
    return *ext == '\0' && *want == '\0';
}

static const char *file_ext(const char *path)
{
    const char *name = eqvita_media_browser_file_name(path);
    const char *dot = NULL;

    if (!name || !*name) {
        return NULL;
    }
    for (const char *p = name; *p; ++p) {
        if (*p == '.') {
            dot = p;
        }
    }
    return dot && dot[1] ? dot + 1 : NULL;
}

int eqvita_media_browser_is_supported_file(const char *path)
{
    return eqvita_media_browser_is_supported_file_for_filter(path, EQVITA_MEDIA_FILTER_AUDIO);
}

int eqvita_media_browser_is_supported_file_for_filter(const char *path,
                                                       eqvita_media_file_filter_t filter)
{
    const char *ext = file_ext(path);
    if (filter == EQVITA_MEDIA_FILTER_EQUALIZER_APO) {
        return ext_equals(ext, "txt");
    }
    return ext_equals(ext, "ogg") ||
           ext_equals(ext, "mp3") ||
           ext_equals(ext, "wav");
}

const char *eqvita_media_browser_file_name(const char *path)
{
    const char *last = path;

    if (!path) {
        return "";
    }
    for (const char *p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            last = p + 1;
        }
    }
    return last;
}

int eqvita_media_browser_join_path(char *out, size_t out_size, const char *dir, const char *name)
{
    int written;
    int needs_slash;
    char last;

    if (!out || out_size == 0 || !dir || !name || !*dir || !*name) {
        return -1;
    }

    last = dir[strlen(dir) - 1];
    needs_slash = last != ':' && last != '/' && last != '\\';
    written = snprintf(out, out_size, "%s%s%s", dir, needs_slash ? "/" : "", name);
    if (written < 0 || (size_t)written >= out_size) {
        out[0] = '\0';
        return -1;
    }
    return 0;
}

int eqvita_media_browser_parent_path(char *out, size_t out_size, const char *path)
{
    size_t len;
    size_t parent_len = 0;
    size_t colon = (size_t)-1;
    size_t slash = (size_t)-1;

    if (!out || out_size == 0 || !path || !*path) {
        return -1;
    }

    len = strlen(path);
    while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
        len--;
    }

    for (size_t i = 0; i < len; ++i) {
        if (path[i] == ':') {
            colon = i;
        } else if (path[i] == '/' || path[i] == '\\') {
            slash = i;
        }
    }

    if (colon != (size_t)-1 && colon + 1 == len) {
        return -1;
    }

    if (slash != (size_t)-1) {
        parent_len = slash + 1;
        if (colon != (size_t)-1 && slash == colon + 1) {
            parent_len = colon + 1;
        }
    } else if (colon != (size_t)-1) {
        parent_len = colon + 1;
    }

    if (parent_len == 0 || parent_len >= out_size) {
        out[0] = '\0';
        return -1;
    }

    memcpy(out, path, parent_len);
    out[parent_len] = '\0';
    return 0;
}

int eqvita_media_browser_is_root_path(const char *path)
{
    const char *colon;

    if (!path || !*path) {
        return 0;
    }

    colon = strchr(path, ':');
    if (!colon || colon == path) {
        return 0;
    }

    for (const char *p = colon + 1; *p; ++p) {
        if (*p != '/' && *p != '\\') {
            return 0;
        }
    }
    return 1;
}

static size_t normalized_path_length(const char *path)
{
    size_t len;

    if (!path) {
        return 0;
    }
    len = strlen(path);
    while (len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\')) {
        len--;
    }
    return len;
}

static int path_char_equal(char a, char b)
{
    if ((a == '/' || a == '\\') && (b == '/' || b == '\\')) {
        return 1;
    }
    return a == b;
}

int eqvita_media_browser_paths_equal(const char *a, const char *b)
{
    size_t a_len = normalized_path_length(a);
    size_t b_len = normalized_path_length(b);

    if (!a || !b || a_len != b_len) {
        return 0;
    }
    for (size_t i = 0; i < a_len; ++i) {
        if (!path_char_equal(a[i], b[i])) {
            return 0;
        }
    }
    return 1;
}

int eqvita_media_browser_path_is_within(const char *path, const char *root)
{
    size_t path_len = normalized_path_length(path);
    size_t root_len = normalized_path_length(root);

    if (!path || !root || root_len == 0 || path_len < root_len) {
        return 0;
    }
    for (size_t i = 0; i < root_len; ++i) {
        if (!path_char_equal(path[i], root[i])) {
            return 0;
        }
    }
    return path_len == root_len || path[root_len] == '/' || path[root_len] == '\\';
}

static int ascii_strcasecmp_local(const char *a, const char *b)
{
    while (*a && *b) {
        int ca = ascii_tolower((unsigned char)*a);
        int cb = ascii_tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        a++;
        b++;
    }
    return ascii_tolower((unsigned char)*a) - ascii_tolower((unsigned char)*b);
}

static int entry_compare(const eqvita_media_entry_t *a, const eqvita_media_entry_t *b)
{
    if (a->kind == EQVITA_MEDIA_ENTRY_PARENT && b->kind != EQVITA_MEDIA_ENTRY_PARENT) {
        return -1;
    }
    if (b->kind == EQVITA_MEDIA_ENTRY_PARENT && a->kind != EQVITA_MEDIA_ENTRY_PARENT) {
        return 1;
    }
    if (a->kind == EQVITA_MEDIA_ENTRY_DIRECTORY && b->kind == EQVITA_MEDIA_ENTRY_FILE) {
        return -1;
    }
    if (a->kind == EQVITA_MEDIA_ENTRY_FILE && b->kind == EQVITA_MEDIA_ENTRY_DIRECTORY) {
        return 1;
    }
    return ascii_strcasecmp_local(a->name, b->name);
}

static int add_entry(eqvita_media_listing_t *listing,
                     eqvita_media_entry_kind_t kind,
                     const char *name,
                     const char *path)
{
    eqvita_media_entry_t *entry;
    eqvita_media_entry_t added;
    int name_len;
    int path_len;
    int insert_at;

    if (!listing || !name || !path || listing->count >= EQVITA_MEDIA_MAX_ENTRIES) {
        return -1;
    }

    memset(&added, 0, sizeof(added));
    name_len = snprintf(added.name, sizeof(added.name), "%s", name);
    path_len = snprintf(added.path, sizeof(added.path), "%s", path);
    if (name_len < 0 || name_len >= (int)sizeof(added.name) ||
        path_len < 0 || path_len >= (int)sizeof(added.path)) {
        return -1;
    }
    added.kind = kind;

    insert_at = 0;
    while (insert_at < listing->count &&
           entry_compare(&listing->entries[insert_at], &added) <= 0) {
        insert_at++;
    }

    if (insert_at < listing->count) {
        memmove(&listing->entries[insert_at + 1],
                &listing->entries[insert_at],
                (size_t)(listing->count - insert_at) * sizeof(listing->entries[0]));
    }
    entry = &listing->entries[insert_at];
    *entry = added;
    listing->count++;
    return 0;
}

#ifndef EQVITA_HOST_TESTS
static int is_dir(const SceIoDirent *dir)
{
    return SCE_S_ISDIR(dir->d_stat.st_mode);
}
#endif

int eqvita_media_browser_read_roots(eqvita_media_listing_t *listing)
{
#ifndef EQVITA_HOST_TESTS
    static const char *roots[] = {
        "ux0:",
        "uma0:",
        "imc0:",
        "xmc0:",
        "ur0:"
    };
#endif
    eqvita_media_listing_t *next;

    if (!listing) {
        return -1;
    }

    next = (eqvita_media_listing_t *)malloc(sizeof(*next));
    if (!next) {
        return -1;
    }

    memset(next, 0, sizeof(*next));
    snprintf(next->path, sizeof(next->path), "%s", "Storage");

#ifdef EQVITA_HOST_TESTS
    add_entry(next, EQVITA_MEDIA_ENTRY_DIRECTORY, "ux0:", "ux0:");
    add_entry(next, EQVITA_MEDIA_ENTRY_DIRECTORY, "ux0:music/", "ux0:music/");
#else
    for (size_t i = 0; i < sizeof(roots) / sizeof(roots[0]); ++i) {
        SceIoStat stat;
        memset(&stat, 0, sizeof(stat));
        if (sceIoGetstat(roots[i], &stat) >= 0) {
            add_entry(next, EQVITA_MEDIA_ENTRY_DIRECTORY, roots[i], roots[i]);
            if (strcmp(roots[i], "ux0:") == 0) {
                add_entry(next, EQVITA_MEDIA_ENTRY_DIRECTORY, "ux0:music/", "ux0:music/");
            }
        }
    }
#endif

    *listing = *next;
    free(next);
    return listing->count;
}

int eqvita_media_browser_read_dir(eqvita_media_listing_t *listing, const char *path)
{
    return eqvita_media_browser_read_dir_filtered(listing, path, EQVITA_MEDIA_FILTER_AUDIO);
}

int eqvita_media_browser_read_dir_filtered(eqvita_media_listing_t *listing,
                                           const char *path,
                                           eqvita_media_file_filter_t filter)
{
#ifdef EQVITA_HOST_TESTS
    DIR *fd;
    struct dirent *dir;
    struct stat st;
    char child[EQVITA_MEDIA_MAX_PATH];
    char parent[EQVITA_MEDIA_MAX_PATH];
    eqvita_media_listing_t *next;
    int path_len;

    if (!listing || !path || !*path) {
        return -1;
    }
    fd = opendir(path);
    if (!fd) {
        return -1;
    }
    next = (eqvita_media_listing_t *)malloc(sizeof(*next));
    if (!next) {
        closedir(fd);
        return -1;
    }
    memset(next, 0, sizeof(*next));
    path_len = snprintf(next->path, sizeof(next->path), "%s", path);
    if (path_len < 0 || path_len >= (int)sizeof(next->path)) {
        closedir(fd);
        free(next);
        return -1;
    }
    if (eqvita_media_browser_parent_path(parent, sizeof(parent), path) == 0) {
        add_entry(next, EQVITA_MEDIA_ENTRY_PARENT, "..", parent);
    }
    while ((dir = readdir(fd)) != NULL && next->count < EQVITA_MEDIA_MAX_ENTRIES) {
        if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0 ||
            eqvita_media_browser_join_path(child, sizeof(child), path, dir->d_name) < 0 ||
            stat(child, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            size_t child_len = strlen(child);
            if (child_len + 1 < sizeof(child) && child[child_len - 1] != '/') {
                child[child_len] = '/';
                child[child_len + 1] = '\0';
            }
            add_entry(next, EQVITA_MEDIA_ENTRY_DIRECTORY, dir->d_name, child);
        } else if (eqvita_media_browser_is_supported_file_for_filter(dir->d_name, filter)) {
            add_entry(next, EQVITA_MEDIA_ENTRY_FILE, dir->d_name, child);
        }
    }
    closedir(fd);
    *listing = *next;
    free(next);
    return listing->count;
#else
    SceUID fd;
    SceIoDirent dir;
    char child[EQVITA_MEDIA_MAX_PATH];
    char open_path[EQVITA_MEDIA_MAX_PATH];
    char parent[EQVITA_MEDIA_MAX_PATH];
    eqvita_media_listing_t *next;
    int path_len;
    int read_entries = 0;

    if (!listing || !path || !*path) {
        return -1;
    }

    path_len = snprintf(open_path, sizeof(open_path), "%s", path);
    if (path_len < 0 || path_len >= (int)sizeof(open_path)) {
        return -1;
    }

    fd = sceIoDopen(open_path);
    if (fd < 0) {
        return fd;
    }

    next = (eqvita_media_listing_t *)malloc(sizeof(*next));
    if (!next) {
        sceIoDclose(fd);
        return -1;
    }

    memset(next, 0, sizeof(*next));
    path_len = snprintf(next->path, sizeof(next->path), "%s", open_path);
    if (path_len < 0 || path_len >= (int)sizeof(next->path)) {
        sceIoDclose(fd);
        free(next);
        return -1;
    }

    if (eqvita_media_browser_parent_path(parent, sizeof(parent), open_path) == 0) {
        add_entry(next, EQVITA_MEDIA_ENTRY_PARENT, "..", parent);
    }

    memset(&dir, 0, sizeof(dir));
    while (sceIoDread(fd, &dir) > 0 && next->count < EQVITA_MEDIA_MAX_ENTRIES) {
        if (strcmp(dir.d_name, ".") == 0 || strcmp(dir.d_name, "..") == 0) {
            memset(&dir, 0, sizeof(dir));
            continue;
        }
        if (eqvita_media_browser_join_path(child, sizeof(child), open_path, dir.d_name) < 0) {
            memset(&dir, 0, sizeof(dir));
            continue;
        }
        if (is_dir(&dir)) {
            size_t child_len = strlen(child);
            if (child_len + 1 < sizeof(child) && child[child_len - 1] != '/') {
                child[child_len] = '/';
                child[child_len + 1] = '\0';
            }
            add_entry(next, EQVITA_MEDIA_ENTRY_DIRECTORY, dir.d_name, child);
        } else if (eqvita_media_browser_is_supported_file_for_filter(dir.d_name, filter)) {
            add_entry(next, EQVITA_MEDIA_ENTRY_FILE, dir.d_name, child);
        }
        read_entries++;
        if ((read_entries % BROWSER_READ_YIELD_ENTRIES) == 0) {
            sceKernelDelayThread(500);
        }
        memset(&dir, 0, sizeof(dir));
    }

    sceIoDclose(fd);
    *listing = *next;
    free(next);
    return listing->count;
#endif
}

int eqvita_media_browser_read_dir_filtered_at_root(eqvita_media_listing_t *listing,
                                                   const char *path,
                                                   eqvita_media_file_filter_t filter,
                                                   const char *root)
{
    int result;

    if (!root || !eqvita_media_browser_path_is_within(path, root)) {
        return -1;
    }
    result = eqvita_media_browser_read_dir_filtered(listing, path, filter);
    if (result < 0) {
        return result;
    }
    if (eqvita_media_browser_paths_equal(path, root) && listing->count > 0 &&
        listing->entries[0].kind == EQVITA_MEDIA_ENTRY_PARENT) {
        memmove(&listing->entries[0], &listing->entries[1],
                (size_t)(listing->count - 1) * sizeof(listing->entries[0]));
        listing->count--;
    }
    return listing->count;
}
