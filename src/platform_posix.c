#define _GNU_SOURCE
#include "platform.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#define DB_HAS_X11 1
#include <X11/Xlib.h>
#endif

char plat_path_sep(void) { return '/'; }

bool plat_is_sep(char c) { return c == '/'; }

void plat_local_timestamp(char *buffer, size_t size, bool withMillis)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);

    if (withMillis)
        snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d,%03ld",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000L);
    else
        snprintf(buffer, size, "%04d-%02d-%02d_%02d-%02d-%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

FILE *plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

bool plat_path_is_absolute(const char *path)
{
    return path && path[0] == '/';
}

bool plat_file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

bool plat_is_dir(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

long long plat_file_size(const char *path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0) return -1;
    return (long long)st.st_size;
}

bool plat_getcwd(char *out, size_t outSize)
{
    return getcwd(out, outSize) != NULL;
}

static char g_exeDir[DB_PATH_MAX];

const char *plat_exe_dir(void)
{
    if (g_exeDir[0]) return g_exeDir;

    char buf[DB_PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0)
    {
        buf[n] = '\0';
        char *slash = strrchr(buf, '/');
        if (slash) *slash = '\0';
        snprintf(g_exeDir, sizeof(g_exeDir), "%s", buf);
    }
    else if (!getcwd(g_exeDir, sizeof(g_exeDir)))
    {
        g_exeDir[0] = '\0';
    }
    return g_exeDir;
}

bool plat_home_dir(char *out, size_t outSize)
{
    const char *home = getenv("HOME");
    if (!home || !home[0]) return false;
    return snprintf(out, outSize, "%s", home) < (int)outSize;
}

bool plat_path_absolute(const char *path, char *out, size_t outSize)
{
    if (!path || !path[0]) return false;

    char expanded[DB_PATH_MAX];
    if (path[0] == '~' && (path[1] == '/' || path[1] == '\0'))
    {
        const char *home = getenv("HOME");
        if (!home) home = "/";
        snprintf(expanded, sizeof(expanded), "%s%s", home, path + 1);
    }
    else
    {
        snprintf(expanded, sizeof(expanded), "%s", path);
    }

    char resolved[DB_PATH_MAX];
    if (realpath(expanded, resolved))
    {
        snprintf(out, outSize, "%s", resolved);
        return true;
    }

    if (plat_path_is_absolute(expanded))
    {
        snprintf(out, outSize, "%s", expanded);
        return true;
    }

    char cwd[DB_PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return false;
    return plat_path_join(out, outSize, cwd, expanded);
}

/* ------------------------------------------------------------------------- */
/* Directory iteration                                                       */
/* ------------------------------------------------------------------------- */

struct PlatDir {
    DIR *dir;
    char path[DB_PATH_MAX];
};

bool plat_dir_open(PlatDir **out, const char *path)
{
    if (!out || !path) return false;
    DIR *dir = opendir(path);
    if (!dir) return false;

    PlatDir *result = (PlatDir *)calloc(1, sizeof(PlatDir));
    if (!result)
    {
        closedir(dir);
        return false;
    }
    result->dir = dir;
    snprintf(result->path, sizeof(result->path), "%s", path);
    *out = result;
    return true;
}

bool plat_dir_next(PlatDir *dir, PlatDirEntry *entry)
{
    if (!dir || !entry) return false;

    struct dirent *item;
    while ((item = readdir(dir->dir)) != NULL)
    {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;

        char full[DB_PATH_MAX];
        if (!plat_path_join(full, sizeof(full), dir->path, item->d_name)) continue;

        struct stat st;
        if (stat(full, &st) != 0) continue;

        snprintf(entry->name, sizeof(entry->name), "%s", item->d_name);
        entry->isDir = S_ISDIR(st.st_mode);
        entry->size = (long long)st.st_size;
        return true;
    }
    return false;
}

void plat_dir_close(PlatDir *dir)
{
    if (!dir) return;
    if (dir->dir) closedir(dir->dir);
    free(dir);
}

bool plat_open_url(const char *url)
{
    if (!url || !url[0]) return false;

    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0)
    {
        freopen("/dev/null", "w", stderr);
        execlp("xdg-open", "xdg-open", url, (char *)NULL);
        _exit(127);
    }
    return true;
}

const char *plat_system_language(void)
{
    setlocale(LC_ALL, "");

    const char *env = getenv("LC_ALL");
    if (!env || !env[0]) env = getenv("LC_MESSAGES");
    if (!env || !env[0]) env = getenv("LANG");
    if (!env || !env[0]) env = setlocale(LC_MESSAGES, NULL);

    if (!env || !env[0] || strcmp(env, "C") == 0 || strcmp(env, "POSIX") == 0)
        return "En";

    char code[3] = { 0 };
    code[0] = (char)((env[0] >= 'A' && env[0] <= 'Z') ? env[0] + 32 : env[0]);
    code[1] = env[1] ? (char)((env[1] >= 'A' && env[1] <= 'Z') ? env[1] + 32 : env[1]) : 0;

    if (strcmp(code, "ru") == 0) return "Ru";
    if (strcmp(code, "uk") == 0) return "Uk";
    if (strcmp(code, "be") == 0) return "Be";
    if (strcmp(code, "pl") == 0) return "Pl";
    if (strcmp(code, "en") == 0) return "En";
    return "En";
}

void plat_ignore_sigchld(void)
{
    signal(SIGCHLD, SIG_IGN);
}

static Display *g_x11Display = NULL;
static bool g_x11Checked = false;

bool plat_global_mouse(int *x, int *y)
{
#if defined(DB_HAS_X11)
    if (!g_x11Checked)
    {
        g_x11Checked = true;
        g_x11Display = XOpenDisplay(NULL);
        if (!g_x11Display) log_warning("X11 display unavailable; window dragging will use a fallback");
    }
    if (!g_x11Display) return false;

    Window root = DefaultRootWindow(g_x11Display);
    Window rootReturn, childReturn;
    int rootX = 0, rootY = 0, winX = 0, winY = 0;
    unsigned int mask = 0;
    if (!XQueryPointer(g_x11Display, root, &rootReturn, &childReturn,
                       &rootX, &rootY, &winX, &winY, &mask))
        return false;

    if (x) *x = rootX;
    if (y) *y = rootY;
    return true;
#else
    (void)x;
    (void)y;
    return false;
#endif
}

void plat_platform_shutdown(void)
{
#if defined(DB_HAS_X11)
    if (g_x11Display)
    {
        XCloseDisplay(g_x11Display);
        g_x11Display = NULL;
    }
#endif
}
