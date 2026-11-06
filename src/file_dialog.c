#define _GNU_SOURCE
#include "file_dialog.h"
#include "i18n.h"
#include "platform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FD_PANEL_W 540.0f
#define FD_PANEL_H 480.0f
#define FD_ROW_H   26.0f
#define FD_LIST_PAD 4.0f

static char g_lastDir[3][DB_PATH_MAX];

static bool has_extension(const char *name, FdFilter filter)
{
    const char *ext = plat_path_ext(name);
    switch (filter)
    {
        case FD_FILTER_BIN:
            return plat_strcasecmp(ext, ".bin") == 0;
        case FD_FILTER_RAW_PNG:
            return plat_strcasecmp(ext, ".raw") == 0 || plat_strcasecmp(ext, ".png") == 0;
        case FD_FILTER_JSON:
            return plat_strcasecmp(ext, ".json") == 0;
    }
    return false;
}

static int compare_entries(const void *a, const void *b)
{
    const FdEntry *ea = (const FdEntry *)a;
    const FdEntry *eb = (const FdEntry *)b;
    if (ea->isDir != eb->isDir) return ea->isDir ? -1 : 1;
    return plat_strcasecmp(ea->name, eb->name);
}

static void fd_reload(FileDialog *fd)
{
    free(fd->entries);
    fd->entries = NULL;
    fd->entryCount = 0;
    fd->scroll = 0;
    fd->reload = false;

    if (fd->drivesView)
    {
        char roots[26][DB_PATH_MAX];
        int count = plat_list_drives(roots, 26);
        int capacity = 0;
        for (int i = 0; i < count; i++)
        {
            if (fd->entryCount == capacity)
            {
                int nextCapacity = capacity ? capacity * 2 : 16;
                FdEntry *grown = (FdEntry *)realloc(fd->entries,
                                                    (size_t)nextCapacity * sizeof(FdEntry));
                if (!grown) break;
                fd->entries = grown;
                capacity = nextCapacity;
            }
            FdEntry *entry = &fd->entries[fd->entryCount++];
            strncpy(entry->name, roots[i], sizeof(entry->name) - 1);
            entry->name[sizeof(entry->name) - 1] = '\0';
            entry->isDir = true;
            entry->size = 0;
        }
        return;
    }

    PlatDir *dir = NULL;
    if (!plat_dir_open(&dir, fd->dir))
    {
        log_warning("File picker: unable to open directory: %s", fd->dir);
        return;
    }

    int capacity = 0;
    PlatDirEntry item;
    while (plat_dir_next(dir, &item))
    {
        const char *name = item.name;
        if (name[0] == '.') continue;
        if (!item.isDir && !has_extension(name, fd->filter)) continue;

        if (fd->entryCount == capacity)
        {
            int nextCapacity = capacity ? capacity * 2 : 64;
            FdEntry *grown = (FdEntry *)realloc(fd->entries, (size_t)nextCapacity * sizeof(FdEntry));
            if (!grown) break;
            fd->entries = grown;
            capacity = nextCapacity;
        }

        FdEntry *entry = &fd->entries[fd->entryCount++];
        snprintf(entry->name, sizeof(entry->name), "%s", name);
        entry->isDir = item.isDir;
        entry->size = item.size;
    }
    plat_dir_close(dir);

    if (fd->entryCount > 1)
        qsort(fd->entries, (size_t)fd->entryCount, sizeof(FdEntry), compare_entries);
}

static void fd_navigate(FileDialog *fd, const char *path)
{
    char absolute[DB_PATH_MAX];
    if (!plat_path_absolute(path, absolute, sizeof(absolute))) return;
    if (!plat_is_dir(absolute)) return;

    snprintf(fd->dir, sizeof(fd->dir), "%s", absolute);
    snprintf(fd->pathInput, sizeof(fd->pathInput), "%s", absolute);
    fd->drivesView = false;
    fd->selected[0] = '\0';
    fd->active = -1;
    fd->reload = true;
    fd->pathEditMode = false;
}

static bool fd_accept(FileDialog *fd, const char *path)
{
    if (!plat_file_exists(path) || plat_is_dir(path)) return false;
    if (!has_extension(path, fd->filter)) return false;

    snprintf(fd->result, sizeof(fd->result), "%s", path);
    fd->accepted = true;
    fd->open = false;
    return true;
}

static void fd_apply_path_input(FileDialog *fd)
{
    char absolute[DB_PATH_MAX];
    if (!plat_path_absolute(fd->pathInput, absolute, sizeof(absolute))) return;

    if (plat_is_dir(absolute))
    {
        fd_navigate(fd, absolute);
        return;
    }
    if (plat_file_exists(absolute))
    {
        if (has_extension(absolute, fd->filter))
        {
            fd_accept(fd, absolute);
            return;
        }
        char parent[DB_PATH_MAX];
        plat_path_dirname(absolute, parent, sizeof(parent));
        fd_navigate(fd, parent);
        snprintf(fd->pathInput, sizeof(fd->pathInput), "%s", absolute);
    }
}

void fd_open(FileDialog *fd, FdFilter filter, const char *startPath, LangId lang)
{
    (void)lang;
    fd_free(fd);
    memset(fd, 0, sizeof(*fd));
    fd->open = true;
    fd->filter = filter;
    fd->active = -1;
    fd->pathEditMode = false;

    char start[DB_PATH_MAX] = { 0 };
    if (startPath && startPath[0])
    {
        if (plat_is_dir(startPath)) snprintf(start, sizeof(start), "%s", startPath);
        else
        {
            char absolute[DB_PATH_MAX];
            if (plat_path_absolute(startPath, absolute, sizeof(absolute)))
                plat_path_dirname(absolute, start, sizeof(start));
        }
    }

    if (!start[0] && g_lastDir[filter][0])
        snprintf(start, sizeof(start), "%s", g_lastDir[filter]);

    if (!start[0] || !plat_is_dir(start))
    {
        if (!plat_getcwd(start, sizeof(start))) snprintf(start, sizeof(start), "/");
    }

    char absolute[DB_PATH_MAX];
    if (plat_path_absolute(start, absolute, sizeof(absolute)))
        snprintf(fd->dir, sizeof(fd->dir), "%s", absolute);
    else
        snprintf(fd->dir, sizeof(fd->dir), "%s", start);

    snprintf(fd->pathInput, sizeof(fd->pathInput), "%s", fd->dir);
    fd->reload = true;
}

void fd_free(FileDialog *fd)
{
    free(fd->entries);
    fd->entries = NULL;
    fd->entryCount = 0;
}

static void fd_entry_path(const FileDialog *fd, int index, char *out, size_t outSize)
{
    if (fd->drivesView)
        snprintf(out, outSize, "%s", fd->entries[index].name);
    else
        plat_path_join(out, outSize, fd->dir, fd->entries[index].name);
}

static void draw_row_icon(Ui *ui, Rectangle bounds, bool isDir, Color color, Color background)
{
    if (isDir) ui_draw_icon(ui, UI_ICON_FOLDER, bounds, color, background);
    else ui_draw_icon(ui, UI_ICON_FILE, bounds, color, background);
}

void fd_draw(FileDialog *fd, Ui *ui, LangId lang)
{
    if (!fd->open) return;
    if (fd->reload) fd_reload(fd);

    Theme *theme = &ui->theme;
    Vector2 mouse = GetMousePosition();

    float panelX = (WINDOW_W - FD_PANEL_W) * 0.5f;
    float panelY = (WINDOW_H - FD_PANEL_H) * 0.5f;
    Rectangle panel = { panelX, panelY, FD_PANEL_W, FD_PANEL_H };

    DrawRectangle(0, 0, WINDOW_W, WINDOW_H, theme->overlay);
    ui_panel(panel, 0.035f, theme->panel, theme->panelBorder, 1.2f);

    /* Header */
    ui_draw_text(ui, i18n(lang, STR_SEL_BUTTON), panel.x + 20.0f, panel.y + 16.0f, 17.0f,
                 theme->text, true);

    Rectangle closeBtn = { panel.x + panel.width - 44.0f, panel.y + 12.0f, 28.0f, 28.0f };
    if (ui_icon_button(ui, closeBtn, UI_ICON_CLOSE, theme->text))
    {
        fd->cancelled = true;
        fd->open = false;
        snprintf(g_lastDir[fd->filter], DB_PATH_MAX, "%s", fd->dir);
        return;
    }

    /* Path row */
    float rowY = panel.y + 52.0f;
    Rectangle upBtn = { panel.x + 16.0f, rowY, 34.0f, 32.0f };
    Rectangle homeBtn = { panel.x + 54.0f, rowY, 34.0f, 32.0f };

    if (ui_icon_button(ui, upBtn, UI_ICON_UP, theme->accent))
    {
        if (!fd->drivesView)
        {
            char parent[DB_PATH_MAX];
            plat_path_dirname(fd->dir, parent, sizeof(parent));
            if (strcmp(parent, fd->dir) == 0)
            {
                char roots[26][DB_PATH_MAX];
                if (plat_list_drives(roots, 26) > 0)
                {
                    fd->drivesView = true;
                    fd->reload = true;
                    fd->active = -1;
                    fd->selected[0] = '\0';
                    fd->scroll = 0;
                }
            }
            else
            {
                fd_navigate(fd, parent);
            }
        }
    }
    if (ui_icon_button(ui, homeBtn, UI_ICON_HOME, theme->accent))
    {
        char home[DB_PATH_MAX];
        if (plat_home_dir(home, sizeof(home))) fd_navigate(fd, home);
    }

    Rectangle pathBox = { panel.x + 96.0f, rowY, panel.width - 112.0f, 32.0f };
    GuiSetStyle(TEXTBOX, TEXT_PADDING, 8);
    bool wasEditingPath = fd->pathEditMode;
    if (GuiTextBox(pathBox, fd->pathInput, (int)sizeof(fd->pathInput), fd->pathEditMode))
        fd->pathEditMode = !fd->pathEditMode;
    if (wasEditingPath && IsKeyPressed(KEY_ENTER))
    {
        fd_apply_path_input(fd);
        if (!fd->open) return;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !CheckCollisionPointRec(mouse, pathBox))
        fd->pathEditMode = false;

    /* File list */
    Rectangle list = { panel.x + 16.0f, panel.y + 96.0f, panel.width - 32.0f, 302.0f };
    ui_panel(list, 0.02f, theme->fieldBg, theme->border, 1.0f);

    Rectangle listInner = { list.x + FD_LIST_PAD, list.y + FD_LIST_PAD,
                            list.width - FD_LIST_PAD * 2.0f - 6.0f,
                            list.height - FD_LIST_PAD * 2.0f };
    int visibleRows = (int)(listInner.height / FD_ROW_H);

    if (CheckCollisionPointRec(mouse, list))
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            fd->scroll -= (int)wheel * 2;
            if (fd->scroll < 0) fd->scroll = 0;
            int maxScroll = fd->entryCount - visibleRows;
            if (maxScroll < 0) maxScroll = 0;
            if (fd->scroll > maxScroll) fd->scroll = maxScroll;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, listInner))
        {
            int row = (int)((mouse.y - listInner.y) / FD_ROW_H);
            int index = fd->scroll + row;
            if (row >= 0 && row < visibleRows && index >= 0 && index < fd->entryCount)
            {
                double now = GetTime();
                bool doubleClick = (index == fd->lastClickIndex) && (now - fd->lastClickTime < 0.40);
                fd->lastClickIndex = index;
                fd->lastClickTime = now;
                fd->active = index;

                char full[DB_PATH_MAX];
                fd_entry_path(fd, index, full, sizeof(full));
                snprintf(fd->pathInput, sizeof(fd->pathInput), "%s", full);
                snprintf(fd->selected, sizeof(fd->selected), "%s", full);

                if (doubleClick)
                {
                    if (fd->entries[index].isDir) fd_navigate(fd, full);
                    else fd_accept(fd, full);
                    if (!fd->open) return;
                }
            }
        }
    }

    ui_scissor(listInner);
    for (int row = 0; row < visibleRows; row++)
    {
        int index = fd->scroll + row;
        if (index < 0 || index >= fd->entryCount) break;

        Rectangle rowRect = {
            listInner.x, listInner.y + (float)row * FD_ROW_H,
            listInner.width - 2.0f, FD_ROW_H
        };
        bool hovered = CheckCollisionPointRec(mouse, rowRect) && CheckCollisionPointRec(mouse, listInner);

        if (index == fd->active)
            ui_panel(rowRect, 0.18f, theme->selection, BLANK, 0);
        else if (hovered)
            ui_panel(rowRect, 0.18f, Fade(theme->accent, 0.08f), BLANK, 0);

        Color itemColor = fd->entries[index].isDir ? theme->accent : theme->text;
        Rectangle iconRect = { rowRect.x + 8.0f, rowRect.y + 5.0f, 16.0f, 16.0f };
        draw_row_icon(ui, iconRect, fd->entries[index].isDir, itemColor, theme->fieldBg);

        Rectangle nameRect = { rowRect.x + 30.0f, rowRect.y, rowRect.width - 36.0f, rowRect.height };
        ui_draw_text_clip(ui, fd->entries[index].name, nameRect, 13.0f, itemColor, false);
    }
    EndScissorMode();

    /* Scrollbar */
    if (fd->entryCount > visibleRows)
    {
        float trackX = list.x + list.width - 9.0f;
        float trackY = list.y + 6.0f;
        float trackH = list.height - 12.0f;
        float thumbH = fmaxf(24.0f, trackH * (float)visibleRows / (float)fd->entryCount);
        float progress = (fd->entryCount > visibleRows)
                             ? (float)fd->scroll / (float)(fd->entryCount - visibleRows)
                             : 0.0f;
        float thumbY = trackY + (trackH - thumbH) * progress;
        DrawRectangleRounded((Rectangle){ trackX, trackY, 4.0f, trackH }, 0.5f, 6,
                             Fade(theme->border, 0.35f));
        DrawRectangleRounded((Rectangle){ trackX, thumbY, 4.0f, thumbH }, 0.5f, 6,
                             Fade(theme->accent, 0.65f));
    }

    /* Footer */
    float footerY = panel.y + panel.height - 52.0f;
    Rectangle selectedRect = { panel.x + 20.0f, footerY + 8.0f, panel.width - 240.0f, 30.0f };
    if (fd->selected[0])
        ui_draw_text_clip_left(ui, fd->selected, selectedRect, 11.5f, theme->textDim, false);
    else
        ui_draw_text(ui, fd->dir, selectedRect.x, selectedRect.y + 8.0f, 11.5f, theme->textDim, false);

    Rectangle cancelBtn = { panel.x + panel.width - 20.0f - 100.0f, footerY, 100.0f, 34.0f };
    Rectangle openBtn = { cancelBtn.x - 12.0f - 100.0f, footerY, 100.0f, 34.0f };

    if (ui_button(ui, cancelBtn, i18n(lang, STR_CANCEL), true))
    {
        fd->cancelled = true;
        fd->open = false;
        snprintf(g_lastDir[fd->filter], DB_PATH_MAX, "%s", fd->dir);
        return;
    }

    if (ui_button(ui, openBtn, "OK", true))
    {
        if (fd->active >= 0 && fd->active < fd->entryCount)
        {
            char full[DB_PATH_MAX];
            fd_entry_path(fd, fd->active, full, sizeof(full));
            if (fd->entries[fd->active].isDir) fd_navigate(fd, full);
            else fd_accept(fd, full);
        }
        else
        {
            fd_apply_path_input(fd);
        }
        if (!fd->open) return;
    }

    if (IsKeyPressed(KEY_ESCAPE))
    {
        fd->cancelled = true;
        fd->open = false;
        snprintf(g_lastDir[fd->filter], DB_PATH_MAX, "%s", fd->dir);
        return;
    }

    if (IsKeyPressed(KEY_UP) && fd->entryCount > 0 && !fd->pathEditMode)
    {
        if (fd->active > 0) fd->active--;
        else if (fd->active < 0) fd->active = 0;
        if (fd->active < fd->scroll) fd->scroll = fd->active;
    }
    if (IsKeyPressed(KEY_DOWN) && fd->entryCount > 0 && !fd->pathEditMode)
    {
        if (fd->active < fd->entryCount - 1) fd->active++;
        else if (fd->active < 0) fd->active = 0;
        if (fd->active >= fd->scroll + visibleRows) fd->scroll = fd->active - visibleRows + 1;
    }
    if (IsKeyPressed(KEY_ENTER) && !fd->pathEditMode && fd->active >= 0 && fd->active < fd->entryCount)
    {
        char full[DB_PATH_MAX];
        fd_entry_path(fd, fd->active, full, sizeof(full));
        if (fd->entries[fd->active].isDir) fd_navigate(fd, full);
        else fd_accept(fd, full);
    }

    if (fd->open)
        snprintf(g_lastDir[fd->filter], DB_PATH_MAX, "%s", fd->dir);
}
