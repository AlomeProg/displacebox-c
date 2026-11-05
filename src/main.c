#define _GNU_SOURCE
#include "app.h"
#include "converter.h"
#include "file_dialog.h"
#include "i18n.h"
#include "platform.h"
#include "png_io.h"
#include "raygui.h"
#include "raylib.h"
#include "ui.h"
#include "updater.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum { MODE_FORWARD = 0, MODE_REVERSE } AppMode;

typedef enum {
    DLG_NONE = 0,
    DLG_LANGUAGE,
    DLG_HELP,
    DLG_META,
    DLG_VERSION,
    DLG_SUCCESS,
    DLG_ERROR,
    DLG_CONFIRM,
    DLG_UPDATE
} DialogKind;

typedef struct {
    Ui ui;
    AppMode mode;
    LangId lang;
    bool shouldClose;
    bool dark;

    char inputPath[PATH_MAX];
    char jsonPath[PATH_MAX];
    Metadata meta;
    Metadata pendingMeta;
    bool metaLoaded;
    char pendingJson[PATH_MAX];

    int format;     /* -1 = not selected, 0 = RAW, 1 = PNG */
    int sizeIndex;  /* -1 = not selected */
    bool sizeEdit;

    DialogKind dialog;
    char dlgTitle[128];
    char dlgMessage[8192];
    bool dlgHelp;
    float dlgScroll;

    char successOut[PATH_MAX];
    char successMeta[PATH_MAX];

    FileDialog fd;
    FdFilter fdFilter;

    Updater updater;
    bool updateStarted;
    bool updatePending;
    char updateVersion[64];
    char updateBody[8192];

    int hoverId;
    float hoverTimer;
    const char *tooltip;
    Rectangle tooltipAnchor;

    bool dragging;
    bool dragJustEnded;
    bool dragUseGlobal;
    Vector2 dragOffset;
    Vector2 dragStartWindow;
    Vector2 dragStartGlobal;

    char screenshotPath[PATH_MAX];
    int screenshotAtFrame;
    int frame;
} App;

static App g_app;
static char g_dialogLines[UI_MAX_LINES][UI_LINE_LEN];

static const char *tr(StrId id)
{
    return i18n(g_app.lang, id);
}

/* ------------------------------------------------------------------------- */
/* Small utilities                                                           */
/* ------------------------------------------------------------------------- */

static void str_append(char *buffer, size_t size, const char *text)
{
    size_t used = strlen(buffer);
    if (used + 1 >= size) return;
    snprintf(buffer + used, size - used, "%s", text);
}

static void make_output_path(const char *input, const char *extension, char *out, size_t outSize)
{
    char dir[PATH_MAX];
    char stem[512];
    char name[600];
    plat_path_dirname(input, dir, sizeof(dir));
    plat_path_stem(input, stem, sizeof(stem));
    snprintf(name, sizeof(name), "%s%s", stem, extension);
    plat_path_join(out, outSize, dir, name);
}

static void open_dialog(App *app, DialogKind kind, const char *title, const char *message)
{
    app->dialog = kind;
    snprintf(app->dlgTitle, sizeof(app->dlgTitle), "%s", title ? title : "");
    snprintf(app->dlgMessage, sizeof(app->dlgMessage), "%s", message ? message : "");
    app->dlgScroll = 0.0f;
    app->dlgHelp = false;
    app->sizeEdit = false;
}

static void show_error(App *app, const char *message, bool withHelp)
{
    open_dialog(app, DLG_ERROR, tr(STR_ERROR), message);
    app->dlgHelp = withHelp;
    log_error("%s", message);
}

static void show_success(App *app, const char *output, const char *metadata)
{
    snprintf(app->successOut, sizeof(app->successOut), "%s", output ? output : "");
    snprintf(app->successMeta, sizeof(app->successMeta), "%s", metadata ? metadata : "");
    open_dialog(app, DLG_SUCCESS, tr(STR_RESULT), "");
    log_info("Converted file saved to: %s", output ? output : "");
}

static void show_help_dialog(App *app)
{
    const char *text = (app->mode == MODE_REVERSE) ? tr(STR_REVERSE_HELP_TEXT) : tr(STR_HELP_TEXT);
    open_dialog(app, DLG_HELP, tr(STR_HELP), text);
}

static void show_meta_dialog(App *app)
{
    open_dialog(app, DLG_META, tr(STR_METADATAINFO), tr(STR_META_TEXT));
}

static void show_language_dialog(App *app)
{
    open_dialog(app, DLG_LANGUAGE, tr(STR_SEL_LANG), "");
}

static void show_version_dialog(App *app)
{
    open_dialog(app, DLG_VERSION, tr(STR_INFO), "");
}

static void apply_json_selection(App *app, const char *path, const Metadata *meta)
{
    snprintf(app->jsonPath, sizeof(app->jsonPath), "%s", path);
    app->meta = *meta;
    app->metaLoaded = true;
    log_info("Selected JSON metadata: %s", path);
}

static void handle_json_selection(App *app, const char *path)
{
    Metadata meta;
    char missing[64] = { 0 };
    MetaStatus status = metadata_read(path, &meta, missing, sizeof(missing));

    if (status == META_ERR_MISSING)
    {
        log_error("JSON is missing required keys: %s", missing);
        show_error(app, tr(STR_INVALID_JSON_METADATA), false);
        return;
    }
    if (status != META_OK)
    {
        log_error("Unable to read JSON metadata: %s", path);
        show_error(app, tr(STR_INVALID_JSON_METADATA), false);
        return;
    }

    if (!meta.hasDgcver || strncmp(meta.dgcver, "DisplaceGUI_", 12) != 0)
    {
        log_error("Invalid or missing DGCVer field");
        show_error(app, tr(STR_INVALID_JSON_DGCVER), false);
        return;
    }

    int fileMajor = 0, fileMinor = 0;
    if (sscanf(meta.dgcver + 12, "%d.%d", &fileMajor, &fileMinor) != 2)
    {
        log_error("Invalid version format in DGCVer: %s", meta.dgcver);
        show_error(app, tr(STR_INVALID_JSON_VERSION_FORMAT), false);
        return;
    }

    int currentMajor = 0, currentMinor = 0;
    sscanf(APP_VERSION, "%d.%d", &currentMajor, &currentMinor);

    bool older = fileMajor < currentMajor || (fileMajor == currentMajor && fileMinor < currentMinor);
    bool newer = fileMajor > currentMajor || (fileMajor == currentMajor && fileMinor > currentMinor);

    if (older || newer)
    {
        snprintf(app->pendingJson, sizeof(app->pendingJson), "%s", path);
        app->pendingMeta = meta;
        open_dialog(app, DLG_CONFIRM, tr(STR_WARNING),
                    older ? tr(STR_JSON_OLDER) : tr(STR_JSON_NEWER));
        log_info("JSON version %d.%d differs from current %s; asking user", fileMajor, fileMinor,
                 APP_VERSION);
        return;
    }

    apply_json_selection(app, path, &meta);
}

/* ------------------------------------------------------------------------- */
/* Conversions                                                               */
/* ------------------------------------------------------------------------- */

static void forward_convert(App *app)
{
    if (!app->inputPath[0] || app->format < 0 || app->sizeIndex < 0)
    {
        show_error(app, tr(STR_PLSSEL_FILE), false);
        log_warning("File, format or size is not selected");
        return;
    }

    int size = 64 << app->sizeIndex; /* 64, 128, 256, 512, 1024 */
    bool toPng = (app->format == 1);
    char outputPath[PATH_MAX];
    make_output_path(app->inputPath, toPng ? ".png" : ".raw", outputPath, sizeof(outputPath));

    ConvStats stats;
    char metaPath[PATH_MAX] = { 0 };
    char errorText[512] = { 0 };

    ConvStatus status = convert_forward(app->inputPath, outputPath, size, toPng, true, &stats,
                                        metaPath, sizeof(metaPath), errorText, sizeof(errorText));

    if (status == CONV_OK)
    {
        log_info("Min: %.1f, Max: %.1f, Delta: %.1f", stats.min, stats.max, stats.delta);
        show_success(app, outputPath, metaPath);
        return;
    }

    log_error("Conversion failed (%s): %s", conv_status_string(status), errorText);
    if (status == CONV_ERR_INPUT_SIZE)
    {
        show_error(app, tr(STR_STRUCT_ERROR), true);
    }
    else
    {
        char message[1200];
        snprintf(message, sizeof(message), "%s\n\n%s", tr(STR_ZERODIV_ERROR), errorText);
        show_error(app, message, false);
    }
}

static void reverse_convert(App *app)
{
    if (!app->metaLoaded)
    {
        show_error(app, tr(STR_INVALID_JSON_METADATA), false);
        return;
    }
    if (!app->inputPath[0])
    {
        show_error(app, tr(STR_PLSSEL_FILE), false);
        return;
    }

    const char *ext = plat_path_ext(app->inputPath);
    if (strcasecmp(ext, ".raw") != 0 && strcasecmp(ext, ".png") != 0)
    {
        show_error(app, tr(STR_WRONG_EXT_REVERSE), false);
        return;
    }

    char outputPath[PATH_MAX];
    make_output_path(app->inputPath, ".bin", outputPath, sizeof(outputPath));

    ConvStats stats;
    char errorText[512] = { 0 };
    ConvStatus status = convert_reverse(app->inputPath, outputPath, &app->meta, &stats,
                                        errorText, sizeof(errorText));

    if (status == CONV_OK)
    {
        log_info("Min: %.1f, Max: %.1f, Delta: %.1f", stats.min, stats.max, stats.delta);
        show_success(app, outputPath, NULL);
        return;
    }

    log_error("Reverse conversion failed (%s): %s", conv_status_string(status), errorText);

    switch (status)
    {
        case CONV_ERR_INPUT_SIZE:
            show_error(app, tr(STR_STRUCT_ERROR), true);
            break;
        case CONV_ERR_RAW_EMPTY:
            show_error(app, tr(STR_INVALID_RAW), false);
            break;
        case CONV_ERR_RAW_INCOMPLETE:
            show_error(app, tr(STR_INCOMPLETE_16BIT), false);
            break;
        case CONV_ERR_PNG_NOT16:
            show_error(app, tr(STR_NOT_16BIT), false);
            break;
        case CONV_ERR_PNG_READ:
        case CONV_ERR_PNG_WRITE:
            show_error(app, tr(STR_INVALID_PNG), false);
            break;
        case CONV_ERR_META_MISSING:
        case CONV_ERR_META_NAN:
        case CONV_ERR_META_ZERO:
            show_error(app, tr(STR_UNSUPPORTED_FILE), false);
            break;
        default:
        {
            char message[1200];
            snprintf(message, sizeof(message), "%s\n\n%s", tr(STR_ZERODIV_ERROR), errorText);
            show_error(app, message, true);
            break;
        }
    }
}

/* ------------------------------------------------------------------------- */
/* File selection                                                            */
/* ------------------------------------------------------------------------- */

static void open_file_picker(App *app, FdFilter filter, const char *startPath)
{
    app->fdFilter = filter;
    fd_open(&app->fd, filter, startPath, app->lang);
    log_info("Opening file picker (%s)", filter == FD_FILTER_JSON ? "json"
                                        : filter == FD_FILTER_RAW_PNG ? "raw/png" : "bin");
}

static void accept_input_path(App *app, const char *path)
{
    if (app->mode == MODE_FORWARD)
    {
        if (strcasecmp(plat_path_ext(path), ".bin") != 0)
        {
            show_error(app, tr(STR_WRONG_EXTENSION), false);
            log_error("Selected file is not .bin file!");
            return;
        }
        snprintf(app->inputPath, sizeof(app->inputPath), "%s", path);
        log_info("Selected input file: %s", path);
        return;
    }

    const char *ext = plat_path_ext(path);
    if (strcasecmp(ext, ".png") == 0)
    {
        uint16_t *pixels = NULL;
        int width = 0, height = 0;
        PngStatus status = png_read_gray16(path, &pixels, &width, &height);
        if (status == PNGIO_ERR_NOT_GRAY16)
        {
            show_error(app, tr(STR_NOT_16BIT), false);
            log_error("Selected PNG is not 16-bit grayscale!");
            return;
        }
        if (status != PNGIO_OK)
        {
            show_error(app, tr(STR_INVALID_PNG), false);
            log_error("Unable to read selected PNG");
            return;
        }
        free(pixels);
    }
    else if (strcasecmp(ext, ".raw") == 0)
    {
        long long size = plat_file_size(path);
        if (size > 0 && (size % 2) != 0)
        {
            show_error(app, tr(STR_INCOMPLETE_16BIT), false);
            log_error("Selected .raw file has incomplete 16-bit data");
            return;
        }
    }
    else
    {
        show_error(app, tr(STR_WRONG_EXT_REVERSE), false);
        log_error("Selected file is not .raw or .png!");
        return;
    }

    snprintf(app->inputPath, sizeof(app->inputPath), "%s", path);
    log_info("Selected input file: %s", path);
}

static void handle_file_picker_result(App *app)
{
    if (app->fd.accepted)
    {
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s", app->fd.result);
        app->fd.accepted = false;

        if (app->fdFilter == FD_FILTER_JSON) handle_json_selection(app, path);
        else accept_input_path(app, path);
    }
    else if (app->fd.cancelled)
    {
        app->fd.cancelled = false;
        log_info("User closed file picker.");
    }
}

/* ------------------------------------------------------------------------- */
/* Top bar                                                                   */
/* ------------------------------------------------------------------------- */

static void topbar_tooltip_button(App *app, int id, Rectangle bounds, IconKind icon,
                                  const char *tooltip)
{
    bool hovered = app->ui.inputEnabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    bool clicked = ui_icon_button(&app->ui, bounds, icon, app->ui.theme.accent);
    if (hovered)
    {
        if (app->hoverId != id)
        {
            app->hoverId = id;
            app->hoverTimer = 0.0f;
        }
        app->hoverTimer += GetFrameTime();
        if (app->hoverTimer > 0.45f)
        {
            app->tooltip = tooltip;
            app->tooltipAnchor = bounds;
        }
    }
    else if (app->hoverId == id)
    {
        app->hoverId = -1;
        app->hoverTimer = 0.0f;
    }
    if (clicked) log_debug("Top bar button %d clicked", id);
}

static void update_window_drag(App *app, bool allowed)
{
    if (!allowed) return;

    Rectangle minimizeRect = { WINDOW_W - 60.0f, 2.0f, 24.0f, 24.0f };
    Rectangle closeRect = { WINDOW_W - 32.0f, 2.0f, 24.0f, 24.0f };
    Vector2 mouse = GetMousePosition();
    int globalX = 0, globalY = 0;

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && mouse.y < 28.0f &&
        !CheckCollisionPointRec(mouse, minimizeRect) &&
        !CheckCollisionPointRec(mouse, closeRect))
    {
        app->dragging = true;
        app->dragOffset = mouse;
        app->dragUseGlobal = plat_global_mouse(&globalX, &globalY);
        app->dragStartGlobal = (Vector2){ (float)globalX, (float)globalY };
        app->dragStartWindow = GetWindowPosition();
    }

    if (app->dragging && IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        if (app->dragUseGlobal && plat_global_mouse(&globalX, &globalY))
        {
            /* Absolute screen coordinates: the window cannot "run away" if
             * the cursor stops but the button is still held. */
            SetWindowPosition((int)(app->dragStartWindow.x + globalX - app->dragStartGlobal.x),
                              (int)(app->dragStartWindow.y + globalY - app->dragStartGlobal.y));
        }
        else
        {
            Vector2 windowPos = GetWindowPosition();
            Vector2 current = GetMousePosition();
            SetWindowPosition((int)(windowPos.x + (current.x - app->dragOffset.x) * UI_SCALE),
                              (int)(windowPos.y + (current.y - app->dragOffset.y) * UI_SCALE));
        }
    }

    if (app->dragging && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        app->dragging = false;
        app->dragJustEnded = true;
    }
}

static void draw_topbar(App *app, bool enabled)
{
    Theme *theme = &app->ui.theme;
    DrawRectangle(0, 0, WINDOW_W, 28, theme->topbar);
    DrawLine(0, 28, WINDOW_W, 28, Fade(theme->border, 0.35f));

    ui_draw_app_icon(&app->ui, (Rectangle){ 8.0f, 6.0f, 16.0f, 16.0f });

    char title[128];
    snprintf(title, sizeof(title), "%s %s", APP_NAME, APP_VERSION);
    ui_draw_text(&app->ui, title, 30.0f, 7.0f, 12.0f, theme->text, true);

    Rectangle minimizeRect = { WINDOW_W - 60.0f, 2.0f, 24.0f, 24.0f };
    Rectangle closeRect = { WINDOW_W - 32.0f, 2.0f, 24.0f, 24.0f };

    if (enabled)
    {
        topbar_tooltip_button(app, 1, minimizeRect, UI_ICON_MIN, tr(STR_MIN));
        if (CheckCollisionPointRec(GetMousePosition(), minimizeRect) &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            MinimizeWindow();

        topbar_tooltip_button(app, 2, closeRect, UI_ICON_CLOSE, tr(STR_EXIT));
        if (CheckCollisionPointRec(GetMousePosition(), closeRect) &&
            IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
            app->shouldClose = true;
    }
    else
    {
        ui_draw_icon(&app->ui, UI_ICON_MIN, (Rectangle){ minimizeRect.x + 2, minimizeRect.y + 2, 20, 20 },
                     theme->textDim, theme->topbar);
        ui_draw_icon(&app->ui, UI_ICON_CLOSE, (Rectangle){ closeRect.x + 2, closeRect.y + 2, 20, 20 },
                     theme->textDim, theme->topbar);
    }
}

/* ------------------------------------------------------------------------- */
/* Footer                                                                    */
/* ------------------------------------------------------------------------- */

static bool social_button(App *app, int index, Rectangle bounds, const char *tooltip)
{
    bool hovered = app->ui.inputEnabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    Color background = app->ui.theme.bg;
    if (hovered)
    {
        background = ColorAlphaBlend(app->ui.theme.bg, Fade(app->ui.theme.accent, 0.12f), WHITE);
        ui_panel(bounds, 0.35f, background, BLANK, 0);
    }
    ui_draw_social(&app->ui, index,
                   (Rectangle){ bounds.x + 6.0f, bounds.y + 6.0f, bounds.width - 12.0f,
                                bounds.height - 12.0f },
                   app->ui.theme.accent);

    if (hovered)
    {
        int id = 100 + index;
        if (app->hoverId != id)
        {
            app->hoverId = id;
            app->hoverTimer = 0.0f;
        }
        app->hoverTimer += GetFrameTime();
        if (app->hoverTimer > 0.45f)
        {
            app->tooltip = tooltip;
            app->tooltipAnchor = bounds;
        }
    }
    return clicked;
}

static void draw_footer(App *app, bool enabled)
{
    Theme *theme = &app->ui.theme;
    float toolbarY = 452.0f;
    float toolbarX = 195.0f;

    topbar_tooltip_button(app, 10, (Rectangle){ toolbarX, toolbarY, 40.0f, 40.0f }, UI_ICON_HELP,
                          tr(STR_HELP));
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){ toolbarX, toolbarY, 40, 40 }) &&
        enabled && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        show_help_dialog(app);

    topbar_tooltip_button(app, 11, (Rectangle){ toolbarX + 50, toolbarY, 40.0f, 40.0f }, UI_ICON_GLOBE,
                          tr(STR_CNGLANG));
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){ toolbarX + 50, toolbarY, 40, 40 }) &&
        enabled && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        show_language_dialog(app);

    IconKind themeIcon = app->dark ? UI_ICON_MOON : UI_ICON_SUN;
    topbar_tooltip_button(app, 12, (Rectangle){ toolbarX + 100, toolbarY, 40.0f, 40.0f }, themeIcon,
                          tr(STR_TOGGLETHEME));
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){ toolbarX + 100, toolbarY, 40, 40 }) &&
        enabled && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        app->dark = !app->dark;
        ui_set_theme(&app->ui, app->dark);
        log_info("Theme switched to %s", app->dark ? "dark" : "light");
    }

    topbar_tooltip_button(app, 13, (Rectangle){ toolbarX + 150, toolbarY, 40.0f, 40.0f }, UI_ICON_SWAP,
                          tr(app->mode == MODE_FORWARD ? STR_MODESWITCH2 : STR_MODESWITCH1));
    if (CheckCollisionPointRec(GetMousePosition(), (Rectangle){ toolbarX + 150, toolbarY, 40, 40 }) &&
        enabled && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
    {
        app->mode = (app->mode == MODE_FORWARD) ? MODE_REVERSE : MODE_FORWARD;
        app->inputPath[0] = '\0';
        app->jsonPath[0] = '\0';
        app->pendingJson[0] = '\0';
        app->metaLoaded = false;
        app->format = -1;
        app->sizeIndex = -1;
        app->sizeEdit = false;
        log_info("Switched to \"%s\" mode", app->mode == MODE_FORWARD ? ".bin --> .raw/.png"
                                                                       : ".raw/.png --> .bin");
    }

    float socialY = 506.0f;
    float socialX = 205.0f;
    if (social_button(app, 0, (Rectangle){ socialX, socialY, 32.0f, 32.0f }, tr(STR_GITHUB)) &&
        enabled)
        plat_open_url(REPO_URL);
    if (social_button(app, 1, (Rectangle){ socialX + 46, socialY, 32.0f, 32.0f }, tr(STR_DISCORD)) &&
        enabled)
        plat_open_url(DISCORD_URL);
    if (social_button(app, 2, (Rectangle){ socialX + 92, socialY, 32.0f, 32.0f }, tr(STR_TELEGRAM)) &&
        enabled)
        plat_open_url(TELEGRAM_URL);
    if (social_button(app, 3, (Rectangle){ socialX + 138, socialY, 32.0f, 32.0f }, tr(STR_YOUTUBE)) &&
        enabled)
        plat_open_url(YOUTUBE_URL);

    char versionText[64];
    snprintf(versionText, sizeof(versionText), "%s %s", APP_VERSION, APP_BUILD);
    float versionWidth = ui_measure(&app->ui, versionText, 10.0f, false);
    Rectangle versionRect = { WINDOW_W - 10.0f - versionWidth - 4.0f, WINDOW_H - 24.0f,
                              versionWidth + 4.0f, 16.0f };
    ui_draw_text_right(&app->ui, versionText, versionRect, 10.0f, theme->version, false);
    if (enabled && CheckCollisionPointRec(GetMousePosition(), versionRect) &&
        IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        show_version_dialog(app);
}

/* ------------------------------------------------------------------------- */
/* Screens                                                                   */
/* ------------------------------------------------------------------------- */

static void draw_logo(App *app)
{
    Rectangle logo = { (WINDOW_W - 470.0f) * 0.5f, 38.0f, 470.0f, 81.0f };
    ui_draw_logo(&app->ui, logo);
}

static void draw_size_dropdown(App *app, bool enabled)
{
    static const char *items[] = { "4x4", "8x8", "16x16", "32x32", "64x64" };
    const int itemCount = 5;
    Theme *theme = &app->ui.theme;
    Vector2 mouse = GetMousePosition();

    Rectangle box = { (WINDOW_W - 350.0f) * 0.5f, 330.0f, 350.0f, 40.0f };
    bool hovered = enabled && CheckCollisionPointRec(mouse, box);

    ui_panel(box, 0.16f, theme->fieldBg, hovered ? theme->accent : theme->border, 1.2f);

    if (app->sizeIndex >= 0 && app->sizeIndex < itemCount)
    {
        ui_draw_text(&app->ui, items[app->sizeIndex], box.x + 12.0f,
                     box.y + (box.height - 13.0f) * 0.5f, 13.0f, theme->text, false);
    }
    else
    {
        Rectangle hint = { box.x + 12.0f, box.y, box.width - 46.0f, box.height };
        ui_draw_text_clip(&app->ui, tr(STR_SELECT_SIZE), hint, 12.0f, theme->textDim, false);
    }

    float ax = box.x + box.width - 24.0f;
    float ay = box.y + box.height * 0.5f;
    if (app->sizeEdit)
        DrawTriangle((Vector2){ ax, ay + 4.0f }, (Vector2){ ax + 10.0f, ay + 4.0f },
                     (Vector2){ ax + 5.0f, ay - 3.0f }, theme->text);
    else
        DrawTriangle((Vector2){ ax, ay - 4.0f }, (Vector2){ ax + 10.0f, ay - 4.0f },
                     (Vector2){ ax + 5.0f, ay + 3.0f }, theme->text);

    if (enabled && hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        app->sizeEdit = !app->sizeEdit;

    if (!app->sizeEdit)
    {
        if (enabled && IsKeyPressed(KEY_ESCAPE)) app->sizeEdit = false;
        return;
    }

    Rectangle popup = { box.x, box.y + box.height + 4.0f, box.width, (float)itemCount * 30.0f + 8.0f };
    if (enabled && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
        !CheckCollisionPointRec(mouse, box) && !CheckCollisionPointRec(mouse, popup))
    {
        app->sizeEdit = false;
        return;
    }

    ui_panel(popup, 0.06f, theme->panel, theme->border, 1.0f);
    for (int i = 0; i < itemCount; i++)
    {
        Rectangle row = { popup.x + 4.0f, popup.y + 4.0f + (float)i * 30.0f,
                          popup.width - 8.0f, 28.0f };
        bool rowHovered = enabled && CheckCollisionPointRec(mouse, row);
        if (i == app->sizeIndex)
            ui_panel(row, 0.18f, theme->selection, BLANK, 0);
        else if (rowHovered)
            ui_panel(row, 0.18f, Fade(theme->accent, 0.10f), BLANK, 0);

        ui_draw_text(&app->ui, items[i], row.x + 10.0f, row.y + (row.height - 13.0f) * 0.5f,
                     13.0f, i == app->sizeIndex ? theme->accent : theme->text, false);

        if (rowHovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT))
        {
            app->sizeIndex = i;
            app->sizeEdit = false;
            log_debug("Map size selection: index %d", i);
            return;
        }
    }

    if (enabled && IsKeyPressed(KEY_ESCAPE)) app->sizeEdit = false;
}

static void draw_forward_screen(App *app, bool enabled)
{
    Theme *theme = &app->ui.theme;
    draw_logo(app);

    ui_field(&app->ui, (Rectangle){ 90.0f, 150.0f, 400.0f, 50.0f }, tr(STR_SELECT_FILE),
             app->inputPath, false);

    if (ui_button(&app->ui, (Rectangle){ (WINDOW_W - 130.0f) * 0.5f, 212.0f, 130.0f, 32.0f },
                  tr(STR_SEL_BUTTON), enabled))
        open_file_picker(app, FD_FILTER_BIN, app->inputPath);

    ui_draw_text_center(&app->ui, tr(STR_SELECT_FORMAT),
                        (Rectangle){ 0.0f, 258.0f, WINDOW_W, 22.0f }, 15.0f, theme->text, false);

    float rawWidth = 24.0f + ui_measure(&app->ui, ".raw", 15.0f, false);
    float pngWidth = 24.0f + ui_measure(&app->ui, ".png", 15.0f, false);
    float totalWidth = rawWidth + 40.0f + pngWidth;
    float radioX = (WINDOW_W - totalWidth) * 0.5f;

    if (ui_radio(&app->ui, (Rectangle){ radioX, 288.0f, rawWidth, 24.0f }, ".raw",
                 app->format == 0) &&
        enabled)
        app->format = 0;
    if (ui_radio(&app->ui, (Rectangle){ radioX + rawWidth + 40.0f, 288.0f, pngWidth, 24.0f },
                 ".png", app->format == 1) &&
        enabled)
        app->format = 1;

    if (ui_button(&app->ui, (Rectangle){ (WINDOW_W - 160.0f) * 0.5f, 398.0f, 160.0f, 36.0f },
                  tr(STR_CONVERT_FILE), enabled))
        forward_convert(app);
}

static void draw_reverse_screen(App *app, bool enabled)
{
    draw_logo(app);

    ui_field(&app->ui, (Rectangle){ 90.0f, 150.0f, 400.0f, 50.0f }, tr(STR_SELECT_FILE),
             app->inputPath, false);
    if (ui_button(&app->ui, (Rectangle){ (WINDOW_W - 130.0f) * 0.5f, 212.0f, 130.0f, 32.0f },
                  tr(STR_SEL_BUTTON), enabled))
        open_file_picker(app, FD_FILTER_RAW_PNG, app->inputPath);

    ui_field(&app->ui, (Rectangle){ 90.0f, 272.0f, 400.0f, 50.0f }, tr(STR_JSON), app->jsonPath,
             false);
    if (ui_button(&app->ui, (Rectangle){ (WINDOW_W - 130.0f) * 0.5f, 334.0f, 130.0f, 32.0f },
                  tr(STR_SEL_BUTTON), enabled))
        open_file_picker(app, FD_FILTER_JSON, app->jsonPath);

    if (ui_button(&app->ui, (Rectangle){ (WINDOW_W - 160.0f) * 0.5f, 398.0f, 160.0f, 36.0f },
                  tr(STR_CONVERT_FILE), enabled))
        reverse_convert(app);
}

/* ------------------------------------------------------------------------- */
/* Dialogs                                                                   */
/* ------------------------------------------------------------------------- */

static Rectangle begin_dialog(App *app, float width, float height, Color fill, Color border)
{
    DrawRectangle(0, 0, WINDOW_W, WINDOW_H, app->ui.theme.overlay);
    Rectangle panel = { (WINDOW_W - width) * 0.5f, (WINDOW_H - height) * 0.5f, width, height };
    ui_panel(panel, 0.05f, fill, border, 1.2f);
    return panel;
}

static void draw_dialog_header(App *app, Rectangle panel, IconKind icon, Color iconColor,
                               Color textColor, Color fill)
{
    ui_draw_icon(&app->ui, icon, (Rectangle){ panel.x + 20.0f, panel.y + 18.0f, 26.0f, 26.0f },
                 iconColor, fill);
    ui_draw_text(&app->ui, app->dlgTitle, panel.x + 56.0f, panel.y + 21.0f, 16.0f, textColor, true);
}

static bool dialog_button(App *app, Rectangle bounds, const char *label, Color color)
{
    bool hovered = app->ui.inputEnabled && CheckCollisionPointRec(GetMousePosition(), bounds);
    bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
    if (hovered) ui_panel(bounds, 0.25f, Fade(color, 0.12f), BLANK, 0);
    ui_draw_text_center(&app->ui, label, bounds, 14.0f, color, false);
    return clicked;
}

static float dialog_button_width(App *app, const char *label)
{
    return ui_measure(&app->ui, label, 14.0f, false) + 30.0f;
}

static void clamp_dialog_scroll(App *app, Rectangle content, float contentHeight)
{
    (void)content;
    float maxScroll = contentHeight - content.height;
    if (maxScroll < 0.0f) maxScroll = 0.0f;
    if (app->dlgScroll < 0.0f) app->dlgScroll = 0.0f;
    if (app->dlgScroll > maxScroll) app->dlgScroll = maxScroll;
}

static void draw_dialog_scrollbar(Rectangle content, float contentHeight, float scroll)
{
    if (contentHeight <= content.height) return;
    float trackH = content.height;
    float thumbH = fmaxf(24.0f, trackH * content.height / contentHeight);
    float maxScroll = contentHeight - content.height;
    float progress = maxScroll > 0.0f ? scroll / maxScroll : 0.0f;
    float x = content.x + content.width - 4.0f;
    DrawRectangleRounded((Rectangle){ x, content.y, 4.0f, trackH }, 0.5f, 6,
                         Fade(g_app.ui.theme.border, 0.35f));
    DrawRectangleRounded((Rectangle){ x, content.y + (trackH - thumbH) * progress, 4.0f, thumbH },
                         0.5f, 6, Fade(g_app.ui.theme.accent, 0.65f));
}

static void draw_message_dialog(App *app, DialogKind kind)
{
    Theme *theme = &app->ui.theme;
    Color fill = theme->panel;
    Color border = theme->panelBorder;
    Color textColor = theme->text;
    Color buttonColor = theme->accent;
    IconKind icon = UI_ICON_INFO;

    if (kind == DLG_SUCCESS)
    {
        fill = theme->greenBg;
        border = fill;
        textColor = WHITE;
        buttonColor = WHITE;
        icon = UI_ICON_CHECK;
    }
    else if (kind == DLG_ERROR)
    {
        fill = theme->redBg;
        border = fill;
        textColor = WHITE;
        buttonColor = WHITE;
        icon = UI_ICON_WARNING;
    }
    else if (kind == DLG_CONFIRM)
    {
        fill = theme->orangeBg;
        border = fill;
        textColor = WHITE;
        buttonColor = WHITE;
        icon = UI_ICON_WARNING;
    }

    char message[8192];
    if (kind == DLG_SUCCESS)
    {
        message[0] = '\0';
        str_append(message, sizeof(message), tr(STR_FILE_SAVED));
        str_append(message, sizeof(message), "\n");
        str_append(message, sizeof(message), app->successOut);
        if (app->successMeta[0])
        {
            str_append(message, sizeof(message), "\n\n");
            str_append(message, sizeof(message), tr(STR_META_PATH));
            str_append(message, sizeof(message), "\n");
            str_append(message, sizeof(message), app->successMeta);
        }
    }
    else
    {
        snprintf(message, sizeof(message), "%s", app->dlgMessage);
    }

    float width = 460.0f;
    int lineCount = ui_wrap(&app->ui, message, 13.5f, width - 40.0f, g_dialogLines, UI_MAX_LINES);
    float contentHeight = ui_lines_height(lineCount, 13.5f);
    float height = 64.0f + contentHeight + 64.0f;
    if (height < 190.0f) height = 190.0f;
    if (height > 540.0f) height = 540.0f;

    Rectangle panel = begin_dialog(app, width, height, fill, border);
    draw_dialog_header(app, panel, icon, textColor, textColor, fill);

    Rectangle content = { panel.x + 20.0f, panel.y + 64.0f, panel.width - 40.0f,
                          panel.height - 128.0f };
    if (CheckCollisionPointRec(GetMousePosition(), content))
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) app->dlgScroll -= wheel * 40.0f;
    }
    clamp_dialog_scroll(app, content, contentHeight);

    ui_draw_lines(&app->ui, g_dialogLines, lineCount, content, 13.5f, textColor, app->dlgScroll);
    draw_dialog_scrollbar(content, contentHeight, app->dlgScroll);

    float buttonY = panel.y + panel.height - 50.0f;
    float right = panel.x + panel.width - 20.0f;

    const char *okLabel = "OK";
    float okWidth = dialog_button_width(app, okLabel);
    Rectangle okRect = { right - okWidth, buttonY, okWidth, 34.0f };
    if (kind == DLG_CONFIRM)
    {
        const char *noLabel = tr(STR_NO);
        float noWidth = dialog_button_width(app, noLabel);
        Rectangle noRect = { right - noWidth, buttonY, noWidth, 34.0f };
        const char *yesLabel = tr(STR_YES);
        float yesWidth = dialog_button_width(app, yesLabel);
        Rectangle yesRect = { noRect.x - 10.0f - yesWidth, buttonY, yesWidth, 34.0f };

        if (dialog_button(app, noRect, noLabel, buttonColor))
        {
            app->dialog = DLG_NONE;
            app->pendingJson[0] = '\0';
        }
        if (dialog_button(app, yesRect, yesLabel, buttonColor))
        {
            if (app->pendingJson[0])
            {
                apply_json_selection(app, app->pendingJson, &app->pendingMeta);
                app->pendingJson[0] = '\0';
            }
            app->dialog = DLG_NONE;
        }
    }
    else
    {
        if (kind == DLG_ERROR && app->dlgHelp)
        {
            const char *helpLabel = tr(STR_HELP);
            float helpWidth = dialog_button_width(app, helpLabel);
            Rectangle helpRect = { right - helpWidth, buttonY, helpWidth, 34.0f };
            okRect.x = helpRect.x - 10.0f - okRect.width;
            if (dialog_button(app, okRect, okLabel, buttonColor)) app->dialog = DLG_NONE;
            if (dialog_button(app, helpRect, helpLabel, buttonColor))
                show_help_dialog(app);
        }
        else
        {
            if (dialog_button(app, okRect, okLabel, buttonColor)) app->dialog = DLG_NONE;
        }
    }
}

static void draw_help_dialog(App *app)
{
    Theme *theme = &app->ui.theme;
    float width = 500.0f, height = 540.0f;
    Rectangle panel = begin_dialog(app, width, height, theme->panel, theme->panelBorder);
    draw_dialog_header(app, panel, UI_ICON_INFO, theme->text, theme->text, theme->panel);

    int lineCount = ui_wrap(&app->ui, app->dlgMessage, 13.0f, width - 40.0f, g_dialogLines,
                            UI_MAX_LINES);
    float contentHeight = ui_lines_height(lineCount, 13.0f);
    Rectangle content = { panel.x + 20.0f, panel.y + 64.0f, panel.width - 40.0f,
                          panel.height - 128.0f };

    if (CheckCollisionPointRec(GetMousePosition(), content))
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) app->dlgScroll -= wheel * 40.0f;
    }
    clamp_dialog_scroll(app, content, contentHeight);
    ui_draw_lines(&app->ui, g_dialogLines, lineCount, content, 13.0f, theme->text, app->dlgScroll);
    draw_dialog_scrollbar(content, contentHeight, app->dlgScroll);

    float buttonY = panel.y + panel.height - 50.0f;
    float right = panel.x + panel.width - 20.0f;
    const char *okLabel = "OK";
    float okWidth = dialog_button_width(app, okLabel);
    Rectangle okRect = { right - okWidth, buttonY, okWidth, 34.0f };

    const char *metaLabel = tr(STR_METADATAINFO);
    float metaWidth = dialog_button_width(app, metaLabel);
    Rectangle metaRect = { okRect.x - 10.0f - metaWidth, buttonY, metaWidth, 34.0f };

    if (dialog_button(app, metaRect, metaLabel, theme->accent)) show_meta_dialog(app);
    if (dialog_button(app, okRect, okLabel, theme->accent)) app->dialog = DLG_NONE;
}

static void draw_meta_dialog(App *app)
{
    Theme *theme = &app->ui.theme;
    float width = 480.0f, height = 380.0f;
    Rectangle panel = begin_dialog(app, width, height, theme->panel, theme->panelBorder);
    draw_dialog_header(app, panel, UI_ICON_INFO, theme->text, theme->text, theme->panel);

    int lineCount = ui_wrap(&app->ui, app->dlgMessage, 13.0f, width - 40.0f, g_dialogLines,
                            UI_MAX_LINES);
    float contentHeight = ui_lines_height(lineCount, 13.0f);
    Rectangle content = { panel.x + 20.0f, panel.y + 64.0f, panel.width - 40.0f,
                          panel.height - 128.0f };
    clamp_dialog_scroll(app, content, contentHeight);
    ui_draw_lines(&app->ui, g_dialogLines, lineCount, content, 13.0f, theme->text, app->dlgScroll);
    draw_dialog_scrollbar(content, contentHeight, app->dlgScroll);

    float buttonY = panel.y + panel.height - 50.0f;
    float right = panel.x + panel.width - 20.0f;
    const char *okLabel = "OK";
    float okWidth = dialog_button_width(app, okLabel);
    Rectangle okRect = { right - okWidth, buttonY, okWidth, 34.0f };
    if (dialog_button(app, okRect, okLabel, theme->accent)) app->dialog = DLG_NONE;
}

static void draw_version_dialog(App *app)
{
    Theme *theme = &app->ui.theme;
    float width = 400.0f, height = 262.0f;
    Rectangle panel = begin_dialog(app, width, height, theme->panel, theme->panelBorder);
    draw_dialog_header(app, panel, UI_ICON_INFO, theme->text, theme->text, theme->panel);

    char text[2048];
    snprintf(text, sizeof(text),
             "powered by C, raylib, raygui, zlib\n"
             "font: DejaVu Sans\n\n"
             "Authors: stakan (GUI), ThePlain (conversion script)\n\n"
             "github.com/stakanyash/displacebin_gui_converter");

    int lineCount = ui_wrap(&app->ui, text, 12.5f, width - 40.0f, g_dialogLines, UI_MAX_LINES);
    Rectangle content = { panel.x + 20.0f, panel.y + 64.0f, panel.width - 40.0f, 120.0f };
    for (int i = 0; i < lineCount; i++)
    {
        float y = content.y + (float)i * ui_line_height(12.5f);
        if (y > content.y + content.height) break;
        bool urlLine = (strncmp(g_dialogLines[i], "github.com/", 11) == 0);
        ui_draw_text(&app->ui, g_dialogLines[i], content.x, y, 12.5f,
                     urlLine ? theme->link : theme->text, false);
    }

    float buttonY = panel.y + panel.height - 50.0f;
    float right = panel.x + panel.width - 20.0f;
    const char *okLabel = "OK";
    float okWidth = dialog_button_width(app, okLabel);
    Rectangle okRect = { right - okWidth, buttonY, okWidth, 34.0f };
    const char *gitLabel = "GitHub";
    float gitWidth = dialog_button_width(app, gitLabel);
    Rectangle gitRect = { okRect.x - 10.0f - gitWidth, buttonY, gitWidth, 34.0f };

    if (dialog_button(app, gitRect, gitLabel, theme->accent)) plat_open_url(REPO_URL);
    if (dialog_button(app, okRect, okLabel, theme->accent)) app->dialog = DLG_NONE;
}

static void draw_language_dialog(App *app)
{
    Theme *theme = &app->ui.theme;
    float width = 320.0f, height = 350.0f;
    Rectangle panel = begin_dialog(app, width, height, theme->panel, theme->panelBorder);
    draw_dialog_header(app, panel, UI_ICON_GLOBE, theme->text, theme->text, theme->panel);

    float y = panel.y + 64.0f;
    for (int i = 0; i < LANG_COUNT; i++)
    {
        Rectangle button = { panel.x + 30.0f, y, panel.width - 60.0f, 34.0f };
        bool hovered = CheckCollisionPointRec(GetMousePosition(), button);
        bool clicked = hovered && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
        Color fill = (i == (int)app->lang) ? Fade(theme->accent, 0.15f) : theme->buttonBg;
        if (hovered) fill = theme->buttonHover;
        ui_panel(button, 0.25f, fill, BLANK, 0);
        ui_draw_text_center(&app->ui, i18n_language((LangId)i)->nativeName, button, 14.0f,
                            theme->text, false);

        if (clicked)
        {
            app->lang = (LangId)i;
            app->dialog = DLG_NONE;
            log_info("Language changed to %s", i18n_language(app->lang)->code);
            break;
        }
        y += 42.0f;
    }

    Rectangle cancelRect = { panel.x + panel.width - 20.0f - 90.0f,
                             panel.y + panel.height - 46.0f, 90.0f, 32.0f };
    if (dialog_button(app, cancelRect, tr(STR_CANCEL), theme->accent))
        app->dialog = DLG_NONE;
}

static void draw_update_dialog(App *app)
{
    Theme *theme = &app->ui.theme;
    float width = 500.0f, height = 470.0f;
    Rectangle panel = begin_dialog(app, width, height, theme->panel, theme->panelBorder);
    draw_dialog_header(app, panel, UI_ICON_OPEN, theme->text, theme->text, theme->panel);

    char text[16384];
    snprintf(text, sizeof(text), "%s %s\n\n%s", tr(STR_VERSION_FOR_DOWNLOAD), app->updateVersion,
             app->updateBody);

    int lineCount = ui_wrap(&app->ui, text, 12.5f, width - 40.0f, g_dialogLines, UI_MAX_LINES);
    float contentHeight = ui_lines_height(lineCount, 12.5f);
    Rectangle content = { panel.x + 20.0f, panel.y + 64.0f, panel.width - 40.0f,
                          panel.height - 128.0f };

    if (CheckCollisionPointRec(GetMousePosition(), content))
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) app->dlgScroll -= wheel * 40.0f;
    }
    clamp_dialog_scroll(app, content, contentHeight);
    ui_draw_lines(&app->ui, g_dialogLines, lineCount, content, 12.5f, theme->text, app->dlgScroll);
    draw_dialog_scrollbar(content, contentHeight, app->dlgScroll);

    float buttonY = panel.y + panel.height - 50.0f;
    float right = panel.x + panel.width - 20.0f;
    const char *cancelLabel = tr(STR_CANCEL);
    float cancelWidth = dialog_button_width(app, cancelLabel);
    Rectangle cancelRect = { right - cancelWidth, buttonY, cancelWidth, 34.0f };
    const char *openLabel = tr(STR_OPEN_GITHUB);
    float openWidth = dialog_button_width(app, openLabel);
    Rectangle openRect = { cancelRect.x - 10.0f - openWidth, buttonY, openWidth, 34.0f };

    if (dialog_button(app, openRect, openLabel, theme->accent))
    {
        plat_open_url(RELEASES_URL);
        app->dialog = DLG_NONE;
    }
    if (dialog_button(app, cancelRect, cancelLabel, theme->accent)) app->dialog = DLG_NONE;
}

static void draw_dialog(App *app)
{
    switch (app->dialog)
    {
        case DLG_LANGUAGE:
            draw_language_dialog(app);
            break;
        case DLG_HELP:
            draw_help_dialog(app);
            break;
        case DLG_META:
            draw_meta_dialog(app);
            break;
        case DLG_VERSION:
            draw_version_dialog(app);
            break;
        case DLG_UPDATE:
            draw_update_dialog(app);
            break;
        case DLG_SUCCESS:
        case DLG_ERROR:
        case DLG_CONFIRM:
            draw_message_dialog(app, app->dialog);
            break;
        default:
            break;
    }

    if (app->dialog != DLG_NONE && IsKeyPressed(KEY_ESCAPE))
    {
        if (app->dialog == DLG_CONFIRM) app->pendingJson[0] = '\0';
        app->dialog = DLG_NONE;
    }
}

/* ------------------------------------------------------------------------- */
/* Update check                                                              */
/* ------------------------------------------------------------------------- */

static void poll_updates(App *app)
{
    if (!app->updateStarted) return;
    if (!app->updatePending)
    {
        char version[64] = { 0 };
        char body[8192] = { 0 };
        if (updater_poll(&app->updater, version, sizeof(version), body, sizeof(body)))
        {
            snprintf(app->updateVersion, sizeof(app->updateVersion), "%s", version);
            snprintf(app->updateBody, sizeof(app->updateBody), "%s", body);
            app->updatePending = true;
            log_info("Update available: %s", version);
        }
    }

    if (app->updatePending && app->dialog == DLG_NONE && !app->fd.open)
    {
        app->updatePending = false;
        open_dialog(app, DLG_UPDATE, tr(STR_UPDATE_AVAILABLE), "");
    }
}

/* ------------------------------------------------------------------------- */
/* Frame                                                                     */
/* ------------------------------------------------------------------------- */

static void draw_frame(App *app)
{
    Theme *theme = &app->ui.theme;
    ClearBackground(theme->bg);

    Camera2D camera = { 0 };
    camera.offset = (Vector2){ 0.0f, 0.0f };
    camera.target = (Vector2){ 0.0f, 0.0f };
    camera.rotation = 0.0f;
    camera.zoom = UI_SCALE;
    BeginMode2D(camera);

    app->tooltip = NULL;
    bool modal = (app->dialog != DLG_NONE);
    bool picker = app->fd.open;
    bool baseEnabled = !modal && !picker;

    update_window_drag(app, baseEnabled && !app->sizeEdit);
    bool baseInteractive = baseEnabled && !app->sizeEdit && !app->dragJustEnded;
    ui_set_input_enabled(&app->ui, baseInteractive);
    if (!baseInteractive) GuiLock();

    draw_topbar(app, baseInteractive);
    if (app->mode == MODE_FORWARD) draw_forward_screen(app, baseInteractive);
    else draw_reverse_screen(app, baseInteractive);
    draw_footer(app, baseInteractive);

    if (app->mode == MODE_FORWARD)
    {
        ui_set_input_enabled(&app->ui, baseEnabled);
        draw_size_dropdown(app, baseEnabled);
    }

    if (!baseInteractive) GuiUnlock();

    if (modal)
    {
        ui_set_input_enabled(&app->ui, true);
        draw_dialog(app);
    }
    else if (picker)
    {
        ui_set_input_enabled(&app->ui, true);
        fd_draw(&app->fd, &app->ui, app->lang);
        handle_file_picker_result(app);
    }

    if (app->tooltip)
        ui_tooltip(&app->ui, app->tooltip, app->tooltipAnchor);

    EndMode2D();

    app->dragJustEnded = false;
}

/* ------------------------------------------------------------------------- */
/* Command line                                                              */
/* ------------------------------------------------------------------------- */

static const char *find_option_value(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], name) == 0 && i + 1 < argc) return argv[i + 1];
        size_t len = strlen(name);
        if (strncmp(argv[i], name, len) == 0 && argv[i][len] == '=') return argv[i] + len + 1;
    }
    return NULL;
}

static bool has_flag(int argc, char **argv, const char *name)
{
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], name) == 0) return true;
    return false;
}

static void print_usage(void)
{
    printf("DisplaceBox %s - %s\n\n", APP_VERSION, APP_NAME);
    printf("Usage: displacebox [options]\n\n");
    printf("  -lang <En|Ru|Uk|Be|Pl>   force interface language\n");
    printf("  -reverse, --reverse      start in \".raw/.png --> .bin\" mode\n");
    printf("  --light                  start with the light theme\n");
    printf("  --no-update              do not check GitHub for updates\n");
    printf("  --screenshot <file>      save a screenshot after a few frames and exit\n");
    printf("  --frames <n>             frames to wait before the screenshot (default 20)\n");
    printf("  --dialog <name>          open a dialog at startup (help, meta, version,\n");
    printf("                           language, update, success, error, confirm, files)\n");
}

static void setup_demo_dialog(App *app, const char *name)
{
    if (!name) return;

    char message[8192];
    if (strcmp(name, "help") == 0) show_help_dialog(app);
    else if (strcmp(name, "meta") == 0) show_meta_dialog(app);
    else if (strcmp(name, "version") == 0) show_version_dialog(app);
    else if (strcmp(name, "language") == 0) show_language_dialog(app);
    else if (strcmp(name, "files") == 0) open_file_picker(app, FD_FILTER_BIN, NULL);
    else if (strcmp(name, "dropdown") == 0) app->sizeEdit = true;
    else if (strcmp(name, "update") == 0)
    {
        snprintf(app->updateVersion, sizeof(app->updateVersion), "2.3");
        snprintf(app->updateBody, sizeof(app->updateBody),
                 "%s\n\n• Improved performance\n• Fixed theme switching\n• Linux C port",
                 "What's new:");
        open_dialog(app, DLG_UPDATE, tr(STR_UPDATE_AVAILABLE), "");
    }
    else if (strcmp(name, "success") == 0)
    {
        snprintf(app->successOut, sizeof(app->successOut),
                 "/home/user/maps/3m2/displace.png");
        snprintf(app->successMeta, sizeof(app->successMeta),
                 "/home/user/maps/3m2/displace_3m2_metadata.json");
        open_dialog(app, DLG_SUCCESS, tr(STR_RESULT), "");
    }
    else if (strcmp(name, "error") == 0)
    {
        snprintf(message, sizeof(message), "%s", tr(STR_STRUCT_ERROR));
        show_error(app, message, true);
    }
    else if (strcmp(name, "confirm") == 0)
    {
        open_dialog(app, DLG_CONFIRM, tr(STR_WARNING), tr(STR_JSON_OLDER));
    }
}

int main(int argc, char **argv)
{
    plat_ignore_sigchld();
    plat_log_init();

    if (has_flag(argc, argv, "--help") || has_flag(argc, argv, "-h"))
    {
        print_usage();
        plat_log_close();
        return 0;
    }

    App *app = &g_app;
    memset(app, 0, sizeof(*app));
    app->format = -1;
    app->sizeIndex = -1;
    app->dark = !has_flag(argc, argv, "--light");
    app->hoverId = -1;

    LangId language = i18n_from_code(plat_system_language());
    const char *langArg = find_option_value(argc, argv, "-lang");
    if (!langArg) langArg = find_option_value(argc, argv, "--lang");
    if (langArg)
    {
        bool found = false;
        for (int i = 0; i < LANG_COUNT; i++)
        {
            if (strcasecmp(langArg, i18n_language((LangId)i)->code) == 0)
            {
                language = (LangId)i;
                found = true;
                break;
            }
        }
        if (!found) log_warning("Language '%s' from args not found, using system language", langArg);
        else log_info("Language set from args: %s", i18n_language(language)->code);
    }
    app->lang = language;

    if (has_flag(argc, argv, "-reverse") || has_flag(argc, argv, "--reverse"))
        app->mode = MODE_REVERSE;
    else
        app->mode = MODE_FORWARD;

    SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT);
    InitWindow(WINDOW_PIXEL_W, WINDOW_PIXEL_H, APP_NAME);
    if (!IsWindowReady())
    {
        log_error("Unable to create a window. This raylib build requires an X11/XWayland display "
                  "(DISPLAY). Native Wayland-only sessions are not supported by the bundled raylib.");
        fprintf(stderr,
                "DisplaceBox: unable to create a window.\n"
                "The bundled raylib build uses X11; on Wayland it needs XWayland (usually enabled).\n"
                "Make sure DISPLAY is set, or rebuild raylib with Wayland support.\n");
        plat_log_close();
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    SetMouseScale(1.0f / UI_SCALE, 1.0f / UI_SCALE);

    int monitorWidth = GetMonitorWidth(GetCurrentMonitor());
    int monitorHeight = GetMonitorHeight(GetCurrentMonitor());
    if (monitorWidth > 0 && monitorHeight > 0 &&
        (monitorWidth < 1280 || monitorHeight < 720))
    {
        log_warning("Screen resolution %dx%d is below the recommended minimum 1280x720.",
                    monitorWidth, monitorHeight);
    }
    SetWindowPosition((monitorWidth - WINDOW_PIXEL_W) / 2, (monitorHeight - WINDOW_PIXEL_H) / 2);

    ui_init(&app->ui, app->dark);
    ui_set_input_enabled(&app->ui, true);

    const char *screenshot = find_option_value(argc, argv, "--screenshot");
    if (screenshot)
    {
        snprintf(app->screenshotPath, sizeof(app->screenshotPath), "%s", screenshot);
        const char *frames = find_option_value(argc, argv, "--frames");
        app->screenshotAtFrame = frames ? atoi(frames) : 20;
        if (app->screenshotAtFrame < 3) app->screenshotAtFrame = 3;
    }

    setup_demo_dialog(app, find_option_value(argc, argv, "--dialog"));

    bool noUpdate = has_flag(argc, argv, "--no-update") || screenshot != NULL;
    if (!noUpdate)
    {
        updater_start(&app->updater, APP_VERSION);
        app->updateStarted = true;
    }

    log_info("Interface language: %s, theme: %s", i18n_language(app->lang)->code,
             app->dark ? "dark" : "light");

    while (!app->shouldClose && !WindowShouldClose())
    {
        poll_updates(app);
        BeginDrawing();
        draw_frame(app);
        EndDrawing();

        if (IsKeyPressed(KEY_F12))
        {
            TakeScreenshot("displacebox_screenshot.png");
            log_info("Screenshot saved to: displacebox_screenshot.png");
        }

        app->frame++;
        if (app->screenshotPath[0] && app->frame >= app->screenshotAtFrame)
        {
            TakeScreenshot(app->screenshotPath);
            log_info("Screenshot saved to: %s", app->screenshotPath);
            break;
        }
    }

    fd_free(&app->fd);
    updater_join(&app->updater);
    ui_shutdown(&app->ui);
    plat_platform_shutdown();
    CloseWindow();
    log_info("Application closed");
    plat_log_close();
    return 0;
}
