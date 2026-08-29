#include "ui_vita.h"

#include <psp2/gxm.h>
#include <psp2/pgf.h>
#include <psp2/sysmodule.h>
#include <string.h>
#include <vita2d.h>

#define C(r, g, b, a) RGBA8((r), (g), (b), (a))
#define TEXT_SIZE 1.0f
#define ICON_CELL 64.0f
#define ICON_SCALE 0.6f
#define FOOTER_ICON_SCALE 0.32f
#define CHEVRON_ICON_SCALE 0.28f
#define ICON_VARIANT_NORMAL 0
#define ICON_VARIANT_READONLY 1
#define ICON_VARIANT_SELECTED 2
#define FOOTER_ICON_CIRCLE 12
#define FOOTER_ICON_CROSS 13
#define FOOTER_ICON_TRIANGLE 14
#define AFFORDANCE_ICON_LEFT 15
#define AFFORDANCE_ICON_RIGHT 16
#define PLAYER_ICON_MUSIC 17
#define PLAYER_ICON_PLAY 18
#define PLAYER_ICON_STOP 19
#define PLAYER_ICON_LOOP 20
#define PLAYER_ICON_FOLDER 21
#define PLAYER_ICON_FILE 22

typedef struct eq_ui_theme
{
    const char *name;
    const char *description;
    const char *atlas_path;
    unsigned int clear_color;
    unsigned int bg_top;
    unsigned int bg_mid;
    unsigned int bg_bottom;
    unsigned int bg_overlay;
    unsigned int chrome;
    unsigned int footer;
    unsigned int text;
    unsigned int subtext;
    unsigned int row;
    unsigned int icon_bg;
    unsigned int icon_fg;
    unsigned int selected_backdrop;
    unsigned int selected_row;
    unsigned int selected_icon_bg;
    unsigned int selected_icon_fg;
    unsigned int selected_text;
    unsigned int accent;
} eq_ui_theme_t;

static const eq_ui_theme_t g_themes[] = {
    {
        "Vita Teal", "Default Vita-style teal", "app0:/assets/ui_icons_vita_teal.png",
        C(8, 31, 30, 255), C(11, 51, 47, 255), C(22, 122, 105, 255), C(6, 37, 36, 255),
        C(0, 0, 0, 20), C(4, 18, 17, 150), C(4, 16, 15, 175),
        C(245, 255, 249, 255), C(209, 246, 235, 255), C(42, 85, 77, 255),
        C(179, 238, 222, 255), C(11, 78, 67, 255), C(64, 152, 130, 255),
        C(201, 245, 224, 255), C(10, 112, 95, 255), C(216, 255, 240, 255),
        C(4, 50, 43, 255), C(112, 240, 199, 255)
    },
    {
        "Graphite Mono", "Neutral dark gray", "app0:/assets/ui_icons_graphite.png",
        C(12, 15, 18, 255), C(23, 27, 31, 255), C(48, 57, 65, 255), C(12, 15, 18, 255),
        C(0, 0, 0, 22), C(7, 9, 11, 172), C(7, 9, 11, 190),
        C(242, 246, 248, 255), C(199, 209, 215, 255), C(43, 51, 58, 255),
        C(214, 224, 230, 255), C(30, 40, 46, 255), C(105, 119, 131, 255),
        C(229, 237, 243, 255), C(82, 97, 107, 255), C(244, 251, 255, 255),
        C(17, 23, 27, 255), C(159, 177, 190, 255)
    },
    {
        "AMOLED Cyan", "Black with cyan focus", "app0:/assets/ui_icons_amoled_cyan.png",
        C(0, 0, 0, 255), C(0, 0, 0, 255), C(7, 31, 36, 255), C(0, 0, 0, 255),
        C(0, 0, 0, 14), C(0, 0, 0, 230), C(0, 0, 0, 238),
        C(243, 254, 255, 255), C(173, 223, 230, 255), C(11, 32, 37, 255),
        C(159, 248, 255, 255), C(0, 51, 58, 255), C(18, 185, 208, 255),
        C(143, 246, 255, 255), C(0, 116, 134, 255), C(236, 254, 255, 255),
        C(0, 25, 29, 255), C(0, 229, 255, 255)
    },
    {
        "Nord Frost", "Arctic blue-gray", "app0:/assets/ui_icons_nord_frost.png",
        C(36, 41, 51, 255), C(46, 52, 64, 255), C(59, 83, 99, 255), C(36, 41, 51, 255),
        C(0, 0, 0, 20), C(29, 33, 41, 165), C(29, 33, 41, 185),
        C(236, 239, 244, 255), C(197, 208, 221, 255), C(52, 59, 73, 255),
        C(183, 216, 232, 255), C(46, 68, 82, 255), C(136, 192, 208, 255),
        C(216, 222, 233, 255), C(94, 129, 172, 255), C(238, 246, 251, 255),
        C(46, 52, 64, 255), C(136, 192, 208, 255)
    },
    {
        "Midnight Blue", "Deep blue system style", "app0:/assets/ui_icons_midnight_blue.png",
        C(5, 13, 31, 255), C(7, 25, 54, 255), C(18, 58, 112, 255), C(5, 13, 31, 255),
        C(0, 0, 0, 24), C(2, 6, 15, 170), C(2, 6, 15, 190),
        C(241, 248, 255, 255), C(190, 216, 238, 255), C(20, 49, 84, 255),
        C(185, 221, 255, 255), C(11, 49, 87, 255), C(75, 145, 217, 255),
        C(215, 235, 255, 255), C(30, 107, 180, 255), C(244, 251, 255, 255),
        C(6, 23, 45, 255), C(99, 179, 255, 255)
    },
    {
        "Mocha Pastel", "Warm pastel dark", "app0:/assets/ui_icons_mocha_pastel.png",
        C(21, 21, 32, 255), C(30, 30, 46, 255), C(54, 51, 74, 255), C(21, 21, 32, 255),
        C(0, 0, 0, 20), C(13, 13, 20, 165), C(13, 13, 20, 190),
        C(245, 234, 255, 255), C(216, 204, 230, 255), C(49, 46, 66, 255),
        C(242, 205, 205, 255), C(59, 37, 55, 255), C(203, 166, 247, 255),
        C(245, 194, 231, 255), C(136, 57, 168, 255), C(255, 243, 255, 255),
        C(36, 22, 38, 255), C(203, 166, 247, 255)
    },
    {
        "Dracula Bloom", "Purple with bright accents", "app0:/assets/ui_icons_dracula_bloom.png",
        C(27, 29, 39, 255), C(40, 42, 54, 255), C(58, 47, 88, 255), C(27, 29, 39, 255),
        C(0, 0, 0, 24), C(13, 14, 19, 165), C(13, 14, 19, 190),
        C(248, 248, 242, 255), C(215, 215, 206, 255), C(49, 51, 68, 255),
        C(189, 147, 249, 255), C(36, 21, 51, 255), C(255, 121, 198, 255),
        C(80, 250, 123, 255), C(9, 36, 20, 255), C(80, 250, 123, 255),
        C(34, 19, 34, 255), C(255, 121, 198, 255)
    },
    {
        "Tokyo Neon", "Indigo cyan night", "app0:/assets/ui_icons_tokyo_neon.png",
        C(8, 11, 23, 255), C(17, 24, 39, 255), C(31, 42, 90, 255), C(8, 11, 23, 255),
        C(0, 0, 0, 22), C(4, 6, 12, 165), C(4, 6, 12, 190),
        C(240, 246, 255, 255), C(186, 199, 226, 255), C(32, 43, 82, 255),
        C(169, 177, 255, 255), C(17, 24, 58, 255), C(187, 154, 247, 255),
        C(125, 207, 255, 255), C(157, 124, 216, 255), C(251, 248, 255, 255),
        C(7, 20, 38, 255), C(125, 207, 255, 255)
    },
    {
        "Solar Amber", "Warm slate and amber", "app0:/assets/ui_icons_solar_amber.png",
        C(9, 21, 27, 255), C(16, 32, 42, 255), C(36, 64, 73, 255), C(9, 21, 27, 255),
        C(0, 0, 0, 22), C(5, 11, 14, 165), C(5, 11, 14, 190),
        C(248, 245, 223, 255), C(216, 208, 180, 255), C(35, 62, 69, 255),
        C(240, 198, 109, 255), C(53, 35, 0, 255), C(217, 155, 57, 255),
        C(244, 208, 111, 255), C(181, 105, 24, 255), C(255, 248, 221, 255),
        C(29, 22, 6, 255), C(244, 185, 66, 255)
    },
    {
        "Rose Pine", "Soft rose dark", "app0:/assets/ui_icons_rose_pine.png",
        C(18, 17, 26, 255), C(25, 23, 36, 255), C(43, 38, 56, 255), C(18, 17, 26, 255),
        C(0, 0, 0, 20), C(9, 8, 13, 165), C(9, 8, 13, 190),
        C(244, 237, 232, 255), C(215, 200, 203, 255), C(41, 36, 55, 255),
        C(235, 188, 186, 255), C(51, 32, 43, 255), C(196, 167, 231, 255),
        C(235, 188, 186, 255), C(159, 108, 132, 255), C(255, 243, 244, 255),
        C(37, 21, 27, 255), C(196, 167, 231, 255)
    },
    {
        "Aurora Light", "Cool light theme", "app0:/assets/ui_icons_aurora_light.png",
        C(233, 245, 243, 255), C(233, 245, 243, 255), C(203, 231, 229, 255), C(219, 231, 242, 255),
        C(255, 255, 255, 35), C(226, 242, 240, 230), C(215, 228, 238, 240),
        C(23, 51, 52, 255), C(78, 111, 113, 255), C(237, 246, 245, 255),
        C(196, 238, 232, 255), C(11, 94, 85, 255), C(91, 189, 177, 255),
        C(12, 127, 115, 255), C(8, 79, 73, 255), C(234, 255, 251, 255),
        C(243, 255, 251, 255), C(12, 127, 115, 255)
    },
    {
        "High Contrast", "Black, white, yellow focus", "app0:/assets/ui_icons_high_contrast.png",
        C(0, 0, 0, 255), C(0, 0, 0, 255), C(17, 17, 17, 255), C(0, 0, 0, 255),
        C(0, 0, 0, 0), C(0, 0, 0, 245), C(0, 0, 0, 245),
        C(255, 255, 255, 255), C(215, 215, 215, 255), C(22, 22, 22, 255),
        C(255, 255, 255, 255), C(0, 0, 0, 255), C(255, 255, 255, 255),
        C(255, 240, 74, 255), C(0, 0, 0, 255), C(255, 240, 74, 255),
        C(0, 0, 0, 255), C(255, 240, 74, 255)
    },
    {
        "Red Onyx", "OLED black and red", "app0:/assets/ui_icons_red_onyx.png",
        C(0, 0, 0, 255), C(0, 0, 0, 255), C(24, 6, 7, 255), C(0, 0, 0, 255),
        C(0, 0, 0, 16), C(0, 0, 0, 235), C(0, 0, 0, 240),
        C(255, 246, 246, 255), C(217, 183, 183, 255), C(22, 11, 13, 255),
        C(255, 179, 179, 255), C(74, 0, 0, 255), C(255, 122, 122, 255),
        C(255, 59, 59, 255), C(109, 0, 0, 255), C(255, 240, 240, 255),
        C(25, 0, 0, 255), C(255, 59, 59, 255)
    },
    {
        "Crimson Vita", "Polished dark crimson", "app0:/assets/ui_icons_crimson_vita.png",
        C(20, 2, 7, 255), C(42, 7, 13, 255), C(111, 23, 36, 255), C(20, 2, 7, 255),
        C(0, 0, 0, 20), C(10, 1, 3, 170), C(10, 1, 3, 190),
        C(255, 245, 247, 255), C(240, 197, 204, 255), C(74, 23, 32, 255),
        C(255, 204, 210, 255), C(102, 19, 31, 255), C(217, 77, 98, 255),
        C(255, 217, 222, 255), C(179, 22, 46, 255), C(255, 242, 244, 255),
        C(57, 7, 17, 255), C(255, 80, 104, 255)
    },
    {
        "Ember Core", "Warm red-orange", "app0:/assets/ui_icons_ember_core.png",
        C(9, 3, 2, 255), C(23, 8, 6, 255), C(87, 32, 22, 255), C(9, 3, 2, 255),
        C(0, 0, 0, 20), C(5, 2, 1, 170), C(5, 2, 1, 190),
        C(255, 247, 240, 255), C(227, 194, 173, 255), C(50, 25, 19, 255),
        C(255, 192, 154, 255), C(77, 23, 7, 255), C(255, 90, 46, 255),
        C(255, 178, 122, 255), C(170, 43, 10, 255), C(255, 247, 239, 255),
        C(43, 11, 0, 255), C(255, 99, 51, 255)
    },
    {
        "Ruby Noir", "Dark wine and ruby", "app0:/assets/ui_icons_ruby_noir.png",
        C(13, 5, 11, 255), C(22, 10, 20, 255), C(61, 22, 48, 255), C(13, 5, 11, 255),
        C(0, 0, 0, 20), C(7, 3, 6, 170), C(7, 3, 6, 190),
        C(255, 242, 246, 255), C(224, 190, 203, 255), C(47, 25, 41, 255),
        C(245, 182, 201, 255), C(77, 16, 41, 255), C(217, 69, 114, 255),
        C(245, 182, 201, 255), C(151, 33, 73, 255), C(255, 244, 247, 255),
        C(42, 7, 22, 255), C(229, 71, 120, 255)
    },
};

static vita2d_pgf *g_font;
static vita2d_texture *g_icon_atlas;
static int g_pgf_module_loaded;
static int g_ui_ready;
static int g_theme_index = EQ_UI_DEFAULT_THEME_INDEX;

static unsigned int mix_color(unsigned int a, unsigned int b, int t, int max_t)
{
    int ar = a & 0xff;
    int ag = (a >> 8) & 0xff;
    int ab = (a >> 16) & 0xff;
    int aa = (a >> 24) & 0xff;
    int br = b & 0xff;
    int bg = (b >> 8) & 0xff;
    int bb = (b >> 16) & 0xff;
    int ba = (b >> 24) & 0xff;

    if (max_t <= 0) {
        return a;
    }

    ar += ((br - ar) * t) / max_t;
    ag += ((bg - ag) * t) / max_t;
    ab += ((bb - ab) * t) / max_t;
    aa += ((ba - aa) * t) / max_t;
    return C(ar, ag, ab, aa);
}

static unsigned int color_alpha(unsigned int color, int alpha)
{
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    return (color & 0x00ffffffu) | ((unsigned int)alpha << 24);
}

static const eq_ui_theme_t *theme(void)
{
    if (g_theme_index < 0 || g_theme_index >= eq_ui_theme_count()) {
        g_theme_index = EQ_UI_DEFAULT_THEME_INDEX;
    }
    return &g_themes[g_theme_index];
}

static int is_latin_char(unsigned int ch)
{
    return (ch >= 0x20 && ch <= 0x7e) || (ch >= 0xa0 && ch <= 0xff);
}

static int icon_index(const char *icon)
{
    if (!icon) return 10;
    if (strcmp(icon, "simple") == 0 || strcmp(icon, "tune") == 0) return 0;
    if (strcmp(icon, "advanced") == 0) return 1;
    if (strcmp(icon, "speaker") == 0 || strcmp(icon, "route") == 0) return 2;
    if (strcmp(icon, "settings") == 0 || strcmp(icon, "hpf") == 0 || strcmp(icon, "headroom") == 0 ||
        strcmp(icon, "theme") == 0 || strcmp(icon, "themes") == 0) return 3;
    if (strcmp(icon, "about") == 0 || strcmp(icon, "info") == 0 || strcmp(icon, "help") == 0) return 4;
    if (strcmp(icon, "preset") == 0) return 5;
    if (strcmp(icon, "load") == 0 || strcmp(icon, "nav") == 0) return 6;
    if (strcmp(icon, "reset") == 0) return 7;
    if (strcmp(icon, "power") == 0) return 8;
    if (strcmp(icon, "level") == 0) return 9;
    if (strcmp(icon, "status") == 0) return 10;
    if (strcmp(icon, "save") == 0) return 11;
    if (strcmp(icon, "music") == 0) return PLAYER_ICON_MUSIC;
    if (strcmp(icon, "play") == 0) return PLAYER_ICON_PLAY;
    if (strcmp(icon, "stop") == 0) return PLAYER_ICON_STOP;
    if (strcmp(icon, "loop") == 0) return PLAYER_ICON_LOOP;
    if (strcmp(icon, "folder") == 0) return PLAYER_ICON_FOLDER;
    if (strcmp(icon, "file") == 0) return PLAYER_ICON_FILE;
    return 10;
}

static void draw_text(float x, float baseline, float scale, unsigned int color, const char *text)
{
    if (!g_font || !text) {
        return;
    }
    vita2d_pgf_draw_text(g_font, (int)x, (int)baseline, color, scale, text);
}

static void draw_text_bold(float x, float baseline, float scale, unsigned int color, const char *text)
{
    draw_text(x, baseline, scale, color, text);
    draw_text(x + 1.0f, baseline, scale, color, text);
}

static int text_width(float scale, const char *text)
{
    if (!g_font || !text) {
        return 0;
    }
    return vita2d_pgf_text_width(g_font, scale, text);
}

static void fit_text(char *out, size_t out_size, float scale, const char *text, int max_width)
{
    size_t len;
    size_t prefix;

    if (!out || out_size == 0) {
        return;
    }

    if (!text) {
        text = "";
    }

    len = strlen(text);
    if (len >= out_size) {
        len = out_size - 1;
    }

    memcpy(out, text, len);
    out[len] = 0;

    if (max_width <= 0 || text_width(scale, out) <= max_width) {
        return;
    }

    if (out_size < 5) {
        out[0] = 0;
        return;
    }

    prefix = len > 3 ? len - 3 : 0;
    while (prefix > 0) {
        if (prefix + 3 >= out_size) {
            prefix = out_size - 4;
        }
        memcpy(out, text, prefix);
        out[prefix] = '.';
        out[prefix + 1] = '.';
        out[prefix + 2] = '.';
        out[prefix + 3] = 0;
        if (text_width(scale, out) <= max_width) {
            return;
        }
        prefix--;
    }

    out[0] = '.';
    out[1] = '.';
    out[2] = '.';
    out[3] = 0;
}

static void draw_text_fit(float x, float baseline, float scale, unsigned int color, const char *text, int max_width)
{
    char fitted[192];

    fit_text(fitted, sizeof(fitted), scale, text, max_width);
    draw_text(x, baseline, scale, color, fitted);
}

static void draw_text_right(float right, float baseline, float scale, unsigned int color, const char *text)
{
    draw_text(right - (float)text_width(scale, text), baseline, scale, color, text);
}

static void draw_text_right_fit(float right, float baseline, float scale, unsigned int color, const char *text, int max_width)
{
    char fitted[192];

    fit_text(fitted, sizeof(fitted), scale, text, max_width);
    draw_text_right(right, baseline, scale, color, fitted);
}

static void draw_background(void)
{
    const eq_ui_theme_t *t = theme();

    for (int y = 0; y < EQ_UI_SCREEN_H; y += 4) {
        unsigned int color;
        if (y < EQ_UI_SCREEN_H / 2) {
            color = mix_color(t->bg_top, t->bg_mid, y, EQ_UI_SCREEN_H / 2);
        } else {
            color = mix_color(t->bg_mid, t->bg_bottom, y - EQ_UI_SCREEN_H / 2, EQ_UI_SCREEN_H / 2);
        }
        vita2d_draw_rectangle(0.0f, (float)y, (float)EQ_UI_SCREEN_W, 4.0f, color);
    }

    vita2d_draw_rectangle(0, 0, EQ_UI_SCREEN_W, EQ_UI_SCREEN_H, t->bg_overlay);
    vita2d_draw_rectangle(0, 0, EQ_UI_SCREEN_W, 32, t->chrome);
    vita2d_draw_rectangle(0, EQ_UI_FOOTER_Y, EQ_UI_SCREEN_W, EQ_UI_SCREEN_H - EQ_UI_FOOTER_Y, t->footer);
}

static void wait_for_render_idle(void)
{
    if (g_ui_ready) {
        vita2d_wait_rendering_done();
    }
}

static void free_icon_atlas(void)
{
    if (g_icon_atlas) {
        wait_for_render_idle();
        vita2d_free_texture(g_icon_atlas);
        g_icon_atlas = NULL;
    }
}

static int load_icon_atlas(void)
{
    free_icon_atlas();
    g_icon_atlas = vita2d_load_PNG_file(theme()->atlas_path);
    if (!g_icon_atlas && g_theme_index != EQ_UI_DEFAULT_THEME_INDEX) {
        g_icon_atlas = vita2d_load_PNG_file(g_themes[EQ_UI_DEFAULT_THEME_INDEX].atlas_path);
    }
    if (g_icon_atlas) {
        vita2d_texture_set_filters(g_icon_atlas, SCE_GXM_TEXTURE_FILTER_LINEAR, SCE_GXM_TEXTURE_FILTER_LINEAR);
        return 0;
    }
    return -1;
}

int eq_ui_theme_count(void)
{
    return (int)(sizeof(g_themes) / sizeof(g_themes[0]));
}

int eq_ui_theme_index(void)
{
    return g_theme_index;
}

int eq_ui_set_theme(int index)
{
    if (index < 0 || index >= eq_ui_theme_count()) {
        index = EQ_UI_DEFAULT_THEME_INDEX;
    }

    if (g_theme_index == index && (!g_ui_ready || g_icon_atlas)) {
        return 0;
    }

    g_theme_index = index;
    if (g_ui_ready) {
        vita2d_set_clear_color(theme()->clear_color);
        return load_icon_atlas();
    }
    return 0;
}

const char *eq_ui_theme_name(int index)
{
    if (index < 0 || index >= eq_ui_theme_count()) {
        return "";
    }
    return g_themes[index].name;
}

const char *eq_ui_theme_description(int index)
{
    if (index < 0 || index >= eq_ui_theme_count()) {
        return "";
    }
    return g_themes[index].description;
}

int eq_ui_init(void)
{
    if (vita2d_init() < 0) {
        return -1;
    }

    vita2d_set_vblank_wait(1);
    vita2d_set_clear_color(theme()->clear_color);

    if (sceSysmoduleLoadModule(SCE_SYSMODULE_PGF) >= 0) {
        g_pgf_module_loaded = 1;
    }

    vita2d_system_pgf_config configs[] = {
        { SCE_FONT_LANGUAGE_LATIN, is_latin_char },
        { SCE_FONT_LANGUAGE_DEFAULT, NULL },
    };

    g_font = vita2d_load_system_pgf(2, configs);
    if (!g_font) {
        vita2d_fini();
        if (g_pgf_module_loaded) {
            sceSysmoduleUnloadModule(SCE_SYSMODULE_PGF);
            g_pgf_module_loaded = 0;
        }
        return -1;
    }

    g_ui_ready = 1;
    load_icon_atlas();

    return 0;
}

void eq_ui_fini(void)
{
    wait_for_render_idle();
    free_icon_atlas();
    if (g_font) {
        vita2d_free_pgf(g_font);
        g_font = NULL;
    }
    g_ui_ready = 0;
    vita2d_fini();
    if (g_pgf_module_loaded) {
        sceSysmoduleUnloadModule(SCE_SYSMODULE_PGF);
        g_pgf_module_loaded = 0;
    }
}

void eq_ui_begin_frame(void)
{
    vita2d_start_drawing();
    vita2d_clear_screen();
    draw_background();
}

void eq_ui_end_frame(void)
{
    vita2d_end_drawing();
    vita2d_swap_buffers();
}

void eq_ui_draw_shell(const char *title,
                      const char *subtitle,
                      const char *left_status,
                      const char *right_status)
{
    const eq_ui_theme_t *t = theme();
    int title_w;
    char fitted_subtitle[192];
    int subtitle_w;

    if (!title) {
        title = "";
    }
    if (!subtitle) {
        subtitle = "";
    }
    title_w = text_width(TEXT_SIZE, title);
    fit_text(fitted_subtitle, sizeof(fitted_subtitle), TEXT_SIZE, subtitle, 820);
    subtitle_w = text_width(TEXT_SIZE, fitted_subtitle);

    draw_text_fit(24, 24, TEXT_SIZE, t->text, left_status, 380);
    draw_text_right_fit(936, 24, TEXT_SIZE, t->text, right_status, 380);
    if (title[0]) {
        draw_text((EQ_UI_SCREEN_W - title_w) / 2.0f, 66, TEXT_SIZE, t->text, title);
    }
    if (fitted_subtitle[0]) {
        draw_text((EQ_UI_SCREEN_W - subtitle_w) / 2.0f, 96, TEXT_SIZE, t->subtext, fitted_subtitle);
    }
}

static void draw_icon_cell(float cx, float cy, int idx, int variant, float scale, unsigned int color)
{
    if (g_icon_atlas) {
        vita2d_draw_texture_tint_part_scale(g_icon_atlas,
                                            cx - (ICON_CELL * scale) / 2.0f,
                                            cy - (ICON_CELL * scale) / 2.0f,
                                            idx * ICON_CELL, variant * ICON_CELL, ICON_CELL, ICON_CELL,
                                            scale, scale, color);
    }
}

static float draw_footer_symbol_label(float x, int icon, const char *label)
{
    unsigned int color = theme()->text;
    float label_x = x + 28.0f;

    draw_icon_cell(x + 10.0f, 525, icon, 0, FOOTER_ICON_SCALE, color);
    draw_text(label_x, 530, TEXT_SIZE, color, label);
    return label_x + (float)text_width(TEXT_SIZE, label) + 34.0f;
}

static int footer_icon_from_text(const char *text, int fallback)
{
    if (!text) {
        return fallback;
    }
    if (strstr(text, "Circle")) {
        return FOOTER_ICON_CIRCLE;
    }
    if (strstr(text, "Cross")) {
        return FOOTER_ICON_CROSS;
    }
    if (strstr(text, "Triangle")) {
        return FOOTER_ICON_TRIANGLE;
    }
    return fallback;
}

void eq_ui_draw_footer(const char *left, const char *center, const char *right)
{
    unsigned int color = theme()->text;
    const char *left_label = (left && strstr(left, "Back")) ? "Back" : "Exit";
    int left_icon = footer_icon_from_text(left, FOOTER_ICON_CIRCLE);
    int select_icon = footer_icon_from_text(center, FOOTER_ICON_CROSS);
    int help_icon = footer_icon_from_text(right, FOOTER_ICON_TRIANGLE);
    int show_select = center && strstr(center, "Select");
    float x = 404.0f;

    draw_footer_symbol_label(24, left_icon, left_label);

    if (show_select) {
        x = draw_footer_symbol_label(366, select_icon, "Select");
    }
    draw_text_bold(x, 530, TEXT_SIZE, color, "START");
    draw_text(x + text_width(TEXT_SIZE, "START") + 10, 530, TEXT_SIZE, color, "Bypass");

    draw_footer_symbol_label(862, help_icon, "Help");
}

void eq_ui_draw_message(const char *message)
{
    const eq_ui_theme_t *t = theme();
    int w;
    int x;

    if (!message || !message[0]) {
        return;
    }

    w = text_width(TEXT_SIZE, message) + 44;
    if (w < 260) {
        w = 260;
    }
    if (w > 840) {
        w = 840;
    }

    x = (EQ_UI_SCREEN_W - w) / 2;
    vita2d_draw_rectangle((float)x, 450, (float)w, 40, mix_color(t->bg_bottom, t->row, 1, 2));
    vita2d_draw_rectangle((float)x, 454, (float)w, 2, t->accent);
    draw_text_fit((float)x + 22, 477, TEXT_SIZE, t->text, message, w - 44);
}

static void draw_icon_symbol(float cx, float cy, const char *icon, eq_ui_row_kind_t kind, int selected);

void eq_ui_draw_confirm_dialog(const char *title,
                               const char *body,
                               const char * const *actions,
                               const char * const *icons,
                               int action_count,
                               int selected_action)
{
    const eq_ui_theme_t *t = theme();
    int dialog_h;
    int x = EQ_UI_DIALOG_X;
    int y = EQ_UI_DIALOG_Y;
    int w = EQ_UI_DIALOG_W;

    if (!actions || action_count <= 0) {
        return;
    }
    if (action_count > 4) {
        action_count = 4;
    }
    if (selected_action < 0) {
        selected_action = 0;
    }
    if (selected_action >= action_count) {
        selected_action = action_count - 1;
    }

    dialog_h = 92 + action_count * EQ_UI_DIALOG_ROW_H + (action_count - 1) * EQ_UI_DIALOG_ROW_GAP + 28;

    vita2d_draw_rectangle(0, 0, EQ_UI_SCREEN_W, EQ_UI_SCREEN_H, color_alpha(C(0, 0, 0, 255), 104));
    vita2d_draw_rectangle((float)x, (float)y, (float)w, (float)dialog_h, mix_color(t->chrome, t->row, 2, 3));
    vita2d_draw_rectangle((float)x, (float)y, (float)w, 4.0f, t->accent);
    vita2d_draw_rectangle((float)x, (float)(y + dialog_h - 4), (float)w, 4.0f, color_alpha(t->accent, 160));

    draw_text_fit((float)x + 28, (float)y + 34, TEXT_SIZE, t->text, title, w - 56);
    draw_text_fit((float)x + 28, (float)y + 64, TEXT_SIZE, t->subtext, body, w - 56);

    for (int i = 0; i < action_count; ++i) {
        int row_y = EQ_UI_DIALOG_ROW_Y + i * (EQ_UI_DIALOG_ROW_H + EQ_UI_DIALOG_ROW_GAP);
        int selected = i == selected_action;
        unsigned int back = selected ? t->selected_row : mix_color(t->row, t->bg_bottom, 2, 3);
        unsigned int text = selected ? t->selected_text : t->text;
        const char *icon = icons ? icons[i] : NULL;

        vita2d_draw_rectangle((float)(x + 18), (float)row_y, (float)(w - 36), EQ_UI_DIALOG_ROW_H, back);
        if (selected) {
            vita2d_draw_rectangle((float)(x + 18), (float)row_y, 6.0f, EQ_UI_DIALOG_ROW_H, t->accent);
        }
        draw_icon_symbol((float)x + 50, (float)row_y + EQ_UI_DIALOG_ROW_H * 0.5f, icon, EQ_UI_ROW_ACTION, selected);
        draw_text_fit((float)x + 82, (float)row_y + 31, TEXT_SIZE, text, actions[i], w - 150);
        if (selected) {
            float check_x = (float)(x + w - 54);
            float check_y = (float)row_y + EQ_UI_DIALOG_ROW_H * 0.5f;
            unsigned int check_back = color_alpha(t->selected_icon_bg, 235);
            unsigned int check_text = t->selected_icon_fg;
            vita2d_draw_fill_circle(check_x, check_y, 12.0f, check_back);
            vita2d_draw_line(check_x - 6.0f, check_y, check_x - 2.0f, check_y + 5.0f, check_text);
            vita2d_draw_line(check_x - 5.0f, check_y, check_x - 1.0f, check_y + 5.0f, check_text);
            vita2d_draw_line(check_x - 2.0f, check_y + 5.0f, check_x + 8.0f, check_y - 7.0f, check_text);
            vita2d_draw_line(check_x - 1.0f, check_y + 5.0f, check_x + 9.0f, check_y - 7.0f, check_text);
        }
    }
}

void eq_ui_draw_status_chip(float x,
                            float y,
                            const char *label,
                            const char *value,
                            unsigned int color)
{
    const eq_ui_theme_t *t = theme();

    vita2d_draw_rectangle(x, y, 180, 42, mix_color(t->row, t->bg_bottom, 1, 3));
    vita2d_draw_rectangle(x, y, 5, 42, color);
    draw_text_fit(x + 16, y + 17, TEXT_SIZE, t->subtext, label, 146);
    draw_text_fit(x + 16, y + 37, TEXT_SIZE, t->text, value, 146);
}

static void draw_icon_symbol(float cx, float cy, const char *icon, eq_ui_row_kind_t kind, int selected)
{
    const eq_ui_theme_t *t = theme();

    if (g_icon_atlas) {
        int variant = selected ? ICON_VARIANT_SELECTED :
            kind == EQ_UI_ROW_READONLY ? ICON_VARIANT_READONLY : ICON_VARIANT_NORMAL;
        draw_icon_cell(cx, cy, icon_index(icon), variant, ICON_SCALE, C(255, 255, 255, 255));
    } else {
        unsigned int backplate = selected ? t->selected_icon_bg :
            kind == EQ_UI_ROW_READONLY ? mix_color(t->icon_bg, t->row, 1, 3) : t->icon_bg;
        unsigned int color = selected ? t->selected_icon_fg : t->icon_fg;
        vita2d_draw_fill_circle(cx, cy, 14, backplate);
        vita2d_draw_fill_circle(cx, cy, 5, color);
    }
}

static void draw_row_affordance(float right, float y, eq_ui_row_kind_t kind, int selected, unsigned int color)
{
    const eq_ui_theme_t *t = theme();
    unsigned int dim = selected ? t->selected_icon_bg : color_alpha(t->subtext, 225);

    if (kind == EQ_UI_ROW_NAV) {
        draw_icon_cell(right - 6, y + 26, AFFORDANCE_ICON_RIGHT, 0, CHEVRON_ICON_SCALE, color);
    } else if (kind == EQ_UI_ROW_ACTION && selected) {
        vita2d_draw_rectangle(right - 20, y + 17, 20, 18, dim);
        vita2d_draw_line(right - 15, y + 21, right - 5, y + 31, t->selected_icon_fg);
        vita2d_draw_line(right - 5, y + 21, right - 15, y + 31, t->selected_icon_fg);
    } else if (kind == EQ_UI_ROW_ADJUST) {
        draw_icon_cell(right - 31, y + 26, AFFORDANCE_ICON_LEFT, 0, CHEVRON_ICON_SCALE, dim);
        draw_icon_cell(right - 6, y + 26, AFFORDANCE_ICON_RIGHT, 0, CHEVRON_ICON_SCALE, dim);
    }
}

static void add_bound(eq_ui_row_bounds_t *bounds,
                      int max_bounds,
                      int *bound_count,
                      int row,
                      int x,
                      int y,
                      int w,
                      int h)
{
    if (!bounds || !bound_count || *bound_count < 0 || *bound_count >= max_bounds) {
        return;
    }

    bounds[*bound_count].row = row;
    bounds[*bound_count].x = x;
    bounds[*bound_count].y = y;
    bounds[*bound_count].w = w;
    bounds[*bound_count].h = h;
    (*bound_count)++;
}

static void draw_panel(float x, float y, float w, float h, unsigned int color)
{
    const eq_ui_theme_t *t = theme();

    vita2d_draw_rectangle(x, y, w, h, color);
    vita2d_draw_rectangle(x, y, w, 2.0f, color_alpha(t->accent, 120));
    vita2d_draw_rectangle(x, y + h - 2.0f, w, 2.0f, color_alpha(t->bg_bottom, 110));
}

static void draw_music_chip(float x, float y, float w, const char *label, const char *value)
{
    const eq_ui_theme_t *t = theme();

    vita2d_draw_rectangle(x, y, w, 42.0f, mix_color(t->row, t->bg_bottom, 1, 4));
    vita2d_draw_rectangle(x, y, 4.0f, 42.0f, color_alpha(t->accent, 190));
    draw_text_fit(x + 14.0f, y + 17.0f, TEXT_SIZE, t->subtext, label, (int)w - 28);
    draw_text_fit(x + 14.0f, y + 37.0f, TEXT_SIZE, t->text, value, (int)w - 28);
}

static void draw_music_visualizer(float x, float y, float w, float h)
{
    const eq_ui_theme_t *t = theme();
    static const int heights[] = {
        18, 22, 26, 31, 38, 50, 66, 78, 62, 46, 35, 29,
        20, 16, 24, 19, 21, 30, 38, 48, 58, 44, 36, 32,
        28, 22, 18, 16, 24, 34, 42, 25
    };
    int count = (int)(sizeof(heights) / sizeof(heights[0]));
    float gap = 5.0f;
    float bar_w = (w - gap * (count - 1)) / count;

    if (bar_w < 3.0f) {
        bar_w = 3.0f;
    }

    vita2d_draw_rectangle(x, y + h - 3.0f, w, 3.0f, color_alpha(t->accent, 150));
    for (int i = 0; i < count; ++i) {
        float bar_h = (float)heights[i];
        if (bar_h > h - 8.0f) {
            bar_h = h - 8.0f;
        }
        vita2d_draw_rectangle(x + i * (bar_w + gap),
                              y + h - bar_h,
                              bar_w,
                              bar_h - 4.0f,
                              color_alpha(t->accent, 175));
    }
}

static void draw_music_action_row(float x,
                                  float y,
                                  float w,
                                  float h,
                                  int selected,
                                  int row_index,
                                  const char *icon,
                                  const char *label,
                                  const char *description,
                                  const char *value,
                                  eq_ui_row_kind_t kind,
                                  eq_ui_row_bounds_t *bounds,
                                  int max_bounds,
                                  int *bound_count)
{
    const eq_ui_theme_t *t = theme();
    unsigned int back = selected ? t->selected_row :
        kind == EQ_UI_ROW_ADJUST ? mix_color(t->row, t->selected_backdrop, 1, 7) :
        kind == EQ_UI_ROW_READONLY ? mix_color(t->row, t->bg_bottom, 1, 3) :
        mix_color(t->row, t->selected_backdrop, 1, 10);
    unsigned int text = selected ? t->selected_text : t->text;
    unsigned int sub = selected ? t->selected_text : t->subtext;

    add_bound(bounds, max_bounds, bound_count, row_index, (int)x, (int)y, (int)w, (int)h);

    if (selected) {
        vita2d_draw_rectangle(x - 8.0f, y - 3.0f, w + 16.0f, h + 6.0f, t->selected_backdrop);
    }
    vita2d_draw_rectangle(x, y, w, h, back);
    draw_icon_symbol(x + 28.0f, y + h * 0.5f, icon, kind, selected);
    draw_text_fit(x + 58.0f, y + h * 0.44f, TEXT_SIZE, text, label, 265);
    draw_text_fit(x + 58.0f, y + h * 0.86f, TEXT_SIZE, sub, description, 310);
    if (value && value[0]) {
        const float value_right = kind == EQ_UI_ROW_ADJUST ? x + w - 78.0f : x + w - 52.0f;
        const int value_width = kind == EQ_UI_ROW_ADJUST ? 112 : 150;
        draw_text_right_fit(value_right, y + h * 0.64f, TEXT_SIZE, text, value, value_width);
    }
    draw_row_affordance(x + w - 20.0f, y - 4.0f, kind, selected, text);
}

void eq_ui_draw_music_player_deck(const eq_ui_music_player_model_t *model)
{
    const eq_ui_theme_t *t = theme();
    int count;

    if (!model) {
        return;
    }
    if (model->bound_count) {
        *model->bound_count = 0;
    }

    draw_panel(52.0f, 74.0f, 326.0f, 304.0f, mix_color(t->row, t->bg_mid, 1, 5));
    draw_icon_cell(215.0f, 191.0f, PLAYER_ICON_MUSIC, ICON_VARIANT_NORMAL, 2.1f, C(255, 255, 255, 255));
    draw_music_visualizer(76.0f, 296.0f, 278.0f, 60.0f);

    draw_panel(52.0f, 392.0f, 326.0f, 88.0f, mix_color(t->row, t->bg_bottom, 1, 4));
    draw_text_fit(72.0f, 418.0f, TEXT_SIZE, t->subtext, "EQ session", 130);
    draw_text_fit(72.0f, 449.0f, TEXT_SIZE, t->text, model->preset, 84);
    draw_text_fit(176.0f, 449.0f, TEXT_SIZE, t->text, model->output, 96);
    draw_text_right_fit(356.0f, 449.0f, TEXT_SIZE, t->text, model->eq_state, 68);

    draw_panel(398.0f, 74.0f, 510.0f, 104.0f, mix_color(t->row, t->bg_bottom, 1, 4));
    draw_text_fit(424.0f, 105.0f, TEXT_SIZE, t->text,
                  model->file_name && model->file_name[0] ? model->file_name : "No song selected",
                  450);
    draw_text_fit(424.0f, 134.0f, TEXT_SIZE, t->subtext,
                  model->file_path && model->file_path[0] ? model->file_path : "Choose a local OGG, MP3, or WAV",
                  450);
    draw_text_fit(424.0f, 162.0f, TEXT_SIZE, t->subtext, model->stream_info, 350);
    draw_text_right_fit(884.0f, 162.0f, TEXT_SIZE, t->text, model->state, 170);

    draw_music_chip(398.0f, 190.0f, 158.0f, "Preamp", model->preamp);
    draw_music_chip(574.0f, 190.0f, 158.0f, "Sound mode", model->headroom);
    draw_music_chip(750.0f, 190.0f, 158.0f, "Output", model->output);

    count = model->action_count;
    if (count > 5) {
        count = 5;
    }
    for (int i = 0; i < count; ++i) {
        const eq_ui_music_action_row_t *row = &model->actions[i];
        draw_music_action_row(398.0f,
                              252.0f + i * 45.0f,
                              510.0f,
                              42.0f,
                              row->row_index == model->selected_row,
                              row->row_index,
                              row->icon,
                              row->label,
                              row->description,
                              row->value,
                              row->kind,
                              model->bounds,
                              model->max_bounds,
                              model->bound_count);
    }
}

void eq_ui_draw_music_browser_deck(const eq_ui_music_browser_model_t *model)
{
    const eq_ui_theme_t *t = theme();

    if (!model) {
        return;
    }
    if (model->bound_count) {
        *model->bound_count = 0;
    }

    draw_panel(52.0f, 74.0f, 292.0f, 406.0f, mix_color(t->row, t->bg_mid, 1, 5));
    draw_icon_cell(198.0f, 178.0f, PLAYER_ICON_FOLDER, ICON_VARIANT_NORMAL, 1.75f, C(255, 255, 255, 255));
    draw_text_fit(78.0f, 304.0f, TEXT_SIZE, t->text,
                  model->title ? model->title : "Choose music", 238);
    draw_text_fit(78.0f, 333.0f, TEXT_SIZE, t->subtext,
                  model->instruction ? model->instruction : "Open folders or play a song", 238);
    draw_text_fit(78.0f, 402.0f, TEXT_SIZE, t->subtext, "Current folder", 238);
    draw_text_fit(78.0f, 431.0f, TEXT_SIZE, t->text,
                  model->path && model->path[0] ? model->path : "Storage roots",
                  238);

    draw_panel(370.0f, 74.0f, 538.0f, 48.0f, mix_color(t->row, t->bg_bottom, 1, 4));
    draw_text_fit(394.0f, 105.0f, TEXT_SIZE, t->text,
                  model->path && model->path[0] ? model->path : "Pick a storage device",
                  480);

    if (model->entry_count <= 0) {
        draw_panel(370.0f, 144.0f, 538.0f, 92.0f, mix_color(t->row, t->bg_bottom, 1, 3));
        draw_text_fit(394.0f, 181.0f, TEXT_SIZE, t->text,
                      model->empty_message ? model->empty_message : "No music found here", 480);
        draw_text_fit(394.0f, 211.0f, TEXT_SIZE, t->subtext, "Try another folder or storage device", 480);
        return;
    }

    for (int i = 0; i < model->entry_count; ++i) {
        const eq_ui_music_browser_entry_t *entry = &model->entries[i];
        draw_music_action_row(370.0f,
                              144.0f + i * 48.0f,
                              538.0f,
                              45.0f,
                              entry->row_index == model->selected_row,
                              entry->row_index,
                              entry->icon,
                              entry->label,
                              entry->description,
                              entry->value,
                              entry->kind,
                              model->bounds,
                              model->max_bounds,
                              model->bound_count);
    }
}

void eq_ui_draw_row(int visible_index,
                    int row_index,
                    int selected,
                    const char *icon,
                    const char *label,
                    const char *description,
                    const char *value,
                    eq_ui_row_kind_t kind,
                    eq_ui_row_bounds_t *bounds)
{
    const eq_ui_theme_t *t = theme();
    int y = EQ_UI_LIST_Y + visible_index * (EQ_UI_ROW_H + EQ_UI_ROW_GAP);
    int text_x = EQ_UI_LIST_X + 62;
    int value_reserve = 22;
    int value_right;
    unsigned int row_color = t->row;
    unsigned int text_color = t->text;
    unsigned int sub_color = t->subtext;

    if (kind == EQ_UI_ROW_READONLY) {
        row_color = mix_color(t->row, t->bg_bottom, 1, 3);
        sub_color = mix_color(t->subtext, t->bg_bottom, 5, 18);
    } else if (kind == EQ_UI_ROW_SECTION) {
        row_color = C(0, 0, 0, 0);
        sub_color = t->subtext;
    } else if (kind == EQ_UI_ROW_ACTION) {
        row_color = mix_color(t->row, t->selected_backdrop, 1, 9);
    } else if (kind == EQ_UI_ROW_ADJUST) {
        row_color = mix_color(t->row, t->selected_backdrop, 1, 7);
    }

    if (selected) {
        row_color = t->selected_row;
        text_color = t->selected_text;
        sub_color = t->selected_text;
    }

    if (kind == EQ_UI_ROW_ADJUST) {
        value_reserve = 86;
    } else if (kind == EQ_UI_ROW_NAV || (kind == EQ_UI_ROW_ACTION && selected)) {
        value_reserve = 54;
    }
    value_right = EQ_UI_LIST_X + EQ_UI_LIST_W - value_reserve;

    if (kind == EQ_UI_ROW_SECTION) {
        int label_w = text_width(TEXT_SIZE, label);
        unsigned int line_color = color_alpha(t->accent, 130);
        if (bounds) {
            bounds->row = row_index;
            bounds->x = EQ_UI_LIST_X;
            bounds->y = y;
            bounds->w = EQ_UI_LIST_W;
            bounds->h = EQ_UI_ROW_H;
        }
        vita2d_draw_rectangle(EQ_UI_LIST_X, y + 26, 250, 2, line_color);
        vita2d_draw_rectangle(EQ_UI_LIST_X + EQ_UI_LIST_W - 250, y + 26, 250, 2, line_color);
        draw_text((EQ_UI_SCREEN_W - label_w) / 2.0f, y + 31, TEXT_SIZE, t->subtext, label);
        if (description && description[0]) {
            int desc_w = text_width(TEXT_SIZE, description);
            draw_text((EQ_UI_SCREEN_W - desc_w) / 2.0f, y + 50, TEXT_SIZE, sub_color, description);
        }
        return;
    }

    if (bounds) {
        bounds->row = row_index;
        bounds->x = EQ_UI_LIST_X;
        bounds->y = y;
        bounds->w = EQ_UI_LIST_W;
        bounds->h = EQ_UI_ROW_H;
    }

    if (selected) {
        vita2d_draw_rectangle(EQ_UI_LIST_X - 10, y - 4, EQ_UI_LIST_W + 20, EQ_UI_ROW_H + 8, t->selected_backdrop);
    }

    vita2d_draw_rectangle(EQ_UI_LIST_X, y, EQ_UI_LIST_W, EQ_UI_ROW_H, row_color);
    draw_icon_symbol(EQ_UI_LIST_X + 30, y + 26, icon, kind, selected);

    draw_text_fit(text_x, y + 23, TEXT_SIZE, text_color, label, 420);
    if (description && description[0]) {
        draw_text_fit(text_x, y + 44, TEXT_SIZE, sub_color, description, 470);
    }

    if (value && value[0]) {
        draw_text_right_fit(value_right, y + 31, TEXT_SIZE, text_color, value, 250);
    }
    draw_row_affordance(EQ_UI_LIST_X + EQ_UI_LIST_W - 18, (float)y, kind, selected, text_color);
}

void eq_ui_draw_slider(int visible_index,
                       int row_index,
                       int selected,
                       const char *icon,
                       const char *label,
                       const char *description,
                       const char *value,
                       int32_t amount_mdB,
                       eq_ui_row_kind_t kind,
                       eq_ui_row_bounds_t *bounds)
{
    const eq_ui_theme_t *t = theme();
    int y = EQ_UI_LIST_Y + visible_index * (EQ_UI_ROW_H + EQ_UI_ROW_GAP);
    int bar_x = EQ_UI_LIST_X + EQ_UI_LIST_W - 260;
    int bar_y = y + 35;
    int bar_w = 154;
    int center = bar_x + bar_w / 2;
    int fill = (amount_mdB * (bar_w / 2)) / 12000;
    unsigned int bar_color = selected ? t->selected_text : t->text;
    unsigned int fill_color = selected ? t->selected_icon_bg : color_alpha(t->accent, 220);

    if (fill > bar_w / 2) fill = bar_w / 2;
    if (fill < -(bar_w / 2)) fill = -(bar_w / 2);

    eq_ui_draw_row(visible_index, row_index, selected, icon, label, description, value, kind, bounds);
    vita2d_draw_rectangle(bar_x, bar_y, bar_w, 4, selected ? color_alpha(t->selected_text, 72) : color_alpha(t->text, 58));
    vita2d_draw_rectangle(center - 1, bar_y - 6, 2, 16, bar_color);
    if (fill >= 0) {
        vita2d_draw_rectangle(center, bar_y - 2, fill, 8, fill_color);
    } else {
        vita2d_draw_rectangle(center + fill, bar_y - 2, -fill, 8, fill_color);
    }
}
