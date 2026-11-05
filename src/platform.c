#define _GNU_SOURCE
#include "platform.h"

#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <X11/Xlib.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

/* ------------------------------------------------------------------------- */
/* Logging                                                                   */
/* ------------------------------------------------------------------------- */

static FILE *g_logFile = NULL;
static pthread_mutex_t g_logMutex = PTHREAD_MUTEX_INITIALIZER;

static void timestamp_now(char *out, size_t outSize, bool withMillis)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv;
    localtime_r(&ts.tv_sec, &tmv);
    if (withMillis)
    {
        snprintf(out, outSize, "%04d-%02d-%02d %02d:%02d:%02d,%03ld",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000L);
    }
    else
    {
        snprintf(out, outSize, "%04d-%02d-%02d_%02d-%02d-%02d",
                 tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    }
}

void plat_log_init(void)
{
    char stamp[64];
    char name[128];
    timestamp_now(stamp, sizeof(stamp), false);
    snprintf(name, sizeof(name), "dgc_%s.log", stamp);

    g_logFile = fopen(name, "w");
    if (!g_logFile)
    {
        fprintf(stderr, "Unable to create log file '%s': %s\n", name, strerror(errno));
    }
    else
    {
        plat_log(PLAT_LOG_INFO, "DisplaceBox started (C/raylib port)");
    }
}

void plat_log_close(void)
{
    pthread_mutex_lock(&g_logMutex);
    if (g_logFile)
    {
        fflush(g_logFile);
        fclose(g_logFile);
        g_logFile = NULL;
    }
    pthread_mutex_unlock(&g_logMutex);
}

void plat_log(int level, const char *fmt, ...)
{
    static const char *names[] = { "DEBUG", "INFO", "WARNING", "ERROR" };
    char stamp[64];
    timestamp_now(stamp, sizeof(stamp), true);

    va_list args;
    va_start(args, fmt);

    pthread_mutex_lock(&g_logMutex);

    if (g_logFile)
    {
        fprintf(g_logFile, "%s - %s - ", stamp, names[level]);
        va_list copy;
        va_copy(copy, args);
        vfprintf(g_logFile, fmt, copy);
        va_end(copy);
        fputc('\n', g_logFile);
        fflush(g_logFile);
    }

    fprintf(stderr, "%s - %s - ", stamp, names[level]);
    va_list copy2;
    va_copy(copy2, args);
    vfprintf(stderr, fmt, copy2);
    va_end(copy2);
    fputc('\n', stderr);

    pthread_mutex_unlock(&g_logMutex);
    va_end(args);
}

/* ------------------------------------------------------------------------- */
/* Paths                                                                     */
/* ------------------------------------------------------------------------- */

static char g_exeDir[PATH_MAX] = { 0 };

void plat_ignore_sigchld(void)
{
    signal(SIGCHLD, SIG_IGN);
}

const char *plat_exe_dir(void)
{
    if (g_exeDir[0]) return g_exeDir;

    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0)
    {
        buf[n] = '\0';
        char *slash = strrchr(buf, '/');
        if (slash) *slash = '\0';
        snprintf(g_exeDir, sizeof(g_exeDir), "%s", buf);
    }
    else
    {
        if (!getcwd(g_exeDir, sizeof(g_exeDir))) g_exeDir[0] = '\0';
    }
    return g_exeDir;
}

bool plat_path_join(char *out, size_t outSize, const char *a, const char *b)
{
    if (!b || !b[0])
    {
        if (snprintf(out, outSize, "%s", a) >= (int)outSize) return false;
        return true;
    }
    size_t len = strlen(a);
    if (len > 0 && a[len - 1] == '/')
        return snprintf(out, outSize, "%s%s", a, b) < (int)outSize;
    return snprintf(out, outSize, "%s/%s", a, b) < (int)outSize;
}

void plat_path_dirname(const char *path, char *out, size_t outSize)
{
    if (!path || !path[0])
    {
        snprintf(out, outSize, ".");
        return;
    }
    char buf[PATH_MAX];
    snprintf(buf, sizeof(buf), "%s", path);

    size_t len = strlen(buf);
    while (len > 1 && buf[len - 1] == '/') buf[--len] = '\0';

    char *slash = strrchr(buf, '/');
    if (!slash)
    {
        snprintf(out, outSize, ".");
        return;
    }
    if (slash == buf)
    {
        snprintf(out, outSize, "/");
        return;
    }
    *slash = '\0';
    snprintf(out, outSize, "%s", buf);
}

const char *plat_path_ext(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(path, '.');
    if (!dot || (slash && dot < slash)) return "";
    return dot;
}

const char *plat_path_basename(const char *path)
{
    if (!path) return "";
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

void plat_path_stem(const char *path, char *out, size_t outSize)
{
    const char *base = plat_path_basename(path);
    const char *dot = strrchr(base, '.');
    size_t len = dot ? (size_t)(dot - base) : strlen(base);
    if (len >= outSize) len = outSize - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

void plat_path_parent_name(const char *path, char *out, size_t outSize)
{
    /* Mimics Python's Path(path).parent.name: "." and "/" have no name. */
    char dir[PATH_MAX];
    plat_path_dirname(path, dir, sizeof(dir));
    if (strcmp(dir, "/") == 0 || strcmp(dir, ".") == 0)
    {
        snprintf(out, outSize, "%s", "");
        return;
    }
    snprintf(out, outSize, "%s", plat_path_basename(dir));
}

bool plat_path_is_absolute(const char *path)
{
    return path && path[0] == '/';
}

bool plat_path_absolute(const char *path, char *out, size_t outSize)
{
    if (!path || !path[0]) return false;

    char expanded[PATH_MAX];
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

    char resolved[PATH_MAX];
    if (realpath(expanded, resolved))
    {
        snprintf(out, outSize, "%s", resolved);
        return true;
    }

    /* Path may not exist: build an absolute path manually. */
    if (plat_path_is_absolute(expanded))
    {
        snprintf(out, outSize, "%s", expanded);
        return true;
    }

    char cwd[PATH_MAX];
    if (!getcwd(cwd, sizeof(cwd))) return false;
    return plat_path_join(out, outSize, cwd, expanded);
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

bool plat_asset_path(const char *name, char *out, size_t outSize)
{
    const char *env = getenv("DISPLACEBOX_ASSETS");
    if (env && env[0])
    {
        if (plat_path_join(out, outSize, env, name) && plat_file_exists(out)) return true;
    }

    const char *exe = plat_exe_dir();
    const char *prefixes[] = { "assets", "../assets", "../../assets" };
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
    {
        char dir[PATH_MAX];
        if (!plat_path_join(dir, sizeof(dir), exe, prefixes[i])) continue;
        if (plat_path_join(out, outSize, dir, name) && plat_file_exists(out)) return true;
    }

    if (plat_path_join(out, outSize, "assets", name) && plat_file_exists(out)) return true;
    return false;
}

/* ------------------------------------------------------------------------- */
/* Misc                                                                      */
/* ------------------------------------------------------------------------- */

static Display *g_x11Display = NULL;
static bool g_x11Checked = false;

bool plat_global_mouse(int *x, int *y)
{
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
}

void plat_platform_shutdown(void)
{
    if (g_x11Display)
    {
        XCloseDisplay(g_x11Display);
        g_x11Display = NULL;
    }
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
