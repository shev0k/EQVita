#pragma once

#include <stddef.h>

#include "../common/eq_shared.h"

#define EQVITA_APO_MAX_ERROR_MESSAGE 128
#define EQVITA_APO_MAX_ERROR_PATH 512
#define EQVITA_APO_MAX_FILE_BYTES (256u * 1024u)

typedef struct eqvita_apo_import_policy
{
    const char *const *allowed_roots;
    size_t allowed_root_count;
    uint32_t max_file_count;
    uint32_t max_total_bytes;
} eqvita_apo_import_policy_t;

typedef struct eqvita_apo_import_result
{
    int error_line;
    int operation_count;
    int filter_count;
    int copy_count;
    int preamp_count;
    int include_count;
    int ignored_count;
    char error_path[EQVITA_APO_MAX_ERROR_PATH];
    char message[EQVITA_APO_MAX_ERROR_MESSAGE];
} eqvita_apo_import_result_t;

int eqvita_apo_parse_text(const char *text,
                          const eq_control_t *base,
                          eq_control_t *out,
                          eqvita_apo_import_result_t *result);
int eqvita_apo_import_file(const char *path,
                           const eqvita_apo_import_policy_t *policy,
                           const eq_control_t *base,
                           eq_control_t *out,
                           eqvita_apo_import_result_t *result);
