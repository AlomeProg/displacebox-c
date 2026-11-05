#ifndef DISPLACEBOX_UI_H
#define DISPLACEBOX_UI_H

#include "app.h"
#include "raylib.h"
#include "raygui.h"

#define UI_MAX_LINES 320
#define UI_LINE_LEN  512

typedef struct {
    Font regular;
    Font bold;
    Theme theme;
    bool dark;
    Texture2D logoTex[2];   /* 0 = dark-theme logo, 1 = light-theme logo */
    Texture2D iconTex[2];
    Texture2D socialTex[4]; /* github, discord, telegram, youtube */
    Texture2D windowIcon;
    bool inputEnabled;
    bool ready;
} Ui;

typedef enum {
    UI_ICON_HELP,
    UI_ICON_GLOBE,
    UI_ICON_MOON,
    UI_ICON_SUN,
    UI_ICON_SWAP,
    UI_ICON_MIN,
    UI_ICON_CLOSE,
    UI_ICON_CHECK,
    UI_ICON_WARNING,
    UI_ICON_ERROR,
    UI_ICON_INFO,
    UI_ICON_FOLDER,
    UI_ICON_FILE,
    UI_ICON_UP,
    UI_ICON_HOME,
    UI_ICON_OPEN
} IconKind;

bool ui_init(Ui *ui, bool dark);
void ui_shutdown(Ui *ui);
void ui_set_theme(Ui *ui, bool dark);
void ui_set_input_enabled(Ui *ui, bool enabled);
void ui_apply_raygui(Ui *ui);

float ui_measure(Ui *ui, const char *text, float size, bool bold);
float ui_line_height(float size);
void ui_draw_text(Ui *ui, const char *text, float x, float y, float size, Color color, bool bold);
void ui_draw_text_center(Ui *ui, const char *text, Rectangle bounds, float size, Color color, bool bold);
void ui_draw_text_right(Ui *ui, const char *text, Rectangle bounds, float size, Color color, bool bold);
void ui_draw_text_clip(Ui *ui, const char *text, Rectangle bounds, float size, Color color, bool bold);
void ui_draw_text_clip_left(Ui *ui, const char *text, Rectangle bounds, float size, Color color, bool bold);

int ui_wrap(Ui *ui, const char *text, float size, float maxWidth,
            char lines[][UI_LINE_LEN], int maxLines);
float ui_lines_height(int lineCount, float size);
void ui_draw_lines(Ui *ui, char lines[][UI_LINE_LEN], int lineCount, Rectangle bounds,
                   float size, Color color, float scroll);

void ui_scissor(Rectangle bounds);
void ui_panel(Rectangle bounds, float roundness, Color fill, Color border, float borderWidth);
bool ui_button(Ui *ui, Rectangle bounds, const char *label, bool enabled);
bool ui_icon_button(Ui *ui, Rectangle bounds, IconKind icon, Color iconColor);
bool ui_radio(Ui *ui, Rectangle bounds, const char *label, bool selected);
void ui_field(Ui *ui, Rectangle bounds, const char *label, const char *value, bool lightBorder);
void ui_tooltip(Ui *ui, const char *text, Rectangle anchor);
void ui_draw_social(Ui *ui, int index, Rectangle bounds, Color color);
void ui_draw_logo(Ui *ui, Rectangle bounds);
void ui_draw_app_icon(Ui *ui, Rectangle bounds);
void ui_draw_icon(Ui *ui, IconKind icon, Rectangle bounds, Color color, Color background);

/* UTF-8 safe helpers (used by the file browser and clipping code). */
int ui_utf8_len(unsigned char c);
const char *ui_utf8_next(const char *s);
const char *ui_utf8_prev(const char *begin, const char *s);

#endif /* DISPLACEBOX_UI_H */
