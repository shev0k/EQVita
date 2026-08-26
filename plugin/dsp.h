#pragma once

#include <stdint.h>
#include "../common/eq_shared.h"

typedef struct eq_biquad
{
    float b0, b1, b2;
    float a1, a2;
} eq_biquad_t;

typedef struct eq_biquad_delay
{
    float z1, z2;
} eq_biquad_delay_t;

typedef struct eq_dsp_state
{
    uint32_t sample_rate;
    eq_biquad_t active[EQ_PARAMETRIC_FILTERS];
    eq_biquad_t target[EQ_PARAMETRIC_FILTERS];
    eq_biquad_t hpf;
    eq_biquad_delay_t band_z[EQ_DSP_MAX_CHANNELS][EQ_PARAMETRIC_FILTERS];
    eq_biquad_delay_t hpf_z[EQ_DSP_MAX_CHANNELS];
    uint8_t active_band_enabled[EQ_PARAMETRIC_FILTERS];
    uint8_t target_band_enabled[EQ_PARAMETRIC_FILTERS];
    uint8_t active_band_index[EQ_PARAMETRIC_FILTERS];
    uint8_t target_band_index[EQ_PARAMETRIC_FILTERS];
    uint8_t active_band_count;
    uint8_t target_band_count;
    uint8_t active_operation_type[EQ_PARAMETRIC_FILTERS];
    uint8_t target_operation_type[EQ_PARAMETRIC_FILTERS];
    uint8_t active_operation_count;
    uint8_t target_operation_count;
    uint8_t targets_initialized;
    float preamp;
    float target_preamp;
    uint32_t smooth_remaining;
    int hpf_enabled;
} eq_dsp_state_t;

void eq_dsp_init(eq_dsp_state_t *state, uint32_t sample_rate);
void eq_dsp_set_targets(eq_dsp_state_t *state, uint32_t sample_rate, const int32_t *band_mdB, int32_t preamp_mdB, int hpf_enabled);
void eq_dsp_set_parametric_targets(eq_dsp_state_t *state,
                                   uint32_t sample_rate,
                                   const eq_parametric_filter_t *filters,
                                   uint32_t filter_count,
                                   int32_t preamp_mdB,
                                   int hpf_enabled);
void eq_dsp_apply_to(eq_dsp_state_t *state, const int16_t *input, int16_t *output, uint32_t frames, uint32_t channels, int32_t *clip_counter, uint16_t *peak_l, uint16_t *peak_r);
void eq_dsp_apply(eq_dsp_state_t *state, int16_t *pcm, uint32_t frames, uint32_t channels, int32_t *clip_counter, uint16_t *peak_l, uint16_t *peak_r);
uint32_t eq_dsp_active_band_count(const eq_dsp_state_t *state);
