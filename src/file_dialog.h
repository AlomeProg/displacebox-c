#ifndef DISPLACEBOX_FILE_DIALOG_H
#define DISPLACEBOX_FILE_DIALOG_H

#include "app.h"
#include "ui.h"

#include <stdbool.h>

typedef enum {
    FD_FILTER_BIN = 0,
    FD_FILTER_RAW_PNG,
    FD_FILTER_JSON
} FdFilter;

typedef struct {
    char name[256];
    bool isDir;
    long long size;
} FdEntry;

typedef struct {
    bool open;
    FdFilter filter;
    char dir[PATH_MAX];
    char pathInput[PATH_MAX];
    char selected[PATH_MAX];
    char result[PATH_MAX];
    FdEntry *entries;
    int entryCount;
    int scroll;
    int active;
    double lastClickTime;
    int lastClickIndex;
    bool pathEditMode;
    bool reload;
    bool accepted;
    bool cancelled;
} FileDialog;

void fd_open(FileDialog *fd, FdFilter filter, const char *startPath, LangId lang);
void fd_free(FileDialog *fd);
void fd_draw(FileDialog *fd, Ui *ui, LangId lang);

#endif /* DISPLACEBOX_FILE_DIALOG_H */
