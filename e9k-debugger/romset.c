/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "romset.h"
#include "libretro_host.h"
#include "debugger.h"
#include "romset_crypto.h"
#include "strutil.h"

#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include "deps/miniz/miniz.h"

typedef struct romset_romchunk {
    char   *path;
    char   *archivePath;
    size_t  size;
    int     index;
    mz_uint zipIndex;
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

typedef struct romset_chunk_reader {
    FILE *file;
    mz_zip_archive zip;
    mz_zip_reader_extract_iter_state *zipIter;
    int isZip;
} romset_chunk_reader_t;

typedef enum romset_ngh253_revision {
    romset_ngh253_revisionA = 0,
    romset_ngh253_revisionB
} romset_ngh253_revision_t;

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

static uint64_t
romset_hashPath(const char *path)
{
    uint64_t hash = 1469598103934665603ull;
    if (!path) {
        return hash;
    }
    while (*path) {
        hash ^= (uint64_t)(unsigned char)*path++;
        hash *= 1099511628211ull;
    }
    return hash;
}

static void
romset_copySanitizedStem(char *out, size_t cap, const char *path)
{
    if (!out || cap == 0) {
        return;
    }
    out[0] = '\0';
    const char *base = romset_basename(path);
    if (!base || !*base) {
        strutil_strlcpy(out, cap, "rom");
        return;
    }
    const char *dot = strrchr(base, '.');
    size_t len = dot && dot > base ? (size_t)(dot - base) : strlen(base);
    size_t pos = 0;
    for (size_t i = 0; i < len && pos < cap - 1; ++i) {
        unsigned char c = (unsigned char)base[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.') {
            out[pos++] = (char)c;
        } else {
            out[pos++] = '_';
        }
    }
    if (pos == 0) {
        strutil_strlcpy(out, cap, "rom");
        return;
    }
    out[pos] = '\0';
}

static int
romset_appendText(char *out, size_t cap, size_t *pos, const char *text)
{
    if (!out || cap == 0 || !pos || !text) {
        return 0;
    }
    while (*text) {
        if (*pos >= cap - 1) {
            out[cap - 1] = '\0';
            return 0;
        }
        out[*pos] = *text++;
        *pos += 1;
    }
    out[*pos] = '\0';
    return 1;
}

static int
romset_buildGeneratedNeoFileName(char *out, size_t cap, const char *inputPath, const char *prefix)
{
    if (!out || cap == 0) {
        return 0;
    }
    out[0] = '\0';
    char stem[128];
    char hashText[17];
    romset_copySanitizedStem(stem, sizeof(stem), inputPath);
    snprintf(hashText, sizeof(hashText), "%016llx", (unsigned long long)romset_hashPath(inputPath));

    size_t pos = 0;
    return romset_appendText(out, cap, &pos, prefix) &&
           romset_appendText(out, cap, &pos, stem) &&
           romset_appendText(out, cap, &pos, "-") &&
           romset_appendText(out, cap, &pos, hashText) &&
           romset_appendText(out, cap, &pos, ".neo");
}

static int
romset_strEqNoCase(const char *a, const char *b)
{
    if (!a || !b) {
        return 0;
    }
    while (*a && *b) {
        int ca = tolower((unsigned char)*a++);
        int cb = tolower((unsigned char)*b++);
        if (ca != cb) {
            return 0;
        }
    }
    return (*a == '\0' && *b == '\0');
}

static int
romset_strStartsNoCase(const char *text, const char *prefix)
{
    if (!text || !prefix) {
        return 0;
    }
    while (*prefix) {
        if (!*text) {
            return 0;
        }
        int textChar = tolower((unsigned char)*text++);
        int prefixChar = tolower((unsigned char)*prefix++);
        if (textChar != prefixChar) {
            return 0;
        }
    }
    return 1;
}

static int
romset_chunkHasExpectedExtension(const romset_romchunk_t *chunk, char tagChar)
{
    if (!chunk || !chunk->path || !*chunk->path || chunk->index <= 0) {
        return 0;
    }
    const char *base = romset_basename(chunk->path);
    if (!base || !*base) {
        return 0;
    }
    const char *dot = strrchr(base, '.');
    if (!dot || !dot[1]) {
        return 0;
    }
    char expected[16];
    snprintf(expected, sizeof(expected), "%c%d", tagChar, chunk->index);
    return romset_strEqNoCase(dot + 1, expected);
}

static void
romset_dedupeChunksByIndex(romset_romchunk_t *chunks, size_t *count, char tagChar)
{
    if (!chunks || !count || *count == 0) {
        return;
    }
    size_t writeIndex = 0;
    for (size_t readIndex = 0; readIndex < *count; ) {
        size_t runStart = readIndex;
        size_t runEnd = readIndex + 1;
        while (runEnd < *count && chunks[runEnd].index == chunks[runStart].index) {
            runEnd++;
        }

        size_t bestIndex = runStart;
        for (size_t candIndex = runStart + 1; candIndex < runEnd; ++candIndex) {
            int bestHasExt = romset_chunkHasExpectedExtension(&chunks[bestIndex], tagChar);
            int candHasExt = romset_chunkHasExpectedExtension(&chunks[candIndex], tagChar);
            if (candHasExt && !bestHasExt) {
                bestIndex = candIndex;
                continue;
            }
            if (candHasExt == bestHasExt) {
                if (chunks[candIndex].size > chunks[bestIndex].size) {
                    bestIndex = candIndex;
                    continue;
                }
                if (chunks[candIndex].size == chunks[bestIndex].size && chunks[candIndex].path && chunks[bestIndex].path) {
                    if (strcmp(chunks[candIndex].path, chunks[bestIndex].path) < 0) {
                        bestIndex = candIndex;
                        continue;
                    }
                }
            }
        }

        romset_romchunk_t best = chunks[bestIndex];
        chunks[bestIndex].path = NULL;
        chunks[bestIndex].archivePath = NULL;
        chunks[bestIndex].size = 0;
        chunks[bestIndex].index = 0;

        for (size_t freeIndex = runStart; freeIndex < runEnd; ++freeIndex) {
            if (chunks[freeIndex].path) {
                free(chunks[freeIndex].path);
                chunks[freeIndex].path = NULL;
            }
            if (chunks[freeIndex].archivePath) {
                free(chunks[freeIndex].archivePath);
                chunks[freeIndex].archivePath = NULL;
            }
            chunks[freeIndex].size = 0;
            chunks[freeIndex].index = 0;
        }

        chunks[writeIndex++] = best;
        readIndex = runEnd;
    }
    *count = writeIndex;
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
            free(chunkArray[chunkIndex].archivePath);
        }
        free(chunkArray);
    }
    memset(set, 0, sizeof(*set));
}

static int
romset_romchunkAppend(romset_romchunk_t **arr, size_t *count, size_t *capacity,
                      const char *path, const char *archivePath, mz_uint zipIndex, size_t size, int index)
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
    char *pathDup = strdup(path);
    char *archivePathDup = archivePath ? strdup(archivePath) : NULL;
    if (!pathDup || (archivePath && !archivePathDup)) {
        free(pathDup);
        free(archivePathDup);
        return 0;
    }

    romset_romchunk_t *destChunk = &(*arr)[*count];
    memset(destChunk, 0, sizeof(*destChunk));
    destChunk->path = pathDup;
    destChunk->archivePath = archivePathDup;
    destChunk->size = size;
    destChunk->index = index;
    destChunk->zipIndex = zipIndex;
    (*count)++;
    return 1;
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

    const char *dot = strrchr(lower, '.');
    if (romset_strEqNoCase(lower, "neo-sma") ||
        (dot && romset_strEqNoCase(dot + 1, "neo-sma"))) {
        *outTag = 'p';
        *outIndex = 0;
        return 1;
    }
    if (dot && dot[1]) {
        if (romset_strEqNoCase(dot + 1, "pg1")) {
            *outTag = 'p';
            *outIndex = 1;
            return 1;
        }
        if (romset_strEqNoCase(dot + 1, "sp2")) {
            *outTag = 'p';
            *outIndex = 2;
            return 1;
        }

        char tagChar = dot[1];
        if ((tagChar == 'p' || tagChar == 's' || tagChar == 'm' || tagChar == 'v' || tagChar == 'c') &&
            isdigit((unsigned char)dot[2])) {
            int parsedIndex = 0;
            size_t scanIndex = 2;
            while (dot[scanIndex] && isdigit((unsigned char)dot[scanIndex])) {
                parsedIndex = parsedIndex * 10 + (dot[scanIndex] - '0');
                scanIndex++;
            }
            if (!dot[scanIndex]) {
                *outTag = tagChar;
                *outIndex = parsedIndex;
                return 1;
            }
        }
    }

    for (size_t nameIndex = 0; nameIndex + 1 < nameLength; ++nameIndex) {
        char prevChar = nameIndex > 0 ? lower[nameIndex - 1] : '\0';
        if (nameIndex > 0 && prevChar != '-' && prevChar != '_' && prevChar != '.') {
            continue;
        }

        char tagChar = lower[nameIndex];
        if ((tagChar != 'p' && tagChar != 's' && tagChar != 'm' && tagChar != 'v' && tagChar != 'c') ||
            !isdigit((unsigned char)lower[nameIndex + 1])) {
            continue;
        }

        size_t scanIndex = nameIndex + 1;
        int parsedIndex = 0;
        while (scanIndex < nameLength && isdigit((unsigned char)lower[scanIndex])) {
            parsedIndex = parsedIndex * 10 + (lower[scanIndex] - '0');
            scanIndex++;
        }

        char nextChar = lower[scanIndex];
        if (nextChar && nextChar != '-' && nextChar != '_' && nextChar != '.') {
            continue;
        }

        *outTag = tagChar;
        *outIndex = parsedIndex;
        return 1;
    }
    return 0;
}

static int
romset_isBiosFile(const char *path)
{
    const char *base = romset_basename(path);
    if (!base) {
        return 0;
    }

    if (romset_strStartsNoCase(base, "sp-") ||
        romset_strStartsNoCase(base, "uni-bios") ||
        romset_strStartsNoCase(base, "asia-") ||
        romset_strStartsNoCase(base, "japan-") ||
        romset_strEqNoCase(base, "sfix.sfix") ||
        romset_strEqNoCase(base, "sm1.sm1") ||
        romset_strEqNoCase(base, "000-lo.lo") ||
        romset_strEqNoCase(base, "vs-bios.rom") ||
        romset_strEqNoCase(base, "sp1.jipan.1024")) {
        return 1;
    }

    return 0;
}

static int
romset_romsetAddFile(romset_romset_t *set, const char *path, const char *archivePath, mz_uint zipIndex, size_t size)
{
    char tagChar = 0;
    int fileIndex = 0;
    const char *base = romset_basename(path);
    if (!base) {
        return 1;
    }
    if (romset_isBiosFile(base)) {
        return 1;
    }
    if (!romset_parseRomTag(base, &tagChar, &fileIndex)) {
        return 1;
    }
    switch (tagChar)
    {
    case 'p':
        return romset_romchunkAppend(&set->pChunks, &set->pCount, &set->pCap, path, archivePath, zipIndex, size, fileIndex);
    case 's':
        return romset_romchunkAppend(&set->sChunks, &set->sCount, &set->sCap, path, archivePath, zipIndex, size, fileIndex);
    case 'm':
        return romset_romchunkAppend(&set->mChunks, &set->mCount, &set->mCap, path, archivePath, zipIndex, size, fileIndex);
    case 'v':
        return romset_romchunkAppend(&set->vChunks, &set->vCount, &set->vCap, path, archivePath, zipIndex, size, fileIndex);
    case 'c':
        return romset_romchunkAppend(&set->cChunks, &set->cCount, &set->cCap, path, archivePath, zipIndex, size, fileIndex);
    default:
        return 1;
    }
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
    romset_romsetAddFile(set, path, NULL, 0, (size_t)statBuffer.st_size);
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
romset_scanRomZip(const char *zipPath, romset_romset_t *set)
{
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, zipPath, 0)) {
        return 0;
    }

    int ok = 1;
    mz_uint fileCount = mz_zip_reader_get_num_files(&zip);
    for (mz_uint fileIndex = 0; fileIndex < fileCount; ++fileIndex) {
        mz_zip_archive_file_stat statBuffer;
        if (!mz_zip_reader_file_stat(&zip, fileIndex, &statBuffer)) {
            ok = 0;
            break;
        }
        if (mz_zip_reader_is_file_a_directory(&zip, fileIndex) ||
            mz_zip_reader_is_file_encrypted(&zip, fileIndex) ||
            !mz_zip_reader_is_file_supported(&zip, fileIndex)) {
            continue;
        }
        if (!romset_romsetAddFile(set,
                                  statBuffer.m_filename,
                                  zipPath,
                                  fileIndex,
                                  (size_t)statBuffer.m_uncomp_size)) {
            ok = 0;
            break;
        }
    }

    mz_zip_reader_end(&zip);
    return ok;
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
romset_parseLeadingNgh(const char *path, uint32_t *outNgh)
{
    const char *base = romset_basename(path);
    if (!base || !isdigit((unsigned char)base[0]) || !outNgh) {
        return 0;
    }

    uint32_t ngh = 0;
    size_t digitCount = 0;
    while (isdigit((unsigned char)base[digitCount])) {
        ngh = (ngh * 16u) + (uint32_t)(base[digitCount] - '0');
        digitCount++;
    }

    if (digitCount < 3 || base[digitCount] != '-') {
        return 0;
    }

    *outNgh = ngh;
    return 1;
}

static uint32_t
romset_detectNghFromChunks(const romset_romchunk_t *chunks, size_t count)
{
    for (size_t chunkIndex = 0; chunkIndex < count; ++chunkIndex) {
        uint32_t ngh = 0;
        if (romset_parseLeadingNgh(chunks[chunkIndex].path, &ngh)) {
            return ngh;
        }
    }
    return 0;
}

static uint32_t
romset_detectNgh(const romset_romset_t *set)
{
    if (!set) {
        return 0;
    }

    uint32_t ngh = romset_detectNghFromChunks(set->pChunks, set->pCount);
    if (!ngh) {
        ngh = romset_detectNghFromChunks(set->cChunks, set->cCount);
    }
    if (!ngh) {
        ngh = romset_detectNghFromChunks(set->mChunks, set->mCount);
    }
    if (!ngh) {
        ngh = romset_detectNghFromChunks(set->vChunks, set->vCount);
    }
    if (!ngh) {
        ngh = romset_detectNghFromChunks(set->sChunks, set->sCount);
    }
    return ngh;
}

static romset_ngh253_revision_t
romset_detectNgh253Revision(const romset_romset_t *set)
{
    if (!set) {
        return romset_ngh253_revisionA;
    }

    for (size_t chunkIndex = 0; chunkIndex < set->pCount; ++chunkIndex) {
        const char *base = romset_basename(set->pChunks[chunkIndex].path);
        if (base && romset_strEqNoCase(base, "ke.neo-sma")) {
            return romset_ngh253_revisionB;
        }
    }

    if (set->pCount == 3u &&
        set->pChunks[1].size == 0x400000u &&
        set->pChunks[2].size == 0x400000u) {
        return romset_ngh253_revisionB;
    }

    return romset_ngh253_revisionA;
}

static int
romset_chunkReaderOpen(romset_chunk_reader_t *reader, const romset_romchunk_t *chunk)
{
    if (!reader || !chunk || !chunk->path) {
        return 0;
    }
    memset(reader, 0, sizeof(*reader));

    if (chunk->archivePath && chunk->archivePath[0]) {
        reader->isZip = 1;
        memset(&reader->zip, 0, sizeof(reader->zip));
        if (!mz_zip_reader_init_file(&reader->zip, chunk->archivePath, 0)) {
            return 0;
        }
        reader->zipIter = mz_zip_reader_extract_iter_new(&reader->zip, chunk->zipIndex, 0);
        if (!reader->zipIter) {
            mz_zip_reader_end(&reader->zip);
            memset(reader, 0, sizeof(*reader));
            return 0;
        }
        return 1;
    }

    reader->file = fopen(chunk->path, "rb");
    return reader->file != NULL;
}

static void
romset_chunkReaderClose(romset_chunk_reader_t *reader)
{
    if (!reader) {
        return;
    }
    if (reader->zipIter) {
        mz_zip_reader_extract_iter_free(reader->zipIter);
    }
    if (reader->isZip) {
        mz_zip_reader_end(&reader->zip);
    }
    if (reader->file) {
        fclose(reader->file);
    }
    memset(reader, 0, sizeof(*reader));
}

static int
romset_chunkReaderRead(romset_chunk_reader_t *reader, uint8_t *dest, size_t size)
{
    if (!reader || !dest) {
        return 0;
    }
    size_t totalRead = 0;
    while (totalRead < size) {
        size_t readCount = 0;
        if (reader->isZip) {
            readCount = mz_zip_reader_extract_iter_read(reader->zipIter,
                                                        dest + totalRead,
                                                        size - totalRead);
        } else {
            readCount = fread(dest + totalRead, 1, size - totalRead, reader->file);
        }
        if (readCount == 0) {
            return 0;
        }
        totalRead += readCount;
    }
    return 1;
}

static int
romset_chunkReaderSkip(romset_chunk_reader_t *reader, size_t size)
{
    uint8_t buffer[8192];
    size_t remaining = size;
    while (remaining > 0) {
        size_t readSize = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
        if (!romset_chunkReaderRead(reader, buffer, readSize)) {
            return 0;
        }
        remaining -= readSize;
    }
    return 1;
}

static int
romset_writeFileData(FILE *output, romset_romchunk_t *fileChunks, size_t chunkCount)
{
    for (size_t fileIndex = 0; fileIndex < chunkCount; ++fileIndex) {
        romset_chunk_reader_t reader;
        if (!romset_chunkReaderOpen(&reader, &fileChunks[fileIndex])) {
            return 0;
        }

        uint8_t buffer[8192];
        size_t remaining = fileChunks[fileIndex].size;
        int ok = 1;
        while (remaining > 0) {
            size_t readSize = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
            if (!romset_chunkReaderRead(&reader, buffer, readSize) ||
                fwrite(buffer, 1, readSize, output) != readSize) {
                ok = 0;
                break;
            }
            remaining -= readSize;
        }
        romset_chunkReaderClose(&reader);
        if (!ok) {
            return 0;
        }
    }
    return 1;
}

static int
romset_writeChunksInterleavedRange(FILE *output,
                                   const romset_romchunk_t *chunkA,
                                   const romset_romchunk_t *chunkB,
                                   size_t chunkOffset,
                                   size_t chunkSize)
{
    if (!output || !chunkA || !chunkB || chunkA->size != chunkB->size ||
        chunkOffset > chunkA->size || chunkSize > chunkA->size - chunkOffset) {
        return 0;
    }
    if (chunkSize == 0) {
        return 1;
    }

    romset_chunk_reader_t readerA;
    romset_chunk_reader_t readerB;
    int readerAOpen = romset_chunkReaderOpen(&readerA, chunkA);
    int readerBOpen = romset_chunkReaderOpen(&readerB, chunkB);
    if (!readerAOpen || !readerBOpen) {
        if (readerAOpen) {
            romset_chunkReaderClose(&readerA);
        }
        if (readerBOpen) {
            romset_chunkReaderClose(&readerB);
        }
        return 0;
    }

    uint8_t bufferA[8192];
    uint8_t bufferB[8192];
    uint8_t outputBuffer[16384];
    int ok = romset_chunkReaderSkip(&readerA, chunkOffset) &&
             romset_chunkReaderSkip(&readerB, chunkOffset);
    size_t remaining = chunkSize;
    while (ok && remaining > 0) {
        size_t readSize = remaining < sizeof(bufferA) ? remaining : sizeof(bufferA);
        if (!romset_chunkReaderRead(&readerA, bufferA, readSize) ||
            !romset_chunkReaderRead(&readerB, bufferB, readSize)) {
            ok = 0;
            break;
        }
        for (size_t byteIndex = 0; byteIndex < readSize; ++byteIndex) {
            outputBuffer[byteIndex * 2] = bufferA[byteIndex];
            outputBuffer[byteIndex * 2 + 1] = bufferB[byteIndex];
        }
        if (fwrite(outputBuffer, 1, readSize * 2, output) != readSize * 2) {
            ok = 0;
            break;
        }
        remaining -= readSize;
    }
    romset_chunkReaderClose(&readerA);
    romset_chunkReaderClose(&readerB);
    return ok;
}

static int
romset_writeNgh47CChunks(FILE *output, romset_romchunk_t *chunks, size_t chunkCount)
{
    if (!output || !chunks || chunkCount != 4 ||
        chunks[0].index != 1 || chunks[1].index != 2 ||
        chunks[2].index != 3 || chunks[3].index != 4 ||
        chunks[0].size != chunks[1].size ||
        chunks[0].size != chunks[2].size ||
        chunks[0].size != chunks[3].size ||
        (chunks[0].size & 1u)) {
        return 0;
    }

    size_t halfSize = chunks[0].size / 2u;
    return romset_writeChunksInterleavedRange(output, &chunks[0], &chunks[1], 0, halfSize) &&
           romset_writeChunksInterleavedRange(output, &chunks[2], &chunks[3], 0, halfSize) &&
           romset_writeChunksInterleavedRange(output, &chunks[0], &chunks[1], halfSize, halfSize) &&
           romset_writeChunksInterleavedRange(output, &chunks[2], &chunks[3], halfSize, halfSize);
}

static int
romset_readChunkRangeInto(uint8_t *dest, size_t size, const romset_romchunk_t *chunk, size_t chunkOffset)
{
    if (!chunk || !chunk->path || chunkOffset > chunk->size || size > chunk->size - chunkOffset ||
        !dest) {
        return 0;
    }

    romset_chunk_reader_t reader;
    if (!romset_chunkReaderOpen(&reader, chunk)) {
        return 0;
    }
    int ok = romset_chunkReaderSkip(&reader, chunkOffset) &&
             romset_chunkReaderRead(&reader, dest, size);
    romset_chunkReaderClose(&reader);
    return ok;
}

static int
romset_readChunkInto(uint8_t *dest, size_t destSize, size_t offset, const romset_romchunk_t *chunk)
{
    if (!chunk || offset > destSize || chunk->size > destSize - offset ||
        !dest) {
        return 0;
    }
    return romset_readChunkRangeInto(dest + offset, chunk->size, chunk, 0);
}

static int
romset_readChunks(uint8_t *dest, size_t destSize, const romset_romchunk_t *chunks, size_t chunkCount, int allowShort)
{
    size_t offset = 0;
    for (size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
        if (!romset_readChunkInto(dest, destSize, offset, &chunks[chunkIndex])) {
            return 0;
        }
        offset += chunks[chunkIndex].size;
    }
    return allowShort ? offset <= destSize : offset == destSize;
}

static int
romset_readCChunksInterleaved(uint8_t *dest, size_t destSize, const romset_romchunk_t *chunks, size_t chunkCount)
{
    size_t offset = 0;
    for (size_t chunkIndex = 0; chunkIndex < chunkCount; ) {
        const romset_romchunk_t *chunk = &chunks[chunkIndex];
        if ((chunk->index & 1) && (chunkIndex + 1) < chunkCount && chunks[chunkIndex + 1].index == (chunk->index + 1)) {
            const romset_romchunk_t *nextChunk = &chunks[chunkIndex + 1];
            if (chunk->size != nextChunk->size || offset > destSize || (chunk->size * 2) > destSize - offset) {
                return 0;
            }

            uint8_t *bufferA = malloc(chunk->size);
            uint8_t *bufferB = malloc(nextChunk->size);
            if (!bufferA || !bufferB) {
                free(bufferA);
                free(bufferB);
                return 0;
            }
            int ok = romset_readChunkInto(bufferA, chunk->size, 0, chunk) &&
                     romset_readChunkInto(bufferB, nextChunk->size, 0, nextChunk);
            if (ok) {
                for (size_t byteIndex = 0; byteIndex < chunk->size; ++byteIndex) {
                    dest[offset + byteIndex * 2] = bufferA[byteIndex];
                    dest[offset + byteIndex * 2 + 1] = bufferB[byteIndex];
                }
            }
            free(bufferA);
            free(bufferB);
            if (!ok) {
                return 0;
            }
            offset += chunk->size * 2;
            chunkIndex += 2;
            continue;
        }

        if (!romset_readChunkInto(dest, destSize, offset, chunk)) {
            return 0;
        }
        offset += chunk->size;
        chunkIndex += 1;
    }
    return offset == destSize;
}

static int
romset_readNgh256PChunks(uint8_t *dest, size_t destSize, const romset_romchunk_t *chunks, size_t chunkCount)
{
    if (!dest || destSize < 0x900000u || !chunks) {
        return 0;
    }

    int hasSma = 0;
    int hasP1 = 0;
    int hasSp2 = 0;
    for (size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
        size_t offset = 0;
        if (chunks[chunkIndex].index == 0) {
            offset = 0x0c0000u;
            hasSma = 1;
        } else if (chunks[chunkIndex].index == 1) {
            offset = 0x100000u;
            hasP1 = 1;
        } else if (chunks[chunkIndex].index == 2) {
            offset = 0x500000u;
            hasSp2 = 1;
        } else {
            continue;
        }
        if (!romset_readChunkInto(dest, destSize, offset, &chunks[chunkIndex])) {
            return 0;
        }
    }

    return hasSma && hasP1 && hasSp2;
}

static int
romset_readNgh253PChunks(uint8_t *dest, size_t destSize, const romset_romchunk_t *chunks, size_t chunkCount)
{
    if (!dest || destSize < 0x900000u || !chunks) {
        return 0;
    }

    int hasSma = 0;
    int hasProgram = 0;
    size_t programOffset = 0x100000u;
    for (size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex) {
        if (chunks[chunkIndex].index == 0) {
            hasSma = 1;
            if (!romset_readChunkInto(dest, destSize, 0x0c0000u, &chunks[chunkIndex])) {
                return 0;
            }
            continue;
        }
        if (chunks[chunkIndex].index < 1) {
            continue;
        }
        hasProgram = 1;
        if (!romset_readChunkInto(dest, destSize, programOffset, &chunks[chunkIndex])) {
            return 0;
        }
        programOffset += chunks[chunkIndex].size;
    }

    return hasSma && hasProgram && programOffset <= destSize;
}

static int
romset_readNgh261PChunks(uint8_t *dest, size_t destSize, const romset_romchunk_t *chunks, size_t chunkCount)
{
    if (!dest || destSize != 0x200000u || !chunks || chunkCount != 1u ||
        chunks[0].index != 1 || chunks[0].size != 0x200000u) {
        return 0;
    }

    /* NGH261/Sengoku 3 P-ROM layout follows MAME's ROM_CONTINUE mapping:
       src/mame/neogeo/neogeo.cpp and hash/neogeo.xml. */
    return romset_readChunkRangeInto(dest + 0x100000u, 0x100000u, &chunks[0], 0) &&
           romset_readChunkRangeInto(dest, 0x100000u, &chunks[0], 0x100000u);
}

static int
romset_writeBuffer(FILE *output, const uint8_t *data, size_t dataSize)
{
    if (!output || (!data && dataSize > 0)) {
        return 0;
    }
    if (dataSize == 0) {
        return 1;
    }
    return fwrite(data, 1, dataSize, output) == dataSize;
}

static int
romset_writeNgh253Neo(FILE *output, const romset_romset_t *set, const uint8_t *header, size_t headerSize,
                      size_t pSize, size_t sSize, size_t mSize, size_t vSize, size_t cSize,
                      romset_ngh253_revision_t revision)
{
    uint8_t *pBuffer = NULL;
    uint8_t *sBuffer = NULL;
    uint8_t *mBuffer = NULL;
    uint8_t *vBuffer = NULL;
    uint8_t *cBuffer = NULL;
    int ok = 0;

    pBuffer = pSize ? calloc(1, pSize) : NULL;
    sBuffer = sSize ? calloc(1, sSize) : NULL;
    mBuffer = mSize ? calloc(1, mSize) : NULL;
    vBuffer = vSize ? malloc(vSize) : NULL;
    cBuffer = cSize ? malloc(cSize) : NULL;
    if ((pSize && !pBuffer) || (sSize && !sBuffer) || (mSize && !mBuffer) || (vSize && !vBuffer) || (cSize && !cBuffer)) {
        goto done;
    }

    if (!romset_readNgh253PChunks(pBuffer, pSize, set->pChunks, set->pCount) ||
        !romset_readChunks(mBuffer, mSize, set->mChunks, set->mCount, 1) ||
        !romset_readChunks(vBuffer, vSize, set->vChunks, set->vCount, 0) ||
        !romset_readCChunksInterleaved(cBuffer, cSize, set->cChunks, set->cCount)) {
        goto done;
    }

    if (revision == romset_ngh253_revisionB) {
        if (!romset_crypto_applyNgh253RevisionBSmaCmc42(pBuffer, pSize, cBuffer, cSize, sBuffer, sSize)) {
            goto done;
        }
    } else {
        if (!romset_crypto_applyNgh253SmaCmc42(pBuffer, pSize, cBuffer, cSize, sBuffer, sSize)) {
            goto done;
        }
    }

    ok = romset_writeBuffer(output, header, headerSize) &&
         romset_writeBuffer(output, pBuffer, pSize) &&
         romset_writeBuffer(output, sBuffer, sSize) &&
         romset_writeBuffer(output, mBuffer, mSize) &&
         romset_writeBuffer(output, vBuffer, vSize) &&
         romset_writeBuffer(output, cBuffer, cSize);

done:
    free(pBuffer);
    free(sBuffer);
    free(mBuffer);
    free(vBuffer);
    free(cBuffer);
    return ok;
}

static int
romset_writeNgh264Neo(FILE *output, const romset_romset_t *set, const uint8_t *header, size_t headerSize,
                    size_t pSize, size_t sSize, size_t mSize, size_t vSize, size_t cSize)
{
    uint8_t *pBuffer = NULL;
    uint8_t *sBuffer = NULL;
    uint8_t *mBuffer = NULL;
    uint8_t *vBuffer = NULL;
    uint8_t *cBuffer = NULL;
    int ok = 0;

    pBuffer = pSize ? malloc(pSize) : NULL;
    sBuffer = sSize ? calloc(1, sSize) : NULL;
    mBuffer = mSize ? calloc(1, mSize) : NULL;
    vBuffer = vSize ? malloc(vSize) : NULL;
    cBuffer = cSize ? malloc(cSize) : NULL;
    if ((pSize && !pBuffer) || (sSize && !sBuffer) || (mSize && !mBuffer) || (vSize && !vBuffer) || (cSize && !cBuffer)) {
        goto done;
    }

    if (!romset_readChunks(pBuffer, pSize, set->pChunks, set->pCount, 0) ||
        !romset_readChunks(mBuffer, mSize, set->mChunks, set->mCount, 1) ||
        !romset_readChunks(vBuffer, vSize, set->vChunks, set->vCount, 0) ||
        !romset_readCChunksInterleaved(cBuffer, cSize, set->cChunks, set->cCount)) {
        goto done;
    }

    if (!romset_crypto_applyNgh264Cmc50Pcm2(cBuffer, cSize, sBuffer, sSize, mBuffer, mSize, vBuffer, vSize)) {
        goto done;
    }

    ok = romset_writeBuffer(output, header, headerSize) &&
         romset_writeBuffer(output, pBuffer, pSize) &&
         romset_writeBuffer(output, sBuffer, sSize) &&
         romset_writeBuffer(output, mBuffer, mSize) &&
         romset_writeBuffer(output, vBuffer, vSize) &&
         romset_writeBuffer(output, cBuffer, cSize);

done:
    free(pBuffer);
    free(sBuffer);
    free(mBuffer);
    free(vBuffer);
    free(cBuffer);
    return ok;
}

static int
romset_writeNgh261Neo(FILE *output, const romset_romset_t *set, const uint8_t *header, size_t headerSize,
                      size_t pSize, size_t sSize, size_t mSize, size_t vSize, size_t cSize)
{
    uint8_t *pBuffer = NULL;
    uint8_t *sBuffer = NULL;
    uint8_t *mBuffer = NULL;
    uint8_t *vBuffer = NULL;
    uint8_t *cBuffer = NULL;
    int ok = 0;

    pBuffer = pSize ? malloc(pSize) : NULL;
    sBuffer = sSize ? calloc(1, sSize) : NULL;
    mBuffer = mSize ? malloc(mSize) : NULL;
    vBuffer = vSize ? malloc(vSize) : NULL;
    cBuffer = cSize ? malloc(cSize) : NULL;
    if ((pSize && !pBuffer) || (sSize && !sBuffer) || (mSize && !mBuffer) || (vSize && !vBuffer) || (cSize && !cBuffer)) {
        goto done;
    }

    if (!romset_readNgh261PChunks(pBuffer, pSize, set->pChunks, set->pCount) ||
        !romset_readChunks(mBuffer, mSize, set->mChunks, set->mCount, 1) ||
        !romset_readChunks(vBuffer, vSize, set->vChunks, set->vCount, 0) ||
        !romset_readCChunksInterleaved(cBuffer, cSize, set->cChunks, set->cCount)) {
        goto done;
    }

    if (!romset_crypto_applyNgh261Cmc42(cBuffer, cSize, sBuffer, sSize)) {
        goto done;
    }

    ok = romset_writeBuffer(output, header, headerSize) &&
         romset_writeBuffer(output, pBuffer, pSize) &&
         romset_writeBuffer(output, sBuffer, sSize) &&
         romset_writeBuffer(output, mBuffer, mSize) &&
         romset_writeBuffer(output, vBuffer, vSize) &&
         romset_writeBuffer(output, cBuffer, cSize);

done:
    free(pBuffer);
    free(sBuffer);
    free(mBuffer);
    free(vBuffer);
    free(cBuffer);
    return ok;
}

static int
romset_writeNgh256Neo(FILE *output, const romset_romset_t *set, const uint8_t *header, size_t headerSize,
                      size_t pSize, size_t sSize, size_t mSize, size_t vSize, size_t cSize)
{
    uint8_t *pBuffer = NULL;
    uint8_t *sBuffer = NULL;
    uint8_t *mBuffer = NULL;
    uint8_t *vBuffer = NULL;
    uint8_t *cBuffer = NULL;
    int ok = 0;

    pBuffer = pSize ? calloc(1, pSize) : NULL;
    sBuffer = sSize ? calloc(1, sSize) : NULL;
    mBuffer = mSize ? calloc(1, mSize) : NULL;
    vBuffer = vSize ? malloc(vSize) : NULL;
    cBuffer = cSize ? malloc(cSize) : NULL;
    if ((pSize && !pBuffer) || (sSize && !sBuffer) || (mSize && !mBuffer) || (vSize && !vBuffer) || (cSize && !cBuffer)) {
        goto done;
    }

    if (!romset_readNgh256PChunks(pBuffer, pSize, set->pChunks, set->pCount) ||
        !romset_readChunks(mBuffer, mSize, set->mChunks, set->mCount, 1) ||
        !romset_readChunks(vBuffer, vSize, set->vChunks, set->vCount, 0) ||
        !romset_readCChunksInterleaved(cBuffer, cSize, set->cChunks, set->cCount)) {
        goto done;
    }
    if (mSize >= 0x90000u) {
        memmove(mBuffer + 0x10000u, mBuffer, 0x80000u);
    }

    if (!romset_crypto_applyNgh256SmaCmc42(pBuffer, pSize, cBuffer, cSize, sBuffer, sSize)) {
        goto done;
    }

    ok = romset_writeBuffer(output, header, headerSize) &&
         romset_writeBuffer(output, pBuffer, pSize) &&
         romset_writeBuffer(output, sBuffer, sSize) &&
         romset_writeBuffer(output, mBuffer, mSize) &&
         romset_writeBuffer(output, vBuffer, vSize) &&
         romset_writeBuffer(output, cBuffer, cSize);

done:
    free(pBuffer);
    free(sBuffer);
    free(mBuffer);
    free(vBuffer);
    free(cBuffer);
    return ok;
}

static int
romset_buildNeoFromScannedInput(const char *inputPath, int isZip, char *outPath, size_t capacity)
{
    if (!inputPath || !*inputPath) {
        return 0;
    }
    if (!outPath || capacity == 0) {
        return 0;
    }
    romset_romset_t set;
    memset(&set, 0, sizeof(set));
    int scanned = isZip ?
        romset_scanRomZip(inputPath, &set) :
        romset_scanRomFolder(inputPath, &set);
    if (!scanned) {
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

    romset_dedupeChunksByIndex(set.pChunks, &set.pCount, 'p');
    romset_dedupeChunksByIndex(set.sChunks, &set.sCount, 's');
    romset_dedupeChunksByIndex(set.mChunks, &set.mCount, 'm');
    romset_dedupeChunksByIndex(set.vChunks, &set.vCount, 'v');
    romset_dedupeChunksByIndex(set.cChunks, &set.cCount, 'c');

    uint32_t ngh = romset_detectNgh(&set);
    romset_ngh253_revision_t ngh253Revision = romset_detectNgh253Revision(&set);

    if (!romset_buildNeoOutputPath(inputPath,
                                   isZip,
                                   debugger.config.neogeo.libretro.saveDir,
                                   debugger.config.neogeo.libretro.systemDir,
                                   outPath,
                                   capacity)) {
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
    size_t mDisplaySize = mSize;

    if (ngh == 0x253) {
        pSize = 0x900000;
        sSize = 0x80000;
    } else if (ngh == 0x256) {
        pSize = 0x900000;
        sSize = 0x80000;
        if (mSize < 0x90000) {
            mSize = 0x90000;
        }
    } else if (ngh == 0x261) {
        sSize = 0x20000;
    } else if (ngh == 0x264) {
        sSize = 0x20000;
        if (mSize < 0x90000) {
            mSize = 0x90000;
        }
    }

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
    romset_write32le(&header[40], ngh);
    memcpy(&header[96], "E9KD", 4);
    romset_write32le(&header[100], (uint32_t)mDisplaySize);
    const char *name = "E9K GENERATED";
    const char *manu = "E9K";
    memcpy(&header[44], name, strlen(name));
    memcpy(&header[77], manu, strlen(manu));

    const char *saveDir = debugger.config.neogeo.libretro.saveDir;
    if (!libretro_host_makeDirs(saveDir[0] ? saveDir : debugger.config.neogeo.libretro.systemDir)) {
        romset_romsetFree(&set);
        return 0;
    }
    FILE *output = fopen(outPath, "wb");
    if (!output) {
        printf("romset: cannot write generated ROM '%s': %s (check --save-dir PATH)\n",
               outPath, strerror(errno));
        romset_romsetFree(&set);
        return 0;
    }

    if (ngh == 0x253) {
        if (!romset_writeNgh253Neo(output,
                                   &set,
                                   header,
                                   sizeof(header),
                                   pSize,
                                   sSize,
                                   mSize,
                                   v1Size,
                                   cSize,
                                   ngh253Revision)) {
            fclose(output);
            romset_romsetFree(&set);
            return 0;
        }
        fclose(output);
        romset_romsetFree(&set);
        return 1;
    }

    if (ngh == 0x256) {
        if (!romset_writeNgh256Neo(output, &set, header, sizeof(header), pSize, sSize, mSize, v1Size, cSize)) {
            fclose(output);
            romset_romsetFree(&set);
            return 0;
        }
        fclose(output);
        romset_romsetFree(&set);
        return 1;
    }

    if (ngh == 0x261) {
        if (!romset_writeNgh261Neo(output, &set, header, sizeof(header), pSize, sSize, mSize, v1Size, cSize)) {
            fclose(output);
            romset_romsetFree(&set);
            return 0;
        }
        fclose(output);
        romset_romsetFree(&set);
        return 1;
    }

    if (ngh == 0x264) {
        if (!romset_writeNgh264Neo(output, &set, header, sizeof(header), pSize, sSize, mSize, v1Size, cSize)) {
            fclose(output);
            romset_romsetFree(&set);
            return 0;
        }
        fclose(output);
        romset_romsetFree(&set);
        return 1;
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
    if (ngh == 0x047) {
        if (!romset_writeNgh47CChunks(output, set.cChunks, set.cCount)) {
            fclose(output);
            romset_romsetFree(&set);
            return 0;
        }
        fclose(output);
        romset_romsetFree(&set);
        return 1;
    }

    for (size_t chunkIndex = 0; chunkIndex < set.cCount; ) {
        romset_romchunk_t *chunk = &set.cChunks[chunkIndex];
        if ((chunk->index & 1) && (chunkIndex + 1) < set.cCount && set.cChunks[chunkIndex + 1].index == (chunk->index + 1)) {
            romset_romchunk_t *nextChunk = &set.cChunks[chunkIndex + 1];
            if (chunk->size != nextChunk->size) {
                fclose(output);
                romset_romsetFree(&set);
                return 0;
            }
            if (!romset_writeChunksInterleavedRange(output, chunk, nextChunk, 0, chunk->size)) {
                fclose(output);
                romset_romsetFree(&set);
                return 0;
            }
            chunkIndex += 2;
            continue;
        }
        if (!romset_writeFileData(output, chunk, 1)) {
            fclose(output);
            romset_romsetFree(&set);
            return 0;
        }
        chunkIndex += 1;
    }
    fclose(output);
    romset_romsetFree(&set);
    return 1;
}

int
romset_buildNeoFromFolder(const char *folder, char *outPath, size_t capacity)
{
    return romset_buildNeoFromScannedInput(folder, 0, outPath, capacity);
}

int
romset_buildNeoFromZip(const char *zipPath, char *outPath, size_t capacity)
{
    return romset_buildNeoFromScannedInput(zipPath, 1, outPath, capacity);
}

int
romset_buildNeoOutputPath(const char *inputPath,
                          int isZip,
                          const char *saveDir,
                          const char *systemDir,
                          char *outPath,
                          size_t capacity)
{
    if (!outPath || capacity == 0) {
        return 0;
    }
    outPath[0] = '\0';
    const char *base = saveDir && saveDir[0] ? saveDir : systemDir;
    if (!base || !*base) {
        return 0;
    }
    char fileName[PATH_MAX];
    const char *prefix = isZip ? "e9k-zip-" : "e9k-folder-";
    if (!romset_buildGeneratedNeoFileName(fileName, sizeof(fileName), inputPath, prefix)) {
        return 0;
    }
    return debugger_platform_pathJoin(outPath, capacity, base, fileName);
}
