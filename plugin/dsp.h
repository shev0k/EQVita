#pragma once

#include <stdint.h>
#include "../common/eq_shared.h"

typedef struct eq_biquad
{
    float b0, b1, b2;
    float a1, a2;
} eq_biquad_t;

typedef struct eq_stereo_biquad_delay
{
    float z1[EQ_DSP_MAX_CHANNELS];
    float z2[EQ_DSP_MAX_CHANNELS];
} __attribute__((aligned(8))) eq_stereo_biquad_delay_t;

/* Coefficients packed as left/right lanes for the Cortex-A9 NEON kernel. */
typedef struct eq_stereo_biquad
{
    float b0[EQ_DSP_MAX_CHANNELS];
    float b1[EQ_DSP_MAX_CHANNELS];
    float b2[EQ_DSP_MAX_CHANNELS];
    float a1[EQ_DSP_MAX_CHANNELS];
    float a2[EQ_DSP_MAX_CHANNELS];
} __attribute__((aligned(8))) eq_stereo_biquad_t;

typedef struct eq_dsp_state
{
    uint32_t sample_rate;
    eq_biquad_t active[EQ_PARAMETRIC_FILTERS];
    eq_biquad_t target[EQ_PARAMETRIC_FILTERS];
    eq_stereo_biquad_t active_stereo[EQ_PARAMETRIC_FILTERS];
    eq_biquad_t hpf;
    eq_stereo_biquad_delay_t band_z[EQ_PARAMETRIC_FILTERS];
    eq_stereo_biquad_delay_t hpf_z;
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

typedef char eq_stereo_coefficients_must_be_64_bit_aligned[
    (offsetof(eq_dsp_state_t, active_stereo) & 7u) == 0u ? 1 : -1];
typedef char eq_stereo_delay_state_must_be_64_bit_aligned[
    (offsetof(eq_dsp_state_t, band_z) & 7u) == 0u ? 1 : -1];

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

#if defined(EQVITA_DSP_TEST_API)
void eq_dsp_set_neon_enabled_for_tests(int enabled);
#endif
