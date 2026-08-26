#include "dsp.h"

#include <math.h>
#include <string.h>

#define EQ_Q_VALUE 0.707f
#define EQ_PI 3.14159265358979323846f

static int g_errno_stub;
int *__errno(void) { return &g_errno_stub; }

static void biquad_identity(eq_biquad_t *out);
static int biquad_is_finite(const eq_biquad_t *c);

#define EQ_RUNTIME_GAIN_MIN 0.03f
#define EQ_RUNTIME_GAIN_MAX 4.0f
#define EQ_RUNTIME_GAIN_DEFAULT 0.5011872f

static inline float sanitize_runtime_gain(float gain, float fallback) {
    if (isfinite(gain) && gain >= EQ_RUNTIME_GAIN_MIN && gain <= EQ_RUNTIME_GAIN_MAX) {
        return gain;
    }
    if (isfinite(fallback) && fallback >= EQ_RUNTIME_GAIN_MIN && fallback <= EQ_RUNTIME_GAIN_MAX) {
        return fallback;
    }
    return EQ_RUNTIME_GAIN_DEFAULT;
}

static inline void sanitize_preamp_state(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

    state->target_preamp = sanitize_runtime_gain(state->target_preamp, EQ_RUNTIME_GAIN_DEFAULT);
    state->preamp = sanitize_runtime_gain(state->preamp, state->target_preamp);
}

static inline void sanitize_smoothing_state(eq_dsp_state_t *state) {
    if (state && state->smooth_remaining > EQ_SMOOTH_SAMPLES) {
        state->smooth_remaining = EQ_SMOOTH_SAMPLES;
    }
}

static inline float preamp_mdB_to_gain(int32_t mdB) {
    mdB = eq_clamp_preamp_mdB(mdB);
    return powf(10.0f, ((float)mdB) / 20000.0f);
}

static inline float graphic_mdB_to_gain(int32_t mdB) {
    mdB = eq_clamp_mdB(mdB);
    return powf(10.0f, ((float)mdB) / 20000.0f);
}

static inline float parametric_mdB_to_gain(int32_t mdB) {
    mdB = eq_clamp_parametric_gain_mdB(mdB);
    return powf(10.0f, ((float)mdB) / 20000.0f);
}

static uint32_t normalize_sample_rate(uint32_t sample_rate) {
    if (sample_rate < 8000 || sample_rate > 192000) {
        return 48000;
    }
    return sample_rate;
}

static float clamp_filter_frequency(uint32_t sample_rate, float freq) {
    // Clamp frequency to Nyquist - margin to avoid instability
    float nyquist = (float)sample_rate * 0.5f;
    if (!isfinite(freq) || freq < 1.0f) {
        return 1.0f;
    }
    if (nyquist <= 100.0f) {
        return nyquist > 1.0f ? nyquist - 1.0f : 1.0f;
    }
    if (freq >= nyquist) {
        return nyquist - 100.0f;
    }
    return freq;
}

static void biquad_peaking(eq_biquad_t *out, uint32_t sample_rate, float freq, float gain, float q) {
    sample_rate = normalize_sample_rate(sample_rate);
    freq = clamp_filter_frequency(sample_rate, freq);

    if (!(gain > 0.0f) || !isfinite(gain) || !(q > 0.0f) || !isfinite(q)) {
        biquad_identity(out);
        return;
    }

    float omega = 2.0f * EQ_PI * ((float)freq) / (float)sample_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * q);
    
    // RBJ Peaking EQ: A = sqrt( 10^(dB/20) )
    float A = sqrtf(gain);

    float b0 = 1.0f + alpha * A;
    float b1 = -2.0f * cs;
    float b2 = 1.0f - alpha * A;
    float a0 = 1.0f + alpha / A;
    float a1 = -2.0f * cs;
    float a2 = 1.0f - alpha / A;

    float inv_a0 = 1.0f / a0;
    out->b0 = b0 * inv_a0;
    out->b1 = b1 * inv_a0;
    out->b2 = b2 * inv_a0;
    out->a1 = a1 * inv_a0;
    out->a2 = a2 * inv_a0;
}

static void biquad_shelf(eq_biquad_t *out,
                         uint32_t sample_rate,
                         float freq,
                         float gain,
                         float q_or_s,
                         int use_slope,
                         int high_shelf)
{
    /* RBJ shelf equations and Q/S selection used by Equalizer APO's BiQuad. */
    float omega;
    float sn;
    float cs;
    float A;
    float alpha;
    float beta;
    float b0, b1, b2, a0, a1, a2;
    float inv_a0;

    sample_rate = normalize_sample_rate(sample_rate);
    freq = clamp_filter_frequency(sample_rate, freq);
    if (!(gain > 0.0f) || !isfinite(gain) || !(q_or_s > 0.0f) || !isfinite(q_or_s)) {
        biquad_identity(out);
        return;
    }

    omega = 2.0f * EQ_PI * freq / (float)sample_rate;
    sn = sinf(omega);
    cs = cosf(omega);
    A = sqrtf(gain);
    if (use_slope) {
        float radicand = (A + 1.0f / A) * (1.0f / q_or_s - 1.0f) + 2.0f;
        if (!(radicand > 0.0f) || !isfinite(radicand)) {
            biquad_identity(out);
            return;
        }
        alpha = sn * 0.5f * sqrtf(radicand);
    } else {
        alpha = sn / (2.0f * q_or_s);
    }
    beta = 2.0f * sqrtf(A) * alpha;

    if (high_shelf) {
        b0 = A * ((A + 1.0f) + (A - 1.0f) * cs + beta);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
        b2 = A * ((A + 1.0f) + (A - 1.0f) * cs - beta);
        a0 = (A + 1.0f) - (A - 1.0f) * cs + beta;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
        a2 = (A + 1.0f) - (A - 1.0f) * cs - beta;
    } else {
        b0 = A * ((A + 1.0f) - (A - 1.0f) * cs + beta);
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
        b2 = A * ((A + 1.0f) - (A - 1.0f) * cs - beta);
        a0 = (A + 1.0f) + (A - 1.0f) * cs + beta;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
        a2 = (A + 1.0f) + (A - 1.0f) * cs - beta;
    }

    if (!(fabsf(a0) > 0.0000001f) || !isfinite(a0)) {
        biquad_identity(out);
        return;
    }
    inv_a0 = 1.0f / a0;
    out->b0 = b0 * inv_a0;
    out->b1 = b1 * inv_a0;
    out->b2 = b2 * inv_a0;
    out->a1 = a1 * inv_a0;
    out->a2 = a2 * inv_a0;
    if (!biquad_is_finite(out)) {
        biquad_identity(out);
    }
}

static void biquad_identity(eq_biquad_t *out) {
    out->b0 = 1.0f;
    out->b1 = 0.0f;
    out->b2 = 0.0f;
    out->a1 = 0.0f;
    out->a2 = 0.0f;
}

static void operation_identity(eq_biquad_t *out, uint8_t operation_type) {
    biquad_identity(out);
    if (operation_type == EQ_FILTER_COPY) {
        out->a1 = 1.0f;
    }
}

static int biquad_same(const eq_biquad_t *a, const eq_biquad_t *b) {
    const float eps = 0.000001f;
    return fabsf(a->b0 - b->b0) < eps &&
           fabsf(a->b1 - b->b1) < eps &&
           fabsf(a->b2 - b->b2) < eps &&
           fabsf(a->a1 - b->a1) < eps &&
           fabsf(a->a2 - b->a2) < eps;
}

static int biquad_is_finite(const eq_biquad_t *c) {
    return c &&
           isfinite(c->b0) &&
           isfinite(c->b1) &&
           isfinite(c->b2) &&
           isfinite(c->a1) &&
           isfinite(c->a2);
}

static void biquad_highpass(eq_biquad_t *out, uint32_t sample_rate, float freq) {
    sample_rate = normalize_sample_rate(sample_rate);
    freq = clamp_filter_frequency(sample_rate, freq);

    float omega = 2.0f * EQ_PI * ((float)freq) / (float)sample_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * 0.707f); // Q = 0.707 (Butterworth)

    float b0 = (1.0f + cs) / 2.0f;
    float b1 = -(1.0f + cs);
    float b2 = (1.0f + cs) / 2.0f;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * cs;
    float a2 = 1.0f - alpha;

    float inv_a0 = 1.0f / a0;
    out->b0 = b0 * inv_a0;
    out->b1 = b1 * inv_a0;
    out->b2 = b2 * inv_a0;
    out->a1 = a1 * inv_a0;
    out->a2 = a2 * inv_a0;
}

static void reset_delay_state(eq_dsp_state_t *state) {
    memset(state->band_z, 0, sizeof(state->band_z));
    memset(state->hpf_z, 0, sizeof(state->hpf_z));
}

static void reset_hpf_delay_state(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

    memset(state->hpf_z, 0, sizeof(state->hpf_z));
}

static void reset_band_delay_state(eq_dsp_state_t *state, int band) {
    if (!state || band < 0 || band >= EQ_PARAMETRIC_FILTERS) {
        return;
    }

    for (int ch = 0; ch < EQ_DSP_MAX_CHANNELS; ++ch) {
        state->band_z[ch][band].z1 = 0.0f;
        state->band_z[ch][band].z2 = 0.0f;
    }
}

static void rebuild_band_index(const uint8_t *enabled, uint8_t *index, uint8_t *count) {
    uint8_t n = 0;

    if (!enabled || !index || !count) {
        return;
    }

    for (uint8_t b = 0; b < EQ_PARAMETRIC_FILTERS; ++b) {
        if (enabled[b]) {
            index[n++] = b;
        }
    }

    *count = n;
}

static void rebuild_dsp_band_indexes(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

    rebuild_band_index(state->active_band_enabled, state->active_band_index, &state->active_band_count);
    rebuild_band_index(state->target_band_enabled, state->target_band_index, &state->target_band_count);
}

static void sanitize_biquad_state(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

    if (!biquad_is_finite(&state->hpf)) {
        biquad_highpass(&state->hpf, state->sample_rate, 70);
        reset_hpf_delay_state(state);
    }

    for (int b = 0; b < EQ_PARAMETRIC_FILTERS; ++b) {
        if (!biquad_is_finite(&state->target[b])) {
            operation_identity(&state->target[b], state->target_operation_type[b]);
            state->target_band_enabled[b] = 0;
        }

        if (!biquad_is_finite(&state->active[b])) {
            if (state->target_band_enabled[b] ||
                state->target_operation_type[b] == EQ_FILTER_COPY) {
                state->active[b] = state->target[b];
                state->active_band_enabled[b] = state->target_band_enabled[b];
            } else {
                operation_identity(&state->active[b], state->active_operation_type[b]);
                state->active_band_enabled[b] = 0;
            }
            reset_band_delay_state(state, b);
        }
    }

    rebuild_dsp_band_indexes(state);
}

static void flush_denormal_delay_state(eq_dsp_state_t *state, uint32_t channels) {
    if (!state) {
        return;
    }

    if (channels > EQ_DSP_MAX_CHANNELS) {
        channels = EQ_DSP_MAX_CHANNELS;
    }

    for (uint32_t ch = 0; ch < channels; ++ch) {
        if (!isfinite(state->hpf_z[ch].z1) || fabsf(state->hpf_z[ch].z1) < 1e-15f) state->hpf_z[ch].z1 = 0.0f;
        if (!isfinite(state->hpf_z[ch].z2) || fabsf(state->hpf_z[ch].z2) < 1e-15f) state->hpf_z[ch].z2 = 0.0f;
        for (uint8_t i = 0; i < state->active_band_count; ++i) {
            uint8_t b = state->active_band_index[i];
            if (!(state->active_band_enabled[b] & (uint8_t)(1u << ch))) {
                continue;
            }
            if (!isfinite(state->band_z[ch][b].z1) || fabsf(state->band_z[ch][b].z1) < 1e-15f) state->band_z[ch][b].z1 = 0.0f;
            if (!isfinite(state->band_z[ch][b].z2) || fabsf(state->band_z[ch][b].z2) < 1e-15f) state->band_z[ch][b].z2 = 0.0f;
        }
    }
}

static inline int16_t limit_i16(float x, int32_t *clip_counter) {
    const float knee = 32600.0f;
    float sign = 1.0f;
    float ax = x;
    float limit = 32767.0f;
    float knee_range;

    if (x < 0.0f) {
        sign = -1.0f;
        ax = -x;
        limit = 32768.0f;
    }

    if (ax > limit) {
        float over = ax - knee;
        if (clip_counter) {
            (*clip_counter)++;
        }
        knee_range = limit - knee;
        ax = knee + (knee_range * over) / (over + knee_range);
        x = sign * ax;
    }

    x = (x >= 0.0f) ? (x + 0.5f) : (x - 0.5f);
    if (x > 32767.0f) {
        return 32767;
    }
    if (x < -32768.0f) {
        return -32768;
    }
    return (int16_t)x;
}

static inline uint16_t abs_i16_peak(int16_t value) {
    if (value == (int16_t)-32768) {
        return 32768u;
    }
    return (uint16_t)(value < 0 ? -value : value);
}

static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline void lerp_biquad(const eq_biquad_t *a, const eq_biquad_t *b, float t, eq_biquad_t *out) {
    out->b0 = lerp(a->b0, b->b0, t);
    out->b1 = lerp(a->b1, b->b1, t);
    out->b2 = lerp(a->b2, b->b2, t);
    out->a1 = lerp(a->a1, b->a1, t);
    out->a2 = lerp(a->a2, b->a2, t);
}

static void advance_smoothing_state(eq_dsp_state_t *state) {
    float t;

    if (!state || state->smooth_remaining == 0) {
        return;
    }

    t = 1.0f - ((float)state->smooth_remaining / (float)EQ_SMOOTH_SAMPLES);
    if (t < 0.0f) {
        t = 0.0f;
    } else if (t > 1.0f) {
        t = 1.0f;
    }

    state->preamp = lerp(state->preamp, state->target_preamp, t);
    for (int b = 0; b < EQ_PARAMETRIC_FILTERS; ++b) {
        if (state->active_operation_type[b] == EQ_FILTER_COPY) {
            eq_biquad_t current;
            lerp_biquad(&state->active[b], &state->target[b], t, &current);
            state->active[b] = current;
        } else if (state->active_band_enabled[b] || state->target_band_enabled[b]) {
            eq_biquad_t current;
            lerp_biquad(&state->active[b], &state->target[b], t, &current);
            state->active[b] = current;
            state->active_band_enabled[b] |= state->target_band_enabled[b];
        } else {
            biquad_identity(&state->active[b]);
            state->active_band_enabled[b] = 0;
        }
    }
    rebuild_dsp_band_indexes(state);
}

void eq_dsp_init(eq_dsp_state_t *state, uint32_t sample_rate) {
    memset(state, 0, sizeof(*state));
    state->sample_rate = normalize_sample_rate(sample_rate);
    state->preamp = preamp_mdB_to_gain(EQ_DEFAULT_PREAMP_MDB);
    state->target_preamp = state->preamp;
    state->hpf_enabled = 0;
    for (int i = 0; i < EQ_PARAMETRIC_FILTERS; ++i) {
        biquad_identity(&state->active[i]);
        state->target[i] = state->active[i];
        state->active_band_enabled[i] = 0;
        state->target_band_enabled[i] = 0;
    }
    
    // Init HPF at 70Hz
    biquad_highpass(&state->hpf, state->sample_rate, 70);
    reset_delay_state(state);
}

static void commit_targets(eq_dsp_state_t *state,
                           uint32_t sample_rate,
                           const eq_biquad_t *next_target,
                           const uint8_t *next_enabled,
                           const uint8_t *next_operation_type,
                           uint8_t next_operation_count,
                           int32_t preamp_mdB,
                           int hpf_enabled)
{
    float next_preamp;
    int changed = 0;
    int topology_changed = 0;
    int sample_rate_changed = 0;
    int first_target;

    if (!state) {
        return;
    }

    sanitize_preamp_state(state);
    sanitize_smoothing_state(state);
    sanitize_biquad_state(state);
    first_target = !state->targets_initialized;

    sample_rate = normalize_sample_rate(sample_rate);
    if (sample_rate != state->sample_rate) {
        state->sample_rate = sample_rate;
        reset_delay_state(state);
        biquad_highpass(&state->hpf, state->sample_rate, 70);
        changed = 1;
        sample_rate_changed = 1;
    }

    if (state->target_operation_count != next_operation_count) {
        topology_changed = 1;
        changed = 1;
    }
    for (int i = 0; i < EQ_PARAMETRIC_FILTERS; ++i) {
        if (state->target_operation_type[i] != next_operation_type[i]) {
            topology_changed = 1;
            changed = 1;
        }
        if (state->target_band_enabled[i] != next_enabled[i] ||
            !biquad_same(&state->target[i], &next_target[i])) {
            changed = 1;
        }
    }

    next_preamp = preamp_mdB_to_gain(preamp_mdB);
    if (fabsf(state->target_preamp - next_preamp) > 0.000001f ||
        state->hpf_enabled != (hpf_enabled ? 1 : 0)) {
        changed = 1;
    }

    if (!changed) {
        state->targets_initialized = 1;
        return;
    }

    if (!first_target && !sample_rate_changed && !topology_changed) {
        advance_smoothing_state(state);
    }

    for (int i = 0; i < EQ_PARAMETRIC_FILTERS; ++i) {
        state->target[i] = next_target[i];
        state->target_band_enabled[i] = next_enabled[i];
        state->target_operation_type[i] = next_operation_type[i];
        if (first_target || sample_rate_changed || topology_changed) {
            state->active[i] = next_target[i];
            state->active_band_enabled[i] = next_enabled[i];
            state->active_operation_type[i] = next_operation_type[i];
        }
    }
    state->target_operation_count = next_operation_count;
    if (first_target || sample_rate_changed || topology_changed) {
        state->active_operation_count = next_operation_count;
    }
    rebuild_dsp_band_indexes(state);

    state->target_preamp = next_preamp;
    if (first_target || sample_rate_changed || topology_changed) {
        state->preamp = next_preamp;
        state->smooth_remaining = 0;
        if (topology_changed) {
            reset_delay_state(state);
        }
    } else {
        state->smooth_remaining = EQ_SMOOTH_SAMPLES;
    }
    if (state->hpf_enabled != (hpf_enabled ? 1 : 0)) {
        reset_hpf_delay_state(state);
    }
    state->hpf_enabled = hpf_enabled ? 1 : 0;
    state->targets_initialized = 1;
}

void eq_dsp_set_targets(eq_dsp_state_t *state,
                        uint32_t sample_rate,
                        const int32_t *band_mdB,
                        int32_t preamp_mdB,
                        int hpf_enabled)
{
    eq_biquad_t next_target[EQ_PARAMETRIC_FILTERS];
    uint8_t next_enabled[EQ_PARAMETRIC_FILTERS];
    uint8_t next_operation_type[EQ_PARAMETRIC_FILTERS];
    uint32_t normalized_rate = normalize_sample_rate(sample_rate);

    if (!state) {
        return;
    }
    for (int i = 0; i < EQ_PARAMETRIC_FILTERS; ++i) {
        biquad_identity(&next_target[i]);
        next_enabled[i] = 0;
        next_operation_type[i] = EQ_FILTER_NONE;
    }
    for (int i = 0; i < EQ_BANDS; ++i) {
        int32_t gain_mdB = eq_clamp_mdB(band_mdB ? band_mdB[i] : 0);
        next_operation_type[i] = EQ_FILTER_PEAK;
        if (gain_mdB != 0) {
            biquad_peaking(&next_target[i], normalized_rate, (float)eq_band_frequencies[i],
                            graphic_mdB_to_gain(gain_mdB), EQ_Q_VALUE);
            next_enabled[i] = EQ_CHANNEL_STEREO_MASK;
        }
    }
    commit_targets(state, normalized_rate, next_target, next_enabled, next_operation_type,
                   EQ_BANDS, preamp_mdB, hpf_enabled);
}

void eq_dsp_set_parametric_targets(eq_dsp_state_t *state,
                                   uint32_t sample_rate,
                                   const eq_parametric_filter_t *filters,
                                   uint32_t filter_count,
                                   int32_t preamp_mdB,
                                   int hpf_enabled)
{
    eq_biquad_t next_target[EQ_PARAMETRIC_FILTERS];
    uint8_t next_enabled[EQ_PARAMETRIC_FILTERS];
    uint8_t next_operation_type[EQ_PARAMETRIC_FILTERS];
    uint32_t normalized_rate = normalize_sample_rate(sample_rate);

    if (!state) {
        return;
    }
    if (filter_count > EQ_PARAMETRIC_FILTERS) {
        filter_count = EQ_PARAMETRIC_FILTERS;
    }
    for (int i = 0; i < EQ_PARAMETRIC_FILTERS; ++i) {
        biquad_identity(&next_target[i]);
        next_enabled[i] = 0;
        next_operation_type[i] = EQ_FILTER_NONE;
    }

    for (uint32_t i = 0; i < filter_count; ++i) {
        const eq_parametric_filter_t *filter = &filters[i];
        uint8_t channel_mask = filter->channel_mask & EQ_CHANNEL_STEREO_MASK;
        float freq;
        float gain;
        float q_or_s;

        if (filter->type == EQ_FILTER_COPY) {
            next_operation_type[i] = EQ_FILTER_COPY;
            next_target[i].b0 = (float)filter->data.copy.matrix[0] / (float)EQ_COPY_COEFFICIENT_SCALE;
            next_target[i].b1 = (float)filter->data.copy.matrix[1] / (float)EQ_COPY_COEFFICIENT_SCALE;
            next_target[i].b2 = (float)filter->data.copy.matrix[2] / (float)EQ_COPY_COEFFICIENT_SCALE;
            next_target[i].a1 = (float)filter->data.copy.matrix[3] / (float)EQ_COPY_COEFFICIENT_SCALE;
            next_target[i].a2 = 0.0f;
            continue;
        }
        if (!channel_mask || filter->type < EQ_FILTER_PEAK || filter->type > EQ_FILTER_HIGH_SHELF) {
            continue;
        }
        next_operation_type[i] = filter->type;
        freq = (float)eq_clamp_filter_frequency_mHz(filter->data.filter.frequency_mHz) / 1000.0f;
        gain = parametric_mdB_to_gain(filter->data.filter.gain_mdB);
        q_or_s = (float)eq_clamp_filter_q_uQ(filter->data.filter.q_uQ) / 1000000.0f;

        if (filter->type == EQ_FILTER_PEAK) {
            biquad_peaking(&next_target[i], normalized_rate, freq, gain, q_or_s);
        } else {
            int use_slope = filter->shape == EQ_FILTER_SHAPE_S;
            int high_shelf = filter->type == EQ_FILTER_HIGH_SHELF;
            if (filter->frequency_mode == EQ_FILTER_FREQUENCY_CORNER) {
                /* Equalizer APO's DCX2496-compatible corner-to-center adjustment. */
                float A = sqrtf(gain);
                float slope = q_or_s;
                if (!use_slope) {
                    float denominator = ((1.0f / (q_or_s * q_or_s) - 2.0f) / (A + 1.0f / A)) + 1.0f;
                    if (fabsf(denominator) > 0.000001f) {
                        slope = 1.0f / denominator;
                    }
                }
                if (slope > 0.0f && isfinite(slope)) {
                    float db_gain = (float)eq_clamp_parametric_gain_mdB(filter->data.filter.gain_mdB) / 1000.0f;
                    float center_factor = powf(10.0f, fabsf(db_gain) / 80.0f / slope);
                    freq = high_shelf ? freq / center_factor : freq * center_factor;
                }
            }
            biquad_shelf(&next_target[i], normalized_rate, freq, gain, q_or_s, use_slope, high_shelf);
        }
        if (biquad_is_finite(&next_target[i])) {
            next_enabled[i] = channel_mask;
        } else {
            biquad_identity(&next_target[i]);
        }
    }

    commit_targets(state, normalized_rate, next_target, next_enabled, next_operation_type,
                   (uint8_t)filter_count, preamp_mdB, hpf_enabled);
}

static inline float process_biquad(const eq_biquad_t *c, eq_biquad_delay_t *z, float x) {
    float y = c->b0 * x + z->z1;
    z->z1 = c->b1 * x - c->a1 * y + z->z2;
    z->z2 = c->b2 * x - c->a2 * y;
    return y;
}

static void process_stereo_steady(eq_dsp_state_t *state, const int16_t *input, int16_t *output,
                                  uint32_t frames, int32_t *clip_counter,
                                  uint16_t *peak_l, uint16_t *peak_r) {
    const float preamp = state->preamp;
    uint16_t max_l = 0;
    uint16_t max_r = 0;

    for (uint32_t frame = 0; frame < frames; ++frame) {
        float left = (float)input[0];
        float right = (float)input[1];

        if (state->hpf_enabled) {
            left = process_biquad(&state->hpf, &state->hpf_z[0], left) * preamp;
            right = process_biquad(&state->hpf, &state->hpf_z[1], right) * preamp;
        } else {
            left *= preamp;
            right *= preamp;
        }

        for (uint8_t operation = 0; operation < state->active_operation_count; ++operation) {
            if (state->active_operation_type[operation] == EQ_FILTER_COPY) {
                const eq_biquad_t *matrix = &state->active[operation];
                float next_left = matrix->b0 * left + matrix->b1 * right;
                float next_right = matrix->b2 * left + matrix->a1 * right;
                left = next_left;
                right = next_right;
            } else {
                uint8_t channel_mask = state->active_band_enabled[operation];
                if (channel_mask & EQ_CHANNEL_LEFT_MASK) {
                    left = process_biquad(&state->active[operation], &state->band_z[0][operation], left);
                }
                if (channel_mask & EQ_CHANNEL_RIGHT_MASK) {
                    right = process_biquad(&state->active[operation], &state->band_z[1][operation], right);
                }
            }
        }

        {
            int16_t out_l = limit_i16(left, clip_counter);
            int16_t out_r = limit_i16(right, clip_counter);
            uint16_t abs_l = abs_i16_peak(out_l);
            uint16_t abs_r = abs_i16_peak(out_r);

            output[0] = out_l;
            output[1] = out_r;
            if (abs_l > max_l) max_l = abs_l;
            if (abs_r > max_r) max_r = abs_r;
            input += 2;
            output += 2;
        }
    }

    if (peak_l) *peak_l = max_l;
    if (peak_r) *peak_r = max_r;
}

static void process_generic_steady(eq_dsp_state_t *state, const int16_t *input, int16_t *output,
                                   uint32_t frames, uint32_t channels, int32_t *clip_counter,
                                   uint16_t *peak_l, uint16_t *peak_r) {
    const float preamp = state->preamp;
    uint16_t max_l = 0;
    uint16_t max_r = 0;

    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            int32_t idx = (frame * channels) + ch;
            float sample = (float)input[idx];

            if (state->hpf_enabled) {
                sample = process_biquad(&state->hpf, &state->hpf_z[ch], sample);
            }

            sample *= preamp;

            for (uint8_t operation = 0; operation < state->active_operation_count; ++operation) {
                if (state->active_operation_type[operation] == EQ_FILTER_COPY) {
                    sample *= state->active[operation].b0 + state->active[operation].b1;
                } else if (state->active_band_enabled[operation] & EQ_CHANNEL_LEFT_MASK) {
                    sample = process_biquad(&state->active[operation], &state->band_z[ch][operation], sample);
                }
            }

            int16_t out_val = limit_i16(sample, clip_counter);
            uint16_t abs_val = abs_i16_peak(out_val);
            output[idx] = out_val;

            if (ch == 0) {
                if (abs_val > max_l) max_l = abs_val;
            } else if (ch == 1) {
                if (abs_val > max_r) max_r = abs_val;
            }
        }
    }

    if (peak_l) *peak_l = max_l;
    if (peak_r) *peak_r = max_r;
}

void eq_dsp_apply_to(eq_dsp_state_t *state, const int16_t *input, int16_t *output, uint32_t frames, uint32_t channels, int32_t *clip_counter, uint16_t *peak_l, uint16_t *peak_r) {
    if (!state || !input || !output || channels < 1 || channels > EQ_DSP_MAX_CHANNELS) { return; }

    sanitize_preamp_state(state);
    sanitize_smoothing_state(state);
    sanitize_biquad_state(state);
    flush_denormal_delay_state(state, channels);

    if (state->smooth_remaining == 0) {
        if (channels == 2) {
            process_stereo_steady(state, input, output, frames, clip_counter, peak_l, peak_r);
        } else {
            process_generic_steady(state, input, output, frames, channels, clip_counter, peak_l, peak_r);
        }

        flush_denormal_delay_state(state, channels);
        return;
    }

    eq_biquad_t smooth_band[EQ_PARAMETRIC_FILTERS];
    uint8_t smooth_band_enabled[EQ_PARAMETRIC_FILTERS];
    uint16_t max_l = 0;
    uint16_t max_r = 0;

    for (uint32_t i = 0; i < frames; ++i) {
        float sample[EQ_DSP_MAX_CHANNELS] = {0.0f, 0.0f};
        float t = 1.0f;
        int smoothing_now = (state->smooth_remaining > 0);

        if (smoothing_now) {
            t = 1.0f - ((float)state->smooth_remaining / (float)EQ_SMOOTH_SAMPLES);
            memset(smooth_band_enabled, 0, sizeof(smooth_band_enabled));
            for (int operation = 0; operation < state->active_operation_count; ++operation) {
                if (state->active_operation_type[operation] == EQ_FILTER_COPY) {
                    lerp_biquad(&state->active[operation], &state->target[operation], t, &smooth_band[operation]);
                } else if (state->active_band_enabled[operation] || state->target_band_enabled[operation]) {
                    lerp_biquad(&state->active[operation], &state->target[operation], t, &smooth_band[operation]);
                    smooth_band_enabled[operation] = state->active_band_enabled[operation] |
                                                     state->target_band_enabled[operation];
                }
            }
        }

        {
            float preamp = smoothing_now
            ? lerp(state->preamp, state->target_preamp, t)
            : state->preamp;
            for (uint32_t ch = 0; ch < channels; ++ch) {
                sample[ch] = (float)input[(i * channels) + ch];
                if (state->hpf_enabled) {
                    sample[ch] = process_biquad(&state->hpf, &state->hpf_z[ch], sample[ch]);
                }
                sample[ch] *= preamp;
            }
        }

        for (uint8_t operation = 0; operation < state->active_operation_count; ++operation) {
            if (state->active_operation_type[operation] == EQ_FILTER_COPY) {
                const eq_biquad_t *matrix = smoothing_now ? &smooth_band[operation] : &state->active[operation];
                float left = matrix->b0 * sample[0] +
                    matrix->b1 * (channels > 1 ? sample[1] : sample[0]);
                if (channels > 1) {
                    float right = matrix->b2 * sample[0] + matrix->a1 * sample[1];
                    sample[1] = right;
                }
                sample[0] = left;
            } else {
                uint8_t channel_mask = smoothing_now ? smooth_band_enabled[operation] :
                                                       state->active_band_enabled[operation];
                const eq_biquad_t *coefficients = smoothing_now ? &smooth_band[operation] :
                                                                  &state->active[operation];
                for (uint32_t ch = 0; ch < channels; ++ch) {
                    if (channel_mask & (uint8_t)(1u << ch)) {
                        sample[ch] = process_biquad(coefficients, &state->band_z[ch][operation], sample[ch]);
                    }
                }
            }
        }

        for (uint32_t ch = 0; ch < channels; ++ch) {
            int32_t idx = (i * channels) + ch;
            int16_t out_val = limit_i16(sample[ch], clip_counter);
            uint16_t abs_val = abs_i16_peak(out_val);
            output[idx] = out_val;
            if (ch == 0) {
                if (abs_val > max_l) max_l = abs_val;
            } else if (ch == 1) {
                if (abs_val > max_r) max_r = abs_val;
            }
        }

        if (state->smooth_remaining > 0) {
            state->smooth_remaining--;
            if (state->smooth_remaining == 0) {
                for (int b = 0; b < EQ_PARAMETRIC_FILTERS; ++b) {
                    state->active[b] = state->target[b];
                    state->active_band_enabled[b] = state->target_band_enabled[b];
                    state->active_operation_type[b] = state->target_operation_type[b];
                    if (!state->active_band_enabled[b]) {
                        reset_band_delay_state(state, b);
                    } else {
                        for (uint32_t ch = 0; ch < channels; ++ch) {
                            if (!(state->active_band_enabled[b] & (uint8_t)(1u << ch))) {
                                state->band_z[ch][b].z1 = 0.0f;
                                state->band_z[ch][b].z2 = 0.0f;
                            }
                        }
                    }
                }
                memcpy(state->active_band_index, state->target_band_index, sizeof(state->active_band_index));
                state->active_band_count = state->target_band_count;
                state->active_operation_count = state->target_operation_count;
                state->preamp = state->target_preamp;
            }
        }
    }

    flush_denormal_delay_state(state, channels);
    
    if (peak_l) *peak_l = max_l;
    if (peak_r) *peak_r = max_r;
}

void eq_dsp_apply(eq_dsp_state_t *state, int16_t *pcm, uint32_t frames, uint32_t channels, int32_t *clip_counter, uint16_t *peak_l, uint16_t *peak_r) {
    eq_dsp_apply_to(state, pcm, pcm, frames, channels, clip_counter, peak_l, peak_r);
}

uint32_t eq_dsp_active_band_count(const eq_dsp_state_t *state) {
    if (!state) {
        return 0;
    }

    return state->active_band_count;
}
