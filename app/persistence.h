#pragma once

#include "../common/eq_shared.h"

#define EQVITA_DATA_DIR "ur0:data/eqvita"
#define EQVITA_PEQ_DIR_NAME "peq"
#define EQVITA_PEQ_DIR EQVITA_DATA_DIR "/" EQVITA_PEQ_DIR_NAME
#define EQVITA_BOOT_STATE_NAME "boot.eqbs"
#define EQVITA_PRESET_NAME_FMT "preset%d.eqvp"
#define EQVITA_LEGACY_PRESET_NAME_FMT "preset%d.bin"
#define EQVITA_THEME_NAME "theme.cfg"
#define EQVITA_ACTIVE_SLOT_NAME "active_slot.cfg"
#define EQVITA_OUTPUT_PEQ_NAME EQ_ROUTE_PROFILE_FILE_NAME
#define EQVITA_APP_LOG_NAME "app.log"
#define EQVITA_APP_LOG_BACKUP_NAME "app.log.1"
#define EQVITA_APP_LOG_MAX_BYTES (256u * 1024u)

typedef enum eqvita_startup_source
{
    EQVITA_STARTUP_SOURCE_DEFAULT = 0,
    EQVITA_STARTUP_SOURCE_BOOT = 1,
    EQVITA_STARTUP_SOURCE_PRESET = 2,
    EQVITA_STARTUP_SOURCE_LEGACY_PRESET = 3
} eqvita_startup_source_t;

int eqvita_load_boot_state(const char *dir, eq_control_t *out);
int eqvita_load_preset(const char *dir, int slot, eq_control_t *out, int *legacy_loaded);
int eqvita_load_startup_control(const char *dir, int preset_slot, eq_control_t *out, eqvita_startup_source_t *source);
int eqvita_load_app_startup_control(const char *dir,
                                    int slot_count,
                                    int default_slot,
                                    eq_control_t *out,
                                    int *out_slot,
                                    eqvita_startup_source_t *source);
int eqvita_save_boot_state(const char *dir, const eq_control_t *control);
int eqvita_save_preset(const char *dir, int slot, const eq_control_t *control);
int eqvita_load_theme_index(const char *dir, int theme_count, int default_index, int *out_index);
int eqvita_save_theme_index(const char *dir, int theme_index);
int eqvita_load_active_preset_slot(const char *dir, int slot_count, int default_slot, int *out_slot);
int eqvita_save_active_preset_slot(const char *dir, int slot);
int eqvita_append_log_line(const char *dir, const char *line);
int eqvita_build_data_path(char *out, unsigned int out_size, const char *dir, const char *name);
int eqvita_ensure_peq_dir(const char *dir);
int eqvita_seed_peq_file(const char *dir, const char *name, const char *source_path);
int eqvita_load_route_profiles(
    const char *dir,
    eq_route_profile_bank_t *out_bank,
    char out_source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX]);
int eqvita_save_route_profiles(
    const char *dir,
    const eq_route_profile_bank_t *bank,
    const char source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX]);
