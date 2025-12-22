#pragma once

#include <psp2common/types.h>
#include <stdint.h>

#define EQ_VERSION_MAJOR 1
#define EQ_VERSION_MINOR 0
#define EQ_VERSION_PATCH 0

#define EQ_BANDS 10
#define EQ_MAX_ABS_GAIN_MDB 12000
#define EQ_DEFAULT_PREAMP_MDB -6000
#define EQ_SMOOTH_SAMPLES 512

static const uint32_t eq_band_frequencies[EQ_BANDS] = {
    31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

typedef enum eq_route
{
    EQ_ROUTE_UNKNOWN = 0,
    EQ_ROUTE_SPEAKER = 1,
    EQ_ROUTE_HEADPHONES = 2,
    EQ_ROUTE_BLUETOOTH = 3
} eq_route_t;

typedef struct eq_version
{
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;
} eq_version_t;

typedef struct eq_control
{
    uint32_t version;
    uint32_t size;
    volatile uint32_t dirty_counter;
    uint8_t enabled;
    uint8_t speaker_only;
    uint8_t hpf_enabled;
    uint8_t reserved1;
    int32_t preamp_mdB;
    int32_t band_gain_mdB[EQ_BANDS];
} eq_control_t;

typedef struct eq_status
{
    volatile uint32_t status_counter;
    uint32_t sample_rate;
    uint8_t route;
    uint8_t eq_active;
    uint8_t smoothing;
    uint8_t reserved;
    int32_t clip_events;
    int16_t peak_l;
    int16_t peak_r;
    uint32_t debug_port;
    uint32_t debug_len;
    uint32_t debug_channels;
    uint32_t debug_run_count;
} eq_status_t;

typedef struct eq_shared_block
{
    eq_control_t control;
    eq_status_t status;
} eq_shared_block_t;

int EqSetControl(const eq_control_t *ctrl);
int EqGetStatus(eq_status_t *status);
void EqGetVersion(eq_version_t *out);
