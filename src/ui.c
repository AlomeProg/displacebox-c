#include "ui.h"
#include "platform.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Texture2D render_icon_texture(Ui *ui, IconKind icon);
static Texture2D render_radio_texture(bool selected);

#define SDF_FONT_BASE_SIZE 24

/* Same shader as raylib's examples/text/text_font_sdf.c (glsl330). */
static const char *SDF_FRAGMENT_SHADER =
    "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "out vec4 finalColor;\n"
    "void main()\n"
    "{\n"
    "    float distanceFromOutline = texture(texture0, fragTexCoord).a - 0.5;\n"
    "    float distanceChangePerFragment = length(vec2(dFdx(distanceFromOutline), dFdy(distanceFromOutline)));\n"
    "    float alpha = smoothstep(-distanceChangePerFragment, distanceChangePerFragment, distanceFromOutline);\n"
    "    finalColor = vec4(fragColor.rgb, fragColor.a*alpha);\n"
    "}\n";

/* ------------------------------------------------------------------------- */
/* Themes                                                                    */
/* ------------------------------------------------------------------------- */

static const Theme kDarkTheme = {
    .bg = { 11, 13, 16, 255 },
    .surface = { 21, 24, 28, 255 },
    .topbar = { 16, 19, 22, 255 },
    .text = { 255, 255, 255, 255 },
    .textDim = { 154, 164, 175, 255 },
    .accent = { 158, 202, 255, 255 },
    .link = { 100, 181, 246, 255 },
    .border = { 70, 103, 143, 255 },
    .fieldBg = { 13, 16, 20, 255 },
    .buttonBg = { 30, 34, 40, 255 },
    .buttonHover = { 42, 48, 56, 255 },
    .buttonPress = { 22, 25, 30, 255 },
    .version = { 138, 143, 152, 255 },
    .overlay = { 0, 0, 0, 150 },
    .greenBg = { 27, 94, 32, 255 },
    .redBg = { 198, 40, 40, 255 },
    .orangeBg = { 230, 81, 0, 255 },
    .panel = { 27, 31, 36, 255 },
    .panelBorder = { 46, 53, 61, 255 },
    .selection = { 158, 202, 255, 38 },
};

static const Theme kLightTheme = {
    .bg = { 255, 255, 255, 255 },
    .surface = { 247, 248, 250, 255 },
    .topbar = { 242, 243, 245, 255 },
    .text = { 0, 0, 0, 255 },
    .textDim = { 95, 99, 104, 255 },
    .accent = { 0, 0, 0, 255 },
    .link = { 25, 118, 210, 255 },
    .border = { 0, 0, 0, 255 },
    .fieldBg = { 255, 255, 255, 255 },
    .buttonBg = { 240, 240, 240, 255 },
    .buttonHover = { 228, 230, 232, 255 },
    .buttonPress = { 214, 217, 220, 255 },
    .version = { 0, 0, 0, 255 },
    .overlay = { 0, 0, 0, 105 },
    .greenBg = { 27, 94, 32, 255 },
    .redBg = { 198, 40, 40, 255 },
    .orangeBg = { 230, 81, 0, 255 },
    .panel = { 255, 255, 255, 255 },
    .panelBorder = { 0, 0, 0, 255 },
    .selection = { 0, 0, 0, 22 },
};

const Theme *theme_dark(void) { return &kDarkTheme; }
const Theme *theme_light(void) { return &kLightTheme; }

/* ------------------------------------------------------------------------- */
/* UTF-8 helpers                                                             */
/* ------------------------------------------------------------------------- */

int ui_utf8_len(unsigned char c)
{
    if ((c & 0x80) == 0) return 1;
    if ((c & 0xE0) == 0xC0) return 2;
    if ((c & 0xF0) == 0xE0) return 3;
    if ((c & 0xF8) == 0xF0) return 4;
    return 1;
}

const char *ui_utf8_next(const char *s)
{
    if (!s || !*s) return s;
    int len = ui_utf8_len((unsigned char)*s);
    for (int i = 0; i < len && s[i]; i++) s++;
    return s;
}

const char *ui_utf8_prev(const char *begin, const char *s)
{
    if (!s || s <= begin) return begin;
    s--;
    while (s > begin && ((unsigned char)*s & 0xC0) == 0x80) s--;
    return s;
}

/* ------------------------------------------------------------------------- */
/* Fonts / assets                                                            */
/* ------------------------------------------------------------------------- */

static int *build_codepoints(int *outCount)
{
    static int codepoints[1200];
    int n = 0;

    for (int c = 32; c <= 126; c++) codepoints[n++] = c;
    for (int c = 160; c <= 255; c++) codepoints[n++] = c;
    for (int c = 0x100; c <= 0x17F; c++) codepoints[n++] = c;
    for (int c = 0x400; c <= 0x45F; c++) codepoints[n++] = c;
    for (int c = 0x490; c <= 0x4FF; c++) codepoints[n++] = c;
    for (int c = 0x2010; c <= 0x2027; c++) codepoints[n++] = c;

    static const int extra[] = {
        0x2030, 0x2039, 0x203A, 0x20AC, 0x2116, 0x2122,
        0x2190, 0x2191, 0x2192, 0x2193, 0x2212, 0x2264, 0x2265
    };
    for (size_t i = 0; i < sizeof(extra) / sizeof(extra[0]); i++) codepoints[n++] = extra[i];

    *outCount = n;
    return codepoints;
}

static unsigned char *read_asset_file(const char *fileName, int *outSize,
                                      char *path, size_t pathSize)
{
    *outSize = 0;
    if (!plat_asset_path(fileName, path, pathSize))
    {
        log_error("Asset not found: %s", fileName);
        return NULL;
    }

    long long length = plat_file_size(path);
    if (length <= 0 || length > INT_MAX)
    {
        log_error("Invalid asset size: %s", path);
        return NULL;
    }

    FILE *file = plat_fopen(path, "rb");
    if (!file)
    {
        log_error("Unable to open asset: %s", path);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)length);
    if (!data)
    {
        fclose(file);
        return NULL;
    }

    size_t read = fread(data, 1, (size_t)length, file);
    fclose(file);
    if (read != (size_t)length)
    {
        free(data);
        return NULL;
    }

    *outSize = (int)length;
    return data;
}

static Font load_font_regular(const char *fileName, int *codepoints, int count)
{
    char path[DB_PATH_MAX];
    int dataSize = 0;
    unsigned char *data = read_asset_file(fileName, &dataSize, path, sizeof(path));
    if (!data)
    {
        log_error("Font not found: %s (falling back to raylib default font)", fileName);
        return GetFontDefault();
    }

    /* Load from memory: raylib's file loaders use narrow fopen() on Windows,
     * which cannot open assets under a Unicode path (e.g. C:\Users\Иван). */
    Font font = LoadFontFromMemory(".ttf", data, dataSize, 32, codepoints, count);
    free(data);

    if (font.texture.id == 0)
    {
        log_error("Unable to load font: %s", path);
        return GetFontDefault();
    }
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    return font;
}

static Font load_font_sdf(const char *fileName, int *codepoints, int count)
{
    Font font = { 0 };
    char path[DB_PATH_MAX];
    int dataSize = 0;
    unsigned char *data = read_asset_file(fileName, &dataSize, path, sizeof(path));
    if (!data) return font;

    int glyphCount = 0;
    GlyphInfo *glyphs = LoadFontData(data, dataSize, SDF_FONT_BASE_SIZE, codepoints, count,
                                     FONT_SDF, &glyphCount);
    free(data);

    if (!glyphs || glyphCount <= 0)
    {
        if (glyphs) UnloadFontData(glyphs, glyphCount);
        log_error("Unable to generate SDF font data: %s", path);
        return font;
    }

    font.baseSize = SDF_FONT_BASE_SIZE;
    font.glyphCount = glyphCount;
    font.glyphs = glyphs;

    Image atlas = GenImageFontAtlas(glyphs, &font.recs, glyphCount, SDF_FONT_BASE_SIZE, 0, 1);
    if (!atlas.data)
    {
        UnloadFontData(glyphs, glyphCount);
        log_error("Unable to generate SDF font atlas: %s", path);
        memset(&font, 0, sizeof(font));
        return font;
    }

    font.texture = LoadTextureFromImage(atlas);
    UnloadImage(atlas);

    if (font.texture.id == 0)
    {
        UnloadFontData(glyphs, glyphCount);
        free(font.recs);
        log_error("Unable to upload SDF font atlas: %s", path);
        memset(&font, 0, sizeof(font));
        return font;
    }

    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    return font;
}

static Texture2D load_texture_asset(const char *fileName)
{
    Texture2D empty = { 0 };
    char path[DB_PATH_MAX];
    int dataSize = 0;
    unsigned char *data = read_asset_file(fileName, &dataSize, path, sizeof(path));
    if (!data) return empty;

    Image image = LoadImageFromMemory(".png", data, dataSize);
    free(data);
    if (!image.data)
    {
        log_error("Unable to decode image: %s", path);
        return empty;
    }

    Texture2D texture = LoadTextureFromImage(image);
    UnloadImage(image);
    if (texture.id == 0)
    {
        log_error("Unable to upload texture: %s", path);
        return empty;
    }
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

bool ui_init(Ui *ui, bool dark)
{
    memset(ui, 0, sizeof(*ui));
    ui->dark = dark;
    ui->theme = dark ? kDarkTheme : kLightTheme;

    int codepointCount = 0;
    int *codepoints = build_codepoints(&codepointCount);

    /* A regular font is still needed for raygui (text input), because the
     * SDF shader must not be active while raygui draws its own shapes. */
    ui->guiFont = load_font_regular("DejaVuSans.ttf", codepoints, codepointCount);

    ui->sdfShader = LoadShaderFromMemory(NULL, SDF_FRAGMENT_SHADER);
    if (ui->sdfShader.id != 0)
    {
        ui->regular = load_font_sdf("DejaVuSans.ttf", codepoints, codepointCount);
        ui->bold = load_font_sdf("DejaVuSans-Bold.ttf", codepoints, codepointCount);
        ui->sdfEnabled = (ui->regular.texture.id != 0 && ui->bold.texture.id != 0);
    }

    if (!ui->sdfEnabled)
    {
        if (ui->regular.texture.id) UnloadFont(ui->regular);
        if (ui->bold.texture.id) UnloadFont(ui->bold);
        memset(&ui->regular, 0, sizeof(ui->regular));
        memset(&ui->bold, 0, sizeof(ui->bold));

        ui->regular = load_font_regular("DejaVuSans.ttf", codepoints, codepointCount);
        ui->bold = load_font_regular("DejaVuSans-Bold.ttf", codepoints, codepointCount);
    }

    ui->logoTex[0] = load_texture_asset("logo.png");
    ui->logoTex[1] = load_texture_asset("logo_white.png");
    ui->iconTex[0] = load_texture_asset("icon.png");
    ui->iconTex[1] = load_texture_asset("icon_black.png");
    ui->socialTex[0] = load_texture_asset("git.png");
    ui->socialTex[1] = load_texture_asset("dis.png");
    ui->socialTex[2] = load_texture_asset("tg.png");
    ui->socialTex[3] = load_texture_asset("yt.png");
    ui->windowIcon = load_texture_asset("icon.png");

    if (ui->windowIcon.id != 0)
    {
        Image iconImage = LoadImageFromTexture(ui->windowIcon);
        SetWindowIcon(iconImage);
        UnloadImage(iconImage);
    }

    ui->radioTex[0] = render_radio_texture(false);
    ui->radioTex[1] = render_radio_texture(true);

    for (int i = 0; i < UI_ICON_COUNT; i++)
        ui->glyphTex[i] = render_icon_texture(ui, (IconKind)i);

    ui_apply_raygui(ui);
    ui->inputEnabled = true;
    ui->ready = true;
    return true;
}

void ui_shutdown(Ui *ui)
{
    for (int i = 0; i < 2; i++)
    {
        if (ui->logoTex[i].id) UnloadTexture(ui->logoTex[i]);
        if (ui->iconTex[i].id) UnloadTexture(ui->iconTex[i]);
    }
    for (int i = 0; i < 4; i++)
        if (ui->socialTex[i].id) UnloadTexture(ui->socialTex[i]);
    if (ui->guiFont.texture.id && ui->guiFont.texture.id != GetFontDefault().texture.id)
        UnloadFont(ui->guiFont);
    if (ui->sdfShader.id) UnloadShader(ui->sdfShader);
    for (int i = 0; i < 2; i++)
        if (ui->radioTex[i].id) UnloadTexture(ui->radioTex[i]);
    for (int i = 0; i < UI_ICON_COUNT; i++)
        if (ui->glyphTex[i].id) UnloadTexture(ui->glyphTex[i]);
    if (ui->windowIcon.id) UnloadTexture(ui->windowIcon);
    if (ui->regular.texture.id && ui->regular.texture.id != GetFontDefault().texture.id)
        UnloadFont(ui->regular);
    if (ui->bold.texture.id && ui->bold.texture.id != GetFontDefault().texture.id)
        UnloadFont(ui->bold);
}

void ui_set_theme(Ui *ui, bool dark)
{
    ui->dark = dark;
    ui->theme = dark ? kDarkTheme : kLightTheme;
    ui_apply_raygui(ui);
}

void ui_set_input_enabled(Ui *ui, bool enabled)
{
    ui->inputEnabled = enabled;
}

static void raygui_color(int control, int property, Color color)
{
    GuiSetStyle(control, property, ColorToInt(color));
}

void ui_apply_raygui(Ui *ui)
{
    Theme *t = &ui->theme;
    GuiSetFont(ui->guiFont.texture.id ? ui->guiFont : GetFontDefault());

    GuiSetStyle(DEFAULT, TEXT_SIZE, 14);
    GuiSetStyle(DEFAULT, TEXT_SPACING, 0);
    GuiSetStyle(DEFAULT, TEXT_LINE_SPACING, 2);
    GuiSetStyle(DEFAULT, BACKGROUND_COLOR, ColorToInt(t->panel));
    GuiSetStyle(DEFAULT, LINE_COLOR, ColorToInt(t->border));
    raygui_color(DEFAULT, TEXT_COLOR_NORMAL, t->text);
    raygui_color(DEFAULT, TEXT_COLOR_FOCUSED, t->text);
    raygui_color(DEFAULT, TEXT_COLOR_PRESSED, t->text);
    raygui_color(DEFAULT, TEXT_COLOR_DISABLED, t->textDim);

    GuiSetStyle(DROPDOWNBOX, BORDER_WIDTH, 1);
    raygui_color(DROPDOWNBOX, BORDER_COLOR_NORMAL, t->border);
    raygui_color(DROPDOWNBOX, BASE_COLOR_NORMAL, t->fieldBg);
    raygui_color(DROPDOWNBOX, TEXT_COLOR_NORMAL, t->text);
    raygui_color(DROPDOWNBOX, BORDER_COLOR_FOCUSED, t->accent);
    raygui_color(DROPDOWNBOX, BASE_COLOR_FOCUSED, t->fieldBg);
    raygui_color(DROPDOWNBOX, TEXT_COLOR_FOCUSED, t->text);
    raygui_color(DROPDOWNBOX, BORDER_COLOR_PRESSED, t->accent);
    raygui_color(DROPDOWNBOX, BASE_COLOR_PRESSED, t->fieldBg);
    raygui_color(DROPDOWNBOX, TEXT_COLOR_PRESSED, t->text);
    raygui_color(DROPDOWNBOX, BORDER_COLOR_DISABLED, t->border);
    raygui_color(DROPDOWNBOX, BASE_COLOR_DISABLED, t->fieldBg);
    raygui_color(DROPDOWNBOX, TEXT_COLOR_DISABLED, t->textDim);
    GuiSetStyle(DROPDOWNBOX, TEXT_PADDING, 10);
    GuiSetStyle(DROPDOWNBOX, TEXT_ALIGNMENT, 0);
    GuiSetStyle(DROPDOWNBOX, ARROW_PADDING, 12);
    GuiSetStyle(DROPDOWNBOX, DROPDOWN_ITEMS_SPACING, 2);
    GuiSetStyle(DROPDOWNBOX, DROPDOWN_MAX_HEIGHT, 260);

    GuiSetStyle(TEXTBOX, BORDER_WIDTH, 1);
    raygui_color(TEXTBOX, BORDER_COLOR_NORMAL, t->border);
    raygui_color(TEXTBOX, BASE_COLOR_NORMAL, t->fieldBg);
    raygui_color(TEXTBOX, TEXT_COLOR_NORMAL, t->text);
    raygui_color(TEXTBOX, BORDER_COLOR_FOCUSED, t->accent);
    raygui_color(TEXTBOX, BASE_COLOR_FOCUSED, t->fieldBg);
    raygui_color(TEXTBOX, TEXT_COLOR_FOCUSED, t->text);
    raygui_color(TEXTBOX, BORDER_COLOR_PRESSED, t->accent);
    raygui_color(TEXTBOX, BASE_COLOR_PRESSED, t->fieldBg);
    raygui_color(TEXTBOX, TEXT_COLOR_PRESSED, t->text);
    GuiSetStyle(TEXTBOX, TEXT_PADDING, 8);
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT_VERTICAL, 1);

    GuiSetStyle(LISTVIEW, BORDER_WIDTH, 1);
    raygui_color(LISTVIEW, BORDER_COLOR_NORMAL, t->border);
    raygui_color(LISTVIEW, BASE_COLOR_NORMAL, t->fieldBg);
    raygui_color(LISTVIEW, TEXT_COLOR_NORMAL, t->text);
    raygui_color(LISTVIEW, BORDER_COLOR_FOCUSED, t->accent);
    raygui_color(LISTVIEW, BASE_COLOR_FOCUSED, t->selection);
    raygui_color(LISTVIEW, TEXT_COLOR_FOCUSED, t->text);
    raygui_color(LISTVIEW, BORDER_COLOR_PRESSED, t->accent);
    raygui_color(LISTVIEW, BASE_COLOR_PRESSED, t->selection);
    raygui_color(LISTVIEW, TEXT_COLOR_PRESSED, t->text);
    GuiSetStyle(LISTVIEW, TEXT_PADDING, 8);
}

/* ------------------------------------------------------------------------- */
/* Text                                                                      */
/* ------------------------------------------------------------------------- */

float ui_measure(Ui *ui, const char *text, float size, bool bold)
{
    if (!text || !text[0]) return 0.0f;
    Font font = bold ? ui->bold : ui->regular;
    if (!font.texture.id) font = GetFontDefault();
    Vector2 measured = MeasureTextEx(font, text, size, size * 0.02f);
    return measured.x;
}

float ui_line_height(float size)
{
    return size * 1.45f;
}

static bool text_uses_sdf(Ui *ui, Font font)
{
    return ui->sdfEnabled && font.texture.id != 0 &&
           font.texture.id != GetFontDefault().texture.id;
}

static void text_shader_begin(Ui *ui, Font font)
{
    if (text_uses_sdf(ui, font)) BeginShaderMode(ui->sdfShader);
}

static void text_shader_end(Ui *ui, Font font)
{
    if (text_uses_sdf(ui, font)) EndShaderMode();
}

void ui_draw_text(Ui *ui, const char *text, float x, float y, float size, Color color, bool bold)
{
    if (!text || !text[0]) return;
    Font font = bold ? ui->bold : ui->regular;
    if (!font.texture.id) font = GetFontDefault();
    text_shader_begin(ui, font);
    DrawTextEx(font, text, (Vector2){ x, y }, size, size * 0.02f, color);
    text_shader_end(ui, font);
}

void ui_draw_text_center(Ui *ui, const char *text, Rectangle bounds, float size, Color color, bool bold)
{
    if (!text || !text[0]) return;
    Font font = bold ? ui->bold : ui->regular;
    if (!font.texture.id) font = GetFontDefault();
    Vector2 measured = MeasureTextEx(font, text, size, size * 0.02f);
    float x = bounds.x + (bounds.width - measured.x) * 0.5f;
    float y = bounds.y + (bounds.height - measured.y) * 0.5f;
    text_shader_begin(ui, font);
    DrawTextEx(font, text, (Vector2){ x, y }, size, size * 0.02f, color);
    text_shader_end(ui, font);
}

void ui_draw_text_right(Ui *ui, const char *text, Rectangle bounds, float size, Color color, bool bold)
{
    if (!text || !text[0]) return;
    Font font = bold ? ui->bold : ui->regular;
    if (!font.texture.id) font = GetFontDefault();
    Vector2 measured = MeasureTextEx(font, text, size, size * 0.02f);
    float x = bounds.x + bounds.width - measured.x;
    float y = bounds.y + (bounds.height - measured.y) * 0.5f;
    text_shader_begin(ui, font);
    DrawTextEx(font, text, (Vector2){ x, y }, size, size * 0.02f, color);
    text_shader_end(ui, font);
}

void ui_scissor(Rectangle r)
{
    int x = (int)floorf(r.x * UI_SCALE);
    int y = (int)floorf(r.y * UI_SCALE);
    int w = (int)ceilf(r.width * UI_SCALE);
    int h = (int)ceilf(r.height * UI_SCALE);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    BeginScissorMode(x, y, w, h);
}

static void begin_scissor(Rectangle r)
{
    ui_scissor(r);
}

void ui_draw_text_clip(Ui *ui, const char *text, Rectangle bounds, float size, Color color, bool bold)
{
    if (!text || !text[0]) return;
    begin_scissor(bounds);
    Font font = bold ? ui->bold : ui->regular;
    if (!font.texture.id) font = GetFontDefault();
    Vector2 measured = MeasureTextEx(font, text, size, size * 0.02f);
    float y = bounds.y + (bounds.height - measured.y) * 0.5f;
    text_shader_begin(ui, font);
    DrawTextEx(font, text, (Vector2){ bounds.x, y }, size, size * 0.02f, color);
    text_shader_end(ui, font);
    EndScissorMode();
}

void ui_draw_text_clip_left(Ui *ui, const char *text, Rectangle bounds, float size, Color color, bool bold)
{
    if (!text || !text[0]) return;

    float ellipsisWidth = ui_measure(ui, "…", size, bold);
    const char *start = text;
    float available = bounds.width - ellipsisWidth - 2.0f;
    if (available < 0)
    {
        ui_draw_text_clip(ui, text, bounds, size, color, bold);
        return;
    }

    while (*start && ui_measure(ui, start, size, bold) > available)
        start = ui_utf8_next(start);

    Font font = bold ? ui->bold : ui->regular;
    if (!font.texture.id) font = GetFontDefault();
    Vector2 measured = MeasureTextEx(font, start, size, size * 0.02f);
    float y = bounds.y + (bounds.height - measured.y) * 0.5f;

    begin_scissor(bounds);
    text_shader_begin(ui, font);
    if (start != text)
        DrawTextEx(font, "…", (Vector2){ bounds.x, y }, size, size * 0.02f, color);
    DrawTextEx(font, start, (Vector2){ bounds.x + (start != text ? ellipsisWidth + 2.0f : 0.0f), y },
               size, size * 0.02f, color);
    text_shader_end(ui, font);
    EndScissorMode();
}

int ui_wrap(Ui *ui, const char *text, float size, float maxWidth,
            char lines[][UI_LINE_LEN], int maxLines)
{
    int count = 0;
    if (!text || maxWidth <= 1 || maxLines <= 0) return 0;

    const char *p = text;
    while (*p && count < maxLines)
    {
        const char *newline = strchr(p, '\n');
        const char *paragraphEnd = newline ? newline : p + strlen(p);

        if (paragraphEnd == p)
        {
            lines[count][0] = '\0';
            count++;
            p = newline ? newline + 1 : paragraphEnd;
            continue;
        }

        char line[UI_LINE_LEN] = { 0 };
        const char *word = p;
        while (word < paragraphEnd && count < maxLines)
        {
            while (word < paragraphEnd && *word == ' ') word++;
            if (word >= paragraphEnd) break;

            const char *wordEnd = word;
            while (wordEnd < paragraphEnd && *wordEnd != ' ') wordEnd++;

            char wordBuf[UI_LINE_LEN];
            size_t wordLen = (size_t)(wordEnd - word);
            if (wordLen >= sizeof(wordBuf)) wordLen = sizeof(wordBuf) - 1;
            memcpy(wordBuf, word, wordLen);
            wordBuf[wordLen] = '\0';

            size_t lineLen = strlen(line);
            size_t wordLen2 = strlen(wordBuf);
            if (lineLen > 0 && lineLen + 1 + wordLen2 >= sizeof(line))
            {
                snprintf(lines[count], UI_LINE_LEN, "%s", line);
                count++;
                line[0] = '\0';
                lineLen = 0;
                if (count >= maxLines) break;
            }

            char trial[UI_LINE_LEN];
            if (lineLen > 0)
            {
                memcpy(trial, line, lineLen);
                trial[lineLen] = ' ';
                memcpy(trial + lineLen + 1, wordBuf, wordLen2 + 1);
            }
            else
            {
                memcpy(trial, wordBuf, wordLen2 + 1);
            }

            if (ui_measure(ui, trial, size, false) <= maxWidth)
            {
                memcpy(line, trial, strlen(trial) + 1);
            }
            else
            {
                if (line[0])
                {
                    snprintf(lines[count], UI_LINE_LEN, "%s", line);
                    count++;
                    line[0] = '\0';
                    if (count >= maxLines) break;
                }

                if (ui_measure(ui, wordBuf, size, false) <= maxWidth)
                {
                    snprintf(line, sizeof(line), "%s", wordBuf);
                }
                else
                {
                    const char *q = wordBuf;
                    char chunk[UI_LINE_LEN] = { 0 };
                    while (*q && count < maxLines)
                    {
                        const char *next = ui_utf8_next(q);
                        char piece[8] = { 0 };
                        size_t pieceLen = (size_t)(next - q);
                        if (pieceLen >= sizeof(piece)) pieceLen = sizeof(piece) - 1;
                        memcpy(piece, q, pieceLen);

                        char trial[UI_LINE_LEN * 2];
                        snprintf(trial, sizeof(trial), "%s%s", chunk, piece);
                        if (chunk[0] && ui_measure(ui, trial, size, false) > maxWidth)
                        {
                            snprintf(lines[count], UI_LINE_LEN, "%s", chunk);
                            count++;
                            chunk[0] = '\0';
                            if (count >= maxLines) break;
                        }
                        size_t used = strlen(chunk);
                        if (used + strlen(piece) < sizeof(chunk))
                            strcat(chunk, piece);
                        q = next;
                    }
                    if (chunk[0] && count < maxLines)
                        snprintf(line, sizeof(line), "%s", chunk);
                }
            }

            word = wordEnd;
        }

        if (line[0] && count < maxLines)
        {
            snprintf(lines[count], UI_LINE_LEN, "%s", line);
            count++;
        }

        p = newline ? newline + 1 : paragraphEnd;
    }

    return count;
}

float ui_lines_height(int lineCount, float size)
{
    return (float)lineCount * ui_line_height(size);
}

void ui_draw_lines(Ui *ui, char lines[][UI_LINE_LEN], int lineCount, Rectangle bounds,
                   float size, Color color, float scroll)
{
    float lineHeight = ui_line_height(size);
    begin_scissor(bounds);
    for (int i = 0; i < lineCount; i++)
    {
        float y = bounds.y - scroll + (float)i * lineHeight;
        if (y + lineHeight < bounds.y || y > bounds.y + bounds.height) continue;
        ui_draw_text(ui, lines[i], bounds.x, y, size, color, false);
    }
    EndScissorMode();
}

/* ------------------------------------------------------------------------- */
/* Shapes / widgets                                                          */
/* ------------------------------------------------------------------------- */

void ui_panel(Rectangle bounds, float roundness, Color fill, Color border, float borderWidth)
{
    if (fill.a > 0) DrawRectangleRounded(bounds, roundness, 12, fill);
    if (border.a > 0 && borderWidth > 0)
        DrawRectangleRoundedLinesEx(bounds, roundness, 12, borderWidth, border);
}

static Color blend_over(Color background, Color foreground)
{
    return ColorAlphaBlend(background, foreground, WHITE);
}

/* A ring drawn with a small angular overlap to hide the fan seam. */
static void draw_ring(Vector2 center, float inner, float outer, Color color)
{
    DrawRing(center, inner, outer, -1.0f, 361.0f, 64, color);
}

bool ui_button(Ui *ui, Rectangle bounds, const char *label, bool enabled)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = enabled && ui->inputEnabled && CheckCollisionPointRec(mouse, bounds);
    bool pressed = hovered && IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

    Color fill = ui->theme.buttonBg;
    if (hovered) fill = ui->theme.buttonHover;
    if (pressed) fill = ui->theme.buttonPress;
    if (!enabled) fill = Fade(ui->theme.buttonBg, 0.55f);

    ui_panel(bounds, 0.28f, fill, BLANK, 0);
    Color textColor = enabled ? ui->theme.accent : Fade(ui->theme.textDim, 0.6f);
    ui_draw_text_center(ui, label, bounds, 14.0f, textColor, false);
    return clicked;
}

bool ui_icon_button(Ui *ui, Rectangle bounds, IconKind icon, Color iconColor)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = ui->inputEnabled && CheckCollisionPointRec(mouse, bounds);
    bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

    Color background = ui->theme.bg;
    if (hovered) background = blend_over(ui->theme.bg, Fade(ui->theme.accent, 0.12f));
    if (hovered) ui_panel(bounds, 0.35f, background, BLANK, 0);

    Rectangle iconBounds = {
        bounds.x + (bounds.width - 22.0f) * 0.5f,
        bounds.y + (bounds.height - 22.0f) * 0.5f,
        22.0f, 22.0f
    };
    ui_draw_icon(ui, icon, iconBounds, iconColor, background);
    return clicked;
}

bool ui_radio(Ui *ui, Rectangle bounds, const char *label, bool selected)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = ui->inputEnabled && CheckCollisionPointRec(mouse, bounds);
    bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);

    Vector2 center = { bounds.x + 9.0f, bounds.y + bounds.height * 0.5f };
    Color ringColor = selected ? ui->theme.accent : (hovered ? ui->theme.text : ui->theme.textDim);

    Texture2D radio = ui->radioTex[selected ? 1 : 0];
    if (radio.id != 0)
    {
        Rectangle source = { 0.0f, 0.0f, (float)radio.width, (float)radio.height };
        Rectangle destination = { center.x - 9.0f, center.y - 9.0f, 18.0f, 18.0f };
        DrawTexturePro(radio, source, destination, (Vector2){ 0.0f, 0.0f }, 0.0f, ringColor);
    }
    else
    {
        draw_ring(center, 6.4f, 8.0f, ringColor);
        if (selected) DrawCircleV(center, 4.2f, ringColor);
    }

    ui_draw_text(ui, label, bounds.x + 24.0f,
                 bounds.y + (bounds.height - 15.0f) * 0.5f - 1.0f,
                 15.0f, ui->theme.text, false);
    return clicked;
}

void ui_field(Ui *ui, Rectangle bounds, const char *label, const char *value, bool lightBorder)
{
    (void)lightBorder;
    ui_panel(bounds, 0.16f, ui->theme.fieldBg, ui->theme.border, 1.2f);

    ui_draw_text(ui, label, bounds.x + 12.0f, bounds.y + 6.0f, 11.0f, ui->theme.textDim, false);

    Rectangle valueBounds = {
        bounds.x + 12.0f,
        bounds.y + 20.0f,
        bounds.width - 24.0f,
        bounds.height - 24.0f
    };
    if (value && value[0])
        ui_draw_text_clip_left(ui, value, valueBounds, 13.5f, ui->theme.text, false);
}

void ui_tooltip(Ui *ui, const char *text, Rectangle anchor)
{
    if (!text || !text[0]) return;
    float textWidth = ui_measure(ui, text, 12.0f, false);
    float width = textWidth + 20.0f;
    float height = 26.0f;
    float x = anchor.x + anchor.width * 0.5f - width * 0.5f;
    float y = anchor.y - height - 6.0f;
    if (y < 4.0f) y = anchor.y + anchor.height + 6.0f;
    if (x < 4.0f) x = 4.0f;
    if (x + width > WINDOW_W - 4.0f) x = WINDOW_W - width - 4.0f;

    Rectangle bounds = { x, y, width, height };
    ui_panel(bounds, 0.28f, ui->theme.surface, ui->theme.panelBorder, 1.0f);
    ui_draw_text_center(ui, text, bounds, 12.0f, ui->theme.text, false);
}

void ui_draw_social(Ui *ui, int index, Rectangle bounds, Color color)
{
    if (index < 0 || index >= 4) return;
    Texture2D texture = ui->socialTex[index];
    if (texture.id == 0) return;

    Rectangle source = { 0.0f, 0.0f, (float)texture.width, (float)texture.height };
    DrawTexturePro(texture, source, bounds, (Vector2){ 0.0f, 0.0f }, 0.0f, color);
}

void ui_draw_logo(Ui *ui, Rectangle bounds)
{
    Texture2D texture = ui->logoTex[ui->dark ? 0 : 1];
    if (texture.id == 0) return;
    Rectangle source = { 0.0f, 0.0f, (float)texture.width, (float)texture.height };
    DrawTexturePro(texture, source, bounds, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
}

void ui_draw_app_icon(Ui *ui, Rectangle bounds)
{
    Texture2D texture = ui->iconTex[ui->dark ? 0 : 1];
    if (texture.id == 0) return;
    Rectangle source = { 0.0f, 0.0f, (float)texture.width, (float)texture.height };
    DrawTexturePro(texture, source, bounds, (Vector2){ 0.0f, 0.0f }, 0.0f, WHITE);
}

/* ------------------------------------------------------------------------- */
/* Icons                                                                     */
/* ------------------------------------------------------------------------- */

static float angle_norm(float angle)
{
    while (angle < 0.0f) angle += 2.0f * PI;
    while (angle >= 2.0f * PI) angle -= 2.0f * PI;
    return angle;
}

static void draw_crescent(Vector2 c1, float r1, Vector2 c2, float r2, Color color)
{
    float dx = c2.x - c1.x;
    float dy = c2.y - c1.y;
    float d = sqrtf(dx * dx + dy * dy);

    if (d < 1e-4f || d >= r1 + r2 || d <= fabsf(r1 - r2))
    {
        DrawCircleV(c1, r1, color);
        return;
    }

    float a = (r1 * r1 - r2 * r2 + d * d) / (2.0f * d);
    float h = sqrtf(fmaxf(0.0f, r1 * r1 - a * a));
    float ux = dx / d;
    float uy = dy / d;

    Vector2 base = { c1.x + ux * a, c1.y + uy * a };
    Vector2 p1 = { base.x - uy * h, base.y + ux * h };
    Vector2 p2 = { base.x + uy * h, base.y - ux * h };

    float a1 = atan2f(p1.y - c1.y, p1.x - c1.x);
    float a2 = atan2f(p2.y - c1.y, p2.x - c1.x);
    float outerDelta = angle_norm(a2 - a1);
    float outerMid = a1 + outerDelta * 0.5f;
    Vector2 outerMidPoint = { c1.x + cosf(outerMid) * r1, c1.y + sinf(outerMid) * r1 };
    bool outerCcw = hypotf(outerMidPoint.x - c2.x, outerMidPoint.y - c2.y) > r2;
    float outerStep = outerCcw ? outerDelta : -angle_norm(a1 - a2);

    float b1 = atan2f(p1.y - c2.y, p1.x - c2.x);
    float b2 = atan2f(p2.y - c2.y, p2.x - c2.x);
    float innerDelta = angle_norm(b2 - b1);
    float innerMid = b1 + innerDelta * 0.5f;
    Vector2 innerMidPoint = { c2.x + cosf(innerMid) * r2, c2.y + sinf(innerMid) * r2 };
    bool innerCcw = hypotf(innerMidPoint.x - c1.x, innerMidPoint.y - c1.y) < r1;
    float innerStep = innerCcw ? innerDelta : -angle_norm(b1 - b2);

    const int segments = 32;
    Vector2 outer[33];
    Vector2 inner[33];

    for (int i = 0; i <= segments; i++)
    {
        float t = (float)i / (float)segments;
        float ao = a1 + outerStep * t;
        float ai = b1 + innerStep * t;
        outer[i] = (Vector2){ c1.x + cosf(ao) * r1, c1.y + sinf(ao) * r1 };
        inner[i] = (Vector2){ c2.x + cosf(ai) * r2, c2.y + sinf(ai) * r2 };
    }

    for (int i = 0; i < segments; i++)
    {
        DrawTriangle(outer[i], inner[i + 1], outer[i + 1], color);
        DrawTriangle(outer[i], inner[i], inner[i + 1], color);
    }
}

static void draw_icon_shape(Ui *ui, IconKind icon, Rectangle bounds, Color color, Color background)
{
    float unit = fminf(bounds.width, bounds.height) / 24.0f;
    float cx = bounds.x + bounds.width * 0.5f;
    float cy = bounds.y + bounds.height * 0.5f;
    float thickness = fmaxf(1.4f, 1.9f * unit);

#define V(x, y) ((Vector2){ cx + (x) * unit, cy + (y) * unit })
#define LINE(x1, y1, x2, y2) DrawLineEx(V(x1, y1), V(x2, y2), thickness, color)

    switch (icon)
    {
        case UI_ICON_HELP:
            draw_ring((Vector2){ cx, cy }, 8.0f * unit, 9.3f * unit, color);
            DrawRing((Vector2){ cx, cy - 2.0f * unit }, 2.2f * unit, 3.7f * unit,
                     150.0f, 390.0f, 32, color);
            {
                float angle = 30.0f * (PI / 180.0f);
                Vector2 end = { cx + cosf(angle) * 2.95f * unit,
                                cy - 2.0f * unit + sinf(angle) * 2.95f * unit };
                DrawLineEx(end, (Vector2){ cx, cy + 2.0f * unit }, thickness, color);
                DrawCircleV((Vector2){ cx, cy + 4.6f * unit }, 1.15f * unit, color);
            }
            break;

        case UI_ICON_GLOBE:
            draw_ring((Vector2){ cx, cy }, 8.0f * unit, 9.3f * unit, color);
            DrawEllipseLinesEx((Vector2){ cx, cy }, 9.1f * unit, 4.0f * unit, thickness, color);
            DrawEllipseLinesEx((Vector2){ cx, cy }, 4.0f * unit, 9.1f * unit, thickness, color);
            LINE(-8.5f, 0.0f, 8.5f, 0.0f);
            break;

        case UI_ICON_MOON:
            draw_crescent((Vector2){ cx, cy }, 8.3f * unit,
                          (Vector2){ cx + 4.6f * unit, cy - 3.6f * unit },
                          7.6f * unit, color);
            break;

        case UI_ICON_SUN:
            DrawCircleV((Vector2){ cx, cy }, 4.6f * unit, color);
            for (int i = 0; i < 8; i++)
            {
                float angle = (float)i * (PI / 4.0f);
                Vector2 a = { cx + cosf(angle) * 7.0f * unit, cy + sinf(angle) * 7.0f * unit };
                Vector2 b = { cx + cosf(angle) * 9.3f * unit, cy + sinf(angle) * 9.3f * unit };
                DrawLineEx(a, b, thickness, color);
            }
            break;

        case UI_ICON_SWAP:
            LINE(-8.0f, -3.5f, 7.0f, -3.5f);
            LINE(7.0f, -3.5f, 3.5f, -6.5f);
            LINE(7.0f, -3.5f, 3.5f, -0.5f);
            LINE(8.0f, 3.5f, -7.0f, 3.5f);
            LINE(-7.0f, 3.5f, -3.5f, 0.5f);
            LINE(-7.0f, 3.5f, -3.5f, 6.5f);
            break;

        case UI_ICON_MIN:
            LINE(-6.0f, 0.0f, 6.0f, 0.0f);
            break;

        case UI_ICON_CLOSE:
            LINE(-5.0f, -5.0f, 5.0f, 5.0f);
            LINE(5.0f, -5.0f, -5.0f, 5.0f);
            break;

        case UI_ICON_CHECK:
            DrawLineEx((Vector2){ cx - 6.0f * unit, cy + 0.5f * unit },
                       (Vector2){ cx - 1.5f * unit, cy + 5.0f * unit }, thickness + 0.6f, color);
            DrawLineEx((Vector2){ cx - 1.5f * unit, cy + 5.0f * unit },
                       (Vector2){ cx + 7.0f * unit, cy - 5.0f * unit }, thickness + 0.6f, color);
            break;

        case UI_ICON_WARNING:
        {
            Vector2 a = { cx, cy - 8.0f * unit };
            Vector2 b = { cx - 9.0f * unit, cy + 7.0f * unit };
            Vector2 c = { cx + 9.0f * unit, cy + 7.0f * unit };
            DrawTriangle(a, c, b, Fade(color, 0.18f));
            DrawLineEx(a, b, thickness, color);
            DrawLineEx(b, c, thickness, color);
            DrawLineEx(c, a, thickness, color);
            DrawLineEx((Vector2){ cx, cy - 2.5f * unit }, (Vector2){ cx, cy + 2.5f * unit }, thickness, color);
            DrawCircleV((Vector2){ cx, cy + 5.2f * unit }, 1.3f * unit, color);
            break;
        }

        case UI_ICON_ERROR:
            draw_ring((Vector2){ cx, cy }, 8.0f * unit, 9.3f * unit, color);
            DrawLineEx((Vector2){ cx, cy - 4.5f * unit }, (Vector2){ cx, cy + 1.8f * unit }, thickness, color);
            DrawCircleV((Vector2){ cx, cy + 4.8f * unit }, 1.3f * unit, color);
            break;

        case UI_ICON_INFO:
            draw_ring((Vector2){ cx, cy }, 8.0f * unit, 9.3f * unit, color);
            DrawCircleV((Vector2){ cx, cy - 4.6f * unit }, 1.3f * unit, color);
            DrawLineEx((Vector2){ cx, cy - 1.5f * unit }, (Vector2){ cx, cy + 5.0f * unit }, thickness, color);
            break;

        case UI_ICON_FOLDER:
        {
            Rectangle tab = { cx - 8.0f * unit, cy - 7.0f * unit, 8.0f * unit, 5.0f * unit };
            Rectangle body = { cx - 8.5f * unit, cy - 4.5f * unit, 17.0f * unit, 12.0f * unit };
            DrawRectangleRounded(tab, 0.35f, 6, color);
            DrawRectangleRounded(body, 0.22f, 6, color);
            break;
        }

        case UI_ICON_FILE:
        {
            float x = cx - 5.5f * unit;
            float y = cy - 8.0f * unit;
            float w = 11.0f * unit;
            float h = 16.0f * unit;
            float fold = 4.0f * unit;
            Vector2 page[5] = {
                { x, y }, { x + w - fold, y }, { x + w, y + fold }, { x + w, y + h }, { x, y + h }
            };
            for (int i = 1; i < 4; i++)
                DrawTriangle(page[0], page[i + 1], page[i], color);
            break;
        }

        case UI_ICON_UP:
            LINE(0.0f, 7.0f, 0.0f, -6.5f);
            LINE(0.0f, -6.5f, -5.0f, -1.5f);
            LINE(0.0f, -6.5f, 5.0f, -1.5f);
            break;

        case UI_ICON_HOME:
            LINE(-8.0f, -1.0f, 0.0f, -7.5f);
            LINE(0.0f, -7.5f, 8.0f, -1.0f);
            LINE(-6.0f, -0.5f, -6.0f, 7.5f);
            LINE(6.0f, -0.5f, 6.0f, 7.5f);
            LINE(-6.0f, 7.5f, 6.0f, 7.5f);
            break;

        case UI_ICON_OPEN:
            LINE(-7.0f, 1.0f, -7.0f, 7.0f);
            LINE(-7.0f, 7.0f, -1.0f, 7.0f);
            LINE(-7.0f, 7.0f, 7.0f, -7.0f);
            LINE(1.0f, -7.0f, 7.0f, -7.0f);
            LINE(7.0f, -7.0f, 7.0f, -1.0f);
            break;

        default:
            break;
    }

#undef LINE
#undef V
}

static Texture2D render_icon_texture(Ui *ui, IconKind icon)
{
    RenderTexture2D target = LoadRenderTexture(128, 128);
    if (target.id == 0) return (Texture2D){ 0 };

    BeginTextureMode(target);
    ClearBackground(BLANK);
    draw_icon_shape(ui, icon, (Rectangle){ 0.0f, 0.0f, 128.0f, 128.0f }, WHITE, BLANK);
    EndTextureMode();

    Texture2D texture = { 0 };
    Image image = LoadImageFromTexture(target.texture);
    if (image.data)
    {
        ImageFlipVertical(&image);
        texture = LoadTextureFromImage(image);
        UnloadImage(image);
    }
    UnloadRenderTexture(target);

    if (texture.id != 0) SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

static Texture2D render_radio_texture(bool selected)
{
    RenderTexture2D target = LoadRenderTexture(128, 128);
    if (target.id == 0) return (Texture2D){ 0 };

    const float scale = 128.0f / 18.0f;
    Vector2 center = { 64.0f, 64.0f };

    BeginTextureMode(target);
    ClearBackground(BLANK);
    DrawRing(center, 6.4f * scale, 8.0f * scale, -1.0f, 361.0f, 64, WHITE);
    if (selected) DrawCircleV(center, 4.2f * scale, WHITE);
    EndTextureMode();

    Texture2D texture = { 0 };
    Image image = LoadImageFromTexture(target.texture);
    if (image.data)
    {
        ImageFlipVertical(&image);
        texture = LoadTextureFromImage(image);
        UnloadImage(image);
    }
    UnloadRenderTexture(target);

    if (texture.id != 0) SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    return texture;
}

void ui_draw_icon(Ui *ui, IconKind icon, Rectangle bounds, Color color, Color background)
{
    if (icon >= 0 && icon < UI_ICON_COUNT && ui->glyphTex[icon].id != 0)
    {
        Texture2D texture = ui->glyphTex[icon];
        Rectangle source = { 0.0f, 0.0f, (float)texture.width, (float)texture.height };
        DrawTexturePro(texture, source, bounds, (Vector2){ 0.0f, 0.0f }, 0.0f, color);
        return;
    }

    draw_icon_shape(ui, icon, bounds, color, background);
}
