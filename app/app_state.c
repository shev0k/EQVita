#include "app_state.h"

#include <string.h>

static int normalize_slot(int slot)
{
    if (slot < 0 || slot >= EQVITA_PRESET_SLOT_COUNT) {
        return 0;
    }
    return slot;
}

void eqvita_app_state_init(eqvita_app_state_t *state)
{
    if (!state) {
        return;
    }

    memset(state, 0, sizeof(*state));
    eq_control_init_defaults(&state->control);
    state->preset_slot = 0;
    state->plugin_compatible = 1;
    state->last_set_result = 0;
    state->last_status_result = 0;
}

int eqvita_app_state_preset_slot(const eqvita_app_state_t *state)
{
    return state ? state->preset_slot : 0;
}

void eqvita_app_state_set_preset_slot(eqvita_app_state_t *state, int slot)
{
    if (!state) {
        return;
    }
    state->preset_slot = normalize_slot(slot);
}

void eqvita_app_state_adjust_preset_slot(eqvita_app_state_t *state, int delta)
{
    int slot;

    if (!state) {
        return;
    }

    slot = state->preset_slot + delta;
    while (slot < 0) {
        slot += EQVITA_PRESET_SLOT_COUNT;
    }
    while (slot >= EQVITA_PRESET_SLOT_COUNT) {
        slot -= EQVITA_PRESET_SLOT_COUNT;
    }
    state->preset_slot = slot;
}

eq_control_t *eqvita_app_state_begin_edit(eqvita_app_state_t *state)
{
    if (!state) {
        return NULL;
    }

    state->draft_control = state->control;
    state->draft_active = 1;
    return &state->draft_control;
}

void eqvita_app_state_commit_edit(eqvita_app_state_t *state)
{
    if (!state || !state->draft_active) {
        return;
    }

    state->control = state->draft_control;
    state->draft_active = 0;
    eqvita_app_state_mark_boot_dirty(state);
    eqvita_app_state_mark_current_preset_dirty(state);
}

void eqvita_app_state_rollback_edit(eqvita_app_state_t *state)
{
    if (!state) {
        return;
    }

    memset(&state->draft_control, 0, sizeof(state->draft_control));
    state->draft_active = 0;
}

void eqvita_app_state_set_control(eqvita_app_state_t *state, const eq_control_t *control)
{
    if (!state || !control) {
        return;
    }

    state->control = *control;
    state->draft_active = 0;
    memset(&state->draft_control, 0, sizeof(state->draft_control));
}

void eqvita_app_state_mark_boot_dirty(eqvita_app_state_t *state)
{
    if (state) {
        state->boot_dirty = 1;
    }
}

void eqvita_app_state_mark_boot_saved(eqvita_app_state_t *state)
{
    if (state) {
        state->boot_dirty = 0;
    }
}

int eqvita_app_state_boot_dirty(const eqvita_app_state_t *state)
{
    return state ? state->boot_dirty != 0 : 0;
}

void eqvita_app_state_mark_current_preset_dirty(eqvita_app_state_t *state)
{
    if (state && state->preset_slot >= 0 && state->preset_slot < EQVITA_PRESET_SLOT_COUNT) {
        state->preset_dirty[state->preset_slot] = 1;
    }
}

void eqvita_app_state_mark_current_preset_saved(eqvita_app_state_t *state)
{
    if (state && state->preset_slot >= 0 && state->preset_slot < EQVITA_PRESET_SLOT_COUNT) {
        state->preset_dirty[state->preset_slot] = 0;
    }
}

int eqvita_app_state_current_preset_dirty(const eqvita_app_state_t *state)
{
    if (!state || state->preset_slot < 0 || state->preset_slot >= EQVITA_PRESET_SLOT_COUNT) {
        return 0;
    }
    return state->preset_dirty[state->preset_slot] != 0;
}

void eqvita_app_state_set_status_stale(eqvita_app_state_t *state, int stale)
{
    if (state) {
        state->status_stale = stale ? 1u : 0u;
    }
}

int eqvita_app_state_status_stale(const eqvita_app_state_t *state)
{
    return state ? state->status_stale != 0 : 0;
}

int eqvita_app_state_prepare_route_profile_candidate(
    const eq_route_profile_bank_t *current_bank,
    const char current_source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX],
    uint8_t route,
    const eq_control_t *control,
    eq_route_profile_bank_t *out_bank,
    char out_source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX])
{
    int profile_index;
    eq_route_profile_bank_t validated_bank;
    eq_control_t validated_control;

    if (!current_bank || !current_source_names || !control || !out_bank || !out_source_names) {
        return -1;
    }
    validated_bank = *current_bank;
    validated_control = *control;
    if (eq_route_profile_bank_validate(&validated_bank) < 0 ||
        eq_control_validate(&validated_control) < 0) return -1;
    profile_index = eq_route_profile_index(route);
    if (profile_index < 0 || !eq_route_profile_bank_has_route(&validated_bank, route)) {
        return -1;
    }

    *out_bank = validated_bank;
    memcpy(out_source_names, current_source_names,
           EQ_ROUTE_PROFILE_COUNT * EQ_ROUTE_PROFILE_SOURCE_NAME_MAX);
    out_bank->profiles[profile_index] = validated_control;
    out_bank->profiles[profile_index].enabled = 1;
    out_bank->profiles[profile_index].speaker_only = 0;
    out_bank->profiles[profile_index].route_hint = EQ_ROUTE_UNKNOWN;
    out_bank->profiles[profile_index].dirty_counter = 0;
    out_bank->selected_route = route;
    if (validated_control.eq_mode == EQ_MODE_GRAPHIC) {
        out_source_names[profile_index][0] = '\0';
    }
    eq_route_profile_bank_touch(out_bank);
    return eq_route_profile_bank_validate(out_bank);
}
