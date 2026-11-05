/*
 * DisplaceBox - C/raylib port
 * Shared application definitions.
 */
#ifndef DISPLACEBOX_APP_H
#define DISPLACEBOX_APP_H

#include "raylib.h"

#include <stdbool.h>
#include <stddef.h>
#include <limits.h>

#define APP_NAME     "DisplaceBox"
#define APP_VERSION  "2.2"
#define APP_BUILD    "[260110d]"
#define DGCVER       "DisplaceGUI_2.2"

/* Logical layout is designed in a 580x680 space. The window and everything
 * drawn inside it are scaled up by UI_SCALE (textures/fonts are scaled by a
 * 2D camera, so the result stays crisp). */
#define UI_SCALE     1.20f
#define WINDOW_W     580
#define WINDOW_H     680
#define WINDOW_PIXEL_W ((int)(WINDOW_W * UI_SCALE))
#define WINDOW_PIXEL_H ((int)(WINDOW_H * UI_SCALE))

#define REPO_URL     "https://github.com/stakanyash/displacebin_gui_converter"
#define RELEASES_URL "https://github.com/stakanyash/displacebin_gui_converter/releases"
#define ISSUES_URL   "https://github.com/stakanyash/displacebin_gui_converter/issues/new"
#define DISCORD_URL  "https://discord.com/invite/Cd5GanuYud"
#define TELEGRAM_URL "https://t.me/stakanyasher"
#define YOUTUBE_URL  "https://www.youtube.com/@stakanyash"

#ifndef DB_PATH_MAX
#define DB_PATH_MAX 4096
#endif

typedef enum {
    LANG_EN = 0,
    LANG_RU,
    LANG_UK,
    LANG_BE,
    LANG_PL,
    LANG_COUNT
} LangId;

typedef struct {
    Color bg;
    Color surface;
    Color topbar;
    Color text;
    Color textDim;
    Color accent;
    Color link;
    Color border;
    Color fieldBg;
    Color buttonBg;
    Color buttonHover;
    Color buttonPress;
    Color version;
    Color overlay;
    Color greenBg;
    Color redBg;
    Color orangeBg;
    Color panel;
    Color panelBorder;
    Color selection;
} Theme;

const Theme *theme_dark(void);
const Theme *theme_light(void);

#endif /* DISPLACEBOX_APP_H */
