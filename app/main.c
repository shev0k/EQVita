#include <psp2/ctrl.h>
#include <psp2/avconfig.h>
#include <psp2/apputil.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/kernel/threadmgr/lw_mutex.h>
#include <psp2/system_param.h>
#include <psp2/touch.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "app_state.h"
#include "equalizer_apo.h"
#include "media_browser.h"
#include "media_player.h"
#include "persistence.h"
#include "ui_vita.h"
#include "../common/eq_shared.h"

#define STEP_FINE 500
#define STEP_COARSE 1000
#define PRESET_SYNC_FAILED -4
#define APP_LOG_PATH EQVITA_DATA_DIR "/" EQVITA_APP_LOG_NAME
#define BUNDLED_PEQ_NAME "pch-1000.txt"
#define BUNDLED_PEQ_SOURCE "app0:assets/peq/pch-1000.txt"
#define APO_IMPORT_MAX_FILES 32u
#define APO_IMPORT_MAX_TOTAL_BYTES (512u * 1024u)
#define STATUS_LOG_INTERVAL_US 5000000u
#define STATUS_LOG_PREVIEW_INTERVAL_US 12000000u
#define DIAGNOSTIC_DRAIN_INTERVAL_US 1000000u
#define DIAGNOSTIC_DRAIN_PREVIEW_INTERVAL_US 5000000u
#define DIAGNOSTIC_DRAIN_PREVIEW_EVENTS 4u
#define ROUTE_REFRESH_INTERVAL_US 250000u
#define ROUTE_REFRESH_PREVIEW_INTERVAL_US 1000000u
#define ANALOG_CENTER 128
#define ANALOG_NAV_DEADZONE 60
#define BROWSER_THREAD_PRIORITY 0x80
#define BROWSER_THREAD_STACK (32 * 1024)

static const char *const g_apo_import_roots[] = {
    "ur0:data/",
    "ux0:data/"
};

static const eqvita_apo_import_policy_t g_apo_import_policy = {
    g_apo_import_roots,
    sizeof(g_apo_import_roots) / sizeof(g_apo_import_roots[0]),
    APO_IMPORT_MAX_FILES,
    APO_IMPORT_MAX_TOTAL_BYTES
};

#define SCE_AVCONFIG_VOLCTRL_ONBOARD 1
#define SCE_AVCONFIG_VOLCTRL_BLUETOOTH 2
#define SCE_AVCONFIG_AUDIO_DEVICE_VITA_0 0x001
#define SCE_AVCONFIG_AUDIO_DEVICE_AUDIO_OUT 0x004
#define SCE_AVCONFIG_AUDIO_DEVICE_BT_AUDIO_OUT 0x010
#define SCE_AVCONFIG_AUDIO_DEVICE_VITA_8 0x100

int sceAVConfigGetConnectedAudioDevice(uint32_t *flags);
int sceAVConfigGetVolCtrlEnable(uint32_t *volCtrl, int *muted, int *avls);

typedef enum app_screen
{
    SCREEN_HOME = 0,
    SCREEN_STATUS,
    SCREEN_PRESETS,
    SCREEN_OUTPUT_PEQ,
    SCREEN_SIMPLE,
    SCREEN_ADVANCED,
    SCREEN_MUSIC,
    SCREEN_MUSIC_BROWSER,
    SCREEN_THEMES,
    SCREEN_SETTINGS,
    SCREEN_ABOUT,
    SCREEN_COUNT
} app_screen_t;

static const char *band_labels[EQ_BANDS] = {
    "31 Hz", "62 Hz", "125 Hz", "250 Hz", "500 Hz", "1 kHz", "2 kHz", "4 kHz", "8 kHz", "16 kHz"
};

#define HOME_ROW_ENABLED 0
#define HOME_ROW_SIMPLE 1
#define HOME_ROW_ADVANCED 2
#define HOME_ROW_PARAMETRIC 3
#define HOME_ROW_PRESETS 4
#define HOME_ROW_MUSIC 5
#define HOME_ROW_THEMES 6
#define HOME_ROW_SETTINGS 7
#define HOME_ROW_STATUS 8
#define HOME_ROW_ABOUT 9
#define HOME_ROW_COUNT 10

#define STATUS_ROW_EQ_STATUS 0
#define STATUS_ROW_OUTPUT 1
#define STATUS_ROW_PRESET 2
#define STATUS_ROW_PLUGIN 3
#define STATUS_ROW_LEVELS_SECTION 4
#define STATUS_ROW_CLIPPING 5
#define STATUS_ROW_PEAK 6
#define STATUS_ROW_AUDIO_RATE 7
#define STATUS_ROW_PROCESSING_SECTION 8
#define STATUS_ROW_STREAMS 9
#define STATUS_ROW_SKIPPED 10
#define STATUS_ROW_UNKNOWN 11
#define STATUS_ROW_LAST_BLOCK 12
#define STATUS_ROW_SLOWEST_BLOCK 13
#define STATUS_ROW_BLOCKS 14
#define STATUS_ROW_COUNT 15

#define PRESETS_ROW_SLOT 0
#define PRESETS_ROW_STOCK_DEPTH 1
#define PRESETS_ROW_MOD_SWITCH 2
#define PRESETS_ROW_ACTIONS_SECTION 3
#define PRESETS_ROW_OUTPUT_PEQ 4
#define PRESETS_ROW_SAVE 5
#define PRESETS_ROW_LOAD 6
#define PRESETS_ROW_RESET 7
#define PRESETS_ROW_COUNT 8

#define OUTPUT_PEQ_ROW_TARGET 0
#define OUTPUT_PEQ_ROW_ASSIGNED 1
#define OUTPUT_PEQ_ROW_CHOOSE 2
#define OUTPUT_PEQ_ROW_INSPECT 3
#define OUTPUT_PEQ_ROW_CLEAR 4
#define OUTPUT_PEQ_ROW_INFO_SECTION 5
#define OUTPUT_PEQ_ROW_LIVE 6
#define OUTPUT_PEQ_ROW_SWITCHING 7
#define OUTPUT_PEQ_ROW_COUNT 8

#define SIMPLE_ROW_PREAMP 0
#define SIMPLE_ROW_BASS 1
#define SIMPLE_ROW_MIDRANGE 2
#define SIMPLE_ROW_TREBLE 3
#define SIMPLE_ROW_PRESET_SECTION 4
#define SIMPLE_ROW_PRESET_SLOT 5
#define SIMPLE_ROW_SAVE_CURRENT 6
#define SIMPLE_ROW_SAVE_NEXT 7
#define SIMPLE_ROW_UNDO_ENTRY 8
#define SIMPLE_ROW_RESET_EQ 9
#define SIMPLE_ROW_COUNT 10

#define ADV_ROW_PREAMP 0
#define ADV_BAND_ROW_BASE 1
#define ADV_ROW_PRESET_SECTION (ADV_BAND_ROW_BASE + advanced_eq_value_row_count())
#define ADV_ROW_PRESET_SLOT (ADV_ROW_PRESET_SECTION + 1)
#define ADV_ROW_SAVE_CURRENT (ADV_ROW_PRESET_SECTION + 2)
#define ADV_ROW_SAVE_NEXT (ADV_ROW_PRESET_SECTION + 3)
#define ADV_ROW_UNDO_ENTRY (ADV_ROW_PRESET_SECTION + 4)
#define ADV_ROW_RESET_EQ (ADV_ROW_PRESET_SECTION + 5)
#define ADVANCED_ROW_COUNT (ADV_ROW_PRESET_SECTION + 6)

#define SETTINGS_ROW_ENABLED 0
#define SETTINGS_ROW_SCOPE 1
#define SETTINGS_ROW_HPF 2
#define SETTINGS_ROW_SAFETY_SECTION 3
#define SETTINGS_ROW_HEADROOM 4
#define SETTINGS_ROW_ROUTE 5
#define SETTINGS_ROW_STARTUP 6
#define SETTINGS_ROW_COUNT 7

#define MUSIC_ROW_CHOOSE 0
#define MUSIC_ROW_PLAY_PAUSE 1
#define MUSIC_ROW_STOP 2
#define MUSIC_ROW_LOOP 3
#define MUSIC_ROW_VOLUME 4
#define MUSIC_ROW_COUNT 5

#define ABOUT_ROW_CREATOR 0
#define ABOUT_ROW_REPOSITORY 1
#define ABOUT_ROW_LOG_FILE 2
#define ABOUT_ROW_WHAT 3
#define ABOUT_ROW_VERSION 4
#define ABOUT_ROW_PLUGIN 5
#define ABOUT_ROW_EQ_SECTION 6
#define ABOUT_ROW_PRESETS 7
#define ABOUT_ROW_SIMPLE 8
#define ABOUT_ROW_ADVANCED 9
#define ABOUT_ROW_PREAMP 10
#define ABOUT_ROW_HPF 11
#define ABOUT_ROW_OUTPUT_SECTION 12
#define ABOUT_ROW_SPEAKERS 13
#define ABOUT_ROW_ALL_OUTPUTS 14
#define ABOUT_ROW_BYPASS 15
#define ABOUT_ROW_DISTORTION 16
#define ABOUT_ROW_CONTROLS_SECTION 17
#define ABOUT_ROW_CONTROLS 18
#define ABOUT_ROW_DATA_FOLDER 19
#define ABOUT_ROW_COUNT 20

static eqvita_app_state_t g_app_state;
#define g_control (g_app_state.control)
#define g_preset_slot (g_app_state.preset_slot)

static int advanced_eq_value_row_count(void)
{
    return g_control.eq_mode == EQ_MODE_PARAMETRIC ?
        g_control.parametric_filter_count : EQ_BANDS;
}

static int advanced_row_is_eq_value(int row)
{
    return row >= ADV_BAND_ROW_BASE &&
        row < ADV_BAND_ROW_BASE + advanced_eq_value_row_count();
}

static eq_status_t g_status;
static eq_version_t g_version;
static eqvita_media_player_t g_media_player;
static eqvita_media_player_status_t g_media_status;
static eqvita_media_listing_t g_media_listing;
typedef enum browser_purpose
{
    BROWSER_PURPOSE_MUSIC = 0,
    BROWSER_PURPOSE_EQUALIZER_APO = 1
} browser_purpose_t;

typedef struct media_browser_async
{
    SceUID thread_id;
    SceKernelLwMutexWork mutex;
    int mutex_ready;
    volatile int cancel_requested;
    volatile int completed;
    int roots;
    int result;
    eqvita_media_file_filter_t filter;
    char path[EQVITA_MEDIA_MAX_PATH];
    char root_path[EQVITA_MEDIA_MAX_PATH];
    eqvita_media_listing_t listing;
} media_browser_async_t;

static media_browser_async_t g_browser_async;
static browser_purpose_t g_browser_purpose = BROWSER_PURPOSE_MUSIC;
static app_screen_t g_peq_browser_return_screen = SCREEN_PRESETS;
static app_screen_t g_output_peq_return_screen = SCREEN_PRESETS;
static int g_plugin_compatible = 0;
static char g_message[96];
static int g_message_frames = 0;
static uint32_t g_status_log_last_us = 0;
static uint32_t g_diagnostic_drain_last_us = 0;
static uint32_t g_route_refresh_last_us = 0;
static uint32_t g_log_run_id = 0;
static int g_apputil_ready = 0;
static int g_apputil_init_result = 0;
static int g_music_mounted = 0;
static int g_music_mount_result = 0;
static int g_boot_state_save_failed = 0;
static int g_status_failure_count = 0;
static int g_confirm_button = SCE_CTRL_CROSS;
static int g_cancel_button = SCE_CTRL_CIRCLE;

static app_screen_t g_screen = SCREEN_HOME;
static app_screen_t g_eq_return_screen = SCREEN_HOME;
static int g_selected[SCREEN_COUNT];
static int g_scroll_top[SCREEN_COUNT];
static eq_control_t g_eq_entry_control;
static int g_eq_entry_valid = 0;
static eq_route_profile_bank_t g_route_profiles;
static char g_route_profile_sources[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX];
static uint8_t g_route_profile_edit_route = EQ_ROUTE_SPEAKER;
static int g_route_profiles_save_failed = 0;
static int g_exit_prompt_active = 0;
static int g_exit_prompt_selected = 0;
static eq_ui_row_bounds_t g_row_bounds[EQ_UI_MAX_VISIBLE_ROWS];
static int g_row_bound_count = 0;

static SceTouchPanelInfo g_touch_panel;
static int g_touch_panel_ready = 0;
static int g_touch_down = 0;
static int g_touch_start_x = 0;
static int g_touch_start_y = 0;
static int g_touch_last_y = 0;
static int g_touch_moved = 0;

static int clamp(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static int load_theme_index(void)
{
    int index = EQ_UI_DEFAULT_THEME_INDEX;
    eqvita_load_theme_index(EQVITA_DATA_DIR, eq_ui_theme_count(), EQ_UI_DEFAULT_THEME_INDEX, &index);
    return index;
}

static int save_theme_index(int index)
{
    if (index < 0 || index >= eq_ui_theme_count()) {
        index = EQ_UI_DEFAULT_THEME_INDEX;
    }
    return eqvita_save_theme_index(EQVITA_DATA_DIR, index);
}

static int save_active_preset_slot(void)
{
    return eqvita_save_active_preset_slot(EQVITA_DATA_DIR, g_preset_slot);
}

static void app_log(const char *fmt, ...)
{
    char line[256];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(line, sizeof(line), fmt, ap);
    va_end(ap);

    if (len <= 0) {
        return;
    }
    if (len >= (int)sizeof(line)) {
        len = (int)sizeof(line) - 1;
    }
    line[len] = '\0';
    eqvita_append_log_line(EQVITA_DATA_DIR, line);
}

static int prepare_peq_library(void)
{
    int directory_result = eqvita_ensure_peq_dir(EQVITA_DATA_DIR);
    int seed_result = directory_result == 0 ?
        eqvita_seed_peq_file(EQVITA_DATA_DIR,
                             BUNDLED_PEQ_NAME,
                             BUNDLED_PEQ_SOURCE) : -1;

    app_log("peq-library: path=%s ready=%d bundled=%s seed=%d",
            EQVITA_PEQ_DIR,
            directory_result == 0 ? 1 : 0,
            BUNDLED_PEQ_NAME,
            seed_result);
    return directory_result;
}

static int elapsed_us(uint32_t now, uint32_t last, uint32_t interval)
{
    return last == 0 || (uint32_t)(now - last) >= interval;
}

static void init_app_util_resources(void)
{
    SceAppUtilInitParam init_param;
    SceAppUtilBootParam boot_param;

    memset(&init_param, 0, sizeof(init_param));
    memset(&boot_param, 0, sizeof(boot_param));

    g_apputil_init_result = sceAppUtilInit(&init_param, &boot_param);
    if (g_apputil_init_result >= 0) {
        g_apputil_ready = 1;
        g_music_mount_result = sceAppUtilMusicMount();
        g_music_mounted = g_music_mount_result >= 0;
    } else {
        g_music_mount_result = g_apputil_init_result;
    }
}

static int is_ux0_music_path(const char *path)
{
    if (!path) {
        return 0;
    }
    return strncmp(path, "ux0:music", 9) == 0 || strncmp(path, "ux0:/music", 10) == 0;
}

static void ensure_music_mounted(const char *reason)
{
    int res;

    if (g_music_mounted || !g_apputil_ready) {
        return;
    }

    res = sceAppUtilMusicMount();
    g_music_mount_result = res;
    if (res >= 0) {
        g_music_mounted = 1;
        app_log("apputil: music-mount retry reason=%s result=%d", reason ? reason : "", res);
    } else {
        app_log("apputil: music-mount retry-failed reason=%s result=%d", reason ? reason : "", res);
    }
}

static void shutdown_app_util_resources(void)
{
    if (g_music_mounted) {
        sceAppUtilMusicUmount();
        g_music_mounted = 0;
    }
    if (g_apputil_ready) {
        sceAppUtilShutdown();
        g_apputil_ready = 0;
    }
}

static void set_message(const char *fmt, ...);
static void change_screen(app_screen_t screen);

static int preview_is_busy(void)
{
    return g_media_player.decoder &&
        (g_media_status.state == EQVITA_MEDIA_PLAYER_PLAYING ||
         g_media_status.state == EQVITA_MEDIA_PLAYER_PAUSED);
}

static void browser_async_lock(void)
{
    if (g_browser_async.mutex_ready) {
        sceKernelLockLwMutex(&g_browser_async.mutex, 1, NULL);
    }
}

static void browser_async_unlock(void)
{
    if (g_browser_async.mutex_ready) {
        sceKernelUnlockLwMutex(&g_browser_async.mutex, 1);
    }
}

static void browser_async_init(void)
{
    memset(&g_browser_async, 0, sizeof(g_browser_async));
    g_browser_async.thread_id = -1;
    if (sceKernelCreateLwMutex(&g_browser_async.mutex, "eqvita_browser", 0, 1, NULL) >= 0) {
        g_browser_async.mutex_ready = 1;
    }
}

static int browser_async_is_loading(void)
{
    int loading;

    browser_async_lock();
    loading = g_browser_async.thread_id >= 0 && !g_browser_async.completed;
    browser_async_unlock();
    return loading;
}

static int browser_async_thread(unsigned int args, void *argp)
{
    media_browser_async_t *job = NULL;
    int result;

    if (args == sizeof(job) && argp) {
        memcpy(&job, argp, sizeof(job));
    }
    if (!job) {
        sceKernelExitThread(-1);
        return -1;
    }

    result = job->roots ? eqvita_media_browser_read_roots(&job->listing) :
             job->root_path[0] ?
                eqvita_media_browser_read_dir_filtered_at_root(&job->listing,
                                                                job->path,
                                                                job->filter,
                                                                job->root_path) :
                eqvita_media_browser_read_dir_filtered(&job->listing,
                                                        job->path,
                                                        job->filter);

    browser_async_lock();
    job->result = result;
    job->completed = 1;
    browser_async_unlock();

    sceKernelExitThread(0);
    return 0;
}

static void browser_async_cancel(void)
{
    SceUID thread_id;

    browser_async_lock();
    g_browser_async.cancel_requested = 1;
    thread_id = g_browser_async.thread_id;
    browser_async_unlock();

    if (thread_id >= 0) {
        sceKernelWaitThreadEnd(thread_id, NULL, NULL);
    }

    browser_async_lock();
    g_browser_async.thread_id = -1;
    g_browser_async.completed = 0;
    g_browser_async.cancel_requested = 0;
    g_browser_async.result = 0;
    browser_async_unlock();
}

static int browser_async_start(const char *path,
                               int roots,
                               eqvita_media_file_filter_t filter,
                               const char *root_path)
{
    media_browser_async_t *job = &g_browser_async;
    SceUID thread_id;
    int ret;

    browser_async_cancel();

    if (roots || root_path) {
        memset(&g_media_listing, 0, sizeof(g_media_listing));
    }

    if (!roots && is_ux0_music_path(path) && !g_music_mounted) {
        ensure_music_mounted("browser-open");
    }

    browser_async_lock();
    g_browser_async.cancel_requested = 0;
    g_browser_async.completed = 0;
    g_browser_async.roots = roots ? 1 : 0;
    g_browser_async.result = -1;
    g_browser_async.filter = filter;
    g_browser_async.path[0] = '\0';
    g_browser_async.root_path[0] = '\0';
    if (path) {
        snprintf(g_browser_async.path, sizeof(g_browser_async.path), "%s", path);
    }
    if (root_path) {
        snprintf(g_browser_async.root_path, sizeof(g_browser_async.root_path), "%s", root_path);
    }
    memset(&g_browser_async.listing, 0, sizeof(g_browser_async.listing));
    browser_async_unlock();

    g_selected[SCREEN_MUSIC_BROWSER] = 0;
    g_scroll_top[SCREEN_MUSIC_BROWSER] = 0;
    change_screen(SCREEN_MUSIC_BROWSER);
    set_message(roots ? "Loading storage..." : "Loading folder...");

    thread_id = sceKernelCreateThread("eqvita_browser",
                                      browser_async_thread,
                                      BROWSER_THREAD_PRIORITY,
                                      BROWSER_THREAD_STACK,
                                      0,
                                      0,
                                      NULL);
    if (thread_id < 0) {
        set_message("Could not start browser (%d)", thread_id);
        return thread_id;
    }

    browser_async_lock();
    g_browser_async.thread_id = thread_id;
    browser_async_unlock();

    ret = sceKernelStartThread(thread_id, sizeof(job), &job);
    if (ret < 0) {
        sceKernelDeleteThread(thread_id);
        browser_async_lock();
        g_browser_async.thread_id = -1;
        browser_async_unlock();
        set_message("Could not start browser (%d)", ret);
        return ret;
    }

    return 0;
}

static void browser_async_poll(void)
{
    char path[EQVITA_MEDIA_MAX_PATH];
    SceUID thread_id;
    int completed;
    int cancelled;
    int roots;
    int result;

    browser_async_lock();
    completed = g_browser_async.completed;
    if (!completed) {
        browser_async_unlock();
        return;
    }
    thread_id = g_browser_async.thread_id;
    cancelled = g_browser_async.cancel_requested;
    roots = g_browser_async.roots;
    result = g_browser_async.result;
    if (!cancelled && result >= 0) {
        g_media_listing = g_browser_async.listing;
    }
    snprintf(path, sizeof(path), "%s", g_browser_async.path);
    g_browser_async.thread_id = -1;
    g_browser_async.completed = 0;
    browser_async_unlock();

    if (thread_id >= 0) {
        sceKernelWaitThreadEnd(thread_id, NULL, NULL);
    }

    if (cancelled) {
        return;
    }

    if (result < 0) {
        set_message("Could not open folder (%d)", result);
        app_log("browser: open-failed path=%s error=%d", path, result);
        if (!g_media_listing.path[0]) {
            change_screen(g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO ?
                          g_peq_browser_return_screen : SCREEN_MUSIC);
        }
        return;
    }

    g_selected[SCREEN_MUSIC_BROWSER] = 0;
    g_scroll_top[SCREEN_MUSIC_BROWSER] = 0;
    if (result == 0) {
        set_message(roots ? "No storage found" :
                    g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO ?
                        "No .txt configs here" : "No music files here");
        if (roots) {
            app_log("browser: no-storage result=%d", result);
        }
    }
}

static void browser_async_shutdown(void)
{
    browser_async_cancel();
    if (g_browser_async.mutex_ready) {
        sceKernelDeleteLwMutex(&g_browser_async.mutex);
        g_browser_async.mutex_ready = 0;
    }
}

static uint32_t analog_navigation_buttons(const SceCtrlData *pad)
{
    int lx;
    int ly;
    uint32_t buttons = 0;

    if (!pad) {
        return 0;
    }

    lx = (int)pad->lx - ANALOG_CENTER;
    ly = (int)pad->ly - ANALOG_CENTER;

    if (ly <= -ANALOG_NAV_DEADZONE) {
        buttons |= SCE_CTRL_UP;
    } else if (ly >= ANALOG_NAV_DEADZONE) {
        buttons |= SCE_CTRL_DOWN;
    }
    if (lx <= -ANALOG_NAV_DEADZONE) {
        buttons |= SCE_CTRL_LEFT;
    } else if (lx >= ANALOG_NAV_DEADZONE) {
        buttons |= SCE_CTRL_RIGHT;
    }

    return buttons;
}

static const char *startup_source_str(eqvita_startup_source_t source)
{
    switch (source) {
        case EQVITA_STARTUP_SOURCE_BOOT: return "boot";
        case EQVITA_STARTUP_SOURCE_PRESET: return "preset";
        case EQVITA_STARTUP_SOURCE_LEGACY_PRESET: return "legacy-preset";
        case EQVITA_STARTUP_SOURCE_DEFAULT:
        default: return "default";
    }
}

static const char *route_str(uint8_t r)
{
    switch (r) {
        case EQ_ROUTE_SPEAKER: return "Vita speakers";
        case EQ_ROUTE_HEADPHONES: return "Wired headphones";
        case EQ_ROUTE_BLUETOOTH: return "Bluetooth audio";
        default: return "Unknown";
    }
}

static const char *bypass_reason_str(uint8_t r)
{
    switch (r) {
        case EQ_BYPASS_NONE: return "Active";
        case EQ_BYPASS_DISABLED: return "Disabled";
        case EQ_BYPASS_SPEAKER_ONLY: return "Waiting for speakers";
        case EQ_BYPASS_UNKNOWN_ROUTE: return "Unknown output";
        case EQ_BYPASS_INVALID_PORT: return "Invalid port";
        case EQ_BYPASS_BUFFER_TOO_LARGE: return "Large buffer";
        case EQ_BYPASS_COPY_FAILED: return "Copy failed";
        case EQ_BYPASS_UNSUPPORTED_FORMAT: return "Unsupported";
        case EQ_BYPASS_AUDIO_BUSY: return "Audio busy";
        case EQ_BYPASS_NO_ROUTE_PROFILE: return "No PEQ for this output";
        default: return "Bypassed";
    }
}

static const char *headroom_mode_str(uint8_t mode)
{
    switch (mode) {
        case EQ_HEADROOM_LOUD: return "Loud";
        case EQ_HEADROOM_RAW: return "Direct";
        case EQ_HEADROOM_EXACT: return "APO Exact";
        case EQ_HEADROOM_SAFE:
        default: return "Safe";
    }
}

static const char *diag_event_str(uint32_t type)
{
    switch (type) {
        case EQ_DIAG_EVENT_CLIP_BLOCK: return "clip";
        case EQ_DIAG_EVENT_BYPASS_BLOCK: return "bypass";
        case EQ_DIAG_EVENT_SLOW_BLOCK: return "slow";
        case EQ_DIAG_EVENT_OUTPUT_ERROR: return "output-error";
        case EQ_DIAG_EVENT_COPY_ERROR: return "copy-error";
        case EQ_DIAG_EVENT_DSP_RETARGET: return "dsp-retarget";
        case EQ_DIAG_EVENT_PORT_OPEN: return "port-open";
        case EQ_DIAG_EVENT_PORT_SET_CONFIG: return "port-config";
        case EQ_DIAG_EVENT_PORT_RELEASE: return "port-release";
        case EQ_DIAG_EVENT_CONTROL_SET: return "control-set";
        case EQ_DIAG_EVENT_DROPPED_EVENTS: return "dropped";
        case EQ_DIAG_EVENT_ACTIVE_SAMPLE: return "active";
        case EQ_DIAG_EVENT_CONFIG_MISMATCH: return "config-mismatch";
        default: return "unknown";
    }
}

static const char *diag_port_type_str(uint8_t type)
{
    switch (type) {
        case 0: return "main";
        case 1: return "bgm";
        case 2: return "voice";
        default: return "unknown";
    }
}

static const char *eq_target_str(void)
{
    if (g_route_profiles.enabled_mask != 0) {
        return "Output PEQs";
    }
    return g_control.speaker_only ? "Vita speakers" : "All outputs";
}

static int route_profile_count(void)
{
    int count = 0;
    for (uint8_t route = EQ_ROUTE_SPEAKER; route <= EQ_ROUTE_BLUETOOTH; ++route) {
        if (eq_route_profile_bank_has_route(&g_route_profiles, route)) {
            count++;
        }
    }
    return count;
}

static int route_profile_edit_index(void)
{
    return eq_route_profile_index(g_route_profile_edit_route);
}

static const char *route_profile_source(uint8_t route)
{
    int index = eq_route_profile_index(route);
    if (index < 0 || !eq_route_profile_bank_has_route(&g_route_profiles, route)) {
        return "Not assigned";
    }
    return g_route_profile_sources[index][0] ? g_route_profile_sources[index] : "Assigned PEQ";
}

static const char *button_name(int button)
{
    return button == SCE_CTRL_CIRCLE ? "Circle" : "Cross";
}

static void init_button_mapping(void)
{
    int enter_button = SCE_SYSTEM_PARAM_ENTER_BUTTON_CROSS;
    if (sceAppUtilSystemParamGetInt(SCE_SYSTEM_PARAM_ID_ENTER_BUTTON, &enter_button) >= 0 &&
        enter_button == SCE_SYSTEM_PARAM_ENTER_BUTTON_CIRCLE) {
        g_confirm_button = SCE_CTRL_CIRCLE;
        g_cancel_button = SCE_CTRL_CROSS;
    } else {
        g_confirm_button = SCE_CTRL_CROSS;
        g_cancel_button = SCE_CTRL_CIRCLE;
    }
}

static int save_preset(void);
static int save_boot_state(void);
static void change_screen(app_screen_t screen);
static void open_music_browser_roots(void);
static void open_music_browser_path(const char *path);
static void open_peq_browser_root(void);
static void open_apo_browser_path(const char *path);

static void set_message(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_message, sizeof(g_message), fmt, ap);
    va_end(ap);
    g_message_frames = 180;
    app_log("message: %s", g_message);
}

static void apply_theme_index(int index)
{
    int set_res;
    int save_res;

    if (index < 0 || index >= eq_ui_theme_count()) {
        index = EQ_UI_DEFAULT_THEME_INDEX;
    }

    set_res = eq_ui_set_theme(index);
    save_res = save_theme_index(index);
    if (set_res < 0) {
        set_message("Theme changed, icon sheet missing");
    } else if (save_res < 0) {
        set_message("Theme changed, save failed (%d)", save_res);
    } else {
        set_message("Theme: %s", eq_ui_theme_name(index));
    }
}

static eq_route_t detect_route_user(void)
{
    uint32_t flags = 0;
    uint32_t vol_ctrl = 0;
    int muted = 0;
    int avls = 0;

    if (sceAVConfigGetConnectedAudioDevice(&flags) >= 0) {
        int have_vol = (sceAVConfigGetVolCtrlEnable(&vol_ctrl, &muted, &avls) >= 0);
        (void)muted;
        (void)avls;

        if (flags & SCE_AVCONFIG_AUDIO_DEVICE_BT_AUDIO_OUT) {
            if (!have_vol || vol_ctrl == SCE_AVCONFIG_VOLCTRL_BLUETOOTH) {
                return EQ_ROUTE_BLUETOOTH;
            }
        }

        if (flags & SCE_AVCONFIG_AUDIO_DEVICE_AUDIO_OUT) {
            if (!have_vol || vol_ctrl == SCE_AVCONFIG_VOLCTRL_ONBOARD) {
                return EQ_ROUTE_HEADPHONES;
            }
        }

        if ((flags & (SCE_AVCONFIG_AUDIO_DEVICE_VITA_0 | SCE_AVCONFIG_AUDIO_DEVICE_VITA_8)) &&
            !(flags & (SCE_AVCONFIG_AUDIO_DEVICE_AUDIO_OUT | SCE_AVCONFIG_AUDIO_DEVICE_BT_AUDIO_OUT))) {
            return EQ_ROUTE_SPEAKER;
        }
    }

    {
        SceCtrlData data;
        memset(&data, 0, sizeof(data));
        if (sceCtrlPeekBufferPositive(0, &data, 1) >= 0 &&
            (data.buttons & SCE_CTRL_HEADPHONE)) {
            return EQ_ROUTE_HEADPHONES;
        }
    }

    return EQ_ROUTE_UNKNOWN;
}

static uint32_t audio_budget_us(uint32_t frames, uint32_t sample_rate)
{
    if (frames == 0 || sample_rate == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)frames * 1000000u) / sample_rate);
}

static void maybe_log_status(void)
{
    static uint8_t last_route = 0xffu;
    static uint8_t last_reason = 0xffu;
    static uint8_t last_active = 0xffu;
    static int32_t last_clips = -1;
    static uint32_t last_busy_bypass = 0xffffffffu;
    static uint32_t last_unknown_port = 0xffffffffu;
    static uint32_t last_max_us = 0xffffffffu;
    static uint32_t last_max_total_us = 0xffffffffu;
    static uint32_t last_max_dsp_us = 0xffffffffu;
    static int32_t last_min_margin_us = INT32_MAX;
    uint32_t now;
    uint32_t interval;
    int preview_busy;
    int changed;
    int changed_core;
    int changed_detail;
    int force_log;

    if (!g_plugin_compatible) {
        return;
    }

    now = sceKernelGetProcessTimeLow();
    preview_busy = preview_is_busy();
    interval = preview_busy ? STATUS_LOG_PREVIEW_INTERVAL_US : STATUS_LOG_INTERVAL_US;
    force_log = elapsed_us(now, g_status_log_last_us, interval);
    changed_core = (g_status.route != last_route ||
        g_status.bypass_reason != last_reason ||
        g_status.eq_active != last_active ||
        g_status.debug_busy_bypass_count != last_busy_bypass ||
        g_status.debug_unknown_port_count != last_unknown_port);
    changed_detail = (g_status.clip_events != last_clips ||
        g_status.debug_max_us != last_max_us ||
        g_status.debug_max_total_us != last_max_total_us ||
        g_status.debug_max_dsp_us != last_max_dsp_us ||
        g_status.debug_min_margin_us != last_min_margin_us);
    changed = changed_core || (!preview_busy && changed_detail);

    if (!force_log && !changed) {
        return;
    }

    if (force_log || changed) {
        uint32_t budget_us = audio_budget_us(g_status.debug_len, g_status.sample_rate);
        g_status_log_last_us = now;
        app_log("status: run=%08x route=%s active=%u reason=%s sr=%u port=%u len=%u ch=%u runs=%u ports=%u busy=%u unknown=%u last_us=%u max_us=%u clips=%d peak_l=%u peak_r=%u",
            g_log_run_id,
            route_str(g_status.route),
            g_status.eq_active,
            bypass_reason_str(g_status.bypass_reason),
            g_status.sample_rate,
            g_status.debug_port,
            g_status.debug_len,
            g_status.debug_channels,
            g_status.debug_run_count,
            g_status.debug_active_ports,
            g_status.debug_busy_bypass_count,
            g_status.debug_unknown_port_count,
            g_status.debug_last_us,
            g_status.debug_max_us,
            g_status.clip_events,
            g_status.peak_l,
            g_status.peak_r);
        app_log("status-max: run=%08x max_us=%u max_budget_us=%u max_port=%u max_len=%u max_sr=%u max_ch=%u max_route=%s max_reason=%s max_clips=%d",
            g_log_run_id,
            g_status.debug_max_us,
            g_status.debug_max_budget_us,
            g_status.debug_max_port,
            g_status.debug_max_len,
            g_status.debug_max_sample_rate,
            g_status.debug_max_channels,
            route_str((uint8_t)g_status.debug_max_route),
            bypass_reason_str((uint8_t)g_status.debug_max_bypass_reason),
            g_status.debug_max_clip_count);
        app_log("status-stage: run=%08x control_us=%u registry_us=%u route_us=%u copy_in_us=%u retarget_us=%u dsp_us=%u copy_out_us=%u original_us=%u status_us=%u",
            g_log_run_id,
            g_status.debug_max_stage_control_us,
            g_status.debug_max_stage_registry_us,
            g_status.debug_max_stage_route_us,
            g_status.debug_max_stage_copy_in_us,
            g_status.debug_max_stage_retarget_us,
            g_status.debug_max_stage_dsp_us,
            g_status.debug_max_stage_copy_out_us,
            g_status.debug_max_stage_original_us,
            g_status.debug_max_stage_status_us);
        app_log("status-time: run=%08x last_total_us=%u last_budget_us=%u last_margin_us=%d max_total_us=%u max_dsp_us=%u",
            g_log_run_id,
            g_status.debug_last_total_us,
            g_status.debug_last_budget_us,
            g_status.debug_last_margin_us,
            g_status.debug_max_total_us,
            g_status.debug_max_dsp_us);
        app_log("status-margin: run=%08x min_margin_us=%d min_total_us=%u min_budget_us=%u min_port=%u min_len=%u min_sr=%u min_ch=%u",
            g_log_run_id,
            g_status.debug_min_margin_us,
            g_status.debug_min_margin_total_us,
            g_status.debug_min_margin_budget_us,
            g_status.debug_min_margin_port,
            g_status.debug_min_margin_len,
            g_status.debug_min_margin_sample_rate,
            g_status.debug_min_margin_channels);
        app_log("status-margin-stage: run=%08x min_route=%s min_reason=%s dsp_us=%u original_us=%u status_us=%u",
            g_log_run_id,
            route_str((uint8_t)g_status.debug_min_margin_route),
            bypass_reason_str((uint8_t)g_status.debug_min_margin_bypass_reason),
            g_status.debug_min_margin_stage_dsp_us,
            g_status.debug_min_margin_stage_original_us,
            g_status.debug_min_margin_stage_status_us);
        app_log("status-margin-len: run=%08x len256_us=%d len1024_us=%d len2048_us=%d",
            g_log_run_id,
            g_status.debug_min_margin_len_256_us,
            g_status.debug_min_margin_len_1024_us,
            g_status.debug_min_margin_len_2048_us);

        if (budget_us > 0 && g_status.debug_last_us > budget_us) {
            app_log("audio-budget: run=%08x last_us=%u budget_us=%u max_us=%u len=%u sr=%u",
                g_log_run_id,
                g_status.debug_last_us,
                budget_us,
                g_status.debug_max_us,
                g_status.debug_len,
                g_status.sample_rate);
        }

        last_route = g_status.route;
        last_reason = g_status.bypass_reason;
        last_active = g_status.eq_active;
        last_clips = g_status.clip_events;
        last_busy_bypass = g_status.debug_busy_bypass_count;
        last_unknown_port = g_status.debug_unknown_port_count;
        last_max_us = g_status.debug_max_us;
        last_max_total_us = g_status.debug_max_total_us;
        last_max_dsp_us = g_status.debug_max_dsp_us;
        last_min_margin_us = g_status.debug_min_margin_us;
    }
}

static void maybe_log_diagnostics(void)
{
    eq_diag_snapshot_t snapshot;
    uint32_t now;
    uint32_t interval;
    uint32_t events_to_log;
    int preview_busy;

    if (!g_plugin_compatible) {
        return;
    }

    now = sceKernelGetProcessTimeLow();
    preview_busy = preview_is_busy();
    interval = preview_busy ? DIAGNOSTIC_DRAIN_PREVIEW_INTERVAL_US : DIAGNOSTIC_DRAIN_INTERVAL_US;
    if (!elapsed_us(now, g_diagnostic_drain_last_us, interval)) {
        return;
    }
    g_diagnostic_drain_last_us = now;

    memset(&snapshot, 0, sizeof(snapshot));
    if (EqDrainDiagnostics(&snapshot) < 0) {
        return;
    }

    if (snapshot.version != EQ_DIAG_SNAPSHOT_VERSION ||
        snapshot.capacity != EQ_DIAG_MAX_EVENTS_PER_DRAIN) {
        app_log("diag: run=%08x invalid-snapshot version=%u capacity=%u count=%u dropped=%u",
            g_log_run_id,
            snapshot.version,
            snapshot.capacity,
            snapshot.count,
            snapshot.dropped);
        return;
    }

    if (snapshot.dropped > 0) {
        app_log("diag: run=%08x evt=dropped count=%u", g_log_run_id, snapshot.dropped);
    }

    if (snapshot.count > EQ_DIAG_MAX_EVENTS_PER_DRAIN) {
        snapshot.count = EQ_DIAG_MAX_EVENTS_PER_DRAIN;
    }

    events_to_log = snapshot.count;
    if (preview_busy && events_to_log > DIAGNOSTIC_DRAIN_PREVIEW_EVENTS) {
        events_to_log = DIAGNOSTIC_DRAIN_PREVIEW_EVENTS;
        app_log("diag: run=%08x evt=preview-limited count=%u logged=%u",
            g_log_run_id,
            snapshot.count,
            events_to_log);
    }

    for (uint32_t i = 0; i < events_to_log; ++i) {
        const eq_diag_event_t *event = &snapshot.events[i];
        if (event->version != EQ_DIAG_EVENT_VERSION) {
            app_log("diag: run=%08x evt=invalid seq=%u version=%u",
                g_log_run_id,
                event->seq,
                event->version);
            continue;
        }

        app_log("diag-core: run=%08x seq=%u evt=%s port=%d gen=%u type=%s len=%u sr=%u ch=%u route=%s reason=%s ret=%d",
            g_log_run_id,
            event->seq,
            diag_event_str(event->type),
            event->port,
            event->generation,
            diag_port_type_str(event->port_type),
            event->len,
            event->sample_rate,
            event->channels,
            route_str(event->route),
            bypass_reason_str(event->reason),
            event->ret);
        app_log("diag-time: run=%08x seq=%u elapsed_us=%u budget_us=%u dirty=%u flags=0x%02x",
            g_log_run_id,
            event->seq,
            event->elapsed_us,
            event->budget_us,
            event->dirty_counter,
            event->flags);
        app_log("diag-level: run=%08x seq=%u clips=%d in_peak=%u/%u out_peak=%u/%u headroom=%s preamp=%d eff_preamp=%d max_boost=%d hpf=%u",
            g_log_run_id,
            event->seq,
            event->clip_count,
            event->input_peak_l,
            event->input_peak_r,
            event->output_peak_l,
            event->output_peak_r,
            headroom_mode_str(event->headroom_mode),
            event->preamp_mdB,
            event->effective_preamp_mdB,
            event->max_boost_mdB,
            event->hpf_enabled);
    }
}

static void mark_boot_state_dirty(void)
{
    eqvita_app_state_mark_boot_dirty(&g_app_state);
    g_boot_state_save_failed = 0;
}

static void note_status_result(int status_res)
{
    if (status_res >= 0) {
        if (g_status_failure_count > 0) {
            set_message("Plugin status restored");
        }
        g_status_failure_count = 0;
        eqvita_app_state_set_status_stale(&g_app_state, 0);
        return;
    }

    eqvita_app_state_set_status_stale(&g_app_state, 1);
    if (g_status_failure_count == 0) {
        set_message("Plugin status failed (%d)", status_res);
    }
    if (g_status_failure_count < 1000000) {
        g_status_failure_count++;
    }
}

static void poll_plugin_status(void)
{
    if (g_plugin_compatible) {
        note_status_result(EqGetStatus(&g_status));
    }
}

static int apply_control_candidate(const eq_control_t *candidate, int mark_boot_dirty_on_success)
{
    int set_res;
    int profile_res;
    int status_res;
    eq_control_t next;
    eq_route_profile_bank_t next_profiles;
    char next_profile_sources[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX];

    if (!g_plugin_compatible) {
        set_message("Plugin version mismatch");
        return -1;
    }
    if (!candidate) {
        return -1;
    }

    next = *candidate;
    next.route_hint = (uint8_t)detect_route_user();
    next.dirty_counter = eq_control_next_dirty_counter(g_control.dirty_counter);
    next_profiles = g_route_profiles;
    memcpy(next_profile_sources, g_route_profile_sources, sizeof(next_profile_sources));

    if (mark_boot_dirty_on_success &&
        eq_route_profile_bank_has_route(&next_profiles, g_route_profile_edit_route)) {
        if (eqvita_app_state_prepare_route_profile_candidate(
                &g_route_profiles, g_route_profile_sources,
                g_route_profile_edit_route, &next,
                &next_profiles, next_profile_sources) < 0) {
            set_message("Output PEQ state is invalid");
            return -1;
        }
    }
    if (eq_route_profile_bank_validate(&next_profiles) < 0) {
        set_message("Output PEQ state is invalid");
        return -1;
    }

    set_res = EqSetControl(&next);
    if (set_res < 0) {
        set_message("Plugin communication failed (%d)", set_res);
        return set_res;
    }
    profile_res = EqSetRouteProfiles(&next_profiles);
    if (profile_res < 0) {
        (void)EqSetControl(&g_control);
        set_message("Output PEQ sync failed (%d)", profile_res);
        return profile_res;
    }
    status_res = EqGetStatus(&g_status);

    g_control = next;
    g_route_profiles = next_profiles;
    memcpy(g_route_profile_sources, next_profile_sources, sizeof(g_route_profile_sources));
    if (mark_boot_dirty_on_success) {
        mark_boot_state_dirty();
        eqvita_app_state_mark_current_preset_dirty(&g_app_state);
        {
            int save_res = eqvita_save_route_profiles(EQVITA_DATA_DIR,
                                                       &g_route_profiles,
                                                       g_route_profile_sources);
            g_route_profiles_save_failed = save_res < 0;
            if (save_res < 0) {
                app_log("output-peq: save-failed error=%d", save_res);
            }
        }
    }
    note_status_result(status_res);
    return 0;
}

static void refresh_route_hint(void)
{
    uint32_t now;
    uint32_t interval;
    uint8_t route;
    if (!g_plugin_compatible) {
        return;
    }

    now = sceKernelGetProcessTimeLow();
    interval = preview_is_busy() ? ROUTE_REFRESH_PREVIEW_INTERVAL_US : ROUTE_REFRESH_INTERVAL_US;
    if (!elapsed_us(now, g_route_refresh_last_us, interval)) {
        return;
    }
    g_route_refresh_last_us = now;

    route = (uint8_t)detect_route_user();
    if (g_control.route_hint != route) {
        eq_control_t next = g_control;
        next.route_hint = route;
        if (apply_control_candidate(&next, 0) < 0) {
            set_message("Route update failed");
        }
    }
}

static void toggle_enabled(void)
{
    eq_control_t next = g_control;
    next.enabled = !next.enabled;
    apply_control_candidate(&next, 1);
}

static void toggle_speaker_only(void)
{
    eq_control_t next = g_control;
    if (g_route_profiles.enabled_mask != 0) {
        set_message("Output PEQs choose the route automatically");
        return;
    }
    next.speaker_only = !next.speaker_only;
    apply_control_candidate(&next, 1);
}

static void toggle_hpf(void)
{
    eq_control_t next = g_control;
    if (next.eq_mode == EQ_MODE_PARAMETRIC) {
        set_message("Bass guard is off while PEQ is active");
        return;
    }
    eq_control_set_hpf_enabled(&next, !eq_control_hpf_enabled(&next));
    apply_control_candidate(&next, 1);
}

static void adjust_headroom_mode(int delta)
{
    eq_control_t next = g_control;
    int mode = (int)eq_control_get_headroom_mode(&next) + delta;
    if (next.eq_mode == EQ_MODE_PARAMETRIC) {
        set_message("APO Exact is locked while PEQ is active");
        return;
    }
    if (mode < 0) mode = EQ_HEADROOM_EXACT;
    if (mode > EQ_HEADROOM_EXACT) mode = EQ_HEADROOM_SAFE;
    eq_control_set_headroom_mode(&next, (uint8_t)mode);
    apply_control_candidate(&next, 1);
}

static void adjust_preamp(int delta)
{
    eq_control_t next = g_control;
    int v = next.preamp_mdB + delta;
    next.preamp_mdB = clamp(v, EQ_PREAMP_MIN_MDB, EQ_PREAMP_MAX_MDB);
    apply_control_candidate(&next, 1);
}

static void adjust_band(int idx, int delta)
{
    eq_control_t next = g_control;
    int v;
    if (idx < 0 || idx >= EQ_BANDS) {
        return;
    }
    v = next.band_gain_mdB[idx] + delta;
    next.band_gain_mdB[idx] = clamp(v, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    apply_control_candidate(&next, 1);
}

static void adjust_parametric_operation(int idx, int delta)
{
    eq_control_t next = g_control;
    eq_parametric_filter_t *filter;
    int gain;

    if (next.eq_mode != EQ_MODE_PARAMETRIC ||
        idx < 0 || idx >= next.parametric_filter_count) {
        return;
    }
    filter = &next.parametric_filters[idx];
    if (filter->type < EQ_FILTER_PEAK || filter->type > EQ_FILTER_HIGH_SHELF) {
        return;
    }
    gain = filter->data.filter.gain_mdB + delta;
    filter->data.filter.gain_mdB = clamp(gain,
                                         -EQ_PARAMETRIC_MAX_ABS_GAIN_MDB,
                                         EQ_PARAMETRIC_MAX_ABS_GAIN_MDB);
    apply_control_candidate(&next, 1);
}

static int save_preset(void)
{
    return eqvita_save_preset(EQVITA_DATA_DIR, g_preset_slot, &g_control);
}

static int save_boot_state(void)
{
    eq_control_t boot_control = g_control;
    int res;
    int profiles_res;

    boot_control.route_hint = (uint8_t)detect_route_user();
    res = eqvita_save_boot_state(EQVITA_DATA_DIR, &boot_control);
    profiles_res = eqvita_save_route_profiles(EQVITA_DATA_DIR,
                                               &g_route_profiles,
                                               g_route_profile_sources);
    g_route_profiles_save_failed = profiles_res < 0;
    if (res >= 0 && profiles_res >= 0) {
        eqvita_app_state_mark_boot_saved(&g_app_state);
        g_boot_state_save_failed = 0;
    } else {
        g_boot_state_save_failed = 1;
    }
    return res < 0 ? res : profiles_res;
}

static int persist_active_preset_state(const char *reason, int *out_slot_res, int *out_boot_res)
{
    int slot_res = save_active_preset_slot();
    int boot_res = save_boot_state();

    if (out_slot_res) {
        *out_slot_res = slot_res;
    }
    if (out_boot_res) {
        *out_boot_res = boot_res;
    }

    app_log("persist: reason=%s slot=%d active_slot=%d boot=%d",
            reason ? reason : "unknown",
            g_preset_slot + 1,
            slot_res,
            boot_res);

    return (slot_res >= 0 && boot_res >= 0) ? 0 : -1;
}

static void adjust_preset_slot(int delta)
{
    int previous_slot = g_preset_slot;

    eqvita_app_state_adjust_preset_slot(&g_app_state, delta);
    if (g_preset_slot != previous_slot) {
        int slot_res = save_active_preset_slot();
        app_log("persist: reason=slot-select slot=%d active_slot=%d", g_preset_slot + 1, slot_res);
        if (slot_res < 0) {
            set_message("Slot save failed (%d)", slot_res);
        }
    }
}

static int load_preset(void)
{
    eq_control_t loaded;
    int legacy_loaded = 0;

    if (eqvita_load_preset(EQVITA_DATA_DIR, g_preset_slot, &loaded, &legacy_loaded) < 0) {
        return -1;
    }

    if (apply_control_candidate(&loaded, 1) < 0) {
        return PRESET_SYNC_FAILED;
    }
    eqvita_app_state_mark_current_preset_saved(&g_app_state);
    g_eq_entry_control = g_control;
    g_eq_entry_valid = 1;
    return legacy_loaded ? 1 : 0;
}

static void save_boot_state_with_message(void)
{
    int slot_res;
    int boot_res;

    if (persist_active_preset_state("startup-save", &slot_res, &boot_res) >= 0) {
        set_message("Startup settings saved");
    } else if (slot_res < 0) {
        set_message("Startup saved, slot save failed (%d)", slot_res);
    } else {
        set_message("Startup save failed (%d)", boot_res);
    }
}

static int save_preset_with_message_result(void)
{
    int res = save_preset();
    if (res >= 0) {
        int slot_res;
        int boot_res;
        eqvita_app_state_mark_current_preset_saved(&g_app_state);
        g_eq_entry_control = g_control;
        g_eq_entry_valid = 1;
        if (persist_active_preset_state("preset-save", &slot_res, &boot_res) >= 0) {
            set_message("Saved preset %d", g_preset_slot + 1);
        } else if (slot_res < 0) {
            set_message("Preset saved, slot save failed (%d)", slot_res);
        } else {
            set_message("Preset saved, startup save failed (%d)", boot_res);
        }
        return 0;
    } else {
        set_message("Save failed (%d)", res);
        return res;
    }
}

static void save_preset_with_message(void)
{
    (void)save_preset_with_message_result();
}

static void save_as_next_preset(void)
{
    int previous_slot = g_preset_slot;
    int next_slot = (g_preset_slot + 1) % EQVITA_PRESET_SLOT_COUNT;
    g_preset_slot = next_slot;
    if (save_preset() >= 0) {
        int slot_res;
        int boot_res;
        eqvita_app_state_mark_current_preset_saved(&g_app_state);
        g_eq_entry_control = g_control;
        g_eq_entry_valid = 1;
        if (persist_active_preset_state("preset-save-next", &slot_res, &boot_res) >= 0) {
            set_message("Saved preset %d", g_preset_slot + 1);
        } else if (slot_res < 0) {
            set_message("Preset saved, slot save failed (%d)", slot_res);
        } else {
            set_message("Preset saved, startup save failed (%d)", boot_res);
        }
    } else {
        g_preset_slot = previous_slot;
        set_message("Save failed");
    }
}

static void load_preset_with_message(void)
{
    int res = load_preset();
    if (res > 0) {
        persist_active_preset_state("preset-load-legacy", NULL, NULL);
        set_message("Imported legacy preset %d", g_preset_slot + 1);
    } else if (res == 0) {
        int slot_res;
        int boot_res;
        if (persist_active_preset_state("preset-load", &slot_res, &boot_res) >= 0) {
            set_message("Loaded preset %d", g_preset_slot + 1);
        } else if (slot_res < 0) {
            set_message("Preset loaded, slot save failed (%d)", slot_res);
        } else {
            set_message("Preset loaded, startup save failed (%d)", boot_res);
        }
    } else if (res != PRESET_SYNC_FAILED) {
        set_message("Load failed (%d)", res);
    }
}

static int select_route_profile_target(uint8_t route, int load_for_edit)
{
    int index = eq_route_profile_index(route);
    uint8_t previous_edit_route = g_route_profile_edit_route;
    uint8_t previous_selected_route = g_route_profiles.selected_route;

    if (index < 0) {
        route = EQ_ROUTE_SPEAKER;
        index = 0;
    }
    g_route_profile_edit_route = route;
    g_route_profiles.selected_route = route;

    if (load_for_edit && eq_route_profile_bank_has_route(&g_route_profiles, route)) {
        eq_control_t next = g_route_profiles.profiles[index];
        next.enabled = g_control.enabled;
        next.route_hint = (uint8_t)detect_route_user();
        if (apply_control_candidate(&next, 0) == 0) {
            g_eq_entry_control = g_control;
            g_eq_entry_valid = 1;
        } else {
            g_route_profile_edit_route = previous_edit_route;
            g_route_profiles.selected_route = previous_selected_route;
            return -1;
        }
    }
    return 0;
}

static void adjust_route_profile_target(int delta)
{
    int index = route_profile_edit_index();
    int next_index;

    if (index < 0) {
        index = 0;
    }
    next_index = (index + (delta >= 0 ? 1 : -1) + (int)EQ_ROUTE_PROFILE_COUNT) %
        (int)EQ_ROUTE_PROFILE_COUNT;
    if (select_route_profile_target((uint8_t)(EQ_ROUTE_SPEAKER + next_index), 1) == 0) {
        int save_res = eqvita_save_route_profiles(EQVITA_DATA_DIR,
                                                   &g_route_profiles,
                                                   g_route_profile_sources);
        g_route_profiles_save_failed = save_res < 0;
        if (save_res < 0) {
            set_message("Output selection save failed (%d)", save_res);
        }
    }
}

static void open_output_peq_screen(app_screen_t return_screen)
{
    uint8_t live_route = g_status.route != EQ_ROUTE_UNKNOWN ?
        g_status.route : (uint8_t)detect_route_user();
    uint8_t target = g_route_profiles.selected_route;

    if (eq_route_profile_index(live_route) >= 0) {
        target = live_route;
    }
    if (eq_route_profile_index(target) < 0) {
        target = EQ_ROUTE_SPEAKER;
    }
    g_output_peq_return_screen = return_screen;
    select_route_profile_target(target, 1);
    change_screen(SCREEN_OUTPUT_PEQ);
}

static void clear_route_profile(void)
{
    int index = route_profile_edit_index();
    eq_control_t next = g_control;
    eq_control_t previous_profile;
    uint8_t previous_enabled_mask;
    uint8_t previous_selected_route;
    uint32_t previous_dirty_counter;
    char previous_source[EQ_ROUTE_PROFILE_SOURCE_NAME_MAX];

    if (index < 0 || !eq_route_profile_bank_has_route(&g_route_profiles,
                                                       g_route_profile_edit_route)) {
        set_message("No PEQ is assigned to this output");
        return;
    }

    previous_profile = g_route_profiles.profiles[index];
    previous_enabled_mask = g_route_profiles.enabled_mask;
    previous_selected_route = g_route_profiles.selected_route;
    previous_dirty_counter = g_route_profiles.dirty_counter;
    memcpy(previous_source, g_route_profile_sources[index], sizeof(previous_source));

    g_route_profiles.enabled_mask &= (uint8_t)~eq_route_profile_bit(g_route_profile_edit_route);
    eq_control_init_defaults(&g_route_profiles.profiles[index]);
    g_route_profiles.profiles[index].enabled = 1;
    g_route_profiles.profiles[index].speaker_only = 0;
    g_route_profile_sources[index][0] = '\0';
    eq_route_profile_bank_touch(&g_route_profiles);

    if (g_route_profiles.enabled_mask == 0) {
        eq_control_init_defaults(&next);
    }
    if (apply_control_candidate(&next, 1) < 0) {
        g_route_profiles.profiles[index] = previous_profile;
        g_route_profiles.enabled_mask = previous_enabled_mask;
        g_route_profiles.selected_route = previous_selected_route;
        g_route_profiles.dirty_counter = previous_dirty_counter;
        memcpy(g_route_profile_sources[index], previous_source, sizeof(previous_source));
        return;
    }
    (void)save_boot_state();
    set_message("Cleared %s PEQ", route_str(g_route_profile_edit_route));
    app_log("output-peq: cleared route=%s remaining=%d",
            route_str(g_route_profile_edit_route), route_profile_count());
}

static void open_music_browser_roots(void)
{
    g_browser_purpose = BROWSER_PURPOSE_MUSIC;
    browser_async_start(NULL, 1, EQVITA_MEDIA_FILTER_AUDIO, NULL);
}

static void open_music_browser_path(const char *path)
{
    g_browser_purpose = BROWSER_PURPOSE_MUSIC;
    browser_async_start(path, 0, EQVITA_MEDIA_FILTER_AUDIO, NULL);
}

static void open_peq_browser_root(void)
{
    int result = prepare_peq_library();

    if (result < 0) {
        set_message("Could not create %s", EQVITA_PEQ_DIR);
        app_log("peq-library: create-failed path=%s error=%d", EQVITA_PEQ_DIR, result);
        return;
    }
    g_browser_purpose = BROWSER_PURPOSE_EQUALIZER_APO;
    browser_async_start(EQVITA_PEQ_DIR, 0,
                        EQVITA_MEDIA_FILTER_EQUALIZER_APO,
                        EQVITA_PEQ_DIR);
}

static void open_apo_browser_path(const char *path)
{
    if (!eqvita_media_browser_path_is_within(path, EQVITA_PEQ_DIR)) {
        set_message("PEQ browser is limited to %s", EQVITA_PEQ_DIR);
        change_screen(g_peq_browser_return_screen);
        return;
    }
    g_browser_purpose = BROWSER_PURPOSE_EQUALIZER_APO;
    browser_async_start(path, 0,
                        EQVITA_MEDIA_FILTER_EQUALIZER_APO,
                        EQVITA_PEQ_DIR);
}

static void leave_music_browser(void)
{
    char parent[EQVITA_MEDIA_MAX_PATH];

    browser_async_cancel();

    if (g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO) {
        if (!g_media_listing.path[0] ||
            eqvita_media_browser_paths_equal(g_media_listing.path, EQVITA_PEQ_DIR)) {
            change_screen(g_peq_browser_return_screen);
            return;
        }
        if (eqvita_media_browser_parent_path(parent, sizeof(parent), g_media_listing.path) == 0 &&
            eqvita_media_browser_path_is_within(parent, EQVITA_PEQ_DIR)) {
            open_apo_browser_path(parent);
            return;
        }
        change_screen(g_peq_browser_return_screen);
        return;
    }

    if (g_media_listing.path[0] &&
        eqvita_media_browser_parent_path(parent, sizeof(parent), g_media_listing.path) == 0) {
        open_music_browser_path(parent);
        return;
    }
    if (eqvita_media_browser_is_root_path(g_media_listing.path)) {
        open_music_browser_roots();
        return;
    }

    change_screen(SCREEN_MUSIC);
}

static void import_equalizer_apo_file(const char *path)
{
    eq_control_t base;
    eq_control_t imported;
    eq_control_t previous_profile;
    eqvita_apo_import_result_t result;
    int profile_index = route_profile_edit_index();
    const char *source_name = path;
    size_t peq_root_length = strlen(EQVITA_PEQ_DIR);
    uint8_t previous_enabled_mask;
    uint8_t previous_selected_route;
    uint32_t previous_dirty_counter;
    char previous_source[EQ_ROUTE_PROFILE_SOURCE_NAME_MAX];

    if (profile_index < 0) {
        set_message("Choose an output before selecting PEQ");
        return;
    }

    if (eq_route_profile_bank_has_route(&g_route_profiles, g_route_profile_edit_route)) {
        base = g_route_profiles.profiles[profile_index];
    } else {
        eq_control_init_defaults(&base);
    }
    base.enabled = 1;
    base.speaker_only = 0;
    base.route_hint = EQ_ROUTE_UNKNOWN;

    memset(&result, 0, sizeof(result));
    if (eqvita_apo_import_file(path, &g_apo_import_policy, &base, &imported, &result) < 0) {
        const char *error_file = eqvita_media_browser_file_name(
            result.error_path[0] ? result.error_path : path);
        if (result.error_line > 0) {
            set_message("%s:%d: %s", error_file, result.error_line, result.message);
        } else {
            set_message("%s: %s", error_file,
                        result.message[0] ? result.message : "invalid config");
        }
        app_log("apo-import: failed file=%s line=%d error=%s",
                path ? path : "", result.error_line, result.message);
        return;
    }

    imported.enabled = 1;
    imported.speaker_only = 0;
    imported.route_hint = EQ_ROUTE_UNKNOWN;

    previous_profile = g_route_profiles.profiles[profile_index];
    previous_enabled_mask = g_route_profiles.enabled_mask;
    previous_selected_route = g_route_profiles.selected_route;
    previous_dirty_counter = g_route_profiles.dirty_counter;
    memcpy(previous_source, g_route_profile_sources[profile_index], sizeof(previous_source));

    g_route_profiles.enabled_mask |= eq_route_profile_bit(g_route_profile_edit_route);
    g_route_profiles.selected_route = g_route_profile_edit_route;
    g_route_profiles.profiles[profile_index] = imported;
    eq_route_profile_bank_touch(&g_route_profiles);
    if (path && strncmp(path, EQVITA_PEQ_DIR, peq_root_length) == 0 &&
        (path[peq_root_length] == '/' || path[peq_root_length] == '\\')) {
        source_name = path + peq_root_length + 1;
    }
    snprintf(g_route_profile_sources[profile_index],
             sizeof(g_route_profile_sources[profile_index]),
             "%.*s", (int)sizeof(g_route_profile_sources[profile_index]) - 1,
             source_name ? source_name : "PEQ config");

    if (apply_control_candidate(&imported, 1) < 0) {
        g_route_profiles.profiles[profile_index] = previous_profile;
        g_route_profiles.enabled_mask = previous_enabled_mask;
        g_route_profiles.selected_route = previous_selected_route;
        g_route_profiles.dirty_counter = previous_dirty_counter;
        memcpy(g_route_profile_sources[profile_index], previous_source, sizeof(previous_source));
        return;
    }
    (void)save_boot_state();
    set_message("%s PEQ: %d filters, %d Copy ops",
                route_str(g_route_profile_edit_route), result.filter_count, result.copy_count);
    app_log("apo-import: ok route=%s file=%s filters=%d copy=%d preamps=%d includes=%d ignored=%d exclusive=1",
            route_str(g_route_profile_edit_route), path ? path : "",
            result.filter_count, result.copy_count,
            result.preamp_count, result.include_count, result.ignored_count);
    change_screen(SCREEN_OUTPUT_PEQ);
}

static void media_player_play_selected(const char *path)
{
    int res;

    if (!path || !*path) {
        set_message("Choose a music file first");
        return;
    }
    res = eqvita_media_player_open(&g_media_player, path);
    g_media_status = eqvita_media_player_status(&g_media_player);
    if (res < 0) {
        set_message("Could not play this file");
        app_log("preview: open-failed error=%d", g_media_status.last_error);
        return;
    }
    set_message("Playing preview");
    app_log("preview: play format=%s rate=%u ch=%u file=%s",
            eqvita_media_player_format_label(g_media_status.format),
            g_media_status.sample_rate,
            g_media_status.channels,
            eqvita_media_browser_file_name(g_media_status.path));
}

static void media_player_poll_log(void)
{
    static eqvita_media_player_state_t last_state = EQVITA_MEDIA_PLAYER_STOPPED;
    static int last_error = 0;
    static uint32_t last_underruns = 0;
    static uint32_t last_decode_max_us = 0;
    static uint32_t last_output_max_us = 0;
    eqvita_media_player_status_t next = eqvita_media_player_status(&g_media_player);

    if ((next.state == EQVITA_MEDIA_PLAYER_FINISHED ||
         next.state == EQVITA_MEDIA_PLAYER_ERROR) && g_media_player.decoder) {
        eqvita_media_player_stop(&g_media_player);
        next = eqvita_media_player_status(&g_media_player);
    }

    if (next.state != last_state || next.last_error != last_error) {
        app_log("preview: state=%s error=%d file=%s",
                eqvita_media_player_state_label(next.state),
                next.last_error,
                eqvita_media_browser_file_name(next.path));
        last_state = next.state;
        last_error = next.last_error;
    }
    if (next.underrun_count != last_underruns ||
        next.decode_max_us != last_decode_max_us ||
        next.output_max_us != last_output_max_us) {
        app_log("preview-buffer: underruns=%u fill=%u/%u decode_max_us=%u output_max_us=%u",
                next.underrun_count,
                next.ring_fill,
                next.ring_capacity,
                next.decode_max_us,
                next.output_max_us);
        last_underruns = next.underrun_count;
        last_decode_max_us = next.decode_max_us;
        last_output_max_us = next.output_max_us;
    }
    g_media_status = next;
}

static void restore_eq_entry(void)
{
    if (!g_eq_entry_valid) {
        set_message("No edits to undo");
        return;
    }
    if (apply_control_candidate(&g_eq_entry_control, 1) == 0) {
        set_message("Restored screen entry settings");
    }
}

static int is_eq_edit_screen(app_screen_t screen)
{
    return screen == SCREEN_SIMPLE || screen == SCREEN_ADVANCED;
}

static int eq_entry_changed(void)
{
    if (!g_eq_entry_valid) {
        return 0;
    }
    return memcmp(&g_control, &g_eq_entry_control, sizeof(g_control)) != 0;
}

static int should_prompt_before_leaving_eq(void)
{
    return is_eq_edit_screen(g_screen) &&
        g_route_profiles.enabled_mask == 0 &&
        eqvita_app_state_current_preset_dirty(&g_app_state) &&
        eq_entry_changed();
}

static void close_exit_prompt(void)
{
    g_exit_prompt_active = 0;
    g_exit_prompt_selected = 0;
}

static void request_leave_current_screen(void)
{
    app_screen_t return_screen = SCREEN_HOME;

    if (should_prompt_before_leaving_eq()) {
        g_exit_prompt_active = 1;
        g_exit_prompt_selected = 0;
        g_message_frames = 0;
        return;
    }
    if (g_screen == SCREEN_OUTPUT_PEQ) {
        return_screen = g_output_peq_return_screen;
    } else if (is_eq_edit_screen(g_screen)) {
        return_screen = g_eq_return_screen;
    }
    close_exit_prompt();
    change_screen(return_screen);
}

static void discard_eq_edits_and_leave(void)
{
    int res = load_preset();
    if (res >= 0) {
        set_message("Unsaved edits discarded");
        close_exit_prompt();
        change_screen(SCREEN_HOME);
    } else {
        set_message("Could not restore preset (%d)", res);
    }
}

static void activate_exit_prompt(void)
{
    if (!g_exit_prompt_active) {
        return;
    }

    if (g_exit_prompt_selected == 0) {
        if (save_preset_with_message_result() >= 0) {
            close_exit_prompt();
            change_screen(SCREEN_HOME);
        }
    } else if (g_exit_prompt_selected == 1) {
        discard_eq_edits_and_leave();
    } else {
        close_exit_prompt();
    }
}

static void reset_defaults(void)
{
    eq_control_t next = g_control;
    eq_control_set_graphic_mode(&next);
    eq_control_set_headroom_mode(&next, EQ_HEADROOM_SAFE);
    next.preamp_mdB = EQ_DEFAULT_PREAMP_MDB;
    for (int i = 0; i < EQ_BANDS; ++i) {
        next.band_gain_mdB[i] = 0;
    }
    apply_control_candidate(&next, 1);
}

static void apply_simple_eq(int bass, int mid, int treble, int auto_preamp)
{
    eq_control_t next = g_control;

    eq_control_set_graphic_mode(&next);

    bass = clamp(bass, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    mid = clamp(mid, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    treble = clamp(treble, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);

    next.band_gain_mdB[0] = 0;
    for (int i = 1; i <= 3; ++i) next.band_gain_mdB[i] = bass;
    for (int i = 4; i <= 6; ++i) next.band_gain_mdB[i] = mid;
    for (int i = 7; i <= 9; ++i) next.band_gain_mdB[i] = treble;

    if (auto_preamp) {
        int32_t max_boost = bass;
        if (mid > max_boost) max_boost = mid;
        if (treble > max_boost) max_boost = treble;
        if (max_boost < 0) max_boost = 0;
        next.preamp_mdB = -max_boost;
        if (next.preamp_mdB < -EQ_MAX_ABS_GAIN_MDB) next.preamp_mdB = -EQ_MAX_ABS_GAIN_MDB;
    }
    apply_control_candidate(&next, 1);
}

static void apply_preset_stock_depth(void)
{
    eq_control_t next = g_control;
    eq_control_set_graphic_mode(&next);
    eq_control_set_headroom_mode(&next, EQ_HEADROOM_SAFE);
    next.preamp_mdB = -4000;
    next.band_gain_mdB[0] = 0;
    for (int i = 1; i <= 3; ++i) next.band_gain_mdB[i] = 4000;
    for (int i = 4; i <= 6; ++i) next.band_gain_mdB[i] = -2000;
    for (int i = 7; i <= 9; ++i) next.band_gain_mdB[i] = 2000;
    if (apply_control_candidate(&next, 1) == 0) {
        int save_res = save_preset();
        if (save_res >= 0) {
            int slot_res;
            int boot_res;
            eqvita_app_state_mark_current_preset_saved(&g_app_state);
            if (persist_active_preset_state("preset-stock-depth", &slot_res, &boot_res) >= 0) {
                set_message("Applied and saved STOCK Depth");
            } else if (slot_res < 0) {
                set_message("Applied STOCK Depth, slot save failed (%d)", slot_res);
            } else {
                set_message("Applied STOCK Depth, startup save failed (%d)", boot_res);
            }
        } else {
            set_message("Applied STOCK Depth, preset save failed (%d)", save_res);
        }
    }
}

static void apply_preset_mod_switch(void)
{
    static const int32_t gains[EQ_BANDS] = {
        3000, 4000, 4500, 4500, -5000,
        -5000, -5000, -2500, -2500, -3000
    };

    eq_control_t next = g_control;
    eq_control_set_graphic_mode(&next);
    next.enabled = 1;
    next.speaker_only = 1;
    eq_control_set_hpf_enabled(&next, 0);
    eq_control_set_headroom_mode(&next, EQ_HEADROOM_LOUD);
    next.preamp_mdB = -6500;
    for (int i = 0; i < EQ_BANDS; ++i) {
        next.band_gain_mdB[i] = gains[i];
    }
    if (apply_control_candidate(&next, 1) == 0) {
        int save_res = save_preset();
        if (save_res >= 0) {
            int slot_res;
            int boot_res;
            eqvita_app_state_mark_current_preset_saved(&g_app_state);
            if (persist_active_preset_state("preset-mod-switch", &slot_res, &boot_res) >= 0) {
                set_message("Applied and saved MOD Switch");
            } else if (slot_res < 0) {
                set_message("Applied MOD Switch, slot save failed (%d)", slot_res);
            } else {
                set_message("Applied MOD Switch, startup save failed (%d)", boot_res);
            }
        } else {
            set_message("Applied MOD Switch, preset save failed (%d)", save_res);
        }
    }
}

static void format_db(char *out, size_t out_size, int32_t mdB)
{
    snprintf(out, out_size, "%+0.1f dB", mdB / 1000.0f);
}

static const char *screen_title(app_screen_t screen)
{
    switch (screen) {
        case SCREEN_STATUS: return "Telemetry";
        case SCREEN_PRESETS: return "Presets";
        case SCREEN_OUTPUT_PEQ: return "Parametric EQ";
        case SCREEN_SIMPLE: return "Simple EQ";
        case SCREEN_ADVANCED: return "Advanced EQ";
        case SCREEN_MUSIC: return "Music Preview";
        case SCREEN_MUSIC_BROWSER:
            return g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO ?
                "Select PEQ Config" : "Choose Music";
        case SCREEN_THEMES: return "Themes";
        case SCREEN_SETTINGS: return "Settings";
        case SCREEN_ABOUT: return "Help";
        case SCREEN_HOME:
        default: return "EQVita";
    }
}

static const char *screen_subtitle(app_screen_t screen)
{
    switch (screen) {
        case SCREEN_STATUS: return "Live output and app status";
        case SCREEN_PRESETS: return "Choose or save sound profiles";
        case SCREEN_OUTPUT_PEQ: return "Separate PEQ for each audio output";
        case SCREEN_SIMPLE: return "Adjust bass, mids, treble";
        case SCREEN_ADVANCED:
            if (g_route_profiles.enabled_mask != 0 &&
                eq_route_profile_bank_has_route(&g_route_profiles, g_route_profile_edit_route)) {
                static char advanced_subtitle[96];
                snprintf(advanced_subtitle, sizeof(advanced_subtitle), "%s - %s",
                         route_str(g_route_profile_edit_route),
                         g_control.eq_mode == EQ_MODE_PARAMETRIC ? "Equalizer APO operations" : "10-band EQ");
                return advanced_subtitle;
            }
            return g_control.eq_mode == EQ_MODE_PARAMETRIC ?
                "Ordered Equalizer APO operations" : "Preamp and 10 bands";
        case SCREEN_MUSIC: return "Play a song while tuning EQ";
        case SCREEN_MUSIC_BROWSER:
            return browser_async_is_loading() ? "Loading folder..." :
                g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO ?
                    EQVITA_PEQ_DIR :
                g_media_listing.path[0] ? g_media_listing.path : "Choose a folder";
        case SCREEN_THEMES: return "Choose the app color style";
        case SCREEN_SETTINGS: return "Choose where EQ applies";
        case SCREEN_ABOUT: return "Controls, version, and help";
        case SCREEN_HOME:
        default: return "Output - EQ status - preset slot";
    }
}

static app_screen_t home_screen_for_row(int row)
{
    switch (row) {
        case HOME_ROW_SIMPLE: return SCREEN_SIMPLE;
        case HOME_ROW_ADVANCED: return SCREEN_ADVANCED;
        case HOME_ROW_PARAMETRIC: return SCREEN_OUTPUT_PEQ;
        case HOME_ROW_PRESETS: return SCREEN_PRESETS;
        case HOME_ROW_MUSIC: return SCREEN_MUSIC;
        case HOME_ROW_THEMES: return SCREEN_THEMES;
        case HOME_ROW_SETTINGS: return SCREEN_SETTINGS;
        case HOME_ROW_STATUS: return SCREEN_STATUS;
        case HOME_ROW_ABOUT: return SCREEN_ABOUT;
        default: return SCREEN_HOME;
    }
}

static int current_row_count(void)
{
    switch (g_screen) {
        case SCREEN_HOME: return HOME_ROW_COUNT;
        case SCREEN_STATUS: return STATUS_ROW_COUNT;
        case SCREEN_PRESETS: return PRESETS_ROW_COUNT;
        case SCREEN_OUTPUT_PEQ: return OUTPUT_PEQ_ROW_COUNT;
        case SCREEN_SIMPLE: return SIMPLE_ROW_COUNT;
        case SCREEN_ADVANCED: return ADVANCED_ROW_COUNT;
        case SCREEN_MUSIC: return MUSIC_ROW_COUNT;
        case SCREEN_MUSIC_BROWSER: return browser_async_is_loading() ? 1 : g_media_listing.count;
        case SCREEN_THEMES: return eq_ui_theme_count();
        case SCREEN_SETTINGS: return SETTINGS_ROW_COUNT;
        case SCREEN_ABOUT: return ABOUT_ROW_COUNT;
        default: return 0;
    }
}

static int row_is_section(app_screen_t screen, int row)
{
    switch (screen) {
        case SCREEN_STATUS:
            return row == STATUS_ROW_LEVELS_SECTION ||
                   row == STATUS_ROW_PROCESSING_SECTION;
        case SCREEN_PRESETS:
            return row == PRESETS_ROW_ACTIONS_SECTION;
        case SCREEN_OUTPUT_PEQ:
            return row == OUTPUT_PEQ_ROW_INFO_SECTION;
        case SCREEN_SIMPLE:
            return row == SIMPLE_ROW_PRESET_SECTION;
        case SCREEN_ADVANCED:
            return row == ADV_ROW_PRESET_SECTION;
        case SCREEN_SETTINGS:
            return row == SETTINGS_ROW_SAFETY_SECTION;
        case SCREEN_ABOUT:
            return row == ABOUT_ROW_EQ_SECTION ||
                   row == ABOUT_ROW_OUTPUT_SECTION ||
                   row == ABOUT_ROW_CONTROLS_SECTION;
        default:
            return 0;
    }
}

static int row_is_selectable(app_screen_t screen, int row)
{
    if (screen == SCREEN_MUSIC_BROWSER && browser_async_is_loading()) {
        return 0;
    }
    return !row_is_section(screen, row);
}

static int visible_row_capacity(void)
{
    return (EQ_UI_FOOTER_Y - EQ_UI_LIST_Y - 8) / (EQ_UI_ROW_H + EQ_UI_ROW_GAP);
}

static void ensure_selection_visible(void)
{
    int count = current_row_count();
    int visible = visible_row_capacity();
    int *selected = &g_selected[g_screen];
    int *scroll = &g_scroll_top[g_screen];

    if (count <= 0) {
        *selected = 0;
        *scroll = 0;
        return;
    }

    if (*selected < 0) *selected = 0;
    if (*selected >= count) *selected = count - 1;
    if (!row_is_selectable(g_screen, *selected)) {
        if (*selected + 1 < count) {
            (*selected)++;
        } else if (*selected > 0) {
            (*selected)--;
        }
    }
    if (*scroll < 0) *scroll = 0;
    if (*scroll > count - visible) *scroll = count > visible ? count - visible : 0;
    if (*selected < *scroll) *scroll = *selected;
    if (*selected >= *scroll + visible) *scroll = *selected - visible + 1;
}

static void change_screen(app_screen_t screen)
{
    if (screen >= SCREEN_COUNT) {
        return;
    }
    if ((screen == SCREEN_SIMPLE || screen == SCREEN_ADVANCED) && g_screen != screen) {
        g_eq_entry_control = g_control;
        g_eq_entry_valid = 1;
    }
    if (screen == SCREEN_THEMES) {
        g_selected[screen] = eq_ui_theme_index();
    }
    g_screen = screen;
    ensure_selection_visible();
}

static void move_selection(int delta)
{
    int count = current_row_count();
    int *selected = &g_selected[g_screen];
    int next;
    if (count <= 0) {
        return;
    }
    next = *selected;
    for (int i = 0; i < count; ++i) {
        next = (next + delta + count) % count;
        if (row_is_selectable(g_screen, next)) {
            *selected = next;
            break;
        }
    }
    ensure_selection_visible();
}

static void scroll_current(int delta)
{
    int count = current_row_count();
    int visible = visible_row_capacity();
    int max_scroll = count > visible ? count - visible : 0;
    g_scroll_top[g_screen] = clamp(g_scroll_top[g_screen] + delta, 0, max_scroll);
    g_selected[g_screen] = clamp(g_selected[g_screen], g_scroll_top[g_screen],
                                 g_scroll_top[g_screen] + visible - 1);
    ensure_selection_visible();
}

static void row_value(char *value, size_t value_size, app_screen_t screen, int row)
{
    value[0] = 0;
    switch (screen) {
        case SCREEN_HOME:
            if (row == HOME_ROW_ENABLED) {
                snprintf(value, value_size, "%s", g_control.enabled ? "On" : "Off");
            } else if (row == HOME_ROW_PARAMETRIC) {
                snprintf(value, value_size, "%d of 3", route_profile_count());
            } else if (row == HOME_ROW_PRESETS) {
                snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            } else if (row == HOME_ROW_MUSIC) {
                snprintf(value, value_size, "%s", eqvita_media_player_state_label(g_media_status.state));
            } else if (row == HOME_ROW_THEMES) {
                snprintf(value, value_size, "%s", eq_ui_theme_name(eq_ui_theme_index()));
            }
            break;
        case SCREEN_STATUS:
            if (row == STATUS_ROW_EQ_STATUS) snprintf(value, value_size, "%s", g_control.enabled ? (g_status.eq_active ? "Active" : bypass_reason_str(g_status.bypass_reason)) : "Off");
            else if (row == STATUS_ROW_OUTPUT) snprintf(value, value_size, "%s", route_str(g_status.route));
            else if (row == STATUS_ROW_PRESET) snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            else if (row == STATUS_ROW_PLUGIN) snprintf(value, value_size, "%s", g_plugin_compatible ? "Ready" : "Mismatch");
            else if (row == STATUS_ROW_CLIPPING) snprintf(value, value_size, "%d", g_status.clip_events);
            else if (row == STATUS_ROW_PEAK) snprintf(value, value_size, "%u / %u", g_status.peak_l, g_status.peak_r);
            else if (row == STATUS_ROW_AUDIO_RATE) snprintf(value, value_size, "%u Hz", g_status.sample_rate);
            else if (row == STATUS_ROW_STREAMS) snprintf(value, value_size, "%u", g_status.debug_active_ports);
            else if (row == STATUS_ROW_SKIPPED) snprintf(value, value_size, "%u", g_status.debug_busy_bypass_count);
            else if (row == STATUS_ROW_UNKNOWN) snprintf(value, value_size, "%u", g_status.debug_unknown_port_count);
            else if (row == STATUS_ROW_LAST_BLOCK) snprintf(value, value_size, "%u us", g_status.debug_last_us);
            else if (row == STATUS_ROW_SLOWEST_BLOCK) {
                if (g_status.debug_max_budget_us > 0) {
                    snprintf(value, value_size, "%u/%u us", g_status.debug_max_us, g_status.debug_max_budget_us);
                } else {
                    snprintf(value, value_size, "%u us", g_status.debug_max_us);
                }
            }
            else if (row == STATUS_ROW_BLOCKS) snprintf(value, value_size, "%u", g_status.debug_run_count);
            break;
        case SCREEN_PRESETS:
            if (row == PRESETS_ROW_SLOT) snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            break;
        case SCREEN_OUTPUT_PEQ:
            if (row == OUTPUT_PEQ_ROW_TARGET) {
                snprintf(value, value_size, "%s", route_str(g_route_profile_edit_route));
            } else if (row == OUTPUT_PEQ_ROW_ASSIGNED) {
                snprintf(value, value_size, "%s", route_profile_source(g_route_profile_edit_route));
            } else if (row == OUTPUT_PEQ_ROW_CHOOSE) {
                snprintf(value, value_size, "Select .txt");
            } else if (row == OUTPUT_PEQ_ROW_INSPECT) {
                snprintf(value, value_size, "Advanced EQ");
            } else if (row == OUTPUT_PEQ_ROW_CLEAR) {
                snprintf(value, value_size, "%s",
                         eq_route_profile_bank_has_route(&g_route_profiles, g_route_profile_edit_route) ?
                             "Clear" : "Not assigned");
            } else if (row == OUTPUT_PEQ_ROW_LIVE) {
                snprintf(value, value_size, "%s",
                         route_str(g_status.route != EQ_ROUTE_UNKNOWN ?
                                   g_status.route : (uint8_t)detect_route_user()));
            } else if (row == OUTPUT_PEQ_ROW_SWITCHING) {
                snprintf(value, value_size, "%d of 3 assigned", route_profile_count());
            }
            break;
        case SCREEN_SIMPLE:
            if (row == SIMPLE_ROW_PREAMP) format_db(value, value_size, g_control.preamp_mdB);
            else if (row == SIMPLE_ROW_BASS) format_db(value, value_size, g_control.band_gain_mdB[1]);
            else if (row == SIMPLE_ROW_MIDRANGE) format_db(value, value_size, g_control.band_gain_mdB[4]);
            else if (row == SIMPLE_ROW_TREBLE) format_db(value, value_size, g_control.band_gain_mdB[7]);
            else if (row == SIMPLE_ROW_PRESET_SLOT) snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            else if (row == SIMPLE_ROW_SAVE_NEXT) snprintf(value, value_size, "Slot %d", ((g_preset_slot + 1) % 3) + 1);
            break;
        case SCREEN_ADVANCED:
            if (row == ADV_ROW_PREAMP) {
                format_db(value, value_size, g_control.preamp_mdB);
            } else if (advanced_row_is_eq_value(row)) {
                int index = row - ADV_BAND_ROW_BASE;
                if (g_control.eq_mode == EQ_MODE_PARAMETRIC) {
                    const eq_parametric_filter_t *operation = &g_control.parametric_filters[index];
                    if (operation->type == EQ_FILTER_COPY) {
                        snprintf(value, value_size, "Matrix");
                    } else {
                        format_db(value, value_size, operation->data.filter.gain_mdB);
                    }
                } else {
                    format_db(value, value_size, g_control.band_gain_mdB[index]);
                }
            } else if (row == ADV_ROW_PRESET_SLOT) {
                snprintf(value, value_size, "Slot %d", g_preset_slot + 1);
            } else if (row == ADV_ROW_SAVE_NEXT) {
                snprintf(value, value_size, "Slot %d", ((g_preset_slot + 1) % 3) + 1);
            }
            break;
        case SCREEN_MUSIC:
            if (row == MUSIC_ROW_PLAY_PAUSE) {
                snprintf(value, value_size, "%s", eqvita_media_player_state_label(g_media_status.state));
            } else if (row == MUSIC_ROW_LOOP) {
                snprintf(value, value_size, "%s", g_media_status.loop ? "On" : "Off");
            } else if (row == MUSIC_ROW_VOLUME) {
                snprintf(value, value_size, "%d%%", g_media_status.volume);
            }
            break;
        case SCREEN_MUSIC_BROWSER:
            if (browser_async_is_loading()) {
                snprintf(value, value_size, "%s", "");
            } else if (row >= 0 && row < g_media_listing.count) {
                eqvita_media_entry_t *entry = &g_media_listing.entries[row];
                snprintf(value, value_size, "%s",
                         entry->kind == EQVITA_MEDIA_ENTRY_FILE ?
                             (g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO ? "Import" : "Play") :
                         entry->kind == EQVITA_MEDIA_ENTRY_PARENT ? "Back" : "Open");
            }
            break;
        case SCREEN_THEMES:
            if (row == eq_ui_theme_index()) snprintf(value, value_size, "Active");
            break;
        case SCREEN_SETTINGS:
            if (row == SETTINGS_ROW_ENABLED) snprintf(value, value_size, "%s", g_control.enabled ? "On" : "Off");
            else if (row == SETTINGS_ROW_SCOPE) snprintf(value, value_size, "%s", eq_target_str());
            else if (row == SETTINGS_ROW_HPF) snprintf(value, value_size, "%s",
                g_control.eq_mode == EQ_MODE_PARAMETRIC ? "Off (PEQ)" :
                eq_control_hpf_enabled(&g_control) ? "On" : "Off");
            else if (row == SETTINGS_ROW_HEADROOM) snprintf(value, value_size, "%s",
                g_control.eq_mode == EQ_MODE_PARAMETRIC ? "APO Exact (PEQ)" :
                headroom_mode_str(eq_control_get_headroom_mode(&g_control)));
            else if (row == SETTINGS_ROW_ROUTE) snprintf(value, value_size, "%s",
                route_str(g_status.route != EQ_ROUTE_UNKNOWN ? g_status.route : g_control.route_hint));
            else if (row == SETTINGS_ROW_STARTUP) snprintf(value, value_size, "%s",
                (g_boot_state_save_failed || g_route_profiles_save_failed) ? "Save failed" :
                eqvita_app_state_boot_dirty(&g_app_state) ? "Unsaved" : "Saved");
            break;
        case SCREEN_ABOUT:
            if (row == ABOUT_ROW_VERSION) snprintf(value, value_size, "v%d.%d.%d", g_version.major, g_version.minor, g_version.patch);
            else if (row == ABOUT_ROW_PLUGIN) snprintf(value, value_size, "%s", g_plugin_compatible ? "OK" : "Mismatch");
            else if (row == ABOUT_ROW_LOG_FILE) snprintf(value, value_size, "Share for issues");
            break;
        default:
            break;
    }
}

static void row_text(app_screen_t screen,
                     int row,
                     const char **icon,
                     const char **label,
                     const char **desc,
                     eq_ui_row_kind_t *kind)
{
    *icon = "";
    *label = "";
    *desc = "";
    *kind = EQ_UI_ROW_READONLY;

    switch (screen) {
        case SCREEN_HOME: {
            static const char *icons[] = {"power", "simple", "advanced", "advanced", "preset", "music", "themes", "settings", "status", "about"};
            static const char *labels[] = {"Equalizer", "Simple EQ", "Advanced EQ", "Parametric EQ", "Presets", "Music Preview", "Themes", "Settings", "Telemetry", "Help"};
            static const char *descs[] = {
                "Turn sound tuning on or off",
                "Adjust bass, mids, treble",
                "Fine tune the 10-band equalizer",
                "Import APO .txt profiles for each output",
                "Choose or save profiles",
                "Play a song while tuning EQ",
                "Choose the app color style",
                "Output scope and safety",
                "Live output and app status",
                "Controls and short guide"
            };
            *icon = icons[row]; *label = labels[row]; *desc = descs[row];
            *kind = row == HOME_ROW_ENABLED ? EQ_UI_ROW_ACTION : EQ_UI_ROW_NAV;
            break;
        }
        case SCREEN_STATUS:
            if (row_is_section(screen, row)) {
                *label = row == STATUS_ROW_LEVELS_SECTION ? "Audio levels" : "Processing";
                *desc = row == STATUS_ROW_PROCESSING_SECTION ? "Read-only audio counters" : "";
                *kind = EQ_UI_ROW_SECTION;
            } else {
                *icon = (const char *[]){"status", "speaker", "preset", "status", "level", "level", "info", "level", "status", "info", "info", "info"}[
                    row == STATUS_ROW_EQ_STATUS ? 0 :
                    row == STATUS_ROW_OUTPUT ? 1 :
                    row == STATUS_ROW_PRESET ? 2 :
                    row == STATUS_ROW_PLUGIN ? 3 :
                    row == STATUS_ROW_CLIPPING ? 4 :
                    row == STATUS_ROW_PEAK ? 5 :
                    row == STATUS_ROW_AUDIO_RATE ? 6 :
                    row == STATUS_ROW_STREAMS ? 7 :
                    row == STATUS_ROW_SKIPPED ? 8 :
                    row == STATUS_ROW_UNKNOWN ? 9 :
                    row == STATUS_ROW_LAST_BLOCK ? 10 : 11];
                *label = (const char *[]){"EQ status", "Output", "Preset", "Plugin", "Clipping", "Peak level", "Audio rate", "Audio streams", "Skipped audio", "Unknown audio", "Last block", "Slowest block", "Blocks processed"}[
                    row == STATUS_ROW_EQ_STATUS ? 0 :
                    row == STATUS_ROW_OUTPUT ? 1 :
                    row == STATUS_ROW_PRESET ? 2 :
                    row == STATUS_ROW_PLUGIN ? 3 :
                    row == STATUS_ROW_CLIPPING ? 4 :
                    row == STATUS_ROW_PEAK ? 5 :
                    row == STATUS_ROW_AUDIO_RATE ? 6 :
                    row == STATUS_ROW_STREAMS ? 7 :
                    row == STATUS_ROW_SKIPPED ? 8 :
                    row == STATUS_ROW_UNKNOWN ? 9 :
                    row == STATUS_ROW_LAST_BLOCK ? 10 :
                    row == STATUS_ROW_SLOWEST_BLOCK ? 11 : 12];
                *desc = (const char *[]){"Active or why it is skipped", "Speakers, wired, or Bluetooth", "Selected profile slot", "App and plugin must match", "Limiter protected the sound", "Recent left and right volume", "Current stream rate", "Streams being watched", "Skipped to avoid stutter", "Streams EQ could not identify", "Latest audio block time", "Slowest audio block time", "Total blocks seen"}[
                    row == STATUS_ROW_EQ_STATUS ? 0 :
                    row == STATUS_ROW_OUTPUT ? 1 :
                    row == STATUS_ROW_PRESET ? 2 :
                    row == STATUS_ROW_PLUGIN ? 3 :
                    row == STATUS_ROW_CLIPPING ? 4 :
                    row == STATUS_ROW_PEAK ? 5 :
                    row == STATUS_ROW_AUDIO_RATE ? 6 :
                    row == STATUS_ROW_STREAMS ? 7 :
                    row == STATUS_ROW_SKIPPED ? 8 :
                    row == STATUS_ROW_UNKNOWN ? 9 :
                    row == STATUS_ROW_LAST_BLOCK ? 10 :
                    row == STATUS_ROW_SLOWEST_BLOCK ? 11 : 12];
            }
            break;
        case SCREEN_PRESETS:
            if (row_is_section(screen, row)) {
                *label = "Preset actions";
                *desc = "Save, load, or clear curves";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == PRESETS_ROW_SLOT) {
                *icon = "preset";
                *label = "Preset slot";
                *desc = "Left/right changes slot";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == PRESETS_ROW_STOCK_DEPTH) {
                *icon = "preset";
                *label = "Preset: STOCK Depth";
                *desc = "Warmer stock-speaker profile";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == PRESETS_ROW_MOD_SWITCH) {
                *icon = "preset";
                *label = "Preset: MOD Switch";
                *desc = "For Switch speaker mod";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == PRESETS_ROW_OUTPUT_PEQ) {
                *icon = "load";
                *label = "Parametric EQ";
                *desc = "Separate speaker, wired, and Bluetooth EQ";
                *kind = EQ_UI_ROW_NAV;
            } else if (row == PRESETS_ROW_SAVE) {
                *icon = "save";
                *label = "Save current preset";
                *desc = "Saves the selected slot";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == PRESETS_ROW_LOAD) {
                *icon = "load";
                *label = "Load preset";
                *desc = "Loads the selected slot";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == PRESETS_ROW_RESET) {
                *icon = "reset";
                *label = "Reset EQ";
                *desc = "Clears all EQ gains";
                *kind = EQ_UI_ROW_ACTION;
            }
            break;
        case SCREEN_OUTPUT_PEQ:
            if (row == OUTPUT_PEQ_ROW_TARGET) {
                *icon = "route";
                *label = "Configure output";
                *desc = "Left/right selects speakers, wired, or Bluetooth";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == OUTPUT_PEQ_ROW_ASSIGNED) {
                *icon = "file";
                *label = "Assigned PEQ";
                *desc = "Equalizer APO file used for this output";
            } else if (row == OUTPUT_PEQ_ROW_CHOOSE) {
                *icon = "load";
                *label = "Choose PEQ file";
                *desc = EQVITA_PEQ_DIR "/*.txt";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == OUTPUT_PEQ_ROW_INSPECT) {
                *icon = "advanced";
                *label = "Inspect / edit PEQ";
                *desc = "Show this output's ordered operations";
                *kind = EQ_UI_ROW_NAV;
            } else if (row == OUTPUT_PEQ_ROW_CLEAR) {
                *icon = "reset";
                *label = "Clear this output";
                *desc = "Bypass this output until another PEQ is assigned";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == OUTPUT_PEQ_ROW_INFO_SECTION) {
                *label = "Automatic routing";
                *desc = "Wired is live; app refreshes speaker / Bluetooth";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == OUTPUT_PEQ_ROW_LIVE) {
                *icon = "speaker";
                *label = "Live output";
                *desc = "What the Vita is using now";
            } else if (row == OUTPUT_PEQ_ROW_SWITCHING) {
                *icon = "status";
                *label = "Output profiles";
                *desc = "Unassigned outputs are bypassed";
            }
            break;
        case SCREEN_SIMPLE:
            if (row == SIMPLE_ROW_PREAMP) {
                *icon = "level";
                *label = "Preamp";
                *desc = "Overall volume before EQ";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_BASS) {
                *icon = "simple";
                *label = "Bass";
                *desc = "Low sound and depth";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_MIDRANGE) {
                *icon = "simple";
                *label = "Midrange";
                *desc = "Voice and body";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_TREBLE) {
                *icon = "simple";
                *label = "Treble";
                *desc = "Clarity and sparkle";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_PRESET_SECTION) {
                *label = "Preset controls";
                *desc = "Save or undo this curve";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == SIMPLE_ROW_PRESET_SLOT) {
                *icon = "preset";
                *label = "Preset slot";
                *desc = "Left/right chooses save slot";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == SIMPLE_ROW_SAVE_CURRENT) {
                *icon = "save";
                *label = "Save to this slot";
                *desc = "Save this sound profile";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SIMPLE_ROW_SAVE_NEXT) {
                *icon = "save";
                *label = "Save as next slot";
                *desc = "Copy this curve to another slot";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SIMPLE_ROW_UNDO_ENTRY) {
                *icon = "reset";
                *label = "Undo screen edits";
                *desc = "Restore values from entry";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SIMPLE_ROW_RESET_EQ) {
                *icon = "reset";
                *label = "Reset EQ";
                *desc = "Flat bands and preamp";
                *kind = EQ_UI_ROW_ACTION;
            }
            break;
        case SCREEN_ADVANCED:
            if (row == ADV_ROW_PREAMP) {
                *icon = "level";
                *label = "Preamp";
                *desc = "Overall volume before EQ";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (advanced_row_is_eq_value(row)) {
                int index = row - ADV_BAND_ROW_BASE;
                *icon = "advanced";
                if (g_control.eq_mode == EQ_MODE_PARAMETRIC) {
                    static char operation_label[96];
                    static char operation_desc[128];
                    const eq_parametric_filter_t *operation = &g_control.parametric_filters[index];
                    const char *channels = operation->channel_mask == EQ_CHANNEL_LEFT_MASK ? "L" :
                        operation->channel_mask == EQ_CHANNEL_RIGHT_MASK ? "R" : "L/R";

                    if (operation->type == EQ_FILTER_COPY) {
                        snprintf(operation_label, sizeof(operation_label), "%02d  Copy", index + 1);
                        snprintf(operation_desc, sizeof(operation_desc),
                                 "L=%+.3fL%+.3fR  R=%+.3fL%+.3fR",
                                 operation->data.copy.matrix[0] / (float)EQ_COPY_COEFFICIENT_SCALE,
                                 operation->data.copy.matrix[1] / (float)EQ_COPY_COEFFICIENT_SCALE,
                                 operation->data.copy.matrix[2] / (float)EQ_COPY_COEFFICIENT_SCALE,
                                 operation->data.copy.matrix[3] / (float)EQ_COPY_COEFFICIENT_SCALE);
                        *kind = EQ_UI_ROW_READONLY;
                    } else {
                        const char *type = operation->type == EQ_FILTER_PEAK ? "PK" :
                            operation->type == EQ_FILTER_LOW_SHELF ?
                                (operation->frequency_mode == EQ_FILTER_FREQUENCY_CORNER ? "LS" : "LSC") :
                            operation->type == EQ_FILTER_HIGH_SHELF ?
                                (operation->frequency_mode == EQ_FILTER_FREQUENCY_CORNER ? "HS" : "HSC") : "?";
                        snprintf(operation_label, sizeof(operation_label), "%02d  %s %s  %.3f Hz",
                                 index + 1, channels, type,
                                 operation->data.filter.frequency_mHz / 1000.0f);
                        snprintf(operation_desc, sizeof(operation_desc), "%s %.5f - left/right adjusts gain",
                                 operation->shape == EQ_FILTER_SHAPE_S ? "S" : "Q",
                                 operation->data.filter.q_uQ / 1000000.0f);
                        *kind = operation->type >= EQ_FILTER_PEAK && operation->type <= EQ_FILTER_HIGH_SHELF ?
                            EQ_UI_ROW_ADJUST : EQ_UI_ROW_READONLY;
                    }
                    *label = operation_label;
                    *desc = operation_desc;
                } else {
                    *label = band_labels[index];
                    *desc = "Left/right fine tune, L/R coarse tune";
                    *kind = EQ_UI_ROW_ADJUST;
                }
            } else if (row == ADV_ROW_PRESET_SECTION) {
                *label = "Preset controls";
                *desc = "Live edits apply now";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == ADV_ROW_PRESET_SLOT) {
                *icon = "preset";
                *label = "Preset slot";
                *desc = "Left/right chooses save slot";
                *kind = EQ_UI_ROW_ADJUST;
            } else if (row == ADV_ROW_SAVE_CURRENT) {
                *icon = "save";
                *label = "Save to this slot";
                *desc = "Save this sound profile";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == ADV_ROW_SAVE_NEXT) {
                *icon = "save";
                *label = "Save as next slot";
                *desc = "Copy this curve to another slot";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == ADV_ROW_UNDO_ENTRY) {
                *icon = "reset";
                *label = "Undo screen edits";
                *desc = "Restore values from entry";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == ADV_ROW_RESET_EQ) {
                *icon = "reset";
                *label = "Reset EQ";
                *desc = "Flat bands and preamp";
                *kind = EQ_UI_ROW_ACTION;
            }
            break;
        case SCREEN_MUSIC:
            if (row == MUSIC_ROW_CHOOSE) {
                *icon = "folder";
                *label = "Choose file";
                *desc = "Pick OGG, MP3, or WAV";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == MUSIC_ROW_PLAY_PAUSE) {
                *icon = "play";
                *label = "Play / Pause";
                *desc = "Pause or resume preview";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == MUSIC_ROW_STOP) {
                *icon = "stop";
                *label = "Stop";
                *desc = "Stop and release audio";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == MUSIC_ROW_LOOP) {
                *icon = "loop";
                *label = "Loop";
                *desc = "Repeat the selected song";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == MUSIC_ROW_VOLUME) {
                *icon = "level";
                *label = "Volume";
                *desc = "Preview player volume";
                *kind = EQ_UI_ROW_ADJUST;
            }
            break;
        case SCREEN_MUSIC_BROWSER:
            if (browser_async_is_loading()) {
                *icon = "folder";
                *label = "Loading...";
                *desc = "Reading folder contents";
            } else if (row >= 0 && row < g_media_listing.count) {
                eqvita_media_entry_t *entry = &g_media_listing.entries[row];
                *icon = entry->kind == EQVITA_MEDIA_ENTRY_FILE ? "file" : "folder";
                *label = entry->name;
                *desc = entry->kind == EQVITA_MEDIA_ENTRY_FILE ?
                    (g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO ?
                        "Import this Equalizer APO config" : "Play this song") :
                    entry->kind == EQVITA_MEDIA_ENTRY_PARENT ? "Go up one folder" : "Open folder";
                *kind = entry->kind == EQVITA_MEDIA_ENTRY_FILE ? EQ_UI_ROW_ACTION : EQ_UI_ROW_NAV;
            }
            break;
        case SCREEN_THEMES:
            *icon = "themes";
            *label = eq_ui_theme_name(row);
            *desc = eq_ui_theme_description(row);
            *kind = EQ_UI_ROW_ACTION;
            break;
        case SCREEN_SETTINGS:
            if (row_is_section(screen, row)) {
                *label = "Safety and startup";
                *desc = "";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == SETTINGS_ROW_ENABLED) {
                *icon = "power";
                *label = "Equalizer";
                *desc = "Turn EQ processing on or off";
                *kind = EQ_UI_ROW_ACTION;
            } else if (row == SETTINGS_ROW_SCOPE) {
                *icon = "speaker";
                *label = "Apply EQ to";
                *desc = g_route_profiles.enabled_mask != 0 ?
                    "Managed by Output PEQ profiles" : "Speakers only or all outputs";
                *kind = g_route_profiles.enabled_mask != 0 ?
                    EQ_UI_ROW_READONLY : EQ_UI_ROW_ACTION;
            } else if (row == SETTINGS_ROW_HPF) {
                *icon = "hpf";
                *label = "Bass guard";
                *desc = g_control.eq_mode == EQ_MODE_PARAMETRIC ?
                    "Disabled while PEQ is active" : "Reduces deep bass distortion";
                *kind = g_control.eq_mode == EQ_MODE_PARAMETRIC ?
                    EQ_UI_ROW_READONLY : EQ_UI_ROW_ACTION;
            } else if (row == SETTINGS_ROW_HEADROOM) {
                *icon = "headroom";
                *label = "Sound mode";
                *desc = g_control.eq_mode == EQ_MODE_PARAMETRIC ?
                    "Locked so the .txt preamp is exact" : "Safe, Loud, Direct, or APO Exact";
                *kind = g_control.eq_mode == EQ_MODE_PARAMETRIC ?
                    EQ_UI_ROW_READONLY : EQ_UI_ROW_ADJUST;
            } else if (row == SETTINGS_ROW_ROUTE) {
                *icon = "route";
                *label = "Live output";
                *desc = "What the Vita is using now";
            } else if (row == SETTINGS_ROW_STARTUP) {
                *icon = "save";
                *label = "Startup settings";
                *desc = "Save current sound for reboot";
                *kind = EQ_UI_ROW_ACTION;
            }
            break;
        case SCREEN_ABOUT:
            if (row_is_section(screen, row)) {
                *label = row == ABOUT_ROW_EQ_SECTION ? "EQ basics" :
                    row == ABOUT_ROW_OUTPUT_SECTION ? "Output modes" : "Controls and paths";
                *desc = "";
                *kind = EQ_UI_ROW_SECTION;
            } else if (row == ABOUT_ROW_CREATOR) {
                *icon = "about";
                *label = "Created by shevoK";
                *desc = "EQVita app creator";
            } else if (row == ABOUT_ROW_REPOSITORY) {
                *icon = "load";
                *label = "GitHub: shev0k/EQVita";
                *desc = "Source, releases, and issues";
            } else if (row == ABOUT_ROW_LOG_FILE) {
                *icon = "save";
                *label = "Log file";
                *desc = APP_LOG_PATH;
            } else if (row == ABOUT_ROW_WHAT) {
                *icon = "simple";
                *label = "What it does";
                *desc = "Adds EQ to Vita system audio";
            } else if (row == ABOUT_ROW_VERSION) {
                *icon = "about";
                *label = "Version";
                *desc = "Companion app version";
            } else if (row == ABOUT_ROW_PLUGIN) {
                *icon = "status";
                *label = "Plugin";
                *desc = "App and plugin must match";
            } else if (row == ABOUT_ROW_PRESETS) {
                *icon = "preset";
                *label = "Presets";
                *desc = "Save or import APO profiles";
            } else if (row == ABOUT_ROW_SIMPLE) {
                *icon = "simple";
                *label = "Simple EQ";
                *desc = "Fast bass, mids, treble tuning";
            } else if (row == ABOUT_ROW_ADVANCED) {
                *icon = "advanced";
                *label = "Advanced EQ";
                *desc = "10-band or imported parametric EQ";
            } else if (row == ABOUT_ROW_PREAMP) {
                *icon = "level";
                *label = "Preamp";
                *desc = "Volume before the EQ";
            } else if (row == ABOUT_ROW_HPF) {
                *icon = "hpf";
                *label = "HPF / Bass guard";
                *desc = "Cuts very deep bass";
            } else if (row == ABOUT_ROW_SPEAKERS) {
                *icon = "speaker";
                *label = "Speakers mode";
                *desc = "EQ only Vita speakers";
            } else if (row == ABOUT_ROW_ALL_OUTPUTS) {
                *icon = "speaker";
                *label = "Parametric EQ";
                *desc = "Separate speaker, wired, Bluetooth curves";
            } else if (row == ABOUT_ROW_BYPASS) {
                *icon = "status";
                *label = "Bypass";
                *desc = "EQ is skipped or turned off";
            } else if (row == ABOUT_ROW_DISTORTION) {
                *icon = "level";
                *label = "Avoid distortion";
                *desc = "Lower boosts if sound cracks";
            } else if (row == ABOUT_ROW_CONTROLS) {
                *icon = "nav";
                *label = "Controls";
                *desc = "Confirm, Back, START, Triangle";
            } else if (row == ABOUT_ROW_DATA_FOLDER) {
                *icon = "save";
                *label = "PEQ config folder";
                *desc = EQVITA_PEQ_DIR;
            }
            break;
        default:
            break;
    }
}

static void draw_music_player_screen(void)
{
    eq_ui_music_action_row_t actions[MUSIC_ROW_COUNT];
    char values[MUSIC_ROW_COUNT][64];
    char stream_info[64];
    char preamp[32];
    char preset[32];
    char eq_state[32];
    char output[32];
    char headroom[32];
    const char *file_name = g_media_status.path[0] ?
        eqvita_media_browser_file_name(g_media_status.path) : "No song selected";

    for (int row = 0; row < MUSIC_ROW_COUNT; ++row) {
        const char *icon;
        const char *label;
        const char *desc;
        eq_ui_row_kind_t kind;

        row_text(SCREEN_MUSIC, row, &icon, &label, &desc, &kind);
        row_value(values[row], sizeof(values[row]), SCREEN_MUSIC, row);
        actions[row].row_index = row;
        actions[row].icon = icon;
        actions[row].label = label;
        actions[row].description = desc;
        actions[row].value = values[row];
        actions[row].kind = kind;
    }

    if (g_media_status.sample_rate > 0) {
        snprintf(stream_info, sizeof(stream_info), "%s - %u Hz / %u ch",
                 eqvita_media_player_format_label(g_media_status.format),
                 g_media_status.sample_rate, g_media_status.channels);
    } else {
        snprintf(stream_info, sizeof(stream_info), "No stream yet");
    }
    format_db(preamp, sizeof(preamp), g_control.preamp_mdB);
    snprintf(preset, sizeof(preset), "Slot %d", g_preset_slot + 1);
    snprintf(eq_state, sizeof(eq_state), "%s", g_control.enabled ? "EQ on" : "EQ off");
    snprintf(output, sizeof(output), "%s", eq_target_str());
    snprintf(headroom, sizeof(headroom), "%s", headroom_mode_str(eq_control_get_headroom_mode(&g_control)));

    eq_ui_music_player_model_t model = {
        file_name,
        g_media_status.path,
        eqvita_media_player_state_label(g_media_status.state),
        eqvita_media_player_format_label(g_media_status.format),
        stream_info,
        eq_state,
        preset,
        output,
        preamp,
        headroom,
        g_selected[SCREEN_MUSIC],
        actions,
        MUSIC_ROW_COUNT,
        g_row_bounds,
        EQ_UI_MAX_VISIBLE_ROWS,
        &g_row_bound_count
    };

    g_row_bound_count = 0;
    eq_ui_draw_music_player_deck(&model);
}

static void draw_music_browser_screen(void)
{
    eq_ui_music_browser_entry_t entries[EQ_UI_MAX_VISIBLE_ROWS];
    char values[EQ_UI_MAX_VISIBLE_ROWS][64];
    int visible = visible_row_capacity();
    int scroll = g_scroll_top[SCREEN_MUSIC_BROWSER];
    int count = current_row_count();
    int entry_count = 0;
    char peq_title[64];

    snprintf(peq_title, sizeof(peq_title), "%s PEQ configs",
             route_str(g_route_profile_edit_route));

    if (visible > EQ_UI_MAX_VISIBLE_ROWS) {
        visible = EQ_UI_MAX_VISIBLE_ROWS;
    }

    for (int i = 0; i < visible && scroll + i < count; ++i) {
        int row = scroll + i;
        const char *icon;
        const char *label;
        const char *desc;
        eq_ui_row_kind_t kind;

        row_text(SCREEN_MUSIC_BROWSER, row, &icon, &label, &desc, &kind);
        row_value(values[entry_count], sizeof(values[entry_count]), SCREEN_MUSIC_BROWSER, row);
        entries[entry_count].row_index = row;
        entries[entry_count].icon = icon;
        entries[entry_count].label = label;
        entries[entry_count].description = desc;
        entries[entry_count].value = values[entry_count];
        entries[entry_count].kind = kind;
        entry_count++;
    }

    eq_ui_music_browser_model_t model = {
        .path = g_media_listing.path,
        .title = g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO ?
            peq_title : "Choose music",
        .instruction = g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO ?
            "Assign an Equalizer APO .txt to this output" : "Open folders or play a song",
        .empty_message = g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO ?
            "Put .txt files in ur0:data/eqvita/peq" : "No music found here",
        .selected_row = g_selected[SCREEN_MUSIC_BROWSER],
        .entries = entries,
        .entry_count = entry_count,
        .bounds = g_row_bounds,
        .max_bounds = EQ_UI_MAX_VISIBLE_ROWS,
        .bound_count = &g_row_bound_count
    };

    g_row_bound_count = 0;
    eq_ui_draw_music_browser_deck(&model);
}

static void draw_current_rows(void)
{
    int count = current_row_count();
    int visible = visible_row_capacity();
    int selected = g_selected[g_screen];
    int scroll = g_scroll_top[g_screen];
    g_row_bound_count = 0;

    for (int i = 0; i < visible && scroll + i < count && i < EQ_UI_MAX_VISIBLE_ROWS; ++i) {
        int row = scroll + i;
        const char *icon;
        const char *label;
        const char *desc;
        eq_ui_row_kind_t kind;
        char value[64];
        eq_ui_row_bounds_t *bounds = &g_row_bounds[g_row_bound_count++];

        row_text(g_screen, row, &icon, &label, &desc, &kind);
        row_value(value, sizeof(value), g_screen, row);

        if (g_screen == SCREEN_SIMPLE && row >= SIMPLE_ROW_PREAMP && row <= SIMPLE_ROW_TREBLE) {
            int32_t amount = row == SIMPLE_ROW_PREAMP ? g_control.preamp_mdB :
                row == SIMPLE_ROW_BASS ? g_control.band_gain_mdB[1] :
                row == SIMPLE_ROW_MIDRANGE ? g_control.band_gain_mdB[4] : g_control.band_gain_mdB[7];
            eq_ui_draw_slider(i, row, row == selected, icon, label, desc, value, amount, kind, bounds);
        } else if (g_screen == SCREEN_ADVANCED && row == ADV_ROW_PREAMP) {
            eq_ui_draw_slider(i, row, row == selected, icon, label, desc, value, g_control.preamp_mdB, kind, bounds);
        } else if (g_screen == SCREEN_ADVANCED && advanced_row_is_eq_value(row)) {
            int index = row - ADV_BAND_ROW_BASE;
            int32_t amount = g_control.eq_mode == EQ_MODE_PARAMETRIC ?
                (g_control.parametric_filters[index].type == EQ_FILTER_COPY ? 0 :
                    g_control.parametric_filters[index].data.filter.gain_mdB) :
                g_control.band_gain_mdB[index];
            if (kind == EQ_UI_ROW_ADJUST) {
                eq_ui_draw_slider(i, row, row == selected, icon, label, desc, value, amount, kind, bounds);
            } else {
                eq_ui_draw_row(i, row, row == selected, icon, label, desc, value, kind, bounds);
            }
        } else {
            eq_ui_draw_row(i, row, row == selected, icon, label, desc, value, kind, bounds);
        }
    }
}

static eq_ui_row_kind_t selected_row_kind(void)
{
    const char *icon;
    const char *label;
    const char *desc;
    eq_ui_row_kind_t kind;

    row_text(g_screen, g_selected[g_screen], &icon, &label, &desc, &kind);
    return kind;
}

static void render_frame(void)
{
    char left[64];
    char right[64];
    char subtitle[96];
    char footer_left[32];
    char footer_center[64];
    const char *exit_prompt_actions[] = {
        "Save to this slot",
        "Discard and leave",
        "Keep editing"
    };
    const char *exit_prompt_icons[] = {
        "save",
        "reset",
        "tune"
    };
    char exit_prompt_title[64];

    ensure_selection_visible();
    if (g_exit_prompt_active) {
        snprintf(footer_left, sizeof(footer_left), "%s Back", button_name(g_cancel_button));
        snprintf(footer_center, sizeof(footer_center), "%s Select", button_name(g_confirm_button));
    } else {
        snprintf(footer_left, sizeof(footer_left), "%s %s",
                 button_name(g_cancel_button), g_screen == SCREEN_HOME ? "Exit" : "Back");
        if (selected_row_kind() == EQ_UI_ROW_READONLY) {
            snprintf(footer_center, sizeof(footer_center), "START Bypass");
        } else {
            snprintf(footer_center, sizeof(footer_center), "%s Select   START Bypass", button_name(g_confirm_button));
        }
    }

    if (g_plugin_compatible) {
        poll_plugin_status();
    }

    snprintf(left, sizeof(left), "EQVita v%d.%d.%d", g_version.major, g_version.minor, g_version.patch);
    snprintf(right, sizeof(right), "%s - %s", eq_target_str(), g_control.enabled ? "On" : "Bypass");
    snprintf(subtitle, sizeof(subtitle), "%s - Slot %d - %s",
             g_control.enabled ? "EQ on" : "EQ off",
             g_preset_slot + 1,
             eq_target_str());

    eq_ui_begin_frame();
    if (g_screen == SCREEN_MUSIC || g_screen == SCREEN_MUSIC_BROWSER) {
        eq_ui_draw_shell("", "", left, right);
    } else {
        eq_ui_draw_shell(screen_title(g_screen), g_screen == SCREEN_HOME ? subtitle : screen_subtitle(g_screen), left, right);
    }
    if (g_screen == SCREEN_MUSIC) {
        draw_music_player_screen();
    } else if (g_screen == SCREEN_MUSIC_BROWSER) {
        draw_music_browser_screen();
    } else {
        draw_current_rows();
    }
    eq_ui_draw_footer(footer_left, footer_center, "Triangle Help");
    if (g_message_frames > 0 && g_message[0]) {
        eq_ui_draw_message(g_message);
        g_message_frames--;
    }
    if (g_exit_prompt_active) {
        snprintf(exit_prompt_title, sizeof(exit_prompt_title), "Save changes to preset slot %d?", g_preset_slot + 1);
        eq_ui_draw_confirm_dialog(exit_prompt_title,
                                  "Live EQ keeps playing either way.",
                                  exit_prompt_actions,
                                  exit_prompt_icons,
                                  3,
                                  g_exit_prompt_selected);
    }
    eq_ui_end_frame();
}

static void adjust_current(int delta)
{
    int row = g_selected[g_screen];
    switch (g_screen) {
        case SCREEN_PRESETS:
            if (row == PRESETS_ROW_SLOT) {
                adjust_preset_slot(delta > 0 ? 1 : -1);
            }
            break;
        case SCREEN_OUTPUT_PEQ:
            if (row == OUTPUT_PEQ_ROW_TARGET) {
                adjust_route_profile_target(delta);
            }
            break;
        case SCREEN_SIMPLE:
            if (row == SIMPLE_ROW_PREAMP) adjust_preamp(delta);
            else if (row == SIMPLE_ROW_BASS) apply_simple_eq(g_control.band_gain_mdB[1] + delta, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
            else if (row == SIMPLE_ROW_MIDRANGE) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] + delta, g_control.band_gain_mdB[7], 1);
            else if (row == SIMPLE_ROW_TREBLE) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] + delta, 1);
            else if (row == SIMPLE_ROW_PRESET_SLOT) adjust_preset_slot(delta > 0 ? 1 : -1);
            break;
        case SCREEN_ADVANCED:
            if (row == ADV_ROW_PREAMP) {
                adjust_preamp(delta);
            } else if (advanced_row_is_eq_value(row)) {
                if (g_control.eq_mode == EQ_MODE_PARAMETRIC) {
                    adjust_parametric_operation(row - ADV_BAND_ROW_BASE, delta);
                } else {
                    adjust_band(row - ADV_BAND_ROW_BASE, delta);
                }
            } else if (row == ADV_ROW_PRESET_SLOT) {
                adjust_preset_slot(delta > 0 ? 1 : -1);
            }
            break;
        case SCREEN_MUSIC:
            if (row == MUSIC_ROW_VOLUME) {
                eqvita_media_player_adjust_volume(&g_media_player, delta > 0 ? 5 : -5);
                g_media_status = eqvita_media_player_status(&g_media_player);
            }
            break;
        case SCREEN_THEMES: {
            move_selection(delta > 0 ? 1 : -1);
            break;
        }
        case SCREEN_SETTINGS:
            if (row == SETTINGS_ROW_HEADROOM) adjust_headroom_mode(delta > 0 ? 1 : -1);
            break;
        default:
            break;
    }
}

static void activate_current(void)
{
    int row = g_selected[g_screen];
    if (g_screen == SCREEN_HOME) {
        if (row == HOME_ROW_ENABLED) {
            toggle_enabled();
            return;
        }
        {
            app_screen_t destination = home_screen_for_row(row);
            if (destination == SCREEN_OUTPUT_PEQ) {
                open_output_peq_screen(SCREEN_HOME);
                return;
            }
            if (is_eq_edit_screen(destination)) {
                g_eq_return_screen = SCREEN_HOME;
            }
            change_screen(destination);
        }
        return;
    }

    switch (g_screen) {
        case SCREEN_PRESETS:
            if (row == PRESETS_ROW_STOCK_DEPTH) apply_preset_stock_depth();
            else if (row == PRESETS_ROW_MOD_SWITCH) apply_preset_mod_switch();
            else if (row == PRESETS_ROW_OUTPUT_PEQ) open_output_peq_screen(SCREEN_PRESETS);
            else if (row == PRESETS_ROW_SAVE) save_preset_with_message();
            else if (row == PRESETS_ROW_LOAD) load_preset_with_message();
            else if (row == PRESETS_ROW_RESET) reset_defaults();
            break;
        case SCREEN_OUTPUT_PEQ:
            if (row == OUTPUT_PEQ_ROW_CHOOSE) {
                g_peq_browser_return_screen = SCREEN_OUTPUT_PEQ;
                open_peq_browser_root();
            } else if (row == OUTPUT_PEQ_ROW_INSPECT) {
                if (eq_route_profile_bank_has_route(&g_route_profiles,
                                                     g_route_profile_edit_route)) {
                    g_eq_return_screen = SCREEN_OUTPUT_PEQ;
                    change_screen(SCREEN_ADVANCED);
                } else {
                    set_message("Assign a PEQ to this output first");
                }
            } else if (row == OUTPUT_PEQ_ROW_CLEAR) {
                clear_route_profile();
            }
            break;
        case SCREEN_SIMPLE:
            if (row == SIMPLE_ROW_SAVE_CURRENT) save_preset_with_message();
            else if (row == SIMPLE_ROW_SAVE_NEXT) save_as_next_preset();
            else if (row == SIMPLE_ROW_UNDO_ENTRY) restore_eq_entry();
            else if (row == SIMPLE_ROW_RESET_EQ) reset_defaults();
            break;
        case SCREEN_ADVANCED:
            if (row == ADV_ROW_SAVE_CURRENT) save_preset_with_message();
            else if (row == ADV_ROW_SAVE_NEXT) save_as_next_preset();
            else if (row == ADV_ROW_UNDO_ENTRY) restore_eq_entry();
            else if (row == ADV_ROW_RESET_EQ) reset_defaults();
            break;
        case SCREEN_MUSIC:
            if (row == MUSIC_ROW_CHOOSE) {
                open_music_browser_roots();
            } else if (row == MUSIC_ROW_PLAY_PAUSE) {
                if (!g_media_status.path[0]) {
                    set_message("Choose a music file first");
                } else if (g_media_status.state == EQVITA_MEDIA_PLAYER_STOPPED ||
                           g_media_status.state == EQVITA_MEDIA_PLAYER_FINISHED ||
                           g_media_status.state == EQVITA_MEDIA_PLAYER_ERROR) {
                    media_player_play_selected(g_media_status.path);
                } else {
                    eqvita_media_player_toggle_pause(&g_media_player);
                    g_media_status = eqvita_media_player_status(&g_media_player);
                }
            } else if (row == MUSIC_ROW_STOP) {
                eqvita_media_player_stop(&g_media_player);
                g_media_status = eqvita_media_player_status(&g_media_player);
                set_message("Preview stopped");
            } else if (row == MUSIC_ROW_LOOP) {
                eqvita_media_player_set_loop(&g_media_player, !g_media_status.loop);
                g_media_status = eqvita_media_player_status(&g_media_player);
                set_message(g_media_status.loop ? "Loop on" : "Loop off");
            }
            break;
        case SCREEN_MUSIC_BROWSER:
            if (browser_async_is_loading()) {
                set_message("Folder is still loading");
                break;
            }
            if (row >= 0 && row < g_media_listing.count) {
                eqvita_media_entry_t *entry = &g_media_listing.entries[row];
                char next_path[EQVITA_MEDIA_MAX_PATH];
                if (snprintf(next_path, sizeof(next_path), "%s", entry->path) < 0) {
                    set_message("Path error");
                    break;
                }
                if (entry->kind == EQVITA_MEDIA_ENTRY_FILE) {
                    if (g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO) {
                        import_equalizer_apo_file(next_path);
                    } else {
                        media_player_play_selected(next_path);
                        change_screen(SCREEN_MUSIC);
                    }
                } else {
                    if (g_browser_purpose == BROWSER_PURPOSE_EQUALIZER_APO) {
                        open_apo_browser_path(next_path);
                    } else {
                        open_music_browser_path(next_path);
                    }
                }
            }
            break;
        case SCREEN_THEMES:
            apply_theme_index(row);
            break;
        case SCREEN_SETTINGS:
            if (row == SETTINGS_ROW_ENABLED) toggle_enabled();
            else if (row == SETTINGS_ROW_SCOPE) toggle_speaker_only();
            else if (row == SETTINGS_ROW_HPF) toggle_hpf();
            else if (row == SETTINGS_ROW_HEADROOM) adjust_headroom_mode(1);
            else if (row == SETTINGS_ROW_STARTUP) save_boot_state_with_message();
            break;
        default:
            break;
    }
}

static int touch_to_screen(const SceTouchReport *report, int *out_x, int *out_y)
{
    int x;
    int y;
    int min_x;
    int min_y;
    int max_x;
    int max_y;

    if (!report || !out_x || !out_y) {
        return -1;
    }

    x = report->x;
    y = report->y;

    if (g_touch_panel_ready) {
        min_x = g_touch_panel.minDispX;
        min_y = g_touch_panel.minDispY;
        max_x = g_touch_panel.maxDispX;
        max_y = g_touch_panel.maxDispY;
        if (max_x > min_x && max_y > min_y) {
            x = ((report->x - min_x) * EQ_UI_SCREEN_W) / (max_x - min_x);
            y = ((report->y - min_y) * EQ_UI_SCREEN_H) / (max_y - min_y);
        }
    } else if (x >= EQ_UI_SCREEN_W || y >= EQ_UI_SCREEN_H) {
        x /= 2;
        y /= 2;
    }

    *out_x = clamp(x, 0, EQ_UI_SCREEN_W - 1);
    *out_y = clamp(y, 0, EQ_UI_SCREEN_H - 1);
    return 0;
}

static int row_at_point(int x, int y)
{
    for (int i = 0; i < g_row_bound_count; ++i) {
        eq_ui_row_bounds_t *b = &g_row_bounds[i];
        if (x >= b->x && x < b->x + b->w && y >= b->y && y < b->y + b->h) {
            return b->row;
        }
    }
    return -1;
}

static int exit_prompt_row_at_point(int x, int y)
{
    if (x < EQ_UI_DIALOG_X + 18 || x >= EQ_UI_DIALOG_X + EQ_UI_DIALOG_W - 18) {
        return -1;
    }
    for (int i = 0; i < 3; ++i) {
        int row_y = EQ_UI_DIALOG_ROW_Y + i * (EQ_UI_DIALOG_ROW_H + EQ_UI_DIALOG_ROW_GAP);
        if (y >= row_y && y < row_y + EQ_UI_DIALOG_ROW_H) {
            return i;
        }
    }
    return -1;
}

static void handle_touch(void)
{
    SceTouchData touch;
    int has_touch;
    int x = 0;
    int y = 0;

    memset(&touch, 0, sizeof(touch));
    if (sceTouchPeek(SCE_TOUCH_PORT_FRONT, &touch, 1) < 0) {
        return;
    }

    has_touch = touch.reportNum > 0;
    if (has_touch && touch_to_screen(&touch.report[0], &x, &y) < 0) {
        return;
    }

    if (g_exit_prompt_active) {
        if (has_touch && !g_touch_down) {
            int row;
            g_touch_down = 1;
            g_touch_start_x = x;
            g_touch_start_y = y;
            g_touch_last_y = y;
            g_touch_moved = 0;
            row = exit_prompt_row_at_point(x, y);
            if (row >= 0) {
                g_exit_prompt_selected = row;
            }
        } else if (has_touch && g_touch_down) {
            if ((x - g_touch_start_x > 18 || g_touch_start_x - x > 18) ||
                (y - g_touch_start_y > 18 || g_touch_start_y - y > 18)) {
                g_touch_moved = 1;
            }
        } else if (!has_touch && g_touch_down) {
            int row = exit_prompt_row_at_point(g_touch_start_x, g_touch_start_y);
            if (!g_touch_moved && row >= 0) {
                g_exit_prompt_selected = row;
                activate_exit_prompt();
            }
            g_touch_down = 0;
        }
        return;
    }

    if (has_touch && !g_touch_down) {
        int row;
        g_touch_down = 1;
        g_touch_start_x = x;
        g_touch_start_y = y;
        g_touch_last_y = y;
        g_touch_moved = 0;
        row = row_at_point(x, y);
        if (row >= 0 && row_is_selectable(g_screen, row)) {
            g_selected[g_screen] = row;
            ensure_selection_visible();
        }
    } else if (has_touch && g_touch_down) {
        int dy = y - g_touch_last_y;
        if (dy > 34 || dy < -34) {
            g_touch_moved = 1;
            scroll_current(dy > 0 ? -1 : 1);
            g_touch_last_y = y;
        }
        if ((x - g_touch_start_x > 18 || g_touch_start_x - x > 18) ||
            (y - g_touch_start_y > 18 || g_touch_start_y - y > 18)) {
            g_touch_moved = 1;
        }
    } else if (!has_touch && g_touch_down) {
        int row = row_at_point(g_touch_start_x, g_touch_start_y);
        if (!g_touch_moved && row >= 0 && row_is_selectable(g_screen, row)) {
            g_selected[g_screen] = row;
            ensure_selection_visible();
            activate_current();
        }
        g_touch_down = 0;
    }
}

int main(void)
{
    int repeat_timer = 0;
    int last_buttons = 0;
    eqvita_startup_source_t startup_source = EQVITA_STARTUP_SOURCE_DEFAULT;

    g_log_run_id = sceKernelGetProcessTimeLow();
    app_log("---- run begin run_id=%08x ----", g_log_run_id);
    (void)prepare_peq_library();

    eqvita_app_state_init(&g_app_state);
    eq_route_profile_bank_init(&g_route_profiles);
    memset(g_route_profile_sources, 0, sizeof(g_route_profile_sources));
    if (eqvita_load_route_profiles(EQVITA_DATA_DIR,
                                    &g_route_profiles,
                                    g_route_profile_sources) < 0) {
        eq_route_profile_bank_init(&g_route_profiles);
        memset(g_route_profile_sources, 0, sizeof(g_route_profile_sources));
    }
    g_route_profile_edit_route = g_route_profiles.selected_route;
    eqvita_media_player_init(&g_media_player);
    g_media_status = eqvita_media_player_status(&g_media_player);
    browser_async_init();
    init_app_util_resources();
    init_button_mapping();
    eq_ui_set_theme(load_theme_index());
    if (eq_ui_init() < 0) {
        browser_async_shutdown();
        shutdown_app_util_resources();
        return sceKernelExitProcess(1);
    }

    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    g_touch_panel_ready = (sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT, &g_touch_panel) >= 0);

    /* The weak syscall import remains callable when the kernel plugin is
       absent or failed to load. It then leaves this zeroed version unchanged,
       allowing the UI to report a mismatch instead of jumping through a null
       import stub. */
    memset(&g_version, 0, sizeof(g_version));
    EqGetVersion(&g_version);
    g_control.route_hint = (uint8_t)detect_route_user();
    g_plugin_compatible = (g_version.major == EQ_VERSION_MAJOR && g_version.minor == EQ_VERSION_MINOR);
    g_app_state.plugin_compatible = (uint8_t)g_plugin_compatible;
    if (g_plugin_compatible) {
        eq_control_t startup_control;
        int startup_slot = 0;
        eq_control_init_defaults(&startup_control);
        if (eqvita_load_app_startup_control(EQVITA_DATA_DIR,
                                            EQVITA_PRESET_SLOT_COUNT,
                                            0,
                                            &startup_control,
                                            &startup_slot,
                                            &startup_source) < 0) {
            eq_control_init_defaults(&startup_control);
            startup_slot = 0;
        }
        if (g_route_profiles.enabled_mask != 0) {
            uint8_t selected_route = g_route_profiles.selected_route;
            int selected_index;
            uint8_t master_enabled = startup_control.enabled;

            if (!eq_route_profile_bank_has_route(&g_route_profiles, selected_route)) {
                for (uint8_t route = EQ_ROUTE_SPEAKER; route <= EQ_ROUTE_BLUETOOTH; ++route) {
                    if (eq_route_profile_bank_has_route(&g_route_profiles, route)) {
                        selected_route = route;
                        break;
                    }
                }
            }
            selected_index = eq_route_profile_index(selected_route);
            if (selected_index >= 0) {
                startup_control = g_route_profiles.profiles[selected_index];
                startup_control.enabled = master_enabled;
                g_route_profile_edit_route = selected_route;
                g_route_profiles.selected_route = selected_route;
            }
        }
        eqvita_app_state_set_preset_slot(&g_app_state, startup_slot);
        if (apply_control_candidate(&startup_control, 0) < 0) {
            set_message("Plugin startup sync failed");
        }
        eqvita_app_state_mark_boot_saved(&g_app_state);
        eqvita_app_state_mark_current_preset_saved(&g_app_state);
    } else {
        memset(&g_status, 0, sizeof(g_status));
        g_status.sample_rate = 48000;
        g_status.route = EQ_ROUTE_UNKNOWN;
        g_status.bypass_reason = EQ_BYPASS_DISABLED;
    }
    app_log("start: run_id=%08x version=%d.%d.%d compatible=%d source=%s slot=%d route=%s",
        g_log_run_id,
        g_version.major, g_version.minor, g_version.patch,
        g_plugin_compatible,
        startup_source_str(g_plugin_compatible ? startup_source : EQVITA_STARTUP_SOURCE_DEFAULT),
        g_preset_slot + 1,
        route_str(g_control.route_hint));
    app_log("apputil: init=%d music_mount=%d mounted=%d",
        g_apputil_init_result,
        g_music_mount_result,
        g_music_mounted);
    if (sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG) < 0) {
        app_log("input: analog sampling unavailable");
    }

    while (1) {
        SceCtrlData pad;
        int newly;
        int held;
        int active_input;
        int buttons;

        browser_async_poll();
        if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) {
            render_frame();
            sceKernelDelayThread(1000);
            continue;
        }

        buttons = (int)(pad.buttons | analog_navigation_buttons(&pad));
        newly = (~last_buttons) & buttons;
        held = buttons;

        if (held != last_buttons) {
            repeat_timer = 0;
            last_buttons = held;
        }

        active_input = newly;
        if (repeat_timer > 15) {
            if ((repeat_timer - 15) % 3 == 0) {
                active_input |= (held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER));
            }
        }
        if (held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
            repeat_timer++;
        } else {
            repeat_timer = 0;
        }

        if (g_exit_prompt_active) {
            if (active_input & SCE_CTRL_UP) {
                g_exit_prompt_selected = (g_exit_prompt_selected + 2) % 3;
            } else if (active_input & SCE_CTRL_DOWN) {
                g_exit_prompt_selected = (g_exit_prompt_selected + 1) % 3;
            } else if (newly & g_confirm_button) {
                activate_exit_prompt();
            } else if (newly & g_cancel_button) {
                close_exit_prompt();
            }
        } else if (active_input & SCE_CTRL_UP) {
            move_selection(-1);
        } else if (active_input & SCE_CTRL_DOWN) {
            move_selection(1);
        } else if (active_input & SCE_CTRL_LEFT) {
            adjust_current(-STEP_FINE);
        } else if (active_input & SCE_CTRL_RIGHT) {
            adjust_current(STEP_FINE);
        } else if (active_input & SCE_CTRL_LTRIGGER) {
            adjust_current(-STEP_COARSE);
        } else if (active_input & SCE_CTRL_RTRIGGER) {
            adjust_current(STEP_COARSE);
        } else if (newly & g_confirm_button) {
            activate_current();
        } else if (newly & g_cancel_button) {
            if (g_screen == SCREEN_HOME) {
                break;
            }
            if (g_screen == SCREEN_MUSIC_BROWSER) {
                leave_music_browser();
                continue;
            }
            request_leave_current_screen();
        } else if (newly & SCE_CTRL_TRIANGLE) {
            change_screen(SCREEN_ABOUT);
        } else if (newly & SCE_CTRL_START) {
            toggle_enabled();
        }

        handle_touch();
        browser_async_poll();
        refresh_route_hint();
        media_player_poll_log();
        render_frame();
        maybe_log_status();
        maybe_log_diagnostics();
        sceKernelDelayThread(1000);
    }

    browser_async_shutdown();
    eqvita_media_player_shutdown(&g_media_player);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_STOP);
    eq_ui_fini();
    shutdown_app_util_resources();
    return sceKernelExitProcess(0);
}
