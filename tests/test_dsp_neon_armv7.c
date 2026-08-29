#include "../plugin/dsp.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#if !defined(__ARM_NEON)
#error This test must be built for the Vita Cortex-A9 NEON target.
#endif

#if !defined(EQVITA_DSP_TEST_API)
#error This test requires the DSP test-path selector.
#endif

static int failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

static eq_parametric_filter_t make_filter(uint8_t type, uint8_t channels,
                                          uint32_t frequency_mHz, int32_t gain_mdB,
                                          uint32_t q_uQ)
{
    eq_parametric_filter_t filter;
    memset(&filter, 0, sizeof(filter));
    filter.type = type;
    filter.channel_mask = channels;
    filter.shape = EQ_FILTER_SHAPE_Q;
    filter.frequency_mode = EQ_FILTER_FREQUENCY_CENTER;
    filter.data.filter.frequency_mHz = frequency_mHz;
    filter.data.filter.gain_mdB = gain_mdB;
    filter.data.filter.q_uQ = q_uQ;
    return filter;
}

static eq_parametric_filter_t make_copy(int16_t ll, int16_t lr, int16_t rl, int16_t rr)
{
    eq_parametric_filter_t operation;
    memset(&operation, 0, sizeof(operation));
    operation.type = EQ_FILTER_COPY;
    operation.data.copy.matrix[0] = ll;
    operation.data.copy.matrix[1] = lr;
    operation.data.copy.matrix[2] = rl;
    operation.data.copy.matrix[3] = rr;
    return operation;
}

static void make_ordered_profile(eq_dsp_state_t *state)
{
    eq_parametric_filter_t operations[7];

    operations[0] = make_filter(EQ_FILTER_LOW_SHELF, EQ_CHANNEL_STEREO_MASK,
                                94000u, 8030, 558000u);
    operations[1] = make_filter(EQ_FILTER_PEAK, EQ_CHANNEL_LEFT_MASK,
                                6800000u, -8000, 4708300u);
    operations[2] = make_copy(6000, 2000, -2000, 6000);
    operations[3] = make_filter(EQ_FILTER_PEAK, EQ_CHANNEL_RIGHT_MASK,
                                8000000u, -6000, 10000000u);
    operations[4] = make_filter(EQ_FILTER_PEAK, EQ_CHANNEL_STEREO_MASK,
                                3896000u, 7000, 782000u);
    operations[5] = make_copy(0, EQ_COPY_COEFFICIENT_SCALE,
                              EQ_COPY_COEFFICIENT_SCALE, 0);
    operations[6] = make_filter(EQ_FILTER_HIGH_SHELF, EQ_CHANNEL_STEREO_MASK,
                                10000000u, 3980, 700000u);

    eq_dsp_init(state, 48000);
    eq_dsp_set_parametric_targets(state, 48000, operations, 7u, -14000, 1);
}

static void fill_input(int16_t *pcm, uint32_t frames)
{
    for (uint32_t frame = 0; frame < frames; ++frame) {
        int32_t left = (int32_t)((frame * 1297u + 101u) % 40001u) - 20000;
        int32_t right = (int32_t)((frame * 1877u + 701u) % 38001u) - 19000;
        pcm[frame * 2u] = (int16_t)left;
        pcm[frame * 2u + 1u] = (int16_t)right;
    }
}

static int close_float(float a, float b)
{
    float scale = fmaxf(1.0f, fmaxf(fabsf(a), fabsf(b)));
    return fabsf(a - b) <= 0.00002f * scale;
}

#define CHECK_CLOSE_FLOAT(a, b) do { \
    float check_a_ = (a); \
    float check_b_ = (b); \
    if (!close_float(check_a_, check_b_)) { \
        printf("FAIL %s:%d: %s=%g, %s=%g, delta=%g\n", __FILE__, __LINE__, \
               #a, (double)check_a_, #b, (double)check_b_, \
               (double)fabsf(check_a_ - check_b_)); \
        failures++; \
    } \
} while (0)

static void check_neon_matches_scalar_reference(eq_dsp_output_limit_t output_limit)
{
    enum { FRAMES = 257 };
    eq_dsp_state_t scalar;
    eq_dsp_state_t neon;
    int16_t input[FRAMES * 2];
    int16_t scalar_output[FRAMES * 2];
    int16_t neon_output[FRAMES * 2];
    int32_t scalar_clips = 0;
    int32_t neon_clips = 0;
    uint16_t scalar_peak_l = 0;
    uint16_t scalar_peak_r = 0;
    uint16_t neon_peak_l = 0;
    uint16_t neon_peak_r = 0;
    int max_difference = 0;

    make_ordered_profile(&scalar);
    scalar.preamp = 2.0f;
    scalar.target_preamp = 2.0f;
    neon = scalar;
    fill_input(input, FRAMES);

    eq_dsp_set_neon_enabled_for_tests(0);
    eq_dsp_apply_to(&scalar, input, scalar_output, FRAMES, 2, output_limit, &scalar_clips,
                    &scalar_peak_l, &scalar_peak_r);
    eq_dsp_set_neon_enabled_for_tests(1);
    eq_dsp_apply_to(&neon, input, neon_output, FRAMES, 2, output_limit, &neon_clips,
                    &neon_peak_l, &neon_peak_r);

    CHECK(scalar_clips > 0);
    CHECK(neon_clips == scalar_clips);
    for (int sample = 0; sample < FRAMES * 2; ++sample) {
        int difference = scalar_output[sample] - neon_output[sample];
        if (difference < 0) difference = -difference;
        if (difference > max_difference) max_difference = difference;
    }
    CHECK(max_difference <= 1);
    CHECK((int)scalar_peak_l - (int)neon_peak_l <= 1);
    CHECK((int)neon_peak_l - (int)scalar_peak_l <= 1);
    CHECK((int)scalar_peak_r - (int)neon_peak_r <= 1);
    CHECK((int)neon_peak_r - (int)scalar_peak_r <= 1);

    CHECK_CLOSE_FLOAT(scalar.hpf_z.z1[0], neon.hpf_z.z1[0]);
    CHECK_CLOSE_FLOAT(scalar.hpf_z.z2[0], neon.hpf_z.z2[0]);
    CHECK_CLOSE_FLOAT(scalar.hpf_z.z1[1], neon.hpf_z.z1[1]);
    CHECK_CLOSE_FLOAT(scalar.hpf_z.z2[1], neon.hpf_z.z2[1]);
    for (int operation = 0; operation < 7; ++operation) {
        for (int channel = 0; channel < 2; ++channel) {
            CHECK_CLOSE_FLOAT(scalar.band_z[operation].z1[channel],
                              neon.band_z[operation].z1[channel]);
            CHECK_CLOSE_FLOAT(scalar.band_z[operation].z2[channel],
                              neon.band_z[operation].z2[channel]);
        }
    }
}

static void test_neon_matches_scalar_reference_for_both_limit_modes(void)
{
    check_neon_matches_scalar_reference(EQ_DSP_OUTPUT_SOFT_LIMIT);
    check_neon_matches_scalar_reference(EQ_DSP_OUTPUT_HARD_CLIP);
}

static void test_vcvtr_rounds_to_nearest_even_and_restores_fpscr(void)
{
    eq_dsp_state_t state;
    int16_t pcm[8 * 2] = {
        1, -1, 3, -3, 5, -5, 7, -7,
        9, -9, 11, -11, 13, -13, 15, -15
    };
    const int16_t expected[8 * 2] = {
        0, 0, 2, -2, 2, -2, 4, -4,
        4, -4, 6, -6, 6, -6, 8, -8
    };
    uint32_t original_fpscr;
    uint32_t caller_fpscr;
    uint32_t restored_fpscr;

    eq_dsp_init(&state, 48000);
    state.preamp = 0.5f;
    state.target_preamp = 0.5f;
    state.smooth_remaining = 0;
    eq_dsp_set_neon_enabled_for_tests(1);

    __asm__ volatile("vmrs %0, fpscr" : "=r"(original_fpscr));
    caller_fpscr = (original_fpscr & ~(3u << 22)) | (3u << 22);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(caller_fpscr) : "memory");
    eq_dsp_apply(&state, pcm, 8u, 2u, EQ_DSP_OUTPUT_SOFT_LIMIT, NULL, NULL, NULL);
    __asm__ volatile("vmrs %0, fpscr" : "=r"(restored_fpscr));
    __asm__ volatile("vmsr fpscr, %0" : : "r"(original_fpscr) : "memory");

    CHECK((restored_fpscr & (7u << 22)) == (caller_fpscr & (7u << 22)));
    for (int sample = 0; sample < 16; ++sample) {
        CHECK(pcm[sample] == expected[sample]);
    }
}

static void test_neon_hard_saturation_counts_clips(void)
{
    eq_dsp_state_t state;
    int16_t pcm[8 * 2];
    int32_t clips = 0;
    uint16_t peak_l = 0;
    uint16_t peak_r = 0;

    eq_dsp_init(&state, 48000);
    state.preamp = 2.0f;
    state.target_preamp = 2.0f;
    state.smooth_remaining = 0;
    for (int frame = 0; frame < 8; ++frame) {
        pcm[frame * 2] = 32767;
        pcm[frame * 2 + 1] = -32768;
    }

    eq_dsp_set_neon_enabled_for_tests(1);
    eq_dsp_apply(&state, pcm, 8u, 2u, EQ_DSP_OUTPUT_HARD_CLIP, &clips, &peak_l, &peak_r);

    CHECK(clips == 16);
    CHECK(peak_l == 32767u);
    CHECK(peak_r == 32768u);
    for (int frame = 0; frame < 8; ++frame) {
        CHECK(pcm[frame * 2] == 32767);
        CHECK(pcm[frame * 2 + 1] == -32768);
    }
}

static void test_vita_processing_flushes_subnormals_and_restores_fpscr(void)
{
    eq_dsp_state_t state;
    int16_t pcm = 0;
    uint32_t original_fpscr;
    uint32_t caller_fpscr;
    uint32_t restored_fpscr;

    eq_dsp_init(&state, 48000);
    state.active_operation_count = 1;
    state.active_operation_type[0] = EQ_FILTER_PEAK;
    state.active_band_enabled[0] = EQ_CHANNEL_LEFT_MASK;
    state.band_z[0].z1[0] = 1.0e-40f;
    state.band_z[0].z2[0] = -1.0e-40f;

    __asm__ volatile("vmrs %0, fpscr" : "=r"(original_fpscr));
    caller_fpscr = (original_fpscr & ~(7u << 22)) | (1u << 22);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(caller_fpscr) : "memory");
    eq_dsp_apply(&state, &pcm, 1u, 1u, EQ_DSP_OUTPUT_SOFT_LIMIT, NULL, NULL, NULL);
    __asm__ volatile("vmrs %0, fpscr" : "=r"(restored_fpscr));
    __asm__ volatile("vmsr fpscr, %0" : : "r"(original_fpscr) : "memory");

    CHECK((restored_fpscr & (7u << 22)) == (caller_fpscr & (7u << 22)));
    CHECK(pcm == 0);
    CHECK(state.band_z[0].z1[0] == 0.0f);
    CHECK(state.band_z[0].z2[0] == 0.0f);
}

static void test_neon_split_blocks_match_one_block(void)
{
    enum { FRAMES = 257 };
    static const uint16_t chunks[] = {17, 64, 3, 128, 45};
    eq_dsp_state_t single;
    eq_dsp_state_t split;
    int16_t single_pcm[FRAMES * 2];
    int16_t split_pcm[FRAMES * 2];
    uint32_t offset = 0;

    make_ordered_profile(&single);
    split = single;
    fill_input(single_pcm, FRAMES);
    memcpy(split_pcm, single_pcm, sizeof(single_pcm));
    eq_dsp_set_neon_enabled_for_tests(1);

    eq_dsp_apply(&single, single_pcm, FRAMES, 2u, EQ_DSP_OUTPUT_SOFT_LIMIT, NULL, NULL, NULL);
    for (unsigned chunk = 0; chunk < sizeof(chunks) / sizeof(chunks[0]); ++chunk) {
        eq_dsp_apply(&split, split_pcm + offset * 2u, chunks[chunk], 2u,
                     EQ_DSP_OUTPUT_SOFT_LIMIT, NULL, NULL, NULL);
        offset += chunks[chunk];
    }

    CHECK(offset == FRAMES);
    for (int sample = 0; sample < FRAMES * 2; ++sample) {
        CHECK(single_pcm[sample] == split_pcm[sample]);
    }
}

int main(void)
{
    test_neon_matches_scalar_reference_for_both_limit_modes();
    test_vcvtr_rounds_to_nearest_even_and_restores_fpscr();
    test_neon_hard_saturation_counts_clips();
    test_vita_processing_flushes_subnormals_and_restores_fpscr();
    test_neon_split_blocks_match_one_block();

    if (failures) {
        printf("%d ARMv7 NEON DSP failure(s)\n", failures);
        return 1;
    }

    puts("Cortex-A9 NEON DSP tests passed");
    return 0;
}
