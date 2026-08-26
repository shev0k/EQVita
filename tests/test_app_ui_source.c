#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EQVITA_SOURCE_DIR
#define EQVITA_SOURCE_DIR "."
#endif

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "ASSERT_TRUE failed at %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        exit(1); \
    } \
} while (0)

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    long size;
    char *data;

    ASSERT_TRUE(f != NULL);
    ASSERT_TRUE(fseek(f, 0, SEEK_END) == 0);
    size = ftell(f);
    ASSERT_TRUE(size >= 0);
    ASSERT_TRUE(fseek(f, 0, SEEK_SET) == 0);
    data = (char *)malloc((size_t)size + 1u);
    ASSERT_TRUE(data != NULL);
    ASSERT_TRUE(fread(data, 1, (size_t)size, f) == (size_t)size);
    data[size] = '\0';
    fclose(f);
    return data;
}

static int range_contains(const char *start, const char *end, const char *needle)
{
    size_t needle_len;
    if (!start || !end || !needle || end < start) {
        return 0;
    }

    needle_len = strlen(needle);
    if (needle_len == 0) {
        return 1;
    }

    for (const char *p = start; p + needle_len <= end; ++p) {
        if (strncmp(p, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static void test_ui_waits_for_rendering_before_resource_teardown(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/ui_vita.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "vita2d_wait_rendering_done()") != NULL);
    ASSERT_TRUE(strstr(source, "wait_for_render_idle") != NULL);
    free(source);
}

static void test_advanced_eq_exposes_preamp_control(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "#define ADV_ROW_PREAMP") != NULL);
    ASSERT_TRUE(strstr(source, "row == ADV_ROW_PREAMP") != NULL);
    ASSERT_TRUE(strstr(source, "g_screen == SCREEN_ADVANCED && row == ADV_ROW_PREAMP") != NULL);
    ASSERT_TRUE(strstr(source, "adjust_preamp(delta)") != NULL);
    free(source);
}

static void test_app_log_reports_audio_timing_budget(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "audio-budget") != NULL);
    ASSERT_TRUE(strstr(source, "budget_us") != NULL);
    ASSERT_TRUE(strstr(source, "debug_last_us") != NULL);
    ASSERT_TRUE(strstr(source, "debug_max_us") != NULL);
    free(source);
}

static void test_audio_budget_log_flags_current_block_not_stale_max(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "static void maybe_log_status(void)");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "static void ");
    ASSERT_TRUE(fn_end != NULL);

    ASSERT_TRUE(range_contains(fn_start, fn_end, "g_status.debug_last_us > budget_us"));
    ASSERT_TRUE(!range_contains(fn_start, fn_end, "g_status.debug_max_us > budget_us"));

    free(source);
}

static void test_app_log_has_run_ids_and_diagnostic_drain(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "g_log_run_id") != NULL);
    ASSERT_TRUE(strstr(source, "run_id=") != NULL);
    ASSERT_TRUE(strstr(source, "EqDrainDiagnostics") != NULL);
    ASSERT_TRUE(strstr(source, "diag-core: run=") != NULL);
    ASSERT_TRUE(strstr(source, "diag-level: run=") != NULL);
    ASSERT_TRUE(strstr(source, "evt=") != NULL);
    ASSERT_TRUE(strstr(source, "gen=") != NULL);
    ASSERT_TRUE(strstr(source, "type=") != NULL);
    ASSERT_TRUE(strstr(source, "in_peak=") != NULL);
    ASSERT_TRUE(strstr(source, "out_peak=") != NULL);
    ASSERT_TRUE(strstr(source, "eff_preamp=") != NULL);
    free(source);
}

static void test_diagnostic_logs_are_split_to_avoid_truncation(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(!strstr(source, "app_log(\"diag: run=%08x seq=%u evt=%s port=%d gen=%u type=%s len=%u sr=%u ch=%u route=%s reason=%s ret=%d elapsed_us=%u budget_us=%u clips=%d"));
    ASSERT_TRUE(strstr(source, "app_log(\"diag-core: run=%08x seq=%u evt=%s port=%d gen=%u type=%s len=%u sr=%u ch=%u route=%s reason=%s ret=%d\""));
    ASSERT_TRUE(strstr(source, "app_log(\"diag-time: run=%08x seq=%u elapsed_us=%u budget_us=%u dirty=%u flags=0x%02x\""));
    ASSERT_TRUE(strstr(source, "app_log(\"diag-level: run=%08x seq=%u clips=%d in_peak=%u/%u out_peak=%u/%u headroom=%s preamp=%d eff_preamp=%d max_boost=%d hpf=%u\""));

    free(source);
}

static void test_status_log_includes_slowest_block_context(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "max_port=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_len=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_sr=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_ch=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_budget_us=%u") != NULL);
    ASSERT_TRUE(strstr(source, "max_route=%s") != NULL);
    ASSERT_TRUE(strstr(source, "max_reason=%s") != NULL);
    ASSERT_TRUE(strstr(source, "max_clips=%d") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-stage: run=%08x control_us=%u registry_us=%u route_us=%u copy_in_us=%u retarget_us=%u dsp_us=%u copy_out_us=%u original_us=%u status_us=%u\"") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-time: run=%08x last_total_us=%u last_budget_us=%u last_margin_us=%d max_total_us=%u max_dsp_us=%u\"") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-margin: run=%08x min_margin_us=%d min_total_us=%u min_budget_us=%u min_port=%u min_len=%u min_sr=%u min_ch=%u\"") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-margin-stage: run=%08x min_route=%s min_reason=%s dsp_us=%u original_us=%u status_us=%u\"") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-margin-len: run=%08x len256_us=%d len1024_us=%d len2048_us=%d\"") != NULL);
    ASSERT_TRUE(strstr(source, "g_status.debug_max_us != last_max_us") != NULL);

    free(source);
}

static void test_status_log_splits_slowest_block_context(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "app_log(\"status: run=%08x route=%s active=%u reason=%s") != NULL);
    ASSERT_TRUE(strstr(source, "app_log(\"status-max: run=%08x max_us=%u max_budget_us=%u") != NULL);
    ASSERT_TRUE(!strstr(source, "status: run=%08x route=%s active=%u reason=%s sr=%u port=%u len=%u ch=%u runs=%u ports=%u busy=%u unknown=%u last_us=%u max_us=%u max_port=%u"));

    free(source);
}

static void test_builtin_presets_persist_active_slot_after_apply(void)
{
    char path[512];
    char *source;
    char *stock;
    char *mod;
    char *stock_end;
    char *mod_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    stock = strstr(source, "static void apply_preset_stock_depth(void)");
    mod = strstr(source, "static void apply_preset_mod_switch(void)");
    ASSERT_TRUE(stock != NULL);
    ASSERT_TRUE(mod != NULL);

    stock_end = strstr(stock + 1, "static void ");
    mod_end = strstr(mod + 1, "static void ");
    ASSERT_TRUE(stock_end != NULL);
    ASSERT_TRUE(mod_end != NULL);

    ASSERT_TRUE(range_contains(stock, stock_end, "save_preset()"));
    ASSERT_TRUE(range_contains(stock, stock_end, "persist_active_preset_state(\"preset-stock-depth\""));
    ASSERT_TRUE(range_contains(stock, stock_end, "eq_control_set_headroom_mode(&next, EQ_HEADROOM_SAFE)"));
    ASSERT_TRUE(range_contains(mod, mod_end, "save_preset()"));
    ASSERT_TRUE(range_contains(mod, mod_end, "persist_active_preset_state(\"preset-mod-switch\""));

    free(source);
}

static void test_reset_defaults_restores_safe_headroom(void)
{
    char path[512];
    char *source;
    char *reset;
    char *reset_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    reset = strstr(source, "static void reset_defaults(void)");
    ASSERT_TRUE(reset != NULL);
    reset_end = strstr(reset + 1, "static void ");
    ASSERT_TRUE(reset_end != NULL);

    ASSERT_TRUE(range_contains(reset, reset_end, "eq_control_set_headroom_mode(&next, EQ_HEADROOM_SAFE)"));

    free(source);
}

static void test_eq_screens_prompt_before_leaving_unsaved_preset(void)
{
    char path[512];
    char *source;
    char *ui_source;
    char *loop_cancel;
    char *loop_cancel_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);
    snprintf(path, sizeof(path), "%s/app/ui_vita.c", EQVITA_SOURCE_DIR);
    ui_source = read_file(path);

    ASSERT_TRUE(strstr(source, "exit_prompt_active") != NULL);
    ASSERT_TRUE(strstr(source, "Save changes to preset slot") != NULL);
    ASSERT_TRUE(strstr(source, "Save to this slot") != NULL);
    ASSERT_TRUE(strstr(source, "Discard and leave") != NULL);
    ASSERT_TRUE(strstr(source, "Keep editing") != NULL);
    ASSERT_TRUE(strstr(source, "exit_prompt_icons") != NULL);
    ASSERT_TRUE(strstr(source, "\"save\"") != NULL);
    ASSERT_TRUE(strstr(source, "\"reset\"") != NULL);
    ASSERT_TRUE(strstr(source, "\"tune\"") != NULL);
    ASSERT_TRUE(strstr(source, "static void request_leave_current_screen(void)") != NULL);
    ASSERT_TRUE(strstr(source, "static void discard_eq_edits_and_leave(void)") != NULL);
    ASSERT_TRUE(strstr(source, "eqvita_app_state_current_preset_dirty(&g_app_state)") != NULL);
    ASSERT_TRUE(strstr(source, "eq_ui_draw_confirm_dialog(") != NULL);
    ASSERT_TRUE(strstr(source, "load_preset()") != NULL);
    ASSERT_TRUE(strstr(source, "Unsaved edits discarded") != NULL);
    ASSERT_TRUE(strstr(ui_source, "draw_icon_symbol((float)x + 50") != NULL);
    ASSERT_TRUE(strstr(ui_source, "vita2d_draw_fill_circle(check_x, check_y") != NULL);

    loop_cancel = strstr(source, "newly & g_cancel_button");
    ASSERT_TRUE(loop_cancel != NULL);
    loop_cancel_end = strstr(loop_cancel + 1, "} else if (newly & SCE_CTRL_TRIANGLE)");
    ASSERT_TRUE(loop_cancel_end != NULL);

    ASSERT_TRUE(range_contains(loop_cancel, loop_cancel_end, "request_leave_current_screen()"));
    ASSERT_TRUE(!range_contains(loop_cancel, loop_cancel_end, "change_screen(SCREEN_HOME)"));

    free(source);
    free(ui_source);
}

static void test_music_preview_screen_is_wired_into_app_ui(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "SCREEN_MUSIC") != NULL);
    ASSERT_TRUE(strstr(source, "SCREEN_MUSIC_BROWSER") != NULL);
    ASSERT_TRUE(strstr(source, "Music Preview") != NULL);
    ASSERT_TRUE(strstr(source, "Play a song while tuning EQ") != NULL);
    ASSERT_TRUE(strstr(source, "Choose file") != NULL);
    ASSERT_TRUE(strstr(source, "Play / Pause") != NULL);
    ASSERT_TRUE(strstr(source, "Stop") != NULL);
    ASSERT_TRUE(strstr(source, "Loop") != NULL);
    ASSERT_TRUE(strstr(source, "eqvita_media_player_shutdown(&g_media_player)") != NULL);

    free(source);
}

static void test_music_preview_uses_custom_player_surfaces(void)
{
    char path[512];
    char *main_source;
    char *ui_header;
    char *ui_source;
    char *render_fn;
    char *render_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    main_source = read_file(path);
    snprintf(path, sizeof(path), "%s/app/ui_vita.h", EQVITA_SOURCE_DIR);
    ui_header = read_file(path);
    snprintf(path, sizeof(path), "%s/app/ui_vita.c", EQVITA_SOURCE_DIR);
    ui_source = read_file(path);

    ASSERT_TRUE(strstr(ui_header, "eq_ui_music_player_model_t") != NULL);
    ASSERT_TRUE(strstr(ui_header, "eq_ui_music_browser_model_t") != NULL);
    ASSERT_TRUE(strstr(ui_header, "eq_ui_draw_music_player_deck") != NULL);
    ASSERT_TRUE(strstr(ui_header, "eq_ui_draw_music_browser_deck") != NULL);
    ASSERT_TRUE(strstr(ui_source, "void eq_ui_draw_music_player_deck") != NULL);
    ASSERT_TRUE(strstr(ui_source, "void eq_ui_draw_music_browser_deck") != NULL);
    ASSERT_TRUE(strstr(main_source, "static void draw_music_player_screen(void)") != NULL);
    ASSERT_TRUE(strstr(main_source, "static void draw_music_browser_screen(void)") != NULL);

    render_fn = strstr(main_source, "static void render_frame(void)");
    ASSERT_TRUE(render_fn != NULL);
    render_end = strstr(render_fn + 1, "static void ");
    ASSERT_TRUE(render_end != NULL);
    ASSERT_TRUE(range_contains(render_fn, render_end, "g_screen == SCREEN_MUSIC"));
    ASSERT_TRUE(range_contains(render_fn, render_end, "draw_music_player_screen()"));
    ASSERT_TRUE(range_contains(render_fn, render_end, "g_screen == SCREEN_MUSIC_BROWSER"));
    ASSERT_TRUE(range_contains(render_fn, render_end, "draw_music_browser_screen()"));
    ASSERT_TRUE(range_contains(render_fn, render_end, "draw_current_rows()"));

    free(main_source);
    free(ui_header);
    free(ui_source);
}

static void test_music_preview_keeps_actions_not_metadata_rows(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "#define MUSIC_ROW_COUNT 5") != NULL);
    ASSERT_TRUE(strstr(source, "#define MUSIC_ROW_FILE") == NULL);
    ASSERT_TRUE(strstr(source, "#define MUSIC_ROW_FORMAT") == NULL);
    ASSERT_TRUE(strstr(source, "#define MUSIC_ROW_RATE") == NULL);
    ASSERT_TRUE(strstr(source, "MUSIC_ROW_FILE") == NULL);
    ASSERT_TRUE(strstr(source, "MUSIC_ROW_FORMAT") == NULL);
    ASSERT_TRUE(strstr(source, "MUSIC_ROW_RATE") == NULL);

    free(source);
}

static void test_music_browser_copies_selected_path_before_opening(void)
{
    char path[512];
    char *source;
    char *browser_case;
    char *browser_case_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "browser_async_start(NULL, 1, EQVITA_MEDIA_FILTER_AUDIO, NULL)") != NULL);
    ASSERT_TRUE(strstr(source, "browser_async_start(EQVITA_PEQ_DIR, 0,") != NULL);
    ASSERT_TRUE(strstr(source, "PRESETS_ROW_OUTPUT_PEQ") != NULL);
    ASSERT_TRUE(strstr(source, "eqvita_apo_import_file(path, &base, &imported, &result)") != NULL);
    ASSERT_TRUE(strstr(source, "change_screen(SCREEN_ADVANCED)") != NULL);

    browser_case = strstr(source, "static void activate_current(void)");
    ASSERT_TRUE(browser_case != NULL);
    browser_case = strstr(browser_case, "case SCREEN_MUSIC_BROWSER:");
    ASSERT_TRUE(browser_case != NULL);
    browser_case_end = strstr(browser_case + 1, "case SCREEN_THEMES:");
    ASSERT_TRUE(browser_case_end != NULL);

    ASSERT_TRUE(range_contains(browser_case, browser_case_end, "char next_path[EQVITA_MEDIA_MAX_PATH]"));
    ASSERT_TRUE(range_contains(browser_case, browser_case_end, "snprintf(next_path, sizeof(next_path), \"%s\", entry->path)"));
    ASSERT_TRUE(range_contains(browser_case, browser_case_end, "media_player_play_selected(next_path)"));
    ASSERT_TRUE(range_contains(browser_case, browser_case_end, "open_music_browser_path(next_path)"));
    ASSERT_TRUE(range_contains(browser_case, browser_case_end, "import_equalizer_apo_file(next_path)"));
    ASSERT_TRUE(range_contains(browser_case, browser_case_end, "open_apo_browser_path(next_path)"));

    free(source);
}

static void test_peq_library_is_fixed_and_exclusive(void)
{
    char path[512];
    char *main_source;
    char *persistence_header;
    char *shared_header;
    char *apo_source;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    main_source = read_file(path);
    snprintf(path, sizeof(path), "%s/app/persistence.h", EQVITA_SOURCE_DIR);
    persistence_header = read_file(path);
    snprintf(path, sizeof(path), "%s/common/eq_shared.h", EQVITA_SOURCE_DIR);
    shared_header = read_file(path);
    snprintf(path, sizeof(path), "%s/app/equalizer_apo.c", EQVITA_SOURCE_DIR);
    apo_source = read_file(path);

    ASSERT_TRUE(strstr(persistence_header,
                       "#define EQVITA_PEQ_DIR EQVITA_DATA_DIR \"/\" EQVITA_PEQ_DIR_NAME") != NULL);
    ASSERT_TRUE(strstr(main_source, "eqvita_ensure_peq_dir(EQVITA_DATA_DIR)") != NULL);
    ASSERT_TRUE(strstr(main_source, "#define BUNDLED_PEQ_NAME \"pch-1000.txt\"") != NULL);
    ASSERT_TRUE(strstr(main_source, "eqvita_seed_peq_file(EQVITA_DATA_DIR") != NULL);
    ASSERT_TRUE(strstr(main_source,
                       "eqvita_media_browser_read_dir_filtered_at_root") != NULL);
    ASSERT_TRUE(strstr(main_source, "EQVITA_PEQ_DIR \"/*.txt\"") != NULL);
    ASSERT_TRUE(strstr(main_source, "Put .txt files in ur0:data/eqvita/peq") != NULL);
    ASSERT_TRUE(strstr(main_source, "Bass guard is off while PEQ is active") != NULL);
    ASSERT_TRUE(strstr(main_source, "APO Exact is locked while PEQ is active") != NULL);
    ASSERT_TRUE(strstr(shared_header,
                       "Imported PEQ is an exclusive signal path") != NULL);
    ASSERT_TRUE(strstr(apo_source, "ctx->control.enabled = 1") != NULL);
    ASSERT_TRUE(strstr(main_source, "SCREEN_OUTPUT_PEQ") != NULL);
    ASSERT_TRUE(strstr(main_source, "Parametric EQ") != NULL);
    ASSERT_TRUE(strstr(main_source, "Configure output") != NULL);
    ASSERT_TRUE(strstr(main_source, "Choose PEQ file") != NULL);
    ASSERT_TRUE(strstr(main_source, "Inspect / edit PEQ") != NULL);
    ASSERT_TRUE(strstr(main_source, "Clear this output") != NULL);
    ASSERT_TRUE(strstr(main_source, "EqSetRouteProfiles(&next_profiles)") != NULL);
    ASSERT_TRUE(strstr(main_source, "eqvita_save_route_profiles") != NULL);
    ASSERT_TRUE(strstr(main_source, "Unassigned outputs are bypassed") != NULL);
    ASSERT_TRUE(strstr(main_source, "g_eq_return_screen = SCREEN_OUTPUT_PEQ") != NULL);
    ASSERT_TRUE(strstr(main_source, "return_screen = g_output_peq_return_screen") != NULL);

    free(main_source);
    free(persistence_header);
    free(shared_header);
    free(apo_source);
}

static void test_home_exposes_parametric_eq_below_advanced_eq(void)
{
    char path[512];
    char *source;
    char *home_rows;
    char *home_rows_end;
    char *activation;
    char *activation_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "#define HOME_ROW_ADVANCED 2") != NULL);
    ASSERT_TRUE(strstr(source, "#define HOME_ROW_PARAMETRIC 3") != NULL);
    ASSERT_TRUE(strstr(source, "#define HOME_ROW_COUNT 10") != NULL);
    ASSERT_TRUE(strstr(source, "case HOME_ROW_PARAMETRIC: return SCREEN_OUTPUT_PEQ") != NULL);

    home_rows = strstr(source, "case SCREEN_HOME: {");
    ASSERT_TRUE(home_rows != NULL);
    home_rows_end = strstr(home_rows + 1, "case SCREEN_STATUS:");
    ASSERT_TRUE(home_rows_end != NULL);
    ASSERT_TRUE(range_contains(home_rows, home_rows_end,
                               "\"Advanced EQ\", \"Parametric EQ\", \"Presets\""));
    ASSERT_TRUE(range_contains(home_rows, home_rows_end,
                               "Import APO .txt profiles for each output"));

    activation = strstr(source, "static void activate_current(void)");
    ASSERT_TRUE(activation != NULL);
    activation_end = strstr(activation + 1, "case SCREEN_PRESETS:");
    ASSERT_TRUE(activation_end != NULL);
    ASSERT_TRUE(range_contains(activation, activation_end,
                               "open_output_peq_screen(SCREEN_HOME)"));
    ASSERT_TRUE(strstr(source, "open_output_peq_screen(SCREEN_PRESETS)") != NULL);

    free(source);
}

static void test_route_detection_uses_release_proven_avconfig_and_weak_plugin_import(void)
{
    char path[512];
    char *main_source;
    char *cmake_source;
    char *route_start;
    char *route_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    main_source = read_file(path);
    snprintf(path, sizeof(path), "%s/app/CMakeLists.txt", EQVITA_SOURCE_DIR);
    cmake_source = read_file(path);

    ASSERT_TRUE(strstr(main_source, "sceAVConfigGetConnectedAudioDevice") != NULL);
    ASSERT_TRUE(strstr(main_source, "sceAVConfigGetVolCtrlEnable") != NULL);
    ASSERT_TRUE(strstr(cmake_source, "SceAVConfig_stub") != NULL);
    ASSERT_TRUE(strstr(cmake_source, "EQSpeaker_stub_weak") != NULL);
    ASSERT_TRUE(strstr(cmake_source, "\n  EQSpeaker_stub\n") == NULL);

    route_start = strstr(main_source, "static eq_route_t detect_route_user(void)");
    ASSERT_TRUE(route_start != NULL);
    route_end = strstr(route_start + 1, "static uint32_t audio_budget_us");
    ASSERT_TRUE(route_end != NULL);
    ASSERT_TRUE(range_contains(route_start, route_end, "SCE_AVCONFIG_AUDIO_DEVICE_BT_AUDIO_OUT"));
    ASSERT_TRUE(range_contains(route_start, route_end, "SCE_AVCONFIG_AUDIO_DEVICE_AUDIO_OUT"));
    ASSERT_TRUE(range_contains(route_start, route_end, "SCE_CTRL_HEADPHONE"));
    ASSERT_TRUE(range_contains(route_start, route_end, "return EQ_ROUTE_UNKNOWN"));

    free(main_source);
    free(cmake_source);
}

static void test_music_browser_cancel_goes_to_parent_before_player(void)
{
    char path[512];
    char *source;
    char *cancel_branch;
    char *cancel_branch_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "static void leave_music_browser(void)") != NULL);

    cancel_branch = strstr(source, "newly & g_cancel_button");
    ASSERT_TRUE(cancel_branch != NULL);
    cancel_branch = strstr(cancel_branch, "if (g_screen == SCREEN_MUSIC_BROWSER)");
    ASSERT_TRUE(cancel_branch != NULL);
    cancel_branch_end = strstr(cancel_branch + 1, "continue;");
    ASSERT_TRUE(cancel_branch_end != NULL);
    cancel_branch_end += strlen("continue;");

    ASSERT_TRUE(range_contains(cancel_branch, cancel_branch_end, "leave_music_browser()"));
    ASSERT_TRUE(!range_contains(cancel_branch, cancel_branch_end, "change_screen(SCREEN_MUSIC)"));
    ASSERT_TRUE(strstr(source, "eqvita_media_browser_is_root_path(g_media_listing.path)") != NULL);
    ASSERT_TRUE(strstr(source, "open_music_browser_roots()") != NULL);

    free(source);
}

static void test_media_browser_uses_heap_staging_for_large_listings(void)
{
    char path[512];
    char *source;
    char *main_source;
    char *worker;
    char *worker_end;

    snprintf(path, sizeof(path), "%s/app/media_browser.c", EQVITA_SOURCE_DIR);
    source = read_file(path);
    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    main_source = read_file(path);

    ASSERT_TRUE(strstr(source, "int eqvita_media_browser_read_roots") != NULL);
    ASSERT_TRUE(strstr(source, "sceIoGetstat") != NULL);
    ASSERT_TRUE(strstr(source, "eqvita_media_listing_t *next") != NULL);
    ASSERT_TRUE(strstr(source, "malloc(sizeof(*next))") != NULL);
    ASSERT_TRUE(strstr(source, "free(next)") != NULL);
    ASSERT_TRUE(!strstr(source, "eqvita_media_listing_t next;"));
    ASSERT_TRUE(strstr(source, "fd = sceIoDopen(open_path)") != NULL);
    ASSERT_TRUE(strstr(source, "*listing = *next") != NULL);
    ASSERT_TRUE(strstr(source, "BROWSER_READ_YIELD_ENTRIES") != NULL);
    ASSERT_TRUE(strstr(source, "sceKernelDelayThread(500)") != NULL);

    worker = strstr(main_source, "static int browser_async_thread");
    ASSERT_TRUE(worker != NULL);
    worker_end = strstr(worker + 1, "static void ");
    ASSERT_TRUE(worker_end != NULL);
    ASSERT_TRUE(!range_contains(worker, worker_end, "eqvita_media_listing_t listing;"));
    ASSERT_TRUE(range_contains(worker, worker_end, "eqvita_media_browser_read_roots(&job->listing)"));
    ASSERT_TRUE(range_contains(worker, worker_end, "eqvita_media_browser_read_dir_filtered(&job->listing"));

    free(source);
    free(main_source);
}

static void test_music_preview_reduces_main_loop_polling_pressure(void)
{
    char path[512];
    char *source;
    char *status_fn;
    char *status_end;
    char *diag_fn;
    char *diag_end;
    char *loop;
    char *loop_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "preview_is_busy") != NULL);
    ASSERT_TRUE(strstr(source, "STATUS_LOG_INTERVAL_US") != NULL);
    ASSERT_TRUE(strstr(source, "STATUS_LOG_PREVIEW_INTERVAL_US") != NULL);
    ASSERT_TRUE(strstr(source, "DIAGNOSTIC_DRAIN_INTERVAL_US") != NULL);
    ASSERT_TRUE(strstr(source, "DIAGNOSTIC_DRAIN_PREVIEW_INTERVAL_US") != NULL);
    ASSERT_TRUE(strstr(source, "preview-buffer: underruns=%u fill=%u/%u decode_max_us=%u output_max_us=%u") != NULL);

    status_fn = strstr(source, "static void maybe_log_status(void)");
    ASSERT_TRUE(status_fn != NULL);
    status_end = strstr(status_fn + 1, "static void ");
    ASSERT_TRUE(status_end != NULL);
    ASSERT_TRUE(range_contains(status_fn, status_end, "sceKernelGetProcessTimeLow()"));
    ASSERT_TRUE(range_contains(status_fn, status_end, "preview_is_busy()"));

    diag_fn = strstr(source, "static void maybe_log_diagnostics(void)");
    ASSERT_TRUE(diag_fn != NULL);
    diag_end = strstr(diag_fn + 1, "static void ");
    ASSERT_TRUE(diag_end != NULL);
    ASSERT_TRUE(range_contains(diag_fn, diag_end, "preview_is_busy()"));
    ASSERT_TRUE(range_contains(diag_fn, diag_end, "events_to_log"));

    loop = strstr(source, "while (1) {");
    ASSERT_TRUE(loop != NULL);
    loop_end = strstr(loop, "eqvita_media_player_shutdown(&g_media_player)");
    ASSERT_TRUE(loop_end != NULL);
    ASSERT_TRUE(range_contains(loop, loop_end, "sceKernelDelayThread(1000)"));

    free(source);
}

static void test_music_preview_mounts_vita_music_library(void)
{
    char path[512];
    char *source;
    char *main_fn;
    char *shutdown_fn;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "static void init_app_util_resources(void)") != NULL);
    ASSERT_TRUE(strstr(source, "sceAppUtilInit") != NULL);
    ASSERT_TRUE(strstr(source, "sceAppUtilMusicMount") != NULL);
    ASSERT_TRUE(strstr(source, "static void ensure_music_mounted") != NULL);
    ASSERT_TRUE(strstr(source, "is_ux0_music_path") != NULL);
    ASSERT_TRUE(strstr(source, "apputil: init=%d music_mount=%d mounted=%d") != NULL);

    main_fn = strstr(source, "int main(void)");
    ASSERT_TRUE(main_fn != NULL);
    ASSERT_TRUE(range_contains(main_fn, strstr(main_fn, "while (1) {"), "init_app_util_resources()"));
    ASSERT_TRUE(range_contains(main_fn, strstr(main_fn, "while (1) {"), "init_button_mapping()"));

    shutdown_fn = strstr(source, "static void shutdown_app_util_resources(void)");
    ASSERT_TRUE(shutdown_fn != NULL);
    ASSERT_TRUE(range_contains(shutdown_fn, strstr(shutdown_fn + 1, "static "), "sceAppUtilMusicUmount()"));
    ASSERT_TRUE(range_contains(shutdown_fn, strstr(shutdown_fn + 1, "static "), "sceAppUtilShutdown()"));
    ASSERT_TRUE(strstr(source, "shutdown_app_util_resources()") != NULL);

    free(source);
}

static void test_music_browser_loads_async_without_auto_pausing_preview(void)
{
    char path[512];
    char *source;
    char *open_roots;
    char *open_roots_end;
    char *open_path;
    char *open_path_end;
    char *loop;
    char *loop_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "g_media_browser_auto_paused") == NULL);
    ASSERT_TRUE(strstr(source, "pause_preview_for_browser") == NULL);
    ASSERT_TRUE(strstr(source, "resume_preview_after_browser") == NULL);
    ASSERT_TRUE(strstr(source, "browser_async_start") != NULL);
    ASSERT_TRUE(strstr(source, "browser_async_poll") != NULL);
    ASSERT_TRUE(strstr(source, "browser_async_cancel") != NULL);

    open_roots = strstr(source, "static void open_music_browser_roots(void)");
    ASSERT_TRUE(open_roots != NULL);
    open_roots = strstr(open_roots + 1, "static void open_music_browser_roots(void)");
    ASSERT_TRUE(open_roots != NULL);
    open_roots_end = strstr(open_roots + 1, "static void ");
    ASSERT_TRUE(open_roots_end != NULL);
    ASSERT_TRUE(range_contains(open_roots, open_roots_end,
                               "browser_async_start(NULL, 1, EQVITA_MEDIA_FILTER_AUDIO, NULL)"));
    ASSERT_TRUE(!range_contains(open_roots, open_roots_end, "eqvita_media_browser_read_roots(&g_media_listing)"));

    open_path = strstr(source, "static void open_music_browser_path(const char *path)");
    ASSERT_TRUE(open_path != NULL);
    open_path = strstr(open_path + 1, "static void open_music_browser_path(const char *path)");
    ASSERT_TRUE(open_path != NULL);
    open_path_end = strstr(open_path + 1, "static void ");
    ASSERT_TRUE(open_path_end != NULL);
    ASSERT_TRUE(range_contains(open_path, open_path_end,
                               "browser_async_start(path, 0, EQVITA_MEDIA_FILTER_AUDIO, NULL)"));
    ASSERT_TRUE(!range_contains(open_path, open_path_end, "eqvita_media_browser_read_dir(&g_media_listing"));

    loop = strstr(source, "while (1) {");
    ASSERT_TRUE(loop != NULL);
    loop_end = strstr(loop, "eqvita_media_player_shutdown(&g_media_player)");
    ASSERT_TRUE(loop_end != NULL);
    ASSERT_TRUE(range_contains(loop, loop_end, "browser_async_poll()"));

    free(source);
}

static void test_left_stick_navigation_uses_button_flow(void)
{
    char path[512];
    char *source;
    char *loop;
    char *loop_end;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "ANALOG_NAV_DEADZONE") != NULL);
    ASSERT_TRUE(strstr(source, "static uint32_t analog_navigation_buttons") != NULL);
    ASSERT_TRUE(strstr(source, "pad->lx") != NULL);
    ASSERT_TRUE(strstr(source, "pad->ly") != NULL);
    ASSERT_TRUE(strstr(source, "sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG)") != NULL);

    loop = strstr(source, "while (1) {");
    ASSERT_TRUE(loop != NULL);
    loop_end = strstr(loop, "eqvita_media_player_shutdown(&g_media_player)");
    ASSERT_TRUE(loop_end != NULL);
    ASSERT_TRUE(range_contains(loop, loop_end, "pad.buttons | analog_navigation_buttons(&pad)"));
    ASSERT_TRUE(range_contains(loop, loop_end, "newly = (~last_buttons) & buttons"));
    ASSERT_TRUE(range_contains(loop, loop_end, "held = buttons"));

    free(source);
}

int main(void)
{
    test_ui_waits_for_rendering_before_resource_teardown();
    test_advanced_eq_exposes_preamp_control();
    test_app_log_reports_audio_timing_budget();
    test_audio_budget_log_flags_current_block_not_stale_max();
    test_app_log_has_run_ids_and_diagnostic_drain();
    test_diagnostic_logs_are_split_to_avoid_truncation();
    test_status_log_includes_slowest_block_context();
    test_status_log_splits_slowest_block_context();
    test_builtin_presets_persist_active_slot_after_apply();
    test_reset_defaults_restores_safe_headroom();
    test_eq_screens_prompt_before_leaving_unsaved_preset();
    test_music_preview_screen_is_wired_into_app_ui();
    test_music_preview_uses_custom_player_surfaces();
    test_music_preview_keeps_actions_not_metadata_rows();
    test_music_browser_copies_selected_path_before_opening();
    test_peq_library_is_fixed_and_exclusive();
    test_home_exposes_parametric_eq_below_advanced_eq();
    test_route_detection_uses_release_proven_avconfig_and_weak_plugin_import();
    test_music_browser_cancel_goes_to_parent_before_player();
    test_media_browser_uses_heap_staging_for_large_listings();
    test_music_preview_reduces_main_loop_polling_pressure();
    test_music_preview_mounts_vita_music_library();
    test_music_browser_loads_async_without_auto_pausing_preview();
    test_left_stick_navigation_uses_button_flow();
    return 0;
}
