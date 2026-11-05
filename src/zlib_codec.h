#ifndef DISPLACEBOX_ZLIB_CODEC_H
#define DISPLACEBOX_ZLIB_CODEC_H

#include <stddef.h>

/* Minimal zlib-stream helpers used by the PNG codec.
 * The implementation lives in zlib_codec.c and is built from the
 * single-header sdefl/sinfl libraries shipped with raylib (MIT/public domain). */

size_t zlib_bound(size_t sourceSize);
long zlib_compress(const unsigned char *src, size_t srcSize,
                   unsigned char *dst, size_t dstCapacity);
long zlib_decompress(const unsigned char *src, size_t srcSize,
                     unsigned char *dst, size_t dstCapacity);

#endif /* DISPLACEBOX_ZLIB_CODEC_H */
