#include "dsp.h"

#include <math.h>
#include <string.h>

#define EQ_Q_VALUE 0.707f
#define EQ_PI 3.14159265358979323846f

static int g_errno_stub;
int *__errno(void) { return &g_errno_stub; }

static inline float mdB_to_gain(int32_t mdB) {
    // mdB is in millibels (1000 mdB = 1 dB)
    return powf(10.0f, ((float)mdB) / 20000.0f);
}

static void biquad_compute(eq_biquad_t *out, uint32_t sample_rate, uint32_t freq, float gain) {
    // Clamp frequency to Nyquist - margin to avoid instability
    if (sample_rate > 0 && freq >= (sample_rate / 2)) {
        freq = (sample_rate / 2) - 100;
    }

    float omega = 2.0f * EQ_PI * ((float)freq) / (float)sample_rate;
    float sn = sinf(omega);
    float cs = cosf(omega);
    float alpha = sn / (2.0f * EQ_Q_VALUE);
    
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

static void biquad_highpass(eq_biquad_t *out, uint32_t sample_rate, uint32_t freq) {
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

static inline float soft_clip(float x) {
    // Normalize to -1.0 .. 1.0 range (32768.0f)
    float norm = x / 32768.0f;
    float out = tanhf(norm);
    return out * 32768.0f;
}

void eq_dsp_init(eq_dsp_state_t *state, uint32_t sample_rate) {
    memset(state, 0, sizeof(*state));
    state->sample_rate = sample_rate;
    state->preamp = mdB_to_gain(EQ_DEFAULT_PREAMP_MDB);
    state->target_preamp = state->preamp;
    state->hpf_enabled = 0;
    
    for (int i = 0; i < EQ_BANDS; ++i) {
        biquad_compute(&state->active[i], sample_rate ? sample_rate : 44100, eq_band_frequencies[i], 1.0f);
        state->target[i] = state->active[i];
        state->active[i].z1 = 0.0f;
        state->active[i].z2 = 0.0f;
        state->target[i].z1 = 0.0f;
        state->target[i].z2 = 0.0f;
    }
    
    // Init HPF at 70Hz
    biquad_highpass(&state->hpf, sample_rate ? sample_rate : 44100, 70);
    state->hpf.z1 = 0.0f;
    state->hpf.z2 = 0.0f;
}

void eq_dsp_set_targets(eq_dsp_state_t *state, uint32_t sample_rate, const int32_t *band_mdB, int32_t preamp_mdB, int hpf_enabled) {
    if (sample_rate != state->sample_rate && sample_rate != 0) {
        state->sample_rate = sample_rate;
        // Re-init HPF if sample rate changes
        biquad_highpass(&state->hpf, state->sample_rate, 70);
    }

    for (int i = 0; i < EQ_BANDS; ++i) {
        float gain = mdB_to_gain(band_mdB ? band_mdB[i] : 0);
        biquad_compute(&state->target[i], state->sample_rate, eq_band_frequencies[i], gain);
    }

    state->target_preamp = mdB_to_gain(preamp_mdB);
    state->smooth_remaining = EQ_SMOOTH_SAMPLES;
    state->hpf_enabled = hpf_enabled;
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
    out->z1 = a->z1;
    out->z2 = a->z2;
}

static inline float process_biquad(eq_biquad_t *s, float x) {
    float y = s->b0 * x + s->z1;
    s->z1 = s->b1 * x - s->a1 * y + s->z2;
    s->z2 = s->b2 * x - s->a2 * y;
    
    // Flush denormals to zero to avoid performance penalty and potential instability
    if (fabsf(s->z1) < 1e-15f) s->z1 = 0.0f;
    if (fabsf(s->z2) < 1e-15f) s->z2 = 0.0f;
    
    return y;
}

void eq_dsp_apply(eq_dsp_state_t *state, int16_t *pcm, uint32_t frames, uint32_t channels, int32_t *clip_counter, int16_t *peak_l, int16_t *peak_r) {
    if (channels < 1) { return; }

    int16_t max_l = 0;
    int16_t max_r = 0;

    for (uint32_t i = 0; i < frames; ++i) {
        float t = 1.0f;
        if (state->smooth_remaining > 0) {
            t = 1.0f - ((float)state->smooth_remaining / (float)EQ_SMOOTH_SAMPLES);
        }

        float preamp = (state->smooth_remaining > 0)
            ? lerp(state->preamp, state->target_preamp, t)
            : state->preamp;

        for (uint32_t ch = 0; ch < channels; ++ch) {
            int32_t idx = (i * channels) + ch;
            float sample = (float)pcm[idx];
            
            // 1. Apply HPF if enabled
            if (state->hpf_enabled) {
                sample = process_biquad(&state->hpf, sample);
            }
            
            // 2. Apply Preamp
            sample *= preamp;

            // 3. Apply EQ Bands
            for (int b = 0; b < EQ_BANDS; ++b) {
                if (state->smooth_remaining > 0) {
                    eq_biquad_t tmp;
                    lerp_biquad(&state->active[b], &state->target[b], t, &tmp);
                    // Preserve state from active biquad
                    tmp.z1 = state->active[b].z1;
                    tmp.z2 = state->active[b].z2;
                    
                    sample = process_biquad(&tmp, sample);
                    
                    // Update state in active biquad
                    state->active[b].z1 = tmp.z1;
                    state->active[b].z2 = tmp.z2;
                } else {
                    sample = process_biquad(&state->active[b], sample);
                }
            }

            // 4. Soft Clip
            sample = soft_clip(sample);

            // 5. Hard Clamp (Safety)
            if (sample > 32767.0f) {
                sample = 32767.0f;
                if (clip_counter) { (*clip_counter)++; }
            } else if (sample < -32768.0f) {
                sample = -32768.0f;
                if (clip_counter) { (*clip_counter)++; }
            }

            int16_t out_val = (int16_t)sample;
            pcm[idx] = out_val;
            
            // Peak metering
            int16_t abs_val = (out_val < 0) ? -out_val : out_val;
            if (ch == 0) {
                if (abs_val > max_l) max_l = abs_val;
            } else if (ch == 1) {
                if (abs_val > max_r) max_r = abs_val;
            }
        }

        if (state->smooth_remaining > 0) {
            state->smooth_remaining--;
            if (state->smooth_remaining == 0) {
                for (int b = 0; b < EQ_BANDS; ++b) {
                    // Preserve state when switching to target coefficients
                    float z1 = state->active[b].z1;
                    float z2 = state->active[b].z2;
                    state->active[b] = state->target[b];
                    state->active[b].z1 = z1;
                    state->active[b].z2 = z2;
                }
                state->preamp = state->target_preamp;
            }
        }
    }
    
    if (peak_l) *peak_l = max_l;
    if (peak_r) *peak_r = max_r;
}
