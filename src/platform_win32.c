#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#include "platform.h"

#include <windows.h>
#include <shellapi.h>

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <wctype.h>

char plat_path_sep(void) { return '\\'; }

bool plat_is_sep(char c) { return c == '\\' || c == '/'; }

/* ------------------------------------------------------------------------- */
/* UTF-8 <-> UTF-16                                                          */
/* ------------------------------------------------------------------------- */

static wchar_t *utf8_to_wide(const char *text)
{
    if (!text) return NULL;
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (length <= 0) return NULL;

    wchar_t *result = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!result) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result, length) <= 0)
    {
        free(result);
        return NULL;
    }
    return result;
}

static char *wide_to_utf8(const wchar_t *text)
{
    if (!text) return NULL;
    int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (length <= 0) return NULL;

    char *result = (char *)malloc((size_t)length);
    if (!result) return NULL;
    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, result, length, NULL, NULL) <= 0)
    {
        free(result);
        return NULL;
    }
    return result;
}

/* Windows APIs are limited to MAX_PATH unless the path is prefixed. */
static wchar_t *utf8_to_wide_path(const char *path)
{
    wchar_t *wide = utf8_to_wide(path);
    if (!wide) return NULL;

    size_t length = wcslen(wide);
    if (length < 248 || !plat_path_is_absolute(path)) return wide;

    bool unc = (wide[0] == L'\\' && wide[1] == L'\\');
    const wchar_t *prefix = unc ? L"\\\\?\\UNC\\" : L"\\\\?\\";
    size_t prefixLength = wcslen(prefix);
    size_t skip = unc ? 2 : 0;

    wchar_t *extended = (wchar_t *)malloc((prefixLength + length - skip + 1) * sizeof(wchar_t));
    if (!extended) return wide;

    wcscpy(extended, prefix);
    wcscat(extended, wide + skip);
    free(wide);
    return extended;
}

/* ------------------------------------------------------------------------- */
/* Paths / files                                                             */
/* ------------------------------------------------------------------------- */

void plat_local_timestamp(char *buffer, size_t size, bool withMillis)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    if (withMillis)
        snprintf(buffer, size, "%04d-%02d-%02d %02d:%02d:%02d,%03d",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                 st.wMilliseconds);
    else
        snprintf(buffer, size, "%04d-%02d-%02d_%02d-%02d-%02d",
                 st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

FILE *plat_fopen(const char *path, const char *mode)
{
    wchar_t *widePath = utf8_to_wide_path(path);
    if (!widePath) return NULL;

    wchar_t wideMode[16];
    size_t i = 0;
    for (; mode[i] && i < 15; i++) wideMode[i] = (wchar_t)(unsigned char)mode[i];
    wideMode[i] = L'\0';

    FILE *file = _wfopen(widePath, wideMode);
    free(widePath);
    return file;
}

bool plat_path_is_absolute(const char *path)
{
    if (!path || !path[0]) return false;
    if (path[0] == '\\' || path[0] == '/') return true;
    if (isalpha((unsigned char)path[0]) && path[1] == ':')
    {
        char next = path[2];
        return next == '\\' || next == '/' || next == '\0';
    }
    return false;
}

bool plat_home_dir(char *out, size_t outSize)
{
    wchar_t buffer[32768];
    DWORD length = GetEnvironmentVariableW(L"USERPROFILE", buffer,
                                           (DWORD)(sizeof(buffer) / sizeof(buffer[0])));
    if (length == 0 || length >= (DWORD)(sizeof(buffer) / sizeof(buffer[0])))
    {
        wchar_t drive[16] = { 0 };
        wchar_t path[32768] = { 0 };
        if (GetEnvironmentVariableW(L"HOMEDRIVE", drive, 16) == 0 ||
            GetEnvironmentVariableW(L"HOMEPATH", path, 32768) == 0)
            return false;
        if (swprintf(buffer, 32768, L"%ls%ls", drive, path) < 0) return false;
    }

    char *utf8 = wide_to_utf8(buffer);
    if (!utf8) return false;
    bool ok = snprintf(out, outSize, "%s", utf8) < (int)outSize;
    free(utf8);
    return ok;
}

int plat_list_drives(char roots[][DB_PATH_MAX], int maxRoots)
{
    DWORD mask = GetLogicalDrives();
    int count = 0;
    for (int i = 0; i < 26 && count < maxRoots; i++)
    {
        if (mask & (1u << i))
        {
            snprintf(roots[count], DB_PATH_MAX, "%c:\\", 'A' + i);
            count++;
        }
    }
    return count;
}

bool plat_path_absolute(const char *path, char *out, size_t outSize)
{
    wchar_t *wide = utf8_to_wide(path);
    if (!wide) return false;

    DWORD length = GetFullPathNameW(wide, 0, NULL, NULL);
    if (length == 0)
    {
        free(wide);
        return false;
    }

    wchar_t *full = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!full)
    {
        free(wide);
        return false;
    }

    bool ok = false;
    if (GetFullPathNameW(wide, length, full, NULL) > 0)
    {
        char *utf8 = wide_to_utf8(full);
        if (utf8)
        {
            ok = snprintf(out, outSize, "%s", utf8) < (int)outSize;
            free(utf8);
        }
    }

    free(full);
    free(wide);
    return ok;
}

static DWORD file_attributes(const char *path)
{
    wchar_t *wide = utf8_to_wide_path(path);
    if (!wide) return INVALID_FILE_ATTRIBUTES;
    DWORD attributes = GetFileAttributesW(wide);
    free(wide);
    return attributes;
}

bool plat_file_exists(const char *path)
{
    return file_attributes(path) != INVALID_FILE_ATTRIBUTES;
}

bool plat_is_dir(const char *path)
{
    DWORD attributes = file_attributes(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
}

long long plat_file_size(const char *path)
{
    wchar_t *wide = utf8_to_wide_path(path);
    if (!wide) return -1;

    WIN32_FILE_ATTRIBUTE_DATA data;
    BOOL ok = GetFileAttributesExW(wide, GetFileExInfoStandard, &data);
    free(wide);
    if (!ok) return -1;

    return ((long long)data.nFileSizeHigh << 32) | (long long)data.nFileSizeLow;
}

bool plat_getcwd(char *out, size_t outSize)
{
    DWORD length = GetCurrentDirectoryW(0, NULL);
    if (length == 0) return false;

    wchar_t *buffer = (wchar_t *)malloc((size_t)length * sizeof(wchar_t));
    if (!buffer) return false;

    bool ok = false;
    if (GetCurrentDirectoryW(length, buffer) > 0)
    {
        char *utf8 = wide_to_utf8(buffer);
        if (utf8)
        {
            ok = snprintf(out, outSize, "%s", utf8) < (int)outSize;
            free(utf8);
        }
    }

    free(buffer);
    return ok;
}

static char g_exeDir[DB_PATH_MAX];

const char *plat_exe_dir(void)
{
    if (g_exeDir[0]) return g_exeDir;

    wchar_t buffer[32768];
    DWORD length = GetModuleFileNameW(NULL, buffer, (DWORD)(sizeof(buffer) / sizeof(buffer[0])));
    if (length > 0 && length < (DWORD)(sizeof(buffer) / sizeof(buffer[0])))
    {
        wchar_t *slash = wcsrchr(buffer, L'\\');
        if (!slash) slash = wcsrchr(buffer, L'/');
        if (slash) *slash = L'\0';

        char *utf8 = wide_to_utf8(buffer);
        if (utf8)
        {
            snprintf(g_exeDir, sizeof(g_exeDir), "%s", utf8);
            free(utf8);
        }
    }

    if (!g_exeDir[0] && !plat_getcwd(g_exeDir, sizeof(g_exeDir)))
        g_exeDir[0] = '\0';

    return g_exeDir;
}

/* ------------------------------------------------------------------------- */
/* Directory iteration                                                       */
/* ------------------------------------------------------------------------- */

struct PlatDir {
    HANDLE handle;
    WIN32_FIND_DATAW data;
    bool first;
    bool empty;
};

bool plat_dir_open(PlatDir **out, const char *path)
{
    if (!out || !path) return false;

    char pattern[DB_PATH_MAX];
    size_t length = strlen(path);
    if (length > 0 && plat_is_sep(path[length - 1]))
        snprintf(pattern, sizeof(pattern), "%s*", path);
    else
        snprintf(pattern, sizeof(pattern), "%s%c*", path, plat_path_sep());

    wchar_t *widePattern = utf8_to_wide_path(pattern);
    if (!widePattern) return false;

    PlatDir *dir = (PlatDir *)calloc(1, sizeof(PlatDir));
    if (!dir)
    {
        free(widePattern);
        return false;
    }

    dir->handle = FindFirstFileW(widePattern, &dir->data);
    free(widePattern);

    if (dir->handle == INVALID_HANDLE_VALUE)
    {
        DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
        {
            dir->empty = true;
            *out = dir;
            return true;
        }
        free(dir);
        return false;
    }

    dir->first = true;
    *out = dir;
    return true;
}

bool plat_dir_next(PlatDir *dir, PlatDirEntry *entry)
{
    if (!dir || !entry || dir->empty) return false;

    for (;;)
    {
        if (!dir->first)
        {
            if (!FindNextFileW(dir->handle, &dir->data)) return false;
        }
        dir->first = false;

        if (wcscmp(dir->data.cFileName, L".") == 0 ||
            wcscmp(dir->data.cFileName, L"..") == 0)
            continue;

        if (dir->data.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM))
            continue;

        char *name = wide_to_utf8(dir->data.cFileName);
        if (!name) continue;

        snprintf(entry->name, sizeof(entry->name), "%s", name);
        free(name);

        entry->isDir = (dir->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        entry->size = ((long long)dir->data.nFileSizeHigh << 32) |
                      (long long)dir->data.nFileSizeLow;
        return true;
    }
}

void plat_dir_close(PlatDir *dir)
{
    if (!dir) return;
    if (dir->handle != INVALID_HANDLE_VALUE) FindClose(dir->handle);
    free(dir);
}

/* ------------------------------------------------------------------------- */
/* OS integration                                                            */
/* ------------------------------------------------------------------------- */

bool plat_open_url(const char *url)
{
    wchar_t *wide = utf8_to_wide(url);
    if (!wide) return false;

    HINSTANCE result = ShellExecuteW(NULL, L"open", wide, NULL, NULL, SW_SHOWNORMAL);
    free(wide);
    return (INT_PTR)result > 32;
}

const char *plat_system_language(void)
{
    wchar_t locale[LOCALE_NAME_MAX_LENGTH];
    if (GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH) > 0)
    {
        wchar_t a = (wchar_t)towlower(locale[0]);
        wchar_t b = (wchar_t)towlower(locale[1]);
        if (a == L'\0') return "En";
        if (a == L'r' && b == L'u') return "Ru";
        if (a == L'u' && b == L'k') return "Uk";
        if (a == L'b' && b == L'e') return "Be";
        if (a == L'p' && b == L'l') return "Pl";
        return "En";
    }

    LANGID language = GetUserDefaultUILanguage();
    switch (PRIMARYLANGID(language))
    {
        case LANG_RUSSIAN: return "Ru";
        case LANG_UKRAINIAN: return "Uk";
        case LANG_BELARUSIAN: return "Be";
        case LANG_POLISH: return "Pl";
        default: return "En";
    }
}

void plat_ignore_sigchld(void)
{
    /* No child processes are used on Windows. */
}

bool plat_global_mouse(int *x, int *y)
{
    POINT point;
    if (!GetCursorPos(&point)) return false;
    if (x) *x = point.x;
    if (y) *y = point.y;
    return true;
}

void plat_platform_shutdown(void)
{
}
