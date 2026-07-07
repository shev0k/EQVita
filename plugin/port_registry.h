#pragma once

#include <stdint.h>
#include <string.h>

#include "dsp.h"
#include "port_state.h"

#define EQ_AUDIO_MAX_TRACKED_PORTS 16
#define EQ_AUDIO_PORT_ID_UNUSED (-1)
#define EQ_AUDIO_SCRATCH_MAX_FRAMES 4096

typedef struct eq_audio_tracked_port
{
    uint32_t owner_id;
    int port_id;
    eq_audio_port_config_t config;
    eq_dsp_state_t dsp;
    eq_control_t control_cache;
    uint32_t generation;
    uint32_t last_dirty;
    uint32_t diag_active_sample_count;
    uintptr_t retry_bypass_buf;
    int32_t last_preamp_mdB;
    int32_t last_effective_preamp_mdB;
    int32_t last_max_boost_mdB;
    uint8_t last_route;
    uint8_t last_headroom_mode;
    uint8_t last_hpf_enabled;
    uint8_t processing;
    uint8_t processing_complete;
    uint8_t release_pending;
    uint8_t pending_config_valid;
    uint8_t control_cache_valid;
    uint8_t retry_bypass_valid;
    eq_audio_port_config_t pending_config;
    int16_t original[EQ_AUDIO_SCRATCH_MAX_FRAMES * EQ_DSP_MAX_CHANNELS];
    int16_t scratch[EQ_AUDIO_SCRATCH_MAX_FRAMES * EQ_DSP_MAX_CHANNELS];
} eq_audio_tracked_port_t;

typedef struct eq_audio_port_registry
{
    eq_audio_tracked_port_t slots[EQ_AUDIO_MAX_TRACKED_PORTS];
    uint32_t next_generation;
} eq_audio_port_registry_t;

static inline void eq_audio_tracked_port_reset(eq_audio_tracked_port_t *slot)
{
    if (!slot) {
        return;
    }

    memset(slot, 0, sizeof(*slot));
    slot->port_id = EQ_AUDIO_PORT_ID_UNUSED;
    slot->last_route = EQ_ROUTE_UNKNOWN;
}

static inline void eq_audio_tracked_port_note_output_error(eq_audio_tracked_port_t *slot, const void *buf)
{
    if (!slot || !buf) {
        return;
    }

    slot->retry_bypass_buf = (uintptr_t)buf;
    slot->retry_bypass_valid = 1;
}

static inline int eq_audio_tracked_port_consume_retry_bypass(eq_audio_tracked_port_t *slot, const void *buf)
{
    if (!slot || !buf || !slot->retry_bypass_valid) {
        return 0;
    }

    if (slot->retry_bypass_buf != (uintptr_t)buf) {
        slot->retry_bypass_valid = 0;
        slot->retry_bypass_buf = 0;
        return 0;
    }

    slot->retry_bypass_valid = 0;
    slot->retry_bypass_buf = 0;
    return 1;
}

static inline void eq_audio_tracked_port_reset_dsp_state(eq_audio_tracked_port_t *slot)
{
    if (!slot || !slot->config.in_use) {
        return;
    }

    eq_dsp_init(&slot->dsp, slot->config.freq);
    slot->last_dirty = 0;
    slot->last_route = EQ_ROUTE_UNKNOWN;
    slot->retry_bypass_valid = 0;
    slot->retry_bypass_buf = 0;
}

static inline void eq_audio_port_registry_init(eq_audio_port_registry_t *registry)
{
    if (!registry) {
        return;
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        eq_audio_tracked_port_reset(&registry->slots[i]);
    }
    registry->next_generation = 1;
}

static inline uint32_t eq_audio_port_registry_take_generation(eq_audio_port_registry_t *registry)
{
    uint32_t generation;

    if (!registry) {
        return 0;
    }

    generation = registry->next_generation;
    if (generation == 0) {
        generation = 1;
    }

    registry->next_generation = generation + 1;
    if (registry->next_generation == 0) {
        registry->next_generation = 1;
    }

    return generation;
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_find_owned(eq_audio_port_registry_t *registry,
                                                                         uint32_t owner_id,
                                                                         int port_id)
{
    if (!registry) {
        return NULL;
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        if (registry->slots[i].owner_id == owner_id &&
            registry->slots[i].port_id == port_id &&
            registry->slots[i].config.in_use &&
            !registry->slots[i].release_pending) {
            return &registry->slots[i];
        }
    }

    return NULL;
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_find(eq_audio_port_registry_t *registry, int port_id)
{
    return eq_audio_port_registry_find_owned(registry, 0u, port_id);
}

static inline void eq_audio_port_registry_end_processing(eq_audio_tracked_port_t *slot);
static inline void eq_audio_port_registry_drain_completed(eq_audio_port_registry_t *registry);

static inline eq_audio_tracked_port_t *eq_audio_port_registry_alloc_owned(eq_audio_port_registry_t *registry,
                                                                          uint32_t owner_id,
                                                                          int port_id)
{
    eq_audio_tracked_port_t *processing_match = NULL;

    if (!registry) {
        return NULL;
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        eq_audio_tracked_port_t *slot = &registry->slots[i];
        if (slot->owner_id == owner_id &&
            slot->port_id == port_id &&
            slot->config.in_use &&
            !slot->release_pending) {
            if (slot->processing) {
                processing_match = slot;
                break;
            }
            return slot;
        }
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        if (!registry->slots[i].config.in_use && !registry->slots[i].processing) {
            if (processing_match) {
                processing_match->release_pending = 1;
            }
            eq_audio_tracked_port_reset(&registry->slots[i]);
            registry->slots[i].owner_id = owner_id;
            registry->slots[i].port_id = port_id;
            return &registry->slots[i];
        }
    }

    return NULL;
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_alloc(eq_audio_port_registry_t *registry, int port_id)
{
    return eq_audio_port_registry_alloc_owned(registry, 0u, port_id);
}

static inline void eq_audio_tracked_port_apply_config(eq_audio_tracked_port_t *slot,
                                                      const eq_audio_port_config_t *next_config)
{
    uint32_t old_freq;
    uint8_t old_channels;

    if (!slot || !next_config || !next_config->in_use) {
        return;
    }

    old_freq = slot->config.freq;
    old_channels = slot->config.channels;
    slot->config = *next_config;
    slot->retry_bypass_valid = 0;
    slot->retry_bypass_buf = 0;
    slot->control_cache_valid = 0;
    memset(&slot->control_cache, 0, sizeof(slot->control_cache));

    if (slot->config.freq != old_freq || slot->config.channels != old_channels || slot->dsp.sample_rate == 0) {
        eq_dsp_init(&slot->dsp, slot->config.freq);
        slot->last_dirty = 0;
        slot->last_route = EQ_ROUTE_UNKNOWN;
    }
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_open_owned(eq_audio_port_registry_t *registry,
                                                                         uint32_t owner_id,
                                                                         int port_id,
                                                                         uint32_t type,
                                                                         uint32_t len,
                                                                         uint32_t freq,
                                                                         int mode)
{
    eq_audio_tracked_port_t *slot;
    eq_audio_port_config_t config;
    uint32_t generation;

    memset(&config, 0, sizeof(config));
    if (eq_audio_port_open(&config, type, len, freq, mode) < 0) {
        return NULL;
    }

    eq_audio_port_registry_drain_completed(registry);
    slot = eq_audio_port_registry_alloc_owned(registry, owner_id, port_id);
    if (!slot) {
        return NULL;
    }

    generation = eq_audio_port_registry_take_generation(registry);
    eq_audio_tracked_port_reset(slot);
    slot->owner_id = owner_id;
    slot->port_id = port_id;
    slot->generation = generation;
    slot->config = config;
    eq_dsp_init(&slot->dsp, freq);
    slot->last_dirty = 0;
    slot->last_route = EQ_ROUTE_UNKNOWN;
    return slot;
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_open(eq_audio_port_registry_t *registry,
                                                                   int port_id,
                                                                   uint32_t type,
                                                                   uint32_t len,
                                                                   uint32_t freq,
                                                                   int mode)
{
    return eq_audio_port_registry_open_owned(registry, 0u, port_id, type, len, freq, mode);
}

static inline int eq_audio_port_registry_set_config_owned(eq_audio_port_registry_t *registry,
                                                          uint32_t owner_id,
                                                          int port_id,
                                                          uint32_t len,
                                                          int freq,
                                                          int mode)
{
    eq_audio_tracked_port_t *slot;
    eq_audio_port_config_t next_config;

    eq_audio_port_registry_drain_completed(registry);
    slot = eq_audio_port_registry_find_owned(registry, owner_id, port_id);
    if (!slot) {
        return -1;
    }

    next_config = slot->config;
    if (eq_audio_port_set_config(&next_config, len, freq, mode) < 0) {
        if (slot->processing) {
            slot->release_pending = 1;
        } else {
            eq_audio_tracked_port_reset(slot);
        }
        return -1;
    }

    if (slot->processing) {
        slot->pending_config = next_config;
        slot->pending_config_valid = 1;
        return 0;
    }

    eq_audio_tracked_port_apply_config(slot, &next_config);
    return 0;
}

static inline int eq_audio_port_registry_set_config(eq_audio_port_registry_t *registry,
                                                    int port_id,
                                                    uint32_t len,
                                                    int freq,
                                                    int mode)
{
    return eq_audio_port_registry_set_config_owned(registry, 0u, port_id, len, freq, mode);
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_recover_config_owned(eq_audio_port_registry_t *registry,
                                                                                   uint32_t owner_id,
                                                                                   int port_id,
                                                                                   uint32_t type,
                                                                                   uint32_t len,
                                                                                   uint32_t freq,
                                                                                   int mode)
{
    eq_audio_tracked_port_t *slot;
    eq_audio_port_config_t next_config;

    if (!registry) {
        return NULL;
    }

    eq_audio_port_registry_drain_completed(registry);
    slot = eq_audio_port_registry_find_owned(registry, owner_id, port_id);
    if (!slot) {
        return eq_audio_port_registry_open_owned(registry, owner_id, port_id, type, len, freq, mode);
    }

    next_config = slot->config;
    if (eq_audio_port_set_config(&next_config, len, (int)freq, mode) < 0) {
        return NULL;
    }

    if (slot->processing) {
        slot->pending_config = next_config;
        slot->pending_config_valid = 1;
        return slot;
    }

    eq_audio_tracked_port_apply_config(slot, &next_config);
    return slot;
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_recover_config(eq_audio_port_registry_t *registry,
                                                                             int port_id,
                                                                             uint32_t type,
                                                                             uint32_t len,
                                                                             uint32_t freq,
                                                                             int mode)
{
    return eq_audio_port_registry_recover_config_owned(registry, 0u, port_id, type, len, freq, mode);
}

static inline int eq_audio_port_registry_release_owned(eq_audio_port_registry_t *registry,
                                                       uint32_t owner_id,
                                                       int port_id)
{
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_drain_completed(registry);
    slot = eq_audio_port_registry_find_owned(registry, owner_id, port_id);
    if (!slot) {
        return -1;
    }

    if (slot->processing) {
        slot->release_pending = 1;
        return 0;
    }

    eq_audio_tracked_port_reset(slot);
    return 0;
}

static inline int eq_audio_port_registry_release(eq_audio_port_registry_t *registry, int port_id)
{
    return eq_audio_port_registry_release_owned(registry, 0u, port_id);
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_begin_processing_owned(eq_audio_port_registry_t *registry,
                                                                                    uint32_t owner_id,
                                                                                    int port_id)
{
    eq_audio_tracked_port_t *slot;

    eq_audio_port_registry_drain_completed(registry);
    slot = eq_audio_port_registry_find_owned(registry, owner_id, port_id);
    if (!slot || slot->processing) {
        return NULL;
    }

    slot->processing = 1;
    slot->processing_complete = 0;
    return slot;
}

static inline eq_audio_tracked_port_t *eq_audio_port_registry_begin_processing(eq_audio_port_registry_t *registry, int port_id)
{
    return eq_audio_port_registry_begin_processing_owned(registry, 0u, port_id);
}

static inline void eq_audio_port_registry_mark_processing_complete(eq_audio_tracked_port_t *slot)
{
    if (!slot || !slot->processing) {
        return;
    }

    slot->processing_complete = 1;
}

static inline void eq_audio_port_registry_end_processing(eq_audio_tracked_port_t *slot)
{
    if (!slot) {
        return;
    }

    slot->processing = 0;
    slot->processing_complete = 0;
    if (slot->release_pending) {
        eq_audio_tracked_port_reset(slot);
    } else if (slot->pending_config_valid) {
        eq_audio_port_config_t next_config = slot->pending_config;
        slot->pending_config_valid = 0;
        memset(&slot->pending_config, 0, sizeof(slot->pending_config));
        eq_audio_tracked_port_apply_config(slot, &next_config);
    }
}

static inline void eq_audio_port_registry_drain_completed(eq_audio_port_registry_t *registry)
{
    if (!registry) {
        return;
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        eq_audio_tracked_port_t *slot = &registry->slots[i];
        if (slot->processing && slot->processing_complete) {
            eq_audio_port_registry_end_processing(slot);
        }
    }
}

static inline uint32_t eq_audio_port_registry_count(const eq_audio_port_registry_t *registry)
{
    uint32_t count = 0;

    if (!registry) {
        return 0;
    }

    for (int i = 0; i < EQ_AUDIO_MAX_TRACKED_PORTS; ++i) {
        if (registry->slots[i].config.in_use && !registry->slots[i].release_pending) {
            count++;
        }
    }

    return count;
}
