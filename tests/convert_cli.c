/* Small command-line harness used by tests/compare_with_python.py.
 * It exercises the exact converter code that the GUI uses. */
#include "converter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc < 5)
    {
        fprintf(stderr,
                "usage:\n"
                "  %s forward <input.bin> <output> <size> <raw|png>\n"
                "  %s reverse <input.raw|input.png> <output.bin> <metadata.json>\n",
                argv[0], argv[0]);
        return 2;
    }

    char error[512] = { 0 };
    ConvStats stats;

    if (strcmp(argv[1], "forward") == 0)
    {
        if (argc != 6) return 2;
        int size = atoi(argv[4]);
        bool png = strcmp(argv[5], "png") == 0;
        char metaPath[4096] = { 0 };

        ConvStatus status = convert_forward(argv[2], argv[3], size, png, true, &stats,
                                            metaPath, sizeof(metaPath), error, sizeof(error));
        if (status != CONV_OK)
        {
            fprintf(stderr, "error: %s (%s)\n", conv_status_string(status), error);
            return 1;
        }
        printf("min=%.17g max=%.17g delta=%.17g\n", stats.min, stats.max, stats.delta);
        if (metaPath[0]) printf("metadata=%s\n", metaPath);
        return 0;
    }

    if (strcmp(argv[1], "reverse") == 0)
    {
        if (argc != 5) return 2;
        Metadata meta;
        char missing[128] = { 0 };
        MetaStatus metaStatus = metadata_read(argv[4], &meta, missing, sizeof(missing));
        if (metaStatus != META_OK)
        {
            fprintf(stderr, "metadata error: %d (%s)\n", (int)metaStatus, missing);
            return 1;
        }

        ConvStatus status = convert_reverse(argv[2], argv[3], &meta, &stats,
                                            error, sizeof(error));
        if (status != CONV_OK)
        {
            fprintf(stderr, "error: %s (%s)\n", conv_status_string(status), error);
            return 1;
        }
        printf("min=%.17g max=%.17g delta=%.17g\n", stats.min, stats.max, stats.delta);
        return 0;
    }

    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 2;
}
