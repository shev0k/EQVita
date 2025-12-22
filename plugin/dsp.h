#pragma once

#include <stdint.h>
#include "../common/eq_shared.h"

typedef struct eq_biquad
{
    float b0, b1, b2;
    float a1, a2;
    float z1, z2;
} eq_biquad_t;

typedef struct eq_dsp_state
{
    uint32_t sample_rate;
    eq_biquad_t active[EQ_BANDS];
    eq_biquad_t target[EQ_BANDS];
    eq_biquad_t hpf; // High-Pass Filter state
    float preamp;
    float target_preamp;
    uint32_t smooth_remaining;
    int hpf_enabled;
} eq_dsp_state_t;

void eq_dsp_init(eq_dsp_state_t *state, uint32_t sample_rate);
void eq_dsp_set_targets(eq_dsp_state_t *state, uint32_t sample_rate, const int32_t *band_mdB, int32_t preamp_mdB, int hpf_enabled);
void eq_dsp_apply(eq_dsp_state_t *state, int16_t *pcm, uint32_t frames, uint32_t channels, int32_t *clip_counter, int16_t *peak_l, int16_t *peak_r);
