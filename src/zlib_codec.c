#include "zlib_codec.h"

#include <limits.h>
#include <stdlib.h>

/* Rename the public sdefl/sinfl symbols: raylib already embeds its own copy
 * of these headers in rcore.o, and duplicate global symbols break linking. */
#define sdeflate       db_sdeflate
#define zsdeflate      db_zsdeflate
#define sdefl_bound    db_sdefl_bound
#define sinflate       db_sinflate
#define zsinflate      db_zsinflate

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#define SDEFL_IMPLEMENTATION
#include "external/sdefl.h"

#define SINFL_IMPLEMENTATION
#include "external/sinfl.h"

size_t zlib_bound(size_t sourceSize)
{
    if (sourceSize > (size_t)INT_MAX) return 0;
    return (size_t)sdefl_bound((int)sourceSize);
}

long zlib_compress(const unsigned char *src, size_t srcSize,
                   unsigned char *dst, size_t dstCapacity)
{
    if (!src || !dst || srcSize > (size_t)INT_MAX || dstCapacity > (size_t)INT_MAX)
        return -1;

    struct sdefl *state = (struct sdefl *)calloc(1, sizeof(struct sdefl));
    if (!state) return -1;

    int size = zsdeflate(state, dst, src, (int)srcSize, 8);
    free(state);

    if (size <= 0 || (size_t)size > dstCapacity) return -1;
    return (long)size;
}

long zlib_decompress(const unsigned char *src, size_t srcSize,
                     unsigned char *dst, size_t dstCapacity)
{
    if (!src || !dst || srcSize > (size_t)INT_MAX || dstCapacity > (size_t)INT_MAX)
        return -1;

    int size = zsinflate(dst, (int)dstCapacity, src, (int)srcSize);
    return (long)size;
}
