#pragma once

#include "../common/eq_shared.h"

#define EQVITA_PRESET_SLOT_COUNT 3

typedef struct eqvita_app_state
{
    eq_control_t control;
    eq_control_t draft_control;
    int draft_active;
    int preset_slot;
    uint8_t boot_dirty;
    uint8_t preset_dirty[EQVITA_PRESET_SLOT_COUNT];
    uint8_t plugin_compatible;
    uint8_t status_stale;
    int last_set_result;
    int last_status_result;
} eqvita_app_state_t;

void eqvita_app_state_init(eqvita_app_state_t *state);
int eqvita_app_state_preset_slot(const eqvita_app_state_t *state);
void eqvita_app_state_set_preset_slot(eqvita_app_state_t *state, int slot);
void eqvita_app_state_adjust_preset_slot(eqvita_app_state_t *state, int delta);
eq_control_t *eqvita_app_state_begin_edit(eqvita_app_state_t *state);
void eqvita_app_state_commit_edit(eqvita_app_state_t *state);
void eqvita_app_state_rollback_edit(eqvita_app_state_t *state);
void eqvita_app_state_set_control(eqvita_app_state_t *state, const eq_control_t *control);
void eqvita_app_state_mark_boot_dirty(eqvita_app_state_t *state);
void eqvita_app_state_mark_boot_saved(eqvita_app_state_t *state);
int eqvita_app_state_boot_dirty(const eqvita_app_state_t *state);
void eqvita_app_state_mark_current_preset_dirty(eqvita_app_state_t *state);
void eqvita_app_state_mark_current_preset_saved(eqvita_app_state_t *state);
int eqvita_app_state_current_preset_dirty(const eqvita_app_state_t *state);
void eqvita_app_state_set_status_stale(eqvita_app_state_t *state, int stale);
int eqvita_app_state_status_stale(const eqvita_app_state_t *state);
int eqvita_app_state_prepare_route_profile_candidate(
    const eq_route_profile_bank_t *current_bank,
    const char current_source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX],
    uint8_t route,
    const eq_control_t *control,
    eq_route_profile_bank_t *out_bank,
    char out_source_names[EQ_ROUTE_PROFILE_COUNT][EQ_ROUTE_PROFILE_SOURCE_NAME_MAX]);
