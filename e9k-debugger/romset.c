/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "romset.h"
#include "debugger.h"

int
debugger_platform_pathJoin(char *out, size_t cap, const char *dir, const char *name);

int
debugger_platform_scanFolder(const char *folder, int (*cb)(const char *path, void *user), void *user);

typedef struct romset_romchunk {
    char   *path;
    size_t  size;
    int     index;
} romset_romchunk_t;

typedef struct romset_romset {
    romset_romchunk_t *pChunks;
    size_t pCount;
    size_t pCap;
    romset_romchunk_t *sChunks;
    size_t sCount;
    size_t sCap;
    romset_romchunk_t *mChunks;
    size_t mCount;
    size_t mCap;
    romset_romchunk_t *vChunks;
    size_t vCount;
    size_t vCap;
    romset_romchunk_t *cChunks;
    size_t cCount;
    size_t cCap;
} romset_romset_t;

static const char *
romset_basename(const char *path)
{
    if (!path || !*path) {
        return NULL;
    }
    const char *slash = strrchr(path, '/');
    const char *back = strrchr(path, '\\');
    const char *best = slash > back ? slash : back;
    return best ? best + 1 : path;
}

static void
romset_romsetFree(romset_romset_t *set)
{
    if (!set) {
        return;
    }
    romset_romchunk_t *chunkArrays[] = { set->pChunks, set->sChunks, set->mChunks, set->vChunks, set->cChunks };
    size_t counts[] = { set->pCount, set->sCount, set->mCount, set->vCount, set->cCount };
    for (size_t arrayIndex = 0; arrayIndex < sizeof(chunkArrays)/sizeof(chunkArrays[0]); ++arrayIndex) {
        romset_romchunk_t *chunkArray = chunkArrays[arrayIndex];
        size_t count = counts[arrayIndex];
        for (size_t chunkIndex = 0; chunkIndex < count; ++chunkIndex) {
            free(chunkArray[chunkIndex].path);
        }
        free(chunkArray);
    }
    memset(set, 0, sizeof(*set));
}

static int
romset_romchunkAppend(romset_romchunk_t **arr, size_t *count, size_t *capacity,
                      const char *path, size_t size, int index)
{
    if (!arr || !count || !capacity || !path) {
        return 0;
    }
    if (*count + 1 > *capacity) {
        size_t nextCap = (*capacity == 0) ? 8 : (*capacity * 2);
        romset_romchunk_t *nextArr = realloc(*arr, nextCap * sizeof(**arr));
        if (!nextArr) {
            return 0;
        }
        *arr = nextArr;
        *capacity = nextCap;
    }
    romset_romchunk_t *destChunk = &(*arr)[(*count)++];
    destChunk->path = strdup(path);
    destChunk->size = size;
    destChunk->index = index;
    return destChunk->path != NULL;
}

static int
romset_parseRomTag(const char *name, char *outTag, int *outIndex)
{
    if (!name || !outTag || !outIndex) {
        return 0;
    }
    char lower[PATH_MAX];
    size_t nameLength = strlen(name);
    if (nameLength >= sizeof(lower)) {
        nameLength = sizeof(lower) - 1;
    }
    for (size_t nameIndex = 0; nameIndex < nameLength; ++nameIndex) {
        lower[nameIndex] = (char)tolower((unsigned char)name[nameIndex]);
    }
    lower[nameLength] = '\0';
    for (size_t nameIndex = 0; nameIndex < nameLength; ++nameIndex) {
        char tagChar = lower[nameIndex];
        if (tagChar != 'p' && tagChar != 's' && tagChar != 'm' && tagChar != 'v' && tagChar != 'c') {
            continue;
        }
        if (tagChar == 'p') {
            if (strncmp(&lower[nameIndex], "pg1", 3) == 0) {
                *outTag = 'p';
                *outIndex = 1;
                return 1;
            }
            if (strncmp(&lower[nameIndex], "sp2", 3) == 0) {
                *outTag = 'p';
                *outIndex = 2;
                return 1;
            }
        }
        size_t scanIndex = nameIndex + 1;
        if (scanIndex >= nameLength || !isdigit((unsigned char)lower[scanIndex])) {
            continue;
        }
        int parsedIndex = 0;
        while (scanIndex < nameLength && isdigit((unsigned char)lower[scanIndex])) {
            parsedIndex = parsedIndex * 10 + (lower[scanIndex] - '0');
            scanIndex++;
        }
        *outTag = tagChar;
        *outIndex = parsedIndex;
        return 1;
    }
    return 0;
}

static int
romset_romsetAddFile(romset_romset_t *set, const char *path, size_t size)
{
    char tagChar = 0;
    int fileIndex = 0;
    const char *base = romset_basename(path);
    if (!romset_parseRomTag(base ? base : path, &tagChar, &fileIndex)) {
        return 0;
    }
    if (tagChar == 'p') {
        return romset_romchunkAppend(&set->pChunks, &set->pCount, &set->pCap, path, size, fileIndex);
    } else if (tagChar == 's') {
        return romset_romchunkAppend(&set->sChunks, &set->sCount, &set->sCap, path, size, fileIndex);
    } else if (tagChar == 'm') {
        return romset_romchunkAppend(&set->mChunks, &set->mCount, &set->mCap, path, size, fileIndex);
    } else if (tagChar == 'v') {
        return romset_romchunkAppend(&set->vChunks, &set->vCount, &set->vCap, path, size, fileIndex);
    } else if (tagChar == 'c') {
        return romset_romchunkAppend(&set->cChunks, &set->cCount, &set->cCap, path, size, fileIndex);
    }
    return 0;
}

static int
romset_scanRomFolderEntry(const char *path, void *user)
{
    romset_romset_t *set = (romset_romset_t *)user;
    if (!path || !*path || !set) {
        return 1;
    }
    struct stat statBuffer;
    if (stat(path, &statBuffer) != 0) {
        return 1;
    }
    if (!S_ISREG(statBuffer.st_mode)) {
        return 1;
    }
    romset_romsetAddFile(set, path, (size_t)statBuffer.st_size);
    return 1;
}

static int
romset_scanRomFolder(const char *folder, romset_romset_t *set)
{
    if (!folder || !*folder || !set) {
        return 0;
    }
    return debugger_platform_scanFolder(folder, romset_scanRomFolderEntry, set);
}

static int
romset_romchunkCompare(const void *left, const void *right)
{
    const romset_romchunk_t *chunkA = (const romset_romchunk_t *)left;
    const romset_romchunk_t *chunkB = (const romset_romchunk_t *)right;
    if (chunkA->index != chunkB->index) {
        return chunkA->index - chunkB->index;
    }
    if (!chunkA->path || !chunkB->path) {
        return 0;
    }
    return strcmp(chunkA->path, chunkB->path);
}

static void
romset_write32le(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value & 0xffu);
    dest[1] = (uint8_t)((value >> 8) & 0xffu);
    dest[2] = (uint8_t)((value >> 16) & 0xffu);
    dest[3] = (uint8_t)((value >> 24) & 0xffu);
}

static int
romset_writeFileData(FILE *output, romset_romchunk_t *fileChunks, size_t chunkCount)
{
    for (size_t fileIndex = 0; fileIndex < chunkCount; ++fileIndex) {
        FILE *input = fopen(fileChunks[fileIndex].path, "rb");
        if (!input) {
            return 0;
        }
        char buffer[8192];
        size_t readCount;
        while ((readCount = fread(buffer, 1, sizeof(buffer), input)) > 0) {
            if (fwrite(buffer, 1, readCount, output) != readCount) {
                fclose(input);
                return 0;
            }
        }
        fclose(input);
    }
    return 1;
}

static int
romset_writeFileInterleaved(FILE *output, const char *pathA, const char *pathB)
{
    if (!output || !pathA || !*pathA || !pathB || !*pathB) {
        return 0;
    }
    FILE *fileA = fopen(pathA, "rb");
    if (!fileA) {
        return 0;
    }
    FILE *fileB = fopen(pathB, "rb");
    if (!fileB) {
        fclose(fileA);
        return 0;
    }
    uint8_t bufferA[8192];
    uint8_t bufferB[8192];
    uint8_t outputBuffer[16384];
    int ok = 1;
    for (;;) {
        size_t readA = fread(bufferA, 1, sizeof(bufferA), fileA);
        size_t readB = fread(bufferB, 1, sizeof(bufferB), fileB);
        if (readA != readB) {
            if (readA == 0 && readB == 0) {
                break;
            }
            ok = 0;
            break;
        }
        if (readA == 0) {
            break;
        }
        for (size_t byteIndex = 0; byteIndex < readA; ++byteIndex) {
            outputBuffer[byteIndex * 2] = bufferA[byteIndex];
            outputBuffer[byteIndex * 2 + 1] = bufferB[byteIndex];
        }
        if (fwrite(outputBuffer, 1, readA * 2, output) != readA * 2) {
            ok = 0;
            break;
        }
    }
    fclose(fileA);
    fclose(fileB);
    return ok;
}

int
romset_buildNeoFromFolder(const char *folder, char *outPath, size_t capacity)
{
    if (!folder || !*folder || !outPath || capacity == 0) {
        return 0;
    }
    romset_romset_t set;
    memset(&set, 0, sizeof(set));
    if (!romset_scanRomFolder(folder, &set)) {
        romset_romsetFree(&set);
        return 0;
    }
    if (set.pCount == 0 || set.cCount == 0) {
        romset_romsetFree(&set);
        return 0;
    }
    if (set.pCount > 1) qsort(set.pChunks, set.pCount, sizeof(*set.pChunks), romset_romchunkCompare);
    if (set.sCount > 1) qsort(set.sChunks, set.sCount, sizeof(*set.sChunks), romset_romchunkCompare);
    if (set.mCount > 1) qsort(set.mChunks, set.mCount, sizeof(*set.mChunks), romset_romchunkCompare);
    if (set.vCount > 1) qsort(set.vChunks, set.vCount, sizeof(*set.vChunks), romset_romchunkCompare);
    if (set.cCount > 1) qsort(set.cChunks, set.cCount, sizeof(*set.cChunks), romset_romchunkCompare);

    const char *base = debugger.config.neogeo.libretro.saveDir[0] ? debugger.config.neogeo.libretro.saveDir : debugger.config.neogeo.libretro.systemDir;
    if (!base || !*base) {
        romset_romsetFree(&set);
        return 0;
    }
    if (!debugger_platform_pathJoin(outPath, capacity, base, "e9k-romfolder.neo")) {
        romset_romsetFree(&set);
        return 0;
    }

    size_t pSize = 0, sSize = 0, mSize = 0, v1Size = 0, v2Size = 0, cSize = 0;
    for (size_t chunkIndex = 0; chunkIndex < set.pCount; ++chunkIndex) pSize += set.pChunks[chunkIndex].size;
    for (size_t chunkIndex = 0; chunkIndex < set.sCount; ++chunkIndex) sSize += set.sChunks[chunkIndex].size;
    for (size_t chunkIndex = 0; chunkIndex < set.mCount; ++chunkIndex) mSize += set.mChunks[chunkIndex].size;
    for (size_t chunkIndex = 0; chunkIndex < set.cCount; ++chunkIndex) cSize += set.cChunks[chunkIndex].size;
    for (size_t chunkIndex = 0; chunkIndex < set.vCount; ++chunkIndex) v1Size += set.vChunks[chunkIndex].size;
    v2Size = 0;

    uint8_t header[4096];
    memset(header, 0, sizeof(header));
    header[0] = 'N';
    header[1] = 'E';
    header[2] = 'O';
    header[3] = 0x01;
    romset_write32le(&header[4], (uint32_t)pSize);
    romset_write32le(&header[8], (uint32_t)sSize);
    romset_write32le(&header[12], (uint32_t)mSize);
    romset_write32le(&header[16], (uint32_t)v1Size);
    romset_write32le(&header[20], (uint32_t)v2Size);
    romset_write32le(&header[24], (uint32_t)cSize);
    const char *name = "E9K GENERATED";
    const char *manu = "E9K";
    memcpy(&header[44], name, strlen(name));
    memcpy(&header[77], manu, strlen(manu));

    FILE *output = fopen(outPath, "wb");
    if (!output) {
        romset_romsetFree(&set);
        return 0;
    }
    if (fwrite(header, 1, sizeof(header), output) != sizeof(header)) {
        fclose(output);
        romset_romsetFree(&set);
        return 0;
    }
    if (!romset_writeFileData(output, set.pChunks, set.pCount) ||
        !romset_writeFileData(output, set.sChunks, set.sCount) ||
        !romset_writeFileData(output, set.mChunks, set.mCount)) {
        fclose(output);
        romset_romsetFree(&set);
        return 0;
    }
    if (!romset_writeFileData(output, set.vChunks, set.vCount)) {
        fclose(output);
        romset_romsetFree(&set);
        return 0;
    }
    if (set.cCount == 1) {
        if (!romset_writeFileData(output, set.cChunks, set.cCount)) {
            fclose(output);
            romset_romsetFree(&set);
            return 0;
        }
    } else {
        for (size_t chunkIndex = 0; chunkIndex + 1 < set.cCount; chunkIndex += 2) {
            if (set.cChunks[chunkIndex].size != set.cChunks[chunkIndex + 1].size) {
                fclose(output);
                romset_romsetFree(&set);
                return 0;
            }
            if (!romset_writeFileInterleaved(output, set.cChunks[chunkIndex].path, set.cChunks[chunkIndex + 1].path)) {
                fclose(output);
                romset_romsetFree(&set);
                return 0;
            }
        }
        if (set.cCount % 2 != 0) {
            romset_romchunk_t *lastChunk = &set.cChunks[set.cCount - 1];
            if (!romset_writeFileData(output, lastChunk, 1)) {
                fclose(output);
                romset_romsetFree(&set);
                return 0;
            }
        }
    }
    fclose(output);
    romset_romsetFree(&set);
    return 1;
}
