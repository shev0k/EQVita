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

static char *range_find_last(char *start, char *end, const char *needle)
{
    size_t needle_len;
    char *last = NULL;

    if (!start || !end || !needle || end < start) {
        return NULL;
    }

    needle_len = strlen(needle);
    if (needle_len == 0) {
        return start;
    }

    for (char *p = start; p + needle_len <= end; ++p) {
        if (strncmp(p, needle, needle_len) == 0) {
            last = p;
        }
    }
    return last;
}

static void test_output_hook_does_not_sleep(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(!range_contains(hook_start, hook_end, "ksceKernelDelayThread"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "unlock_retry"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "if (lock_audio()"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "= lock_audio()"));

    free(source);
}

static void test_output_hook_drains_completed_before_processing_check(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *audio_lock_branch;
    char *find_call;
    char *drain_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    audio_lock_branch = strstr(hook_start, "if (try_lock_audio() >= 0) {");
    ASSERT_TRUE(audio_lock_branch != NULL);
    ASSERT_TRUE(audio_lock_branch < hook_end);

    find_call = strstr(audio_lock_branch, "eq_audio_port_registry_find_owned(&g_ports, owner_id, port)");
    ASSERT_TRUE(find_call != NULL);
    ASSERT_TRUE(find_call < hook_end);

    drain_call = strstr(audio_lock_branch, "eq_audio_port_registry_drain_completed(&g_ports)");
    ASSERT_TRUE(drain_call != NULL);
    ASSERT_TRUE(drain_call < find_call);

    free(source);
}

static void test_output_hook_recovers_unknown_ports_after_original_output(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *main_continue;
    char *recover_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    recover_call = strstr(hook_start, "recover_port_config_after_output(port)");
    ASSERT_TRUE(recover_call != NULL);
    ASSERT_TRUE(recover_call > main_continue);

    free(source);
}

static void test_output_hook_recovers_when_returned_frames_disagree_with_tracked_config(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *main_continue;
    char *mismatch_check;
    char *recover_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    mismatch_check = strstr(main_continue, "output_frame_mismatch");
    ASSERT_TRUE(mismatch_check != NULL);
    ASSERT_TRUE(mismatch_check < hook_end);
    ASSERT_TRUE(range_contains(main_continue, hook_end, "ret > 0 && frames > 0 && (uint32_t)ret != frames"));

    recover_call = strstr(main_continue, "recover_port_config_after_output(port)");
    ASSERT_TRUE(recover_call != NULL);
    ASSERT_TRUE(recover_call > mismatch_check);
    ASSERT_TRUE(range_contains(main_continue, recover_call, "recover_after_output || output_frame_mismatch"));

    free(source);
}

static void test_output_hook_does_not_query_live_len_before_copying_audio(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *copy_original;
    char *main_continue;
    char *mismatch_check;
    char *recover_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    copy_original = strstr(hook_start, "ksceKernelCopyFromUser(processing_port->original, buf, processing_bytes)");
    ASSERT_TRUE(copy_original != NULL);
    ASSERT_TRUE(copy_original < hook_end);

    ASSERT_TRUE(!range_contains(hook_start, copy_original, "live_port_len_differs"));
    ASSERT_TRUE(!range_contains(hook_start, copy_original, "SCE_AUDIO_OUT_CONFIG_TYPE_LEN"));

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    mismatch_check = strstr(main_continue, "ret > 0 && frames > 0 && (uint32_t)ret != frames");
    ASSERT_TRUE(mismatch_check != NULL);
    ASSERT_TRUE(mismatch_check < hook_end);

    recover_call = strstr(main_continue, "recover_port_config_after_output(port)");
    ASSERT_TRUE(recover_call != NULL);
    ASSERT_TRUE(recover_call > mismatch_check);

    free(source);
}

static void test_unknown_port_recovery_reads_config_before_audio_lock(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;
    char *get_len;
    char *lock_call;
    char *recover_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "static int recover_port_config_after_output");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "static int sceAudioOutGetConfig_hook");
    ASSERT_TRUE(fn_end != NULL);

    get_len = strstr(fn_start, "TAI_CONTINUE(int, g_hook_get_config, port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN)");
    ASSERT_TRUE(get_len != NULL);
    ASSERT_TRUE(get_len < fn_end);

    lock_call = strstr(fn_start, "if (try_lock_audio() < 0)");
    ASSERT_TRUE(lock_call != NULL);
    ASSERT_TRUE(lock_call < fn_end);
    ASSERT_TRUE(get_len < lock_call);

    recover_call = strstr(lock_call, "eq_audio_port_registry_recover_config_owned(&g_ports, owner_id, port");
    ASSERT_TRUE(recover_call != NULL);
    ASSERT_TRUE(recover_call < fn_end);

    free(source);
}

static void test_unknown_port_recovery_preserves_recovered_port_type_and_owner(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "static int recover_port_config_after_output");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "static int sceAudioOutGetConfig_hook");
    ASSERT_TRUE(fn_end != NULL);

    ASSERT_TRUE(range_contains(fn_start, fn_end, "uint32_t owner_id = current_audio_owner_id();"));
    ASSERT_TRUE(range_contains(fn_start, fn_end, "eq_audio_port_registry_recover_config_owned(&g_ports, owner_id, port, eq_audio_port_type_for_recovered_id(port)"));

    free(source);
}

static void test_audio_hooks_use_process_owner_for_port_registry(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "static uint32_t current_audio_owner_id(void)") != NULL);
    ASSERT_TRUE(strstr(source, "ksceKernelGetProcessId()") != NULL);
    ASSERT_TRUE(strstr(source, "eq_audio_port_registry_find_owned(&g_ports, owner_id, port)") != NULL);
    ASSERT_TRUE(strstr(source, "eq_audio_port_registry_begin_processing_owned(&g_ports, owner_id, port)") != NULL);
    ASSERT_TRUE(strstr(source, "eq_audio_port_registry_open_owned(&g_ports, owner_id, port") != NULL);
    ASSERT_TRUE(strstr(source, "eq_audio_port_registry_set_config_owned(&g_ports, owner_id, port") != NULL);
    ASSERT_TRUE(strstr(source, "eq_audio_port_registry_release_owned(&g_ports, owner_id, port)") != NULL);

    free(source);
}

static void test_unknown_port_recovery_rejects_negative_ports_before_get_config(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;
    char *negative_guard;
    char *get_len;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "static int recover_port_config_after_output");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "static int sceAudioOutGetConfig_hook");
    ASSERT_TRUE(fn_end != NULL);

    negative_guard = strstr(fn_start, "port < 0");
    ASSERT_TRUE(negative_guard != NULL);
    ASSERT_TRUE(negative_guard < fn_end);

    get_len = strstr(fn_start, "TAI_CONTINUE(int, g_hook_get_config, port, SCE_AUDIO_OUT_CONFIG_TYPE_LEN)");
    ASSERT_TRUE(get_len != NULL);
    ASSERT_TRUE(get_len < fn_end);
    ASSERT_TRUE(negative_guard < get_len);

    free(source);
}

static void test_output_hook_does_not_take_state_mutex_before_original_output(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *main_continue;
    char *first_state_lock;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    first_state_lock = strstr(hook_start, "try_lock_state()");
    ASSERT_TRUE(first_state_lock == NULL || first_state_lock > main_continue);

    free(source);
}

static void test_output_hook_updates_status_after_original_output(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *main_continue;
    char *status_update;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    status_update = strstr(hook_start, "update_status(");
    ASSERT_TRUE(status_update != NULL);
    ASSERT_TRUE(status_update > main_continue);

    free(source);
}

static void test_output_hook_marks_failed_original_output_inactive_before_status(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *main_continue;
    char *failure_guard;
    char *peak_accounting;
    char *status_update;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    failure_guard = strstr(main_continue, "if (ret < 0) {");
    ASSERT_TRUE(failure_guard != NULL);
    ASSERT_TRUE(failure_guard < hook_end);
    ASSERT_TRUE(range_contains(failure_guard, hook_end, "applied = 0"));
    ASSERT_TRUE(range_contains(failure_guard, hook_end, "smoothing = 0"));
    ASSERT_TRUE(range_contains(failure_guard, hook_end, "reason = EQ_BYPASS_AUDIO_BUSY"));
    ASSERT_TRUE(range_contains(failure_guard, hook_end, "eq_audio_tracked_port_reset_dsp_state(processing_port)"));

    peak_accounting = strstr(main_continue, "if (applied) {");
    ASSERT_TRUE(peak_accounting != NULL);
    ASSERT_TRUE(failure_guard < peak_accounting);

    status_update = strstr(main_continue, "update_status(");
    ASSERT_TRUE(status_update != NULL);
    ASSERT_TRUE(failure_guard < status_update);

    free(source);
}

static void test_output_hook_times_pre_output_preparation(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *first_time_call;
    char *copy_control;
    char *main_continue;
    char *elapsed_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    first_time_call = strstr(hook_start, "ksceKernelGetSystemTimeLow()");
    ASSERT_TRUE(first_time_call != NULL);
    ASSERT_TRUE(first_time_call < hook_end);

    copy_control = strstr(hook_start, "copy_control_snapshot(&control)");
    ASSERT_TRUE(copy_control != NULL);
    ASSERT_TRUE(first_time_call < copy_control);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    elapsed_call = range_find_last(hook_start, main_continue, "elapsed_us = ksceKernelGetSystemTimeLow() - start_us");
    ASSERT_TRUE(elapsed_call != NULL);
    ASSERT_TRUE(elapsed_call < main_continue);

    free(source);
}

static void test_route_detection_gates_controller_headphone_probe(void)
{
    char path[512];
    char *source;
    char *helper_start;
    char *detect_start;
    char *detect_end;
    char *probe_call;
    char *probe_guard;
    char *throttle_helper;
    char *countdown_decl;
    char *cached_decl;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    helper_start = strstr(source, "static int should_probe_wired_headphone");
    ASSERT_TRUE(helper_start != NULL);
    ASSERT_TRUE(strstr(helper_start, "EQ_ROUTE_SPEAKER") != NULL);
    ASSERT_TRUE(strstr(helper_start, "EQ_ROUTE_UNKNOWN") != NULL);

    ASSERT_TRUE(strstr(source, "EQ_WIRED_HEADPHONE_PROBE_INTERVAL") != NULL);
    countdown_decl = strstr(source, "static uint32_t g_wired_headphone_probe_countdown");
    ASSERT_TRUE(countdown_decl != NULL);
    cached_decl = strstr(source, "static int g_cached_wired_headphones_connected");
    ASSERT_TRUE(cached_decl != NULL);
    throttle_helper = strstr(source, "static int should_run_wired_headphone_probe");
    ASSERT_TRUE(throttle_helper != NULL);
    ASSERT_TRUE(strstr(throttle_helper, "g_wired_headphone_probe_countdown--") != NULL);
    ASSERT_TRUE(strstr(throttle_helper, "g_wired_headphone_probe_countdown = EQ_WIRED_HEADPHONE_PROBE_INTERVAL") != NULL);

    detect_start = strstr(source, "static eq_route_t detect_route");
    ASSERT_TRUE(detect_start != NULL);
    detect_end = strstr(detect_start + 1, "#include <psp2kern/io/fcntl.h>");
    ASSERT_TRUE(detect_end != NULL);

    probe_call = strstr(detect_start, "ksceCtrlPeekBufferPositive");
    ASSERT_TRUE(probe_call != NULL);
    ASSERT_TRUE(probe_call < detect_end);

    probe_guard = strstr(detect_start, "should_run_wired_headphone_probe(control)");
    ASSERT_TRUE(probe_guard != NULL);
    ASSERT_TRUE(probe_guard < probe_call);
    ASSERT_TRUE(strstr(detect_start, "wired_headphones_connected = g_cached_wired_headphones_connected") != NULL);

    free(source);
}

static void test_output_hook_does_not_update_route_stale_counter_per_block(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(!range_contains(hook_start, hook_end, "g_route_hint_stale_buffers++"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "route_stale_buffers = g_route_hint_stale_buffers"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "if (g_route_hint_stale_buffers < UINT32_MAX)"));

    free(source);
}

static void test_output_hook_uses_cached_control_if_snapshot_is_busy(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *fallback;
    char *cache_store;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(range_contains(hook_start, hook_end, "processing_port->control_cache_valid"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "processing_port->control_cache"));

    fallback = strstr(hook_start, "control = processing_port->control_cache");
    ASSERT_TRUE(fallback != NULL);
    ASSERT_TRUE(fallback < hook_end);
    ASSERT_TRUE(range_contains(fallback, hook_end, "route_last_counter = control.dirty_counter"));

    cache_store = strstr(hook_start, "processing_port->control_cache = control");
    ASSERT_TRUE(cache_store != NULL);
    ASSERT_TRUE(cache_store < hook_end);

    free(source);
}

static void test_output_hook_bypasses_same_buffer_retry_only_when_buffer_may_still_be_processed(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *consume_call;
    char *copy_from_user;
    char *main_continue;
    char *error_guard;
    char *note_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    consume_call = strstr(hook_start, "retry_bypass = eq_audio_tracked_port_consume_retry_bypass(processing_port, buf)");
    ASSERT_TRUE(consume_call != NULL);
    ASSERT_TRUE(consume_call < hook_end);

    copy_from_user = strstr(hook_start, "ksceKernelCopyFromUser(processing_port->original, buf, processing_bytes)");
    ASSERT_TRUE(copy_from_user != NULL);
    ASSERT_TRUE(consume_call < copy_from_user);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    error_guard = strstr(main_continue, "if (((ret < 0 && retry_bypass) || restore_failed) && buf && processing_port)");
    ASSERT_TRUE(error_guard != NULL);
    ASSERT_TRUE(error_guard < hook_end);
    ASSERT_TRUE(!range_contains(error_guard, hook_end, "ret < 0 && (applied || retry_bypass)"));

    note_call = strstr(error_guard, "eq_audio_tracked_port_note_output_error(processing_port, buf)");
    ASSERT_TRUE(note_call != NULL);
    ASSERT_TRUE(note_call < hook_end);

    free(source);
}

static void test_output_retry_guard_uses_processing_generation_directly(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *main_continue;
    char *error_guard;
    char *complete_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    error_guard = strstr(main_continue, "if (((ret < 0 && retry_bypass) || restore_failed) && buf && processing_port)");
    ASSERT_TRUE(error_guard != NULL);
    ASSERT_TRUE(error_guard < hook_end);

    complete_call = strstr(error_guard, "eq_audio_port_registry_mark_processing_complete(processing_port)");
    ASSERT_TRUE(complete_call != NULL);
    ASSERT_TRUE(complete_call < hook_end);

    ASSERT_TRUE(!range_contains(error_guard, complete_call, "ret < 0 && (applied || retry_bypass)"));
    ASSERT_TRUE(range_contains(error_guard, complete_call, "eq_audio_tracked_port_note_output_error(processing_port, buf)"));
    ASSERT_TRUE(!range_contains(error_guard, complete_call, "eq_audio_port_registry_find_owned(&g_ports, owner_id, port)"));

    free(source);
}

static void test_output_hook_captures_busy_port_metadata_for_diagnostics(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *busy_branch;
    char *capture_call;
    char *diag_call;
    char *diag_guard;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "typedef struct eq_diag_port_meta") != NULL);
    ASSERT_TRUE(strstr(source, "static void diag_capture_port_meta") != NULL);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(range_contains(hook_start, hook_end, "eq_diag_port_meta_t diag_port_meta"));

    busy_branch = strstr(hook_start, "if (p && p->processing) {");
    ASSERT_TRUE(busy_branch != NULL);
    ASSERT_TRUE(busy_branch < hook_end);

    capture_call = strstr(busy_branch, "diag_capture_port_meta(&diag_port_meta, p)");
    ASSERT_TRUE(capture_call != NULL);
    ASSERT_TRUE(capture_call < hook_end);
    diag_guard = range_find_last(busy_branch, capture_call, "#if EQVITA_AUDIO_DIAGNOSTICS");
    ASSERT_TRUE(diag_guard != NULL);
    ASSERT_TRUE(range_contains(capture_call, hook_end, "frames = p->config.len"));
    ASSERT_TRUE(range_contains(capture_call, hook_end, "sample_rate = p->config.freq"));
    ASSERT_TRUE(range_contains(capture_call, hook_end, "channels = p->config.channels"));

    diag_call = strstr(hook_start, "diag_emit_output_locked");
    ASSERT_TRUE(diag_call != NULL);
    ASSERT_TRUE(diag_call < hook_end);
    ASSERT_TRUE(range_contains(diag_call, hook_end, "&diag_port_meta"));

    free(source);
}

static void test_output_hook_does_not_restore_caller_buffer_after_original_output(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *copy_original;
    char *copy_processed;
    char *apply_to_call;
    char *main_continue;
    char *complete_call;
    char registry_path[512];
    char *registry_source;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    snprintf(registry_path, sizeof(registry_path), "%s/plugin/port_registry.h", EQVITA_SOURCE_DIR);
    registry_source = read_file(registry_path);
    ASSERT_TRUE(strstr(registry_source, "int16_t original[EQ_AUDIO_SCRATCH_MAX_FRAMES * EQ_DSP_MAX_CHANNELS]") != NULL);
    free(registry_source);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    copy_original = strstr(hook_start, "ksceKernelCopyFromUser(processing_port->original, buf, processing_bytes)");
    ASSERT_TRUE(copy_original != NULL);
    ASSERT_TRUE(copy_original < hook_end);

    ASSERT_TRUE(!range_contains(copy_original, hook_end, "memcpy(processing_port->scratch, processing_port->original, processing_bytes)"));

    apply_to_call = strstr(copy_original, "eq_dsp_apply_to(&processing_port->dsp, processing_port->original, processing_port->scratch");
    ASSERT_TRUE(apply_to_call != NULL);
    ASSERT_TRUE(apply_to_call < hook_end);

    copy_processed = strstr(apply_to_call, "ksceKernelCopyToUser((void *)buf, processing_port->scratch, processing_bytes)");
    ASSERT_TRUE(copy_processed != NULL);
    ASSERT_TRUE(copy_processed < hook_end);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);
    ASSERT_TRUE(range_contains(copy_processed, main_continue, "ksceKernelCopyToUser((void *)buf, processing_port->original, processing_bytes)"));

    complete_call = strstr(main_continue, "eq_audio_port_registry_mark_processing_complete(processing_port)");
    ASSERT_TRUE(complete_call != NULL);
    ASSERT_TRUE(complete_call < hook_end);
    ASSERT_TRUE(!range_contains(main_continue, complete_call, "ksceKernelCopyToUser((void *)buf, processing_port->original, processing_bytes)"));

    free(source);
}

static void test_output_hook_resets_dsp_when_processed_copy_to_user_fails(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *copy_processed;
    char *copy_failed_branch;
    char *main_continue;
    char *reset_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    copy_processed = strstr(hook_start, "ksceKernelCopyToUser((void *)buf, processing_port->scratch, processing_bytes)");
    ASSERT_TRUE(copy_processed != NULL);
    ASSERT_TRUE(copy_processed < hook_end);

    copy_failed_branch = strstr(copy_processed, "} else {");
    ASSERT_TRUE(copy_failed_branch != NULL);
    ASSERT_TRUE(copy_failed_branch < hook_end);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);
    ASSERT_TRUE(copy_failed_branch < main_continue);

    reset_call = strstr(copy_failed_branch, "eq_audio_tracked_port_reset_dsp_state(processing_port)");
    ASSERT_TRUE(reset_call != NULL);
    ASSERT_TRUE(reset_call < main_continue);
    ASSERT_TRUE(range_contains(reset_call, main_continue, "smoothing = 0"));
    ASSERT_TRUE(range_contains(reset_call, main_continue, "reason = EQ_BYPASS_COPY_FAILED"));

    free(source);
}

static void test_hooks_continue_original_calls_during_unload(void)
{
    char path[512];
    char *source;
    char *hook_names[] = {
        "sceAudioOutOutput_hook",
        "sceAudioOutOpenPort_hook",
        "sceAudioOutSetConfig_hook",
        "sceAudioOutGetConfig_hook",
        "sceAudioOutReleasePort_hook"
    };
    char *next_names[] = {
        "static int sceAudioOutOpenPort_hook",
        "static int sceAudioOutSetConfig_hook",
        "static int sceAudioOutReleasePort_hook",
        "void EqGetVersion",
        "void EqGetVersion"
    };
    const char *continues[] = {
        "TAI_CONTINUE(int, g_hook_output",
        "TAI_CONTINUE(int, g_hook_open",
        "TAI_CONTINUE(int, g_hook_set_config",
        "TAI_CONTINUE(int, g_hook_get_config",
        "TAI_CONTINUE(int, g_hook_release"
    };

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    for (size_t i = 0; i < sizeof(hook_names) / sizeof(hook_names[0]); ++i) {
        char *hook_start = strstr(source, hook_names[i]);
        char *hook_end;
        char *unload_branch;

        ASSERT_TRUE(hook_start != NULL);
        hook_end = strstr(hook_start + 1, next_names[i]);
        ASSERT_TRUE(hook_end != NULL);
        unload_branch = strstr(hook_start, "if (!hook_enter())");
        ASSERT_TRUE(unload_branch != NULL);
        ASSERT_TRUE(unload_branch < hook_end);
        ASSERT_TRUE(range_contains(unload_branch, hook_end, continues[i]));
        ASSERT_TRUE(!range_contains(unload_branch, hook_end, "return SCE_AUDIO_OUT_ERROR_BUSY"));
    }

    free(source);
}

static void test_get_status_clears_peaks_only_after_successful_copy(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;
    char *copy_call;
    char *success_guard;
    char *peak_clear;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "int EqGetStatus(eq_status_t *out_status)");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "static int wait_for_hooks_to_idle");
    ASSERT_TRUE(fn_end != NULL);

    copy_call = strstr(fn_start, "copy_res = ksceKernelCopyToUser");
    ASSERT_TRUE(copy_call != NULL);
    ASSERT_TRUE(copy_call < fn_end);

    success_guard = strstr(copy_call, "if (copy_res >= 0)");
    ASSERT_TRUE(success_guard != NULL);
    ASSERT_TRUE(success_guard < fn_end);

    peak_clear = strstr(success_guard, "g_status.peak_l = 0");
    ASSERT_TRUE(peak_clear != NULL);
    ASSERT_TRUE(peak_clear < fn_end);
    ASSERT_TRUE(range_contains(success_guard, fn_end, "g_status.peak_r = 0"));

    free(source);
}

static void test_exported_syscalls_are_tracked_during_cleanup(void)
{
    char path[512];
    char *source;
    char *version_start;
    char *set_start;
    char *status_start;
    char *copy_to_version;
    char *copy_from_control;
    char *lock_status;
    char *version_enter;
    char *set_enter;
    char *status_enter;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    version_start = strstr(source, "void EqGetVersion(eq_version_t *out)");
    ASSERT_TRUE(version_start != NULL);
    set_start = strstr(version_start + 1, "int EqSetControl(const eq_control_t *user_ctrl)");
    ASSERT_TRUE(set_start != NULL);
    status_start = strstr(set_start + 1, "int EqGetStatus(eq_status_t *out_status)");
    ASSERT_TRUE(status_start != NULL);

    copy_to_version = strstr(version_start, "ksceKernelCopyToUser");
    ASSERT_TRUE(copy_to_version != NULL);
    ASSERT_TRUE(copy_to_version < set_start);
    version_enter = strstr(version_start, "hook_enter()");
    ASSERT_TRUE(version_enter != NULL);
    ASSERT_TRUE(version_enter < copy_to_version);
    ASSERT_TRUE(range_contains(version_enter, set_start, "hook_leave()"));

    copy_from_control = strstr(set_start, "ksceKernelCopyFromUser");
    ASSERT_TRUE(copy_from_control != NULL);
    ASSERT_TRUE(copy_from_control < status_start);
    set_enter = strstr(set_start, "hook_enter()");
    ASSERT_TRUE(set_enter != NULL);
    ASSERT_TRUE(set_enter < copy_from_control);
    ASSERT_TRUE(range_contains(set_enter, status_start, "hook_leave()"));

    lock_status = strstr(status_start, "lock_state()");
    ASSERT_TRUE(lock_status != NULL);
    status_enter = strstr(status_start, "hook_enter()");
    ASSERT_TRUE(status_enter != NULL);
    ASSERT_TRUE(status_enter < lock_status);
    ASSERT_TRUE(range_contains(status_enter, strstr(status_start + 1, "static int wait_for_hooks_to_idle"), "hook_leave()"));

    free(source);
}

static void test_status_counters_use_saturating_increment(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *status_start;
    char *status_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    status_start = strstr(source, "static void update_status");
    ASSERT_TRUE(status_start != NULL);
    status_end = strstr(status_start + 1, "static int recover_port_config_after_output");
    ASSERT_TRUE(status_end != NULL);
    ASSERT_TRUE(range_contains(status_start, status_end, "eq_status_increment_u32(&g_status.status_counter)"));
    ASSERT_TRUE(!range_contains(status_start, status_end, "g_status.status_counter++"));

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(range_contains(hook_start, hook_end, "eq_status_increment_u32(&g_status.debug_run_count)"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "eq_status_increment_u32(&g_status.debug_busy_bypass_count)"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "eq_status_increment_u32(&g_status.debug_unknown_port_count)"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "g_status.debug_run_count++"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "g_status.debug_busy_bypass_count++"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "g_status.debug_unknown_port_count++"));

    free(source);
}

static void test_kernel_boot_preset_reads_require_exact_file_size(void)
{
    char path[512];
    char *source;
    char *helper_start;
    char *helper_end;
    char *boot_start;
    char *boot_end;
    char *preset_start;
    char *preset_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    helper_start = strstr(source, "static int kernel_read_exact");
    ASSERT_TRUE(helper_start != NULL);
    helper_end = strstr(helper_start + 1, "static int load_boot_state_kernel");
    ASSERT_TRUE(helper_end != NULL);
    ASSERT_TRUE(range_contains(helper_start, helper_end, "ksceIoLseek(fd, 0, SCE_SEEK_END)"));
    ASSERT_TRUE(range_contains(helper_start, helper_end, "size != expected_size"));
    ASSERT_TRUE(range_contains(helper_start, helper_end, "ksceIoLseek(fd, 0, SCE_SEEK_SET)"));
    ASSERT_TRUE(range_contains(helper_start, helper_end, "ksceIoRead(fd, data, expected_size)"));

    boot_start = strstr(source, "static int load_boot_state_kernel");
    ASSERT_TRUE(boot_start != NULL);
    boot_end = strstr(boot_start + 1, "static void load_preset_kernel");
    ASSERT_TRUE(boot_end != NULL);
    ASSERT_TRUE(range_contains(boot_start, boot_end, "kernel_read_exact(fd, &state, sizeof(state))"));
    ASSERT_TRUE(!range_contains(boot_start, boot_end, "ksceIoRead(fd, &state"));

    preset_start = boot_end;
    preset_end = strstr(preset_start + 1, "static void set_defaults");
    ASSERT_TRUE(preset_end != NULL);
    ASSERT_TRUE(range_contains(preset_start, preset_end, "kernel_read_exact(fd, &preset, sizeof(preset))"));
    ASSERT_TRUE(range_contains(preset_start, preset_end, "kernel_read_exact(fd, &tmp, sizeof(tmp))"));
    ASSERT_TRUE(!range_contains(preset_start, preset_end, "ksceIoRead(fd, &preset"));
    ASSERT_TRUE(!range_contains(preset_start, preset_end, "ksceIoRead(fd, &tmp"));

    free(source);
}

static void test_diagnostics_are_exported_and_drained_by_syscall(void)
{
    char path[512];
    char *source;
    char *exports;

    snprintf(path, sizeof(path), "%s/common/eq_shared.h", EQVITA_SOURCE_DIR);
    source = read_file(path);
    ASSERT_TRUE(strstr(source, "typedef struct eq_diag_event") != NULL);
    ASSERT_TRUE(strstr(source, "typedef struct eq_diag_snapshot") != NULL);
    ASSERT_TRUE(strstr(source, "int EqDrainDiagnostics(eq_diag_snapshot_t *snapshot)") != NULL);
    free(source);

    snprintf(path, sizeof(path), "%s/plugin/exports.yml", EQVITA_SOURCE_DIR);
    exports = read_file(path);
    ASSERT_TRUE(strstr(exports, "- EqDrainDiagnostics") != NULL);
    free(exports);

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);
    ASSERT_TRUE(strstr(source, "static eq_diag_snapshot_t g_diag_pending") != NULL);
    ASSERT_TRUE(strstr(source, "static void diag_emit_locked") != NULL);
    ASSERT_TRUE(strstr(source, "int EqDrainDiagnostics(eq_diag_snapshot_t *out_snapshot)") != NULL);
    ASSERT_TRUE(strstr(source, "ksceKernelCopyToUser(out_snapshot") != NULL);
    free(source);
}

static void test_plugin_cmake_exposes_audio_diagnostics_option(void)
{
    char path[512];
    char *cmake;

    snprintf(path, sizeof(path), "%s/plugin/CMakeLists.txt", EQVITA_SOURCE_DIR);
    cmake = read_file(path);

    ASSERT_TRUE(strstr(cmake, "option(EQVITA_AUDIO_DIAGNOSTICS") != NULL);
    ASSERT_TRUE(strstr(cmake, "target_compile_definitions(eq_speaker PRIVATE EQVITA_AUDIO_DIAGNOSTICS=1)") != NULL);
    ASSERT_TRUE(strstr(cmake, "-mcpu=cortex-a9") != NULL);
    ASSERT_TRUE(strstr(cmake, "-mfpu=neon") != NULL);
    ASSERT_TRUE(strstr(cmake, "-mfloat-abi=hard") != NULL);
    ASSERT_TRUE(strstr(cmake, "$<$<CONFIG:Release>:-O3>") != NULL);
    ASSERT_TRUE(strstr(cmake, "-ffast-math") == NULL);
    ASSERT_TRUE(strstr(cmake, "-Ofast") == NULL);

    free(cmake);
}

static void test_output_only_diagnostic_helpers_compile_only_in_diagnostic_builds(void)
{
    char path[512];
    char *source;
    char *rich_guard;
    char *rich_end;
    char *lifecycle_start;
    char *hook_start;
    char *hook_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    rich_guard = strstr(source, "#if EQVITA_AUDIO_DIAGNOSTICS");
    ASSERT_TRUE(rich_guard != NULL);
    ASSERT_TRUE(strstr(rich_guard, "static uint16_t diag_abs_i16_peak") != NULL);
    rich_end = strstr(rich_guard, "static void diag_emit_lifecycle_now");
    ASSERT_TRUE(rich_end != NULL);
    lifecycle_start = rich_end;

    ASSERT_TRUE(strstr(rich_guard, "static void diag_measure_input_peak") < rich_end);
    ASSERT_TRUE(strstr(rich_guard, "static int diag_should_sample_active_block") < rich_end);
    ASSERT_TRUE(strstr(rich_guard, "static void diag_capture_port_meta") < rich_end);
    ASSERT_TRUE(strstr(rich_guard, "static void diag_emit_output_locked") < rich_end);
    ASSERT_TRUE(strstr(source, "static uint32_t diag_audio_budget_us") < rich_guard);
    ASSERT_TRUE(lifecycle_start == rich_end);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);
    ASSERT_TRUE(range_contains(hook_start, hook_end, "#if EQVITA_AUDIO_DIAGNOSTICS"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "int retargeted = 0;"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "retargeted = update_dsp_if_needed"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "#else"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "(void)update_dsp_if_needed"));

    free(source);
}

static void test_output_hook_emits_compact_diagnostics_after_original_output(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *main_continue;
    char *diag_call;
    char *diag_guard_start;
    char *diag_guard_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    main_continue = range_find_last(hook_start, hook_end, "TAI_CONTINUE(int, g_hook_output, port, buf)");
    ASSERT_TRUE(main_continue != NULL);

    ASSERT_TRUE(strstr(source, "#ifndef EQVITA_AUDIO_DIAGNOSTICS") != NULL);
    ASSERT_TRUE(strstr(source, "#define EQVITA_AUDIO_DIAGNOSTICS 0") != NULL);

    diag_call = strstr(hook_start, "diag_emit_output_locked");
    ASSERT_TRUE(diag_call != NULL);
    ASSERT_TRUE(diag_call > main_continue);
    ASSERT_TRUE(diag_call < hook_end);

    diag_guard_start = range_find_last(main_continue, diag_call, "#if EQVITA_AUDIO_DIAGNOSTICS");
    ASSERT_TRUE(diag_guard_start != NULL);
    diag_guard_end = strstr(diag_call, "#endif");
    ASSERT_TRUE(diag_guard_end != NULL);
    ASSERT_TRUE(diag_guard_end < hook_end);

    ASSERT_TRUE(!range_contains(hook_start, hook_end, "ksceKernelPrintf"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "ksceIoOpen"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "snprintf"));
    ASSERT_TRUE(strstr(source, "input_peak_l") != NULL);
    ASSERT_TRUE(strstr(source, "output_peak_l") != NULL);
    ASSERT_TRUE(strstr(source, "effective_preamp_mdB") != NULL);
    ASSERT_TRUE(strstr(source, "max_boost_mdB") != NULL);
    ASSERT_TRUE(strstr(source, "generation") != NULL);

    free(source);
}

static void test_output_hook_emits_config_mismatch_diagnostic(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *diag_call;
    char *recover_call;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(strstr(source, "EQ_DIAG_EVENT_CONFIG_MISMATCH") != NULL);
    ASSERT_TRUE(range_contains(hook_start, hook_end, "output_frame_mismatch"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "diag_type = EQ_DIAG_EVENT_CONFIG_MISMATCH"));

    diag_call = strstr(hook_start, "diag_emit_output_locked");
    ASSERT_TRUE(diag_call != NULL);
    ASSERT_TRUE(diag_call < hook_end);
    ASSERT_TRUE(range_contains(range_find_last(hook_start, diag_call, "#if EQVITA_AUDIO_DIAGNOSTICS"), diag_call, "#if EQVITA_AUDIO_DIAGNOSTICS"));

    recover_call = strstr(hook_start, "recover_port_config_after_output(port)");
    ASSERT_TRUE(recover_call != NULL);
    ASSERT_TRUE(recover_call < hook_end);
    ASSERT_TRUE(diag_call < recover_call);

    free(source);
}

static void test_diagnostic_buffer_keeps_latest_events_when_app_was_closed(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "static uint32_t g_diag_ring_start") != NULL);
    ASSERT_TRUE(strstr(source, "(g_diag_ring_start + g_diag_pending.count) % EQ_DIAG_MAX_EVENTS_PER_DRAIN") != NULL);
    ASSERT_TRUE(strstr(source, "g_diag_ring_start = (g_diag_ring_start + 1u) % EQ_DIAG_MAX_EVENTS_PER_DRAIN") != NULL);
    ASSERT_TRUE(strstr(source, "tmp.events[i] = g_diag_pending.events[index]") != NULL);

    free(source);
}

static void test_output_hook_samples_active_blocks_without_waiting_for_clips(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(strstr(source, "EQ_DIAG_ACTIVE_SAMPLE_INTERVAL") != NULL);
    ASSERT_TRUE(strstr(source, "diag_should_sample_active_block") != NULL);
    ASSERT_TRUE(range_contains(hook_start, hook_end, "EQ_DIAG_EVENT_ACTIVE_SAMPLE"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "applied && diag_should_sample_active_block(processing_port)"));
    ASSERT_TRUE(range_contains(range_find_last(hook_start, strstr(hook_start, "EQ_DIAG_EVENT_ACTIVE_SAMPLE"), "#if EQVITA_AUDIO_DIAGNOSTICS"),
                               strstr(hook_start, "EQ_DIAG_EVENT_ACTIVE_SAMPLE"), "#if EQVITA_AUDIO_DIAGNOSTICS"));

    free(source);
}

static void test_dsp_smoothing_coefficients_are_not_lerped_inside_channel_loop(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;
    char *smoothing_guard;
    char *channel_loop;
    char *frame_tail;

    snprintf(path, sizeof(path), "%s/plugin/dsp.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "void eq_dsp_apply");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "uint32_t eq_dsp_active_band_count");
    ASSERT_TRUE(fn_end != NULL);

    smoothing_guard = strstr(fn_start, "if (smoothing_now) {");
    ASSERT_TRUE(smoothing_guard != NULL);
    ASSERT_TRUE(smoothing_guard < fn_end);

    channel_loop = strstr(smoothing_guard, "for (uint32_t ch = 0; ch < channels; ++ch)");
    ASSERT_TRUE(channel_loop != NULL);
    ASSERT_TRUE(channel_loop < fn_end);
    frame_tail = range_find_last(channel_loop, fn_end, "if (state->smooth_remaining > 0)");
    ASSERT_TRUE(frame_tail != NULL);
    ASSERT_TRUE(frame_tail < fn_end);

    ASSERT_TRUE(!range_contains(channel_loop, frame_tail, "lerp_biquad("));
    ASSERT_TRUE(range_contains(fn_start, channel_loop, "lerp_biquad("));

    free(source);
}

static void test_dsp_smoothing_scratch_arrays_are_not_declared_inside_frame_loop(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;
    char *frame_loop;
    char *smooth_band_decl;
    char *smooth_enabled_decl;

    snprintf(path, sizeof(path), "%s/plugin/dsp.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "void eq_dsp_apply_to");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "void eq_dsp_apply(");
    ASSERT_TRUE(fn_end != NULL);

    frame_loop = strstr(fn_start, "for (uint32_t i = 0; i < frames; ++i)");
    ASSERT_TRUE(frame_loop != NULL);
    ASSERT_TRUE(frame_loop < fn_end);

    smooth_band_decl = strstr(fn_start, "eq_biquad_t smooth_band[EQ_BANDS]");
    ASSERT_TRUE(smooth_band_decl != NULL);
    ASSERT_TRUE(smooth_band_decl < frame_loop);

    smooth_enabled_decl = strstr(fn_start, "uint8_t smooth_band_enabled[EQ_BANDS]");
    ASSERT_TRUE(smooth_enabled_decl != NULL);
    ASSERT_TRUE(smooth_enabled_decl < frame_loop);

    ASSERT_TRUE(!range_contains(frame_loop, fn_end, "eq_biquad_t smooth_band[EQ_BANDS]"));
    ASSERT_TRUE(!range_contains(frame_loop, fn_end, "uint8_t smooth_band_enabled[EQ_BANDS]"));

    free(source);
}

static void test_dsp_steady_state_band_loop_avoids_smoothing_branch_work(void)
{
    char path[512];
    char *source;
    char *stereo_start;
    char *generic_start;
    char *steady_end;
    char *fast_path;
    char *fast_return;

    snprintf(path, sizeof(path), "%s/plugin/dsp.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    stereo_start = strstr(source, "static void process_stereo_steady");
    ASSERT_TRUE(stereo_start != NULL);
    generic_start = strstr(source, "static void process_generic_steady");
    ASSERT_TRUE(generic_start != NULL);
    steady_end = strstr(generic_start + 1, "void eq_dsp_apply_to");
    ASSERT_TRUE(steady_end != NULL);

    ASSERT_TRUE(range_contains(stereo_start, steady_end, "for (uint8_t i = 0; i < active_count; ++i)"));
    ASSERT_TRUE(range_contains(stereo_start, steady_end, "active_band_index"));
    ASSERT_TRUE(!range_contains(stereo_start, steady_end, "smoothing_now"));
    ASSERT_TRUE(!range_contains(stereo_start, steady_end, "smooth_band"));

    fast_path = strstr(steady_end, "if (state->smooth_remaining == 0)");
    ASSERT_TRUE(fast_path != NULL);
    fast_return = strstr(fast_path, "return;");
    ASSERT_TRUE(fast_return != NULL);
    ASSERT_TRUE(!range_contains(fast_path, fast_return, "smoothing_now"));
    ASSERT_TRUE(!range_contains(fast_path, fast_return, "smooth_band"));

    free(source);
}

static void test_dsp_apply_has_steady_state_fast_path(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;
    char *fast_path;
    char *first_frame_loop;
    char *fast_return;

    snprintf(path, sizeof(path), "%s/plugin/dsp.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "void eq_dsp_apply_to");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "void eq_dsp_apply(");
    ASSERT_TRUE(fn_end != NULL);

    fast_path = strstr(fn_start, "if (state->smooth_remaining == 0)");
    ASSERT_TRUE(fast_path != NULL);
    ASSERT_TRUE(fast_path < fn_end);

    first_frame_loop = strstr(fn_start, "for (uint32_t i = 0; i < frames; ++i)");
    ASSERT_TRUE(first_frame_loop != NULL);
    ASSERT_TRUE(fast_path < first_frame_loop);

    fast_return = strstr(fast_path, "return;");
    ASSERT_TRUE(fast_return != NULL);
    ASSERT_TRUE(fast_return < first_frame_loop);
    ASSERT_TRUE(!range_contains(fast_path, fast_return, "smooth_band"));
    ASSERT_TRUE(!range_contains(fast_path, fast_return, "smoothing_now"));

    free(source);
}

static void test_dsp_state_caches_active_band_indexes(void)
{
    char path[512];
    char *source;

    snprintf(path, sizeof(path), "%s/plugin/dsp.h", EQVITA_SOURCE_DIR);
    source = read_file(path);

    ASSERT_TRUE(strstr(source, "active_band_count") != NULL);
    ASSERT_TRUE(strstr(source, "active_band_index") != NULL);
    ASSERT_TRUE(strstr(source, "target_band_count") != NULL);
    ASSERT_TRUE(strstr(source, "target_band_index") != NULL);

    free(source);
}

static void test_dsp_apply_has_stereo_steady_state_fast_path(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;

    snprintf(path, sizeof(path), "%s/plugin/dsp.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "void eq_dsp_apply_to");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "void eq_dsp_apply(");
    ASSERT_TRUE(fn_end != NULL);

    ASSERT_TRUE(strstr(source, "process_stereo_steady") != NULL);
    ASSERT_TRUE(range_contains(fn_start, fn_end, "channels == 2"));
    ASSERT_TRUE(range_contains(fn_start, fn_end, "process_stereo_steady"));
    ASSERT_TRUE(range_contains(fn_start, fn_end, "active_band_count"));
    ASSERT_TRUE(range_contains(fn_start, fn_end, "active_band_index"));

    free(source);
}

static void test_dsp_hot_sample_helpers_avoid_finite_checks(void)
{
    char path[512];
    char *source;
    char *limit_start;
    char *limit_end;
    char *biquad_start;
    char *biquad_end;

    snprintf(path, sizeof(path), "%s/plugin/dsp.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    limit_start = strstr(source, "static inline int16_t limit_i16");
    ASSERT_TRUE(limit_start != NULL);
    limit_end = strstr(limit_start + 1, "static inline uint16_t abs_i16_peak");
    ASSERT_TRUE(limit_end != NULL);
    ASSERT_TRUE(!range_contains(limit_start, limit_end, "isfinite"));

    biquad_start = strstr(source, "static inline float process_biquad");
    ASSERT_TRUE(biquad_start != NULL);
    biquad_end = strstr(biquad_start + 1, "void eq_dsp_apply_to");
    ASSERT_TRUE(biquad_end != NULL);
    ASSERT_TRUE(!range_contains(biquad_start, biquad_end, "isfinite"));

    free(source);
}

static void test_speaker_retarget_honors_hpf_control_before_folding_31hz(void)
{
    char path[512];
    char *source;
    char *fn_start;
    char *fn_end;
    char *hpf_decl;
    char *fold_guard;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    fn_start = strstr(source, "static int update_dsp_if_needed");
    ASSERT_TRUE(fn_start != NULL);
    fn_end = strstr(fn_start + 1, "static void update_status");
    ASSERT_TRUE(fn_end != NULL);

    hpf_decl = strstr(fn_start, "int hpf_enabled = eq_control_hpf_enabled(control);");
    ASSERT_TRUE(hpf_decl != NULL);
    ASSERT_TRUE(hpf_decl < fn_end);

    fold_guard = strstr(fn_start, "if (route == EQ_ROUTE_SPEAKER && hpf_enabled)");
    ASSERT_TRUE(fold_guard != NULL);
    ASSERT_TRUE(fold_guard < fn_end);

    ASSERT_TRUE(!range_contains(fn_start, fn_end, "(route == EQ_ROUTE_SPEAKER) ? 1"));

    free(source);
}

static void test_output_hook_records_slowest_block_context(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;
    char *max_update;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    max_update = strstr(hook_start, "if (elapsed_us > g_status.debug_max_us)");
    ASSERT_TRUE(max_update != NULL);
    ASSERT_TRUE(max_update < hook_end);

    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_port = (uint32_t)port"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_len = frames"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_sample_rate = sample_rate"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_channels = channels"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_budget_us = budget_us"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_route = route"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_bypass_reason = reason"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_clip_count = clip_count"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_stage_control_us = stage_control_us"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_stage_registry_us = stage_registry_us"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_stage_route_us = stage_route_us"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_stage_copy_in_us = stage_copy_in_us"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_stage_retarget_us = stage_retarget_us"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_stage_dsp_us = stage_dsp_us"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_stage_copy_out_us = stage_copy_out_us"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_stage_original_us = stage_original_us"));
    ASSERT_TRUE(range_contains(max_update, hook_end, "g_status.debug_max_stage_status_us = stage_status_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "g_status.debug_last_total_us = total_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "g_status.debug_last_budget_us = budget_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "g_status.debug_last_margin_us = margin_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "g_status.debug_max_total_us = total_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "g_status.debug_max_dsp_us = stage_dsp_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "g_status.debug_min_margin_us = margin_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "g_status.debug_min_margin_len_256_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "g_status.debug_min_margin_len_1024_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "g_status.debug_min_margin_len_2048_us"));

    free(source);
}

static void test_output_hook_measures_total_time_and_deadline_margin(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(range_contains(hook_start, hook_end, "uint32_t total_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "int32_t margin_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "total_us ="));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "margin_us ="));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "diag_audio_margin_us(budget_us, total_us)"));

    free(source);
}

static void test_output_hook_measures_stage_timing_for_slowest_block(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(range_contains(hook_start, hook_end, "stage_control_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "stage_registry_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "stage_route_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "stage_copy_in_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "stage_retarget_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "stage_dsp_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "stage_copy_out_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "stage_original_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "stage_status_us"));
    ASSERT_TRUE(range_contains(hook_start, hook_end, "ksceKernelGetSystemTimeLow()"));

    free(source);
}

static void test_output_hook_does_not_deadline_bypass_processed_audio(void)
{
    char path[512];
    char *source;
    char *hook_start;
    char *hook_end;

    snprintf(path, sizeof(path), "%s/plugin/main.c", EQVITA_SOURCE_DIR);
    source = read_file(path);

    hook_start = strstr(source, "static int sceAudioOutOutput_hook");
    ASSERT_TRUE(hook_start != NULL);
    hook_end = strstr(hook_start + 1, "static int sceAudioOutOpenPort_hook");
    ASSERT_TRUE(hook_end != NULL);

    ASSERT_TRUE(!range_contains(hook_start, hook_end, "deadline_missed"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "EQ_BYPASS_DEADLINE_MISSED"));
    ASSERT_TRUE(!range_contains(hook_start, hook_end, "reason = EQ_BYPASS_DEADLINE_MISSED"));

    free(source);
}

int main(void)
{
    test_output_hook_does_not_sleep();
    test_output_hook_drains_completed_before_processing_check();
    test_output_hook_recovers_unknown_ports_after_original_output();
    test_output_hook_recovers_when_returned_frames_disagree_with_tracked_config();
    test_output_hook_does_not_query_live_len_before_copying_audio();
    test_unknown_port_recovery_reads_config_before_audio_lock();
    test_unknown_port_recovery_preserves_recovered_port_type_and_owner();
    test_audio_hooks_use_process_owner_for_port_registry();
    test_unknown_port_recovery_rejects_negative_ports_before_get_config();
    test_output_hook_does_not_take_state_mutex_before_original_output();
    test_output_hook_updates_status_after_original_output();
    test_output_hook_marks_failed_original_output_inactive_before_status();
    test_output_hook_times_pre_output_preparation();
    test_route_detection_gates_controller_headphone_probe();
    test_output_hook_does_not_update_route_stale_counter_per_block();
    test_output_hook_uses_cached_control_if_snapshot_is_busy();
    test_output_hook_bypasses_same_buffer_retry_only_when_buffer_may_still_be_processed();
    test_output_retry_guard_uses_processing_generation_directly();
    test_output_hook_captures_busy_port_metadata_for_diagnostics();
    test_output_hook_does_not_restore_caller_buffer_after_original_output();
    test_output_hook_resets_dsp_when_processed_copy_to_user_fails();
    test_hooks_continue_original_calls_during_unload();
    test_get_status_clears_peaks_only_after_successful_copy();
    test_exported_syscalls_are_tracked_during_cleanup();
    test_status_counters_use_saturating_increment();
    test_kernel_boot_preset_reads_require_exact_file_size();
    test_diagnostics_are_exported_and_drained_by_syscall();
    test_plugin_cmake_exposes_audio_diagnostics_option();
    test_output_only_diagnostic_helpers_compile_only_in_diagnostic_builds();
    test_output_hook_emits_compact_diagnostics_after_original_output();
    test_output_hook_emits_config_mismatch_diagnostic();
    test_diagnostic_buffer_keeps_latest_events_when_app_was_closed();
    test_output_hook_samples_active_blocks_without_waiting_for_clips();
    test_dsp_smoothing_coefficients_are_not_lerped_inside_channel_loop();
    test_dsp_smoothing_scratch_arrays_are_not_declared_inside_frame_loop();
    test_dsp_steady_state_band_loop_avoids_smoothing_branch_work();
    test_dsp_apply_has_steady_state_fast_path();
    test_dsp_state_caches_active_band_indexes();
    test_dsp_apply_has_stereo_steady_state_fast_path();
    test_dsp_hot_sample_helpers_avoid_finite_checks();
    test_speaker_retarget_honors_hpf_control_before_folding_31hz();
    test_output_hook_records_slowest_block_context();
    test_output_hook_measures_total_time_and_deadline_margin();
    test_output_hook_measures_stage_timing_for_slowest_block();
    test_output_hook_does_not_deadline_bypass_processed_audio();
    return 0;
}
