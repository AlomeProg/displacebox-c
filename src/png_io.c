#include "png_io.h"
#include "platform.h"
#include "zlib_codec.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PNG_MAX_DIMENSION 32768

static const unsigned char pngSignature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };

/* Small CRC-32 implementation (same polynomial as zlib/PNG). */
static uint32_t g_crcTable[256];
static bool g_crcTableReady = false;

static void crc32_init(void)
{
    if (g_crcTableReady) return;
    for (uint32_t i = 0; i < 256; i++)
    {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        g_crcTable[i] = c;
    }
    g_crcTableReady = true;
}

static uint32_t crc32_update(uint32_t crc, const unsigned char *data, size_t length)
{
    crc32_init();
    crc = ~crc;
    for (size_t i = 0; i < length; i++)
        crc = g_crcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static uint32_t read_be32(const unsigned char *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void write_be32(unsigned char *p, uint32_t value)
{
    p[0] = (unsigned char)(value >> 24);
    p[1] = (unsigned char)(value >> 16);
    p[2] = (unsigned char)(value >> 8);
    p[3] = (unsigned char)value;
}

static unsigned char *read_file(const char *path, size_t *outSize)
{
    FILE *f = plat_fopen(path, "rb");
    if (!f) return NULL;

    if (fseek(f, 0, SEEK_END) != 0)
    {
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || fseek(f, 0, SEEK_SET) != 0)
    {
        fclose(f);
        return NULL;
    }

    unsigned char *data = (unsigned char *)malloc((size_t)size + 1);
    if (!data)
    {
        fclose(f);
        return NULL;
    }

    size_t got = fread(data, 1, (size_t)size, f);
    fclose(f);
    if (got != (size_t)size)
    {
        free(data);
        return NULL;
    }

    data[size] = 0;
    *outSize = (size_t)size;
    return data;
}

static int paeth_predictor(int a, int b, int c)
{
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    if (pa <= pb && pa <= pc) return a;
    if (pb <= pc) return b;
    return c;
}

PngStatus png_read_gray16(const char *path, uint16_t **outPixels, int *outW, int *outH)
{
    *outPixels = NULL;
    *outW = 0;
    *outH = 0;

    size_t fileSize = 0;
    unsigned char *file = read_file(path, &fileSize);
    if (!file) return PNGIO_ERR_IO;

    if (fileSize < 8 || memcmp(file, pngSignature, 8) != 0)
    {
        free(file);
        return PNGIO_ERR_FORMAT;
    }

    int width = 0, height = 0;
    int bitDepth = 0, colorType = -1, compression = -1, filterMethod = -1, interlace = -1;
    unsigned char *idat = NULL;
    size_t idatSize = 0, idatCap = 0;
    bool sawIhdr = false, sawIend = false;

    size_t pos = 8;
    while (pos + 12 <= fileSize)
    {
        uint32_t length = read_be32(file + pos);
        const unsigned char *type = file + pos + 4;
        if ((size_t)length > fileSize || pos + 12 + (size_t)length > fileSize)
        {
            free(idat);
            free(file);
            return PNGIO_ERR_FORMAT;
        }
        const unsigned char *chunk = file + pos + 8;

        if (memcmp(type, "IHDR", 4) == 0)
        {
            if (length != 13)
            {
                free(idat);
                free(file);
                return PNGIO_ERR_FORMAT;
            }
            width = (int)read_be32(chunk);
            height = (int)read_be32(chunk + 4);
            bitDepth = chunk[8];
            colorType = chunk[9];
            compression = chunk[10];
            filterMethod = chunk[11];
            interlace = chunk[12];
            sawIhdr = true;

            if (width <= 0 || height <= 0 || width > PNG_MAX_DIMENSION || height > PNG_MAX_DIMENSION)
            {
                free(idat);
                free(file);
                return PNGIO_ERR_FORMAT;
            }
            if ((long long)width * (long long)height > 268435456LL)
            {
                free(idat);
                free(file);
                return PNGIO_ERR_FORMAT;
            }
            if (bitDepth != 16 || colorType != 0)
            {
                free(idat);
                free(file);
                return PNGIO_ERR_NOT_GRAY16;
            }
            if (compression != 0 || filterMethod != 0)
            {
                free(idat);
                free(file);
                return PNGIO_ERR_FORMAT;
            }
            if (interlace != 0)
            {
                free(idat);
                free(file);
                return PNGIO_ERR_FORMAT;
            }
        }
        else if (memcmp(type, "IDAT", 4) == 0)
        {
            if (!sawIhdr)
            {
                free(idat);
                free(file);
                return PNGIO_ERR_FORMAT;
            }
            if (idatSize + length > idatCap)
            {
                size_t newCap = idatCap ? idatCap : 65536;
                while (newCap < idatSize + length) newCap *= 2;
                unsigned char *grown = (unsigned char *)realloc(idat, newCap);
                if (!grown)
                {
                    free(idat);
                    free(file);
                    return PNGIO_ERR_MEMORY;
                }
                idat = grown;
                idatCap = newCap;
            }
            memcpy(idat + idatSize, chunk, length);
            idatSize += length;
        }
        else if (memcmp(type, "IEND", 4) == 0)
        {
            sawIend = true;
            break;
        }

        pos += 12 + (size_t)length;
    }

    if (!sawIhdr || !sawIend || idatSize == 0)
    {
        free(idat);
        free(file);
        return PNGIO_ERR_FORMAT;
    }

    size_t rowBytes = (size_t)width * 2;
    size_t stride = rowBytes + 1;
    size_t expected = stride * (size_t)height;
    unsigned char *raw = (unsigned char *)malloc(expected);
    uint16_t *pixels = (uint16_t *)malloc((size_t)width * (size_t)height * sizeof(uint16_t));
    if (!raw || !pixels)
    {
        free(raw);
        free(pixels);
        free(idat);
        free(file);
        return PNGIO_ERR_MEMORY;
    }

    long inflated = zlib_decompress(idat, idatSize, raw, expected);
    free(idat);
    if (inflated != (long)expected)
    {
        free(raw);
        free(pixels);
        free(file);
        return PNGIO_ERR_FORMAT;
    }

    unsigned char *prev = NULL;
    for (int y = 0; y < height; y++)
    {
        unsigned char *row = raw + (size_t)y * stride;
        unsigned char filter = row[0];
        unsigned char *cur = row + 1;

        switch (filter)
        {
            case 0:
                break;
            case 1:
                for (size_t x = 2; x < rowBytes; x++)
                    cur[x] = (unsigned char)(cur[x] + cur[x - 2]);
                break;
            case 2:
                if (prev)
                    for (size_t x = 0; x < rowBytes; x++)
                        cur[x] = (unsigned char)(cur[x] + prev[x]);
                break;
            case 3:
                for (size_t x = 0; x < rowBytes; x++)
                {
                    int a = (x >= 2) ? cur[x - 2] : 0;
                    int b = prev ? prev[x] : 0;
                    cur[x] = (unsigned char)(cur[x] + ((a + b) >> 1));
                }
                break;
            case 4:
                for (size_t x = 0; x < rowBytes; x++)
                {
                    int a = (x >= 2) ? cur[x - 2] : 0;
                    int b = prev ? prev[x] : 0;
                    int c = (prev && x >= 2) ? prev[x - 2] : 0;
                    cur[x] = (unsigned char)(cur[x] + paeth_predictor(a, b, c));
                }
                break;
            default:
                free(raw);
                free(pixels);
                free(file);
                return PNGIO_ERR_FORMAT;
        }

        for (int x = 0; x < width; x++)
            pixels[(size_t)y * (size_t)width + (size_t)x] =
                (uint16_t)(((uint16_t)cur[x * 2] << 8) | (uint16_t)cur[x * 2 + 1]);

        prev = cur;
    }

    free(raw);
    free(file);
    *outPixels = pixels;
    *outW = width;
    *outH = height;
    return PNGIO_OK;
}

static bool write_chunk(FILE *f, const char *type, const unsigned char *data, uint32_t length)
{
    unsigned char header[8];
    write_be32(header, length);
    memcpy(header + 4, type, 4);
    if (fwrite(header, 1, 8, f) != 8) return false;

    if (length > 0 && fwrite(data, 1, length, f) != length) return false;

    uint32_t crc = crc32_update(0, (const unsigned char *)type, 4);
    if (length > 0) crc = crc32_update(crc, data, length);

    unsigned char crcbuf[4];
    write_be32(crcbuf, crc);
    return fwrite(crcbuf, 1, 4, f) == 4;
}

PngStatus png_write_gray16(const char *path, const uint16_t *pixels, int width, int height)
{
    if (width <= 0 || height <= 0 || width > PNG_MAX_DIMENSION || height > PNG_MAX_DIMENSION)
        return PNGIO_ERR_FORMAT;

    size_t rowBytes = (size_t)width * 2;
    size_t stride = rowBytes + 1;
    size_t rawSize = stride * (size_t)height;
    unsigned char *raw = (unsigned char *)malloc(rawSize);
    if (!raw) return PNGIO_ERR_MEMORY;

    for (int y = 0; y < height; y++)
    {
        unsigned char *row = raw + (size_t)y * stride;
        row[0] = 0; /* filter: none */
        for (int x = 0; x < width; x++)
        {
            uint16_t v = pixels[(size_t)y * (size_t)width + (size_t)x];
            row[1 + x * 2] = (unsigned char)(v >> 8);
            row[2 + x * 2] = (unsigned char)(v & 0xFF);
        }
    }

    size_t compressedCap = zlib_bound(rawSize);
    unsigned char *compressed = (unsigned char *)malloc(compressedCap);
    if (!compressed)
    {
        free(raw);
        return PNGIO_ERR_MEMORY;
    }

    long compressedSize = zlib_compress(raw, rawSize, compressed, compressedCap);
    free(raw);
    if (compressedSize <= 0)
    {
        free(compressed);
        return PNGIO_ERR_IO;
    }

    FILE *f = plat_fopen(path, "wb");
    if (!f)
    {
        free(compressed);
        return PNGIO_ERR_IO;
    }

    unsigned char ihdr[13];
    write_be32(ihdr, (uint32_t)width);
    write_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 16;  /* bit depth */
    ihdr[9] = 0;   /* color type: grayscale */
    ihdr[10] = 0;  /* compression */
    ihdr[11] = 0;  /* filter */
    ihdr[12] = 0;  /* interlace */

    bool ok = fwrite(pngSignature, 1, 8, f) == 8;
    ok = ok && write_chunk(f, "IHDR", ihdr, 13);
    ok = ok && write_chunk(f, "IDAT", compressed, (uint32_t)compressedSize);
    ok = ok && write_chunk(f, "IEND", NULL, 0);

    free(compressed);
    if (fclose(f) != 0) ok = false;
    return ok ? PNGIO_OK : PNGIO_ERR_IO;
}
