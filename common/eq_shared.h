#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define EQ_VERSION_MAJOR 1
#define EQ_VERSION_MINOR 15
#define EQ_VERSION_PATCH 0

#define EQ_BANDS 10
#define EQ_PARAMETRIC_FILTERS 48
#define EQ_MAX_ABS_GAIN_MDB 12000
#define EQ_PARAMETRIC_MAX_ABS_GAIN_MDB 24000
#define EQ_PREAMP_MIN_MDB -30000
#define EQ_PREAMP_MAX_MDB 12000
#define EQ_DEFAULT_PREAMP_MDB -6000
#define EQ_PARAMETRIC_MIN_FREQUENCY_MHZ 1000u
#define EQ_PARAMETRIC_MAX_FREQUENCY_MHZ 24000000u
#define EQ_PARAMETRIC_MIN_Q_UQ 10000u
#define EQ_PARAMETRIC_MAX_Q_UQ 100000000u
#define EQ_PARAMETRIC_DEFAULT_SHELF_S_UQ 900000u
#define EQ_COPY_COEFFICIENT_SCALE 8000
#define EQ_COPY_MAX_ABS_COEFFICIENT 32000
#define EQ_CHANNEL_LEFT_MASK 0x01u
#define EQ_CHANNEL_RIGHT_MASK 0x02u
#define EQ_CHANNEL_STEREO_MASK (EQ_CHANNEL_LEFT_MASK | EQ_CHANNEL_RIGHT_MASK)
#define EQ_SMOOTH_SAMPLES 512
#define EQ_DSP_MAX_CHANNELS 2

#define EQ_VERSION_PACK(major, minor) ((((uint32_t)(major)) << 16) | ((uint32_t)(minor)))
#define EQ_ABI_VERSION EQ_VERSION_PACK(EQ_VERSION_MAJOR, EQ_VERSION_MINOR)
#define EQ_LEGACY_ABI_VERSION_1_10 EQ_VERSION_PACK(1, 10)
#define EQ_LEGACY_ABI_VERSION_1_11 EQ_VERSION_PACK(1, 11)
#define EQ_LEGACY_ABI_VERSION_1_12 EQ_VERSION_PACK(1, 12)
#define EQ_LEGACY_ABI_VERSION_1_13 EQ_VERSION_PACK(1, 13)
#define EQ_LEGACY_ABI_VERSION_1_14 EQ_VERSION_PACK(1, 14)
#define EQ_LEGACY_CONTROL_SIZE 60u
#define EQ_LEGACY_SHARED_BLOCK_SIZE 264u
#define EQ_CONTROL_SIZE_V1_15 832u

#define EQ_ROUTE_PROFILE_COUNT 3u
#define EQ_ROUTE_PROFILE_BANK_VERSION 1u
#define EQ_ROUTE_PROFILE_FILE_MAGIC 0x46525045u /* EPRF */
#define EQ_ROUTE_PROFILE_FILE_VERSION 1u
#define EQ_ROUTE_PROFILE_SOURCE_NAME_MAX 128u
#define EQ_ROUTE_PROFILE_FILE_NAME "output-peq.eqpf"

#define EQ_PRESET_MAGIC 0x50565145u /* EQVP */
#define EQ_PRESET_VERSION 3u
#define EQ_LEGACY_PRESET_VERSION 2u
#define EQ_PRESET_BAND_COUNT EQ_BANDS
#define EQ_BOOT_STATE_MAGIC 0x53425145u /* EQBS */
#define EQ_BOOT_STATE_VERSION 2u
#define EQ_LEGACY_BOOT_STATE_VERSION 1u
/* Route hints are persistent advisory state; app route truth is not always live at boot. */
#define EQ_ROUTE_HINT_MAX_STALE_BUFFERS UINT32_MAX
#define EQ_HPF_ENABLED_MASK 0x01u
#define EQ_HEADROOM_MODE_SHIFT 4u
#define EQ_HEADROOM_MODE_MASK 0x03u
#define EQ_DIAG_EVENT_VERSION 1u
#define EQ_DIAG_SNAPSHOT_VERSION 1u
#define EQ_DIAG_MAX_EVENTS_PER_DRAIN 32u

static const uint32_t eq_band_frequencies[EQ_BANDS] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

typedef enum eq_route
{
    EQ_ROUTE_UNKNOWN = 0,
    EQ_ROUTE_SPEAKER = 1,
    EQ_ROUTE_HEADPHONES = 2,
    EQ_ROUTE_BLUETOOTH = 3
} eq_route_t;

typedef enum eq_bypass_reason
{
    EQ_BYPASS_NONE = 0,
    EQ_BYPASS_DISABLED = 1,
    EQ_BYPASS_SPEAKER_ONLY = 2,
    EQ_BYPASS_UNKNOWN_ROUTE = 3,
    EQ_BYPASS_INVALID_PORT = 4,
    EQ_BYPASS_BUFFER_TOO_LARGE = 5,
    EQ_BYPASS_COPY_FAILED = 6,
    EQ_BYPASS_UNSUPPORTED_FORMAT = 7,
    EQ_BYPASS_AUDIO_BUSY = 8,
    EQ_BYPASS_NO_ROUTE_PROFILE = 9
} eq_bypass_reason_t;

typedef enum eq_preset_primary_status
{
    EQ_PRESET_PRIMARY_MISSING = 0,
    EQ_PRESET_PRIMARY_VALID = 1,
    EQ_PRESET_PRIMARY_INVALID = 2
} eq_preset_primary_status_t;

typedef enum eq_headroom_mode
{
    EQ_HEADROOM_SAFE = 0,
    EQ_HEADROOM_LOUD = 1,
    EQ_HEADROOM_RAW = 2,
    EQ_HEADROOM_EXACT = 3
} eq_headroom_mode_t;

typedef enum eq_mode
{
    EQ_MODE_GRAPHIC = 0,
    EQ_MODE_PARAMETRIC = 1
} eq_mode_t;

typedef enum eq_filter_type
{
    EQ_FILTER_NONE = 0,
    EQ_FILTER_PEAK = 1,
    EQ_FILTER_LOW_SHELF = 2,
    EQ_FILTER_HIGH_SHELF = 3,
    EQ_FILTER_COPY = 4
} eq_filter_type_t;

typedef enum eq_filter_shape
{
    EQ_FILTER_SHAPE_Q = 0,
    EQ_FILTER_SHAPE_S = 1
} eq_filter_shape_t;

typedef enum eq_filter_frequency_mode
{
    EQ_FILTER_FREQUENCY_CENTER = 0,
    EQ_FILTER_FREQUENCY_CORNER = 1
} eq_filter_frequency_mode_t;

typedef enum eq_diag_event_type
{
    EQ_DIAG_EVENT_NONE = 0,
    EQ_DIAG_EVENT_CLIP_BLOCK = 1,
    EQ_DIAG_EVENT_BYPASS_BLOCK = 2,
    EQ_DIAG_EVENT_SLOW_BLOCK = 3,
    EQ_DIAG_EVENT_OUTPUT_ERROR = 4,
    EQ_DIAG_EVENT_COPY_ERROR = 5,
    EQ_DIAG_EVENT_DSP_RETARGET = 6,
    EQ_DIAG_EVENT_PORT_OPEN = 7,
    EQ_DIAG_EVENT_PORT_SET_CONFIG = 8,
    EQ_DIAG_EVENT_PORT_RELEASE = 9,
    EQ_DIAG_EVENT_CONTROL_SET = 10,
    EQ_DIAG_EVENT_DROPPED_EVENTS = 11,
    EQ_DIAG_EVENT_ACTIVE_SAMPLE = 12,
    EQ_DIAG_EVENT_CONFIG_MISMATCH = 13
} eq_diag_event_type_t;

typedef struct eq_version
{
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;
} eq_version_t;

typedef struct eq_parametric_filter_data
{
    uint32_t frequency_mHz;
    int32_t gain_mdB;
    uint32_t q_uQ;
} eq_parametric_filter_data_t;

typedef struct eq_copy_operation_data
{
    int16_t matrix[4];
    uint32_t reserved;
} eq_copy_operation_data_t;

typedef union eq_parametric_operation_data
{
    eq_parametric_filter_data_t filter;
    eq_copy_operation_data_t copy;
} eq_parametric_operation_data_t;

typedef struct eq_parametric_filter
{
    eq_parametric_operation_data_t data;
    uint8_t type;
    uint8_t channel_mask;
    uint8_t shape;
    uint8_t frequency_mode;
} eq_parametric_filter_t;

/* Exact on-disk layout used by EQVita 1.10 through 1.14. */
typedef struct eq_legacy_control_v1_14
{
    uint32_t version;
    uint32_t size;
    volatile uint32_t dirty_counter;
    uint8_t enabled;
    uint8_t speaker_only;
    uint8_t hpf_enabled;
    uint8_t route_hint;
    int32_t preamp_mdB;
    int32_t band_gain_mdB[EQ_BANDS];
} eq_legacy_control_v1_14_t;

typedef struct eq_control
{
    uint32_t version;
    uint32_t size;
    volatile uint32_t dirty_counter;
    uint8_t enabled;
    uint8_t speaker_only;
    uint8_t hpf_enabled;
    uint8_t route_hint;
    int32_t preamp_mdB;
    int32_t band_gain_mdB[EQ_BANDS];
    uint8_t eq_mode;
    uint8_t parametric_filter_count;
    uint16_t reserved;
    eq_parametric_filter_t parametric_filters[EQ_PARAMETRIC_FILTERS];
} eq_control_t;

/*
 * Optional per-output controls. A non-zero enabled_mask enables route-profile
 * mode: an assigned route uses its own control and an unassigned route is
 * bypassed. The ordinary eq_control_t remains the global on/off switch and the
 * backwards-compatible fallback while the mask is zero.
 */
typedef struct eq_route_profile_bank
{
    uint32_t version;
    uint32_t size;
    volatile uint32_t dirty_counter;
    uint8_t enabled_mask;
    uint8_t selected_route;
    uint16_t reserved;
    eq_control_t profiles[EQ_ROUTE_PROFILE_COUNT];
} eq_route_profile_bank_t;

typedef char eq_parametric_filter_layout_must_be_16_bytes[
    sizeof(eq_parametric_filter_t) == 16u ? 1 : -1];
typedef char eq_legacy_control_layout_must_be_60_bytes[
    sizeof(eq_legacy_control_v1_14_t) == EQ_LEGACY_CONTROL_SIZE ? 1 : -1];
typedef char eq_control_v1_15_layout_must_be_832_bytes[
    sizeof(eq_control_t) == EQ_CONTROL_SIZE_V1_15 ? 1 : -1];

typedef struct eq_status
{
    volatile uint32_t status_counter;
    uint32_t sample_rate;
    uint8_t route;
    uint8_t eq_active;
    uint8_t smoothing;
    uint8_t bypass_reason;
    int32_t clip_events;
    uint16_t peak_l;
    uint16_t peak_r;
    uint32_t debug_port;
    uint32_t debug_len;
    uint32_t debug_channels;
    uint32_t debug_run_count;
    uint32_t debug_active_ports;
    uint32_t debug_busy_bypass_count;
    uint32_t debug_unknown_port_count;
    uint32_t debug_last_us;
    uint32_t debug_max_us;
    uint32_t debug_max_port;
    uint32_t debug_max_len;
    uint32_t debug_max_sample_rate;
    uint32_t debug_max_channels;
    uint32_t debug_max_budget_us;
    uint32_t debug_max_route;
    uint32_t debug_max_bypass_reason;
    int32_t debug_max_clip_count;
    uint32_t debug_max_stage_control_us;
    uint32_t debug_max_stage_registry_us;
    uint32_t debug_max_stage_route_us;
    uint32_t debug_max_stage_copy_in_us;
    uint32_t debug_max_stage_retarget_us;
    uint32_t debug_max_stage_dsp_us;
    uint32_t debug_max_stage_copy_out_us;
    uint32_t debug_max_stage_original_us;
    uint32_t debug_max_stage_status_us;
    uint32_t debug_last_total_us;
    uint32_t debug_last_budget_us;
    int32_t debug_last_margin_us;
    uint32_t debug_max_total_us;
    uint32_t debug_max_dsp_us;
    int32_t debug_min_margin_us;
    uint32_t debug_min_margin_port;
    uint32_t debug_min_margin_len;
    uint32_t debug_min_margin_sample_rate;
    uint32_t debug_min_margin_channels;
    uint32_t debug_min_margin_budget_us;
    uint32_t debug_min_margin_total_us;
    uint32_t debug_min_margin_route;
    uint32_t debug_min_margin_bypass_reason;
    uint32_t debug_min_margin_stage_dsp_us;
    uint32_t debug_min_margin_stage_original_us;
    uint32_t debug_min_margin_stage_status_us;
    int32_t debug_min_margin_len_256_us;
    int32_t debug_min_margin_len_1024_us;
    int32_t debug_min_margin_len_2048_us;
} eq_status_t;

typedef struct eq_diag_event
{
    uint32_t version;
    uint32_t seq;
    uint32_t type;
    int32_t port;
    uint32_t generation;
    uint32_t dirty_counter;
    uint32_t len;
    uint32_t sample_rate;
    uint32_t elapsed_us;
    uint32_t budget_us;
    int32_t ret;
    int32_t clip_count;
    int32_t preamp_mdB;
    int32_t effective_preamp_mdB;
    int32_t max_boost_mdB;
    uint16_t input_peak_l;
    uint16_t input_peak_r;
    uint16_t output_peak_l;
    uint16_t output_peak_r;
    uint8_t channels;
    uint8_t route;
    uint8_t reason;
    uint8_t port_type;
    uint8_t headroom_mode;
    uint8_t hpf_enabled;
    uint8_t flags;
    uint8_t reserved0;
} eq_diag_event_t;

typedef struct eq_diag_snapshot
{
    uint32_t version;
    uint32_t count;
    uint32_t dropped;
    uint32_t capacity;
    eq_diag_event_t events[EQ_DIAG_MAX_EVENTS_PER_DRAIN];
} eq_diag_snapshot_t;

typedef struct eq_shared_block
{
    eq_control_t control;
    eq_status_t status;
} eq_shared_block_t;

typedef struct eq_preset_file
{
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t payload_size;
    uint32_t band_count;
    uint32_t checksum;
    eq_control_t control;
} eq_preset_file_t;

typedef struct eq_boot_state_file
{
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t payload_size;
    uint32_t checksum;
    eq_control_t control;
} eq_boot_state_file_t;

typedef struct eq_route_profile_file
{
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t payload_size;
    uint32_t checksum;
    eq_route_profile_bank_t bank;
    char source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX];
} eq_route_profile_file_t;

typedef struct eq_legacy_preset_file_v2
{
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t payload_size;
    uint32_t band_count;
    uint32_t checksum;
    eq_legacy_control_v1_14_t control;
} eq_legacy_preset_file_v2_t;

typedef struct eq_legacy_boot_state_file_v1
{
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t payload_size;
    uint32_t checksum;
    eq_legacy_control_v1_14_t control;
} eq_legacy_boot_state_file_v1_t;

static inline int32_t eq_clamp_mdB(int32_t value)
{
    if (value > EQ_MAX_ABS_GAIN_MDB) {
        return EQ_MAX_ABS_GAIN_MDB;
    }
    if (value < -EQ_MAX_ABS_GAIN_MDB) {
        return -EQ_MAX_ABS_GAIN_MDB;
    }
    return value;
}

static inline int32_t eq_clamp_parametric_gain_mdB(int32_t value)
{
    if (value > EQ_PARAMETRIC_MAX_ABS_GAIN_MDB) {
        return EQ_PARAMETRIC_MAX_ABS_GAIN_MDB;
    }
    if (value < -EQ_PARAMETRIC_MAX_ABS_GAIN_MDB) {
        return -EQ_PARAMETRIC_MAX_ABS_GAIN_MDB;
    }
    return value;
}

static inline int32_t eq_clamp_preamp_mdB(int32_t value)
{
    if (value > EQ_PREAMP_MAX_MDB) {
        return EQ_PREAMP_MAX_MDB;
    }
    if (value < EQ_PREAMP_MIN_MDB) {
        return EQ_PREAMP_MIN_MDB;
    }
    return value;
}

static inline uint32_t eq_clamp_filter_frequency_mHz(uint32_t value)
{
    if (value > EQ_PARAMETRIC_MAX_FREQUENCY_MHZ) {
        return EQ_PARAMETRIC_MAX_FREQUENCY_MHZ;
    }
    if (value < EQ_PARAMETRIC_MIN_FREQUENCY_MHZ) {
        return EQ_PARAMETRIC_MIN_FREQUENCY_MHZ;
    }
    return value;
}

static inline uint32_t eq_clamp_filter_q_uQ(uint32_t value)
{
    if (value > EQ_PARAMETRIC_MAX_Q_UQ) {
        return EQ_PARAMETRIC_MAX_Q_UQ;
    }
    if (value < EQ_PARAMETRIC_MIN_Q_UQ) {
        return EQ_PARAMETRIC_MIN_Q_UQ;
    }
    return value;
}

static inline uint8_t eq_bool(uint8_t value)
{
    return value ? 1u : 0u;
}

static inline uint32_t eq_control_next_dirty_counter(uint32_t current)
{
    uint32_t next = current + 1u;
    return next ? next : 1u;
}

static inline void eq_status_add_clip_events(eq_status_t *status, int32_t count)
{
    if (!status || count <= 0) {
        return;
    }

    if (status->clip_events > INT32_MAX - count) {
        status->clip_events = INT32_MAX;
    } else {
        status->clip_events += count;
    }
}

static inline void eq_status_increment_u32(volatile uint32_t *counter)
{
    if (!counter) {
        return;
    }

    if (*counter < UINT32_MAX) {
        (*counter)++;
    }
}

static inline uint8_t eq_control_hpf_enabled(const eq_control_t *ctrl)
{
    return ctrl ? (ctrl->hpf_enabled & EQ_HPF_ENABLED_MASK) : 0u;
}

static inline uint8_t eq_control_get_headroom_mode(const eq_control_t *ctrl)
{
    if (!ctrl) {
        return EQ_HEADROOM_SAFE;
    }
    return (uint8_t)((ctrl->hpf_enabled >> EQ_HEADROOM_MODE_SHIFT) & EQ_HEADROOM_MODE_MASK);
}

static inline void eq_control_set_hpf_enabled(eq_control_t *ctrl, uint8_t enabled)
{
    uint8_t mode;
    if (!ctrl) {
        return;
    }
    mode = eq_control_get_headroom_mode(ctrl);
    ctrl->hpf_enabled = (eq_bool(enabled) & EQ_HPF_ENABLED_MASK) |
        (uint8_t)(mode << EQ_HEADROOM_MODE_SHIFT);
}

static inline void eq_control_set_headroom_mode(eq_control_t *ctrl, uint8_t mode)
{
    uint8_t hpf;
    if (!ctrl) {
        return;
    }
    if (mode > EQ_HEADROOM_EXACT) {
        mode = EQ_HEADROOM_SAFE;
    }
    hpf = eq_control_hpf_enabled(ctrl);
    ctrl->hpf_enabled = (hpf & EQ_HPF_ENABLED_MASK) |
        (uint8_t)(mode << EQ_HEADROOM_MODE_SHIFT);
}

static inline int32_t eq_control_max_positive_band_mdB(const int32_t *band_mdB)
{
    int32_t max_boost = 0;

    if (!band_mdB) {
        return 0;
    }

    for (int i = 0; i < EQ_BANDS; ++i) {
        int32_t gain = eq_clamp_mdB(band_mdB[i]);
        if (gain > max_boost) {
            max_boost = gain;
        }
    }

    return max_boost;
}

static inline int32_t eq_control_max_positive_parametric_mdB(const eq_control_t *ctrl)
{
    int32_t max_boost = 0;
    uint8_t count;

    if (!ctrl || ctrl->eq_mode != EQ_MODE_PARAMETRIC) {
        return 0;
    }

    count = ctrl->parametric_filter_count;
    if (count > EQ_PARAMETRIC_FILTERS) {
        count = EQ_PARAMETRIC_FILTERS;
    }
    for (uint8_t i = 0; i < count; ++i) {
        const eq_parametric_filter_t *filter = &ctrl->parametric_filters[i];
        int32_t gain;
        if (!(filter->channel_mask & EQ_CHANNEL_STEREO_MASK) ||
            filter->type < EQ_FILTER_PEAK || filter->type > EQ_FILTER_HIGH_SHELF) {
            continue;
        }
        gain = eq_clamp_parametric_gain_mdB(filter->data.filter.gain_mdB);
        if (gain > max_boost) {
            max_boost = gain;
        }
    }

    return max_boost;
}

static inline int32_t eq_control_max_positive_eq_mdB(const eq_control_t *ctrl, const int32_t *graphic_band_mdB)
{
    if (ctrl && ctrl->eq_mode == EQ_MODE_PARAMETRIC) {
        return eq_control_max_positive_parametric_mdB(ctrl);
    }
    return eq_control_max_positive_band_mdB(graphic_band_mdB);
}

static inline int32_t eq_control_effective_preamp_mdB(const eq_control_t *ctrl, const int32_t *band_mdB)
{
    int32_t effective_preamp;
    int32_t max_boost;
    uint8_t headroom_mode;

    if (!ctrl) {
        return EQ_DEFAULT_PREAMP_MDB;
    }

    effective_preamp = eq_clamp_preamp_mdB(ctrl->preamp_mdB);
    max_boost = eq_control_max_positive_eq_mdB(ctrl, band_mdB);
    headroom_mode = eq_control_get_headroom_mode(ctrl);

    if (headroom_mode == EQ_HEADROOM_LOUD) {
        int32_t makeup = max_boost / 2;
        if (makeup > 3000) {
            makeup = 3000;
        }
        effective_preamp += makeup;
    } else if (headroom_mode == EQ_HEADROOM_RAW) {
        effective_preamp = 0;
    } else if (headroom_mode == EQ_HEADROOM_SAFE) {
        int32_t safe_preamp = -max_boost;
        if (effective_preamp > safe_preamp) {
            effective_preamp = safe_preamp;
        }
    }

    return eq_clamp_preamp_mdB(effective_preamp);
}

static inline void eq_control_set_graphic_mode(eq_control_t *ctrl)
{
    if (!ctrl) {
        return;
    }
    ctrl->eq_mode = EQ_MODE_GRAPHIC;
    ctrl->parametric_filter_count = 0;
    ctrl->reserved = 0;
    memset(ctrl->parametric_filters, 0, sizeof(ctrl->parametric_filters));
}

static inline void eq_control_set_parametric_mode(eq_control_t *ctrl, uint8_t filter_count)
{
    if (!ctrl) {
        return;
    }
    ctrl->eq_mode = EQ_MODE_PARAMETRIC;
    ctrl->parametric_filter_count = filter_count > EQ_PARAMETRIC_FILTERS ? EQ_PARAMETRIC_FILTERS : filter_count;
    ctrl->reserved = 0;
    memset(ctrl->band_gain_mdB, 0, sizeof(ctrl->band_gain_mdB));
    eq_control_set_hpf_enabled(ctrl, 0);
    eq_control_set_headroom_mode(ctrl, EQ_HEADROOM_EXACT);
}

static inline void eq_control_init_defaults(eq_control_t *ctrl)
{
    if (!ctrl) {
        return;
    }

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->version = EQ_ABI_VERSION;
    ctrl->size = (uint32_t)sizeof(eq_control_t);
    ctrl->enabled = 0;
    ctrl->speaker_only = 1;
    ctrl->hpf_enabled = 1;
    eq_control_set_headroom_mode(ctrl, EQ_HEADROOM_SAFE);
    ctrl->preamp_mdB = EQ_DEFAULT_PREAMP_MDB;
    eq_control_set_graphic_mode(ctrl);
}

static inline int eq_control_is_compatible(const eq_control_t *ctrl)
{
    if (!ctrl) {
        return 0;
    }

    return ctrl->version == EQ_ABI_VERSION && ctrl->size == sizeof(eq_control_t);
}

static inline int eq_control_validate(eq_control_t *ctrl)
{
    if (!eq_control_is_compatible(ctrl)) {
        return -1;
    }

    ctrl->version = EQ_ABI_VERSION;
    ctrl->size = (uint32_t)sizeof(eq_control_t);
    ctrl->enabled = eq_bool(ctrl->enabled);
    ctrl->speaker_only = eq_bool(ctrl->speaker_only);
    {
        uint8_t hpf = eq_bool(ctrl->hpf_enabled & EQ_HPF_ENABLED_MASK);
        uint8_t mode = eq_control_get_headroom_mode(ctrl);
        if (mode > EQ_HEADROOM_EXACT) {
            mode = EQ_HEADROOM_SAFE;
        }
        ctrl->hpf_enabled = 0;
        eq_control_set_hpf_enabled(ctrl, hpf);
        eq_control_set_headroom_mode(ctrl, mode);
    }
    if (ctrl->route_hint > EQ_ROUTE_BLUETOOTH) {
        ctrl->route_hint = EQ_ROUTE_UNKNOWN;
    }
    ctrl->preamp_mdB = eq_clamp_preamp_mdB(ctrl->preamp_mdB);

    for (int i = 0; i < EQ_BANDS; ++i) {
        ctrl->band_gain_mdB[i] = eq_clamp_mdB(ctrl->band_gain_mdB[i]);
    }

    ctrl->reserved = 0;
    if (ctrl->eq_mode != EQ_MODE_PARAMETRIC) {
        eq_control_set_graphic_mode(ctrl);
        return 0;
    }

    /* Imported PEQ is an exclusive signal path: never layer graphic bands,
       Bass guard, or an automatic headroom mode over the APO operations. */
    memset(ctrl->band_gain_mdB, 0, sizeof(ctrl->band_gain_mdB));
    eq_control_set_hpf_enabled(ctrl, 0);
    eq_control_set_headroom_mode(ctrl, EQ_HEADROOM_EXACT);

    if (ctrl->parametric_filter_count > EQ_PARAMETRIC_FILTERS) {
        ctrl->parametric_filter_count = EQ_PARAMETRIC_FILTERS;
    }
    for (uint8_t i = 0; i < ctrl->parametric_filter_count; ++i) {
        eq_parametric_filter_t *filter = &ctrl->parametric_filters[i];
        filter->channel_mask &= EQ_CHANNEL_STEREO_MASK;
        if (filter->type < EQ_FILTER_PEAK || filter->type > EQ_FILTER_COPY) {
            memset(filter, 0, sizeof(*filter));
            continue;
        }
        if (filter->type == EQ_FILTER_COPY) {
            filter->channel_mask = EQ_CHANNEL_STEREO_MASK;
            filter->shape = EQ_FILTER_SHAPE_Q;
            filter->frequency_mode = EQ_FILTER_FREQUENCY_CENTER;
            for (int coefficient = 0; coefficient < 4; ++coefficient) {
                if (filter->data.copy.matrix[coefficient] > EQ_COPY_MAX_ABS_COEFFICIENT) {
                    filter->data.copy.matrix[coefficient] = EQ_COPY_MAX_ABS_COEFFICIENT;
                } else if (filter->data.copy.matrix[coefficient] < -EQ_COPY_MAX_ABS_COEFFICIENT) {
                    filter->data.copy.matrix[coefficient] = -EQ_COPY_MAX_ABS_COEFFICIENT;
                }
            }
            filter->data.copy.reserved = 0;
            continue;
        }
        filter->data.filter.frequency_mHz = eq_clamp_filter_frequency_mHz(filter->data.filter.frequency_mHz);
        filter->data.filter.gain_mdB = eq_clamp_parametric_gain_mdB(filter->data.filter.gain_mdB);
        filter->data.filter.q_uQ = eq_clamp_filter_q_uQ(filter->data.filter.q_uQ);
        if (filter->shape > EQ_FILTER_SHAPE_S) {
            filter->shape = EQ_FILTER_SHAPE_Q;
        }
        if (filter->frequency_mode > EQ_FILTER_FREQUENCY_CORNER) {
            filter->frequency_mode = EQ_FILTER_FREQUENCY_CENTER;
        }
        if (filter->type == EQ_FILTER_PEAK) {
            filter->shape = EQ_FILTER_SHAPE_Q;
            filter->frequency_mode = EQ_FILTER_FREQUENCY_CENTER;
        }
    }
    for (uint8_t i = ctrl->parametric_filter_count; i < EQ_PARAMETRIC_FILTERS; ++i) {
        memset(&ctrl->parametric_filters[i], 0, sizeof(ctrl->parametric_filters[i]));
    }

    return 0;
}

static inline int eq_route_profile_index(uint8_t route)
{
    if (route < EQ_ROUTE_SPEAKER || route > EQ_ROUTE_BLUETOOTH) {
        return -1;
    }
    return (int)route - (int)EQ_ROUTE_SPEAKER;
}

static inline uint8_t eq_route_profile_bit(uint8_t route)
{
    int index = eq_route_profile_index(route);
    return index < 0 ? 0u : (uint8_t)(1u << (unsigned int)index);
}

static inline int eq_route_profile_bank_is_compatible(const eq_route_profile_bank_t *bank)
{
    return bank &&
        bank->version == EQ_ROUTE_PROFILE_BANK_VERSION &&
        bank->size == sizeof(eq_route_profile_bank_t);
}

static inline void eq_route_profile_bank_init(eq_route_profile_bank_t *bank)
{
    if (!bank) {
        return;
    }

    memset(bank, 0, sizeof(*bank));
    bank->version = EQ_ROUTE_PROFILE_BANK_VERSION;
    bank->size = (uint32_t)sizeof(*bank);
    bank->dirty_counter = 1;
    bank->selected_route = EQ_ROUTE_SPEAKER;
    for (unsigned int i = 0; i < EQ_ROUTE_PROFILE_COUNT; ++i) {
        eq_control_init_defaults(&bank->profiles[i]);
        bank->profiles[i].enabled = 1;
        bank->profiles[i].speaker_only = 0;
        bank->profiles[i].route_hint = EQ_ROUTE_UNKNOWN;
        bank->profiles[i].dirty_counter = 0;
    }
}

static inline int eq_route_profile_bank_validate(eq_route_profile_bank_t *bank)
{
    if (!eq_route_profile_bank_is_compatible(bank)) {
        return -1;
    }

    bank->enabled_mask &= (uint8_t)((1u << EQ_ROUTE_PROFILE_COUNT) - 1u);
    if (eq_route_profile_index(bank->selected_route) < 0) {
        bank->selected_route = EQ_ROUTE_SPEAKER;
    }
    bank->reserved = 0;
    if (bank->dirty_counter == 0) {
        bank->dirty_counter = 1;
    }

    for (unsigned int i = 0; i < EQ_ROUTE_PROFILE_COUNT; ++i) {
        eq_control_t *profile = &bank->profiles[i];
        if (eq_control_validate(profile) < 0) {
            return -1;
        }
        /* Profile assignment is the enable switch; these fields cannot make
           an explicitly assigned wired/Bluetooth profile act speaker-only. */
        profile->enabled = 1;
        profile->speaker_only = 0;
        profile->route_hint = EQ_ROUTE_UNKNOWN;
        profile->dirty_counter = 0;
    }
    return 0;
}

static inline int eq_route_profile_bank_has_route(const eq_route_profile_bank_t *bank, uint8_t route)
{
    uint8_t bit = eq_route_profile_bit(route);
    return bank && bit && (bank->enabled_mask & bit) != 0;
}

static inline void eq_route_profile_bank_touch(eq_route_profile_bank_t *bank)
{
    if (bank) {
        bank->dirty_counter = eq_control_next_dirty_counter(bank->dirty_counter);
    }
}

static inline int eq_legacy_control_is_compatible(const eq_legacy_control_v1_14_t *legacy)
{
    if (!legacy) {
        return 0;
    }
    if (legacy->version != EQ_LEGACY_ABI_VERSION_1_10 &&
        legacy->version != EQ_LEGACY_ABI_VERSION_1_11 &&
        legacy->version != EQ_LEGACY_ABI_VERSION_1_12 &&
        legacy->version != EQ_LEGACY_ABI_VERSION_1_13 &&
        legacy->version != EQ_LEGACY_ABI_VERSION_1_14) {
        return 0;
    }
    return legacy->size == EQ_LEGACY_CONTROL_SIZE ||
           legacy->size == EQ_LEGACY_SHARED_BLOCK_SIZE;
}

static inline int eq_control_import_legacy(eq_control_t *out, const eq_legacy_control_v1_14_t *legacy)
{
    uint8_t hpf;
    uint8_t mode = EQ_HEADROOM_SAFE;

    if (!out || !eq_legacy_control_is_compatible(legacy)) {
        return -1;
    }

    eq_control_init_defaults(out);
    out->dirty_counter = legacy->dirty_counter;
    out->enabled = eq_bool(legacy->enabled);
    out->speaker_only = eq_bool(legacy->speaker_only);
    hpf = eq_bool(legacy->hpf_enabled & EQ_HPF_ENABLED_MASK);
    if (legacy->version == EQ_LEGACY_ABI_VERSION_1_14) {
        mode = (uint8_t)((legacy->hpf_enabled >> EQ_HEADROOM_MODE_SHIFT) & EQ_HEADROOM_MODE_MASK);
        if (mode > EQ_HEADROOM_RAW) {
            mode = EQ_HEADROOM_SAFE;
        }
        if (legacy->route_hint <= EQ_ROUTE_BLUETOOTH) {
            out->route_hint = legacy->route_hint;
        }
    }
    eq_control_set_hpf_enabled(out, hpf);
    eq_control_set_headroom_mode(out, mode);
    out->preamp_mdB = eq_clamp_mdB(legacy->preamp_mdB);
    for (int i = 0; i < EQ_BANDS; ++i) {
        out->band_gain_mdB[i] = eq_clamp_mdB(legacy->band_gain_mdB[i]);
    }
    eq_control_set_graphic_mode(out);
    return 0;
}

static inline int eq_legacy_control_is_canonical(const eq_legacy_control_v1_14_t *legacy)
{
    uint8_t allowed_hpf;

    if (!eq_legacy_control_is_compatible(legacy) ||
        legacy->enabled > 1 || legacy->speaker_only > 1 ||
        legacy->route_hint > EQ_ROUTE_BLUETOOTH ||
        legacy->preamp_mdB != eq_clamp_mdB(legacy->preamp_mdB)) {
        return 0;
    }

    allowed_hpf = EQ_HPF_ENABLED_MASK;
    if (legacy->version == EQ_LEGACY_ABI_VERSION_1_14) {
        allowed_hpf |= (uint8_t)(EQ_HEADROOM_MODE_MASK << EQ_HEADROOM_MODE_SHIFT);
        if (((legacy->hpf_enabled >> EQ_HEADROOM_MODE_SHIFT) & EQ_HEADROOM_MODE_MASK) > EQ_HEADROOM_RAW) {
            return 0;
        }
    }
    if ((legacy->hpf_enabled & (uint8_t)~allowed_hpf) != 0) {
        return 0;
    }
    for (int i = 0; i < EQ_BANDS; ++i) {
        if (legacy->band_gain_mdB[i] != eq_clamp_mdB(legacy->band_gain_mdB[i])) {
            return 0;
        }
    }
    return 1;
}

static inline int eq_route_hint_is_usable(uint8_t route, uint32_t counter, uint32_t last_counter, uint32_t stale_buffers)
{
    if (route == EQ_ROUTE_UNKNOWN || route > EQ_ROUTE_BLUETOOTH || counter == 0) {
        return 0;
    }

    if (counter != last_counter) {
        return 1;
    }

    return stale_buffers <= EQ_ROUTE_HINT_MAX_STALE_BUFFERS;
}

static inline uint8_t eq_route_select(uint8_t route_hint, uint32_t counter, uint32_t last_counter,
                                      uint32_t stale_buffers, int wired_headphones_connected)
{
    if (wired_headphones_connected) {
        return EQ_ROUTE_HEADPHONES;
    }

    if (eq_route_hint_is_usable(route_hint, counter, last_counter, stale_buffers)) {
        return route_hint;
    }

    return EQ_ROUTE_UNKNOWN;
}

static inline void eq_control_prepare_for_boot(eq_control_t *ctrl)
{
    if (!ctrl) {
        return;
    }

    /*
     * The app supplies AVConfig route hints, but it is not running at boot.
     * Assume speaker for an enabled saved state; the kernel headphone bit still
     * overrides this later in the plugin before speaker-only EQ is applied.
     */
    if (ctrl->enabled && ctrl->route_hint == EQ_ROUTE_UNKNOWN) {
        ctrl->route_hint = EQ_ROUTE_SPEAKER;
    }
}

static inline int eq_preset_should_try_legacy(eq_preset_primary_status_t primary_status)
{
    return primary_status == EQ_PRESET_PRIMARY_MISSING;
}

static inline uint32_t eq_hash_update(uint32_t hash, const void *data, size_t len)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; ++i) {
        hash ^= bytes[i];
        hash *= 16777619u;
    }
    return hash;
}

static inline uint32_t eq_preset_checksum(const eq_preset_file_t *preset)
{
    uint32_t hash = 2166136261u;
    hash = eq_hash_update(hash, &preset->magic, sizeof(preset->magic));
    hash = eq_hash_update(hash, &preset->version, sizeof(preset->version));
    hash = eq_hash_update(hash, &preset->header_size, sizeof(preset->header_size));
    hash = eq_hash_update(hash, &preset->payload_size, sizeof(preset->payload_size));
    hash = eq_hash_update(hash, &preset->band_count, sizeof(preset->band_count));
    hash = eq_hash_update(hash, &preset->control, sizeof(preset->control));
    return hash;
}

static inline uint32_t eq_legacy_preset_checksum(const eq_legacy_preset_file_v2_t *preset)
{
    uint32_t hash = 2166136261u;
    hash = eq_hash_update(hash, &preset->magic, sizeof(preset->magic));
    hash = eq_hash_update(hash, &preset->version, sizeof(preset->version));
    hash = eq_hash_update(hash, &preset->header_size, sizeof(preset->header_size));
    hash = eq_hash_update(hash, &preset->payload_size, sizeof(preset->payload_size));
    hash = eq_hash_update(hash, &preset->band_count, sizeof(preset->band_count));
    hash = eq_hash_update(hash, &preset->control, sizeof(preset->control));
    return hash;
}

static inline uint32_t eq_boot_state_checksum(const eq_boot_state_file_t *state)
{
    uint32_t hash = 2166136261u;
    hash = eq_hash_update(hash, &state->magic, sizeof(state->magic));
    hash = eq_hash_update(hash, &state->version, sizeof(state->version));
    hash = eq_hash_update(hash, &state->header_size, sizeof(state->header_size));
    hash = eq_hash_update(hash, &state->payload_size, sizeof(state->payload_size));
    hash = eq_hash_update(hash, &state->control, sizeof(state->control));
    return hash;
}

static inline uint32_t eq_legacy_boot_state_checksum(const eq_legacy_boot_state_file_v1_t *state)
{
    uint32_t hash = 2166136261u;
    hash = eq_hash_update(hash, &state->magic, sizeof(state->magic));
    hash = eq_hash_update(hash, &state->version, sizeof(state->version));
    hash = eq_hash_update(hash, &state->header_size, sizeof(state->header_size));
    hash = eq_hash_update(hash, &state->payload_size, sizeof(state->payload_size));
    hash = eq_hash_update(hash, &state->control, sizeof(state->control));
    return hash;
}

static inline uint32_t eq_route_profile_file_checksum(const eq_route_profile_file_t *file)
{
    uint32_t hash = 2166136261u;
    hash = eq_hash_update(hash, &file->magic, sizeof(file->magic));
    hash = eq_hash_update(hash, &file->version, sizeof(file->version));
    hash = eq_hash_update(hash, &file->header_size, sizeof(file->header_size));
    hash = eq_hash_update(hash, &file->payload_size, sizeof(file->payload_size));
    hash = eq_hash_update(hash, &file->bank, sizeof(file->bank));
    hash = eq_hash_update(hash, file->source_names, sizeof(file->source_names));
    return hash;
}

static inline void eq_route_profile_file_build(
    eq_route_profile_file_t *file,
    const eq_route_profile_bank_t *bank,
    const char source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX])
{
    if (!file) {
        return;
    }

    memset(file, 0, sizeof(*file));
    file->magic = EQ_ROUTE_PROFILE_FILE_MAGIC;
    file->version = EQ_ROUTE_PROFILE_FILE_VERSION;
    file->header_size = (uint32_t)offsetof(eq_route_profile_file_t, bank);
    file->payload_size = (uint32_t)(sizeof(file->bank) + sizeof(file->source_names));
    if (bank) {
        file->bank = *bank;
    } else {
        eq_route_profile_bank_init(&file->bank);
    }
    if (eq_route_profile_bank_validate(&file->bank) < 0) {
        eq_route_profile_bank_init(&file->bank);
    }
    file->bank.dirty_counter = 0;

    if (source_names) {
        for (unsigned int i = 0; i < EQ_ROUTE_PROFILE_COUNT; ++i) {
            size_t length = 0;
            while (length + 1u < EQ_ROUTE_PROFILE_SOURCE_NAME_MAX && source_names[i][length]) {
                file->source_names[i][length] = source_names[i][length];
                ++length;
            }
            file->source_names[i][length] = '\0';
        }
    }
    file->checksum = eq_route_profile_file_checksum(file);
}

static inline int eq_route_profile_file_extract(
    const eq_route_profile_file_t *file,
    eq_route_profile_bank_t *out_bank,
    char out_source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX])
{
    uint32_t validated_dirty_counter;

    if (!file || !out_bank ||
        file->magic != EQ_ROUTE_PROFILE_FILE_MAGIC ||
        file->version != EQ_ROUTE_PROFILE_FILE_VERSION ||
        file->header_size != offsetof(eq_route_profile_file_t, bank) ||
        file->payload_size != sizeof(file->bank) + sizeof(file->source_names) ||
        file->checksum != eq_route_profile_file_checksum(file)) {
        return -1;
    }

    for (unsigned int i = 0; i < EQ_ROUTE_PROFILE_COUNT; ++i) {
        if (!memchr(file->source_names[i], '\0', EQ_ROUTE_PROFILE_SOURCE_NAME_MAX)) {
            return -1;
        }
    }

    *out_bank = file->bank;
    if (eq_route_profile_bank_validate(out_bank) < 0) {
        return -1;
    }

    /* The persisted form deliberately stores a zero dirty counter. Compare the
       validated bank with that original representation without allocating two
       complete route banks on the Vita kernel stack. */
    validated_dirty_counter = out_bank->dirty_counter;
    out_bank->dirty_counter = file->bank.dirty_counter;
    if (memcmp(&file->bank, out_bank, sizeof(*out_bank)) != 0) {
        out_bank->dirty_counter = validated_dirty_counter;
        return -1;
    }
    out_bank->dirty_counter = validated_dirty_counter;

    if (out_source_names) {
        memcpy(out_source_names, file->source_names, sizeof(file->source_names));
    }
    return 0;
}

static inline void eq_preset_build(eq_preset_file_t *preset, const eq_control_t *ctrl)
{
    if (!preset) {
        return;
    }

    memset(preset, 0, sizeof(*preset));
    preset->magic = EQ_PRESET_MAGIC;
    preset->version = EQ_PRESET_VERSION;
    preset->header_size = (uint32_t)offsetof(eq_preset_file_t, control);
    preset->payload_size = (uint32_t)sizeof(eq_control_t);
    preset->band_count = EQ_PRESET_BAND_COUNT;

    if (ctrl) {
        preset->control = *ctrl;
        if (eq_control_validate(&preset->control) < 0) {
            eq_control_init_defaults(&preset->control);
        }
    } else {
        eq_control_init_defaults(&preset->control);
    }

    preset->control.dirty_counter = 0;
    preset->control.route_hint = EQ_ROUTE_UNKNOWN;
    preset->checksum = eq_preset_checksum(preset);
}

static inline int eq_preset_extract_control(const eq_preset_file_t *preset, eq_control_t *out)
{
    eq_control_t raw;
    eq_control_t ctrl;

    if (!preset || !out) {
        return -1;
    }

    if (preset->magic != EQ_PRESET_MAGIC ||
        preset->version != EQ_PRESET_VERSION ||
        preset->header_size != offsetof(eq_preset_file_t, control) ||
        preset->payload_size != sizeof(eq_control_t) ||
        preset->band_count != EQ_PRESET_BAND_COUNT) {
        return -1;
    }

    if (preset->checksum != eq_preset_checksum(preset)) {
        return -1;
    }

    raw = preset->control;
    if (raw.route_hint > EQ_ROUTE_BLUETOOTH) {
        return -1;
    }

    ctrl = raw;
    if (eq_control_validate(&ctrl) < 0) {
        return -1;
    }

    raw.dirty_counter = 0;
    raw.version = EQ_ABI_VERSION;
    raw.size = (uint32_t)sizeof(eq_control_t);
    raw.route_hint = EQ_ROUTE_UNKNOWN;
    ctrl.dirty_counter = 0;
    ctrl.route_hint = EQ_ROUTE_UNKNOWN;

    if (memcmp(&raw, &ctrl, sizeof(raw)) != 0) {
        return -1;
    }

    *out = ctrl;
    return 0;
}

static inline int eq_preset_validate(const eq_preset_file_t *preset)
{
    eq_control_t ctrl;
    return eq_preset_extract_control(preset, &ctrl);
}

static inline int eq_legacy_preset_extract_control(const eq_legacy_preset_file_v2_t *preset, eq_control_t *out)
{
    eq_control_t ctrl;

    if (!preset || !out ||
        preset->magic != EQ_PRESET_MAGIC ||
        preset->version != EQ_LEGACY_PRESET_VERSION ||
        preset->header_size != offsetof(eq_legacy_preset_file_v2_t, control) ||
        preset->payload_size != sizeof(eq_legacy_control_v1_14_t) ||
        preset->band_count != EQ_PRESET_BAND_COUNT ||
        preset->checksum != eq_legacy_preset_checksum(preset) ||
        !eq_legacy_control_is_canonical(&preset->control) ||
        eq_control_import_legacy(&ctrl, &preset->control) < 0) {
        return -1;
    }

    ctrl.dirty_counter = 0;
    ctrl.route_hint = EQ_ROUTE_UNKNOWN;
    *out = ctrl;
    return 0;
}

static inline void eq_boot_state_build(eq_boot_state_file_t *state, const eq_control_t *ctrl)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    state->magic = EQ_BOOT_STATE_MAGIC;
    state->version = EQ_BOOT_STATE_VERSION;
    state->header_size = (uint32_t)offsetof(eq_boot_state_file_t, control);
    state->payload_size = (uint32_t)sizeof(eq_control_t);

    if (ctrl) {
        state->control = *ctrl;
        if (eq_control_validate(&state->control) < 0) {
            eq_control_init_defaults(&state->control);
        }
    } else {
        eq_control_init_defaults(&state->control);
    }

    state->control.dirty_counter = 0;
    state->checksum = eq_boot_state_checksum(state);
}

static inline int eq_boot_state_extract_control(const eq_boot_state_file_t *state, eq_control_t *out)
{
    eq_control_t raw;
    eq_control_t ctrl;

    if (!state || !out) {
        return -1;
    }

    if (state->magic != EQ_BOOT_STATE_MAGIC ||
        state->version != EQ_BOOT_STATE_VERSION ||
        state->header_size != offsetof(eq_boot_state_file_t, control) ||
        state->payload_size != sizeof(eq_control_t)) {
        return -1;
    }

    if (state->checksum != eq_boot_state_checksum(state)) {
        return -1;
    }

    raw = state->control;
    if (raw.route_hint > EQ_ROUTE_BLUETOOTH) {
        return -1;
    }

    ctrl = raw;
    if (eq_control_validate(&ctrl) < 0) {
        return -1;
    }

    raw.dirty_counter = 0;
    raw.version = EQ_ABI_VERSION;
    raw.size = (uint32_t)sizeof(eq_control_t);
    ctrl.dirty_counter = 0;

    if (memcmp(&raw, &ctrl, sizeof(raw)) != 0) {
        return -1;
    }

    eq_control_prepare_for_boot(&ctrl);
    *out = ctrl;
    return 0;
}

static inline int eq_boot_state_validate(const eq_boot_state_file_t *state)
{
    eq_control_t ctrl;
    return eq_boot_state_extract_control(state, &ctrl);
}

static inline int eq_legacy_boot_state_extract_control(const eq_legacy_boot_state_file_v1_t *state, eq_control_t *out)
{
    eq_control_t ctrl;

    if (!state || !out ||
        state->magic != EQ_BOOT_STATE_MAGIC ||
        state->version != EQ_LEGACY_BOOT_STATE_VERSION ||
        state->header_size != offsetof(eq_legacy_boot_state_file_v1_t, control) ||
        state->payload_size != sizeof(eq_legacy_control_v1_14_t) ||
        state->checksum != eq_legacy_boot_state_checksum(state) ||
        !eq_legacy_control_is_canonical(&state->control) ||
        eq_control_import_legacy(&ctrl, &state->control) < 0) {
        return -1;
    }

    ctrl.dirty_counter = 0;
    eq_control_prepare_for_boot(&ctrl);
    *out = ctrl;
    return 0;
}

int EqSetControl(const eq_control_t *ctrl);
int EqSetRouteProfiles(const eq_route_profile_bank_t *bank);
int EqGetStatus(eq_status_t *status);
int EqDrainDiagnostics(eq_diag_snapshot_t *snapshot);
void EqGetVersion(eq_version_t *out);
