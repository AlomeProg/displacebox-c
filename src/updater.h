#ifndef DISPLACEBOX_UPDATER_H
#define DISPLACEBOX_UPDATER_H

#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    pthread_t thread;
    pthread_mutex_t mutex;
    bool started;
    bool done;
    bool available;
    char version[64];
    char body[8192];
} Updater;

void updater_start(Updater *updater, const char *currentVersion);
/* Returns true once when a newer release has been found; copies the details. */
bool updater_poll(Updater *updater, char *version, size_t versionSize, char *body, size_t bodySize);
void updater_join(Updater *updater);

#endif /* DISPLACEBOX_UPDATER_H */
