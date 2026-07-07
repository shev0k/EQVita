#include "../common/eq_shared.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        failures++; \
    } \
} while (0)

#define ASSERT_EQ_U32(actual, expected) do { \
    uint32_t a_ = (uint32_t)(actual); \
    uint32_t e_ = (uint32_t)(expected); \
    if (a_ != e_) { \
        printf("FAIL %s:%d: expected %s == %u, got %u\n", __FILE__, __LINE__, #actual, e_, a_); \
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

static void test_defaults_are_compatible_and_safe(void) {
    eq_control_t ctrl;
    memset(&ctrl, 0xA5, sizeof(ctrl));

    eq_control_init_defaults(&ctrl);

    ASSERT_EQ_U32(ctrl.version, EQ_ABI_VERSION);
    ASSERT_EQ_U32(ctrl.size, sizeof(eq_control_t));
    ASSERT_EQ_U32(ctrl.enabled, 0);
    ASSERT_EQ_U32(ctrl.speaker_only, 1);
    ASSERT_EQ_U32(eq_control_hpf_enabled(&ctrl), 1);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_SAFE);
    ASSERT_EQ_I32(ctrl.preamp_mdB, EQ_DEFAULT_PREAMP_MDB);
    ASSERT_TRUE(eq_control_is_compatible(&ctrl));
}

static void test_current_branch_uses_1_14_abi(void) {
    ASSERT_EQ_U32(EQ_VERSION_MAJOR, 1);
    ASSERT_EQ_U32(EQ_VERSION_MINOR, 14);
    ASSERT_EQ_U32(EQ_VERSION_PATCH, 0);
    ASSERT_EQ_U32(EQ_ABI_VERSION, EQ_VERSION_PACK(1, 14));
}

static void test_validation_rejects_wrong_abi(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ctrl.version = EQ_ABI_VERSION + 1;
    ASSERT_TRUE(eq_control_validate(&ctrl) < 0);

    eq_control_init_defaults(&ctrl);
    ctrl.size = sizeof(eq_control_t) - 1;
    ASSERT_TRUE(eq_control_validate(&ctrl) < 0);
}

static void test_validation_accepts_legacy_size_and_normalizes(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ctrl.version = EQ_LEGACY_ABI_VERSION_1_10;
    ctrl.size = sizeof(eq_shared_block_t);
    ctrl.enabled = 7;
    ctrl.route_hint = EQ_ROUTE_SPEAKER;
    ctrl.band_gain_mdB[2] = EQ_MAX_ABS_GAIN_MDB + 1;

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(ctrl.version, EQ_ABI_VERSION);
    ASSERT_EQ_U32(ctrl.size, sizeof(eq_control_t));
    ASSERT_EQ_U32(ctrl.enabled, 1);
    ASSERT_EQ_U32(ctrl.route_hint, EQ_ROUTE_UNKNOWN);
    ASSERT_EQ_I32(ctrl.band_gain_mdB[2], EQ_MAX_ABS_GAIN_MDB);
}

static void test_validation_accepts_1_11_control_files(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ctrl.version = EQ_LEGACY_ABI_VERSION_1_11;
    ctrl.route_hint = EQ_ROUTE_SPEAKER;

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(ctrl.version, EQ_ABI_VERSION);
    ASSERT_EQ_U32(ctrl.route_hint, EQ_ROUTE_UNKNOWN);
}

static void test_validation_accepts_1_13_control_files(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ctrl.version = EQ_LEGACY_ABI_VERSION_1_13;
    ctrl.route_hint = EQ_ROUTE_SPEAKER;

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(ctrl.version, EQ_ABI_VERSION);
    ASSERT_EQ_U32(ctrl.route_hint, EQ_ROUTE_UNKNOWN);
}

static void test_validation_normalizes_and_clamps(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ctrl.enabled = 9;
    ctrl.speaker_only = 3;
    ctrl.hpf_enabled = 2;
    ctrl.route_hint = 99;
    ctrl.preamp_mdB = EQ_MAX_ABS_GAIN_MDB + 5000;
    ctrl.band_gain_mdB[0] = -(EQ_MAX_ABS_GAIN_MDB + 1000);
    ctrl.band_gain_mdB[1] = EQ_MAX_ABS_GAIN_MDB + 1000;

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(ctrl.enabled, 1);
    ASSERT_EQ_U32(ctrl.speaker_only, 1);
    ASSERT_EQ_U32(ctrl.hpf_enabled, 1);
    ASSERT_EQ_U32(ctrl.route_hint, EQ_ROUTE_UNKNOWN);
    ASSERT_EQ_I32(ctrl.preamp_mdB, EQ_MAX_ABS_GAIN_MDB);
    ASSERT_EQ_I32(ctrl.band_gain_mdB[0], -EQ_MAX_ABS_GAIN_MDB);
    ASSERT_EQ_I32(ctrl.band_gain_mdB[1], EQ_MAX_ABS_GAIN_MDB);
}

static void test_headroom_mode_is_encoded_without_changing_control_size(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    ASSERT_EQ_U32(sizeof(ctrl), 60);

    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_LOUD);
    ASSERT_EQ_U32(eq_control_hpf_enabled(&ctrl), 1);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_LOUD);

    eq_control_set_hpf_enabled(&ctrl, 0);
    ASSERT_EQ_U32(eq_control_hpf_enabled(&ctrl), 0);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_LOUD);

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(eq_control_hpf_enabled(&ctrl), 0);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_LOUD);
}

static void test_legacy_payload_resets_headroom_mode(void) {
    eq_control_t ctrl;
    eq_control_init_defaults(&ctrl);

    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_RAW);
    ctrl.version = EQ_LEGACY_ABI_VERSION_1_12;

    ASSERT_TRUE(eq_control_validate(&ctrl) == 0);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&ctrl), EQ_HEADROOM_SAFE);
}

static void test_safe_headroom_reserves_largest_positive_band(void) {
    eq_control_t ctrl;
    int32_t bands[EQ_BANDS] = {0};

    eq_control_init_defaults(&ctrl);
    ctrl.preamp_mdB = 0;
    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_SAFE);
    bands[1] = 7000;
    bands[4] = 4000;
    bands[6] = -5000;

    ASSERT_EQ_I32(eq_control_effective_preamp_mdB(&ctrl, bands), -7000);
}

static void test_safe_headroom_keeps_user_cut_when_it_is_safer(void) {
    eq_control_t ctrl;
    int32_t bands[EQ_BANDS] = {0};

    eq_control_init_defaults(&ctrl);
    ctrl.preamp_mdB = -9000;
    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_SAFE);
    bands[1] = 4000;

    ASSERT_EQ_I32(eq_control_effective_preamp_mdB(&ctrl, bands), -9000);
}

static void test_loud_and_raw_headroom_modes_remain_explicit(void) {
    eq_control_t ctrl;
    int32_t bands[EQ_BANDS] = {0};

    eq_control_init_defaults(&ctrl);
    ctrl.preamp_mdB = -6500;
    bands[1] = 8000;

    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_LOUD);
    ASSERT_EQ_I32(eq_control_effective_preamp_mdB(&ctrl, bands), -3500);

    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_RAW);
    ASSERT_EQ_I32(eq_control_effective_preamp_mdB(&ctrl, bands), 0);
}

static void test_route_hint_selection_persists_for_global_audio(void) {
    ASSERT_TRUE(!eq_route_hint_is_usable(EQ_ROUTE_UNKNOWN, 1, 0, 0));
    ASSERT_TRUE(!eq_route_hint_is_usable(EQ_ROUTE_SPEAKER, 0, 0, 0));
    ASSERT_TRUE(eq_route_hint_is_usable(EQ_ROUTE_SPEAKER, 2, 1, UINT32_MAX));
    ASSERT_TRUE(eq_route_hint_is_usable(EQ_ROUTE_BLUETOOTH, 2, 2, UINT32_MAX));

    ASSERT_EQ_U32(eq_route_select(EQ_ROUTE_SPEAKER, 2, 2, UINT32_MAX, 0), EQ_ROUTE_SPEAKER);
    ASSERT_EQ_U32(eq_route_select(EQ_ROUTE_SPEAKER, 2, 2, UINT32_MAX, 1), EQ_ROUTE_HEADPHONES);
    ASSERT_EQ_U32(eq_route_select(EQ_ROUTE_UNKNOWN, 2, 2, 0, 0), EQ_ROUTE_UNKNOWN);
}

static void test_dirty_counter_advance_never_publishes_zero(void) {
    ASSERT_EQ_U32(eq_control_next_dirty_counter(0), 1);
    ASSERT_EQ_U32(eq_control_next_dirty_counter(41), 42);
    ASSERT_EQ_U32(eq_control_next_dirty_counter(UINT32_MAX), 1);
}

static void test_clip_event_counter_saturates(void) {
    eq_status_t status;
    memset(&status, 0, sizeof(status));

    eq_status_add_clip_events(&status, 3);
    ASSERT_EQ_I32(status.clip_events, 3);

    status.clip_events = INT32_MAX - 2;
    eq_status_add_clip_events(&status, 1);
    ASSERT_EQ_I32(status.clip_events, INT32_MAX - 1);

    eq_status_add_clip_events(&status, 99);
    ASSERT_EQ_I32(status.clip_events, INT32_MAX);

    eq_status_add_clip_events(&status, -10);
    ASSERT_EQ_I32(status.clip_events, INT32_MAX);
}

static void test_status_u32_counter_saturates(void) {
    uint32_t counter = 0;

    eq_status_increment_u32(&counter);
    ASSERT_EQ_U32(counter, 1);

    counter = UINT32_MAX - 1u;
    eq_status_increment_u32(&counter);
    ASSERT_EQ_U32(counter, UINT32_MAX);

    eq_status_increment_u32(&counter);
    ASSERT_EQ_U32(counter, UINT32_MAX);
}

static void test_status_keeps_slowest_block_context(void) {
    eq_status_t status;

    memset(&status, 0, sizeof(status));
    status.debug_max_us = 10569;
    status.debug_max_port = 7;
    status.debug_max_len = 256;
    status.debug_max_sample_rate = 48000;
    status.debug_max_channels = 2;
    status.debug_max_budget_us = 5333;
    status.debug_max_route = EQ_ROUTE_SPEAKER;
    status.debug_max_bypass_reason = EQ_BYPASS_NONE;
    status.debug_max_stage_control_us = 11;
    status.debug_max_stage_registry_us = 22;
    status.debug_max_stage_route_us = 33;
    status.debug_max_stage_copy_in_us = 44;
    status.debug_max_stage_retarget_us = 55;
    status.debug_max_stage_dsp_us = 66;
    status.debug_max_stage_copy_out_us = 77;
    status.debug_max_stage_original_us = 88;
    status.debug_max_stage_status_us = 99;
    status.debug_last_total_us = 111;
    status.debug_last_budget_us = 222;
    status.debug_last_margin_us = 333;
    status.debug_max_total_us = 444;
    status.debug_max_dsp_us = 555;
    status.debug_min_margin_us = -66;
    status.debug_min_margin_port = 9;
    status.debug_min_margin_len = 1024;
    status.debug_min_margin_sample_rate = 48000;
    status.debug_min_margin_channels = 2;
    status.debug_min_margin_budget_us = 21333;
    status.debug_min_margin_total_us = 21400;
    status.debug_min_margin_route = EQ_ROUTE_SPEAKER;
    status.debug_min_margin_bypass_reason = EQ_BYPASS_NONE;
    status.debug_min_margin_stage_dsp_us = 3000;
    status.debug_min_margin_stage_original_us = 18100;
    status.debug_min_margin_stage_status_us = 5;
    status.debug_min_margin_len_256_us = 100;
    status.debug_min_margin_len_1024_us = -66;
    status.debug_min_margin_len_2048_us = 2000;

    ASSERT_EQ_U32(status.debug_max_us, 10569);
    ASSERT_EQ_U32(status.debug_max_port, 7);
    ASSERT_EQ_U32(status.debug_max_len, 256);
    ASSERT_EQ_U32(status.debug_max_sample_rate, 48000);
    ASSERT_EQ_U32(status.debug_max_channels, 2);
    ASSERT_EQ_U32(status.debug_max_budget_us, 5333);
    ASSERT_EQ_U32(status.debug_max_route, EQ_ROUTE_SPEAKER);
    ASSERT_EQ_U32(status.debug_max_bypass_reason, EQ_BYPASS_NONE);
    ASSERT_EQ_U32(status.debug_max_stage_control_us, 11);
    ASSERT_EQ_U32(status.debug_max_stage_registry_us, 22);
    ASSERT_EQ_U32(status.debug_max_stage_route_us, 33);
    ASSERT_EQ_U32(status.debug_max_stage_copy_in_us, 44);
    ASSERT_EQ_U32(status.debug_max_stage_retarget_us, 55);
    ASSERT_EQ_U32(status.debug_max_stage_dsp_us, 66);
    ASSERT_EQ_U32(status.debug_max_stage_copy_out_us, 77);
    ASSERT_EQ_U32(status.debug_max_stage_original_us, 88);
    ASSERT_EQ_U32(status.debug_max_stage_status_us, 99);
    ASSERT_EQ_U32(status.debug_last_total_us, 111);
    ASSERT_EQ_U32(status.debug_last_budget_us, 222);
    ASSERT_EQ_I32(status.debug_last_margin_us, 333);
    ASSERT_EQ_U32(status.debug_max_total_us, 444);
    ASSERT_EQ_U32(status.debug_max_dsp_us, 555);
    ASSERT_EQ_I32(status.debug_min_margin_us, -66);
    ASSERT_EQ_U32(status.debug_min_margin_port, 9);
    ASSERT_EQ_U32(status.debug_min_margin_len, 1024);
    ASSERT_EQ_U32(status.debug_min_margin_sample_rate, 48000);
    ASSERT_EQ_U32(status.debug_min_margin_channels, 2);
    ASSERT_EQ_U32(status.debug_min_margin_budget_us, 21333);
    ASSERT_EQ_U32(status.debug_min_margin_total_us, 21400);
    ASSERT_EQ_U32(status.debug_min_margin_route, EQ_ROUTE_SPEAKER);
    ASSERT_EQ_U32(status.debug_min_margin_bypass_reason, EQ_BYPASS_NONE);
    ASSERT_EQ_U32(status.debug_min_margin_stage_dsp_us, 3000);
    ASSERT_EQ_U32(status.debug_min_margin_stage_original_us, 18100);
    ASSERT_EQ_U32(status.debug_min_margin_stage_status_us, 5);
    ASSERT_EQ_I32(status.debug_min_margin_len_256_us, 100);
    ASSERT_EQ_I32(status.debug_min_margin_len_1024_us, -66);
    ASSERT_EQ_I32(status.debug_min_margin_len_2048_us, 2000);
}

static void test_diagnostic_event_batch_shape_is_bounded(void) {
    eq_diag_event_t event;
    eq_diag_snapshot_t snapshot;

    ASSERT_TRUE(EQ_DIAG_MAX_EVENTS_PER_DRAIN >= 16);
    ASSERT_TRUE(EQ_DIAG_MAX_EVENTS_PER_DRAIN <= 64);
    ASSERT_TRUE(sizeof(eq_diag_event_t) <= 96);
    ASSERT_TRUE(sizeof(eq_diag_snapshot_t) <= 8192);

    memset(&event, 0, sizeof(event));
    event.version = EQ_DIAG_EVENT_VERSION;
    event.type = EQ_DIAG_EVENT_CLIP_BLOCK;
    event.port = 7;
    event.generation = 3;
    event.port_type = 0;
    event.len = 256;
    event.sample_rate = 48000;
    event.channels = 2;
    event.route = EQ_ROUTE_SPEAKER;
    event.reason = EQ_BYPASS_NONE;
    event.headroom_mode = EQ_HEADROOM_SAFE;
    event.preamp_mdB = -12000;
    event.effective_preamp_mdB = -12000;
    event.max_boost_mdB = 7000;
    event.clip_count = 42;
    event.input_peak_l = 30000;
    event.output_peak_l = 32767;

    ASSERT_EQ_U32(event.version, EQ_DIAG_EVENT_VERSION);
    ASSERT_EQ_U32(event.type, EQ_DIAG_EVENT_CLIP_BLOCK);
    ASSERT_EQ_U32(event.port, 7);
    ASSERT_EQ_I32(event.effective_preamp_mdB, -12000);
    ASSERT_EQ_U32(event.output_peak_l, 32767);

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.version = EQ_DIAG_SNAPSHOT_VERSION;
    snapshot.capacity = EQ_DIAG_MAX_EVENTS_PER_DRAIN;
    snapshot.count = 1;
    snapshot.events[0] = event;

    ASSERT_EQ_U32(snapshot.version, EQ_DIAG_SNAPSHOT_VERSION);
    ASSERT_EQ_U32(snapshot.capacity, EQ_DIAG_MAX_EVENTS_PER_DRAIN);
    ASSERT_EQ_U32(snapshot.count, 1);
    ASSERT_EQ_U32(snapshot.events[0].type, EQ_DIAG_EVENT_CLIP_BLOCK);
    ASSERT_TRUE(EQ_DIAG_EVENT_ACTIVE_SAMPLE != EQ_DIAG_EVENT_CLIP_BLOCK);
    ASSERT_TRUE(EQ_DIAG_EVENT_CONFIG_MISMATCH != EQ_DIAG_EVENT_CLIP_BLOCK);
}

static void test_legacy_preset_fallback_only_when_new_missing(void) {
    ASSERT_TRUE(eq_preset_should_try_legacy(EQ_PRESET_PRIMARY_MISSING));
    ASSERT_TRUE(!eq_preset_should_try_legacy(EQ_PRESET_PRIMARY_VALID));
    ASSERT_TRUE(!eq_preset_should_try_legacy(EQ_PRESET_PRIMARY_INVALID));
}

static void test_preset_wrapper_round_trips_and_detects_corruption(void) {
    eq_control_t ctrl;
    eq_control_t loaded;
    eq_preset_file_t preset;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 1;
    ctrl.dirty_counter = 77;
    ctrl.route_hint = EQ_ROUTE_BLUETOOTH;
    ctrl.band_gain_mdB[4] = 2500;

    eq_preset_build(&preset, &ctrl);
    ASSERT_TRUE(eq_preset_validate(&preset) == 0);
    ASSERT_TRUE(eq_preset_extract_control(&preset, &loaded) == 0);
    ASSERT_EQ_U32(loaded.dirty_counter, 0);
    ASSERT_EQ_U32(preset.control.route_hint, EQ_ROUTE_UNKNOWN);
    ASSERT_EQ_U32(loaded.route_hint, EQ_ROUTE_UNKNOWN);

    preset.control.band_gain_mdB[4] = 3000;
    ASSERT_TRUE(eq_preset_validate(&preset) < 0);

    eq_preset_build(&preset, &ctrl);
    preset.magic ^= 0x10u;
    ASSERT_TRUE(eq_preset_validate(&preset) < 0);
}

static void test_preset_wrapper_imports_legacy_control_version(void) {
    eq_control_t ctrl;
    eq_control_t loaded;
    eq_preset_file_t preset;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 1;
    ctrl.band_gain_mdB[1] = 4000;

    eq_preset_build(&preset, &ctrl);
    preset.control.version = EQ_LEGACY_ABI_VERSION_1_12;
    preset.checksum = eq_preset_checksum(&preset);

    ASSERT_TRUE(eq_preset_extract_control(&preset, &loaded) == 0);
    ASSERT_EQ_U32(loaded.version, EQ_ABI_VERSION);
    ASSERT_EQ_U32(loaded.enabled, 1);
    ASSERT_EQ_I32(loaded.band_gain_mdB[1], 4000);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&loaded), EQ_HEADROOM_SAFE);
}

static void test_preset_rejects_checksum_valid_invalid_control(void) {
    eq_control_t ctrl;
    eq_preset_file_t preset;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 9;
    ctrl.band_gain_mdB[2] = EQ_MAX_ABS_GAIN_MDB + 1000;

    memset(&preset, 0, sizeof(preset));
    preset.magic = EQ_PRESET_MAGIC;
    preset.version = EQ_PRESET_VERSION;
    preset.header_size = (uint32_t)offsetof(eq_preset_file_t, control);
    preset.payload_size = (uint32_t)sizeof(eq_control_t);
    preset.band_count = EQ_PRESET_BAND_COUNT;
    preset.control = ctrl;
    preset.checksum = eq_preset_checksum(&preset);

    ASSERT_TRUE(eq_preset_validate(&preset) < 0);
}

static void test_boot_state_preserves_route_hint_and_round_trips(void) {
    eq_control_t ctrl;
    eq_control_t loaded;
    eq_boot_state_file_t state;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 1;
    ctrl.route_hint = EQ_ROUTE_SPEAKER;
    ctrl.band_gain_mdB[1] = 4000;
    eq_control_set_headroom_mode(&ctrl, EQ_HEADROOM_LOUD);

    eq_boot_state_build(&state, &ctrl);
    ASSERT_TRUE(eq_boot_state_validate(&state) == 0);
    ASSERT_TRUE(eq_boot_state_extract_control(&state, &loaded) == 0);
    ASSERT_EQ_U32(loaded.route_hint, EQ_ROUTE_SPEAKER);
    ASSERT_EQ_U32(loaded.enabled, 1);
    ASSERT_EQ_I32(loaded.band_gain_mdB[1], 4000);
    ASSERT_EQ_U32(eq_control_get_headroom_mode(&loaded), EQ_HEADROOM_LOUD);
}

static void test_boot_state_assumes_speaker_when_enabled_route_unknown(void) {
    eq_control_t ctrl;
    eq_control_t loaded;
    eq_boot_state_file_t state;

    eq_control_init_defaults(&ctrl);
    ctrl.enabled = 1;
    ctrl.route_hint = EQ_ROUTE_UNKNOWN;

    eq_boot_state_build(&state, &ctrl);
    ASSERT_TRUE(eq_boot_state_extract_control(&state, &loaded) == 0);
    ASSERT_EQ_U32(loaded.route_hint, EQ_ROUTE_SPEAKER);

    ctrl.enabled = 0;
    eq_boot_state_build(&state, &ctrl);
    ASSERT_TRUE(eq_boot_state_extract_control(&state, &loaded) == 0);
    ASSERT_EQ_U32(loaded.route_hint, EQ_ROUTE_UNKNOWN);
}

int main(void) {
    test_defaults_are_compatible_and_safe();
    test_current_branch_uses_1_14_abi();
    test_validation_rejects_wrong_abi();
    test_validation_accepts_legacy_size_and_normalizes();
    test_validation_accepts_1_11_control_files();
    test_validation_accepts_1_13_control_files();
    test_validation_normalizes_and_clamps();
    test_headroom_mode_is_encoded_without_changing_control_size();
    test_legacy_payload_resets_headroom_mode();
    test_safe_headroom_reserves_largest_positive_band();
    test_safe_headroom_keeps_user_cut_when_it_is_safer();
    test_loud_and_raw_headroom_modes_remain_explicit();
    test_route_hint_selection_persists_for_global_audio();
    test_dirty_counter_advance_never_publishes_zero();
    test_clip_event_counter_saturates();
    test_status_u32_counter_saturates();
    test_status_keeps_slowest_block_context();
    test_diagnostic_event_batch_shape_is_bounded();
    test_legacy_preset_fallback_only_when_new_missing();
    test_preset_wrapper_round_trips_and_detects_corruption();
    test_preset_wrapper_imports_legacy_control_version();
    test_preset_rejects_checksum_valid_invalid_control();
    test_boot_state_preserves_route_hint_and_round_trips();
    test_boot_state_assumes_speaker_when_enabled_route_unknown();

    if (failures) {
        printf("%d shared validation failure(s)\n", failures);
        return 1;
    }

    return 0;
}
