#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/ctrl.h>
#include <taihen.h>
#include <psp2/audioout.h>
#include <string.h>

#include "dsp.h"
#include "../common/eq_shared.h"

#define MAX_PORTS 16
#define SCRATCH_MAX_FRAMES 4096

// Diagnostic build flag: when set to 1, skip all initialization.
#define DIAG_MINIMAL 0

static int16_t g_scratch[SCRATCH_MAX_FRAMES * 2];
static SceUID g_scratch_mutex = -1;

typedef struct {
    uint8_t in_use;
    uint8_t channels;
    uint16_t reserved;
    uint32_t len;
    uint32_t freq;
    eq_dsp_state_t dsp;
    uint32_t last_dirty;
    uint8_t last_route;
} port_state_t;

static port_state_t g_ports[MAX_PORTS];
static eq_control_t g_control;
static eq_status_t g_status;

static tai_hook_ref_t g_hook_output;
static tai_hook_ref_t g_hook_open;
static tai_hook_ref_t g_hook_set_config;
static tai_hook_ref_t g_hook_release;
static SceUID g_hook_id_output = -1;
static SceUID g_hook_id_open = -1;
static SceUID g_hook_id_set_config = -1;
static SceUID g_hook_id_release = -1;

static void dsp_reset_identity(void) {
    // Individual ports are reset on open.
}

static inline int32_t compute_max_boost_mdB(const int32_t *band_mdB) {
    int32_t max_boost = 0;
    if (!band_mdB) { return 0; }
    for (int i = 0; i < EQ_BANDS; ++i) {
        if (band_mdB[i] > max_boost) {
            max_boost = band_mdB[i];
        }
    }
    return max_boost;
}

static eq_route_t detect_route(void) {
    SceCtrlData data;
    memset(&data, 0, sizeof(data));
    if (ksceCtrlPeekBufferPositive(0, &data, 1) >= 0) {
        if (data.buttons & SCE_CTRL_HEADPHONE) {
            return EQ_ROUTE_HEADPHONES;
        }
    }
    // TODO: add Bluetooth route detection when a reliable flag/NID is identified.
    return EQ_ROUTE_SPEAKER;
}

static void clamp_control(eq_control_t *ctrl) {
    if (!ctrl) { return; }
    if (ctrl->preamp_mdB > EQ_MAX_ABS_GAIN_MDB) ctrl->preamp_mdB = EQ_MAX_ABS_GAIN_MDB;
    if (ctrl->preamp_mdB < -EQ_MAX_ABS_GAIN_MDB) ctrl->preamp_mdB = -EQ_MAX_ABS_GAIN_MDB;
    for (int i = 0; i < EQ_BANDS; ++i) {
        if (ctrl->band_gain_mdB[i] > EQ_MAX_ABS_GAIN_MDB) ctrl->band_gain_mdB[i] = EQ_MAX_ABS_GAIN_MDB;
        if (ctrl->band_gain_mdB[i] < -EQ_MAX_ABS_GAIN_MDB) ctrl->band_gain_mdB[i] = -EQ_MAX_ABS_GAIN_MDB;
    }
}

#include <psp2kern/io/fcntl.h>

// ... existing includes ...

#define PRESET_PATH "ur0:data/eqvita/preset0.bin"

static void load_preset_kernel(void) {
    SceUID fd = ksceIoOpen(PRESET_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        eq_control_t tmp;
        int r = ksceIoRead(fd, &tmp, sizeof(tmp));
        ksceIoClose(fd);
        if (r == sizeof(tmp)) {
            g_control = tmp;
            // Force update
            g_control.dirty_counter++;
        }
    }
}

static void set_defaults(void) {
    memset(&g_control, 0, sizeof(g_control));
    memset(&g_status, 0, sizeof(g_status));
    g_control.version = (EQ_VERSION_MAJOR << 16) | EQ_VERSION_MINOR;
    g_control.size = sizeof(eq_shared_block_t);
    g_control.enabled = 0;
    g_control.speaker_only = 1;
    g_control.preamp_mdB = EQ_DEFAULT_PREAMP_MDB;
    for (int i = 0; i < EQ_BANDS; ++i) {
        g_control.band_gain_mdB[i] = 0;
    }
    
    // Try to load preset
    load_preset_kernel();

    g_status.sample_rate = 44100;
    g_status.route = EQ_ROUTE_UNKNOWN;
    g_status.eq_active = 0;
}

// No shared mem block needed; app uses syscalls to set/get control and status.

static void update_dsp_if_needed(port_state_t *port, eq_route_t route) {
    if (!port || !port->in_use) return;
    
    uint32_t dirty = g_control.dirty_counter;
    if (dirty != port->last_dirty || port->freq != port->dsp.sample_rate || port->last_route != (uint8_t)route) {
        clamp_control(&g_control);
        int32_t band_mdB[EQ_BANDS];
        for (int i = 0; i < EQ_BANDS; ++i) {
            band_mdB[i] = g_control.band_gain_mdB[i];
        }

        if (route == EQ_ROUTE_SPEAKER) {
            // Do not excite the 31 Hz band on speakers; fold it into 62 Hz and zero it out
            intFold 31 Hz into 62 Hz for speakers
            if (merged > EQ_MAX_ABS_GAIN_MDB) merged = EQ_MAX_ABS_GAIN_MDB;
            if (merged < -EQ_MAX_ABS_GAIN_MDB) merged = -EQ_MAX_ABS_GAIN_MDB;
            band_mdB[1] = merged;
            band_mdB[0] = 0;
        }

        int32_t max_boost = compute_max_boost_mdB(band_mdB);
        int32_t effective_preamp = g_control.preamp_mdB + max_boost;
        if (effective_preamp > EQ_MAX_ABS_GAIN_MDB) effective_preamp = EQ_MAX_ABS_GAIN_MDB;
        if (effective_preamp < -EQ_MAX_ABS_GAIN_MDB) effective_preamp = -EQ_MAX_ABS_GAIN_MDB;

        int hpf_enabled = (route == EQ_ROUTE_SPEAKER) ? 1 : g_control.hpf_enabled;

        eq_dsp_set_targets(&port->dsp, port->freq, band_mdB, effective_preamp, hpf_enabled);
        port->last_dirty = dirty;
        port->last_route = (uint8_t)route;
    }
}

static void update_status(uint32_t sample_rate, eq_route_t route, int eq_active, int smoothing) {
    g_status.sample_rate = sample_rate;
    g_status.route = (uint8_t)route;
    g_status.eq_active = (uint8_t)eq_active;
    g_status.smoothing = (uint8_t)smoothing;
    g_status.status_counter++;
}

static inline uint32_t get_port_sample_rate(int port) {
    if (port >= 0 && port < MAX_PORTS && g_ports[port].in_use) {
        return g_ports[port].freq;
    }
    return 44100;
}

static inline uint32_t get_port_len(int port) {
    if (port >= 0 && port < MAX_PORTS && g_ports[port].in_use) {
        return g_ports[port].len;
    }
    return 0;
}

static inline uint32_t get_port_channels(int port) {
    if (port >= 0 && port < MAX_PORTS && g_ports[port].in_use) {
        return g_ports[port].channels ? g_ports[port].channels : 2;
    }
    return 2;
}

static int sceAudioOutOutput_hook(int port, const void *buf) {
    uint32_t sample_rate = get_port_sample_rate(port);
    uint32_t channels = get_port_channels(port);
    uint32_t frames = get_port_len(port);
    if (frames == 0 || frames > SCRATCH_MAX_FRAMES) {
        frames = (frames == 0) ? 256 : SCRATCH_MAX_FRAMES;
    }

    eq_route_t route = detect_route();
    int eq_allowed = g_control.enabled && (!g_control.speaker_only || route == EQ_ROUTE_SPEAKER);

    int applied = 0;
    int clip_count = 0;
    int smoothing = 0;

    if (eq_allowed && buf && port >= 0 && port < MAX_PORTS) {
        size_t bytes = (size_t)frames * channels * sizeof(int16_t);
        if (bytes <= sizeof(g_scratch)) {
            // Use mutex to protect scratch buffer
            if (ksceKernelLockMutex(g_scratch_mutex, 1, NULL) >= 0) {
                if (ksceKernelCopyFromUser(g_scratch, buf, bytes) >= 0) {
                    port_state_t *p = &g_ports[port];
                    // Ensure DSP is initialized if it wasn't (e.g. port opened before plugin load)
                    if (p->dsp.sample_rate == 0) {
                        eq_dsp_init(&p->dsp, sample_rate);
                    }
                    
                    update_dsp_if_needed(p, route);
                    
                    int16_t peak_l = 0;
                    int16_t peak_r = 0;
                    eq_dsp_apply(&p->dsp, g_scratch, frames, channels, &clip_count, &peak_l, &peak_r);
                    
                    // Update global peak status
                    if (peak_l > g_status.peak_l) g_status.peak_l = peak_l;
                    if (peak_r > g_status.peak_r) g_status.peak_r = peak_r;
                    
                    smoothing = (p->dsp.smooth_remaining > 0);
                    
                    ksceKernelCopyToUser((void *)buf, g_scratch, bytes);
                    applied = 1;
                }
                ksceKernelUnlockMutex(g_scratch_mutex, 1);
            }
        }
    }

    if (clip_count > 0) {
        g_status.clip_events += clip_count;
    }

    // Debug counters
    g_status.debug_port = (uint32_t)port;
    g_status.debug_len = frames;
    g_status.debug_channels = channels;
    g_status.debug_run_count++;

    update_status(sample_rate, route, applied, smoothing);

    return TAI_CONTINUE(int, g_hook_output, port, buf);
}

static int sceAudioOutOpenPort_hook(int type, int len, int freq, int mode) {
    int port = TAI_CONTINUE(int, g_hook_open, type, len, freq, mode);
    if (port >= 0 && port < MAX_PORTS) {
        g_ports[port].in_use = 1;
        g_ports[port].len = (uint32_t)len;
        g_ports[port].freq = (uint32_t)freq;
        g_ports[port].channels = (mode == SCE_AUDIO_OUT_MODE_MONO) ? 1 : 2;
        eq_dsp_init(&g_ports[port].dsp, (uint32_t)freq);
        g_ports[port].last_dirty = 0; // Force update on first use
    }
    return port;
}

static int sceAudioOutSetConfig_hook(int port, SceSize len, int freq, int mode) {
    int res = TAI_CONTINUE(int, g_hook_set_config, port, len, freq, mode);
    if (port >= 0 && port < MAX_PORTS && res >= 0) {
        if (len > 0) { g_ports[port].len = (uint32_t)len; }
        if (freq > 0) { 
            g_ports[port].freq = (uint32_t)freq; 
        }
        if (mode >= 0) { g_ports[port].channels = (mode == SCE_AUDIO_OUT_MODE_MONO) ? 1 : 2; }
    }
    return res;
}

static int sceAudioOutReleasePort_hook(int port) {
    if (port >= 0 && port < MAX_PORTS) {
        memset(&g_ports[port], 0, sizeof(port_state_t));
    }
    return TAI_CONTINUE(int, g_hook_release, port);
}

void EqGetVersion(eq_version_t *out) {
    if (!out) { return; }
    eq_version_t v = {EQ_VERSION_MAJOR, EQ_VERSION_MINOR, EQ_VERSION_PATCH, 0};
    ksceKernelCopyToUser((void *)out, &v, sizeof(v));
}

int EqSetControl(const eq_control_t *user_ctrl) {
    if (!user_ctrl) { return -1; }
    eq_control_t tmp;
    if (ksceKernelCopyFromUser(&tmp, user_ctrl, sizeof(tmp)) < 0) {
        return -1;
    }
    g_control = tmp;
    // bump dirty to ensure DSP refresh even if app forgets
    g_control.dirty_counter++;
    return 0;
}

int EqGetStatus(eq_status_t *out_status) {
    if (!out_status) { return -1; }
    int res = ksceKernelCopyToUser(out_status, &g_status, sizeof(g_status));
    if (res >= 0) {
        // Reset peaks after reading so we get fresh max values next time
        g_status.peak_l = 0;
        g_status.peak_r = 0;
    }
    return res;
}

static void cleanup(void) {
    if (g_hook_id_output >= 0) { taiHookReleaseForKernel(g_hook_id_output, g_hook_output); }
    if (g_hook_id_open >= 0) { taiHookReleaseForKernel(g_hook_id_open, g_hook_open); }
    if (g_hook_id_set_config >= 0) { taiHookReleaseForKernel(g_hook_id_set_config, g_hook_set_config); }
    if (g_hook_id_release >= 0) { taiHookReleaseForKernel(g_hook_id_release, g_hook_release); }
    if (g_scratch_mutex >= 0) { ksceKernelDeleteMutex(g_scratch_mutex); }
}

int _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *argv) {
    (void)argc; (void)argv;

#if DIAG_MINIMAL
    // Minimal diagnostic: do nothing, just signal success.
    return SCE_KERNEL_START_SUCCESS;
#else
    dsp_reset_identity();
    // eq_dsp_init(&g_dsp, 44100); // Removed global init
    set_defaults();
    
    g_scratch_mutex = ksceKernelCreateMutex("eq_scratch_mutex", 0, 0, NULL);

    g_hook_id_output = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_output, "SceAudio", 0x438BB957, 0x02DB3F5F, sceAudioOutOutput_hook);
    g_hook_id_open = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_open, "SceAudio", 0x438BB957, 0x5BC341E4, sceAudioOutOpenPort_hook);
    g_hook_id_set_config = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_set_config, "SceAudio", 0x438BB957, 0xB8BA0D07, sceAudioOutSetConfig_hook);
    g_hook_id_release = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_release, "SceAudio", 0x438BB957, 0x69E2E6B5, sceAudioOutReleasePort_hook);

    if (g_hook_id_output < 0 || g_hook_id_open < 0 || g_hook_id_set_config < 0 || g_hook_id_release < 0) {
        cleanup();
        return SCE_KERNEL_START_FAILED;
    }

    return SCE_KERNEL_START_SUCCESS;
#endif
}

int module_stop(SceSize argc, const void *argv) {
    (void)argc; (void)argv;
    cleanup();
    return SCE_KERNEL_STOP_SUCCESS;
}
