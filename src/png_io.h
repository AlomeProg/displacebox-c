#ifndef DISPLACEBOX_PNG_IO_H
#define DISPLACEBOX_PNG_IO_H

#include <stdint.h>

typedef enum {
    PNGIO_OK = 0,
    PNGIO_ERR_IO,
    PNGIO_ERR_FORMAT,     /* corrupt or structurally unsupported PNG */
    PNGIO_ERR_NOT_GRAY16, /* valid PNG, but not 16-bit grayscale */
    PNGIO_ERR_MEMORY
} PngStatus;

/* Reads a non-interlaced 16-bit grayscale PNG. Allocates *outPixels (w*h
 * samples, free() by caller) on success. */
PngStatus png_read_gray16(const char *path, uint16_t **outPixels, int *outW, int *outH);

/* Writes a non-interlaced 16-bit grayscale PNG. */
PngStatus png_write_gray16(const char *path, const uint16_t *pixels, int width, int height);

#endif /* DISPLACEBOX_PNG_IO_H */
