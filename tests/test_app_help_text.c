#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef EQVITA_SOURCE_DIR
#define EQVITA_SOURCE_DIR "."
#endif

static char *read_text_file(const char *path)
{
    FILE *f;
    long size;
    char *data;

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "failed to open %s\n", path);
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return NULL;
    }
    size = ftell(f);
    if (size < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);

    data = (char *)malloc((size_t)size + 1u);
    if (!data) {
        fclose(f);
        return NULL;
    }

    if (fread(data, 1u, (size_t)size, f) != (size_t)size) {
        free(data);
        fclose(f);
        return NULL;
    }
    data[size] = '\0';
    fclose(f);
    return data;
}

static int expect_contains(const char *text, const char *needle)
{
    if (!strstr(text, needle)) {
        fprintf(stderr, "missing expected Help/About text: %s\n", needle);
        return 0;
    }
    return 1;
}

static int expect_order(const char *text, const char *first, const char *second)
{
    const char *first_pos = strstr(text, first);
    const char *second_pos = strstr(text, second);

    if (!first_pos || !second_pos || first_pos >= second_pos) {
        fprintf(stderr, "expected `%s` before `%s`\n", first, second);
        return 0;
    }
    return 1;
}

int main(void)
{
    char path[512];
    char *app_main;
    int ok = 1;

    snprintf(path, sizeof(path), "%s/app/main.c", EQVITA_SOURCE_DIR);
    app_main = read_text_file(path);
    if (!app_main) {
        return 1;
    }

    ok &= expect_contains(app_main, "#define APP_LOG_PATH EQVITA_DATA_DIR \"/\" EQVITA_APP_LOG_NAME");
    ok &= expect_contains(app_main, "ABOUT_ROW_LOG_FILE");
    ok &= expect_contains(app_main, "\"Log file\"");
    ok &= expect_contains(app_main, "*desc = APP_LOG_PATH");
    ok &= expect_contains(app_main, "\"Share for issues\"");
    ok &= expect_order(app_main, "#define ABOUT_ROW_REPOSITORY 1", "#define ABOUT_ROW_LOG_FILE 2");
    ok &= expect_order(app_main, "#define ABOUT_ROW_LOG_FILE 2", "#define ABOUT_ROW_WHAT 3");
    ok &= expect_order(app_main, "\"GitHub: shev0k/EQVita\"", "\"Log file\"");
    ok &= expect_order(app_main, "\"Log file\"", "\"What it does\"");
    ok &= expect_contains(app_main, "\"Save or import APO profiles\"");
    ok &= expect_contains(app_main, "\"10-band or imported parametric EQ\"");

    free(app_main);
    return ok ? 0 : 1;
}
