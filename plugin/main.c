#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/cpu.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/kernel/threadmgr/misc.h>
#include <psp2kern/ctrl.h>
#include <taihen.h>
#include <psp2/audioout.h>
#include <string.h>

#include "dsp.h"
#include "port_registry.h"
#include "../common/eq_shared.h"

// Diagnostic build flag: when set to 1, skip all initialization.
#define DIAG_MINIMAL 0

#ifndef EQVITA_AUDIO_DIAGNOSTICS
#define EQVITA_AUDIO_DIAGNOSTICS 0
#endif

#define EQ_DIAG_ACTIVE_SAMPLE_INTERVAL 120u
#define EQ_WIRED_HEADPHONE_PROBE_INTERVAL 60u

typedef struct eq_diag_port_meta
{
    uint8_t valid;
    uint8_t port_type;
    uint8_t channels;
    uint8_t headroom_mode;
    uint8_t hpf_enabled;
    uint8_t reserved0;
    uint16_t reserved1;
    uint32_t generation;
    uint32_t len;
    uint32_t freq;
    int32_t effective_preamp_mdB;
    int32_t max_boost_mdB;
} eq_diag_port_meta_t;

static SceUID g_state_mutex = -1;
static SceUID g_audio_mutex = -1;

static eq_audio_port_registry_t g_ports;
static eq_control_t g_control;
static volatile uint32_t g_control_seq;
static eq_status_t g_status;
static eq_diag_snapshot_t g_diag_pending;
static uint32_t g_diag_ring_start;
static uint32_t g_diag_next_seq;
static uint32_t g_diag_dropped;
static uint32_t g_wired_headphone_probe_countdown;
static int g_cached_wired_headphones_connected;
static volatile int g_processing_enabled;
static volatile int g_active_callbacks;
static volatile int g_unloading;
static uint32_t g_route_hint_last_counter;

static tai_hook_ref_t g_hook_output;
static tai_hook_ref_t g_hook_open;
static tai_hook_ref_t g_hook_set_config;
static tai_hook_ref_t g_hook_get_config;
static tai_hook_ref_t g_hook_release;
static SceUID g_hook_id_output = -1;
static SceUID g_hook_id_open = -1;
static SceUID g_hook_id_set_config = -1;
static SceUID g_hook_id_get_config = -1;
static SceUID g_hook_id_release = -1;

static void dsp_reset_identity(void) {
    // Individual ports are reset on open.
}

static int lock_state(void) {
    if (g_state_mutex < 0) {
        return -1;
    }
    return ksceKernelLockMutex(g_state_mutex, 1, NULL);
}

static int try_lock_state(void) {
    if (g_state_mutex < 0) {
        return -1;
    }
    return ksceKernelTryLockMutex(g_state_mutex, 1);
}

static void unlock_state(void) {
    if (g_state_mutex >= 0) {
        ksceKernelUnlockMutex(g_state_mutex, 1);
    }
}

static int lock_audio(void) {
    if (g_audio_mutex < 0) {
        return -1;
    }
    return ksceKernelLockMutex(g_audio_mutex, 1, NULL);
}

static int try_lock_audio(void) {
    if (g_audio_mutex < 0) {
        return -1;
    }
    return ksceKernelTryLockMutex(g_audio_mutex, 1);
}

static void unlock_audio(void) {
    if (g_audio_mutex >= 0) {
        ksceKernelUnlockMutex(g_audio_mutex, 1);
    }
}

static int hook_enter(void) {
    __sync_add_and_fetch(&g_active_callbacks, 1);
    return !g_unloading;
}

static void hook_leave(void) {
    __sync_sub_and_fetch(&g_active_callbacks, 1);
}

static uint32_t current_audio_owner_id(void) {
    SceUID pid = ksceKernelGetProcessId();
    if (pid < 0) {
        return 0;
    }
    return (uint32_t)pid;
}

static void publish_control_locked(const eq_control_t *control) {
    if (!control) {
        return;
    }

    __sync_add_and_fetch(&g_control_seq, 1);
    __sync_synchronize();
    g_control = *control;
    __sync_synchronize();
    __sync_add_and_fetch(&g_control_seq, 1);
}

static int copy_control_snapshot(eq_control_t *out) {
    if (!out) {
        return -1;
    }

    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t before = g_control_seq;
        eq_control_t copy;
        __sync_synchronize();
        if (before & 1u) {
            continue;
        }
        copy = g_control;
        __sync_synchronize();
        if (before == g_control_seq && !(before & 1u) && eq_control_is_compatible(&copy)) {
            *out = copy;
            return 0;
        }
    }

    return -1;
}

static int should_probe_wired_headphone(const eq_control_t *control)
{
    uint8_t route_hint;

    if (!control) {
        return 1;
    }

    route_hint = control->route_hint;
    return route_hint == EQ_ROUTE_UNKNOWN ||
           route_hint == EQ_ROUTE_SPEAKER ||
           route_hint > EQ_ROUTE_BLUETOOTH;
}

static int should_run_wired_headphone_probe(const eq_control_t *control)
{
    if (!should_probe_wired_headphone(control)) {
        g_cached_wired_headphones_connected = 0;
        g_wired_headphone_probe_countdown = 0;
        return 0;
    }

    if (g_wired_headphone_probe_countdown > 0) {
        g_wired_headphone_probe_countdown--;
        return 0;
    }

    g_wired_headphone_probe_countdown = EQ_WIRED_HEADPHONE_PROBE_INTERVAL;
    return 1;
}

static eq_route_t detect_route(const eq_control_t *control, uint32_t last_counter, uint32_t stale_buffers) {
    int wired_headphones_connected = 0;

    uint8_t route_hint = control ? control->route_hint : EQ_ROUTE_UNKNOWN;
    uint32_t counter = control ? control->dirty_counter : 0;

    /*
     * The app can see AVConfig route information that this kernel plugin cannot.
     * Still prefer the kernel headphone bit so a persisted speaker hint cannot
     * keep applying speaker EQ after the user inserts wired headphones.
     */
    if (should_run_wired_headphone_probe(control)) {
        SceCtrlData data;
        memset(&data, 0, sizeof(data));
        g_cached_wired_headphones_connected =
            (ksceCtrlPeekBufferPositive(0, &data, 1) >= 0 && (data.buttons & SCE_CTRL_HEADPHONE)) ? 1 : 0;
    }
    wired_headphones_connected = g_cached_wired_headphones_connected;

    return (eq_route_t)eq_route_select(route_hint, counter, last_counter, stale_buffers, wired_headphones_connected);
}

static uint32_t diag_audio_budget_us(uint32_t frames, uint32_t sample_rate)
{
    if (frames == 0 || sample_rate == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)frames * 1000000ull) / sample_rate);
}

static int32_t diag_audio_margin_us(uint32_t budget_us, uint32_t total_us)
{
    if (budget_us >= total_us) {
        uint32_t margin = budget_us - total_us;
        return (margin > (uint32_t)INT32_MAX) ? INT32_MAX : (int32_t)margin;
    }

    uint32_t over = total_us - budget_us;
    return (over > (uint32_t)INT32_MAX) ? INT32_MIN : -(int32_t)over;
}

static void diag_emit_locked(const eq_diag_event_t *event)
{
    eq_diag_event_t *slot;
    uint32_t index;

    if (!event) {
        return;
    }

    if (g_diag_pending.count < EQ_DIAG_MAX_EVENTS_PER_DRAIN) {
        index = (g_diag_ring_start + g_diag_pending.count) % EQ_DIAG_MAX_EVENTS_PER_DRAIN;
        g_diag_pending.count++;
    } else {
        index = g_diag_ring_start;
        g_diag_ring_start = (g_diag_ring_start + 1u) % EQ_DIAG_MAX_EVENTS_PER_DRAIN;
        if (g_diag_dropped < UINT32_MAX) {
            g_diag_dropped++;
        }
    }

    slot = &g_diag_pending.events[index];
    *slot = *event;
    slot->version = EQ_DIAG_EVENT_VERSION;
    slot->seq = ++g_diag_next_seq;
}

#if EQVITA_AUDIO_DIAGNOSTICS
static uint16_t diag_abs_i16_peak(int16_t value)
{
    if (value == (int16_t)-32768) {
        return 32768u;
    }
    return (uint16_t)(value < 0 ? -value : value);
}

static void diag_measure_input_peak(const int16_t *pcm, uint32_t frames, uint32_t channels,
                                    uint16_t *peak_l, uint16_t *peak_r)
{
    uint16_t max_l = 0;
    uint16_t max_r = 0;

    if (!pcm || !peak_l || !peak_r || channels == 0 || channels > EQ_DSP_MAX_CHANNELS) {
        if (peak_l) *peak_l = 0;
        if (peak_r) *peak_r = 0;
        return;
    }

    for (uint32_t i = 0; i < frames; ++i) {
        uint16_t l = diag_abs_i16_peak(pcm[i * channels]);
        uint16_t r = (channels > 1) ? diag_abs_i16_peak(pcm[i * channels + 1u]) : l;
        if (l > max_l) max_l = l;
        if (r > max_r) max_r = r;
    }

    *peak_l = max_l;
    *peak_r = max_r;
}

static int diag_should_sample_active_block(eq_audio_tracked_port_t *tracked)
{
    if (!tracked) {
        return 0;
    }

    if (tracked->diag_active_sample_count < UINT32_MAX) {
        tracked->diag_active_sample_count++;
    }

    return tracked->diag_active_sample_count == 1u ||
           (tracked->diag_active_sample_count % EQ_DIAG_ACTIVE_SAMPLE_INTERVAL) == 0u;
}

static void diag_capture_port_meta(eq_diag_port_meta_t *meta, const eq_audio_tracked_port_t *tracked)
{
    if (!meta) {
        return;
    }

    memset(meta, 0, sizeof(*meta));
    if (!tracked || !tracked->config.in_use) {
        return;
    }

    meta->valid = 1;
    meta->generation = tracked->generation;
    meta->port_type = (uint8_t)tracked->config.type;
    meta->len = tracked->config.len;
    meta->freq = tracked->config.freq;
    meta->channels = tracked->config.channels;
    meta->effective_preamp_mdB = tracked->last_effective_preamp_mdB;
    meta->max_boost_mdB = tracked->last_max_boost_mdB;
    meta->headroom_mode = tracked->last_headroom_mode;
    meta->hpf_enabled = tracked->last_hpf_enabled;
}

static void diag_emit_output_locked(uint32_t type, int port, const eq_diag_port_meta_t *meta,
                                    const eq_control_t *control, eq_route_t route, eq_bypass_reason_t reason,
                                    uint32_t frames, uint32_t sample_rate, uint32_t channels, int ret,
                                    uint32_t elapsed_us, int32_t clip_count,
                                    uint16_t input_peak_l, uint16_t input_peak_r,
                                    uint16_t output_peak_l, uint16_t output_peak_r)
{
    eq_diag_event_t event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.port = port;
    event.ret = ret;
    event.len = frames;
    event.sample_rate = sample_rate;
    event.channels = (uint8_t)channels;
    event.route = (uint8_t)route;
    event.reason = (uint8_t)reason;
    event.elapsed_us = elapsed_us;
    event.budget_us = diag_audio_budget_us(frames, sample_rate);
    event.clip_count = clip_count;
    event.input_peak_l = input_peak_l;
    event.input_peak_r = input_peak_r;
    event.output_peak_l = output_peak_l;
    event.output_peak_r = output_peak_r;
    if (control) {
        event.dirty_counter = control->dirty_counter;
        event.preamp_mdB = control->preamp_mdB;
        event.headroom_mode = eq_control_get_headroom_mode(control);
        event.hpf_enabled = eq_control_hpf_enabled(control);
    }
    if (meta && meta->valid) {
        event.generation = meta->generation;
        event.port_type = meta->port_type;
        event.effective_preamp_mdB = meta->effective_preamp_mdB;
        event.max_boost_mdB = meta->max_boost_mdB;
        event.headroom_mode = meta->headroom_mode;
        event.hpf_enabled = meta->hpf_enabled;
    }
    diag_emit_locked(&event);
}
#endif

static void diag_emit_lifecycle_now(uint32_t type, int port, uint32_t generation, uint32_t port_type,
                                    uint32_t dirty_counter, uint32_t len, uint32_t freq,
                                    uint32_t channels, int ret)
{
    eq_diag_event_t event;

    memset(&event, 0, sizeof(event));
    event.type = type;
    event.port = port;
    event.ret = ret;
    event.len = len;
    event.sample_rate = freq;
    event.channels = (uint8_t)channels;
    event.budget_us = diag_audio_budget_us(len, freq);
    event.generation = generation;
    event.port_type = (uint8_t)port_type;
    event.dirty_counter = dirty_counter;
    if (try_lock_state() >= 0) {
        diag_emit_locked(&event);
        unlock_state();
    }
}

#include <psp2kern/io/fcntl.h>

#define BOOT_STATE_PATH "ur0:data/eqvita/boot.eqbs"
#define PRESET_PATH "ur0:data/eqvita/preset0.eqvp"
#define LEGACY_PRESET_PATH "ur0:data/eqvita/preset0.bin"

static int kernel_read_exact(SceUID fd, void *data, SceSize expected_size) {
    SceOff size;

    if (fd < 0 || !data || expected_size == 0) {
        return -1;
    }

    size = ksceIoLseek(fd, 0, SCE_SEEK_END);
    if (size < 0 || size != expected_size) {
        return -1;
    }
    if (ksceIoLseek(fd, 0, SCE_SEEK_SET) != 0) {
        return -1;
    }

    return (ksceIoRead(fd, data, expected_size) == (int)expected_size) ? 0 : -1;
}

static int load_boot_state_kernel(void) {
    SceUID fd = ksceIoOpen(BOOT_STATE_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        eq_boot_state_file_t state;
        eq_control_t loaded;
        int r = kernel_read_exact(fd, &state, sizeof(state));
        ksceIoClose(fd);
        if (r == 0 && eq_boot_state_extract_control(&state, &loaded) == 0) {
            g_control = loaded;
            g_control.dirty_counter = eq_control_next_dirty_counter(g_control.dirty_counter);
            return 0;
        }
    }

    return -1;
}

static void load_preset_kernel(void) {
    eq_preset_primary_status_t primary_status = EQ_PRESET_PRIMARY_MISSING;
    SceUID fd;

    if (load_boot_state_kernel() == 0) {
        return;
    }

    fd = ksceIoOpen(PRESET_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        eq_preset_file_t preset;
        eq_control_t loaded;
        int r = kernel_read_exact(fd, &preset, sizeof(preset));
        ksceIoClose(fd);
        if (r == 0 && eq_preset_extract_control(&preset, &loaded) == 0) {
            primary_status = EQ_PRESET_PRIMARY_VALID;
            eq_control_prepare_for_boot(&loaded);
            g_control = loaded;
            g_control.dirty_counter = eq_control_next_dirty_counter(g_control.dirty_counter);
            return;
        }
        primary_status = EQ_PRESET_PRIMARY_INVALID;
    }

    if (!eq_preset_should_try_legacy(primary_status)) {
        return;
    }

    fd = ksceIoOpen(LEGACY_PRESET_PATH, SCE_O_RDONLY, 0);
    if (fd >= 0) {
        eq_control_t tmp;
        int r = kernel_read_exact(fd, &tmp, sizeof(tmp));
        ksceIoClose(fd);
        if (r == 0 && eq_control_validate(&tmp) == 0) {
            eq_control_prepare_for_boot(&tmp);
            g_control = tmp;
            g_control.dirty_counter = eq_control_next_dirty_counter(g_control.dirty_counter);
        }
    }
}

static void set_defaults(void) {
    memset(&g_status, 0, sizeof(g_status));
    memset(&g_diag_pending, 0, sizeof(g_diag_pending));
    eq_control_init_defaults(&g_control);
    
    // Try to load preset
    load_preset_kernel();

    g_status.sample_rate = 48000;
    g_status.route = EQ_ROUTE_UNKNOWN;
    g_status.eq_active = 0;
    g_status.bypass_reason = EQ_BYPASS_DISABLED;
    g_diag_pending.version = EQ_DIAG_SNAPSHOT_VERSION;
    g_diag_pending.capacity = EQ_DIAG_MAX_EVENTS_PER_DRAIN;
    g_diag_ring_start = 0;
    g_diag_next_seq = 0;
    g_diag_dropped = 0;
    g_wired_headphone_probe_countdown = 0;
    g_cached_wired_headphones_connected = 0;
    g_control_seq = 2;
}

// No shared mem block needed; app uses syscalls to set/get control and status.

static int update_dsp_if_needed(eq_audio_tracked_port_t *port, const eq_control_t *control, eq_route_t route) {
    if (!port || !port->config.in_use || !control) return 0;
    
    uint32_t dirty = control->dirty_counter;
    if (dirty != port->last_dirty || port->config.freq != port->dsp.sample_rate || port->last_route != (uint8_t)route) {
        int32_t band_mdB[EQ_BANDS];
        for (int i = 0; i < EQ_BANDS; ++i) {
            band_mdB[i] = control->band_gain_mdB[i];
        }

        int hpf_enabled = eq_control_hpf_enabled(control);

        if (route == EQ_ROUTE_SPEAKER && hpf_enabled) {
            // Do not excite the 31 Hz band on speakers; fold it into 62 Hz and zero it out
            int32_t merged = band_mdB[1] + band_mdB[0];
            if (merged > EQ_MAX_ABS_GAIN_MDB) merged = EQ_MAX_ABS_GAIN_MDB;
            if (merged < -EQ_MAX_ABS_GAIN_MDB) merged = -EQ_MAX_ABS_GAIN_MDB;
            band_mdB[1] = merged;
            band_mdB[0] = 0;
        }

        int32_t effective_preamp = eq_control_effective_preamp_mdB(control, band_mdB);

        eq_dsp_set_targets(&port->dsp, port->config.freq, band_mdB, effective_preamp, hpf_enabled);
        port->last_dirty = dirty;
        port->last_route = (uint8_t)route;
        port->last_preamp_mdB = control->preamp_mdB;
        port->last_effective_preamp_mdB = effective_preamp;
        port->last_max_boost_mdB = eq_control_max_positive_band_mdB(band_mdB);
        port->last_headroom_mode = eq_control_get_headroom_mode(control);
        port->last_hpf_enabled = (uint8_t)hpf_enabled;
        return 1;
    }
    return 0;
}

static void update_status(uint32_t sample_rate, eq_route_t route, int eq_active, int smoothing, eq_bypass_reason_t reason) {
    g_status.sample_rate = sample_rate;
    g_status.route = (uint8_t)route;
    g_status.eq_active = (uint8_t)eq_active;
    g_status.smoothing = (uint8_t)smoothing;
    g_status.bypass_reason = (uint8_t)reason;
    eq_status_increment_u32(&g_status.status_counter);
}

static int recover_port_config_after_output(int port) {
    uint32_t owner_id = current_audio_owner_id();
    int len;
    int freq;
    int mode;
    int res;

    if (port < 0 || g_hook_id_get_config < 0) {
        return -1;
    }

    len = TAI_CONTINUE(int, g_hook_get_config, port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN);
    freq = TAI_CONTINUE(int, g_hook_get_config, port, SCE_AUDIO_OUT_CONFIG_TYPE_FREQ);
    mode = TAI_CONTINUE(int, g_hook_get_config, port, SCE_AUDIO_OUT_CONFIG_TYPE_MODE);

    if (len <= 0 || freq <= 0 || mode < 0) {
        return -1;
    }

    if (try_lock_audio() < 0) {
        return -1;
    }
    res = eq_audio_port_registry_recover_config_owned(&g_ports, owner_id, port, eq_audio_port_type_for_recovered_id(port), (uint32_t)len, (uint32_t)freq, mode) ? 0 : -1;
    unlock_audio();
    return res;
}

static int sceAudioOutGetConfig_hook(int port, int type) {
    int res;
    if (!hook_enter()) {
        res = TAI_CONTINUE(int, g_hook_get_config, port, type);
        hook_leave();
        return res;
    }

    res = TAI_CONTINUE(int, g_hook_get_config, port, type);
    hook_leave();
    return res;
}

static int sceAudioOutOutput_hook(int port, const void *buf) {
    uint32_t sample_rate = 48000;
    uint32_t channels = 0;
    uint32_t frames = 0;
    uint32_t active_ports = 0;
    uint32_t start_us = 0;
    uint32_t elapsed_us = 0;
    uint32_t budget_us = 0;
    uint32_t total_us = 0;
    uint32_t stage_start_us = 0;
    uint32_t stage_control_us = 0;
    uint32_t stage_registry_us = 0;
    uint32_t stage_route_us = 0;
    uint32_t stage_copy_in_us = 0;
    uint32_t stage_retarget_us = 0;
    uint32_t stage_dsp_us = 0;
    uint32_t stage_copy_out_us = 0;
    uint32_t stage_original_us = 0;
    uint32_t stage_status_us = 0;
    int32_t margin_us = 0;
    eq_route_t route = EQ_ROUTE_UNKNOWN;
    eq_bypass_reason_t reason = EQ_BYPASS_INVALID_PORT;
    int applied = 0;
    int clip_count = 0;
    int smoothing = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;
    eq_control_t control;
    eq_audio_tracked_port_t *processing_port = NULL;
    eq_audio_port_config_t processing_config;
    uint32_t owner_id = 0;
#if EQVITA_AUDIO_DIAGNOSTICS
    eq_diag_port_meta_t diag_port_meta;
#endif
    uint32_t route_last_counter = 0;
    uint32_t route_stale_buffers = 0;
    int control_ready = 0;
    int recover_after_output = 0;
    int retry_bypass = 0;
#if EQVITA_AUDIO_DIAGNOSTICS
    int retargeted = 0;
#endif
    int restore_failed = 0;
    int output_frame_mismatch = 0;
    int ret;
    size_t processing_bytes = 0;

    if (!hook_enter()) {
        ret = TAI_CONTINUE(int, g_hook_output, port, buf);
        hook_leave();
        return ret;
    }

    if (!g_processing_enabled || !buf) {
        ret = TAI_CONTINUE(int, g_hook_output, port, buf);
        hook_leave();
        return ret;
    }

    start_us = ksceKernelGetSystemTimeLow();
    owner_id = current_audio_owner_id();

    memset(&control, 0, sizeof(control));
#if EQVITA_AUDIO_DIAGNOSTICS
    memset(&diag_port_meta, 0, sizeof(diag_port_meta));
#endif
    stage_start_us = ksceKernelGetSystemTimeLow();
    control_ready = (copy_control_snapshot(&control) == 0);
    stage_control_us = ksceKernelGetSystemTimeLow() - stage_start_us;

    if (!control_ready) {
        reason = EQ_BYPASS_AUDIO_BUSY;
    }

    stage_start_us = ksceKernelGetSystemTimeLow();
    if (try_lock_audio() >= 0) {
        eq_audio_tracked_port_t *p;

        route_last_counter = g_route_hint_last_counter;
        if (control_ready && control.dirty_counter != g_route_hint_last_counter) {
            g_route_hint_last_counter = control.dirty_counter;
            g_wired_headphone_probe_countdown = 0;
        }

        eq_audio_port_registry_drain_completed(&g_ports);
        p = eq_audio_port_registry_find_owned(&g_ports, owner_id, port);
        if (!p) {
            recover_after_output = 1;
        }

        if (p && p->processing) {
#if EQVITA_AUDIO_DIAGNOSTICS
            diag_capture_port_meta(&diag_port_meta, p);
#endif
            frames = p->config.len;
            sample_rate = p->config.freq;
            channels = p->config.channels;
            reason = EQ_BYPASS_AUDIO_BUSY;
        } else if (p && p->config.in_use) {
            processing_port = eq_audio_port_registry_begin_processing_owned(&g_ports, owner_id, port);
            if (processing_port) {
                processing_config = processing_port->config;
#if EQVITA_AUDIO_DIAGNOSTICS
                diag_capture_port_meta(&diag_port_meta, processing_port);
#endif
            }
        }

        active_ports = eq_audio_port_registry_count(&g_ports);
        unlock_audio();
    } else {
        reason = EQ_BYPASS_AUDIO_BUSY;
    }
    stage_registry_us = ksceKernelGetSystemTimeLow() - stage_start_us;

    if (processing_port) {
        sample_rate = processing_config.freq;
        channels = processing_config.channels;
        frames = processing_config.len;
        retry_bypass = eq_audio_tracked_port_consume_retry_bypass(processing_port, buf);

        if (retry_bypass) {
            reason = EQ_BYPASS_AUDIO_BUSY;
        } else {
            if (control_ready) {
                processing_port->control_cache = control;
                processing_port->control_cache_valid = 1;
            } else if (processing_port->control_cache_valid &&
                       eq_control_is_compatible(&processing_port->control_cache)) {
                control = processing_port->control_cache;
                control_ready = 1;
                route_last_counter = control.dirty_counter;
                route_stale_buffers = 0;
            }

            if (!control_ready) {
                reason = EQ_BYPASS_AUDIO_BUSY;
            } else if (!eq_audio_port_can_process(&processing_config, EQ_AUDIO_SCRATCH_MAX_FRAMES)) {
                reason = (frames > EQ_AUDIO_SCRATCH_MAX_FRAMES) ? EQ_BYPASS_BUFFER_TOO_LARGE : EQ_BYPASS_UNSUPPORTED_FORMAT;
            } else if (!control.enabled) {
                reason = EQ_BYPASS_DISABLED;
            } else {
                processing_bytes = (size_t)frames * channels * sizeof(int16_t);

                stage_start_us = ksceKernelGetSystemTimeLow();
                route = detect_route(&control, route_last_counter, route_stale_buffers);
                stage_route_us = ksceKernelGetSystemTimeLow() - stage_start_us;
                if (route == EQ_ROUTE_UNKNOWN) {
                    reason = EQ_BYPASS_UNKNOWN_ROUTE;
                } else if (control.speaker_only && route != EQ_ROUTE_SPEAKER) {
                    reason = EQ_BYPASS_SPEAKER_ONLY;
                } else {
                    reason = EQ_BYPASS_NONE;
                    stage_start_us = ksceKernelGetSystemTimeLow();
                    if (ksceKernelCopyFromUser(processing_port->original, buf, processing_bytes) >= 0) {
                        stage_copy_in_us = ksceKernelGetSystemTimeLow() - stage_start_us;
                        if (processing_port->dsp.sample_rate == 0) {
                            eq_dsp_init(&processing_port->dsp, sample_rate);
                        }

                        stage_start_us = ksceKernelGetSystemTimeLow();
#if EQVITA_AUDIO_DIAGNOSTICS
                        retargeted = update_dsp_if_needed(processing_port, &control, route);
#else
                        (void)update_dsp_if_needed(processing_port, &control, route);
#endif
                        stage_retarget_us = ksceKernelGetSystemTimeLow() - stage_start_us;
#if EQVITA_AUDIO_DIAGNOSTICS
                        diag_capture_port_meta(&diag_port_meta, processing_port);
#endif

                        stage_start_us = ksceKernelGetSystemTimeLow();
                        eq_dsp_apply_to(&processing_port->dsp, processing_port->original, processing_port->scratch, frames, channels, &clip_count, &peak_l, &peak_r);
                        stage_dsp_us = ksceKernelGetSystemTimeLow() - stage_start_us;

                        smoothing = (processing_port->dsp.smooth_remaining > 0);

                        stage_start_us = ksceKernelGetSystemTimeLow();
                        if (ksceKernelCopyToUser((void *)buf, processing_port->scratch, processing_bytes) >= 0) {
                            stage_copy_out_us = ksceKernelGetSystemTimeLow() - stage_start_us;
                            applied = 1;
                        } else {
                            stage_copy_out_us = ksceKernelGetSystemTimeLow() - stage_start_us;
                            if (ksceKernelCopyToUser((void *)buf, processing_port->original, processing_bytes) < 0) {
                                restore_failed = 1;
                            }
                            eq_audio_tracked_port_reset_dsp_state(processing_port);
                            smoothing = 0;
                            reason = EQ_BYPASS_COPY_FAILED;
                        }
                    } else {
                        stage_copy_in_us = ksceKernelGetSystemTimeLow() - stage_start_us;
                        reason = EQ_BYPASS_COPY_FAILED;
                    }
                }
            }
        }
    }

    if (start_us != 0) {
        elapsed_us = ksceKernelGetSystemTimeLow() - start_us;
    }

    stage_start_us = ksceKernelGetSystemTimeLow();
    ret = TAI_CONTINUE(int, g_hook_output, port, buf);
    stage_original_us = ksceKernelGetSystemTimeLow() - stage_start_us;
    if (ret > 0 && frames > 0 && (uint32_t)ret != frames) {
        output_frame_mismatch = 1;
    }
    if (((ret < 0 && retry_bypass) || restore_failed) && buf && processing_port) {
        eq_audio_tracked_port_note_output_error(processing_port, buf);
    }
    if (ret < 0) {
        applied = 0;
        smoothing = 0;
        reason = EQ_BYPASS_AUDIO_BUSY;
        if (processing_port) {
            eq_audio_tracked_port_reset_dsp_state(processing_port);
        }
    }
    if (processing_port) {
        eq_audio_port_registry_mark_processing_complete(processing_port);
        if (try_lock_audio() >= 0) {
            eq_audio_port_registry_drain_completed(&g_ports);
            active_ports = eq_audio_port_registry_count(&g_ports);
            unlock_audio();
        }
    }
    stage_start_us = ksceKernelGetSystemTimeLow();
    if (try_lock_state() >= 0) {
#if EQVITA_AUDIO_DIAGNOSTICS
        uint32_t diag_type = EQ_DIAG_EVENT_NONE;
        uint16_t input_peak_l = 0;
        uint16_t input_peak_r = 0;
#endif

        if (applied) {
            if (peak_l > g_status.peak_l) g_status.peak_l = peak_l;
            if (peak_r > g_status.peak_r) g_status.peak_r = peak_r;
            if (clip_count > 0) {
                eq_status_add_clip_events(&g_status, clip_count);
            }
        }

#if EQVITA_AUDIO_DIAGNOSTICS
        if (ret < 0) {
            diag_type = EQ_DIAG_EVENT_OUTPUT_ERROR;
        } else if (output_frame_mismatch) {
            diag_type = EQ_DIAG_EVENT_CONFIG_MISMATCH;
        } else if (reason == EQ_BYPASS_COPY_FAILED) {
            diag_type = EQ_DIAG_EVENT_COPY_ERROR;
        } else if (applied && clip_count > 0) {
            diag_type = EQ_DIAG_EVENT_CLIP_BLOCK;
        } else if (elapsed_us > 0 && diag_audio_budget_us(frames, sample_rate) > 0 &&
                   elapsed_us > diag_audio_budget_us(frames, sample_rate)) {
            diag_type = EQ_DIAG_EVENT_SLOW_BLOCK;
        } else if (reason != EQ_BYPASS_NONE &&
                   reason != EQ_BYPASS_DISABLED &&
                   reason != EQ_BYPASS_SPEAKER_ONLY) {
            diag_type = EQ_DIAG_EVENT_BYPASS_BLOCK;
        } else if (retargeted) {
            diag_type = EQ_DIAG_EVENT_DSP_RETARGET;
        } else if (applied && diag_should_sample_active_block(processing_port)) {
            diag_type = EQ_DIAG_EVENT_ACTIVE_SAMPLE;
        }

        if (diag_type != EQ_DIAG_EVENT_NONE) {
            if (processing_port && frames > 0 && channels > 0) {
                diag_measure_input_peak(processing_port->original, frames, channels, &input_peak_l, &input_peak_r);
            }
            diag_emit_output_locked(diag_type, port, &diag_port_meta, control_ready ? &control : NULL,
                route, reason, frames, sample_rate, channels, ret, elapsed_us, clip_count,
                input_peak_l, input_peak_r, peak_l, peak_r);
        }
#endif

        g_status.debug_port = (uint32_t)port;
        g_status.debug_len = frames;
        g_status.debug_channels = channels;
        eq_status_increment_u32(&g_status.debug_run_count);
        g_status.debug_active_ports = active_ports;
        g_status.debug_last_us = elapsed_us;
        stage_status_us = ksceKernelGetSystemTimeLow() - stage_start_us;
        budget_us = diag_audio_budget_us(frames, sample_rate);
        total_us = elapsed_us + stage_original_us + stage_status_us;
        margin_us = budget_us ? diag_audio_margin_us(budget_us, total_us) : 0;
        g_status.debug_last_total_us = total_us;
        g_status.debug_last_budget_us = budget_us;
        g_status.debug_last_margin_us = margin_us;
        if (total_us > g_status.debug_max_total_us) {
            g_status.debug_max_total_us = total_us;
        }
        if (stage_dsp_us > g_status.debug_max_dsp_us) {
            g_status.debug_max_dsp_us = stage_dsp_us;
        }
        if (budget_us > 0 &&
            (g_status.debug_min_margin_budget_us == 0 || margin_us < g_status.debug_min_margin_us)) {
            g_status.debug_min_margin_us = margin_us;
            g_status.debug_min_margin_port = (uint32_t)port;
            g_status.debug_min_margin_len = frames;
            g_status.debug_min_margin_sample_rate = sample_rate;
            g_status.debug_min_margin_channels = channels;
            g_status.debug_min_margin_budget_us = budget_us;
            g_status.debug_min_margin_total_us = total_us;
            g_status.debug_min_margin_route = route;
            g_status.debug_min_margin_bypass_reason = reason;
            g_status.debug_min_margin_stage_dsp_us = stage_dsp_us;
            g_status.debug_min_margin_stage_original_us = stage_original_us;
            g_status.debug_min_margin_stage_status_us = stage_status_us;
        }
        if (budget_us > 0 && frames == 256 &&
            (g_status.debug_min_margin_len_256_us == 0 || margin_us < g_status.debug_min_margin_len_256_us)) {
            g_status.debug_min_margin_len_256_us = margin_us;
        } else if (budget_us > 0 && frames == 1024 &&
            (g_status.debug_min_margin_len_1024_us == 0 || margin_us < g_status.debug_min_margin_len_1024_us)) {
            g_status.debug_min_margin_len_1024_us = margin_us;
        } else if (budget_us > 0 && frames == 2048 &&
            (g_status.debug_min_margin_len_2048_us == 0 || margin_us < g_status.debug_min_margin_len_2048_us)) {
            g_status.debug_min_margin_len_2048_us = margin_us;
        }
        if (elapsed_us > g_status.debug_max_us) {
            g_status.debug_max_us = elapsed_us;
            g_status.debug_max_port = (uint32_t)port;
            g_status.debug_max_len = frames;
            g_status.debug_max_sample_rate = sample_rate;
            g_status.debug_max_channels = channels;
            g_status.debug_max_budget_us = budget_us;
            g_status.debug_max_route = route;
            g_status.debug_max_bypass_reason = reason;
            g_status.debug_max_clip_count = clip_count;
            g_status.debug_max_stage_control_us = stage_control_us;
            g_status.debug_max_stage_registry_us = stage_registry_us;
            g_status.debug_max_stage_route_us = stage_route_us;
            g_status.debug_max_stage_copy_in_us = stage_copy_in_us;
            g_status.debug_max_stage_retarget_us = stage_retarget_us;
            g_status.debug_max_stage_dsp_us = stage_dsp_us;
            g_status.debug_max_stage_copy_out_us = stage_copy_out_us;
            g_status.debug_max_stage_original_us = stage_original_us;
            g_status.debug_max_stage_status_us = stage_status_us;
        }
        if (reason == EQ_BYPASS_AUDIO_BUSY) {
            eq_status_increment_u32(&g_status.debug_busy_bypass_count);
        } else if (reason == EQ_BYPASS_INVALID_PORT) {
            eq_status_increment_u32(&g_status.debug_unknown_port_count);
        }

        update_status(sample_rate, route, applied, smoothing, reason);
        unlock_state();
    }
    if ((recover_after_output || output_frame_mismatch) && ret >= 0 && g_processing_enabled) {
        (void)recover_port_config_after_output(port);
    }
    hook_leave();
    return ret;
}

static int sceAudioOutOpenPort_hook(int type, int len, int freq, int mode) {
    uint32_t generation = 0;
    uint32_t dirty_counter = 0;
    uint32_t channels = 0;
    uint32_t owner_id = 0;
    if (!hook_enter()) {
        int port = TAI_CONTINUE(int, g_hook_open, type, len, freq, mode);
        hook_leave();
        return port;
    }
    owner_id = current_audio_owner_id();
    int port = TAI_CONTINUE(int, g_hook_open, type, len, freq, mode);
    channels = (uint32_t)(eq_audio_mode_to_channels(mode) > 0 ? eq_audio_mode_to_channels(mode) : 0);
    if (g_processing_enabled && port >= 0) {
        if (lock_audio() >= 0) {
            eq_audio_tracked_port_t *tracked = eq_audio_port_registry_open_owned(&g_ports, owner_id, port, (uint32_t)type, (uint32_t)len, (uint32_t)freq, mode);
            if (tracked) {
                generation = tracked->generation;
                dirty_counter = tracked->last_dirty;
            }
            unlock_audio();
        }
        diag_emit_lifecycle_now(EQ_DIAG_EVENT_PORT_OPEN, port, generation, (uint32_t)type,
            dirty_counter, (uint32_t)len, (uint32_t)freq, channels, port);
    }
    hook_leave();
    return port;
}

static int sceAudioOutSetConfig_hook(int port, SceSize len, int freq, int mode) {
    uint32_t generation = 0;
    uint32_t port_type = 0;
    uint32_t dirty_counter = 0;
    uint32_t final_len = (len == (SceSize)-1) ? 0 : (uint32_t)len;
    uint32_t final_freq = (freq < 0) ? 0 : (uint32_t)freq;
    uint32_t final_channels = (mode < 0 || eq_audio_mode_to_channels(mode) < 0) ? 0 : (uint32_t)eq_audio_mode_to_channels(mode);
    uint32_t owner_id = 0;
    if (!hook_enter()) {
        int res = TAI_CONTINUE(int, g_hook_set_config, port, len, freq, mode);
        hook_leave();
        return res;
    }
    owner_id = current_audio_owner_id();
    int res = TAI_CONTINUE(int, g_hook_set_config, port, len, freq, mode);
    if (g_processing_enabled && port >= 0 && res >= 0) {
        if (lock_audio() >= 0) {
            uint32_t len_arg = (len == (SceSize)-1) ? EQ_AUDIO_KEEP_U32 : (uint32_t)len;
            (void)eq_audio_port_registry_set_config_owned(&g_ports, owner_id, port, len_arg, freq, mode);
            {
                eq_audio_tracked_port_t *tracked = eq_audio_port_registry_find_owned(&g_ports, owner_id, port);
                if (tracked) {
                    generation = tracked->generation;
                    port_type = tracked->config.type;
                    dirty_counter = tracked->last_dirty;
                    final_len = tracked->config.len;
                    final_freq = tracked->config.freq;
                    final_channels = tracked->config.channels;
                }
            }
            unlock_audio();
        }
        diag_emit_lifecycle_now(EQ_DIAG_EVENT_PORT_SET_CONFIG, port, generation, port_type,
            dirty_counter, final_len, final_freq, final_channels, res);
    }
    hook_leave();
    return res;
}

static int sceAudioOutReleasePort_hook(int port) {
    uint32_t generation = 0;
    uint32_t port_type = 0;
    uint32_t dirty_counter = 0;
    uint32_t len = 0;
    uint32_t freq = 0;
    uint32_t channels = 0;
    uint32_t owner_id = 0;
    if (!hook_enter()) {
        int res = TAI_CONTINUE(int, g_hook_release, port);
        hook_leave();
        return res;
    }
    owner_id = current_audio_owner_id();
    int res = TAI_CONTINUE(int, g_hook_release, port);
    if (g_processing_enabled && port >= 0 && res >= 0) {
        if (lock_audio() >= 0) {
            eq_audio_tracked_port_t *tracked = eq_audio_port_registry_find_owned(&g_ports, owner_id, port);
            if (tracked) {
                generation = tracked->generation;
                port_type = tracked->config.type;
                dirty_counter = tracked->last_dirty;
                len = tracked->config.len;
                freq = tracked->config.freq;
                channels = tracked->config.channels;
            }
            (void)eq_audio_port_registry_release_owned(&g_ports, owner_id, port);
            unlock_audio();
        }
        diag_emit_lifecycle_now(EQ_DIAG_EVENT_PORT_RELEASE, port, generation, port_type,
            dirty_counter, len, freq, channels, res);
    }
    hook_leave();
    return res;
}

void EqGetVersion(eq_version_t *out) {
    if (!out) { return; }
    if (!hook_enter()) {
        hook_leave();
        return;
    }
    eq_version_t v = {EQ_VERSION_MAJOR, EQ_VERSION_MINOR, EQ_VERSION_PATCH, 0};
    ksceKernelCopyToUser((void *)out, &v, sizeof(v));
    hook_leave();
}

int EqSetControl(const eq_control_t *user_ctrl) {
    if (!user_ctrl) { return -1; }
    if (!hook_enter()) {
        hook_leave();
        return -3;
    }
    eq_control_t tmp;
    if (ksceKernelCopyFromUser(&tmp, user_ctrl, sizeof(tmp)) < 0) {
        hook_leave();
        return -1;
    }
    if (eq_control_validate(&tmp) < 0) {
        hook_leave();
        return -2;
    }
    if (lock_state() < 0) {
        hook_leave();
        return -3;
    }
    tmp.dirty_counter = eq_control_next_dirty_counter(g_control.dirty_counter);
    publish_control_locked(&tmp);
    {
        eq_diag_event_t event;
        memset(&event, 0, sizeof(event));
        event.type = EQ_DIAG_EVENT_CONTROL_SET;
        event.dirty_counter = tmp.dirty_counter;
        event.route = tmp.route_hint;
        event.headroom_mode = eq_control_get_headroom_mode(&tmp);
        event.hpf_enabled = eq_control_hpf_enabled(&tmp);
        event.preamp_mdB = tmp.preamp_mdB;
        event.max_boost_mdB = eq_control_max_positive_band_mdB(tmp.band_gain_mdB);
        event.effective_preamp_mdB = eq_control_effective_preamp_mdB(&tmp, tmp.band_gain_mdB);
        diag_emit_locked(&event);
    }
    unlock_state();
    hook_leave();
    return 0;
}

int EqGetStatus(eq_status_t *out_status) {
    if (!out_status) { return -1; }
    if (!hook_enter()) {
        hook_leave();
        return -2;
    }
    eq_status_t tmp;
    int copy_res;
    if (lock_state() < 0) {
        hook_leave();
        return -2;
    }
    tmp = g_status;
    copy_res = ksceKernelCopyToUser(out_status, &tmp, sizeof(tmp));
    if (copy_res >= 0) {
        // Reset peaks after a successful read so the next UI frame reports fresh max values.
        g_status.peak_l = 0;
        g_status.peak_r = 0;
    }
    unlock_state();
    hook_leave();
    return copy_res;
}

int EqDrainDiagnostics(eq_diag_snapshot_t *out_snapshot) {
    eq_diag_snapshot_t tmp;
    int copy_res;

    if (!out_snapshot) { return -1; }
    if (!hook_enter()) {
        hook_leave();
        return -2;
    }
    if (lock_state() < 0) {
        hook_leave();
        return -2;
    }

    memset(&tmp, 0, sizeof(tmp));
    tmp.version = EQ_DIAG_SNAPSHOT_VERSION;
    tmp.capacity = EQ_DIAG_MAX_EVENTS_PER_DRAIN;
    tmp.count = g_diag_pending.count;
    tmp.dropped = g_diag_dropped;
    for (uint32_t i = 0; i < tmp.count && i < EQ_DIAG_MAX_EVENTS_PER_DRAIN; ++i) {
        uint32_t index = (g_diag_ring_start + i) % EQ_DIAG_MAX_EVENTS_PER_DRAIN;
        tmp.events[i] = g_diag_pending.events[index];
    }

    copy_res = ksceKernelCopyToUser(out_snapshot, &tmp, sizeof(tmp));
    if (copy_res >= 0) {
        memset(&g_diag_pending, 0, sizeof(g_diag_pending));
        g_diag_pending.version = EQ_DIAG_SNAPSHOT_VERSION;
        g_diag_pending.capacity = EQ_DIAG_MAX_EVENTS_PER_DRAIN;
        g_diag_ring_start = 0;
        g_diag_dropped = 0;
    }

    unlock_state();
    hook_leave();
    return copy_res;
}

static int wait_for_hooks_to_idle(void) {
    for (int i = 0; i < 100; ++i) {
        if (g_active_callbacks <= 0) {
            return 0;
        }
        ksceKernelDelayThread(1000);
    }
    return (g_active_callbacks <= 0) ? 0 : -1;
}

static int release_hook_if_installed(SceUID *hook_id, tai_hook_ref_t hook_ref) {
    if (!hook_id || *hook_id < 0) {
        return 0;
    }

    int res = taiHookReleaseForKernel(*hook_id, hook_ref);
    if (res < 0) {
        return res;
    }

    *hook_id = -1;
    return 0;
}

static int cleanup(void) {
    g_processing_enabled = 0;
    g_unloading = 1;

    if (wait_for_hooks_to_idle() < 0) {
        g_unloading = 0;
        return -1;
    }

    int release_res = 0;
    if (release_hook_if_installed(&g_hook_id_output, g_hook_output) < 0) { release_res = -1; }
    if (release_hook_if_installed(&g_hook_id_open, g_hook_open) < 0) { release_res = -1; }
    if (release_hook_if_installed(&g_hook_id_set_config, g_hook_set_config) < 0) { release_res = -1; }
    if (release_hook_if_installed(&g_hook_id_get_config, g_hook_get_config) < 0) { release_res = -1; }
    if (release_hook_if_installed(&g_hook_id_release, g_hook_release) < 0) { release_res = -1; }
    if (release_res < 0) {
        g_unloading = 0;
        return -1;
    }

    if (wait_for_hooks_to_idle() < 0) {
        g_unloading = 0;
        return -1;
    }

    if (g_audio_mutex >= 0) { ksceKernelDeleteMutex(g_audio_mutex); g_audio_mutex = -1; }
    if (g_state_mutex >= 0) { ksceKernelDeleteMutex(g_state_mutex); g_state_mutex = -1; }
    return 0;
}

int _start() __attribute__((weak, alias("module_start")));
int module_start(SceSize argc, const void *argv) {
    (void)argc; (void)argv;

#if DIAG_MINIMAL
    // Minimal diagnostic: do nothing, just signal success.
    return SCE_KERNEL_START_SUCCESS;
#else
    dsp_reset_identity();
    set_defaults();
    eq_audio_port_registry_init(&g_ports);
    
    g_processing_enabled = 0;
    g_active_callbacks = 0;
    g_unloading = 0;
    g_route_hint_last_counter = 0;
    g_state_mutex = ksceKernelCreateMutex("eq_state_mutex", 0, 0, NULL);
    if (g_state_mutex < 0) {
        return SCE_KERNEL_START_FAILED;
    }
    g_audio_mutex = ksceKernelCreateMutex("eq_audio_mutex", 0, 0, NULL);
    if (g_audio_mutex < 0) {
        (void)cleanup();
        return SCE_KERNEL_START_FAILED;
    }

    g_hook_id_output = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_output, "SceAudio", 0x438BB957, 0x02DB3F5F, sceAudioOutOutput_hook);
    g_hook_id_open = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_open, "SceAudio", 0x438BB957, 0x5BC341E4, sceAudioOutOpenPort_hook);
    g_hook_id_set_config = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_set_config, "SceAudio", 0x438BB957, 0xB8BA0D07, sceAudioOutSetConfig_hook);
    g_hook_id_get_config = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_get_config, "SceAudio", 0x438BB957, 0x9C8EDAEA, sceAudioOutGetConfig_hook);
    g_hook_id_release = taiHookFunctionExportForKernel(KERNEL_PID, &g_hook_release, "SceAudio", 0x438BB957, 0x69E2E6B5, sceAudioOutReleasePort_hook);

    if (g_hook_id_output < 0 || g_hook_id_open < 0 || g_hook_id_set_config < 0 || g_hook_id_get_config < 0 || g_hook_id_release < 0) {
        (void)cleanup();
        return SCE_KERNEL_START_FAILED;
    }

    g_processing_enabled = 1;
    return SCE_KERNEL_START_SUCCESS;
#endif
}

int module_stop(SceSize argc, const void *argv) {
    (void)argc; (void)argv;
    return (cleanup() == 0) ? SCE_KERNEL_STOP_SUCCESS : SCE_KERNEL_STOP_FAIL;
}
