#include "../plugin/dsp.h"

#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define ASSERT_EQ_I32(actual, expected) do { \
    int32_t a_ = (int32_t)(actual); \
    int32_t e_ = (int32_t)(expected); \
    if (a_ != e_) { \
        printf("FAIL %s:%d: expected %s == %d, got %d\n", __FILE__, __LINE__, #actual, e_, a_); \
        failures++; \
    } \
} while (0)

#define ASSERT_FINITE_FLOAT(value) do { \
    float v_ = (float)(value); \
    if (!isfinite(v_)) { \
        printf("FAIL %s:%d: expected %s to be finite, got %f\n", __FILE__, __LINE__, #value, v_); \
        failures++; \
    } \
} while (0)

static float test_mdB_to_gain(int32_t mdB) {
    return powf(10.0f, ((float)mdB) / 20000.0f);
}

static void fill_pattern(int16_t *pcm, int frames, int channels)
{
    for (int frame = 0; frame < frames; ++frame) {
        for (int ch = 0; ch < channels; ++ch) {
            int value = ((frame * 173) + (ch * 997)) % 56000;
            pcm[(frame * channels) + ch] = (int16_t)(value - 28000);
        }
    }
}

static void test_left_channel_filter_state_does_not_bleed_into_right(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[1024 * 2];
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    for (int i = 0; i < EQ_BANDS; ++i) {
        bands[i] = 0;
    }
    bands[4] = 9000;

    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);

    for (int i = 0; i < 1024; ++i) {
        pcm[i * 2] = 8000;
        pcm[(i * 2) + 1] = 0;
    }

    eq_dsp_apply(&dsp, pcm, 1024, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, &peak_l, &peak_r);

    for (int i = 0; i < 1024; ++i) {
        if (pcm[(i * 2) + 1] != 0) {
            printf("right channel changed at frame %d: %d\n", i, pcm[(i * 2) + 1]);
            failures++;
            break;
        }
    }
    ASSERT_TRUE(peak_l > 0);
    ASSERT_EQ_I32(peak_r, 0);
}

static void test_negative_full_scale_peak_is_reported(void) {
    eq_dsp_state_t dsp;
    int16_t pcm[4] = {-32768, -32768, 0, 0};
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    eq_dsp_init(&dsp, 48000);
    dsp.preamp = 1.0f;
    dsp.target_preamp = 1.0f;
    dsp.smooth_remaining = 0;

    eq_dsp_apply(&dsp, pcm, 2, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, &peak_l, &peak_r);

    ASSERT_EQ_I32(peak_l, 32768);
    ASSERT_EQ_I32(peak_r, 32768);
}

static void test_overload_soft_limits_and_counts_clips(void) {
    eq_dsp_state_t dsp;
    int16_t pcm[2] = {32767, -32768};
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    eq_dsp_init(&dsp, 48000);
    dsp.preamp = 2.0f;
    dsp.target_preamp = 2.0f;
    dsp.smooth_remaining = 0;

    eq_dsp_apply(&dsp, pcm, 1, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, &peak_l, &peak_r);

    ASSERT_EQ_I32(clips, 2);
    ASSERT_TRUE(pcm[0] >= 32600);
    ASSERT_TRUE(pcm[0] < 32767);
    ASSERT_TRUE(pcm[1] <= -32600);
    ASSERT_TRUE(pcm[1] > -32768);
}

static void test_smoothing_reaches_target(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[EQ_SMOOTH_SAMPLES * 2];
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    memset(pcm, 0, sizeof(pcm));
    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    eq_dsp_apply(&dsp, pcm, EQ_SMOOTH_SAMPLES, 1, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, &peak_l, &peak_r);

    ASSERT_EQ_I32(dsp.smooth_remaining, 0);
}

static void test_flat_eq_does_not_keep_active_band_filters(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[EQ_SMOOTH_SAMPLES * 2];
    int32_t clips = 0;

    memset(pcm, 0, sizeof(pcm));
    eq_dsp_init(&dsp, 48000);
    ASSERT_EQ_I32(eq_dsp_active_band_count(&dsp), 0);

    bands[4] = 6000;
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    eq_dsp_apply(&dsp, pcm, EQ_SMOOTH_SAMPLES, 1, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, NULL, NULL);
    ASSERT_EQ_I32(eq_dsp_active_band_count(&dsp), 1);

    bands[4] = 0;
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    eq_dsp_apply(&dsp, pcm, EQ_SMOOTH_SAMPLES, 1, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, NULL, NULL);
    ASSERT_EQ_I32(eq_dsp_active_band_count(&dsp), 0);
}

static void test_reapplying_same_targets_does_not_restart_smoothing(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[EQ_SMOOTH_SAMPLES];
    int32_t clips = 0;

    memset(pcm, 0, sizeof(pcm));
    eq_dsp_init(&dsp, 48000);

    eq_dsp_set_targets(&dsp, 48000, bands, EQ_DEFAULT_PREAMP_MDB, 0);
    ASSERT_EQ_I32(dsp.smooth_remaining, 0);

    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    ASSERT_TRUE(dsp.smooth_remaining > 0);
    eq_dsp_apply(&dsp, pcm, EQ_SMOOTH_SAMPLES, 1, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, NULL, NULL);
    ASSERT_EQ_I32(dsp.smooth_remaining, 0);

    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    ASSERT_EQ_I32(dsp.smooth_remaining, 0);
}

static void test_first_non_default_targets_snap_without_smoothing(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    float target_preamp;

    bands[0] = 3000;
    bands[1] = 4500;
    bands[4] = -5000;

    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, -6000, 0);

    target_preamp = dsp.target_preamp;
    ASSERT_EQ_I32(dsp.smooth_remaining, 0);
    ASSERT_EQ_I32(dsp.active_band_enabled[0], EQ_CHANNEL_STEREO_MASK);
    ASSERT_EQ_I32(dsp.active_band_enabled[1], EQ_CHANNEL_STEREO_MASK);
    ASSERT_EQ_I32(dsp.active_band_enabled[4], EQ_CHANNEL_STEREO_MASK);
    ASSERT_TRUE(fabsf(dsp.preamp - target_preamp) < 0.000001f);

    bands[1] = 2000;
    eq_dsp_set_targets(&dsp, 48000, bands, -6000, 0);
    ASSERT_TRUE(dsp.smooth_remaining > 0);
}

static void test_out_of_place_apply_matches_in_place_and_preserves_input(void) {
    eq_dsp_state_t in_place_dsp;
    eq_dsp_state_t out_place_dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t input[128 * 2];
    int16_t input_before[128 * 2];
    int16_t in_place_pcm[128 * 2];
    int16_t out_place_pcm[128 * 2];
    int32_t in_place_clips = 0;
    int32_t out_place_clips = 0;
    uint16_t in_peak_l = 0;
    uint16_t in_peak_r = 0;
    uint16_t out_peak_l = 0;
    uint16_t out_peak_r = 0;

    bands[0] = 3000;
    bands[1] = 4500;
    bands[4] = -5000;
    bands[6] = -2500;
    bands[8] = 2000;

    for (int i = 0; i < 128; ++i) {
        input[i * 2] = (int16_t)((i % 17) * 1200 - 9000);
        input[(i * 2) + 1] = (int16_t)(9000 - ((i % 19) * 1000));
    }
    memcpy(input_before, input, sizeof(input));
    memcpy(in_place_pcm, input, sizeof(input));
    memset(out_place_pcm, 0x7f, sizeof(out_place_pcm));

    eq_dsp_init(&in_place_dsp, 48000);
    eq_dsp_set_targets(&in_place_dsp, 48000, bands, -6000, 1);
    out_place_dsp = in_place_dsp;

    eq_dsp_apply(&in_place_dsp, in_place_pcm, 128, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &in_place_clips, &in_peak_l, &in_peak_r);
    eq_dsp_apply_to(&out_place_dsp, input, out_place_pcm, 128, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &out_place_clips, &out_peak_l, &out_peak_r);

    ASSERT_EQ_I32(out_place_clips, in_place_clips);
    ASSERT_EQ_I32(out_peak_l, in_peak_l);
    ASSERT_EQ_I32(out_peak_r, in_peak_r);
    ASSERT_EQ_I32(out_place_dsp.smooth_remaining, in_place_dsp.smooth_remaining);
    for (int i = 0; i < 128 * 2; ++i) {
        ASSERT_EQ_I32(out_place_pcm[i], in_place_pcm[i]);
        ASSERT_EQ_I32(input[i], input_before[i]);
    }
}

static void test_high_gain_bursts_stay_bounded_over_many_blocks(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    bands[0] = 12000;
    bands[1] = 12000;
    bands[2] = 9000;
    bands[4] = -6000;

    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, -12000, 1);

    for (int block = 0; block < 128; ++block) {
        int16_t pcm[256 * 2];
        for (int frame = 0; frame < 256; ++frame) {
            int burst = ((block + frame) % 32) < 16;
            pcm[frame * 2] = burst ? 28000 : -28000;
            pcm[(frame * 2) + 1] = burst ? -24000 : 24000;
        }

        eq_dsp_apply(&dsp, pcm, 256, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, &peak_l, &peak_r);

        ASSERT_TRUE(isfinite(dsp.preamp));
        ASSERT_TRUE(isfinite(dsp.target_preamp));
        ASSERT_TRUE(peak_l <= 32768u);
        ASSERT_TRUE(peak_r <= 32768u);
        for (int sample = 0; sample < 256 * 2; ++sample) {
            ASSERT_TRUE(pcm[sample] >= -32768);
            ASSERT_TRUE(pcm[sample] <= 32767);
        }
    }
}

static void test_soft_limit_is_monotonic_for_positive_full_scale_ramp(void) {
    eq_dsp_state_t dsp;
    int16_t pcm[16];
    int32_t clips = 0;

    eq_dsp_init(&dsp, 48000);
    dsp.preamp = 2.0f;
    dsp.target_preamp = 2.0f;
    dsp.smooth_remaining = 0;

    for (int i = 0; i < 16; ++i) {
        pcm[i] = (int16_t)(24000 + (i * 500));
    }

    eq_dsp_apply(&dsp, pcm, 16, 1, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, NULL, NULL);

    ASSERT_TRUE(clips > 0);
    for (int i = 1; i < 16; ++i) {
        ASSERT_TRUE(pcm[i] >= pcm[i - 1]);
        ASSERT_TRUE(pcm[i] <= 32767);
    }
}

static void test_soft_limit_bounds_sustained_overload_and_counts_clips(void) {
    eq_dsp_state_t dsp;
    int16_t pcm[64 * 2];
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    eq_dsp_init(&dsp, 48000);
    dsp.preamp = 2.0f;
    dsp.target_preamp = 2.0f;
    dsp.smooth_remaining = 0;

    for (int i = 0; i < 64; ++i) {
        pcm[i * 2] = 32767;
        pcm[(i * 2) + 1] = -32768;
    }

    eq_dsp_apply(&dsp, pcm, 64, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, &peak_l, &peak_r);

    ASSERT_EQ_I32(clips, 128);
    ASSERT_TRUE(peak_l < 32767u);
    ASSERT_TRUE(peak_r < 32768u);
    for (int i = 0; i < 64; ++i) {
        ASSERT_TRUE(pcm[i * 2] >= 32600);
        ASSERT_TRUE(pcm[i * 2] < 32767);
        ASSERT_TRUE(pcm[(i * 2) + 1] <= -32600);
        ASSERT_TRUE(pcm[(i * 2) + 1] > -32768);
    }
}

static void test_extreme_target_values_are_clamped_before_coefficients(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[64 * 2];
    int32_t clips = 0;

    bands[0] = INT32_MAX;
    bands[1] = INT32_MIN;
    bands[2] = 500000;
    bands[3] = -500000;

    memset(pcm, 0, sizeof(pcm));
    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, INT32_MAX, 1);

    ASSERT_FINITE_FLOAT(dsp.target_preamp);
    for (int i = 0; i < EQ_BANDS; ++i) {
        ASSERT_FINITE_FLOAT(dsp.target[i].b0);
        ASSERT_FINITE_FLOAT(dsp.target[i].b1);
        ASSERT_FINITE_FLOAT(dsp.target[i].b2);
        ASSERT_FINITE_FLOAT(dsp.target[i].a1);
        ASSERT_FINITE_FLOAT(dsp.target[i].a2);
    }

    eq_dsp_apply(&dsp, pcm, 64, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, NULL, NULL);
    for (int i = 0; i < 64 * 2; ++i) {
        ASSERT_TRUE(pcm[i] >= -32768);
        ASSERT_TRUE(pcm[i] <= 32767);
    }
}

static void test_invalid_internal_state_is_recovered_at_target_commit(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};

    bands[4] = 6000;
    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    ASSERT_EQ_I32(dsp.active_band_enabled[4], EQ_CHANNEL_STEREO_MASK);

    dsp.preamp = NAN;
    dsp.target_preamp = INFINITY;
    dsp.smooth_remaining = EQ_SMOOTH_SAMPLES * 4u;
    dsp.hpf.b0 = INFINITY;
    dsp.hpf_z.z1[0] = 44.0f;
    dsp.hpf_z.z2[0] = -88.0f;
    dsp.active[4].b0 = NAN;
    dsp.band_z[4].z1[0] = 55.0f;
    dsp.band_z[4].z2[0] = -77.0f;
    dsp.target[5].b0 = NAN;

    eq_dsp_set_targets(&dsp, 48000, bands, 0, 1);

    ASSERT_FINITE_FLOAT(dsp.preamp);
    ASSERT_FINITE_FLOAT(dsp.target_preamp);
    ASSERT_TRUE(dsp.smooth_remaining <= EQ_SMOOTH_SAMPLES);
    ASSERT_FINITE_FLOAT(dsp.hpf.b0);
    ASSERT_FINITE_FLOAT(dsp.hpf.b1);
    ASSERT_FINITE_FLOAT(dsp.hpf.b2);
    ASSERT_FINITE_FLOAT(dsp.hpf.a1);
    ASSERT_FINITE_FLOAT(dsp.hpf.a2);
    ASSERT_EQ_I32((int32_t)dsp.hpf_z.z1[0], 0);
    ASSERT_EQ_I32((int32_t)dsp.hpf_z.z2[0], 0);
    ASSERT_FINITE_FLOAT(dsp.active[4].b0);
    ASSERT_FINITE_FLOAT(dsp.active[4].b1);
    ASSERT_FINITE_FLOAT(dsp.active[4].b2);
    ASSERT_FINITE_FLOAT(dsp.active[4].a1);
    ASSERT_FINITE_FLOAT(dsp.active[4].a2);
    ASSERT_EQ_I32((int32_t)dsp.band_z[4].z1[0], 0);
    ASSERT_EQ_I32((int32_t)dsp.band_z[4].z2[0], 0);
    ASSERT_FINITE_FLOAT(dsp.target[5].b0);
    ASSERT_FINITE_FLOAT(dsp.target[5].b1);
    ASSERT_FINITE_FLOAT(dsp.target[5].b2);
    ASSERT_FINITE_FLOAT(dsp.target[5].a1);
    ASSERT_FINITE_FLOAT(dsp.target[5].a2);
    ASSERT_EQ_I32(dsp.target_band_enabled[5], 0);
}

static void test_apo_exact_hard_saturates_and_counts_clips(void) {
    eq_dsp_state_t dsp;
    int16_t pcm[2] = {32767, -32768};
    int32_t clips = 0;

    eq_dsp_init(&dsp, 48000);
    dsp.preamp = 2.0f;
    dsp.target_preamp = 2.0f;
    dsp.smooth_remaining = 0;

    eq_dsp_apply(&dsp, pcm, 1, 2, EQ_DSP_OUTPUT_HARD_CLIP, &clips, NULL, NULL);

    ASSERT_EQ_I32(clips, 2);
    ASSERT_EQ_I32(pcm[0], 32767);
    ASSERT_EQ_I32(pcm[1], -32768);
}

static void test_invalid_runtime_state_is_recovered_before_processing(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[8] = {12000, -12000, 8000, -8000, 4000, -4000, 1000, -1000};
    int32_t clips = 0;

    bands[0] = 6000;
    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, -6000, 1);
    dsp.preamp = NAN;
    dsp.smooth_remaining = UINT32_MAX;
    dsp.active[0].b0 = NAN;
    dsp.band_z[0].z1[0] = NAN;
    dsp.hpf_z.z2[1] = INFINITY;

    eq_dsp_apply(&dsp, pcm, 4, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, NULL, NULL);

    ASSERT_TRUE(isfinite(dsp.preamp));
    ASSERT_TRUE(dsp.smooth_remaining <= EQ_SMOOTH_SAMPLES);
    ASSERT_TRUE(isfinite(dsp.active[0].b0));
    ASSERT_TRUE(isfinite(dsp.band_z[0].z1[0]));
    ASSERT_TRUE(isfinite(dsp.hpf_z.z2[1]));
}

static void test_hpf_delay_state_resets_when_hpf_target_changes(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};

    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 1);

    dsp.hpf_z.z1[0] = 123.0f;
    dsp.hpf_z.z2[0] = -456.0f;
    dsp.band_z[2].z1[0] = 789.0f;

    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);

    ASSERT_EQ_I32((int32_t)dsp.hpf_z.z1[0], 0);
    ASSERT_EQ_I32((int32_t)dsp.hpf_z.z2[0], 0);
    ASSERT_EQ_I32((int32_t)dsp.band_z[2].z1[0], 789);

    dsp.hpf_z.z1[1] = -321.0f;
    dsp.hpf_z.z2[1] = 654.0f;
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 1);

    ASSERT_EQ_I32((int32_t)dsp.hpf_z.z1[1], 0);
    ASSERT_EQ_I32((int32_t)dsp.hpf_z.z2[1], 0);
}

static void test_retargeting_mid_smoothing_starts_from_current_gain(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[EQ_SMOOTH_SAMPLES / 2];
    int32_t clips = 0;
    float initial_gain = test_mdB_to_gain(EQ_DEFAULT_PREAMP_MDB);
    float zero_db_gain = test_mdB_to_gain(0);

    memset(pcm, 0, sizeof(pcm));
    eq_dsp_init(&dsp, 48000);
    ASSERT_TRUE(fabsf(dsp.preamp - initial_gain) < 0.0001f);

    eq_dsp_set_targets(&dsp, 48000, bands, EQ_DEFAULT_PREAMP_MDB, 0);
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 0);
    eq_dsp_apply(&dsp, pcm, EQ_SMOOTH_SAMPLES / 2, 1, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, NULL, NULL);
    ASSERT_TRUE(dsp.smooth_remaining > 0);

    eq_dsp_set_targets(&dsp, 48000, bands, -12000, 0);

    ASSERT_TRUE(dsp.preamp > initial_gain);
    ASSERT_TRUE(dsp.preamp < zero_db_gain);
    ASSERT_TRUE(dsp.target_preamp < dsp.preamp);
}

static void test_sample_rate_change_resets_smoothing_and_delay_state(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[64];
    int32_t clips = 0;

    bands[4] = 6000;
    memset(pcm, 0, sizeof(pcm));
    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_targets(&dsp, 48000, bands, EQ_DEFAULT_PREAMP_MDB, 1);
    eq_dsp_set_targets(&dsp, 48000, bands, 0, 1);
    eq_dsp_apply(&dsp, pcm, 64, 1, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, NULL, NULL);
    ASSERT_TRUE(dsp.smooth_remaining > 0);

    dsp.band_z[4].z1[0] = 123.0f;
    dsp.band_z[4].z2[0] = -456.0f;
    dsp.hpf_z.z1[0] = 789.0f;
    dsp.hpf_z.z2[0] = -321.0f;

    bands[4] = -6000;
    eq_dsp_set_targets(&dsp, 44100, bands, -9000, 1);

    ASSERT_EQ_I32(dsp.sample_rate, 44100);
    ASSERT_EQ_I32(dsp.smooth_remaining, 0);
    ASSERT_EQ_I32(dsp.active_band_enabled[4], EQ_CHANNEL_STEREO_MASK);
    ASSERT_EQ_I32(dsp.target_band_enabled[4], EQ_CHANNEL_STEREO_MASK);
    ASSERT_TRUE(fabsf(dsp.preamp - dsp.target_preamp) < 0.000001f);
    ASSERT_EQ_I32((int32_t)dsp.band_z[4].z1[0], 0);
    ASSERT_EQ_I32((int32_t)dsp.band_z[4].z2[0], 0);
    ASSERT_EQ_I32((int32_t)dsp.hpf_z.z1[0], 0);
    ASSERT_EQ_I32((int32_t)dsp.hpf_z.z2[0], 0);
}

static void test_active_band_index_cache_tracks_enabled_bands_in_order(void) {
    eq_dsp_state_t dsp;
    int32_t bands[EQ_BANDS] = {0};
    int16_t pcm[64 * 2];
    int32_t clips = 0;

    fill_pattern(pcm, 64, 2);
    eq_dsp_init(&dsp, 48000);
    ASSERT_EQ_I32(dsp.active_band_count, 0);
    ASSERT_EQ_I32(dsp.target_band_count, 0);

    bands[0] = 3000;
    bands[3] = -2500;
    bands[6] = 1500;
    bands[9] = -3000;
    eq_dsp_set_targets(&dsp, 48000, bands, -6000, 0);

    ASSERT_EQ_I32(dsp.active_band_count, 4);
    ASSERT_EQ_I32(dsp.target_band_count, 4);
    ASSERT_EQ_I32(dsp.active_band_index[0], 0);
    ASSERT_EQ_I32(dsp.active_band_index[1], 3);
    ASSERT_EQ_I32(dsp.active_band_index[2], 6);
    ASSERT_EQ_I32(dsp.active_band_index[3], 9);
    ASSERT_EQ_I32(dsp.target_band_index[0], 0);
    ASSERT_EQ_I32(dsp.target_band_index[1], 3);
    ASSERT_EQ_I32(dsp.target_band_index[2], 6);
    ASSERT_EQ_I32(dsp.target_band_index[3], 9);

    eq_dsp_apply(&dsp, pcm, 64, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &clips, NULL, NULL);
    ASSERT_EQ_I32(dsp.active_band_count, 4);
}

static void test_split_blocks_match_single_block_for_full_stereo_preset(void) {
    eq_dsp_state_t single;
    eq_dsp_state_t split;
    int32_t bands[EQ_BANDS] = {0};
    int16_t input[512 * 2];
    int16_t single_pcm[512 * 2];
    int16_t split_pcm[512 * 2];
    int32_t single_clips = 0;
    int32_t split_clips = 0;
    uint16_t single_peak_l = 0;
    uint16_t single_peak_r = 0;
    uint16_t split_peak_l = 0;
    uint16_t split_peak_r = 0;
    const int chunks[] = {17, 63, 128, 1, 303};
    int offset = 0;

    for (int i = 0; i < EQ_BANDS; ++i) {
        bands[i] = (i % 2 == 0) ? (2500 + (i * 100)) : (-2000 - (i * 150));
    }
    fill_pattern(input, 512, 2);
    memcpy(single_pcm, input, sizeof(input));
    memcpy(split_pcm, input, sizeof(input));

    eq_dsp_init(&single, 48000);
    eq_dsp_set_targets(&single, 48000, bands, -9000, 0);
    split = single;

    eq_dsp_apply(&single, single_pcm, 512, 2, EQ_DSP_OUTPUT_SOFT_LIMIT, &single_clips, &single_peak_l, &single_peak_r);

    for (unsigned i = 0; i < sizeof(chunks) / sizeof(chunks[0]); ++i) {
        eq_dsp_apply(&split, &split_pcm[offset * 2], chunks[i], 2,
                     EQ_DSP_OUTPUT_SOFT_LIMIT, &split_clips, &split_peak_l, &split_peak_r);
        offset += chunks[i];
    }

    ASSERT_EQ_I32(offset, 512);
    ASSERT_EQ_I32(split_clips, single_clips);
    ASSERT_EQ_I32(split.smooth_remaining, single.smooth_remaining);
    ASSERT_EQ_I32(split.active_band_count, single.active_band_count);
    ASSERT_EQ_I32(split.target_band_count, single.target_band_count);
    for (int i = 0; i < 512 * 2; ++i) {
        ASSERT_EQ_I32(split_pcm[i], single_pcm[i]);
    }
}

int main(void) {
    test_left_channel_filter_state_does_not_bleed_into_right();
    test_negative_full_scale_peak_is_reported();
    test_overload_soft_limits_and_counts_clips();
    test_smoothing_reaches_target();
    test_flat_eq_does_not_keep_active_band_filters();
    test_reapplying_same_targets_does_not_restart_smoothing();
    test_first_non_default_targets_snap_without_smoothing();
    test_out_of_place_apply_matches_in_place_and_preserves_input();
    test_high_gain_bursts_stay_bounded_over_many_blocks();
    test_soft_limit_is_monotonic_for_positive_full_scale_ramp();
    test_soft_limit_bounds_sustained_overload_and_counts_clips();
    test_apo_exact_hard_saturates_and_counts_clips();
    test_extreme_target_values_are_clamped_before_coefficients();
    test_invalid_internal_state_is_recovered_at_target_commit();
    test_invalid_runtime_state_is_recovered_before_processing();
    test_hpf_delay_state_resets_when_hpf_target_changes();
    test_retargeting_mid_smoothing_starts_from_current_gain();
    test_sample_rate_change_resets_smoothing_and_delay_state();
    test_active_band_index_cache_tracks_enabled_bands_in_order();
    test_split_blocks_match_single_block_for_full_stereo_preset();

    if (failures) {
        printf("%d dsp failure(s)\n", failures);
        return 1;
    }

    return 0;
}
