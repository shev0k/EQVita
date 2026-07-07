#include "../plugin/port_registry.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define TEST_OWNER_A 0x1001u
#define TEST_OWNER_B 0x2002u

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

#define ASSERT_EQ_U32(actual, expected) do { \
    uint32_t a_ = (uint32_t)(actual); \
    uint32_t e_ = (uint32_t)(expected); \
    if (a_ != e_) { \
        printf("FAIL %s:%d: expected %s == %u, got %u\n", __FILE__, __LINE__, #actual, e_, a_); \
        failures++; \
    } \
} while (0)

static void test_sparse_port_id_is_tracked_without_using_id_as_array_index(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);

    slot = eq_audio_port_registry_open(&registry, 256, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);
    ASSERT_EQ_I32(slot->port_id, 256);
    ASSERT_EQ_U32(slot->config.len, 256);
    ASSERT_EQ_U32(slot->config.freq, 48000);
    ASSERT_EQ_U32(slot->config.channels, 2);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), 1);

    ASSERT_TRUE(eq_audio_port_registry_find(&registry, 256) == slot);
    ASSERT_TRUE(eq_audio_port_registry_find(&registry, 7) == NULL);
}

static void test_sparse_port_config_and_release_use_port_id_lookup(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);

    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 256, 0, 256, 48000, EQ_AUDIO_MODE_STEREO) != NULL);
    ASSERT_TRUE(eq_audio_port_registry_set_config(&registry, 256, EQ_AUDIO_KEEP_U32, -1, -1) == 0);

    slot = eq_audio_port_registry_find(&registry, 256);
    ASSERT_TRUE(slot != NULL);
    ASSERT_EQ_U32(slot->config.len, 256);
    ASSERT_EQ_U32(slot->config.freq, 48000);
    ASSERT_EQ_U32(slot->config.channels, 2);

    ASSERT_TRUE(eq_audio_port_registry_release(&registry, 256) == 0);
    ASSERT_TRUE(eq_audio_port_registry_find(&registry, 256) == NULL);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), 0);
}

static void test_same_port_id_can_be_tracked_for_different_owners(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *first;
    eq_audio_tracked_port_t *second;

    eq_audio_port_registry_init(&registry);

    first = eq_audio_port_registry_open_owned(&registry, TEST_OWNER_A, 7, EQ_AUDIO_PORT_TYPE_MAIN, 256, 48000, EQ_AUDIO_MODE_STEREO);
    second = eq_audio_port_registry_open_owned(&registry, TEST_OWNER_B, 7, EQ_AUDIO_PORT_TYPE_MAIN, 512, 48000, EQ_AUDIO_MODE_STEREO);

    ASSERT_TRUE(first != NULL);
    ASSERT_TRUE(second != NULL);
    ASSERT_TRUE(first != second);
    ASSERT_EQ_U32(first->owner_id, TEST_OWNER_A);
    ASSERT_EQ_U32(second->owner_id, TEST_OWNER_B);
    ASSERT_EQ_U32(first->config.len, 256);
    ASSERT_EQ_U32(second->config.len, 512);
    ASSERT_TRUE(eq_audio_port_registry_find_owned(&registry, TEST_OWNER_A, 7) == first);
    ASSERT_TRUE(eq_audio_port_registry_find_owned(&registry, TEST_OWNER_B, 7) == second);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), 2);
}

static void test_releasing_one_owner_keeps_same_port_for_other_owner(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *first;
    eq_audio_tracked_port_t *second;

    eq_audio_port_registry_init(&registry);

    first = eq_audio_port_registry_open_owned(&registry, TEST_OWNER_A, 7, EQ_AUDIO_PORT_TYPE_MAIN, 256, 48000, EQ_AUDIO_MODE_STEREO);
    second = eq_audio_port_registry_open_owned(&registry, TEST_OWNER_B, 7, EQ_AUDIO_PORT_TYPE_MAIN, 512, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(first != NULL);
    ASSERT_TRUE(second != NULL);

    ASSERT_EQ_I32(eq_audio_port_registry_release_owned(&registry, TEST_OWNER_A, 7), 0);

    ASSERT_TRUE(eq_audio_port_registry_find_owned(&registry, TEST_OWNER_A, 7) == NULL);
    ASSERT_TRUE(eq_audio_port_registry_find_owned(&registry, TEST_OWNER_B, 7) == second);
    ASSERT_EQ_U32(second->config.len, 512);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), 1);
}

static void test_registry_tracks_bgm_after_all_main_ports_are_open(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *bgm;

    eq_audio_port_registry_init(&registry);

    for (int i = 0; i < 8; ++i) {
        ASSERT_TRUE(eq_audio_port_registry_open_owned(&registry, TEST_OWNER_A, i, EQ_AUDIO_PORT_TYPE_MAIN, 256, 48000, EQ_AUDIO_MODE_STEREO) != NULL);
    }

    bgm = eq_audio_port_registry_open_owned(&registry, TEST_OWNER_A, EQ_AUDIO_BGM_PORT_ID, EQ_AUDIO_PORT_TYPE_BGM, 2048, 48000, EQ_AUDIO_MODE_STEREO);

    ASSERT_TRUE(bgm != NULL);
    ASSERT_EQ_I32(bgm->port_id, EQ_AUDIO_BGM_PORT_ID);
    ASSERT_EQ_U32(bgm->owner_id, TEST_OWNER_A);
    ASSERT_EQ_U32(bgm->config.type, EQ_AUDIO_PORT_TYPE_BGM);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), 9);
}

static void test_recovered_bgm_port_keeps_bgm_type(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);

    slot = eq_audio_port_registry_recover_config_owned(&registry,
                                                       TEST_OWNER_A,
                                                       EQ_AUDIO_BGM_PORT_ID,
                                                       eq_audio_port_type_for_recovered_id(EQ_AUDIO_BGM_PORT_ID),
                                                       2048,
                                                       48000,
                                                       EQ_AUDIO_MODE_STEREO);

    ASSERT_TRUE(slot != NULL);
    ASSERT_EQ_I32(slot->port_id, EQ_AUDIO_BGM_PORT_ID);
    ASSERT_EQ_U32(slot->owner_id, TEST_OWNER_A);
    ASSERT_EQ_U32(slot->config.type, EQ_AUDIO_PORT_TYPE_BGM);
}

static void test_begin_processing_allows_different_ports_but_rejects_same_port_reentry(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *port7;
    eq_audio_tracked_port_t *port256;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO) != NULL);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 256, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    port7 = eq_audio_port_registry_begin_processing(&registry, 7);
    ASSERT_TRUE(port7 != NULL);
    ASSERT_TRUE(eq_audio_port_registry_begin_processing(&registry, 7) == NULL);

    port256 = eq_audio_port_registry_begin_processing(&registry, 256);
    ASSERT_TRUE(port256 != NULL);
    ASSERT_TRUE(port256 != port7);

    eq_audio_port_registry_end_processing(port7);
    eq_audio_port_registry_end_processing(port256);

    ASSERT_TRUE(eq_audio_port_registry_begin_processing(&registry, 7) == port7);
    eq_audio_port_registry_end_processing(port7);
}

static void test_release_defers_reset_until_processing_finishes(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 256, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    slot = eq_audio_port_registry_begin_processing(&registry, 256);
    ASSERT_TRUE(slot != NULL);
    ASSERT_EQ_I32(eq_audio_port_registry_release(&registry, 256), 0);
    ASSERT_TRUE(eq_audio_port_registry_find(&registry, 256) == NULL);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), 0);
    ASSERT_TRUE(slot->processing != 0);
    ASSERT_TRUE(slot->release_pending != 0);
    ASSERT_EQ_U32(slot->config.in_use, 1);
    ASSERT_EQ_U32(slot->config.len, 2048);
    ASSERT_EQ_U32(slot->config.freq, 48000);
    ASSERT_EQ_U32(slot->config.channels, 2);

    eq_audio_port_registry_end_processing(slot);
    ASSERT_EQ_I32(slot->port_id, EQ_AUDIO_PORT_ID_UNUSED);
    ASSERT_TRUE(slot->processing == 0);
    ASSERT_TRUE(slot->release_pending == 0);
}

static void test_release_pending_slot_is_not_reused_while_processing(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;
    eq_audio_tracked_port_t *new_slot;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 256, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    slot = eq_audio_port_registry_begin_processing(&registry, 256);
    ASSERT_TRUE(slot != NULL);
    ASSERT_EQ_I32(eq_audio_port_registry_release(&registry, 256), 0);

    new_slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(new_slot != NULL);
    ASSERT_TRUE(new_slot != slot);
    ASSERT_EQ_I32(slot->port_id, 256);
    ASSERT_TRUE(slot->processing != 0);
    ASSERT_TRUE(slot->release_pending != 0);
    ASSERT_EQ_U32(slot->config.in_use, 1);

    eq_audio_port_registry_end_processing(slot);
}

static void test_invalid_config_while_processing_preserves_active_config_until_drain(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 256, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    slot = eq_audio_port_registry_begin_processing(&registry, 256);
    ASSERT_TRUE(slot != NULL);
    ASSERT_EQ_I32(eq_audio_port_registry_set_config(&registry, 256, 65, -1, -1), -1);
    ASSERT_TRUE(eq_audio_port_registry_find(&registry, 256) == NULL);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), 0);
    ASSERT_TRUE(slot->processing != 0);
    ASSERT_TRUE(slot->release_pending != 0);
    ASSERT_EQ_U32(slot->config.in_use, 1);
    ASSERT_EQ_U32(slot->config.len, 2048);
    ASSERT_EQ_U32(slot->config.freq, 48000);
    ASSERT_EQ_U32(slot->config.channels, 2);

    eq_audio_port_registry_mark_processing_complete(slot);
    eq_audio_port_registry_drain_completed(&registry);
    ASSERT_EQ_I32(slot->port_id, EQ_AUDIO_PORT_ID_UNUSED);
    ASSERT_EQ_U32(slot->config.in_use, 0);
}

static void test_reopening_port_gets_new_generation_and_fresh_dsp(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *first;
    eq_audio_tracked_port_t *second;
    uint32_t first_generation;

    eq_audio_port_registry_init(&registry);

    first = eq_audio_port_registry_open(&registry, 256, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(first != NULL);
    first_generation = first->generation;
    ASSERT_TRUE(first_generation != 0);

    first->dsp.preamp = 123.0f;
    first->last_dirty = 99;
    first->last_route = EQ_ROUTE_SPEAKER;

    ASSERT_EQ_I32(eq_audio_port_registry_release(&registry, 256), 0);
    second = eq_audio_port_registry_open(&registry, 256, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(second != NULL);
    ASSERT_TRUE(second->generation != 0);
    ASSERT_TRUE(second->generation != first_generation);
    ASSERT_EQ_U32(second->last_dirty, 0);
    ASSERT_EQ_U32(second->last_route, EQ_ROUTE_UNKNOWN);
    ASSERT_EQ_U32(second->dsp.sample_rate, 48000);
    ASSERT_TRUE(second->dsp.preamp != 123.0f);
}

static void test_reopening_released_processing_port_uses_separate_generation(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *old_slot;
    eq_audio_tracked_port_t *new_slot;
    uint32_t old_generation;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 256, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    old_slot = eq_audio_port_registry_begin_processing(&registry, 256);
    ASSERT_TRUE(old_slot != NULL);
    old_generation = old_slot->generation;
    ASSERT_TRUE(old_generation != 0);
    ASSERT_EQ_I32(eq_audio_port_registry_release(&registry, 256), 0);

    new_slot = eq_audio_port_registry_open(&registry, 256, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(new_slot != NULL);
    ASSERT_TRUE(new_slot != old_slot);
    ASSERT_TRUE(new_slot->generation != 0);
    ASSERT_TRUE(new_slot->generation != old_generation);
    ASSERT_TRUE(old_slot->processing != 0);
    ASSERT_TRUE(old_slot->release_pending != 0);

    eq_audio_port_registry_end_processing(old_slot);
    ASSERT_EQ_U32(old_slot->generation, 0);
}

static void test_reopening_processing_port_does_not_reset_active_slot(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *old_slot;
    eq_audio_tracked_port_t *new_slot;
    uint32_t old_generation;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 7, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    old_slot = eq_audio_port_registry_begin_processing(&registry, 7);
    ASSERT_TRUE(old_slot != NULL);
    old_generation = old_slot->generation;
    ASSERT_TRUE(old_generation != 0);
    old_slot->scratch[0] = 1234;

    new_slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(new_slot != NULL);
    ASSERT_TRUE(new_slot != old_slot);
    ASSERT_TRUE(new_slot->generation != 0);
    ASSERT_TRUE(new_slot->generation != old_generation);
    ASSERT_TRUE(old_slot->processing != 0);
    ASSERT_EQ_U32(old_slot->generation, old_generation);
    ASSERT_EQ_U32(old_slot->config.in_use, 1);
    ASSERT_EQ_U32(old_slot->config.len, 2048);
    ASSERT_EQ_I32(old_slot->scratch[0], 1234);

    eq_audio_port_registry_end_processing(old_slot);
}

static void test_full_registry_reopen_processing_port_does_not_retire_active_slot(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *old_slot;
    uint32_t old_generation;

    eq_audio_port_registry_init(&registry);
    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        ASSERT_TRUE(eq_audio_port_registry_open(&registry, 100 + i, 0, 256, 48000, EQ_AUDIO_MODE_STEREO) != NULL);
    }

    old_slot = eq_audio_port_registry_begin_processing(&registry, 100);
    ASSERT_TRUE(old_slot != NULL);
    old_generation = old_slot->generation;
    ASSERT_TRUE(old_generation != 0);

    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 100, 0, 1024, 48000, EQ_AUDIO_MODE_STEREO) == NULL);
    ASSERT_TRUE(eq_audio_port_registry_find(&registry, 100) == old_slot);
    ASSERT_EQ_U32(old_slot->generation, old_generation);
    ASSERT_EQ_U32(old_slot->release_pending, 0);
    ASSERT_EQ_U32(old_slot->config.in_use, 1);
    ASSERT_EQ_U32(old_slot->config.len, 256);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), EQ_AUDIO_MAX_TRACKED_PORTS);

    eq_audio_port_registry_end_processing(old_slot);
}

static void test_invalid_reopen_does_not_destroy_existing_port(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;
    uint32_t generation;

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);
    generation = slot->generation;

    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 7, 0, 0, 48000, EQ_AUDIO_MODE_STEREO) == NULL);
    ASSERT_TRUE(eq_audio_port_registry_find(&registry, 7) == slot);
    ASSERT_EQ_U32(slot->generation, generation);
    ASSERT_EQ_U32(slot->config.in_use, 1);
    ASSERT_EQ_U32(slot->config.len, 256);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), 1);
}

static void test_completed_processing_drains_before_reentry(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;
    eq_audio_tracked_port_t *again;
    uint32_t generation;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    slot = eq_audio_port_registry_begin_processing(&registry, 7);
    ASSERT_TRUE(slot != NULL);
    generation = slot->generation;
    eq_audio_port_registry_mark_processing_complete(slot);

    again = eq_audio_port_registry_begin_processing(&registry, 7);
    ASSERT_TRUE(again == slot);
    ASSERT_EQ_U32(again->generation, generation);
    ASSERT_TRUE(again->processing != 0);
    ASSERT_EQ_U32(again->processing_complete, 0);

    eq_audio_port_registry_mark_processing_complete(again);
    eq_audio_port_registry_drain_completed(&registry);
    ASSERT_TRUE(slot->processing == 0);
}

static void test_incomplete_processing_is_not_drained(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    slot = eq_audio_port_registry_begin_processing(&registry, 7);
    ASSERT_TRUE(slot != NULL);
    eq_audio_port_registry_drain_completed(&registry);
    ASSERT_TRUE(slot->processing != 0);
    ASSERT_TRUE(eq_audio_port_registry_begin_processing(&registry, 7) == NULL);

    eq_audio_port_registry_end_processing(slot);
}

static void test_completed_release_pending_drains_to_unused(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 256, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    slot = eq_audio_port_registry_begin_processing(&registry, 256);
    ASSERT_TRUE(slot != NULL);
    ASSERT_EQ_I32(eq_audio_port_registry_release(&registry, 256), 0);
    eq_audio_port_registry_mark_processing_complete(slot);
    eq_audio_port_registry_drain_completed(&registry);

    ASSERT_EQ_I32(slot->port_id, EQ_AUDIO_PORT_ID_UNUSED);
    ASSERT_EQ_U32(slot->generation, 0);
    ASSERT_EQ_U32(eq_audio_port_registry_count(&registry), 0);
}

static void test_set_config_defers_until_processing_finishes(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);
    ASSERT_TRUE(eq_audio_port_registry_open(&registry, 256, 0, 2048, 48000, EQ_AUDIO_MODE_STEREO) != NULL);

    slot = eq_audio_port_registry_begin_processing(&registry, 256);
    ASSERT_TRUE(slot != NULL);
    ASSERT_EQ_I32(eq_audio_port_registry_set_config(&registry, 256, 256, -1, -1), 0);
    ASSERT_EQ_U32(slot->config.len, 2048);
    ASSERT_EQ_U32(slot->pending_config_valid, 1);

    eq_audio_port_registry_end_processing(slot);
    ASSERT_EQ_U32(slot->config.len, 256);
    ASSERT_EQ_U32(slot->pending_config_valid, 0);
    ASSERT_TRUE(slot->processing == 0);
    ASSERT_TRUE(eq_audio_port_registry_find(&registry, 256) == slot);
}

static void test_set_config_clears_retry_bypass_guard(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;
    int16_t buffer[512];

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);

    eq_audio_tracked_port_note_output_error(slot, buffer);
    ASSERT_EQ_I32(eq_audio_port_registry_set_config(&registry, 7, 512, -1, -1), 0);
    ASSERT_EQ_U32(slot->config.len, 512);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 0);
}

static void test_deferred_set_config_clears_retry_bypass_guard_on_drain(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;
    int16_t buffer[512];

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);
    ASSERT_TRUE(eq_audio_port_registry_begin_processing(&registry, 7) == slot);

    eq_audio_tracked_port_note_output_error(slot, buffer);
    ASSERT_EQ_I32(eq_audio_port_registry_set_config(&registry, 7, 512, -1, -1), 0);
    ASSERT_EQ_U32(slot->pending_config_valid, 1);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 1);

    eq_audio_tracked_port_note_output_error(slot, buffer);
    eq_audio_port_registry_mark_processing_complete(slot);
    eq_audio_port_registry_drain_completed(&registry);

    ASSERT_EQ_U32(slot->config.len, 512);
    ASSERT_EQ_U32(slot->pending_config_valid, 0);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 0);
}

static void test_set_config_invalidates_cached_control_fallback(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);

    slot->control_cache_valid = 1;
    slot->control_cache.dirty_counter = 77;

    ASSERT_EQ_I32(eq_audio_port_registry_set_config(&registry, 7, 512, -1, -1), 0);
    ASSERT_EQ_U32(slot->config.len, 512);
    ASSERT_EQ_U32(slot->control_cache_valid, 0);
    ASSERT_EQ_U32(slot->control_cache.dirty_counter, 0);
}

static void test_deferred_set_config_invalidates_cached_control_on_drain(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);
    ASSERT_TRUE(eq_audio_port_registry_begin_processing(&registry, 7) == slot);

    slot->control_cache_valid = 1;
    slot->control_cache.dirty_counter = 88;

    ASSERT_EQ_I32(eq_audio_port_registry_set_config(&registry, 7, 512, -1, -1), 0);
    ASSERT_EQ_U32(slot->pending_config_valid, 1);
    ASSERT_EQ_U32(slot->control_cache_valid, 1);

    eq_audio_port_registry_mark_processing_complete(slot);
    eq_audio_port_registry_drain_completed(&registry);

    ASSERT_EQ_U32(slot->config.len, 512);
    ASSERT_EQ_U32(slot->pending_config_valid, 0);
    ASSERT_EQ_U32(slot->control_cache_valid, 0);
    ASSERT_EQ_U32(slot->control_cache.dirty_counter, 0);
}

static void test_recover_length_only_config_preserves_dsp_state(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;
    eq_audio_tracked_port_t *recovered;
    uint32_t generation;

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);

    generation = slot->generation;
    slot->dsp.sample_rate = 48000;
    slot->dsp.preamp = 0.75f;
    slot->dsp.target_preamp = 0.80f;
    slot->dsp.smooth_remaining = 37;
    slot->last_dirty = 99;
    slot->last_route = EQ_ROUTE_SPEAKER;

    recovered = eq_audio_port_registry_recover_config(&registry, 7, 0, 1024, 48000, EQ_AUDIO_MODE_STEREO);

    ASSERT_TRUE(recovered == slot);
    ASSERT_EQ_U32(slot->config.len, 1024);
    ASSERT_EQ_U32(slot->config.freq, 48000);
    ASSERT_EQ_U32(slot->config.channels, 2);
    ASSERT_EQ_U32(slot->generation, generation);
    ASSERT_EQ_U32(slot->dsp.sample_rate, 48000);
    ASSERT_TRUE(slot->dsp.preamp == 0.75f);
    ASSERT_TRUE(slot->dsp.target_preamp == 0.80f);
    ASSERT_EQ_U32(slot->dsp.smooth_remaining, 37);
    ASSERT_EQ_U32(slot->last_dirty, 99);
    ASSERT_EQ_U32(slot->last_route, EQ_ROUTE_SPEAKER);
}

static void test_output_error_resets_dsp_state_without_dropping_config(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);

    slot->dsp.preamp = 123.0f;
    slot->last_dirty = 99;
    slot->last_route = EQ_ROUTE_SPEAKER;
    slot->control_cache_valid = 1;

    eq_audio_tracked_port_reset_dsp_state(slot);

    ASSERT_EQ_U32(slot->config.in_use, 1);
    ASSERT_EQ_U32(slot->config.len, 256);
    ASSERT_EQ_U32(slot->config.freq, 48000);
    ASSERT_EQ_U32(slot->config.channels, 2);
    ASSERT_EQ_U32(slot->dsp.sample_rate, 48000);
    ASSERT_TRUE(slot->dsp.preamp != 123.0f);
    ASSERT_EQ_U32(slot->last_dirty, 0);
    ASSERT_EQ_U32(slot->last_route, EQ_ROUTE_UNKNOWN);
    ASSERT_EQ_U32(slot->control_cache_valid, 1);
}

static void test_dsp_reset_clears_stale_retry_bypass_guard(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;
    int16_t buffer[512];

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);

    eq_audio_tracked_port_note_output_error(slot, buffer);
    eq_audio_tracked_port_reset_dsp_state(slot);

    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 0);
    ASSERT_EQ_U32(slot->config.in_use, 1);
    ASSERT_EQ_U32(slot->config.len, 256);
    ASSERT_EQ_U32(slot->config.freq, 48000);
}

static void test_retry_bypass_is_consumed_once_for_same_buffer_after_output_error(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;
    int16_t buffer[512];
    int16_t other_buffer[512];

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);

    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 0);

    eq_audio_tracked_port_note_output_error(slot, buffer);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 1);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 0);

    eq_audio_tracked_port_note_output_error(slot, buffer);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, other_buffer), 0);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 0);

    eq_audio_tracked_port_note_output_error(slot, buffer);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 1);
}

static void test_retry_bypass_ignores_null_buffers(void) {
    eq_audio_port_registry_t registry;
    eq_audio_tracked_port_t *slot;
    int16_t buffer[512];

    eq_audio_port_registry_init(&registry);
    slot = eq_audio_port_registry_open(&registry, 7, 0, 256, 48000, EQ_AUDIO_MODE_STEREO);
    ASSERT_TRUE(slot != NULL);

    eq_audio_tracked_port_note_output_error(slot, NULL);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, NULL), 0);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 0);

    eq_audio_tracked_port_note_output_error(slot, buffer);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, NULL), 0);
    ASSERT_EQ_U32(eq_audio_tracked_port_consume_retry_bypass(slot, buffer), 1);
}

int main(void) {
    test_sparse_port_id_is_tracked_without_using_id_as_array_index();
    test_sparse_port_config_and_release_use_port_id_lookup();
    test_same_port_id_can_be_tracked_for_different_owners();
    test_releasing_one_owner_keeps_same_port_for_other_owner();
    test_registry_tracks_bgm_after_all_main_ports_are_open();
    test_recovered_bgm_port_keeps_bgm_type();
    test_begin_processing_allows_different_ports_but_rejects_same_port_reentry();
    test_release_defers_reset_until_processing_finishes();
    test_release_pending_slot_is_not_reused_while_processing();
    test_invalid_config_while_processing_preserves_active_config_until_drain();
    test_reopening_port_gets_new_generation_and_fresh_dsp();
    test_reopening_released_processing_port_uses_separate_generation();
    test_reopening_processing_port_does_not_reset_active_slot();
    test_full_registry_reopen_processing_port_does_not_retire_active_slot();
    test_invalid_reopen_does_not_destroy_existing_port();
    test_completed_processing_drains_before_reentry();
    test_incomplete_processing_is_not_drained();
    test_completed_release_pending_drains_to_unused();
    test_set_config_defers_until_processing_finishes();
    test_set_config_clears_retry_bypass_guard();
    test_deferred_set_config_clears_retry_bypass_guard_on_drain();
    test_set_config_invalidates_cached_control_fallback();
    test_deferred_set_config_invalidates_cached_control_on_drain();
    test_recover_length_only_config_preserves_dsp_state();
    test_output_error_resets_dsp_state_without_dropping_config();
    test_dsp_reset_clears_stale_retry_bypass_guard();
    test_retry_bypass_is_consumed_once_for_same_buffer_after_output_error();
    test_retry_bypass_ignores_null_buffers();

    if (failures) {
        printf("%d port-registry failure(s)\n", failures);
        return 1;
    }

    return 0;
}
