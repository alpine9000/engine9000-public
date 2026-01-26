// Windows stubs for analyse features
#include "analyse.h"
#include "debug.h"
#include "debugger.h"

int
analyse_init(void)
{
    return 0;
}

void
analyse_shutdown(void)
{
}

int
analyse_reset(void)
{
    return 0;
}

int
analyse_handlePacket(const char *line, size_t len)
{
    (void)line;
    (void)len;
    return 0;
}

int
analyse_writeFinalJson(const char *jsonPath)
{
    (void)jsonPath;
    return 0;
}

int
analyse_profileSnapshot(analyse_profile_sample_entry **out, size_t *count)
{
    if (out) {
        *out = NULL;
    }
    if (count) {
        *count = 0;
    }
    return 0;
}

void
analyse_profileSnapshotFree(analyse_profile_sample_entry *entries)
{
    (void)entries;
}

void
analyse_populateSampleLocations(analyse_profile_sample_entry *entries, size_t count)
{
    (void)entries;
    (void)count;
}
