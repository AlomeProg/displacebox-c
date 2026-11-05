#ifndef DISPLACEBOX_CONVERTER_H
#define DISPLACEBOX_CONVERTER_H

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    bool hasMin, hasMax, hasDelta;
    double min, max, delta;
    bool hasDgcver;
    char dgcver[64];
} Metadata;

typedef struct {
    double min, max, delta;
} ConvStats;

typedef enum {
    META_OK = 0,
    META_ERR_IO,
    META_ERR_PARSE,
    META_ERR_MISSING
} MetaStatus;

typedef enum {
    CONV_OK = 0,
    CONV_ERR_OPEN_INPUT,
    CONV_ERR_INPUT_SIZE,     /* struct.error: wrong .bin size */
    CONV_ERR_OPEN_OUTPUT,
    CONV_ERR_MEMORY,
    CONV_ERR_PNG_WRITE,
    CONV_ERR_PNG_READ,
    CONV_ERR_PNG_NOT16,
    CONV_ERR_RAW_EMPTY,
    CONV_ERR_RAW_INCOMPLETE,
    CONV_ERR_UNSUPPORTED,
    CONV_ERR_META_MISSING,
    CONV_ERR_META_NAN,
    CONV_ERR_META_ZERO,
    CONV_ERR_IO
} ConvStatus;

MetaStatus metadata_read(const char *path, Metadata *meta, char *missingKeys, size_t missingSize);
bool metadata_has_required(const Metadata *meta);

ConvStatus convert_forward(const char *inputPath, const char *outputPath, int size,
                           bool toPng, bool saveMetadata, ConvStats *stats,
                           char *metadataPathOut, size_t metadataPathSize,
                           char *errorOut, size_t errorOutSize);

ConvStatus convert_reverse(const char *inputPath, const char *outputPath,
                           const Metadata *meta, ConvStats *stats,
                           char *errorOut, size_t errorOutSize);

const char *conv_status_string(ConvStatus status);

#endif /* DISPLACEBOX_CONVERTER_H */
