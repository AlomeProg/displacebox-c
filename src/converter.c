#include "app.h"
#include "converter.h"
#include "platform.h"
#include "png_io.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ------------------------------------------------------------------------- */
/* Small helpers                                                             */
/* ------------------------------------------------------------------------- */

static void set_error(char *buf, size_t size, const char *fmt, ...)
{
    if (!buf || size == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, size, fmt, args);
    va_end(args);
}

static unsigned char *read_entire_file(const char *path, size_t *outSize)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)length + 1);
    if (!data)
    {
        fclose(f);
        return NULL;
    }

    size_t got = fread(data, 1, (size_t)length, f);
    fclose(f);
    if (got != (size_t)length)
    {
        free(data);
        return NULL;
    }

    data[length] = 0;
    *outSize = (size_t)length;
    return data;
}

static float read_f32_le(const unsigned char *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
}

static void write_f32_le(unsigned char *p, float value)
{
    uint32_t u;
    memcpy(&u, &value, sizeof(u));
    p[0] = (unsigned char)(u & 0xFF);
    p[1] = (unsigned char)((u >> 8) & 0xFF);
    p[2] = (unsigned char)((u >> 16) & 0xFF);
    p[3] = (unsigned char)((u >> 24) & 0xFF);
}

static void format_double(double value, char *out, size_t size)
{
    if (isnan(value))
    {
        snprintf(out, size, "NaN");
        return;
    }
    if (isinf(value))
    {
        snprintf(out, size, value < 0 ? "-Infinity" : "Infinity");
        return;
    }

    for (int precision = 15; precision <= 17; precision++)
    {
        snprintf(out, size, "%.*g", precision, value);
        char *end = NULL;
        double back = strtod(out, &end);
        if (end && *end == '\0' && back == value) break;
    }

    if (!strpbrk(out, ".eE"))
    {
        size_t len = strlen(out);
        if (len + 2 < size)
        {
            out[len] = '.';
            out[len + 1] = '0';
            out[len + 2] = '\0';
        }
    }
}

/* ------------------------------------------------------------------------- */
/* Metadata JSON                                                             */
/* ------------------------------------------------------------------------- */

typedef struct {
    const char *p;
} JsonParser;

static void json_skip_ws(JsonParser *jp)
{
    while (*jp->p && isspace((unsigned char)*jp->p)) jp->p++;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool json_parse_string(JsonParser *jp, char *out, size_t outSize)
{
    if (*jp->p != '"') return false;
    jp->p++;

    size_t written = 0;
    while (*jp->p && *jp->p != '"')
    {
        unsigned char c = (unsigned char)*jp->p++;
        if (c == '\\')
        {
            char esc = *jp->p++;
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
                    int h0 = hex_value(jp->p[0]);
                    int h1 = hex_value(jp->p[1]);
                    int h2 = hex_value(jp->p[2]);
                    int h3 = hex_value(jp->p[3]);
                    if (h0 < 0 || h1 < 0 || h2 < 0 || h3 < 0) return false;
                    jp->p += 4;
                    unsigned cp = (unsigned)((h0 << 12) | (h1 << 8) | (h2 << 4) | h3);
                    if (cp < 0x80) c = (unsigned char)cp;
                    else if (cp < 0x800)
                    {
                        if (!out) continue;
                        if (written + 2 >= outSize) return false;
                        out[written++] = (char)(0xC0 | (cp >> 6));
                        out[written++] = (char)(0x80 | (cp & 0x3F));
                        continue;
                    }
                    else
                    {
                        if (!out) continue;
                        if (written + 3 >= outSize) return false;
                        out[written++] = (char)(0xE0 | (cp >> 12));
                        out[written++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        out[written++] = (char)(0x80 | (cp & 0x3F));
                        continue;
                    }
                    break;
                }
                default:
                    return false;
            }
        }
        if (out && outSize > 0)
        {
            if (written + 1 >= outSize) return false;
            out[written++] = (char)c;
        }
    }

    if (*jp->p != '"') return false;
    jp->p++;
    if (out && outSize > 0) out[written] = '\0';
    return true;
}

static bool json_skip_value(JsonParser *jp);

static bool json_skip_container(JsonParser *jp, char open, char close)
{
    if (*jp->p != open) return false;
    jp->p++;
    json_skip_ws(jp);
    if (*jp->p == close)
    {
        jp->p++;
        return true;
    }

    for (;;)
    {
        json_skip_ws(jp);
        if (*jp->p == '"')
        {
            if (!json_parse_string(jp, NULL, 0)) return false;
        }
        else if (!json_skip_value(jp)) return false;

        json_skip_ws(jp);
        if (*jp->p == ',')
        {
            jp->p++;
            continue;
        }
        if (*jp->p == close)
        {
            jp->p++;
            return true;
        }
        return false;
    }
}

static bool json_skip_value(JsonParser *jp)
{
    json_skip_ws(jp);
    char c = *jp->p;
    if (c == '"') return json_parse_string(jp, NULL, 0);
    if (c == '{') return json_skip_container(jp, '{', '}');
    if (c == '[') return json_skip_container(jp, '[', ']');

    if (c == '-' || c == '+' || c == '.' || isdigit((unsigned char)c) ||
        strncmp(jp->p, "NaN", 3) == 0 || strncmp(jp->p, "Infinity", 8) == 0 ||
        strncmp(jp->p, "-Infinity", 9) == 0)
    {
        char *end = NULL;
        (void)strtod(jp->p, &end);
        if (!end || end == jp->p) return false;
        jp->p = end;
        return true;
    }

    if (strncmp(jp->p, "true", 4) == 0) { jp->p += 4; return true; }
    if (strncmp(jp->p, "false", 5) == 0) { jp->p += 5; return true; }
    if (strncmp(jp->p, "null", 4) == 0) { jp->p += 4; return true; }
    return false;
}

MetaStatus metadata_read(const char *path, Metadata *meta, char *missingKeys, size_t missingSize)
{
    memset(meta, 0, sizeof(*meta));
    if (missingKeys && missingSize) missingKeys[0] = '\0';

    size_t size = 0;
    unsigned char *data = read_entire_file(path, &size);
    if (!data) return META_ERR_IO;

    JsonParser jp = { (const char *)data };
    json_skip_ws(&jp);
    if (*jp.p != '{')
    {
        free(data);
        return META_ERR_PARSE;
    }
    jp.p++;

    bool failed = false;
    for (;;)
    {
        json_skip_ws(&jp);
        if (*jp.p == '}')
        {
            jp.p++;
            break;
        }
        if (*jp.p == '\0')
        {
            failed = true;
            break;
        }

        char key[64];
        if (!json_parse_string(&jp, key, sizeof(key)))
        {
            failed = true;
            break;
        }

        json_skip_ws(&jp);
        if (*jp.p != ':')
        {
            failed = true;
            break;
        }
        jp.p++;
        json_skip_ws(&jp);

        if (strcmp(key, "Min") == 0 || strcmp(key, "Max") == 0 || strcmp(key, "Delta") == 0)
        {
            char *end = NULL;
            double value = strtod(jp.p, &end);
            if (!end || end == jp.p)
            {
                failed = true;
                break;
            }
            jp.p = end;
            if (strcmp(key, "Min") == 0) { meta->min = value; meta->hasMin = true; }
            else if (strcmp(key, "Max") == 0) { meta->max = value; meta->hasMax = true; }
            else { meta->delta = value; meta->hasDelta = true; }
        }
        else if (strcmp(key, "DGCVer") == 0)
        {
            if (!json_parse_string(&jp, meta->dgcver, sizeof(meta->dgcver)))
            {
                failed = true;
                break;
            }
            meta->hasDgcver = true;
        }
        else if (!json_skip_value(&jp))
        {
            failed = true;
            break;
        }

        json_skip_ws(&jp);
        if (*jp.p == ',') { jp.p++; continue; }
        if (*jp.p == '}') { jp.p++; break; }
        failed = true;
        break;
    }

    free(data);
    if (failed) return META_ERR_PARSE;

    if (!meta->hasMin || !meta->hasMax || !meta->hasDelta)
    {
        if (missingKeys && missingSize)
        {
            size_t used = 0;
            const char *names[3];
            int count = 0;
            if (!meta->hasMin) names[count++] = "Min";
            if (!meta->hasMax) names[count++] = "Max";
            if (!meta->hasDelta) names[count++] = "Delta";
            for (int i = 0; i < count; i++)
            {
                int n = snprintf(missingKeys + used, missingSize - used, "%s%s",
                                 i ? ", " : "", names[i]);
                if (n < 0 || (size_t)n >= missingSize - used) break;
                used += (size_t)n;
            }
        }
        return META_ERR_MISSING;
    }

    return META_OK;
}

bool metadata_has_required(const Metadata *meta)
{
    return meta && meta->hasMin && meta->hasMax && meta->hasDelta;
}

/* ------------------------------------------------------------------------- */
/* Conversion                                                                */
/* ------------------------------------------------------------------------- */

static ConvStatus write_raw_u16(const char *path, const uint16_t *values, size_t count)
{
    FILE *f = fopen(path, "wb");
    if (!f) return CONV_ERR_OPEN_OUTPUT;

    unsigned char buffer[8192];
    size_t written = 0;
    bool ok = true;
    while (written < count)
    {
        size_t chunk = count - written;
        if (chunk > sizeof(buffer) / 2) chunk = sizeof(buffer) / 2;
        for (size_t i = 0; i < chunk; i++)
        {
            uint16_t v = values[written + i];
            buffer[i * 2] = (unsigned char)(v & 0xFF);
            buffer[i * 2 + 1] = (unsigned char)(v >> 8);
        }
        if (fwrite(buffer, 2, chunk, f) != chunk)
        {
            ok = false;
            break;
        }
        written += chunk;
    }

    if (fclose(f) != 0) ok = false;
    return ok ? CONV_OK : CONV_ERR_IO;
}

ConvStatus convert_forward(const char *inputPath, const char *outputPath, int size,
                           bool toPng, bool saveMetadata, ConvStats *stats,
                           char *metadataPathOut, size_t metadataPathSize,
                           char *errorOut, size_t errorOutSize)
{
    if (metadataPathOut && metadataPathSize) metadataPathOut[0] = '\0';
    if (errorOut && errorOutSize) errorOut[0] = '\0';
    if (!stats) return CONV_ERR_IO;

    if (size <= 0)
    {
        set_error(errorOut, errorOutSize, "Invalid map size: %d", size);
        return CONV_ERR_INPUT_SIZE;
    }

    size_t count = (size_t)size * (size_t)size;
    size_t expectedBytes = count * 4;

    size_t inputSize = 0;
    unsigned char *input = read_entire_file(inputPath, &inputSize);
    if (!input)
    {
        set_error(errorOut, errorOutSize, "Unable to open input file: %s", inputPath);
        return CONV_ERR_OPEN_INPUT;
    }

    if (inputSize != expectedBytes)
    {
        set_error(errorOut, errorOutSize,
                  "Input size mismatch: expected %zu bytes, got %zu (map size %d)",
                  expectedBytes, inputSize, size);
        free(input);
        return CONV_ERR_INPUT_SIZE;
    }

    float *values = (float *)malloc(count * sizeof(float));
    uint16_t *normalized = (uint16_t *)malloc(count * sizeof(uint16_t));
    if (!values || !normalized)
    {
        free(values);
        free(normalized);
        free(input);
        return CONV_ERR_MEMORY;
    }

    double minimum = (double)read_f32_le(input);
    double maximum = minimum;
    for (size_t i = 0; i < count; i++)
    {
        values[i] = read_f32_le(input + i * 4);
        double v = (double)values[i];
        if (v < minimum) minimum = v;
        if (v > maximum) maximum = v;
    }
    free(input);

    double delta = maximum - minimum;

    if (delta == 0.0)
    {
        log_warning("All values are identical. Writing flat white map (0xFFFF).");
        for (size_t i = 0; i < count; i++) normalized[i] = 0xFFFF;
    }
    else
    {
        for (size_t i = 0; i < count; i++)
        {
            double relative = ((double)values[i] - minimum) / delta;
            double scaled = relative * 65535.0;
            long v = (long)scaled;
            if (v < 0) v = 0;
            if (v > 65535) v = 65535;
            normalized[i] = (uint16_t)v;
        }
    }

    free(values);

    ConvStatus status;
    if (toPng)
    {
        PngStatus png = png_write_gray16(outputPath, normalized, size, size);
        if (png == PNGIO_OK) status = CONV_OK;
        else if (png == PNGIO_ERR_IO) status = CONV_ERR_OPEN_OUTPUT;
        else if (png == PNGIO_ERR_MEMORY) status = CONV_ERR_MEMORY;
        else status = CONV_ERR_PNG_WRITE;
    }
    else
    {
        status = write_raw_u16(outputPath, normalized, count);
    }

    free(normalized);

    stats->min = minimum;
    stats->max = maximum;
    stats->delta = delta;

    if (status != CONV_OK)
    {
        set_error(errorOut, errorOutSize, "Unable to write output file: %s", outputPath);
        return status;
    }

    if (saveMetadata && metadataPathOut && metadataPathSize)
    {
        char dir[PATH_MAX];
        char mapName[256];
        char fileName[512];
        plat_path_dirname(outputPath, dir, sizeof(dir));
        plat_path_parent_name(outputPath, mapName, sizeof(mapName));
        snprintf(fileName, sizeof(fileName), "displace_%s_metadata.json", mapName);

        if (!plat_path_join(metadataPathOut, metadataPathSize, dir, fileName))
        {
            metadataPathOut[0] = '\0';
        }
        else
        {
            char fmin[64], fmax[64], fdelta[64];
            format_double(minimum, fmin, sizeof(fmin));
            format_double(maximum, fmax, sizeof(fmax));
            format_double(delta, fdelta, sizeof(fdelta));

            FILE *meta = fopen(metadataPathOut, "wb");
            if (!meta)
            {
                set_error(errorOut, errorOutSize, "Unable to write metadata: %s", metadataPathOut);
                metadataPathOut[0] = '\0';
            }
            else
            {
                fprintf(meta,
                        "{\n"
                        "  \"Min\": %s,\n"
                        "  \"Max\": %s,\n"
                        "  \"Delta\": %s,\n"
                        "  \"DGCVer\": \"%s\"\n"
                        "}",
                        fmin, fmax, fdelta, DGCVER);
                if (fclose(meta) != 0)
                {
                    set_error(errorOut, errorOutSize, "Unable to write metadata: %s", metadataPathOut);
                    metadataPathOut[0] = '\0';
                }
                else
                {
                    log_info("Metadata saved to: %s", metadataPathOut);
                }
            }
        }
    }

    return CONV_OK;
}

static ConvStatus read_pixels(const char *inputPath, uint16_t **pixels, int *width, int *height,
                              size_t *count, char *errorOut, size_t errorOutSize)
{
    const char *ext = plat_path_ext(inputPath);
    if (strcasecmp(ext, ".raw") == 0)
    {
        size_t size = 0;
        unsigned char *data = read_entire_file(inputPath, &size);
        if (!data)
        {
            set_error(errorOut, errorOutSize, "Unable to open input file: %s", inputPath);
            return CONV_ERR_OPEN_INPUT;
        }
        if (size == 0)
        {
            free(data);
            set_error(errorOut, errorOutSize, "Input .raw file contains no data.");
            return CONV_ERR_RAW_EMPTY;
        }
        if (size % 2 != 0)
        {
            free(data);
            set_error(errorOut, errorOutSize, "Input .raw file has incomplete 16-bit data.");
            return CONV_ERR_RAW_INCOMPLETE;
        }

        size_t n = size / 2;
        uint16_t *values = (uint16_t *)malloc(n * sizeof(uint16_t));
        if (!values)
        {
            free(data);
            return CONV_ERR_MEMORY;
        }
        for (size_t i = 0; i < n; i++)
            values[i] = (uint16_t)(data[i * 2] | ((uint16_t)data[i * 2 + 1] << 8));

        free(data);
        *pixels = values;
        *count = n;
        *width = 0;
        *height = 0;
        return CONV_OK;
    }

    if (strcasecmp(ext, ".png") == 0)
    {
        PngStatus png = png_read_gray16(inputPath, pixels, width, height);
        switch (png)
        {
            case PNGIO_OK:
                *count = (size_t)(*width) * (size_t)(*height);
                return CONV_OK;
            case PNGIO_ERR_NOT_GRAY16:
                set_error(errorOut, errorOutSize, "Input PNG must be 16-bit integer grayscale.");
                return CONV_ERR_PNG_NOT16;
            case PNGIO_ERR_IO:
                set_error(errorOut, errorOutSize, "Unable to read PNG file: %s", inputPath);
                return CONV_ERR_PNG_READ;
            case PNGIO_ERR_MEMORY:
                return CONV_ERR_MEMORY;
            default:
                set_error(errorOut, errorOutSize, "PNG file is corrupted or unsupported: %s", inputPath);
                return CONV_ERR_PNG_READ;
        }
    }

    set_error(errorOut, errorOutSize, "Unsupported input format: %s", inputPath);
    return CONV_ERR_UNSUPPORTED;
}

ConvStatus convert_reverse(const char *inputPath, const char *outputPath,
                           const Metadata *meta, ConvStats *stats,
                           char *errorOut, size_t errorOutSize)
{
    if (errorOut && errorOutSize) errorOut[0] = '\0';
    if (!meta || !metadata_has_required(meta))
    {
        set_error(errorOut, errorOutSize, "Missing required metadata fields.");
        return CONV_ERR_META_MISSING;
    }

    double minimum = meta->min;
    double maximum = meta->max;
    double delta = meta->delta;

    if (isnan(minimum) || isnan(maximum) || isnan(delta))
    {
        set_error(errorOut, errorOutSize, "Invalid values detected: Min, Max or Delta is NaN.");
        return CONV_ERR_META_NAN;
    }

    if (fabs(minimum) < 1e-7 && fabs(maximum) < 1e-7 && fabs(delta) < 1e-7)
    {
        set_error(errorOut, errorOutSize, "Invalid values detected: Min and Max is zero.");
        return CONV_ERR_META_ZERO;
    }

    uint16_t *pixels = NULL;
    int width = 0, height = 0;
    size_t count = 0;
    ConvStatus status = read_pixels(inputPath, &pixels, &width, &height, &count, errorOut, errorOutSize);
    if (status != CONV_OK) return status;

    size_t bytes = count * 4;
    unsigned char *output = (unsigned char *)malloc(bytes ? bytes : 1);
    if (!output)
    {
        free(pixels);
        return CONV_ERR_MEMORY;
    }

    if (delta == 0.0)
    {
        log_warning("Delta is zero. Restoring flat value of min.");
        for (size_t i = 0; i < count; i++)
            write_f32_le(output + i * 4, (float)minimum);
    }
    else
    {
        for (size_t i = 0; i < count; i++)
        {
            double value = ((double)pixels[i] / 65535.0) * delta + minimum;
            write_f32_le(output + i * 4, (float)value);
        }
    }
    free(pixels);

    FILE *f = fopen(outputPath, "wb");
    if (!f)
    {
        free(output);
        set_error(errorOut, errorOutSize, "Unable to open output file: %s", outputPath);
        return CONV_ERR_OPEN_OUTPUT;
    }
    bool ok = fwrite(output, 1, bytes, f) == bytes;
    if (fclose(f) != 0) ok = false;
    free(output);

    if (!ok)
    {
        set_error(errorOut, errorOutSize, "Unable to write output file: %s", outputPath);
        return CONV_ERR_IO;
    }

    if (stats)
    {
        stats->min = minimum;
        stats->max = maximum;
        stats->delta = delta;
    }
    return CONV_OK;
}

const char *conv_status_string(ConvStatus status)
{
    switch (status)
    {
        case CONV_OK: return "ok";
        case CONV_ERR_OPEN_INPUT: return "cannot open input";
        case CONV_ERR_INPUT_SIZE: return "input size mismatch";
        case CONV_ERR_OPEN_OUTPUT: return "cannot open output";
        case CONV_ERR_MEMORY: return "out of memory";
        case CONV_ERR_PNG_WRITE: return "PNG write error";
        case CONV_ERR_PNG_READ: return "PNG read error";
        case CONV_ERR_PNG_NOT16: return "PNG is not 16-bit grayscale";
        case CONV_ERR_RAW_EMPTY: return "raw file is empty";
        case CONV_ERR_RAW_INCOMPLETE: return "raw file has incomplete 16-bit data";
        case CONV_ERR_UNSUPPORTED: return "unsupported format";
        case CONV_ERR_META_MISSING: return "metadata is incomplete";
        case CONV_ERR_META_NAN: return "metadata contains NaN";
        case CONV_ERR_META_ZERO: return "metadata min/max are zero";
        case CONV_ERR_IO: return "I/O error";
        default: return "unknown error";
    }
}
