#pragma once

#include <stdint.h>

#define EQ_UI_SCREEN_W 960
#define EQ_UI_SCREEN_H 544
#define EQ_UI_LIST_X 72
#define EQ_UI_LIST_Y 126
#define EQ_UI_LIST_W 816
#define EQ_UI_ROW_H 52
#define EQ_UI_ROW_GAP 7
#define EQ_UI_FOOTER_Y 500
#define EQ_UI_MAX_VISIBLE_ROWS 7
#define EQ_UI_DEFAULT_THEME_INDEX 13
#define EQ_UI_DIALOG_X 210
#define EQ_UI_DIALOG_Y 164
#define EQ_UI_DIALOG_W 540
#define EQ_UI_DIALOG_ROW_Y 272
#define EQ_UI_DIALOG_ROW_H 48
#define EQ_UI_DIALOG_ROW_GAP 6

typedef struct eq_ui_row_bounds
{
    int row;
    int x;
    int y;
    int w;
    int h;
} eq_ui_row_bounds_t;

typedef enum eq_ui_row_kind
{
    EQ_UI_ROW_READONLY = 0,
    EQ_UI_ROW_ACTION,
    EQ_UI_ROW_ADJUST,
    EQ_UI_ROW_NAV,
    EQ_UI_ROW_SECTION
} eq_ui_row_kind_t;

typedef struct eq_ui_music_action_row
{
    int row_index;
    const char *icon;
    const char *label;
    const char *description;
    const char *value;
    eq_ui_row_kind_t kind;
} eq_ui_music_action_row_t;

typedef struct eq_ui_music_player_model
{
    const char *file_name;
    const char *file_path;
    const char *state;
    const char *format;
    const char *stream_info;
    const char *eq_state;
    const char *preset;
    const char *output;
    const char *preamp;
    const char *headroom;
    int selected_row;
    const eq_ui_music_action_row_t *actions;
    int action_count;
    eq_ui_row_bounds_t *bounds;
    int max_bounds;
    int *bound_count;
} eq_ui_music_player_model_t;

typedef struct eq_ui_music_browser_entry
{
    int row_index;
    const char *icon;
    const char *label;
    const char *description;
    const char *value;
    eq_ui_row_kind_t kind;
} eq_ui_music_browser_entry_t;

typedef struct eq_ui_music_browser_model
{
    const char *path;
    const char *title;
    const char *instruction;
    const char *empty_message;
    int selected_row;
    const eq_ui_music_browser_entry_t *entries;
    int entry_count;
    eq_ui_row_bounds_t *bounds;
    int max_bounds;
    int *bound_count;
} eq_ui_music_browser_model_t;

int eq_ui_init(void);
void eq_ui_fini(void);
void eq_ui_begin_frame(void);
void eq_ui_end_frame(void);
int eq_ui_theme_count(void);
int eq_ui_theme_index(void);
int eq_ui_set_theme(int index);
const char *eq_ui_theme_name(int index);
const char *eq_ui_theme_description(int index);

void eq_ui_draw_shell(const char *title,
                      const char *subtitle,
                      const char *left_status,
                      const char *right_status);
void eq_ui_draw_footer(const char *left, const char *center, const char *right);
void eq_ui_draw_message(const char *message);
void eq_ui_draw_confirm_dialog(const char *title,
                               const char *body,
                               const char * const *actions,
                               const char * const *icons,
                               int action_count,
                               int selected_action);
void eq_ui_draw_row(int visible_index,
                    int row_index,
                    int selected,
                    const char *icon,
                    const char *label,
                    const char *description,
                    const char *value,
                    eq_ui_row_kind_t kind,
                    eq_ui_row_bounds_t *bounds);
void eq_ui_draw_slider(int visible_index,
                       int row_index,
                       int selected,
                       const char *icon,
                       const char *label,
                       const char *description,
                       const char *value,
                       int32_t amount_mdB,
                       eq_ui_row_kind_t kind,
                       eq_ui_row_bounds_t *bounds);
void eq_ui_draw_status_chip(float x,
                            float y,
                            const char *label,
                            const char *value,
                            unsigned int color);
void eq_ui_draw_music_player_deck(const eq_ui_music_player_model_t *model);
void eq_ui_draw_music_browser_deck(const eq_ui_music_browser_model_t *model);
