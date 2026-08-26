#include "../app/persistence.h"
#include "../common/eq_shared.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define ASSERT_EQ_I32(actual, expected) do { \
    int32_t a_ = (int32_t)(actual); \
    int32_t e_ = (int32_t)(expected); \
    if (a_ != e_) { \
        printf("FAIL %s:%d: expected %s == %d, got %d\n", __FILE__, __LINE__, #actual, e_, a_); \
        failures++; \
    } \
} while (0)

static void path_join(char *out, size_t out_size, const char *dir, const char *name)
{
    snprintf(out, out_size, "%s/%s", dir, name);
}

static char *make_temp_dir(void)
{
    char templ[] = "/tmp/eqvita-persist-XXXXXX";
    char *dir = mkdtemp(templ);
    if (!dir) {
        perror("mkdtemp");
        exit(2);
    }
    return strdup(dir);
}

static void write_file_or_die(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(2);
    }
    if (fwrite(data, 1, size, f) != size) {
        perror("fwrite");
        fclose(f);
        exit(2);
    }
    fclose(f);
}

static void write_file_with_extra_byte_or_die(const char *path, const void *data, size_t size)
{
    static const unsigned char extra = 0x5a;
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(2);
    }
    if (fwrite(data, 1, size, f) != size ||
        fwrite(&extra, 1, 1, f) != 1) {
        perror("fwrite");
        fclose(f);
        exit(2);
    }
    fclose(f);
}

static void write_boot_control(const char *dir, const eq_control_t *ctrl)
{
    char path[256];
    eq_boot_state_file_t state;
    eq_boot_state_build(&state, ctrl);
    path_join(path, sizeof(path), dir, "boot.eqbs");
    write_file_or_die(path, &state, sizeof(state));
}

static void write_preset_control(const char *dir, int slot, const eq_control_t *ctrl)
{
    char path[256];
    eq_preset_file_t preset;
    eq_preset_build(&preset, ctrl);
    snprintf(path, sizeof(path), "%s/preset%d.eqvp", dir, slot);
    write_file_or_die(path, &preset, sizeof(preset));
}

static eq_legacy_control_v1_14_t make_legacy_control(void)
{
    eq_legacy_control_v1_14_t legacy;
    memset(&legacy, 0, sizeof(legacy));
    legacy.version = EQ_LEGACY_ABI_VERSION_1_14;
    legacy.size = EQ_LEGACY_CONTROL_SIZE;
    legacy.enabled = 1;
    legacy.speaker_only = 1;
    legacy.hpf_enabled = (uint8_t)(1u | (EQ_HEADROOM_LOUD << EQ_HEADROOM_MODE_SHIFT));
    legacy.route_hint = EQ_ROUTE_HEADPHONES;
    legacy.preamp_mdB = -7000;
    legacy.band_gain_mdB[2] = 4500;
    return legacy;
}

static void write_legacy_boot_control(const char *dir)
{
    char path[256];
    eq_legacy_boot_state_file_v1_t state;
    memset(&state, 0, sizeof(state));
    state.magic = EQ_BOOT_STATE_MAGIC;
    state.version = EQ_LEGACY_BOOT_STATE_VERSION;
    state.header_size = (uint32_t)offsetof(eq_legacy_boot_state_file_v1_t, control);
    state.payload_size = sizeof(eq_legacy_control_v1_14_t);
    state.control = make_legacy_control();
    state.checksum = eq_legacy_boot_state_checksum(&state);
    path_join(path, sizeof(path), dir, "boot.eqbs");
    write_file_or_die(path, &state, sizeof(state));
}

static void write_legacy_preset_wrapper(const char *dir, int slot)
{
    char path[256];
    eq_legacy_preset_file_v2_t preset;
    memset(&preset, 0, sizeof(preset));
    preset.magic = EQ_PRESET_MAGIC;
    preset.version = EQ_LEGACY_PRESET_VERSION;
    preset.header_size = (uint32_t)offsetof(eq_legacy_preset_file_v2_t, control);
    preset.payload_size = sizeof(eq_legacy_control_v1_14_t);
    preset.band_count = EQ_PRESET_BAND_COUNT;
    preset.control = make_legacy_control();
    preset.checksum = eq_legacy_preset_checksum(&preset);
    snprintf(path, sizeof(path), "%s/preset%d.eqvp", dir, slot);
    write_file_or_die(path, &preset, sizeof(preset));
}

static int read_preset_control(const char *dir, int slot, eq_control_t *out)
{
    char path[256];
    eq_preset_file_t preset;
    FILE *f;
    size_t r;

    snprintf(path, sizeof(path), "%s/preset%d.eqvp", dir, slot);
    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }
    r = fread(&preset, 1, sizeof(preset), f);
    fclose(f);
    if (r != sizeof(preset)) {
        return -1;
    }
    return eq_preset_extract_control(&preset, out);
}

static long file_size_or_neg1(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return -1;
    }
    return (long)st.st_size;
}

static void test_startup_load_prefers_boot_state_over_preset0(void)
{
    char *dir = make_temp_dir();
    eq_control_t boot;
    eq_control_t preset;
    eq_control_t loaded;
    eqvita_startup_source_t source = EQVITA_STARTUP_SOURCE_DEFAULT;

    eq_control_init_defaults(&boot);
    boot.enabled = 0;
    boot.route_hint = EQ_ROUTE_SPEAKER;
    boot.band_gain_mdB[1] = 4500;
    eq_control_set_headroom_mode(&boot, EQ_HEADROOM_LOUD);

    eq_control_init_defaults(&preset);
    preset.enabled = 1;
    preset.band_gain_mdB[1] = -3000;

    write_boot_control(dir, &boot);
    write_preset_control(dir, 0, &preset);

    ASSERT_TRUE(eqvita_load_startup_control(dir, 0, &loaded, &source) == 0);
    ASSERT_EQ_I32(source, EQVITA_STARTUP_SOURCE_BOOT);
    ASSERT_EQ_I32(loaded.enabled, 0);
    ASSERT_EQ_I32(loaded.band_gain_mdB[1], 4500);
    ASSERT_EQ_I32(eq_control_get_headroom_mode(&loaded), EQ_HEADROOM_LOUD);
    free(dir);
}

static void test_legacy_boot_and_preset_wrappers_migrate(void)
{
    char *dir = make_temp_dir();
    eq_control_t loaded;
    int legacy_loaded = 0;

    write_legacy_boot_control(dir);
    ASSERT_TRUE(eqvita_load_boot_state(dir, &loaded) == 0);
    ASSERT_EQ_I32(loaded.version, EQ_ABI_VERSION);
    ASSERT_EQ_I32(loaded.band_gain_mdB[2], 4500);
    ASSERT_EQ_I32(eq_control_get_headroom_mode(&loaded), EQ_HEADROOM_LOUD);
    ASSERT_EQ_I32(loaded.eq_mode, EQ_MODE_GRAPHIC);

    write_legacy_preset_wrapper(dir, 1);
    ASSERT_TRUE(eqvita_load_preset(dir, 1, &loaded, &legacy_loaded) == 0);
    ASSERT_EQ_I32(legacy_loaded, 1);
    ASSERT_EQ_I32(loaded.band_gain_mdB[2], 4500);
    ASSERT_EQ_I32(loaded.parametric_filter_count, 0);
    free(dir);
}

static void test_parametric_preset_persists_all_operations(void)
{
    char *dir = make_temp_dir();
    eq_control_t control;
    eq_control_t loaded;
    int legacy_loaded = 1;

    eq_control_init_defaults(&control);
    eq_control_set_parametric_mode(&control, 2);
    control.preamp_mdB = -16800;
    eq_control_set_headroom_mode(&control, EQ_HEADROOM_EXACT);
    control.parametric_filters[0].type = EQ_FILTER_PEAK;
    control.parametric_filters[0].channel_mask = EQ_CHANNEL_LEFT_MASK;
    control.parametric_filters[0].data.filter.frequency_mHz = 6800000;
    control.parametric_filters[0].data.filter.gain_mdB = -8000;
    control.parametric_filters[0].data.filter.q_uQ = 4708300;
    control.parametric_filters[1].type = EQ_FILTER_COPY;
    control.parametric_filters[1].data.copy.matrix[0] = -EQ_COPY_COEFFICIENT_SCALE;
    control.parametric_filters[1].data.copy.matrix[3] = -EQ_COPY_COEFFICIENT_SCALE;

    ASSERT_TRUE(eqvita_save_preset(dir, 2, &control) == 0);
    ASSERT_TRUE(eqvita_load_preset(dir, 2, &loaded, &legacy_loaded) == 0);
    ASSERT_EQ_I32(legacy_loaded, 0);
    ASSERT_EQ_I32(loaded.eq_mode, EQ_MODE_PARAMETRIC);
    ASSERT_EQ_I32(loaded.parametric_filter_count, 2);
    ASSERT_EQ_I32(loaded.preamp_mdB, -16800);
    ASSERT_EQ_I32(loaded.parametric_filters[0].channel_mask, EQ_CHANNEL_LEFT_MASK);
    ASSERT_EQ_I32(loaded.parametric_filters[1].data.copy.matrix[0],
                  -EQ_COPY_COEFFICIENT_SCALE);
    free(dir);
}

static void test_startup_load_falls_back_to_preset0_without_writing(void)
{
    char *dir = make_temp_dir();
    eq_control_t preset;
    eq_control_t loaded;
    eqvita_startup_source_t source = EQVITA_STARTUP_SOURCE_DEFAULT;
    char boot_path[256];

    eq_control_init_defaults(&preset);
    preset.enabled = 1;
    preset.band_gain_mdB[2] = 7000;
    write_preset_control(dir, 0, &preset);

    ASSERT_TRUE(eqvita_load_startup_control(dir, 0, &loaded, &source) == 0);
    ASSERT_EQ_I32(source, EQVITA_STARTUP_SOURCE_PRESET);
    ASSERT_EQ_I32(loaded.enabled, 1);
    ASSERT_EQ_I32(loaded.band_gain_mdB[2], 7000);

    path_join(boot_path, sizeof(boot_path), dir, "boot.eqbs");
    ASSERT_TRUE(access(boot_path, F_OK) != 0);
    free(dir);
}

static void test_startup_load_uses_defaults_without_writing(void)
{
    char *dir = make_temp_dir();
    eq_control_t loaded;
    eqvita_startup_source_t source = EQVITA_STARTUP_SOURCE_BOOT;
    char boot_path[256];
    char preset_path[256];

    ASSERT_TRUE(eqvita_load_startup_control(dir, 0, &loaded, &source) == 0);
    ASSERT_EQ_I32(source, EQVITA_STARTUP_SOURCE_DEFAULT);
    ASSERT_EQ_I32(loaded.enabled, 0);
    ASSERT_EQ_I32(loaded.speaker_only, 1);

    path_join(boot_path, sizeof(boot_path), dir, "boot.eqbs");
    path_join(preset_path, sizeof(preset_path), dir, "preset0.eqvp");
    ASSERT_TRUE(access(boot_path, F_OK) != 0);
    ASSERT_TRUE(access(preset_path, F_OK) != 0);
    free(dir);
}

static void test_atomic_preset_save_preserves_old_file_when_temp_write_fails(void)
{
    char *dir = make_temp_dir();
    char tmp_path[256];
    eq_control_t first;
    eq_control_t second;
    eq_control_t loaded;

    eq_control_init_defaults(&first);
    first.enabled = 1;
    first.band_gain_mdB[1] = 1000;

    eq_control_init_defaults(&second);
    second.enabled = 1;
    second.band_gain_mdB[1] = 9000;

    ASSERT_TRUE(eqvita_save_preset(dir, 1, &first) == 0);
    snprintf(tmp_path, sizeof(tmp_path), "%s/preset1.eqvp.tmp", dir);
    ASSERT_TRUE(mkdir(tmp_path, 0777) == 0);

    ASSERT_TRUE(eqvita_save_preset(dir, 1, &second) < 0);
    ASSERT_TRUE(read_preset_control(dir, 1, &loaded) == 0);
    ASSERT_EQ_I32(loaded.band_gain_mdB[1], 1000);
    free(dir);
}

static void test_theme_save_is_atomic_and_load_validates_bounds(void)
{
    char *dir = make_temp_dir();
    char tmp_path[256];
    int theme = -1;

    ASSERT_TRUE(eqvita_save_theme_index(dir, 4) == 0);
    ASSERT_TRUE(eqvita_load_theme_index(dir, 8, 2, &theme) == 0);
    ASSERT_EQ_I32(theme, 4);
    ASSERT_TRUE(eqvita_load_theme_index(dir, 4, 2, &theme) < 0);
    ASSERT_EQ_I32(theme, 2);

    snprintf(tmp_path, sizeof(tmp_path), "%s/theme.cfg.tmp", dir);
    ASSERT_TRUE(mkdir(tmp_path, 0777) == 0);
    ASSERT_TRUE(eqvita_save_theme_index(dir, 1) < 0);
    ASSERT_TRUE(eqvita_load_theme_index(dir, 8, 2, &theme) == 0);
    ASSERT_EQ_I32(theme, 4);
    free(dir);
}

static void test_active_preset_slot_round_trips_and_validates_bounds(void)
{
    char *dir = make_temp_dir();
    int slot = -1;

    ASSERT_TRUE(eqvita_load_active_preset_slot(dir, 3, 1, &slot) < 0);
    ASSERT_EQ_I32(slot, 1);

    ASSERT_TRUE(eqvita_save_active_preset_slot(dir, 2) == 0);
    ASSERT_TRUE(eqvita_load_active_preset_slot(dir, 3, 0, &slot) == 0);
    ASSERT_EQ_I32(slot, 2);

    ASSERT_TRUE(eqvita_save_active_preset_slot(dir, 99) < 0);
    ASSERT_TRUE(eqvita_load_active_preset_slot(dir, 3, 0, &slot) == 0);
    ASSERT_EQ_I32(slot, 2);

    ASSERT_TRUE(eqvita_load_active_preset_slot(dir, 2, 0, &slot) < 0);
    ASSERT_EQ_I32(slot, 0);
    free(dir);
}

static void test_app_startup_prefers_active_slot_preset_over_stale_boot_state(void)
{
    char *dir = make_temp_dir();
    eq_control_t boot;
    eq_control_t preset;
    eq_control_t loaded;
    eqvita_startup_source_t source = EQVITA_STARTUP_SOURCE_DEFAULT;
    int slot = -1;

    eq_control_init_defaults(&boot);
    boot.enabled = 1;
    boot.band_gain_mdB[1] = 1000;

    eq_control_init_defaults(&preset);
    preset.enabled = 1;
    preset.band_gain_mdB[1] = 7000;

    write_boot_control(dir, &boot);
    write_preset_control(dir, 2, &preset);
    ASSERT_TRUE(eqvita_save_active_preset_slot(dir, 2) == 0);

    ASSERT_TRUE(eqvita_load_app_startup_control(dir, 3, 0, &loaded, &slot, &source) == 0);
    ASSERT_EQ_I32(source, EQVITA_STARTUP_SOURCE_PRESET);
    ASSERT_EQ_I32(slot, 2);
    ASSERT_EQ_I32(loaded.band_gain_mdB[1], 7000);
    free(dir);
}

static void test_app_startup_keeps_legacy_boot_state_when_no_active_slot_exists(void)
{
    char *dir = make_temp_dir();
    eq_control_t boot;
    eq_control_t preset;
    eq_control_t loaded;
    eqvita_startup_source_t source = EQVITA_STARTUP_SOURCE_DEFAULT;
    int slot = -1;

    eq_control_init_defaults(&boot);
    boot.enabled = 1;
    boot.band_gain_mdB[1] = 3000;

    eq_control_init_defaults(&preset);
    preset.enabled = 1;
    preset.band_gain_mdB[1] = 9000;

    write_boot_control(dir, &boot);
    write_preset_control(dir, 0, &preset);

    ASSERT_TRUE(eqvita_load_app_startup_control(dir, 3, 0, &loaded, &slot, &source) == 0);
    ASSERT_EQ_I32(source, EQVITA_STARTUP_SOURCE_BOOT);
    ASSERT_EQ_I32(slot, 0);
    ASSERT_EQ_I32(loaded.band_gain_mdB[1], 3000);
    free(dir);
}

static void test_preset_with_trailing_bytes_is_rejected(void)
{
    char *dir = make_temp_dir();
    char path[256];
    eq_control_t ctrl;
    eq_control_t loaded;
    eq_preset_file_t preset;
    int legacy_loaded = 0;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 1;
    ctrl.band_gain_mdB[1] = 3000;
    eq_preset_build(&preset, &ctrl);

    snprintf(path, sizeof(path), "%s/preset1.eqvp", dir);
    write_file_with_extra_byte_or_die(path, &preset, sizeof(preset));

    ASSERT_TRUE(eqvita_load_preset(dir, 1, &loaded, &legacy_loaded) < 0);
    ASSERT_EQ_I32(legacy_loaded, 0);
    free(dir);
}

static void test_log_append_rotates_when_cap_is_exceeded(void)
{
    char *dir = make_temp_dir();
    char log_path[256];
    char old_path[256];
    char filler[256];
    int writes = (EQVITA_APP_LOG_MAX_BYTES / 200u) + 2u;

    memset(filler, 'a', sizeof(filler));
    filler[sizeof(filler) - 1] = '\0';
    for (int i = 0; i < writes; ++i) {
        ASSERT_TRUE(eqvita_append_log_line(dir, filler) == 0);
    }

    path_join(log_path, sizeof(log_path), dir, "app.log");
    path_join(old_path, sizeof(old_path), dir, "app.log.1");
    ASSERT_TRUE(file_size_or_neg1(log_path) > 0);
    ASSERT_TRUE(file_size_or_neg1(log_path) <= (long)EQVITA_APP_LOG_MAX_BYTES);
    ASSERT_TRUE(file_size_or_neg1(old_path) > 0);
    free(dir);
}

static void test_peq_directory_is_created_under_data_dir(void)
{
    static const char bundled[] = "Preamp: -5 dB\n";
    static const char user_edit[] = "Preamp: -7 dB\n";
    char *parent = make_temp_dir();
    char data_dir[256];
    char peq_dir[256];
    char source_path[256];
    char seeded_path[256];
    char readback[64];
    FILE *file;
    struct stat st;

    path_join(data_dir, sizeof(data_dir), parent, "eqvita");
    path_join(peq_dir, sizeof(peq_dir), data_dir, EQVITA_PEQ_DIR_NAME);
    path_join(source_path, sizeof(source_path), parent, "bundled.txt");
    path_join(seeded_path, sizeof(seeded_path), peq_dir, "pch-1000.txt");
    write_file_or_die(source_path, bundled, sizeof(bundled) - 1);
    ASSERT_TRUE(eqvita_ensure_peq_dir(data_dir) == 0);
    ASSERT_TRUE(stat(data_dir, &st) == 0 && S_ISDIR(st.st_mode));
    ASSERT_TRUE(stat(peq_dir, &st) == 0 && S_ISDIR(st.st_mode));
    ASSERT_TRUE(eqvita_ensure_peq_dir(data_dir) == 0);
    ASSERT_TRUE(eqvita_seed_peq_file(data_dir, "pch-1000.txt", source_path) == 0);
    write_file_or_die(seeded_path, user_edit, sizeof(user_edit) - 1);
    ASSERT_TRUE(eqvita_seed_peq_file(data_dir, "pch-1000.txt", source_path) == 1);
    memset(readback, 0, sizeof(readback));
    file = fopen(seeded_path, "rb");
    ASSERT_TRUE(file != NULL);
    if (file) {
        ASSERT_TRUE(fread(readback, 1, sizeof(user_edit) - 1, file) == sizeof(user_edit) - 1);
        ASSERT_TRUE(fclose(file) == 0);
    }
    ASSERT_TRUE(memcmp(readback, user_edit, sizeof(user_edit) - 1) == 0);
    ASSERT_TRUE(eqvita_seed_peq_file(data_dir, "../escape.txt", source_path) < 0);
    ASSERT_TRUE(unlink(seeded_path) == 0);
    ASSERT_TRUE(unlink(source_path) == 0);
    ASSERT_TRUE(rmdir(peq_dir) == 0);
    ASSERT_TRUE(rmdir(data_dir) == 0);
    ASSERT_TRUE(rmdir(parent) == 0);
    free(parent);
}

static void test_output_peq_profiles_persist_atomically(void)
{
    char *dir = make_temp_dir();
    char path[256];
    eq_route_profile_bank_t bank;
    eq_route_profile_bank_t loaded;
    char names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX] = {{0}};
    char loaded_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX] = {{0}};

    eq_route_profile_bank_init(&bank);
    bank.enabled_mask = eq_route_profile_bit(EQ_ROUTE_SPEAKER) |
                        eq_route_profile_bit(EQ_ROUTE_BLUETOOTH);
    bank.selected_route = EQ_ROUTE_BLUETOOTH;
    eq_control_set_parametric_mode(&bank.profiles[0], 1);
    bank.profiles[0].preamp_mdB = -5000;
    bank.profiles[0].parametric_filters[0].type = EQ_FILTER_PEAK;
    bank.profiles[0].parametric_filters[0].channel_mask = EQ_CHANNEL_STEREO_MASK;
    bank.profiles[0].parametric_filters[0].data.filter.frequency_mHz = 889000;
    bank.profiles[0].parametric_filters[0].data.filter.gain_mdB = -5770;
    bank.profiles[0].parametric_filters[0].data.filter.q_uQ = 687000;
    snprintf(names[0], sizeof(names[0]), "pch-1000.txt");
    snprintf(names[2], sizeof(names[2]), "living-room.txt");

    ASSERT_TRUE(eqvita_load_route_profiles(dir, &loaded, loaded_names) < 0);
    ASSERT_TRUE(eqvita_save_route_profiles(dir, &bank, names) == 0);
    ASSERT_TRUE(eqvita_load_route_profiles(dir, &loaded, loaded_names) == 0);
    ASSERT_EQ_I32(loaded.enabled_mask, 5);
    ASSERT_EQ_I32(loaded.selected_route, EQ_ROUTE_BLUETOOTH);
    ASSERT_EQ_I32(loaded.profiles[0].preamp_mdB, -5000);
    ASSERT_TRUE(strcmp(loaded_names[0], "pch-1000.txt") == 0);
    ASSERT_TRUE(strcmp(loaded_names[2], "living-room.txt") == 0);

    path_join(path, sizeof(path), dir, EQVITA_OUTPUT_PEQ_NAME);
    ASSERT_TRUE(file_size_or_neg1(path) == (long)sizeof(eq_route_profile_file_t));
    ASSERT_TRUE(unlink(path) == 0);
    ASSERT_TRUE(rmdir(dir) == 0);
    free(dir);
}

int main(void)
{
    test_startup_load_prefers_boot_state_over_preset0();
    test_legacy_boot_and_preset_wrappers_migrate();
    test_parametric_preset_persists_all_operations();
    test_startup_load_falls_back_to_preset0_without_writing();
    test_startup_load_uses_defaults_without_writing();
    test_atomic_preset_save_preserves_old_file_when_temp_write_fails();
    test_theme_save_is_atomic_and_load_validates_bounds();
    test_active_preset_slot_round_trips_and_validates_bounds();
    test_app_startup_prefers_active_slot_preset_over_stale_boot_state();
    test_app_startup_keeps_legacy_boot_state_when_no_active_slot_exists();
    test_preset_with_trailing_bytes_is_rejected();
    test_log_append_rotates_when_cap_is_exceeded();
    test_peq_directory_is_created_under_data_dir();
    test_output_peq_profiles_persist_atomically();
    return failures ? 1 : 0;
}
