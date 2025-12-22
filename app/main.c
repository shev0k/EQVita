#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/system_param.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "debug_screen/debugScreen.h"
#include "../common/eq_shared.h"

#define X_MARGIN 16
#define Y_MARGIN 16
#define UI_BUF 512
#define STEP_FINE 500
#define STEP_COARSE 1000
#define PRESET_PATH_FMT "ur0:data/eqvita/preset%d.bin"

// Colors
#define COL_RESET "\e[0m"
#define COL_HEADER "\e[1;36m"
#define COL_SECTION "\e[1;33m"
#define COL_SELECTED "\e[7m"
#define COL_VALUE "\e[1;37m"
#define COL_METER_L "\e[32m"
#define COL_METER_M "\e[33m"
#define COL_METER_H "\e[31m"

static const char *band_labels[EQ_BANDS] = {
    "31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
};

static eq_control_t g_control;
static eq_status_t g_status;
static eq_version_t g_version;
static int g_selected = 0;
static int g_preset_slot = 0;
static int g_scroll_top = 0;
static int g_view_mode = 0;

static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static const char *route_str(uint8_t r) {
    switch (r) {
        case EQ_ROUTE_SPEAKER: return "Speaker";
        case EQ_ROUTE_HEADPHONES: return "Headphones";
        case EQ_ROUTE_BLUETOOTH: return "Bluetooth";
        default: return "Unknown";
    }
}

static void ui_init(void) {
    PsvDebugScreenFont *font = psvDebugScreenGetFont();
    font = psvDebugScreenScaleFont2x(font);
    psvDebugScreenSetFont(font);
    psvDebugScreenSetBgColor(0x000000);
    psvDebugScreenSetFgColor(0xFFFFFF);

    // Enable EQ on first launch
    g_control.enabled = 1;
    g_control.dirty_counter++;
}

static void ui_reset(void) {
    psvDebugScreenBlank(0x00);
    psvDebugScreenSetCoordsXY((int[]){X_MARGIN}, (int[]){Y_MARGIN});
}

static void ui_line(const char *fmt, ...) {
    char buf[UI_BUF];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    psvDebugScreenPuts(buf);
}

static void mark_dirty(void) {
    g_control.dirty_counter++;
    EqSetControl(&g_control);
    EqGetStatus(&g_status);
}

static void toggle_enabled(void) {
    g_control.enabled = !g_control.enabled;
    mark_dirty();
}

static void toggle_speaker_only(void) {
    g_control.speaker_only = !g_control.speaker_only;
    mark_dirty();
}

static void toggle_hpf(void) {
    g_control.hpf_enabled = !g_control.hpf_enabled;
    mark_dirty();
}

static void adjust_preamp(int delta) {
    int v = g_control.preamp_mdB + delta;
    g_control.preamp_mdB = clamp(v, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    mark_dirty();
}

static void adjust_band(int idx, int delta) {
    int v = g_control.band_gain_mdB[idx] + delta;
    g_control.band_gain_mdB[idx] = clamp(v, -EQ_MAX_ABS_GAIN_MDB, EQ_MAX_ABS_GAIN_MDB);
    mark_dirty();
}

static void save_preset(void) {
    sceIoMkdir("ur0:data", 0777);
    sceIoMkdir("ur0:data/eqvita", 0777);
    char path[64];
    snprintf(path, sizeof(path), PRESET_PATH_FMT, g_preset_slot);
    SceUID fd = sceIoOpen(path, SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC, 0777);
    if (fd < 0) return;
    sceIoWrite(fd, &g_control, sizeof(g_control));
    sceIoClose(fd);
}

static void load_preset(void) {
    char path[64];
    snprintf(path, sizeof(path), PRESET_PATH_FMT, g_preset_slot);
    SceUID fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) return;
    eq_control_t tmp;
    int r = sceIoRead(fd, &tmp, sizeof(tmp));
    sceIoClose(fd);
    if (r == sizeof(tmp)) {
        g_control = tmp;
        EqSetControl(&g_control);
        EqGetStatus(&g_status);
        mark_dirty();
    }
}

static void reset_defaults(void) {
    g_control.preamp_mdB = EQ_DEFAULT_PREAMP_MDB;
    for (int i = 0; i < EQ_BANDS; ++i) {
        g_control.band_gain_mdB[i] = 0;
    }
    mark_dirty();
}

static void apply_simple_eq(int bass, int mid, int treble, int auto_preamp) {
    g_control.band_gain_mdB[0] = 0;
    for (int i = 1; i <= 3; ++i) g_control.band_gain_mdB[i] = bass;
    for (int i = 4; i <= 6; ++i) g_control.band_gain_mdB[i] = mid;
    for (int i = 7; i <= 9; ++i) g_control.band_gain_mdB[i] = treble;

    if (auto_preamp) {
        int32_t max_boost = bass;
        if (mid > max_boost) max_boost = mid;
        if (treble > max_boost) max_boost = treble;
        if (max_boost < 0) max_boost = 0;
        g_control.preamp_mdB = -max_boost;
        if (g_control.preamp_mdB < -EQ_MAX_ABS_GAIN_MDB) g_control.preamp_mdB = -EQ_MAX_ABS_GAIN_MDB;
    }
    mark_dirty();
}

static void apply_preset_depth(void) {
    g_control.preamp_mdB = -4000;
    apply_simple_eq(4000, -2000, 2000, 0);
    g_control.preamp_mdB = -4000;
    mark_dirty();
}

static void draw_bar(int mdB) {
    int val = mdB / 1000;
    if (val < -12) val = -12;
    if (val > 12) val = 12;
    
    char bar[22];
    memset(bar, ' ', 21);
    bar[21] = 0;
    bar[10] = '|';

    if (val > 0) {
        int len = (val * 10) / 12;
        for (int i = 0; i < len; ++i) bar[11 + i] = '=';
    } else if (val < 0) {
        int len = (-val * 10) / 12;
        for (int i = 0; i < len; ++i) bar[9 - i] = '=';
    }
    
    psvDebugScreenPuts("[");
    psvDebugScreenPuts(bar);
    psvDebugScreenPuts("]");
}

static void draw_meter_colored(int16_t peak) {
    int val = (peak * 20) / 32767;
    if (val > 20) val = 20;
    
    psvDebugScreenPuts("[");
    for (int i = 0; i < 20; ++i) {
        if (i < val) {
            if (i < 12) psvDebugScreenPuts(COL_METER_L "#");
            else if (i < 16) psvDebugScreenPuts(COL_METER_M "#");
            else psvDebugScreenPuts(COL_METER_H "#");
        } else {
            psvDebugScreenPuts(" ");
        }
    }
    psvDebugScreenPuts(COL_RESET "]");
}

static void ui_render(void) {
    ui_reset();
    
    // Header
    ui_line(COL_HEADER "EQ Vita v%d.%d.%d [%s MODE]" COL_RESET "\n", 
        g_version.major, g_version.minor, g_version.patch, 
        g_view_mode == 0 ? "SIMPLE" : "ADVANCED");
    
    // Status Line
    EqGetStatus(&g_status);
    ui_line("Route: %s (%s) | SR: %u | EQ: %s/%s | Smooth: %s\n",
        route_str(g_status.route),
        g_control.speaker_only ? "Spk" : "All",
        g_status.sample_rate,
        g_control.enabled ? "On" : "Off",
        g_status.eq_active ? "Act" : "Byp",
        g_status.smoothing ? "Yes" : "No");
    ui_line("------------------------------------------------\n");

    // Viewport Calculation
    int total_items = (g_view_mode == 0) ? 11 : 18;
    int viewport_height = 12;
    
    // Auto-scroll
    if (g_selected < g_scroll_top) g_scroll_top = g_selected;
    if (g_selected >= g_scroll_top + viewport_height) g_scroll_top = g_selected - viewport_height + 1;

    // Render List
    for (int i = g_scroll_top; i < g_scroll_top + viewport_height && i < total_items; ++i) {
        int is_sel = (i == g_selected);
        const char *sel_prefix = is_sel ? COL_SELECTED "> " : "  ";
        const char *sel_suffix = is_sel ? COL_RESET : "";

        // Settings Section
        if (i == 0) {
            ui_line(COL_SECTION "[SETTINGS]" COL_RESET "\n");
            ui_line("%sEnabled:      [%s]%s\n", sel_prefix, g_control.enabled?"ON ":"OFF", sel_suffix);
        } else if (i == 1) {
            ui_line("%sSpeaker only: [%s]%s\n", sel_prefix, g_control.speaker_only?"YES":"NO ", sel_suffix);
        } else if (i == 2) {
            ui_line("%sHPF (70Hz):   [%s]%s\n", sel_prefix, g_control.hpf_enabled?"ON ":"OFF", sel_suffix);
        } else if (i == 3) {
            ui_line("%sPreamp:       %+5.1f dB%s\n", sel_prefix, g_control.preamp_mdB/1000.0f, sel_suffix);
        }
        // EQ Section
        else if (g_view_mode == 0) {
            if (i == 4) {
                ui_line("\n");
                ui_line(COL_SECTION "[EQUALIZER]" COL_RESET "\n");
                psvDebugScreenPrintf("%sBass:    ", sel_prefix);
                draw_bar(g_control.band_gain_mdB[1]);
                psvDebugScreenPrintf(" " COL_VALUE "%+5.1f dB" COL_RESET "%s\n", g_control.band_gain_mdB[1]/1000.0f, sel_suffix);
            } else if (i == 5) {
                psvDebugScreenPrintf("%sMidrange:", sel_prefix);
                draw_bar(g_control.band_gain_mdB[4]);
                psvDebugScreenPrintf(" " COL_VALUE "%+5.1f dB" COL_RESET "%s\n", g_control.band_gain_mdB[4]/1000.0f, sel_suffix);
            } else if (i == 6) {
                psvDebugScreenPrintf("%sTreble:  ", sel_prefix);
                draw_bar(g_control.band_gain_mdB[7]);
                psvDebugScreenPrintf(" " COL_VALUE "%+5.1f dB" COL_RESET "%s\n", g_control.band_gain_mdB[7]/1000.0f, sel_suffix);
            }
        } else {
            if (i == 4) {
                ui_line("\n");
                ui_line(COL_SECTION "[EQUALIZER]" COL_RESET "\n");
            }
            if (i >= 4 && i < 14) {
                int band_idx = i - 4;
                psvDebugScreenPrintf("%s%4s Hz ", sel_prefix, band_labels[band_idx]);
                draw_bar(g_control.band_gain_mdB[band_idx]);
                psvDebugScreenPrintf(" " COL_VALUE "%+5.1f dB" COL_RESET "%s\n", g_control.band_gain_mdB[band_idx]/1000.0f, sel_suffix);
            }
        }
        
        // Actions Section
        int action_start = (g_view_mode == 0) ? 7 : 14;
        if (i == action_start) {
            ui_line("\n");
            ui_line(COL_SECTION "[ACTIONS]" COL_RESET "\n");
            ui_line("%sPreset Slot:  [%d]%s\n", sel_prefix, g_preset_slot + 1, sel_suffix);
        } else if (i == action_start + 1) {
            if (g_view_mode == 0) ui_line("%s[ Preset: Depth ]%s\n", sel_prefix, sel_suffix);
            else ui_line("%s[ Save Preset ]%s\n", sel_prefix, sel_suffix);
        } else if (i == action_start + 2) {
            if (g_view_mode == 0) ui_line("%s[ Save Preset   ]%s\n", sel_prefix, sel_suffix);
            else ui_line("%s[ Load Preset ]%s\n", sel_prefix, sel_suffix);
        } else if (i == action_start + 3) {
            if (g_view_mode == 0) ui_line("%s[ Load Preset   ]%s\n", sel_prefix, sel_suffix);
            else ui_line("%s[ Reset EQ    ]%s\n", sel_prefix, sel_suffix);
        } else if (i == action_start + 4 && g_view_mode == 0) {
             ui_line("%s[ Reset EQ      ]%s\n", sel_prefix, sel_suffix);
        }
    }
    
    // Peak Meters (Fixed at bottom)
    ui_line("\n" COL_SECTION "[PEAK LEVELS]" COL_RESET "\n");
    ui_line("L: "); draw_meter_colored(g_status.peak_l); ui_line("\n");
    ui_line("R: "); draw_meter_colored(g_status.peak_r); ui_line(" Clips: %d\n", g_status.clip_events);

    ui_line("------------------------------------------------\n");
    ui_line("[HELP] " COL_VALUE "X" COL_RESET ":Toggle " COL_VALUE "O" COL_RESET ":Exit " COL_VALUE "Start" COL_RESET ":Bypass " COL_VALUE "Select" COL_RESET ":View\n");
}

int main(void) {
    psvDebugScreenInit();
    psvDebugScreenSetBgColor(0x000000);
    psvDebugScreenSetFgColor(0xFFFFFF);
    psvDebugScreenPuts("EQ Vita starting...\n");

    EqGetVersion(&g_version);
    psvDebugScreenPrintf("Version %d.%d.%d\n", g_version.major, g_version.minor, g_version.patch);
    psvDebugScreenSwapFb();

    memset(&g_control, 0, sizeof(g_control));
    g_control.version = (EQ_VERSION_MAJOR << 16) | EQ_VERSION_MINOR;
    g_control.size = sizeof(eq_shared_block_t);
    g_control.preamp_mdB = EQ_DEFAULT_PREAMP_MDB;
    g_control.speaker_only = 1;
    g_control.enabled = 1;
    g_control.hpf_enabled = 1;
    g_control.dirty_counter = 1;
    EqSetControl(&g_control);
    EqGetStatus(&g_status);

    for (int i = 0; i < 60; ++i) { sceDisplayWaitVblankStartMulti(1); }

    ui_init();
    psvDebugScreenSwapFb();

#define REPEAT_DELAY 15
#define REPEAT_RATE 3

    SceCtrlData last = {0};
    int repeat_timer = 0;
    int last_buttons = 0;

    load_preset();

    while (1) {
        sceDisplayWaitVblankStartMulti(1);

        SceCtrlData pad;
        if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0) { continue; }
        
        int newly = (~last.buttons) & pad.buttons;
        int held = pad.buttons;
        
        if (held != last_buttons) {
            repeat_timer = 0;
            last_buttons = held;
        }

        int active_input = newly;
        
        if (repeat_timer > REPEAT_DELAY) {
            if ((repeat_timer - REPEAT_DELAY) % REPEAT_RATE == 0) {
                active_input |= (held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER));
            }
        }
        if (held & (SCE_CTRL_UP | SCE_CTRL_DOWN | SCE_CTRL_LEFT | SCE_CTRL_RIGHT | SCE_CTRL_LTRIGGER | SCE_CTRL_RTRIGGER)) {
            repeat_timer++;
        } else {
            repeat_timer = 0;
        }

        last = pad;

        int rows_simple = 4 + 3 + 5; // 12 items (0-11)
        int rows_advanced = 4 + EQ_BANDS + 4; // 18 items (0-17)
        int rows = (g_view_mode == 0) ? rows_simple : rows_advanced;

        if (active_input & SCE_CTRL_UP) {
            g_selected = (g_selected - 1 + rows) % rows;
        } else if (active_input & SCE_CTRL_DOWN) {
            g_selected = (g_selected + 1) % rows;
        } else if (active_input & SCE_CTRL_LEFT) {
            if (g_selected == 0 && (newly & SCE_CTRL_LEFT)) toggle_enabled();
            else if (g_selected == 1 && (newly & SCE_CTRL_LEFT)) toggle_speaker_only();
            else if (g_selected == 2 && (newly & SCE_CTRL_LEFT)) toggle_hpf();
            else if (g_selected == 3) adjust_preamp(-STEP_FINE);
            else if (g_view_mode == 0) { // Simple Mode
                if (g_selected == 4) apply_simple_eq(g_control.band_gain_mdB[1] - STEP_FINE, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
                else if (g_selected == 5) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] - STEP_FINE, g_control.band_gain_mdB[7], 1);
                else if (g_selected == 6) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] - STEP_FINE, 1);
                else if (g_selected == 7 && (newly & SCE_CTRL_LEFT)) { g_preset_slot = (g_preset_slot + 2) % 3; }
            } else { // Advanced Mode
                if (g_selected >= 4 && g_selected < 4 + EQ_BANDS) {
                    adjust_band(g_selected - 4, -STEP_FINE);
                } else if (g_selected == 4 + EQ_BANDS && (newly & SCE_CTRL_LEFT)) {
                    g_preset_slot = (g_preset_slot + 2) % 3;
                }
            }
        } else if (active_input & SCE_CTRL_RIGHT) {
            if (g_selected == 0 && (newly & SCE_CTRL_RIGHT)) toggle_enabled();
            else if (g_selected == 1 && (newly & SCE_CTRL_RIGHT)) toggle_speaker_only();
            else if (g_selected == 2 && (newly & SCE_CTRL_RIGHT)) toggle_hpf();
            else if (g_selected == 3) adjust_preamp(STEP_FINE);
            else if (g_view_mode == 0) { // Simple Mode
                if (g_selected == 4) apply_simple_eq(g_control.band_gain_mdB[1] + STEP_FINE, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
                else if (g_selected == 5) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] + STEP_FINE, g_control.band_gain_mdB[7], 1);
                else if (g_selected == 6) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] + STEP_FINE, 1);
                else if (g_selected == 7 && (newly & SCE_CTRL_RIGHT)) { g_preset_slot = (g_preset_slot + 1) % 3; }
            } else { // Advanced Mode
                if (g_selected >= 4 && g_selected < 4 + EQ_BANDS) {
                    adjust_band(g_selected - 4, STEP_FINE);
                } else if (g_selected == 4 + EQ_BANDS && (newly & SCE_CTRL_RIGHT)) {
                    g_preset_slot = (g_preset_slot + 1) % 3;
                }
            }
        } else if (active_input & SCE_CTRL_LTRIGGER) {
            if (g_selected == 3) adjust_preamp(-STEP_COARSE);
            else if (g_view_mode == 0) {
                if (g_selected == 4) apply_simple_eq(g_control.band_gain_mdB[1] - STEP_COARSE, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
                else if (g_selected == 5) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] - STEP_COARSE, g_control.band_gain_mdB[7], 1);
                else if (g_selected == 6) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] - STEP_COARSE, 1);
            } else {
                if (g_selected >= 4 && g_selected < 4 + EQ_BANDS) {
                    adjust_band(g_selected - 4, -STEP_COARSE);
                }
            }
        } else if (active_input & SCE_CTRL_RTRIGGER) {
            if (g_selected == 3) adjust_preamp(STEP_COARSE);
            else if (g_view_mode == 0) {
                if (g_selected == 4) apply_simple_eq(g_control.band_gain_mdB[1] + STEP_COARSE, g_control.band_gain_mdB[4], g_control.band_gain_mdB[7], 1);
                else if (g_selected == 5) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4] + STEP_COARSE, g_control.band_gain_mdB[7], 1);
                else if (g_selected == 6) apply_simple_eq(g_control.band_gain_mdB[1], g_control.band_gain_mdB[4], g_control.band_gain_mdB[7] + STEP_COARSE, 1);
            } else {
                if (g_selected >= 4 && g_selected < 4 + EQ_BANDS) {
                    adjust_band(g_selected - 4, STEP_COARSE);
                }
            }
        } else if (newly & SCE_CTRL_CROSS) {
            if (g_selected == 0) toggle_enabled();
            else if (g_selected == 1) toggle_speaker_only();
            else if (g_selected == 2) toggle_hpf();
            else if (g_view_mode == 0) { // Simple
                if (g_selected == 8) apply_preset_depth();
                else if (g_selected == 9) save_preset();
                else if (g_selected == 10) load_preset();
                else if (g_selected == 11) reset_defaults();
            } else { // Advanced
                int action_start = 14;
                if (g_selected == action_start + 1) save_preset();
                else if (g_selected == action_start + 2) load_preset();
                else if (g_selected == action_start + 3) reset_defaults();
            }
        } else if (newly & SCE_CTRL_SELECT) {
            g_view_mode = !g_view_mode;
            g_selected = 0;
            g_scroll_top = 0;
        } else if (newly & SCE_CTRL_START) {
            toggle_enabled();
        } else if (newly & SCE_CTRL_CIRCLE) {
            break;
        }

        ui_render();
        psvDebugScreenSwapFb();
    }
    
    save_preset();

    return sceKernelExitProcess(0);
}
