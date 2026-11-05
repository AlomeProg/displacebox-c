#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#include <stdlib.h>
#include <string.h>

int displacebox_main(int argc, char **argv);

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

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR commandLine, int showCommand)
{
    (void)instance;
    (void)previous;
    (void)commandLine;
    (void)showCommand;

    int wideArgc = 0;
    wchar_t **wideArgs = CommandLineToArgvW(GetCommandLineW(), &wideArgc);

    char **args = NULL;
    int argc = 0;
    if (wideArgs && wideArgc > 0)
    {
        args = (char **)calloc((size_t)wideArgc + 1, sizeof(char *));
        if (args)
        {
            argc = wideArgc;
            for (int i = 0; i < wideArgc; i++)
            {
                args[i] = wide_to_utf8(wideArgs[i]);
                if (!args[i])
                {
                    args[i] = (char *)calloc(1, 1);
                    if (!args[i])
                    {
                        argc = i;
                        break;
                    }
                }
            }
        }
    }

    int result = displacebox_main(argc, args);

    if (args)
    {
        for (int i = 0; i < argc; i++) free(args[i]);
        free(args);
    }
    if (wideArgs) LocalFree(wideArgs);

    return result;
}
