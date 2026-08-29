#include "dsp.h"

#include <math.h>
#include <string.h>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#define EQVITA_CORTEX_A9_NEON 1
#else
#define EQVITA_CORTEX_A9_NEON 0
#endif

#define EQ_Q_VALUE 0.707f
#define EQ_PI 3.14159265358979323846f

static int g_errno_stub;
int *__errno(void) { return &g_errno_stub; }

#if defined(EQVITA_DSP_TEST_API)
static int g_neon_enabled_for_tests = 1;

void eq_dsp_set_neon_enabled_for_tests(int enabled) {
    g_neon_enabled_for_tests = enabled ? 1 : 0;
}
#endif

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
    memset(&state->hpf_z, 0, sizeof(state->hpf_z));
}

static void reset_hpf_delay_state(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

    memset(&state->hpf_z, 0, sizeof(state->hpf_z));
}

static void reset_band_delay_state(eq_dsp_state_t *state, int band) {
    if (!state || band < 0 || band >= EQ_PARAMETRIC_FILTERS) {
        return;
    }

    memset(&state->band_z[band], 0, sizeof(state->band_z[band]));
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

static void rebuild_active_stereo_coefficients(eq_dsp_state_t *state) {
    if (!state) {
        return;
    }

    for (int operation = 0; operation < EQ_PARAMETRIC_FILTERS; ++operation) {
        const eq_biquad_t *source = &state->active[operation];
        eq_stereo_biquad_t *packed = &state->active_stereo[operation];
        uint8_t channel_mask = state->active_band_enabled[operation];

        if (state->active_operation_type[operation] == EQ_FILTER_COPY) {
            packed->b0[0] = source->b0;
            packed->b0[1] = source->b2;
            packed->b1[0] = source->b1;
            packed->b1[1] = source->a1;
            continue;
        }

        for (int channel = 0; channel < EQ_DSP_MAX_CHANNELS; ++channel) {
            if (channel_mask & (uint8_t)(1u << channel)) {
                packed->b0[channel] = source->b0;
                packed->b1[channel] = source->b1;
                packed->b2[channel] = source->b2;
                packed->a1[channel] = source->a1;
                packed->a2[channel] = source->a2;
            } else {
                packed->b0[channel] = 1.0f;
                packed->b1[channel] = 0.0f;
                packed->b2[channel] = 0.0f;
                packed->a1[channel] = 0.0f;
                packed->a2[channel] = 0.0f;
            }
        }
    }
}

static int sanitize_biquad_state(eq_dsp_state_t *state) {
    int active_changed = 0;

    if (!state) {
        return 0;
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
            active_changed = 1;
        }
    }

    rebuild_dsp_band_indexes(state);
    return active_changed;
}

static void sanitize_delay_state(eq_dsp_state_t *state, uint32_t channels) {
    if (!state) {
        return;
    }
    if (channels > EQ_DSP_MAX_CHANNELS) {
        channels = EQ_DSP_MAX_CHANNELS;
    }

    for (uint32_t channel = 0; channel < channels; ++channel) {
        float *hpf_z1 = &state->hpf_z.z1[channel];
        float *hpf_z2 = &state->hpf_z.z2[channel];
        if (!isfinite(*hpf_z1) || fabsf(*hpf_z1) < 1e-15f) *hpf_z1 = 0.0f;
        if (!isfinite(*hpf_z2) || fabsf(*hpf_z2) < 1e-15f) *hpf_z2 = 0.0f;

        for (uint8_t operation = 0; operation < EQ_PARAMETRIC_FILTERS; ++operation) {
            float *z1 = &state->band_z[operation].z1[channel];
            float *z2 = &state->band_z[operation].z2[channel];
            if (!isfinite(*z1) || fabsf(*z1) < 1e-15f) *z1 = 0.0f;
            if (!isfinite(*z2) || fabsf(*z2) < 1e-15f) *z2 = 0.0f;
        }
    }
}

static inline float soft_limit_output_sample(float x, int32_t *clip_counter) {
    const float positive_limit = 32767.0f;
    const float negative_limit = 32768.0f;
    const float knee = 32600.0f;
    float sign = 1.0f;
    float ax = x;
    float limit = positive_limit;

    if (!isfinite(x)) {
        if (clip_counter) (*clip_counter)++;
        return 0.0f;
    }
    if (x < 0.0f) {
        sign = -1.0f;
        ax = -x;
        limit = negative_limit;
    }
    if (ax <= limit) {
        return x;
    }

    if (clip_counter) (*clip_counter)++;
    {
        float over = ax - knee;
        float knee_range = limit - knee;
        return sign * (knee + (knee_range * over) / (over + knee_range));
    }
}

static inline float hard_clip_output_sample(float x, int32_t *clip_counter) {
    if (!isfinite(x)) {
        if (clip_counter) (*clip_counter)++;
        return 0.0f;
    }
    if (x > 32767.0f) {
        if (clip_counter) (*clip_counter)++;
        return 32767.0f;
    }
    if (x < -32768.0f) {
        if (clip_counter) (*clip_counter)++;
        return -32768.0f;
    }
    return x;
}

static inline int16_t limit_output_i16(float x,
                                       eq_dsp_output_limit_t output_limit,
                                       int32_t *clip_counter) {
    x = output_limit == EQ_DSP_OUTPUT_HARD_CLIP
        ? hard_clip_output_sample(x, clip_counter)
        : soft_limit_output_sample(x, clip_counter);
    x = (x >= 0.0f) ? (x + 0.5f) : (x - 0.5f);
    if (x > 32767.0f) return 32767;
    if (x < -32768.0f) return -32768;
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
    rebuild_active_stereo_coefficients(state);
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
    if (sanitize_biquad_state(state)) {
        rebuild_active_stereo_coefficients(state);
    }
    sanitize_delay_state(state, EQ_DSP_MAX_CHANNELS);
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
    rebuild_active_stereo_coefficients(state);
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

static inline float process_biquad_channel(const eq_biquad_t *c,
                                           eq_stereo_biquad_delay_t *z,
                                           uint32_t channel,
                                           float x) {
    float y = c->b0 * x + z->z1[channel];
    z->z1[channel] = z->z2[channel] + c->b1 * x - c->a1 * y;
    z->z2[channel] = c->b2 * x - c->a2 * y;
    return y;
}

#if !EQVITA_CORTEX_A9_NEON || defined(EQVITA_DSP_TEST_API)
static void process_stereo_steady(eq_dsp_state_t *state, const int16_t *input, int16_t *output,
                                  uint32_t frames, eq_dsp_output_limit_t output_limit,
                                  int32_t *clip_counter,
                                  uint16_t *peak_l, uint16_t *peak_r) {
    const float preamp = state->preamp;
    uint16_t max_l = 0;
    uint16_t max_r = 0;

    for (uint32_t frame = 0; frame < frames; ++frame) {
        float left = (float)input[0];
        float right = (float)input[1];

        if (state->hpf_enabled) {
            left = process_biquad_channel(&state->hpf, &state->hpf_z, 0, left) * preamp;
            right = process_biquad_channel(&state->hpf, &state->hpf_z, 1, right) * preamp;
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
                    left = process_biquad_channel(&state->active[operation], &state->band_z[operation], 0, left);
                }
                if (channel_mask & EQ_CHANNEL_RIGHT_MASK) {
                    right = process_biquad_channel(&state->active[operation], &state->band_z[operation], 1, right);
                }
            }
        }

        {
            int16_t out_l = limit_output_i16(left, output_limit, clip_counter);
            int16_t out_r = limit_output_i16(right, output_limit, clip_counter);
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
#endif

#if EQVITA_CORTEX_A9_NEON
static inline __attribute__((always_inline)) void process_stereo_steady_neon_core(
                                       eq_dsp_state_t *state, const int16_t *input, int16_t *output,
                                       uint32_t frames, eq_dsp_output_limit_t output_limit,
                                       int32_t *clip_counter,
                                       uint16_t *peak_l, uint16_t *peak_r) {
    const float32x2_t lower = vdup_n_f32(-32768.0f);
    const float32x2_t upper = vdup_n_f32(32767.0f);
    const float32x4_t lower_q = vdupq_n_f32(-32768.0f);
    const float32x4_t upper_q = vdupq_n_f32(32767.0f);
    const float32x2_t preamp = vdup_n_f32(state->preamp);
    float32x2_t peak = vdup_n_f32(0.0f);
    uint32x4_t clip_count = vdupq_n_u32(0u);

#define EQ_NEON_DF2T_STEP(SAMPLE) do { \
        float32x2_t eq_y = vmla_f32(z1, (SAMPLE), b0); \
        z1 = vmls_f32(vmla_f32(z2, (SAMPLE), b1), eq_y, a1); \
        z2 = vmls_f32(vmul_f32((SAMPLE), b2), eq_y, a2); \
        (SAMPLE) = eq_y; \
    } while (0)

#define EQ_NEON_COPY_STEP(SAMPLE) do { \
        float32x2_t eq_copy_input = (SAMPLE); \
        float32x2_t eq_copy_output = vmul_lane_f32(column_l, eq_copy_input, 0); \
        (SAMPLE) = vmla_lane_f32(eq_copy_output, column_r, eq_copy_input, 1); \
    } while (0)

#define EQ_NEON_HARD_CLIP_FOUR(SAMPLE0, SAMPLE1, SAMPLE2, SAMPLE3) do { \
        float32x4_t eq_samples0 = vcombine_f32((SAMPLE0), (SAMPLE1)); \
        float32x4_t eq_samples1 = vcombine_f32((SAMPLE2), (SAMPLE3)); \
        float32x4_t eq_original0 = eq_samples0; \
        float32x4_t eq_original1 = eq_samples1; \
        eq_samples0 = vmaxq_f32(eq_samples0, lower_q); \
        eq_samples0 = vminq_f32(eq_samples0, upper_q); \
        eq_samples1 = vmaxq_f32(eq_samples1, lower_q); \
        eq_samples1 = vminq_f32(eq_samples1, upper_q); \
        clip_count = vsubq_u32(clip_count, vmvnq_u32(vceqq_f32(eq_samples0, eq_original0))); \
        clip_count = vsubq_u32(clip_count, vmvnq_u32(vceqq_f32(eq_samples1, eq_original1))); \
        (SAMPLE0) = vget_low_f32(eq_samples0); \
        (SAMPLE1) = vget_high_f32(eq_samples0); \
        (SAMPLE2) = vget_low_f32(eq_samples1); \
        (SAMPLE3) = vget_high_f32(eq_samples1); \
        peak = vmax_f32(peak, vabs_f32((SAMPLE0))); \
        peak = vmax_f32(peak, vabs_f32((SAMPLE1))); \
        peak = vmax_f32(peak, vabs_f32((SAMPLE2))); \
        peak = vmax_f32(peak, vabs_f32((SAMPLE3))); \
    } while (0)

#define EQ_NEON_HARD_CLIP_PAIR(SAMPLE) do { \
        float32x4_t eq_samples = vcombine_f32((SAMPLE), vdup_n_f32(0.0f)); \
        float32x4_t eq_original = eq_samples; \
        eq_samples = vmaxq_f32(eq_samples, lower_q); \
        eq_samples = vminq_f32(eq_samples, upper_q); \
        clip_count = vsubq_u32(clip_count, vmvnq_u32(vceqq_f32(eq_samples, eq_original))); \
        (SAMPLE) = vget_low_f32(eq_samples); \
        peak = vmax_f32(peak, vabs_f32((SAMPLE))); \
    } while (0)

#define EQ_NEON_SOFT_LIMIT_PAIR(SAMPLE) do { \
        uint32x2_t eq_over = vorr_u32(vcgt_f32((SAMPLE), upper), vclt_f32((SAMPLE), lower)); \
        eq_over = vorr_u32(eq_over, vmvn_u32(vceq_f32((SAMPLE), (SAMPLE)))); \
        if (vget_lane_u32(eq_over, 0) || vget_lane_u32(eq_over, 1)) { \
            float eq_lanes[2]; \
            vst1_f32(eq_lanes, (SAMPLE)); \
            eq_lanes[0] = soft_limit_output_sample(eq_lanes[0], clip_counter); \
            eq_lanes[1] = soft_limit_output_sample(eq_lanes[1], clip_counter); \
            (SAMPLE) = vld1_f32(eq_lanes); \
        } \
        peak = vmax_f32(peak, vabs_f32((SAMPLE))); \
    } while (0)

#define EQ_VFP_ROUND_PAIR(SAMPLE, RESULT) do { \
        __asm__ volatile( \
            "vcvtr.s32.f32 %0, %0\n\t" \
            "vcvtr.s32.f32 %p0, %p0" \
            : "+t"(SAMPLE)); \
        (RESULT) = vreinterpret_s32_f32(SAMPLE); \
    } while (0)

    while (frames >= 4u) {
        int16x8_t packed0 = vld1q_s16(input);
        float32x4_t q0 = vcvtq_f32_s32(vmovl_s16(vget_low_s16(packed0)));
        float32x4_t q1 = vcvtq_f32_s32(vmovl_s16(vget_high_s16(packed0)));
        float32x2_t x0 = vget_low_f32(q0);
        float32x2_t x1 = vget_high_f32(q0);
        float32x2_t x2 = vget_low_f32(q1);
        float32x2_t x3 = vget_high_f32(q1);

        if (state->hpf_enabled) {
            const eq_biquad_t *coefficients = &state->hpf;
            float32x2_t b0 = vdup_n_f32(coefficients->b0);
            float32x2_t b1 = vdup_n_f32(coefficients->b1);
            float32x2_t b2 = vdup_n_f32(coefficients->b2);
            float32x2_t a1 = vdup_n_f32(coefficients->a1);
            float32x2_t a2 = vdup_n_f32(coefficients->a2);
            float32x2_t z1 = vld1_f32(state->hpf_z.z1);
            float32x2_t z2 = vld1_f32(state->hpf_z.z2);

            EQ_NEON_DF2T_STEP(x0);
            EQ_NEON_DF2T_STEP(x1);
            EQ_NEON_DF2T_STEP(x2);
            EQ_NEON_DF2T_STEP(x3);

            vst1_f32(state->hpf_z.z1, z1);
            vst1_f32(state->hpf_z.z2, z2);
        }

        x0 = vmul_f32(x0, preamp);
        x1 = vmul_f32(x1, preamp);
        x2 = vmul_f32(x2, preamp);
        x3 = vmul_f32(x3, preamp);

        for (uint8_t operation = 0; operation < state->active_operation_count; ++operation) {
            if (state->active_operation_type[operation] == EQ_FILTER_COPY) {
                const eq_stereo_biquad_t *matrix = &state->active_stereo[operation];
                float32x2_t column_l = vld1_f32(matrix->b0);
                float32x2_t column_r = vld1_f32(matrix->b1);

                EQ_NEON_COPY_STEP(x0);
                EQ_NEON_COPY_STEP(x1);
                EQ_NEON_COPY_STEP(x2);
                EQ_NEON_COPY_STEP(x3);
            } else if (state->active_band_enabled[operation] != 0) {
                const eq_stereo_biquad_t *coefficients = &state->active_stereo[operation];
                float32x2_t b0 = vld1_f32(coefficients->b0);
                float32x2_t b1 = vld1_f32(coefficients->b1);
                float32x2_t b2 = vld1_f32(coefficients->b2);
                float32x2_t a1 = vld1_f32(coefficients->a1);
                float32x2_t a2 = vld1_f32(coefficients->a2);
                float32x2_t z1 = vld1_f32(state->band_z[operation].z1);
                float32x2_t z2 = vld1_f32(state->band_z[operation].z2);

                EQ_NEON_DF2T_STEP(x0);
                EQ_NEON_DF2T_STEP(x1);
                EQ_NEON_DF2T_STEP(x2);
                EQ_NEON_DF2T_STEP(x3);

                vst1_f32(state->band_z[operation].z1, z1);
                vst1_f32(state->band_z[operation].z2, z2);
            }
        }

        {
            int32x2_t rounded0;
            int32x2_t rounded1;
            int32x2_t rounded2;
            int32x2_t rounded3;
            int16x4_t packed0;
            int16x4_t packed1;

            if (output_limit == EQ_DSP_OUTPUT_HARD_CLIP) {
                EQ_NEON_HARD_CLIP_FOUR(x0, x1, x2, x3);
            } else {
                EQ_NEON_SOFT_LIMIT_PAIR(x0);
                EQ_NEON_SOFT_LIMIT_PAIR(x1);
                EQ_NEON_SOFT_LIMIT_PAIR(x2);
                EQ_NEON_SOFT_LIMIT_PAIR(x3);
            }

            EQ_VFP_ROUND_PAIR(x0, rounded0);
            EQ_VFP_ROUND_PAIR(x1, rounded1);
            EQ_VFP_ROUND_PAIR(x2, rounded2);
            EQ_VFP_ROUND_PAIR(x3, rounded3);

            packed0 = vmovn_s32(vcombine_s32(rounded0, rounded1));
            packed1 = vmovn_s32(vcombine_s32(rounded2, rounded3));
            vst1q_s16(output, vcombine_s16(packed0, packed1));
        }

        input += 8;
        output += 8;
        frames -= 4u;
    }

    while (frames > 0u) {
        int16x4_t packed = vdup_n_s16(input[0]);
        float32x2_t x;

        packed = vset_lane_s16(input[1], packed, 1);
        x = vget_low_f32(vcvtq_f32_s32(vmovl_s16(packed)));

        if (state->hpf_enabled) {
            const eq_biquad_t *coefficients = &state->hpf;
            float32x2_t b0 = vdup_n_f32(coefficients->b0);
            float32x2_t b1 = vdup_n_f32(coefficients->b1);
            float32x2_t b2 = vdup_n_f32(coefficients->b2);
            float32x2_t a1 = vdup_n_f32(coefficients->a1);
            float32x2_t a2 = vdup_n_f32(coefficients->a2);
            float32x2_t z1 = vld1_f32(state->hpf_z.z1);
            float32x2_t z2 = vld1_f32(state->hpf_z.z2);

            EQ_NEON_DF2T_STEP(x);

            vst1_f32(state->hpf_z.z1, z1);
            vst1_f32(state->hpf_z.z2, z2);
        }

        x = vmul_f32(x, preamp);
        for (uint8_t operation = 0; operation < state->active_operation_count; ++operation) {
            if (state->active_operation_type[operation] == EQ_FILTER_COPY) {
                const eq_stereo_biquad_t *matrix = &state->active_stereo[operation];
                float32x2_t column_l = vld1_f32(matrix->b0);
                float32x2_t column_r = vld1_f32(matrix->b1);
                EQ_NEON_COPY_STEP(x);
            } else if (state->active_band_enabled[operation] != 0) {
                const eq_stereo_biquad_t *coefficients = &state->active_stereo[operation];
                float32x2_t b0 = vld1_f32(coefficients->b0);
                float32x2_t b1 = vld1_f32(coefficients->b1);
                float32x2_t b2 = vld1_f32(coefficients->b2);
                float32x2_t a1 = vld1_f32(coefficients->a1);
                float32x2_t a2 = vld1_f32(coefficients->a2);
                float32x2_t z1 = vld1_f32(state->band_z[operation].z1);
                float32x2_t z2 = vld1_f32(state->band_z[operation].z2);

                EQ_NEON_DF2T_STEP(x);

                vst1_f32(state->band_z[operation].z1, z1);
                vst1_f32(state->band_z[operation].z2, z2);
            }
        }

        {
            int32x2_t rounded;
            int16x4_t packed;

            if (output_limit == EQ_DSP_OUTPUT_HARD_CLIP) {
                EQ_NEON_HARD_CLIP_PAIR(x);
            } else {
                EQ_NEON_SOFT_LIMIT_PAIR(x);
            }
            EQ_VFP_ROUND_PAIR(x, rounded);
            packed = vmovn_s32(vcombine_s32(rounded, rounded));
            vst1_lane_s16(output, packed, 0);
            vst1_lane_s16(output + 1, packed, 1);
        }
        input += 2;
        output += 2;
        frames--;
    }

#undef EQ_VFP_ROUND_PAIR
#undef EQ_NEON_SOFT_LIMIT_PAIR
#undef EQ_NEON_HARD_CLIP_PAIR
#undef EQ_NEON_HARD_CLIP_FOUR
#undef EQ_NEON_COPY_STEP
#undef EQ_NEON_DF2T_STEP

    {
        int32x2_t rounded_peak;

        __asm__ volatile(
            "vcvtr.s32.f32 %0, %0\n\t"
            "vcvtr.s32.f32 %p0, %p0"
            : "+t"(peak));
        rounded_peak = vreinterpret_s32_f32(peak);
        if (peak_l) *peak_l = (uint16_t)vget_lane_s32(rounded_peak, 0);
        if (peak_r) *peak_r = (uint16_t)vget_lane_s32(rounded_peak, 1);
    }

    if (output_limit == EQ_DSP_OUTPUT_HARD_CLIP && clip_counter) {
        uint32_t clip_lanes[4];
        vst1q_u32(clip_lanes, clip_count);
        *clip_counter += (int32_t)(clip_lanes[0] + clip_lanes[1] +
                                   clip_lanes[2] + clip_lanes[3]);
    }

}

static __attribute__((noinline, noclone)) void process_stereo_steady_neon_hard_clip(
                                       eq_dsp_state_t *state, const int16_t *input, int16_t *output,
                                       uint32_t frames, int32_t *clip_counter,
                                       uint16_t *peak_l, uint16_t *peak_r) {
    process_stereo_steady_neon_core(state, input, output, frames,
                                    EQ_DSP_OUTPUT_HARD_CLIP, clip_counter,
                                    peak_l, peak_r);
}

static __attribute__((noinline, noclone)) void process_stereo_steady_neon_soft_limit(
                                       eq_dsp_state_t *state, const int16_t *input, int16_t *output,
                                       uint32_t frames, int32_t *clip_counter,
                                       uint16_t *peak_l, uint16_t *peak_r) {
    process_stereo_steady_neon_core(state, input, output, frames,
                                    EQ_DSP_OUTPUT_SOFT_LIMIT, clip_counter,
                                    peak_l, peak_r);
}
#endif

static void process_generic_steady(eq_dsp_state_t *state, const int16_t *input, int16_t *output,
                                   uint32_t frames, uint32_t channels,
                                   eq_dsp_output_limit_t output_limit, int32_t *clip_counter,
                                   uint16_t *peak_l, uint16_t *peak_r) {
    const float preamp = state->preamp;
    uint16_t max_l = 0;
    uint16_t max_r = 0;

    for (uint32_t frame = 0; frame < frames; ++frame) {
        for (uint32_t ch = 0; ch < channels; ++ch) {
            int32_t idx = (frame * channels) + ch;
            float sample = (float)input[idx];

            if (state->hpf_enabled) {
                sample = process_biquad_channel(&state->hpf, &state->hpf_z, ch, sample);
            }

            sample *= preamp;

            for (uint8_t operation = 0; operation < state->active_operation_count; ++operation) {
                if (state->active_operation_type[operation] == EQ_FILTER_COPY) {
                    sample *= state->active[operation].b0 + state->active[operation].b1;
                } else if (state->active_band_enabled[operation] & EQ_CHANNEL_LEFT_MASK) {
                    sample = process_biquad_channel(&state->active[operation], &state->band_z[operation], ch, sample);
                }
            }

            int16_t out_val = limit_output_i16(sample, output_limit, clip_counter);
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

void eq_dsp_apply_to(eq_dsp_state_t *state, const int16_t *input, int16_t *output, uint32_t frames, uint32_t channels, eq_dsp_output_limit_t output_limit, int32_t *clip_counter, uint16_t *peak_l, uint16_t *peak_r) {
#if EQVITA_CORTEX_A9_NEON
    uint32_t saved_fpscr;
    uint32_t processing_fpscr;
#endif

    if (!state || !input || !output || channels < 1 || channels > EQ_DSP_MAX_CHANNELS) { return; }
    if (output_limit != EQ_DSP_OUTPUT_HARD_CLIP) {
        output_limit = EQ_DSP_OUTPUT_SOFT_LIMIT;
    }

#if EQVITA_CORTEX_A9_NEON
    /*
     * VCVTR uses FPSCR.RMode. Run every Vita DSP path with nearest-even
     * rounding and scalar VFP flush-to-zero, then restore the caller's FPSCR.
     * Cortex-A9 Advanced SIMD arithmetic is flush-to-zero independently.
     */
    __asm__ volatile("vmrs %0, fpscr" : "=r"(saved_fpscr));
    processing_fpscr = (saved_fpscr & ~(3u << 22)) | (1u << 24);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(processing_fpscr) : "memory");
#endif

    if (state->smooth_remaining == 0) {
        if (channels == 2) {
#if EQVITA_CORTEX_A9_NEON
#if defined(EQVITA_DSP_TEST_API)
            if (g_neon_enabled_for_tests) {
                if (output_limit == EQ_DSP_OUTPUT_HARD_CLIP) {
                    process_stereo_steady_neon_hard_clip(state, input, output, frames, clip_counter, peak_l, peak_r);
                } else {
                    process_stereo_steady_neon_soft_limit(state, input, output, frames, clip_counter, peak_l, peak_r);
                }
            } else {
                process_stereo_steady(state, input, output, frames, output_limit, clip_counter, peak_l, peak_r);
            }
#else
            if (output_limit == EQ_DSP_OUTPUT_HARD_CLIP) {
                process_stereo_steady_neon_hard_clip(state, input, output, frames, clip_counter, peak_l, peak_r);
            } else {
                process_stereo_steady_neon_soft_limit(state, input, output, frames, clip_counter, peak_l, peak_r);
            }
#endif
#else
            process_stereo_steady(state, input, output, frames, output_limit, clip_counter, peak_l, peak_r);
#endif
        } else {
            process_generic_steady(state, input, output, frames, channels, output_limit, clip_counter, peak_l, peak_r);
        }

#if EQVITA_CORTEX_A9_NEON
        __asm__ volatile("vmsr fpscr, %0" : : "r"(saved_fpscr) : "memory");
#endif
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
                    sample[ch] = process_biquad_channel(&state->hpf, &state->hpf_z, ch, sample[ch]);
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
                        sample[ch] = process_biquad_channel(coefficients, &state->band_z[operation], ch, sample[ch]);
                    }
                }
            }
        }

        for (uint32_t ch = 0; ch < channels; ++ch) {
            int32_t idx = (i * channels) + ch;
            int16_t out_val = limit_output_i16(sample[ch], output_limit, clip_counter);
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
                                state->band_z[b].z1[ch] = 0.0f;
                                state->band_z[b].z2[ch] = 0.0f;
                            }
                        }
                    }
                }
                memcpy(state->active_band_index, state->target_band_index, sizeof(state->active_band_index));
                state->active_band_count = state->target_band_count;
                state->active_operation_count = state->target_operation_count;
                state->preamp = state->target_preamp;
                rebuild_active_stereo_coefficients(state);
            }
        }
    }

#if EQVITA_CORTEX_A9_NEON
    __asm__ volatile("vmsr fpscr, %0" : : "r"(saved_fpscr) : "memory");
#endif

    if (peak_l) *peak_l = max_l;
    if (peak_r) *peak_r = max_r;
}

void eq_dsp_apply(eq_dsp_state_t *state, int16_t *pcm, uint32_t frames, uint32_t channels, eq_dsp_output_limit_t output_limit, int32_t *clip_counter, uint16_t *peak_l, uint16_t *peak_r) {
    eq_dsp_apply_to(state, pcm, pcm, frames, channels, output_limit, clip_counter, peak_l, peak_r);
}

uint32_t eq_dsp_active_band_count(const eq_dsp_state_t *state) {
    if (!state) {
        return 0;
    }

    return state->active_band_count;
}
