#ifndef DISPLACEBOX_PLATFORM_H
#define DISPLACEBOX_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* ---- logging ---------------------------------------------------------- */
enum { PLAT_LOG_DEBUG = 0, PLAT_LOG_INFO, PLAT_LOG_WARNING, PLAT_LOG_ERROR };

void plat_log_init(void);
void plat_log_close(void);
void plat_log(int level, const char *fmt, ...);

#define log_debug(...)   plat_log(PLAT_LOG_DEBUG, __VA_ARGS__)
#define log_info(...)    plat_log(PLAT_LOG_INFO, __VA_ARGS__)
#define log_warning(...) plat_log(PLAT_LOG_WARNING, __VA_ARGS__)
#define log_error(...)   plat_log(PLAT_LOG_ERROR, __VA_ARGS__)

/* ---- paths ------------------------------------------------------------ */
const char *plat_exe_dir(void);
bool plat_asset_path(const char *name, char *out, size_t outSize);
bool plat_path_join(char *out, size_t outSize, const char *a, const char *b);
void plat_path_dirname(const char *path, char *out, size_t outSize);
const char *plat_path_ext(const char *path);        /* includes dot, points into path */
const char *plat_path_basename(const char *path);
void plat_path_stem(const char *path, char *out, size_t outSize);
void plat_path_parent_name(const char *path, char *out, size_t outSize);
bool plat_path_absolute(const char *path, char *out, size_t outSize);
bool plat_path_is_absolute(const char *path);

bool plat_file_exists(const char *path);
bool plat_is_dir(const char *path);
long long plat_file_size(const char *path);
bool plat_getcwd(char *out, size_t outSize);

/* ---- misc ------------------------------------------------------------- */
bool plat_open_url(const char *url);
const char *plat_system_language(void);             /* "En", "Ru", "Uk", "Be", "Pl" */
void plat_ignore_sigchld(void);

/* Absolute (screen) pointer position, used for stable window dragging.
 * Returns false when no X11 display is available (e.g. native Wayland). */
bool plat_global_mouse(int *x, int *y);
void plat_platform_shutdown(void);

#endif /* DISPLACEBOX_PLATFORM_H */
