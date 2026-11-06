#ifndef DISPLACEBOX_PLATFORM_H
#define DISPLACEBOX_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#ifndef DB_PATH_MAX
#define DB_PATH_MAX 4096
#endif

/* ---- logging ---------------------------------------------------------- */
enum { PLAT_LOG_DEBUG = 0, PLAT_LOG_INFO, PLAT_LOG_WARNING, PLAT_LOG_ERROR };

void plat_log_init(void);
void plat_log_close(void);
void plat_log(int level, const char *fmt, ...);
void plat_local_timestamp(char *buffer, size_t size, bool withMillis);

#define log_debug(...)   plat_log(PLAT_LOG_DEBUG, __VA_ARGS__)
#define log_info(...)    plat_log(PLAT_LOG_INFO, __VA_ARGS__)
#define log_warning(...) plat_log(PLAT_LOG_WARNING, __VA_ARGS__)
#define log_error(...)   plat_log(PLAT_LOG_ERROR, __VA_ARGS__)

/* ---- paths ------------------------------------------------------------ */
char plat_path_sep(void);
bool plat_is_sep(char c);

const char *plat_exe_dir(void);
bool plat_home_dir(char *out, size_t outSize);
int plat_list_drives(char roots[][DB_PATH_MAX], int maxRoots);
bool plat_asset_path(const char *name, char *out, size_t outSize);
bool plat_path_join(char *out, size_t outSize, const char *a, const char *b);
void plat_path_dirname(const char *path, char *out, size_t outSize);
const char *plat_path_ext(const char *path);        /* includes dot, points into path */
const char *plat_path_basename(const char *path);
void plat_path_stem(const char *path, char *out, size_t outSize);
void plat_path_parent_name(const char *path, char *out, size_t outSize);
bool plat_path_absolute(const char *path, char *out, size_t outSize);
bool plat_path_is_absolute(const char *path);
int plat_strcasecmp(const char *a, const char *b);

bool plat_file_exists(const char *path);
bool plat_is_dir(const char *path);
long long plat_file_size(const char *path);
bool plat_getcwd(char *out, size_t outSize);
FILE *plat_fopen(const char *path, const char *mode);

/* ---- directory iteration --------------------------------------------- */
typedef struct PlatDir PlatDir;

typedef struct {
    char name[1024];
    bool isDir;
    long long size;
} PlatDirEntry;

bool plat_dir_open(PlatDir **dir, const char *path);
bool plat_dir_next(PlatDir *dir, PlatDirEntry *entry);
void plat_dir_close(PlatDir *dir);

/* ---- OS integration --------------------------------------------------- */
bool plat_open_url(const char *url);
const char *plat_system_language(void);             /* "En", "Ru", "Uk", "Be", "Pl" */
bool plat_global_mouse(int *x, int *y);
void plat_ignore_sigchld(void);
void plat_platform_shutdown(void);

#endif /* DISPLACEBOX_PLATFORM_H */
