#include "persistence.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef EQVITA_HOST_TESTS
#include <sys/stat.h>
#include <unistd.h>
#else
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

#define THEME_FILE_MAGIC 0x4d545145u
#define THEME_FILE_VERSION 1u
#define ACTIVE_SLOT_FILE_MAGIC 0x53504551u
#define ACTIVE_SLOT_FILE_VERSION 1u
#define ACTIVE_SLOT_FILE_MAX_SLOT_COUNT 3u
#define PEQ_SEED_MAX_BYTES (256u * 1024u)

typedef struct app_theme_file
{
    uint32_t magic;
    uint32_t version;
    uint32_t theme_index;
    uint32_t checksum;
} app_theme_file_t;

typedef struct app_active_slot_file
{
    uint32_t magic;
    uint32_t version;
    uint32_t slot;
    uint32_t checksum;
} app_active_slot_file_t;

int eqvita_build_data_path(char *out, unsigned int out_size, const char *dir, const char *name)
{
    int needed;

    if (!out || out_size == 0 || !dir || !name) {
        return -1;
    }

    needed = snprintf(out, out_size, "%s/%s", dir, name);
    if (needed < 0 || (unsigned int)needed >= out_size) {
        out[0] = '\0';
        return -1;
    }

    return 0;
}

static uint32_t theme_file_checksum(uint32_t theme_index)
{
    return THEME_FILE_MAGIC ^ THEME_FILE_VERSION ^ theme_index ^ 0x45515448u;
}

static uint32_t active_slot_file_checksum(uint32_t slot)
{
    return ACTIVE_SLOT_FILE_MAGIC ^ ACTIVE_SLOT_FILE_VERSION ^ slot ^ 0x534c4f54u;
}

static int build_preset_path(char *out, unsigned int out_size, const char *dir, int slot, int legacy)
{
    char name[32];

    if (slot < 0 || slot >= 3) {
        return -1;
    }

    snprintf(name, sizeof(name), legacy ? EQVITA_LEGACY_PRESET_NAME_FMT : EQVITA_PRESET_NAME_FMT, slot);
    return eqvita_build_data_path(out, out_size, dir, name);
}

static int file_size_bytes(const char *path, unsigned int *out_size);

static int read_file_exact(const char *path, void *data, unsigned int size)
{
    unsigned int actual_size = 0;

    if (file_size_bytes(path, &actual_size) < 0 || actual_size != size) {
        return -1;
    }

#ifdef EQVITA_HOST_TESTS
    FILE *f;
    size_t read_size;

    if (!path || !data || size == 0) {
        return -1;
    }

    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    read_size = fread(data, 1, size, f);
    fclose(f);
    return read_size == size ? 0 : -1;
#else
    SceUID fd;
    int read_size;

    if (!path || !data || size == 0) {
        return -1;
    }

    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        return fd;
    }
    read_size = sceIoRead(fd, data, size);
    sceIoClose(fd);
    return read_size == (int)size ? 0 : -1;
#endif
}

static int path_exists(const char *path)
{
    if (!path) {
        return 0;
    }
#ifdef EQVITA_HOST_TESTS
    return access(path, F_OK) == 0;
#else
    {
        SceIoStat st;
        memset(&st, 0, sizeof(st));
        return sceIoGetstat(path, &st) >= 0;
    }
#endif
}

static int path_is_directory(const char *path)
{
    if (!path) {
        return 0;
    }
#ifdef EQVITA_HOST_TESTS
    {
        struct stat st;
        return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
    }
#else
    {
        SceIoStat st;
        memset(&st, 0, sizeof(st));
        return sceIoGetstat(path, &st) >= 0 && SCE_S_ISDIR(st.st_mode);
    }
#endif
}

static int ensure_directory(const char *path)
{
    if (!path || !*path) {
        return -1;
    }
    if (path_is_directory(path)) {
        return 0;
    }
#ifdef EQVITA_HOST_TESTS
    (void)mkdir(path, 0777);
#else
    (void)sceIoMkdir(path, 0777);
#endif
    return path_is_directory(path) ? 0 : -1;
}

static void remove_file(const char *path)
{
    if (!path) {
        return;
    }
#ifdef EQVITA_HOST_TESTS
    remove(path);
#else
    sceIoRemove(path);
#endif
}

static int rename_file(const char *from, const char *to)
{
    if (!from || !to) {
        return -1;
    }
#ifdef EQVITA_HOST_TESTS
    return rename(from, to);
#else
    return sceIoRename(from, to);
#endif
}

static int replace_written_file(const char *tmp_path, const char *path)
{
    char bak_path[256];
    int needed;
    int had_existing = 0;

    needed = snprintf(bak_path, sizeof(bak_path), "%s.bak", path);
    if (needed < 0 || (unsigned int)needed >= sizeof(bak_path)) {
        remove_file(tmp_path);
        return -1;
    }

    remove_file(bak_path);
    if (path_exists(path)) {
        if (rename_file(path, bak_path) < 0) {
            remove_file(tmp_path);
            return -1;
        }
        had_existing = 1;
    }

    if (rename_file(tmp_path, path) < 0) {
        remove_file(tmp_path);
        if (had_existing) {
            remove_file(path);
            rename_file(bak_path, path);
        }
        return -1;
    }

    if (had_existing) {
        remove_file(bak_path);
    }
    return 0;
}

static int atomic_write_file(const char *path, const void *data, unsigned int size)
{
    char tmp_path[256];
    int needed;

    if (!path || !data || size == 0) {
        return -1;
    }

    needed = snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);
    if (needed < 0 || (unsigned int)needed >= sizeof(tmp_path)) {
        return -1;
    }

#ifdef EQVITA_HOST_TESTS
    {
        FILE *f = fopen(tmp_path, "wb");
        size_t written;

        if (!f) {
            return -1;
        }
        written = fwrite(data, 1, size, f);
        if (fclose(f) != 0 || written != size) {
            remove(tmp_path);
            return -1;
        }
        return replace_written_file(tmp_path, path);
    }
#else
    {
        SceUID fd = sceIoOpen(tmp_path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
        int written;
        int close_res;

        if (fd < 0) {
            return fd;
        }
        written = sceIoWrite(fd, data, size);
        close_res = sceIoClose(fd);
        if (written != (int)size || close_res < 0) {
            sceIoRemove(tmp_path);
            return written < 0 ? written : -1;
        }
        return replace_written_file(tmp_path, path);
    }
#endif
}

static int file_size_bytes(const char *path, unsigned int *out_size)
{
    if (!path || !out_size) {
        return -1;
    }

#ifdef EQVITA_HOST_TESTS
    {
        struct stat st;
        if (stat(path, &st) != 0) {
            return -1;
        }
        if (st.st_size < 0) {
            return -1;
        }
        *out_size = (unsigned int)st.st_size;
        return 0;
    }
#else
    {
        SceIoStat st;
        memset(&st, 0, sizeof(st));
        if (sceIoGetstat(path, &st) < 0) {
            return -1;
        }
        *out_size = (unsigned int)st.st_size;
        return 0;
    }
#endif
}

static int append_file(const char *path, const void *data, unsigned int size)
{
    if (!path || !data || size == 0) {
        return -1;
    }

#ifdef EQVITA_HOST_TESTS
    {
        FILE *f = fopen(path, "ab");
        size_t written;
        if (!f) {
            return -1;
        }
        written = fwrite(data, 1, size, f);
        if (fclose(f) != 0 || written != size) {
            return -1;
        }
        return 0;
    }
#else
    {
        SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_APPEND, 0777);
        int written;
        int close_res;

        if (fd < 0) {
            return fd;
        }
        written = sceIoWrite(fd, data, size);
        close_res = sceIoClose(fd);
        if (written != (int)size || close_res < 0) {
            return written < 0 ? written : -1;
        }
        return 0;
    }
#endif
}

static void ensure_data_dir(const char *dir)
{
    (void)dir;
#ifdef EQVITA_HOST_TESTS
    if (dir) {
        (void)mkdir(dir, 0777);
    }
#else
    sceIoMkdir("ur0:data", 0777);
    if (dir) {
        sceIoMkdir(dir, 0777);
    }
#endif
}

int eqvita_ensure_peq_dir(const char *dir)
{
    char peq_dir[256];

    if (!dir ||
        eqvita_build_data_path(peq_dir, sizeof(peq_dir), dir, EQVITA_PEQ_DIR_NAME) < 0) {
        return -1;
    }

#ifndef EQVITA_HOST_TESTS
    if (ensure_directory("ur0:data") < 0) {
        return -1;
    }
#endif
    if (ensure_directory(dir) < 0 || ensure_directory(peq_dir) < 0) {
        return -1;
    }
    return 0;
}

int eqvita_seed_peq_file(const char *dir, const char *name, const char *source_path)
{
    char peq_dir[256];
    char destination[256];
    unsigned int size = 0;
    void *data;
    int result;

    if (!dir || !name || !*name || !source_path || !*source_path ||
        strchr(name, '/') || strchr(name, '\\') || strchr(name, ':') ||
        eqvita_build_data_path(peq_dir, sizeof(peq_dir), dir, EQVITA_PEQ_DIR_NAME) < 0 ||
        eqvita_build_data_path(destination, sizeof(destination), peq_dir, name) < 0) {
        return -1;
    }
    if (path_exists(destination)) {
        return 1;
    }
    if (eqvita_ensure_peq_dir(dir) < 0 ||
        file_size_bytes(source_path, &size) < 0 || size == 0 || size > PEQ_SEED_MAX_BYTES) {
        return -1;
    }

    data = malloc(size);
    if (!data) {
        return -1;
    }
    if (read_file_exact(source_path, data, size) < 0) {
        free(data);
        return -1;
    }
    result = atomic_write_file(destination, data, size);
    free(data);
    return result < 0 ? result : 0;
}

int eqvita_load_route_profiles(
    const char *dir,
    eq_route_profile_bank_t *out_bank,
    char out_source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX])
{
    char path[256];
    eq_route_profile_file_t file;

    if (!dir || !out_bank ||
        eqvita_build_data_path(path, sizeof(path), dir, EQVITA_OUTPUT_PEQ_NAME) < 0 ||
        read_file_exact(path, &file, sizeof(file)) < 0) {
        return -1;
    }
    return eq_route_profile_file_extract(&file, out_bank, out_source_names);
}

int eqvita_save_route_profiles(
    const char *dir,
    const eq_route_profile_bank_t *bank,
    const char source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX])
{
    char path[256];
    eq_route_profile_file_t file;

    if (!dir || !bank ||
        eqvita_build_data_path(path, sizeof(path), dir, EQVITA_OUTPUT_PEQ_NAME) < 0) {
        return -1;
    }

    ensure_data_dir(dir);
    eq_route_profile_file_build(&file, bank, source_names);
    return atomic_write_file(path, &file, sizeof(file));
}

int eqvita_load_boot_state(const char *dir, eq_control_t *out)
{
    char path[256];
    unsigned int size = 0;

    if (!dir || !out || eqvita_build_data_path(path, sizeof(path), dir, EQVITA_BOOT_STATE_NAME) < 0) {
        return -1;
    }

    if (file_size_bytes(path, &size) < 0) {
        return -1;
    }
    if (size == sizeof(eq_boot_state_file_t)) {
        eq_boot_state_file_t state;
        if (read_file_exact(path, &state, sizeof(state)) == 0) {
            return eq_boot_state_extract_control(&state, out);
        }
    } else if (size == sizeof(eq_legacy_boot_state_file_v1_t)) {
        eq_legacy_boot_state_file_v1_t state;
        if (read_file_exact(path, &state, sizeof(state)) == 0) {
            return eq_legacy_boot_state_extract_control(&state, out);
        }
    }
    return -1;
}

int eqvita_load_preset(const char *dir, int slot, eq_control_t *out, int *legacy_loaded)
{
    char path[256];
    unsigned int size = 0;
    eq_preset_primary_status_t primary_status = EQ_PRESET_PRIMARY_MISSING;

    if (legacy_loaded) {
        *legacy_loaded = 0;
    }
    if (!dir || !out || build_preset_path(path, sizeof(path), dir, slot, 0) < 0) {
        return -1;
    }

    if (file_size_bytes(path, &size) == 0) {
        if (size == sizeof(eq_preset_file_t)) {
            eq_preset_file_t preset;
            if (read_file_exact(path, &preset, sizeof(preset)) == 0 &&
                eq_preset_extract_control(&preset, out) == 0) {
                return 0;
            }
        } else if (size == sizeof(eq_legacy_preset_file_v2_t)) {
            eq_legacy_preset_file_v2_t preset;
            if (read_file_exact(path, &preset, sizeof(preset)) == 0 &&
                eq_legacy_preset_extract_control(&preset, out) == 0) {
                if (legacy_loaded) {
                    *legacy_loaded = 1;
                }
                return 0;
            }
        }
        primary_status = EQ_PRESET_PRIMARY_INVALID;
    }

    if (!eq_preset_should_try_legacy(primary_status)) {
        return -1;
    }

    if (build_preset_path(path, sizeof(path), dir, slot, 1) < 0) {
        return -1;
    }

    {
        eq_legacy_control_v1_14_t legacy;
        if (read_file_exact(path, &legacy, sizeof(legacy)) == 0 &&
            eq_control_import_legacy(out, &legacy) == 0) {
            if (legacy_loaded) {
                *legacy_loaded = 1;
            }
            return 0;
        }
    }

    return -1;
}

int eqvita_load_startup_control(const char *dir, int preset_slot, eq_control_t *out, eqvita_startup_source_t *source)
{
    int legacy_loaded = 0;

    if (!out) {
        return -1;
    }

    if (source) {
        *source = EQVITA_STARTUP_SOURCE_DEFAULT;
    }

    if (dir && eqvita_load_boot_state(dir, out) == 0) {
        if (source) {
            *source = EQVITA_STARTUP_SOURCE_BOOT;
        }
        return 0;
    }

    if (dir && eqvita_load_preset(dir, preset_slot, out, &legacy_loaded) == 0) {
        if (source) {
            *source = legacy_loaded ? EQVITA_STARTUP_SOURCE_LEGACY_PRESET : EQVITA_STARTUP_SOURCE_PRESET;
        }
        return 0;
    }

    eq_control_init_defaults(out);
    return 0;
}

int eqvita_load_app_startup_control(const char *dir,
                                    int slot_count,
                                    int default_slot,
                                    eq_control_t *out,
                                    int *out_slot,
                                    eqvita_startup_source_t *source)
{
    int slot = default_slot;
    int legacy_loaded = 0;

    if (!out || slot_count <= 0 || default_slot < 0 || default_slot >= slot_count) {
        return -1;
    }

    if (out_slot) {
        *out_slot = default_slot;
    }
    if (source) {
        *source = EQVITA_STARTUP_SOURCE_DEFAULT;
    }

    if (dir && eqvita_load_active_preset_slot(dir, slot_count, default_slot, &slot) == 0 &&
        eqvita_load_preset(dir, slot, out, &legacy_loaded) == 0) {
        if (out_slot) {
            *out_slot = slot;
        }
        if (source) {
            *source = legacy_loaded ? EQVITA_STARTUP_SOURCE_LEGACY_PRESET : EQVITA_STARTUP_SOURCE_PRESET;
        }
        return 0;
    }

    if (eqvita_load_startup_control(dir, default_slot, out, source) == 0) {
        if (out_slot) {
            *out_slot = default_slot;
        }
        return 0;
    }

    eq_control_init_defaults(out);
    return 0;
}

int eqvita_save_boot_state(const char *dir, const eq_control_t *control)
{
    char path[256];
    eq_boot_state_file_t state;

    if (!dir || !control || eqvita_build_data_path(path, sizeof(path), dir, EQVITA_BOOT_STATE_NAME) < 0) {
        return -1;
    }

    ensure_data_dir(dir);
    eq_boot_state_build(&state, control);
    return atomic_write_file(path, &state, sizeof(state));
}

int eqvita_save_preset(const char *dir, int slot, const eq_control_t *control)
{
    char path[256];
    eq_preset_file_t preset;

    if (!dir || !control || build_preset_path(path, sizeof(path), dir, slot, 0) < 0) {
        return -1;
    }

    ensure_data_dir(dir);
    eq_preset_build(&preset, control);
    return atomic_write_file(path, &preset, sizeof(preset));
}

int eqvita_load_theme_index(const char *dir, int theme_count, int default_index, int *out_index)
{
    char path[256];
    app_theme_file_t file;

    if (out_index) {
        *out_index = default_index;
    }
    if (!dir || !out_index || theme_count <= 0 ||
        eqvita_build_data_path(path, sizeof(path), dir, EQVITA_THEME_NAME) < 0) {
        return -1;
    }

    if (read_file_exact(path, &file, sizeof(file)) < 0 ||
        file.magic != THEME_FILE_MAGIC ||
        file.version != THEME_FILE_VERSION ||
        file.checksum != theme_file_checksum(file.theme_index) ||
        file.theme_index >= (uint32_t)theme_count) {
        *out_index = default_index;
        return -1;
    }

    *out_index = (int)file.theme_index;
    return 0;
}

int eqvita_save_theme_index(const char *dir, int theme_index)
{
    char path[256];
    app_theme_file_t file;

    if (!dir || theme_index < 0 ||
        eqvita_build_data_path(path, sizeof(path), dir, EQVITA_THEME_NAME) < 0) {
        return -1;
    }

    ensure_data_dir(dir);
    memset(&file, 0, sizeof(file));
    file.magic = THEME_FILE_MAGIC;
    file.version = THEME_FILE_VERSION;
    file.theme_index = (uint32_t)theme_index;
    file.checksum = theme_file_checksum(file.theme_index);
    return atomic_write_file(path, &file, sizeof(file));
}

int eqvita_load_active_preset_slot(const char *dir, int slot_count, int default_slot, int *out_slot)
{
    char path[256];
    app_active_slot_file_t file;

    if (out_slot) {
        *out_slot = default_slot;
    }
    if (!dir || !out_slot || slot_count <= 0 || default_slot < 0 || default_slot >= slot_count ||
        eqvita_build_data_path(path, sizeof(path), dir, EQVITA_ACTIVE_SLOT_NAME) < 0) {
        return -1;
    }

    if (read_file_exact(path, &file, sizeof(file)) < 0 ||
        file.magic != ACTIVE_SLOT_FILE_MAGIC ||
        file.version != ACTIVE_SLOT_FILE_VERSION ||
        file.checksum != active_slot_file_checksum(file.slot) ||
        file.slot >= (uint32_t)slot_count) {
        *out_slot = default_slot;
        return -1;
    }

    *out_slot = (int)file.slot;
    return 0;
}

int eqvita_save_active_preset_slot(const char *dir, int slot)
{
    char path[256];
    app_active_slot_file_t file;

    if (!dir || slot < 0 || slot >= (int)ACTIVE_SLOT_FILE_MAX_SLOT_COUNT ||
        eqvita_build_data_path(path, sizeof(path), dir, EQVITA_ACTIVE_SLOT_NAME) < 0) {
        return -1;
    }

    ensure_data_dir(dir);
    memset(&file, 0, sizeof(file));
    file.magic = ACTIVE_SLOT_FILE_MAGIC;
    file.version = ACTIVE_SLOT_FILE_VERSION;
    file.slot = (uint32_t)slot;
    file.checksum = active_slot_file_checksum(file.slot);
    return atomic_write_file(path, &file, sizeof(file));
}

int eqvita_append_log_line(const char *dir, const char *line)
{
    char path[256];
    char backup_path[256];
    char buffer[320];
    unsigned int current_size = 0;
    unsigned int len;
    int needed;

    if (!dir || !line ||
        eqvita_build_data_path(path, sizeof(path), dir, EQVITA_APP_LOG_NAME) < 0 ||
        eqvita_build_data_path(backup_path, sizeof(backup_path), dir, EQVITA_APP_LOG_BACKUP_NAME) < 0) {
        return -1;
    }

    needed = snprintf(buffer, sizeof(buffer), "%s\n", line);
    if (needed < 0) {
        return -1;
    }
    len = (unsigned int)needed;
    if (len >= sizeof(buffer)) {
        len = (unsigned int)sizeof(buffer) - 1;
        buffer[len - 1] = '\n';
        buffer[len] = '\0';
    }

    ensure_data_dir(dir);
    if (file_size_bytes(path, &current_size) == 0 &&
        current_size + len > EQVITA_APP_LOG_MAX_BYTES) {
        remove_file(backup_path);
        if (rename_file(path, backup_path) != 0) {
            remove_file(path);
        }
    }

    return append_file(path, buffer, len);
}
