#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "../app/equalizer_apo.h"
#include "../plugin/dsp.h"

static int failures;

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define CHECK_I32(actual, expected) do { \
    int32_t check_actual = (int32_t)(actual); \
    int32_t check_expected = (int32_t)(expected); \
    if (check_actual != check_expected) { \
        fprintf(stderr, "FAIL %s:%d: %s = %d, expected %d\n", \
                __FILE__, __LINE__, #actual, check_actual, check_expected); \
        failures++; \
    } \
} while (0)

#define CHECK_NEAR_F(actual, expected, tolerance) do { \
    float check_actual = (float)(actual); \
    float check_expected = (float)(expected); \
    if (fabsf(check_actual - check_expected) > (float)(tolerance)) { \
        fprintf(stderr, "FAIL %s:%d: %s = %.9f, expected %.9f +/- %.9f\n", \
                __FILE__, __LINE__, #actual, check_actual, check_expected, (float)(tolerance)); \
        failures++; \
    } \
} while (0)

static void fixture_path(char *out, size_t out_size, const char *name)
{
    snprintf(out, out_size, "%s/tests/fixtures/equalizer_apo/%s", EQVITA_SOURCE_DIR, name);
}

static int import_fixture(const char *name,
                          eq_control_t *control,
                          eqvita_apo_import_result_t *result)
{
    char path[1024];
    eq_control_t base;

    fixture_path(path, sizeof(path), name);
    eq_control_init_defaults(&base);
    return eqvita_apo_import_file(path, &base, control, result);
}

static void test_synthetic_fixture_corpus(void)
{
    static const struct {
        const char *name;
        int filters;
        int copies;
        int preamp_mdB;
    } cases[] = {
        {"mixed_syntax.txt", 10, 0, -9000},
        {"numbered_and_shelves.txt", 6, 0, -6000},
        {"dense_filter_bank.txt", 31, 1, -12000},
        {"high_q_filters.txt", 10, 0, 0},
        {"channel_and_order.txt", 6, 1, -2500},
        {"near_capacity_channels.txt", 39, 0, -15000}
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        eq_control_t control;
        eqvita_apo_import_result_t result;
        int parsed = import_fixture(cases[i].name, &control, &result);

        if (parsed < 0) {
            fprintf(stderr, "FAIL fixture %s line %d: %s\n",
                    cases[i].name, result.error_line, result.message);
            failures++;
            continue;
        }
        CHECK_I32(control.eq_mode, EQ_MODE_PARAMETRIC);
        CHECK_I32(control.parametric_filter_count, cases[i].filters + cases[i].copies);
        CHECK_I32(result.filter_count, cases[i].filters);
        CHECK_I32(result.copy_count, cases[i].copies);
        CHECK_I32(control.preamp_mdB, cases[i].preamp_mdB);
        CHECK_I32(control.enabled, 1);
        CHECK_I32(eq_control_get_headroom_mode(&control), EQ_HEADROOM_EXACT);
        CHECK_I32(eq_control_hpf_enabled(&control), 0);
        for (int band = 0; band < EQ_BANDS; ++band) {
            CHECK_I32(control.band_gain_mdB[band], 0);
        }
        CHECK(eq_control_validate(&control) == 0);
    }
}

static void test_import_is_an_exclusive_enabled_peq_mode(void)
{
    static const char text[] =
        "Preamp: -5 dB\n"
        "Filter: ON PK Fc 1000 Hz Gain 3 dB Q 1\n";
    eq_control_t base;
    eq_control_t control;
    eqvita_apo_import_result_t result;

    eq_control_init_defaults(&base);
    base.enabled = 0;
    base.band_gain_mdB[0] = 12000;
    base.band_gain_mdB[8] = -9000;
    eq_control_set_hpf_enabled(&base, 1);
    eq_control_set_headroom_mode(&base, EQ_HEADROOM_LOUD);

    CHECK(eqvita_apo_parse_text(text, &base, &control, &result) == 0);
    CHECK_I32(control.enabled, 1);
    CHECK_I32(control.eq_mode, EQ_MODE_PARAMETRIC);
    CHECK_I32(control.parametric_filter_count, 1);
    CHECK_I32(eq_control_hpf_enabled(&control), 0);
    CHECK_I32(eq_control_get_headroom_mode(&control), EQ_HEADROOM_EXACT);
    for (int band = 0; band < EQ_BANDS; ++band) {
        CHECK_I32(control.band_gain_mdB[band], 0);
    }

    control.band_gain_mdB[4] = 6000;
    eq_control_set_hpf_enabled(&control, 1);
    eq_control_set_headroom_mode(&control, EQ_HEADROOM_SAFE);
    CHECK(eq_control_validate(&control) == 0);
    CHECK_I32(control.band_gain_mdB[4], 0);
    CHECK_I32(eq_control_hpf_enabled(&control), 0);
    CHECK_I32(eq_control_get_headroom_mode(&control), EQ_HEADROOM_EXACT);
}

static void test_bundled_pch1000_peq(void)
{
    char path[1024];
    eq_control_t control;
    eqvita_apo_import_result_t result;

    snprintf(path, sizeof(path), "%s/app/assets/peq/pch-1000.txt", EQVITA_SOURCE_DIR);
    CHECK(eqvita_apo_import_file(path, NULL, &control, &result) == 0);
    CHECK_I32(control.enabled, 1);
    CHECK_I32(control.eq_mode, EQ_MODE_PARAMETRIC);
    CHECK_I32(control.preamp_mdB, -5000);
    CHECK_I32(result.filter_count, 6);
    CHECK_I32(result.copy_count, 0);
    CHECK_I32(control.parametric_filter_count, 6);
    CHECK_I32(control.parametric_filters[0].data.filter.frequency_mHz, 889000);
    CHECK_I32(control.parametric_filters[5].type, EQ_FILTER_HIGH_SHELF);
    CHECK_I32(control.parametric_filters[5].data.filter.frequency_mHz, 7500000);
}

static void test_numbered_filters_and_shelves(void)
{
    eq_control_t control;
    eqvita_apo_import_result_t result;

    CHECK(import_fixture("numbered_and_shelves.txt", &control, &result) == 0);
    CHECK_I32(control.parametric_filters[0].type, EQ_FILTER_LOW_SHELF);
    CHECK_I32(control.parametric_filters[0].frequency_mode, EQ_FILTER_FREQUENCY_CENTER);
    CHECK_I32(control.parametric_filters[0].shape, EQ_FILTER_SHAPE_Q);
    CHECK_I32(control.parametric_filters[0].data.filter.frequency_mHz, 80000);
    CHECK_I32(control.parametric_filters[0].data.filter.gain_mdB, 6000);
    CHECK_I32(control.parametric_filters[0].data.filter.q_uQ, 625000);
    CHECK_I32(control.parametric_filters[3].data.filter.gain_mdB, -4000);
    CHECK_I32(control.parametric_filters[4].type, EQ_FILTER_HIGH_SHELF);
    CHECK_I32(control.parametric_filters[4].frequency_mode, EQ_FILTER_FREQUENCY_CENTER);
}

static void test_default_and_corner_shelf_semantics(void)
{
    static const char text[] =
        "Filter: ON LS Fc 59.57 Hz Gain 5.4 dB\n"
        "Filter: ON LSC Fc 77.99 Hz Gain 5 dB Q 0.4818\n"
        "Filter: ON HS Fc 15089.2 Hz Gain 5 dB\n"
        "Filter: ON HS Fc 10520.4 Hz Gain 6 dB Q 0.7\n"
        "Filter: ON HSC Fc 12000 Hz Gain -3 dB Q 1.2\n"
        "Filter: ON LS 6 dB Fc 80 Hz Gain 4 dB\n"
        "Filter: ON HSC 3 dB Fc 9000 Hz Gain -2 dB\n";
    eq_control_t control;
    eqvita_apo_import_result_t result;

    CHECK(eqvita_apo_parse_text(text, NULL, &control, &result) == 0);
    CHECK_I32(control.parametric_filter_count, 7);
    CHECK_I32(control.parametric_filters[0].shape, EQ_FILTER_SHAPE_S);
    CHECK_I32(control.parametric_filters[0].frequency_mode, EQ_FILTER_FREQUENCY_CENTER);
    CHECK_I32(control.parametric_filters[0].data.filter.q_uQ,
              EQ_PARAMETRIC_DEFAULT_SHELF_S_UQ);
    CHECK_I32(control.parametric_filters[1].shape, EQ_FILTER_SHAPE_Q);
    CHECK_I32(control.parametric_filters[1].frequency_mode, EQ_FILTER_FREQUENCY_CENTER);
    CHECK_I32(control.parametric_filters[2].shape, EQ_FILTER_SHAPE_S);
    CHECK_I32(control.parametric_filters[2].frequency_mode, EQ_FILTER_FREQUENCY_CENTER);
    CHECK_I32(control.parametric_filters[3].frequency_mode, EQ_FILTER_FREQUENCY_CORNER);
    CHECK_I32(control.parametric_filters[4].frequency_mode, EQ_FILTER_FREQUENCY_CENTER);
    CHECK_I32(control.parametric_filters[5].shape, EQ_FILTER_SHAPE_S);
    CHECK_I32(control.parametric_filters[5].data.filter.q_uQ, 500000);
    CHECK_I32(control.parametric_filters[5].frequency_mode, EQ_FILTER_FREQUENCY_CORNER);
    CHECK_I32(control.parametric_filters[6].data.filter.q_uQ, 250000);
    CHECK_I32(control.parametric_filters[6].frequency_mode, EQ_FILTER_FREQUENCY_CENTER);
}

static void test_copy_matrices(void)
{
    static const char text[] =
        "Copy: L=-1.0*L R=-1.0*R (polarity inversion on L and R)\n"
        "Copy: R=L\n"
        "Copy: L=0.5*L+0.5*R R=0.5*L+0.5*R\n";
    eq_control_t control;
    eqvita_apo_import_result_t result;
    const int scale = EQ_COPY_COEFFICIENT_SCALE;

    CHECK(eqvita_apo_parse_text(text, NULL, &control, &result) == 0);
    CHECK_I32(control.parametric_filter_count, 3);
    CHECK_I32(result.copy_count, 3);
    CHECK_I32(control.parametric_filters[0].type, EQ_FILTER_COPY);
    CHECK_I32(control.parametric_filters[0].data.copy.matrix[0], -scale);
    CHECK_I32(control.parametric_filters[0].data.copy.matrix[1], 0);
    CHECK_I32(control.parametric_filters[0].data.copy.matrix[2], 0);
    CHECK_I32(control.parametric_filters[0].data.copy.matrix[3], -scale);
    CHECK_I32(control.parametric_filters[1].data.copy.matrix[0], scale);
    CHECK_I32(control.parametric_filters[1].data.copy.matrix[3], 0);
    for (int coefficient = 0; coefficient < 4; ++coefficient) {
        CHECK_I32(control.parametric_filters[2].data.copy.matrix[coefficient], scale / 2);
    }
}

static void test_channel_masks_and_operation_order(void)
{
    eq_control_t control;
    eqvita_apo_import_result_t result;
    int count;

    CHECK(import_fixture("channel_and_order.txt", &control, &result) == 0);
    count = control.parametric_filter_count;
    CHECK_I32(count, 7);
    CHECK_I32(control.parametric_filters[count - 4].channel_mask, EQ_CHANNEL_LEFT_MASK);
    CHECK_I32(control.parametric_filters[count - 3].channel_mask, EQ_CHANNEL_LEFT_MASK);
    CHECK_I32(control.parametric_filters[count - 2].channel_mask, EQ_CHANNEL_RIGHT_MASK);
    CHECK_I32(control.parametric_filters[count - 1].type, EQ_FILTER_COPY);
    CHECK_I32(control.parametric_filters[count - 1].data.copy.matrix[0], 0);
    CHECK_I32(control.parametric_filters[count - 1].data.copy.matrix[1], EQ_COPY_COEFFICIENT_SCALE);
    CHECK_I32(control.parametric_filters[count - 1].data.copy.matrix[2], EQ_COPY_COEFFICIENT_SCALE);
    CHECK_I32(control.parametric_filters[count - 1].data.copy.matrix[3], 0);

    CHECK(import_fixture("near_capacity_channels.txt", &control, &result) == 0);
    CHECK_I32(control.parametric_filters[29].channel_mask, EQ_CHANNEL_RIGHT_MASK);
    CHECK_I32(control.parametric_filters[31].channel_mask, EQ_CHANNEL_RIGHT_MASK);
    CHECK_I32(control.parametric_filters[32].channel_mask, EQ_CHANNEL_LEFT_MASK);
    CHECK_I32(control.parametric_filters[38].channel_mask, EQ_CHANNEL_LEFT_MASK);
}

static void test_device_is_ignored_and_include_is_inline(void)
{
    eq_control_t control;
    eqvita_apo_import_result_t result;

    CHECK(import_fixture("include_device_directives.txt", &control, &result) == 0);
    CHECK_I32(control.preamp_mdB, -9000);
    CHECK_I32(result.include_count, 3);
    CHECK_I32(result.filter_count, 3);
    CHECK_I32(result.ignored_count, 2);
    CHECK_I32(control.parametric_filter_count, 3);
    CHECK_I32(control.parametric_filters[0].type, EQ_FILTER_PEAK);
    CHECK_I32(control.parametric_filters[1].type, EQ_FILTER_LOW_SHELF);
    CHECK_I32(control.parametric_filters[2].type, EQ_FILTER_HIGH_SHELF);
}

static void test_include_restores_outer_channel_selection(void)
{
    eq_control_t control;
    eqvita_apo_import_result_t result;

    CHECK(import_fixture("channel_scope_parent.txt", &control, &result) == 0);
    CHECK_I32(result.include_count, 1);
    CHECK_I32(control.parametric_filter_count, 2);
    CHECK_I32(control.parametric_filters[0].channel_mask, EQ_CHANNEL_LEFT_MASK);
    CHECK_I32(control.parametric_filters[1].channel_mask, EQ_CHANNEL_RIGHT_MASK);
}

static void test_comments_off_filters_and_decimal_comma(void)
{
    static const char text[] =
        "# Filter: ON PK Fc 100 Hz Gain 20 dB Q 1\n"
        "Filter: OFF PK Fc 200 Hz Gain 3 dB Q 1\n"
        "Preamp: -1,5 dB\n"
        "Channel: L,R\n"
        "Filter 9: ON PK Fc 6800,25 Hz Gain -8,5 dB Q 4,7083\n";
    eq_control_t control;
    eqvita_apo_import_result_t result;

    CHECK(eqvita_apo_parse_text(text, NULL, &control, &result) == 0);
    CHECK_I32(control.preamp_mdB, -1500);
    CHECK_I32(control.parametric_filter_count, 1);
    CHECK_I32(result.ignored_count, 1);
    CHECK_I32(control.parametric_filters[0].data.filter.frequency_mHz, 6800250);
    CHECK_I32(control.parametric_filters[0].data.filter.gain_mdB, -8500);
    CHECK_I32(control.parametric_filters[0].data.filter.q_uQ, 4708300);
    CHECK_I32(control.parametric_filters[0].channel_mask, EQ_CHANNEL_STEREO_MASK);
}

static void test_capacity_and_unsupported_commands_fail_loudly(void)
{
    char text[8192];
    size_t used = 0;
    eq_control_t control;
    eqvita_apo_import_result_t result;

    for (int i = 0; i < EQ_PARAMETRIC_FILTERS + 1; ++i) {
        int written = snprintf(text + used, sizeof(text) - used,
                               "Filter: ON PK Fc %d Hz Gain 1 dB Q 1\n", 100 + i);
        CHECK(written > 0 && (size_t)written < sizeof(text) - used);
        used += (size_t)written;
    }
    CHECK(eqvita_apo_parse_text(text, NULL, &control, &result) < 0);
    CHECK(strstr(result.message, "More than 48") != NULL);

    CHECK(eqvita_apo_parse_text("Preamp: -3 dB\nGraphicEQ: 20 0; 100 2\n",
                                NULL, &control, &result) < 0);
    CHECK(strstr(result.message, "Unsupported active command") != NULL);
    CHECK(eqvita_apo_parse_text("Filter: ON LP Fc 1000 Hz Q 0.7\n",
                                NULL, &control, &result) < 0);
    CHECK(strstr(result.message, "Unsupported active filter type") != NULL);
    CHECK(eqvita_apo_parse_text("Filter: ON LS 0.01 dB Fc 100 Hz Gain 3 dB\n",
                                NULL, &control, &result) < 0);
    CHECK(strstr(result.message, "Shelf slope") != NULL);
}

static void process_text(const char *text, int16_t *pcm, int frames, int channels)
{
    eq_control_t control;
    eq_dsp_state_t dsp;
    eqvita_apo_import_result_t result;
    int32_t clips = 0;

    CHECK(eqvita_apo_parse_text(text, NULL, &control, &result) == 0);
    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_parametric_targets(&dsp, 48000,
                                  control.parametric_filters,
                                  control.parametric_filter_count,
                                  control.preamp_mdB,
                                  eq_control_hpf_enabled(&control));
    eq_dsp_apply(&dsp, pcm, (uint32_t)frames, (uint32_t)channels, &clips, NULL, NULL);
}

static void test_copy_audio_results(void)
{
    int16_t polarity[] = {1000, -2000};
    int16_t left_to_right[] = {1000, -2000};
    int16_t average[] = {1000, -2000};

    process_text("Copy: L=-1.0*L R=-1.0*R\n", polarity, 1, 2);
    CHECK_I32(polarity[0], -1000);
    CHECK_I32(polarity[1], 2000);

    process_text("Copy: R=L\n", left_to_right, 1, 2);
    CHECK_I32(left_to_right[0], 1000);
    CHECK_I32(left_to_right[1], 1000);

    process_text("Copy: L=0.5*L+0.5*R R=0.5*L+0.5*R\n", average, 1, 2);
    CHECK_I32(average[0], -500);
    CHECK_I32(average[1], -500);
}

static void test_copy_and_channel_filter_order_is_audible(void)
{
    static const char filter_then_swap[] =
        "Channel: L\n"
        "Filter: ON PK Fc 1000 Hz Gain 12 dB Q 1\n"
        "Copy: L=R R=L\n";
    static const char swap_then_filter[] =
        "Copy: L=R R=L\n"
        "Channel: L\n"
        "Filter: ON PK Fc 1000 Hz Gain 12 dB Q 1\n";
    int16_t after[] = {10000, 0};
    int16_t before[] = {10000, 0};

    process_text(filter_then_swap, after, 1, 2);
    process_text(swap_then_filter, before, 1, 2);
    CHECK_I32(after[0], 0);
    CHECK(after[1] > 10000);
    CHECK_I32(before[0], 0);
    CHECK_I32(before[1], 10000);
}

static void test_left_and_right_filters_are_independent(void)
{
    int16_t left_only[] = {10000, 10000};
    int16_t right_only[] = {10000, 10000};

    process_text("Channel: L\nFilter: ON PK Fc 1000 Hz Gain 12 dB Q 1\n",
                 left_only, 1, 2);
    CHECK(left_only[0] > 10000);
    CHECK_I32(left_only[1], 10000);

    process_text("Channel: R\nFilter: ON PK Fc 1000 Hz Gain 12 dB Q 1\n",
                 right_only, 1, 2);
    CHECK_I32(right_only[0], 10000);
    CHECK(right_only[1] > 10000);
}

static double measured_sine_rms(const char *config, double frequency)
{
    enum { FRAMES = 8192 };
    int16_t pcm[FRAMES];
    double sum = 0.0;

    for (int i = 0; i < FRAMES; ++i) {
        pcm[i] = (int16_t)(6000.0 * sin(2.0 * 3.14159265358979323846 * frequency * i / 48000.0));
    }
    process_text(config, pcm, FRAMES, 1);
    for (int i = FRAMES / 2; i < FRAMES; ++i) {
        double sample = pcm[i];
        sum += sample * sample;
    }
    return sqrt(sum / (FRAMES / 2));
}

static void test_low_and_high_shelves_shape_the_correct_end(void)
{
    static const char low_shelf[] =
        "Preamp: -12 dB\n"
        "Filter: ON LSC Fc 100 Hz Gain 12 dB Q 0.707\n";
    static const char high_shelf[] =
        "Preamp: -12 dB\n"
        "Filter: ON HSC Fc 8000 Hz Gain 12 dB Q 0.707\n";
    double low_at_30 = measured_sine_rms(low_shelf, 30.0);
    double low_at_5000 = measured_sine_rms(low_shelf, 5000.0);
    double high_at_500 = measured_sine_rms(high_shelf, 500.0);
    double high_at_16000 = measured_sine_rms(high_shelf, 16000.0);

    CHECK(low_at_30 > low_at_5000 * 2.0);
    CHECK(high_at_16000 > high_at_500 * 2.0);
}

static void test_equalizer_apo_reference_coefficients(void)
{
    static const char text[] =
        "Filter: ON PK Fc 1000 Hz Gain 6 dB Q 1\n"
        "Filter: ON HS Fc 15089.2 Hz Gain 5 dB\n"
        "Filter: ON LS Fc 100 Hz Gain 6 dB Q 0.7\n";
    eq_control_t control;
    eq_dsp_state_t dsp;
    eqvita_apo_import_result_t result;
    const float tolerance = 0.00002f;

    CHECK(eqvita_apo_parse_text(text, NULL, &control, &result) == 0);
    eq_dsp_init(&dsp, 48000);
    eq_dsp_set_parametric_targets(&dsp, 48000,
                                  control.parametric_filters,
                                  control.parametric_filter_count,
                                  control.preamp_mdB, 0);

    CHECK_NEAR_F(dsp.active[0].b0, 1.043953087f, tolerance);
    CHECK_NEAR_F(dsp.active[0].b1, -1.895320724f, tolerance);
    CHECK_NEAR_F(dsp.active[0].b2, 0.867722285f, tolerance);
    CHECK_NEAR_F(dsp.active[0].a1, -1.895320724f, tolerance);
    CHECK_NEAR_F(dsp.active[0].a2, 0.911675372f, tolerance);

    CHECK_NEAR_F(dsp.active[1].b0, 1.247121674f, tolerance);
    CHECK_NEAR_F(dsp.active[1].b1, 0.384907631f, tolerance);
    CHECK_NEAR_F(dsp.active[1].b2, 0.202923630f, tolerance);
    CHECK_NEAR_F(dsp.active[1].a1, 0.617991003f, tolerance);
    CHECK_NEAR_F(dsp.active[1].a2, 0.216961933f, tolerance);

    CHECK_NEAR_F(dsp.active[2].b0, 1.003876525f, tolerance);
    CHECK_NEAR_F(dsp.active[2].b1, -1.981156649f, tolerance);
    CHECK_NEAR_F(dsp.active[2].b2, 0.977621088f, tolerance);
    CHECK_NEAR_F(dsp.active[2].a1, -1.981241688f, tolerance);
    CHECK_NEAR_F(dsp.active[2].a2, 0.981412574f, tolerance);
}

static void test_synthetic_configs_run_through_dsp_without_nonfinite_state(void)
{
    static const char *names[] = {
        "dense_filter_bank.txt",
        "channel_and_order.txt",
        "near_capacity_channels.txt"
    };

    for (size_t fixture = 0; fixture < sizeof(names) / sizeof(names[0]); ++fixture) {
        eq_control_t control;
        eq_dsp_state_t dsp;
        eqvita_apo_import_result_t result;
        int32_t clips = 0;

        CHECK(import_fixture(names[fixture], &control, &result) == 0);
        eq_dsp_init(&dsp, 48000);
        eq_dsp_set_parametric_targets(&dsp, 48000,
                                      control.parametric_filters,
                                      control.parametric_filter_count,
                                      control.preamp_mdB, 0);
        CHECK(dsp.preamp >= 0.03f);
        CHECK(dsp.preamp < 1.0f);
        if (fixture == 0) {
            CHECK(fabsf(dsp.preamp - powf(10.0f, -12.0f / 20.0f)) < 0.00001f);
        }

        for (int block = 0; block < 32; ++block) {
            int16_t pcm[256 * 2];
            for (int frame = 0; frame < 256; ++frame) {
                pcm[frame * 2] = (int16_t)(((frame * 197 + block * 83) % 24000) - 12000);
                pcm[frame * 2 + 1] = (int16_t)(((frame * 149 + block * 127) % 22000) - 11000);
            }
            eq_dsp_apply(&dsp, pcm, 256, 2, &clips, NULL, NULL);
        }

        CHECK(isfinite(dsp.preamp));
        CHECK(isfinite(dsp.target_preamp));
        for (int operation = 0; operation < control.parametric_filter_count; ++operation) {
            CHECK(isfinite(dsp.active[operation].b0));
            CHECK(isfinite(dsp.active[operation].b1));
            CHECK(isfinite(dsp.active[operation].b2));
            CHECK(isfinite(dsp.active[operation].a1));
            CHECK(isfinite(dsp.active[operation].a2));
            for (int channel = 0; channel < 2; ++channel) {
                CHECK(isfinite(dsp.band_z[channel][operation].z1));
                CHECK(isfinite(dsp.band_z[channel][operation].z2));
            }
        }
    }
}

static void test_three_output_profiles_keep_independent_armv7_peq_math(void)
{
    static const char *texts[EQ_ROUTE_PROFILE_COUNT] = {
        "Preamp: -5 dB\nFilter: ON PK Fc 889 Hz Gain -5.77 dB Q 0.687\n",
        "Preamp: -9.6 dB\nFilter: ON PK Fc 3896 Hz Gain 12.5 dB Q 0.782\n",
        "Preamp: -16.8 dB\nFilter: ON HS Fc 7345.65 Hz Gain 7 dB\n"
    };
    static const int32_t expected_preamps[EQ_ROUTE_PROFILE_COUNT] = {-5000, -9600, -16800};
    static const uint32_t expected_frequencies[EQ_ROUTE_PROFILE_COUNT] = {889000, 3896000, 7345650};
    eq_route_profile_bank_t bank;
    eq_route_profile_bank_t loaded;
    eq_route_profile_file_t file;
    char names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX] = {{0}};

    eq_route_profile_bank_init(&bank);
    for (unsigned int index = 0; index < EQ_ROUTE_PROFILE_COUNT; ++index) {
        eqvita_apo_import_result_t result;
        CHECK(eqvita_apo_parse_text(texts[index], NULL, &bank.profiles[index], &result) == 0);
        bank.enabled_mask |= (uint8_t)(1u << index);
        snprintf(names[index], sizeof(names[index]), "route-%u.txt", index + 1u);
    }
    bank.selected_route = EQ_ROUTE_BLUETOOTH;
    eq_route_profile_file_build(&file, &bank, names);
    CHECK(eq_route_profile_file_extract(&file, &loaded, NULL) == 0);
    CHECK_I32(loaded.enabled_mask, 7);

    for (unsigned int index = 0; index < EQ_ROUTE_PROFILE_COUNT; ++index) {
        eq_dsp_state_t dsp;
        CHECK_I32(loaded.profiles[index].preamp_mdB, expected_preamps[index]);
        CHECK_I32(loaded.profiles[index].parametric_filter_count, 1);
        CHECK_I32(loaded.profiles[index].parametric_filters[0].data.filter.frequency_mHz,
                  expected_frequencies[index]);
        eq_dsp_init(&dsp, 48000);
        eq_dsp_set_parametric_targets(&dsp, 48000,
                                      loaded.profiles[index].parametric_filters,
                                      loaded.profiles[index].parametric_filter_count,
                                      loaded.profiles[index].preamp_mdB, 0);
        CHECK(isfinite(dsp.active[0].b0));
        CHECK(isfinite(dsp.active[0].a2));
        CHECK_NEAR_F(dsp.preamp,
                     powf(10.0f, expected_preamps[index] / 20000.0f),
                     0.00001f);
    }
}

int main(void)
{
    test_synthetic_fixture_corpus();
    test_import_is_an_exclusive_enabled_peq_mode();
    test_bundled_pch1000_peq();
    test_numbered_filters_and_shelves();
    test_default_and_corner_shelf_semantics();
    test_copy_matrices();
    test_channel_masks_and_operation_order();
    test_device_is_ignored_and_include_is_inline();
    test_include_restores_outer_channel_selection();
    test_comments_off_filters_and_decimal_comma();
    test_capacity_and_unsupported_commands_fail_loudly();
    test_copy_audio_results();
    test_copy_and_channel_filter_order_is_audible();
    test_left_and_right_filters_are_independent();
    test_low_and_high_shelves_shape_the_correct_end();
    test_equalizer_apo_reference_coefficients();
    test_synthetic_configs_run_through_dsp_without_nonfinite_state();
    test_three_output_profiles_keep_independent_armv7_peq_math();

    if (failures) {
        fprintf(stderr, "%d Equalizer APO test failure(s)\n", failures);
        return 1;
    }
    puts("Equalizer APO import tests passed");
    return 0;
}
