#include "platform.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Logging                                                                   */
/* ------------------------------------------------------------------------- */

static FILE *g_logFile = NULL;

void plat_log_init(void)
{
    char stamp[64];
    char name[128];
    plat_local_timestamp(stamp, sizeof(stamp), false);
    snprintf(name, sizeof(name), "dgc_%s.log", stamp);

    g_logFile = plat_fopen(name, "w");
    if (!g_logFile)
        fprintf(stderr, "Unable to create log file '%s'\n", name);
    else
        plat_log(PLAT_LOG_INFO, "DisplaceBox started (C/raylib port)");
}

void plat_log_close(void)
{
    if (g_logFile)
    {
        fflush(g_logFile);
        fclose(g_logFile);
        g_logFile = NULL;
    }
}

void plat_log(int level, const char *fmt, ...)
{
    static const char *names[] = { "DEBUG", "INFO", "WARNING", "ERROR" };
    char stamp[64];

    plat_local_timestamp(stamp, sizeof(stamp), true);

    va_list args;
    va_start(args, fmt);

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

    va_end(args);
}

/* ------------------------------------------------------------------------- */
/* Portable path helpers                                                     */
/* ------------------------------------------------------------------------- */

bool plat_path_join(char *out, size_t outSize, const char *a, const char *b)
{
    if (!b || !b[0])
    {
        if (snprintf(out, outSize, "%s", a) >= (int)outSize) return false;
        return true;
    }
    if (plat_path_is_absolute(b))
    {
        if (snprintf(out, outSize, "%s", b) >= (int)outSize) return false;
        return true;
    }
    if (!a || !a[0])
    {
        if (snprintf(out, outSize, "%s", b) >= (int)outSize) return false;
        return true;
    }

    size_t len = strlen(a);
    if (plat_is_sep(a[len - 1]))
        return snprintf(out, outSize, "%s%s", a, b) < (int)outSize;
    return snprintf(out, outSize, "%s%c%s", a, plat_path_sep(), b) < (int)outSize;
}

void plat_path_dirname(const char *path, char *out, size_t outSize)
{
    if (!path || !path[0])
    {
        snprintf(out, outSize, ".");
        return;
    }

    char buf[DB_PATH_MAX];
    snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);

    /* Trim trailing separators, but keep filesystem/drive roots intact. */
    while (len > 1 && plat_is_sep(buf[len - 1]))
    {
        if (len == 3 && buf[1] == ':') break;
        buf[--len] = '\0';
    }

    /* Windows drive root, e.g. "C:\" */
    if (len == 3 && buf[1] == ':' && plat_is_sep(buf[2]))
    {
        snprintf(out, outSize, "%s", buf);
        return;
    }

    /* POSIX root "/" */
    if (len == 1 && plat_is_sep(buf[0]))
    {
        snprintf(out, outSize, "%c", buf[0]);
        return;
    }

    char *last = NULL;
    for (size_t i = 0; i < len; i++)
        if (plat_is_sep(buf[i])) last = &buf[i];

    if (!last)
    {
        snprintf(out, outSize, ".");
        return;
    }

    /* "C:\file" -> "C:\" (not "C:") */
    if (last == buf + 2 && buf[1] == ':')
    {
        buf[3] = '\0';
        snprintf(out, outSize, "%s", buf);
        return;
    }

    if (last == buf)
    {
        snprintf(out, outSize, "%c", buf[0]);
        return;
    }

    *last = '\0';
    snprintf(out, outSize, "%s", buf);
}

const char *plat_path_basename(const char *path)
{
    if (!path) return "";
    const char *last = NULL;
    for (const char *p = path; *p; p++)
        if (plat_is_sep(*p)) last = p;
    return last ? last + 1 : path;
}

const char *plat_path_ext(const char *path)
{
    if (!path) return "";
    const char *base = plat_path_basename(path);
    const char *dot = strrchr(base, '.');
    if (!dot || dot == base) return "";
    return dot;
}

void plat_path_stem(const char *path, char *out, size_t outSize)
{
    const char *base = plat_path_basename(path);
    const char *dot = strrchr(base, '.');
    size_t len = (dot && dot != base) ? (size_t)(dot - base) : strlen(base);
    if (len >= outSize) len = outSize - 1;
    memcpy(out, base, len);
    out[len] = '\0';
}

void plat_path_parent_name(const char *path, char *out, size_t outSize)
{
    char dir[DB_PATH_MAX];
    plat_path_dirname(path, dir, sizeof(dir));

    if (strcmp(dir, ".") == 0)
    {
        snprintf(out, outSize, "%s", "");
        return;
    }

    const char *base = plat_path_basename(dir);
    if (!base[0])
    {
        snprintf(out, outSize, "%s", "");
        return;
    }
    snprintf(out, outSize, "%s", base);
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
        char dir[DB_PATH_MAX];
        if (!plat_path_join(dir, sizeof(dir), exe, prefixes[i])) continue;
        if (plat_path_join(out, outSize, dir, name) && plat_file_exists(out)) return true;
    }

    if (plat_path_join(out, outSize, "assets", name) && plat_file_exists(out)) return true;
    return false;
}

int plat_strcasecmp(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";

    for (;;)
    {
        unsigned char ca = (unsigned char)*a++;
        unsigned char cb = (unsigned char)*b++;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + ('a' - 'A'));
        if (ca != cb || ca == 0) return (int)ca - (int)cb;
    }
}
