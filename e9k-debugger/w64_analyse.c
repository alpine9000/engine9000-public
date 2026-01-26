// Windows stubs for analyse features
#include "analyse.h"
#include "debug.h"
#include "debugger.h"

int
analyseInit(void)
{
    return 0;
}

void
analyseShutdown(void)
{
}

int
analyseReset(void)
{
    return 0;
}

int
analyseHandlePacket(const char *line, size_t len)
{
    (void)line;
    (void)len;
    return 0;
}

int
analyseWriteFinalJson(const char *jsonPath)
{
    (void)jsonPath;
    return 0;
}

int
analyseProfileSnapshot(analyseProfileSampleEntry **out, size_t *count)
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
analyseProfileSnapshotFree(analyseProfileSampleEntry *entries)
{
    (void)entries;
}

void
analysePopulateSampleLocations(analyseProfileSampleEntry *entries, size_t count)
{
    (void)entries;
    (void)count;
}
