#include "equalizer_apo.h"

#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EQVITA_HOST_TESTS
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#endif

#define APO_MAX_LINE 1024
#define APO_MAX_INCLUDE_DEPTH 8

typedef struct apo_parse_context
{
    eq_control_t control;
    eqvita_apo_import_result_t *result;
    const eqvita_apo_import_policy_t *policy;
    uint32_t file_count;
    uint32_t total_bytes;
    uint8_t channel_mask;
    int saw_setting;
    char include_stack[APO_MAX_INCLUDE_DEPTH][EQVITA_APO_MAX_ERROR_PATH];
} apo_parse_context_t;

static int parse_document(apo_parse_context_t *ctx, const char *text, const char *path, int depth);
static int parse_file_recursive(apo_parse_context_t *ctx, const char *path, int depth);

static int ascii_tolower_local(int c)
{
    return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

static int ascii_equals(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        if (ascii_tolower_local((unsigned char)*a) != ascii_tolower_local((unsigned char)*b)) {
            return 0;
        }
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static char *skip_space(char *p)
{
    while (p && *p && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static const char *skip_space_const(const char *p)
{
    while (p && *p && isspace((unsigned char)*p)) {
        ++p;
    }
    return p;
}

static char *trim(char *text)
{
    char *start;
    char *end;

    if (!text) {
        return text;
    }
    start = skip_space(text);
    end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    return start;
}

static int set_error(apo_parse_context_t *ctx, const char *path, int line, const char *fmt, ...)
{
    va_list ap;

    if (!ctx || !ctx->result) {
        return -1;
    }
    ctx->result->error_line = line;
    snprintf(ctx->result->error_path, sizeof(ctx->result->error_path), "%s", path ? path : "");
    va_start(ap, fmt);
    vsnprintf(ctx->result->message, sizeof(ctx->result->message), fmt, ap);
    va_end(ap);
    return -1;
}

static int word_at(const char *p, const char *word)
{
    const char *start = p;
    if (!p || !word) {
        return 0;
    }
    while (*word) {
        if (ascii_tolower_local((unsigned char)*p) != ascii_tolower_local((unsigned char)*word)) {
            return 0;
        }
        ++p;
        ++word;
    }
    return p > start && !isalnum((unsigned char)*p) && *p != '_';
}

static int consume_word(char **cursor, const char *word)
{
    char *p = skip_space(*cursor);
    size_t len = strlen(word);
    if (!word_at(p, word)) {
        return 0;
    }
    *cursor = p + len;
    return 1;
}

static int consume_unit(char **cursor, const char *unit)
{
    return consume_word(cursor, unit);
}

static int read_number(char **cursor, double *out)
{
    char *p = skip_space(*cursor);
    char number[64];
    char *end = NULL;
    size_t len = 0;
    double value;

    if (!p || !*p || !out) {
        return 0;
    }
    while (p[len] && (isdigit((unsigned char)p[len]) || p[len] == '+' || p[len] == '-' ||
                      p[len] == '.' || p[len] == ',' || p[len] == 'e' || p[len] == 'E')) {
        if (len + 1 >= sizeof(number)) {
            return 0;
        }
        number[len] = p[len] == ',' ? '.' : p[len];
        ++len;
    }
    if (len == 0) {
        return 0;
    }
    number[len] = '\0';
    value = strtod(number, &end);
    if (end != number + len || !isfinite(value)) {
        return 0;
    }
    *cursor = p + len;
    *out = value;
    return 1;
}

static int32_t scaled_i32(double value, double scale)
{
    double scaled = value * scale;
    return (int32_t)(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
}

static uint32_t scaled_u32(double value, double scale)
{
    return (uint32_t)(value * scale + 0.5);
}

static int command_is_filter(const char *command)
{
    const char *p = command;

    if (!p || !word_at(p, "Filter")) {
        return 0;
    }
    p += 6;
    p = skip_space_const(p);
    if (!*p) {
        return 1;
    }
    while (*p) {
        if (!isdigit((unsigned char)*p) && !isspace((unsigned char)*p)) {
            return 0;
        }
        ++p;
    }
    return 1;
}

static int append_operation(apo_parse_context_t *ctx,
                            const eq_parametric_filter_t *operation,
                            const char *path,
                            int line)
{
    uint8_t count = ctx->control.parametric_filter_count;
    if (count >= EQ_PARAMETRIC_FILTERS) {
        return set_error(ctx, path, line, "More than %d active Filter/Copy operations", EQ_PARAMETRIC_FILTERS);
    }
    ctx->control.parametric_filters[count] = *operation;
    ctx->control.parametric_filter_count = (uint8_t)(count + 1);
    ctx->result->operation_count++;
    ctx->saw_setting = 1;
    return 0;
}

static int parse_preamp(apo_parse_context_t *ctx, char *parameters, const char *path, int line)
{
    char *p = parameters;
    double db;
    int32_t mdB;
    int64_t total;

    if (ctx->channel_mask != EQ_CHANNEL_STEREO_MASK) {
        return set_error(ctx, path, line, "Channel-scoped Preamp is not supported");
    }
    if (!read_number(&p, &db) || !consume_unit(&p, "dB")) {
        return set_error(ctx, path, line, "Malformed Preamp; expected '<value> dB'");
    }
    p = skip_space(p);
    if (*p && *p != '(') {
        return set_error(ctx, path, line, "Unexpected text after Preamp");
    }
    mdB = scaled_i32(db, 1000.0);
    total = (int64_t)ctx->control.preamp_mdB + mdB;
    if (total < EQ_PREAMP_MIN_MDB || total > EQ_PREAMP_MAX_MDB) {
        return set_error(ctx, path, line, "Combined Preamp is outside %.1f to %.1f dB",
                         EQ_PREAMP_MIN_MDB / 1000.0, EQ_PREAMP_MAX_MDB / 1000.0);
    }
    ctx->control.preamp_mdB = (int32_t)total;
    ctx->result->preamp_count++;
    ctx->saw_setting = 1;
    return 0;
}

static int parse_filter(apo_parse_context_t *ctx, char *parameters, const char *path, int line)
{
    eq_parametric_filter_t filter;
    char type_name[16];
    char *p = parameters;
    char *type_start;
    size_t type_len;
    double freq;
    double gain;
    double q_or_s = 0.0;
    double slope_db = 0.0;
    int has_q = 0;
    int has_slope = 0;
    int is_shelf = 0;
    int center_form = 0;

    if (consume_word(&p, "OFF")) {
        ctx->result->ignored_count++;
        return 0;
    }
    if (!consume_word(&p, "ON")) {
        return set_error(ctx, path, line, "Filter must start with ON or OFF");
    }

    p = skip_space(p);
    type_start = p;
    while (*p && isalpha((unsigned char)*p)) {
        ++p;
    }
    type_len = (size_t)(p - type_start);
    if (type_len == 0 || type_len >= sizeof(type_name)) {
        return set_error(ctx, path, line, "Missing or invalid filter type");
    }
    memcpy(type_name, type_start, type_len);
    type_name[type_len] = '\0';

    memset(&filter, 0, sizeof(filter));
    filter.channel_mask = ctx->channel_mask;
    if (ascii_equals(type_name, "PK") || ascii_equals(type_name, "PEQ")) {
        filter.type = EQ_FILTER_PEAK;
    } else if (ascii_equals(type_name, "LS") || ascii_equals(type_name, "LSC")) {
        filter.type = EQ_FILTER_LOW_SHELF;
        is_shelf = 1;
        center_form = ascii_equals(type_name, "LSC");
    } else if (ascii_equals(type_name, "HS") || ascii_equals(type_name, "HSC")) {
        filter.type = EQ_FILTER_HIGH_SHELF;
        is_shelf = 1;
        center_form = ascii_equals(type_name, "HSC");
    } else {
        return set_error(ctx, path, line, "Unsupported active filter type '%s'", type_name);
    }

    if (!word_at(skip_space(p), "Fc")) {
        if (!is_shelf || !read_number(&p, &slope_db) || !consume_unit(&p, "dB")) {
            return set_error(ctx, path, line, "Expected Fc after filter type");
        }
        has_slope = 1;
    }
    if (!consume_word(&p, "Fc") || !read_number(&p, &freq) || !consume_unit(&p, "Hz")) {
        return set_error(ctx, path, line, "Malformed filter frequency");
    }
    if (!consume_word(&p, "Gain") || !read_number(&p, &gain) || !consume_unit(&p, "dB")) {
        return set_error(ctx, path, line, "Malformed filter gain");
    }
    if (consume_word(&p, "Q")) {
        if (!read_number(&p, &q_or_s)) {
            return set_error(ctx, path, line, "Malformed filter Q");
        }
        has_q = 1;
    }
    p = skip_space(p);
    if (*p && *p != '(') {
        return set_error(ctx, path, line, "Unexpected text after filter parameters");
    }

    if (freq < EQ_PARAMETRIC_MIN_FREQUENCY_MHZ / 1000.0 ||
        freq > EQ_PARAMETRIC_MAX_FREQUENCY_MHZ / 1000.0) {
        return set_error(ctx, path, line, "Filter frequency %.3f Hz is outside 1 to 24000 Hz", freq);
    }
    if (gain < -EQ_PARAMETRIC_MAX_ABS_GAIN_MDB / 1000.0 ||
        gain > EQ_PARAMETRIC_MAX_ABS_GAIN_MDB / 1000.0) {
        return set_error(ctx, path, line, "Filter gain %.3f dB is outside +/-24 dB", gain);
    }
    if (filter.type == EQ_FILTER_PEAK && !has_q) {
        return set_error(ctx, path, line, "Peak filter requires Q");
    }

    if (has_q) {
        if (q_or_s < EQ_PARAMETRIC_MIN_Q_UQ / 1000000.0 ||
            q_or_s > EQ_PARAMETRIC_MAX_Q_UQ / 1000000.0) {
            return set_error(ctx, path, line, "Filter Q %.5f is outside 0.01 to 100", q_or_s);
        }
        filter.shape = EQ_FILTER_SHAPE_Q;
        filter.frequency_mode = is_shelf && !center_form ? EQ_FILTER_FREQUENCY_CORNER :
                                                           EQ_FILTER_FREQUENCY_CENTER;
    } else if (has_slope) {
        q_or_s = slope_db / 12.0;
        if (q_or_s < EQ_PARAMETRIC_MIN_Q_UQ / 1000000.0 || q_or_s > 1.0) {
            return set_error(ctx, path, line,
                             "Shelf slope must be 0.12 to 12 dB/octave");
        }
        filter.shape = EQ_FILTER_SHAPE_S;
        filter.frequency_mode = center_form ? EQ_FILTER_FREQUENCY_CENTER : EQ_FILTER_FREQUENCY_CORNER;
    } else {
        q_or_s = EQ_PARAMETRIC_DEFAULT_SHELF_S_UQ / 1000000.0;
        filter.shape = EQ_FILTER_SHAPE_S;
        filter.frequency_mode = EQ_FILTER_FREQUENCY_CENTER;
    }

    filter.data.filter.frequency_mHz = scaled_u32(freq, 1000.0);
    filter.data.filter.gain_mdB = scaled_i32(gain, 1000.0);
    filter.data.filter.q_uQ = scaled_u32(q_or_s, 1000000.0);
    if (append_operation(ctx, &filter, path, line) < 0) {
        return -1;
    }
    ctx->result->filter_count++;
    return 0;
}

static const char *find_next_assignment(const char *p)
{
    while (p && *p) {
        if (*p == '(') {
            return p;
        }
        if (isspace((unsigned char)*p)) {
            const char *candidate = skip_space_const(p);
            const char *after_channel;
            if (*candidate == 'L' || *candidate == 'l' || *candidate == 'R' || *candidate == 'r') {
                after_channel = skip_space_const(candidate + 1);
                if (*after_channel == '=') {
                    return candidate;
                }
            }
        }
        ++p;
    }
    return p;
}

static int parse_linear_expression(char *expression, double coefficients[2])
{
    char *p = expression;
    int terms = 0;

    coefficients[0] = 0.0;
    coefficients[1] = 0.0;
    while (1) {
        double sign = 1.0;
        double coefficient = 1.0;
        char *number_end = NULL;
        int channel;

        p = skip_space(p);
        if (!*p) {
            break;
        }
        if (*p == '+' || *p == '-') {
            if (*p == '-') sign = -1.0;
            ++p;
            p = skip_space(p);
        }
        if (isdigit((unsigned char)*p) || *p == '.') {
            coefficient = strtod(p, &number_end);
            if (number_end == p || !isfinite(coefficient)) {
                return -1;
            }
            p = skip_space(number_end);
            if (*p == '*') {
                ++p;
                p = skip_space(p);
            }
        }
        if (*p == 'L' || *p == 'l') {
            channel = 0;
        } else if (*p == 'R' || *p == 'r') {
            channel = 1;
        } else {
            return -1;
        }
        ++p;
        coefficients[channel] += sign * coefficient;
        terms++;
        p = skip_space(p);
        if (!*p) {
            break;
        }
        if (*p != '+' && *p != '-') {
            return -1;
        }
    }
    return terms > 0 ? 0 : -1;
}

static int parse_copy(apo_parse_context_t *ctx, char *parameters, const char *path, int line)
{
    eq_parametric_filter_t operation;
    double matrix[4] = {1.0, 0.0, 0.0, 1.0};
    char *p = parameters;
    int assignments = 0;

    while (1) {
        char destination;
        const char *end;
        char expression[256];
        size_t expression_len;
        double row[2];
        int row_index;

        p = skip_space(p);
        if (!*p || *p == '(') {
            break;
        }
        destination = (char)ascii_tolower_local((unsigned char)*p++);
        if (destination != 'l' && destination != 'r') {
            return set_error(ctx, path, line, "Copy supports only L and R destinations");
        }
        p = skip_space(p);
        if (*p++ != '=') {
            return set_error(ctx, path, line, "Malformed Copy assignment");
        }
        end = find_next_assignment(p);
        expression_len = (size_t)(end - p);
        while (expression_len > 0 && isspace((unsigned char)p[expression_len - 1])) {
            --expression_len;
        }
        if (expression_len == 0 || expression_len >= sizeof(expression)) {
            return set_error(ctx, path, line, "Copy expression is empty or too long");
        }
        memcpy(expression, p, expression_len);
        expression[expression_len] = '\0';
        if (parse_linear_expression(expression, row) < 0) {
            return set_error(ctx, path, line, "Unsupported Copy expression '%s'", expression);
        }
        row_index = destination == 'l' ? 0 : 1;
        matrix[row_index * 2] = row[0];
        matrix[row_index * 2 + 1] = row[1];
        assignments++;
        p = (char *)end;
    }

    if (assignments == 0) {
        return set_error(ctx, path, line, "Copy has no assignments");
    }
    memset(&operation, 0, sizeof(operation));
    operation.type = EQ_FILTER_COPY;
    operation.channel_mask = EQ_CHANNEL_STEREO_MASK;
    for (int i = 0; i < 4; ++i) {
        if (!isfinite(matrix[i]) || fabs(matrix[i]) > 4.0) {
            return set_error(ctx, path, line, "Copy coefficient is outside +/-4.0");
        }
        operation.data.copy.matrix[i] = (int16_t)scaled_i32(matrix[i], EQ_COPY_COEFFICIENT_SCALE);
    }
    if (append_operation(ctx, &operation, path, line) < 0) {
        return -1;
    }
    ctx->result->copy_count++;
    return 0;
}

static int parse_channel(apo_parse_context_t *ctx, char *parameters, const char *path, int line)
{
    char *p = parameters;
    uint8_t mask = 0;

    for (char *c = p; *c; ++c) {
        if (*c == ',') *c = ' ';
    }
    while (*(p = skip_space(p))) {
        char token[16];
        char *start = p;
        size_t len;
        while (*p && !isspace((unsigned char)*p)) ++p;
        len = (size_t)(p - start);
        if (len == 0 || len >= sizeof(token)) {
            return set_error(ctx, path, line, "Invalid Channel token");
        }
        memcpy(token, start, len);
        token[len] = '\0';
        if (ascii_equals(token, "L") || ascii_equals(token, "1")) {
            mask |= EQ_CHANNEL_LEFT_MASK;
        } else if (ascii_equals(token, "R") || ascii_equals(token, "2")) {
            mask |= EQ_CHANNEL_RIGHT_MASK;
        } else if (ascii_equals(token, "ALL")) {
            mask |= EQ_CHANNEL_STEREO_MASK;
        } else {
            return set_error(ctx, path, line, "Unsupported Vita output channel '%s'", token);
        }
    }
    if (!mask) {
        return set_error(ctx, path, line, "Channel requires L, R, or L R");
    }
    ctx->channel_mask = mask;
    return 0;
}

static int path_has_mount_or_drive(const char *path)
{
    const char *p;

    if (!path) return 0;
    for (p = path; *p && *p != '/' && *p != '\\'; ++p) {
        if (*p == ':') return p != path;
    }
    return 0;
}

static int canonicalize_path(char *out, size_t out_size, const char *path)
{
    char normalized[EQVITA_APO_MAX_ERROR_PATH];
    size_t component_rollback[EQVITA_APO_MAX_ERROR_PATH / 2];
    size_t input_len;
    size_t cursor = 0;
    size_t output_len = 0;
    size_t component_count = 0;
    size_t colon = (size_t)-1;

    if (!out || out_size == 0 || !path || !*path) {
        return -1;
    }
    input_len = strlen(path);
    if (input_len >= sizeof(normalized) || input_len >= out_size) {
        return -1;
    }
    for (size_t i = 0; i <= input_len; ++i) {
        normalized[i] = path[i] == '\\' ? '/' : path[i];
    }

    if (normalized[0] == '/') {
        out[output_len++] = '/';
        while (normalized[cursor] == '/') ++cursor;
    } else {
        for (size_t i = 0; normalized[i] && normalized[i] != '/'; ++i) {
            if (normalized[i] == ':') {
                colon = i;
                break;
            }
        }
        if (colon != (size_t)-1) {
            if (colon + 1u >= out_size) return -1;
            memcpy(out, normalized, colon + 1u);
            output_len = colon + 1u;
            cursor = colon + 1u;
            if (normalized[cursor] == '/') {
                out[output_len++] = '/';
                while (normalized[cursor] == '/') ++cursor;
            }
        }
    }

    while (normalized[cursor]) {
        size_t start;
        size_t length;
        size_t rollback;

        while (normalized[cursor] == '/') ++cursor;
        if (!normalized[cursor]) break;
        start = cursor;
        while (normalized[cursor] && normalized[cursor] != '/') ++cursor;
        length = cursor - start;
        if (length == 1u && normalized[start] == '.') continue;
        if (length == 2u && normalized[start] == '.' && normalized[start + 1u] == '.') {
            if (component_count == 0u) return -1;
            output_len = component_rollback[--component_count];
            out[output_len] = '\0';
            continue;
        }
        if (component_count >= sizeof(component_rollback) / sizeof(component_rollback[0])) {
            return -1;
        }
        rollback = output_len;
        if (component_count > 0u) {
            if (output_len + 1u >= out_size) return -1;
            out[output_len++] = '/';
        }
        if (output_len + length >= out_size) return -1;
        component_rollback[component_count++] = rollback;
        memcpy(out + output_len, normalized + start, length);
        output_len += length;
        out[output_len] = '\0';
    }

    if (output_len == 0u || (component_count == 0u && output_len > 1u && out[output_len - 1u] == ':')) {
        return -1;
    }
    out[output_len] = '\0';
    return 0;
}

static int path_is_within_root(const char *path, const char *root)
{
    size_t root_len;

    if (!path || !root) return 0;
    root_len = strlen(root);
    if (root_len == 0u) return 0;
    for (size_t i = 0; i < root_len; ++i) {
        if (!path[i] || ascii_tolower_local((unsigned char)path[i]) !=
                        ascii_tolower_local((unsigned char)root[i])) {
            return 0;
        }
    }
    return path[root_len] == '\0' || path[root_len] == '/';
}

static int path_is_allowed(const apo_parse_context_t *ctx, const char *path)
{
    char root[EQVITA_APO_MAX_ERROR_PATH];

    if (!ctx || !ctx->policy || !path) return 0;
    for (size_t i = 0; i < ctx->policy->allowed_root_count; ++i) {
        const char *allowed_root = ctx->policy->allowed_roots[i];
        if (allowed_root && canonicalize_path(root, sizeof(root), allowed_root) == 0 &&
            path_is_within_root(path, root)) {
            return 1;
        }
    }
    return 0;
}

static int normalize_include_path(char *out, size_t out_size, const char *current_path, const char *include_name)
{
    char joined[EQVITA_APO_MAX_ERROR_PATH];
    const char *last_separator;
    size_t directory_len;
    int written;

    if (!out || out_size == 0 || !include_name || !*include_name) {
        return -1;
    }
    if (include_name[0] == '/' || path_has_mount_or_drive(include_name)) {
        return canonicalize_path(out, out_size, include_name);
    }
    if (!current_path || !*current_path) return -1;
    last_separator = strrchr(current_path, '/');
    if (!last_separator) return -1;
    directory_len = (size_t)(last_separator - current_path);
    written = snprintf(joined, sizeof(joined), "%.*s/%s",
                       (int)directory_len, current_path, include_name);
    if (written < 0 || (size_t)written >= sizeof(joined)) return -1;
    return canonicalize_path(out, out_size, joined);
}

static int read_text_file(const char *path, char **out_text, uint32_t *out_size)
{
    unsigned int size;
    char *text;

    if (!path || !out_text || !out_size) {
        return -1;
    }
#ifdef EQVITA_HOST_TESTS
    {
        FILE *file = fopen(path, "rb");
        long file_size;
        size_t read_size;
        if (!file) return -1;
        if (fseek(file, 0, SEEK_END) != 0 || (file_size = ftell(file)) < 0) {
            fclose(file);
            return -1;
        }
        if (file_size > (long)EQVITA_APO_MAX_FILE_BYTES) {
            fclose(file);
            return -2;
        }
        if (fseek(file, 0, SEEK_SET) != 0) {
            fclose(file);
            return -1;
        }
        size = (unsigned int)file_size;
        text = (char *)malloc((size_t)size + 1u);
        if (!text) {
            fclose(file);
            return -1;
        }
        read_size = fread(text, 1, size, file);
        fclose(file);
        if (read_size != size) {
            free(text);
            return -1;
        }
    }
#else
    {
        SceIoStat stat;
        SceUID fd;
        int read_size;
        memset(&stat, 0, sizeof(stat));
        if (sceIoGetstat(path, &stat) < 0 || stat.st_size < 0) {
            return -1;
        }
        if (stat.st_size > EQVITA_APO_MAX_FILE_BYTES) {
            return -2;
        }
        size = (unsigned int)stat.st_size;
        text = (char *)malloc((size_t)size + 1u);
        if (!text) return -1;
        fd = sceIoOpen(path, SCE_O_RDONLY, 0);
        if (fd < 0) {
            free(text);
            return -1;
        }
        read_size = sceIoRead(fd, text, size);
        sceIoClose(fd);
        if (read_size != (int)size) {
            free(text);
            return -1;
        }
    }
#endif
    text[size] = '\0';
    *out_text = text;
    *out_size = size;
    return 0;
}

static int parse_include(apo_parse_context_t *ctx, char *parameters, const char *path, int line, int depth)
{
    char include_path[EQVITA_APO_MAX_ERROR_PATH];
    char *name = trim(parameters);
    size_t len = strlen(name);
    uint8_t outer_channel_mask;
    int parse_result;

    if (len >= 2 && name[0] == '"' && name[len - 1] == '"') {
        name[len - 1] = '\0';
        ++name;
    }
    if (!*name) {
        return set_error(ctx, path, line, "Include path is empty");
    }
    if (!path || !ctx->policy) {
        return set_error(ctx, path, line, "Include requires a file-based import");
    }
    if (normalize_include_path(include_path, sizeof(include_path), path, name) < 0) {
        return set_error(ctx, path, line, "Invalid include path");
    }
    if (!path_is_allowed(ctx, include_path)) {
        return set_error(ctx, path, line, "Include path is outside an allowed root");
    }
    ctx->result->include_count++;
    outer_channel_mask = ctx->channel_mask;
    parse_result = parse_file_recursive(ctx, include_path, depth + 1);
    ctx->channel_mask = outer_channel_mask;
    if (parse_result < 0) {
        if (!ctx->result->message[0]) {
            return set_error(ctx, include_path, 0, "Could not read included file");
        }
        return -1;
    }
    return 0;
}

static int parse_line(apo_parse_context_t *ctx, char *line_text, const char *path, int line, int depth)
{
    char *comment;
    char *colon;
    char *command;
    char *parameters;

    comment = strchr(line_text, '#');
    if (comment) *comment = '\0';
    command = trim(line_text);
    if (!*command) {
        return 0;
    }
    colon = strchr(command, ':');
    if (!colon) {
        ctx->result->ignored_count++;
        return 0;
    }
    *colon = '\0';
    parameters = trim(colon + 1);
    command = trim(command);

    if (ascii_equals(command, "Preamp")) {
        return parse_preamp(ctx, parameters, path, line);
    }
    if (command_is_filter(command)) {
        return parse_filter(ctx, parameters, path, line);
    }
    if (ascii_equals(command, "Copy")) {
        return parse_copy(ctx, parameters, path, line);
    }
    if (ascii_equals(command, "Channel")) {
        return parse_channel(ctx, parameters, path, line);
    }
    if (ascii_equals(command, "Include")) {
        return parse_include(ctx, parameters, path, line, depth);
    }
    if (ascii_equals(command, "Device")) {
        ctx->result->ignored_count++;
        return 0;
    }

    return set_error(ctx, path, line, "Unsupported active command '%s'", command);
}

static int parse_document(apo_parse_context_t *ctx, const char *text, const char *path, int depth)
{
    const char *cursor = text;
    int line_number = 0;

    if (!ctx || !text) {
        return -1;
    }
    while (*cursor) {
        const char *end = cursor;
        char line[APO_MAX_LINE];
        size_t len;

        while (*end && *end != '\n' && *end != '\r') ++end;
        len = (size_t)(end - cursor);
        line_number++;
        if (len >= sizeof(line)) {
            return set_error(ctx, path, line_number, "Line exceeds %d characters", APO_MAX_LINE - 1);
        }
        memcpy(line, cursor, len);
        line[len] = '\0';
        if (line_number == 1 && len >= 3 &&
            (unsigned char)line[0] == 0xef && (unsigned char)line[1] == 0xbb && (unsigned char)line[2] == 0xbf) {
            memmove(line, line + 3, len - 2);
        }
        if (parse_line(ctx, line, path, line_number, depth) < 0) {
            return -1;
        }
        cursor = end;
        if (*cursor == '\r') ++cursor;
        if (*cursor == '\n') ++cursor;
    }
    return 0;
}

static int parse_file_recursive(apo_parse_context_t *ctx, const char *path, int depth)
{
    char *text = NULL;
    uint32_t text_size = 0;
    int read_result;
    int result;

    if (depth >= APO_MAX_INCLUDE_DEPTH) {
        return set_error(ctx, path, 0, "Include nesting exceeds %d files", APO_MAX_INCLUDE_DEPTH);
    }
    if (!path_is_allowed(ctx, path)) {
        return set_error(ctx, path, 0, "Path is outside an allowed root");
    }
    for (int i = 0; i < depth; ++i) {
        if (ascii_equals(ctx->include_stack[i], path)) {
            return set_error(ctx, path, 0, "Include cycle detected");
        }
    }
    if (ctx->file_count >= ctx->policy->max_file_count) {
        return set_error(ctx, path, 0, "Equalizer APO file limit exceeded");
    }
    ctx->file_count++;
    if (snprintf(ctx->include_stack[depth], sizeof(ctx->include_stack[depth]), "%s", path) < 0) {
        return set_error(ctx, path, 0, "Could not read Equalizer APO file");
    }
    read_result = read_text_file(path, &text, &text_size);
    if (read_result == -2) {
        return set_error(ctx, path, 0, "Equalizer APO file exceeds the 256 KiB limit");
    }
    if (read_result < 0) {
        return set_error(ctx, path, 0, "Could not read Equalizer APO file");
    }
    if (text_size > ctx->policy->max_total_bytes ||
        ctx->total_bytes > ctx->policy->max_total_bytes - text_size) {
        free(text);
        return set_error(ctx, path, 0, "Equalizer APO byte limit exceeded");
    }
    ctx->total_bytes += text_size;
    result = parse_document(ctx, text, path, depth);
    free(text);
    ctx->include_stack[depth][0] = '\0';
    return result;
}

static void init_context(apo_parse_context_t *ctx,
                         const eqvita_apo_import_policy_t *policy,
                         const eq_control_t *base,
                         eqvita_apo_import_result_t *result)
{
    memset(ctx, 0, sizeof(*ctx));
    memset(result, 0, sizeof(*result));
    ctx->result = result;
    ctx->policy = policy;
    if (base && eq_control_is_compatible(base)) {
        ctx->control = *base;
    } else {
        eq_control_init_defaults(&ctx->control);
    }
    memset(ctx->control.band_gain_mdB, 0, sizeof(ctx->control.band_gain_mdB));
    eq_control_set_parametric_mode(&ctx->control, 0);
    memset(ctx->control.parametric_filters, 0, sizeof(ctx->control.parametric_filters));
    ctx->control.enabled = 1;
    ctx->control.preamp_mdB = 0;
    eq_control_set_hpf_enabled(&ctx->control, 0);
    eq_control_set_headroom_mode(&ctx->control, EQ_HEADROOM_EXACT);
    ctx->channel_mask = EQ_CHANNEL_STEREO_MASK;
}

static int finish_context(apo_parse_context_t *ctx, eq_control_t *out)
{
    if (!ctx->saw_setting) {
        return set_error(ctx, NULL, 0, "No supported Equalizer APO settings found");
    }
    if (eq_control_validate(&ctx->control) < 0) {
        return set_error(ctx, NULL, 0, "Imported control data failed validation");
    }
    *out = ctx->control;
    snprintf(ctx->result->message, sizeof(ctx->result->message),
             "Imported %d filters and %d Copy operations",
             ctx->result->filter_count, ctx->result->copy_count);
    return 0;
}

int eqvita_apo_parse_text(const char *text,
                          const eq_control_t *base,
                          eq_control_t *out,
                          eqvita_apo_import_result_t *result)
{
    apo_parse_context_t ctx;
    eqvita_apo_import_result_t local_result;

    if (!text || !out) {
        return -1;
    }
    if (!result) result = &local_result;
    init_context(&ctx, NULL, base, result);
    if (parse_document(&ctx, text, NULL, 0) < 0) {
        return -1;
    }
    return finish_context(&ctx, out);
}

int eqvita_apo_import_file(const char *path,
                           const eqvita_apo_import_policy_t *policy,
                           const eq_control_t *base,
                           eq_control_t *out,
                           eqvita_apo_import_result_t *result)
{
    apo_parse_context_t ctx;
    eqvita_apo_import_result_t local_result;
    char canonical_path[EQVITA_APO_MAX_ERROR_PATH];

    if (!path || !policy || !out) {
        return -1;
    }
    if (!result) result = &local_result;
    init_context(&ctx, policy, base, result);
    if (!policy->allowed_roots || policy->allowed_root_count == 0u ||
        policy->max_file_count == 0u || policy->max_total_bytes == 0u) {
        return set_error(&ctx, path, 0, "Invalid Equalizer APO import policy");
    }
    if (canonicalize_path(canonical_path, sizeof(canonical_path), path) < 0) {
        return set_error(&ctx, path, 0, "Invalid Equalizer APO file path");
    }
    if (!path_is_allowed(&ctx, canonical_path)) {
        return set_error(&ctx, canonical_path, 0, "Path is outside an allowed root");
    }
    if (parse_file_recursive(&ctx, canonical_path, 0) < 0) {
        return -1;
    }
    return finish_context(&ctx, out);
}
