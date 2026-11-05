#define _GNU_SOURCE
#include "updater.h"
#include "platform.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define UPDATE_API_URL \
    "https://api.github.com/repos/stakanyash/displacebin_gui_converter/releases/latest"

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool extract_json_string(const char *json, const char *key, char *out, size_t outSize)
{
    char needle[96];
    snprintf(needle, sizeof(needle), "\"%s\"", key);

    const char *found = strstr(json, needle);
    if (!found) return false;
    found += strlen(needle);
    while (*found && isspace((unsigned char)*found)) found++;
    if (*found != ':') return false;
    found++;
    while (*found && isspace((unsigned char)*found)) found++;
    if (*found != '"') return false;
    found++;

    size_t written = 0;
    while (*found && *found != '"')
    {
        unsigned char c = (unsigned char)*found++;
        if (c == '\\')
        {
            char esc = *found++;
            switch (esc)
            {
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'u':
                {
                    int h0 = hex_value(found[0]);
                    int h1 = hex_value(found[1]);
                    int h2 = hex_value(found[2]);
                    int h3 = hex_value(found[3]);
                    if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) return false;
                    found += 4;
                    unsigned cp = (unsigned)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                    if (cp < 0x80) c = (unsigned char)cp;
                    else if (cp < 0x800)
                    {
                        if (written + 2 >= outSize) return false;
                        out[written++] = (char)(0xC0 | (cp >> 6));
                        out[written++] = (char)(0x80 | (cp & 0x3F));
                        continue;
                    }
                    else
                    {
                        if (written + 3 >= outSize) return false;
                        out[written++] = (char)(0xE0 | (cp >> 12));
                        out[written++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[written++] = (char)(0x80 | (cp & 0x3F));
                        continue;
                    }
                    break;
                }
                default:
                    c = (unsigned char)esc;
                    break;
            }
        }

        if (written + 1 >= outSize) break;
        out[written++] = (char)c;
    }

    out[written] = '\0';
    return true;
}

static void parse_version(const char *text, int parts[3])
{
    parts[0] = parts[1] = parts[2] = 0;
    while (*text && !isdigit((unsigned char)*text)) text++;
    for (int i = 0; i < 3 && *text; i++)
    {
        char *end = NULL;
        parts[i] = (int)strtol(text, &end, 10);
        if (!end || end == text) break;
        text = end;
        if (*text == '.') text++;
        else break;
    }
}

static bool version_is_newer(const char *latest, const char *current)
{
    int a[3], b[3];
    parse_version(latest, a);
    parse_version(current, b);
    for (int i = 0; i < 3; i++)
    {
        if (a[i] > b[i]) return true;
        if (a[i] < b[i]) return false;
    }
    return false;
}

static void strip_markdown(const char *in, char *out, size_t outSize)
{
    size_t o = 0;
    const char *p = in;

    while (*p && o + 8 < outSize)
    {
        const char *end = strchr(p, '\n');
        if (!end) end = p + strlen(p);

        char line[2048];
        size_t li = 0;
        for (const char *q = p; q < end && li + 8 < sizeof(line); q++)
        {
            char c = *q;
            if (c == '#' || c == '*' || c == '`' || c == '>') continue;
            if (c == '\r') continue;
            if (c == '-')
            {
                line[li++] = (char)0xE2;
                line[li++] = (char)0x80;
                line[li++] = (char)0xA2;
                continue;
            }
            line[li++] = c;
        }
        line[li] = '\0';

        char *start = line;
        while (*start == ' ' || *start == '\t') start++;
        size_t len = strlen(start);
        while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) start[--len] = '\0';

        if (len > 0)
        {
            if (o + len + 2 >= outSize) break;
            memcpy(out + o, start, len);
            o += len;
            out[o++] = '\n';
        }

        p = (*end) ? end + 1 : end;
    }

    if (o > 0) o--;
    out[o] = '\0';
}

static void *updater_thread(void *argument)
{
    Updater *updater = (Updater *)argument;

    char command[512];
    snprintf(command, sizeof(command),
             "curl -sL --max-time 10 -H \"Accept: application/vnd.github.v3+json\" "
             "-A \"DisplaceBox-C\" \"%s\" 2>/dev/null",
             UPDATE_API_URL);

    log_info("Checking for updates...");
    FILE *pipe = popen(command, "r");
    if (!pipe)
    {
        log_warning("Update check: unable to run curl");
        pthread_mutex_lock(&updater->mutex);
        updater->done = true;
        pthread_mutex_unlock(&updater->mutex);
        return NULL;
    }

    size_t capacity = 65536;
    size_t length = 0;
    char *response = (char *)malloc(capacity);
    if (!response)
    {
        pclose(pipe);
        pthread_mutex_lock(&updater->mutex);
        updater->done = true;
        pthread_mutex_unlock(&updater->mutex);
        return NULL;
    }

    while (!feof(pipe) && length + 4096 < capacity)
    {
        size_t got = fread(response + length, 1, capacity - length - 1, pipe);
        length += got;
        if (got == 0) break;
    }
    response[length] = '\0';
    int exitCode = pclose(pipe);

    if (exitCode != 0 || length == 0)
    {
        log_warning("Update check failed (network unavailable or curl missing)");
        free(response);
        pthread_mutex_lock(&updater->mutex);
        updater->done = true;
        pthread_mutex_unlock(&updater->mutex);
        return NULL;
    }

    char tag[64] = { 0 };
    if (!extract_json_string(response, "tag_name", tag, sizeof(tag)) || !tag[0])
    {
        log_warning("Update check: unable to parse release tag");
        free(response);
        pthread_mutex_lock(&updater->mutex);
        updater->done = true;
        pthread_mutex_unlock(&updater->mutex);
        return NULL;
    }

    bool newer = version_is_newer(tag, updater->version);
    log_info("Latest release: %s (current: %s)", tag, updater->version);

    if (newer)
    {
        char body[8192] = { 0 };
        char stripped[8192] = { 0 };
        if (extract_json_string(response, "body", body, sizeof(body)))
            strip_markdown(body, stripped, sizeof(stripped));

        pthread_mutex_lock(&updater->mutex);
        snprintf(updater->version, sizeof(updater->version), "%s", tag);
        if (tag[0] == 'v' || tag[0] == 'V')
            memmove(updater->version, updater->version + 1, strlen(updater->version));
        snprintf(updater->body, sizeof(updater->body), "%s", stripped);
        updater->available = true;
        updater->done = true;
        pthread_mutex_unlock(&updater->mutex);
    }
    else
    {
        pthread_mutex_lock(&updater->mutex);
        updater->done = true;
        pthread_mutex_unlock(&updater->mutex);
    }

    free(response);
    return NULL;
}

void updater_start(Updater *updater, const char *currentVersion)
{
    memset(updater, 0, sizeof(*updater));
    pthread_mutex_init(&updater->mutex, NULL);
    snprintf(updater->version, sizeof(updater->version), "%s", currentVersion);

    if (pthread_create(&updater->thread, NULL, updater_thread, updater) != 0)
    {
        log_warning("Update check: unable to start worker thread");
        return;
    }
    pthread_detach(updater->thread);
    updater->started = true;
}

bool updater_poll(Updater *updater, char *version, size_t versionSize, char *body, size_t bodySize)
{
    if (!updater->started) return false;

    bool result = false;
    pthread_mutex_lock(&updater->mutex);
    if (updater->done && updater->available)
    {
        snprintf(version, versionSize, "%s", updater->version);
        snprintf(body, bodySize, "%s", updater->body);
        updater->available = false;
        result = true;
    }
    pthread_mutex_unlock(&updater->mutex);
    return result;
}

void updater_join(Updater *updater)
{
    if (!updater->started) return;

    bool finished = false;
    for (int i = 0; i < 100; i++)
    {
        pthread_mutex_lock(&updater->mutex);
        finished = updater->done;
        pthread_mutex_unlock(&updater->mutex);
        if (finished) break;
        usleep(10000);
    }

    if (finished) pthread_mutex_destroy(&updater->mutex);
    updater->started = false;
}
