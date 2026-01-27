/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"

typedef struct e9ui_text_select_run {
    char *text;
    int textLen;
    TTF_Font *font;
    e9ui_component_t *owner;
    void *bucket;
    int dragOnly;
    int textX;
    int textY;
    int textW;
    int textH;
    int lineY;
    int lineHeight;
    int hitX;
    int hitW;
} e9ui_text_select_run_t;

typedef struct e9ui_text_select_bucket_state {
    void *bucket;
    uint32_t prevHash;
    uint32_t currHash;
    int prevCount;
    int currCount;
    int prevMinY;
    int prevMaxY;
    int currMinY;
    int currMaxY;
    unsigned int lastFrame;
} e9ui_text_select_bucket_state_t;

typedef struct e9ui_text_select_state {
    e9ui_text_select_run_t *runs;
    int runCount;
    int runCap;
    int selecting;
    int anchorRun;
    int anchorIndex;
    int anchorPos;
    int activeRun;
    int activeIndex;
    int activePos;
    void *activeBucket;
    int pendingRun;
    int pendingIndex;
    void *pendingBucket;
    int pendingAny;
    int pendingX;
    int pendingY;
    Uint32 lastClickMs;
    int lastClickX;
    int lastClickY;
    void *lastClickBucket;
    unsigned int frameId;
    e9ui_text_select_bucket_state_t *bucketStates;
    int bucketCount;
    int bucketCap;
    char *scratch;
    int scratchCap;
} e9ui_text_select_state_t;

static e9ui_text_select_state_t text_select_state = {
    .anchorRun = -1,
    .activeRun = -1,
    .anchorPos = -1,
    .activePos = -1,
    .pendingRun = -1,
    .pendingAny = 0
};

static void
text_select_clearRuns(e9ui_text_select_state_t *st)
{
    if (!st || !st->runs) {
        if (st) {
            st->runCount = 0;
        }
        return;
    }
    for (int i = 0; i < st->runCount; ++i) {
        e9ui_text_select_run_t *run = &st->runs[i];
        if (run->text) {
            alloc_free(run->text);
            run->text = NULL;
        }
    }
    st->runCount = 0;
}

static void
text_select_resetSelection(e9ui_text_select_state_t *st)
{
    if (!st) {
        return;
    }
    st->selecting = 0;
    st->anchorRun = -1;
    st->activeRun = -1;
    st->anchorIndex = 0;
    st->activeIndex = 0;
    st->anchorPos = -1;
    st->activePos = -1;
    st->activeBucket = NULL;
}

static void
text_select_clearInteraction(e9ui_text_select_state_t *st)
{
    if (!st) {
        return;
    }
    text_select_resetSelection(st);
    st->pendingRun = -1;
    st->pendingBucket = NULL;
    st->pendingAny = 0;
}

static void
text_select_clearPending(e9ui_text_select_state_t *st)
{
    if (!st) {
        return;
    }
    st->pendingRun = -1;
    st->pendingBucket = NULL;
    st->pendingAny = 0;
}

static int
text_select_bucketPosForRun(const e9ui_text_select_state_t *st, int runIndex, void *bucket);

static int
text_select_isWordChar(char ch)
{
    unsigned char c = (unsigned char)ch;
    return isalnum(c) || c == '_';
}

static int
text_select_selectWord(e9ui_text_select_state_t *st, int runIndex, int index)
{
    if (!st || runIndex < 0 || runIndex >= st->runCount) {
        return 0;
    }
    e9ui_text_select_run_t *run = &st->runs[runIndex];
    if (!run->text || run->textLen <= 0) {
        return 0;
    }
    int pivot = index;
    if (pivot >= run->textLen) {
        pivot = run->textLen - 1;
    }
    if (pivot < 0) {
        return 0;
    }
    int isWord = text_select_isWordChar(run->text[pivot]);
    if (!isWord && pivot > 0 && text_select_isWordChar(run->text[pivot - 1])) {
        pivot -= 1;
        isWord = 1;
    }
    if (!isWord) {
        return 0;
    }
    int start = pivot;
    while (start > 0 && text_select_isWordChar(run->text[start - 1])) {
        start -= 1;
    }
    int end = pivot + 1;
    while (end < run->textLen && text_select_isWordChar(run->text[end])) {
        end += 1;
    }
    int pos = text_select_bucketPosForRun(st, runIndex, run->bucket);
    if (pos < 0) {
        return 0;
    }
    st->selecting = 0;
    st->anchorRun = runIndex;
    st->activeRun = runIndex;
    st->anchorIndex = start;
    st->activeIndex = end;
    st->anchorPos = pos;
    st->activePos = pos;
    st->activeBucket = run->bucket;
    return 1;
}

static int
text_select_hasSelection(const e9ui_text_select_state_t *st)
{
    if (!st) {
        return 0;
    }
    if (st->anchorPos < 0 || st->activePos < 0) {
        return 0;
    }
    return st->anchorPos != st->activePos || st->anchorIndex != st->activeIndex;
}

static void
text_select_ensureScratch(e9ui_text_select_state_t *st, int len)
{
    if (!st || len <= 0) {
        return;
    }
    int need = len + 1;
    if (need <= st->scratchCap) {
        return;
    }
    int nextCap = st->scratchCap > 0 ? st->scratchCap : 64;
    while (nextCap < need) {
        nextCap *= 2;
    }
    char *next = (char*)alloc_realloc(st->scratch, (size_t)nextCap);
    if (!next) {
        return;
    }
    st->scratch = next;
    st->scratchCap = nextCap;
}

static uint32_t
text_select_hashBytes(uint32_t hash, const void *data, size_t len)
{
    const unsigned char *ptr = (const unsigned char*)data;
    for (size_t i = 0; i < len; ++i) {
        hash ^= (uint32_t)ptr[i];
        hash *= 16777619u;
    }
    return hash;
}

static uint32_t
text_select_hashInt(uint32_t hash, int value)
{
    uint32_t v = (uint32_t)value;
    return text_select_hashBytes(hash, &v, sizeof(v));
}

static e9ui_text_select_bucket_state_t *
text_select_bucketStateFind(e9ui_text_select_state_t *st, void *bucket)
{
    if (!st || !bucket) {
        return NULL;
    }
    for (int i = 0; i < st->bucketCount; ++i) {
        e9ui_text_select_bucket_state_t *state = &st->bucketStates[i];
        if (state->bucket == bucket) {
            return state;
        }
    }
    return NULL;
}

static e9ui_text_select_bucket_state_t *
text_select_bucketStateEnsure(e9ui_text_select_state_t *st, void *bucket)
{
    if (!st || !bucket) {
        return NULL;
    }
    e9ui_text_select_bucket_state_t *state = text_select_bucketStateFind(st, bucket);
    if (state) {
        return state;
    }
    if (st->bucketCount >= st->bucketCap) {
        int nextCap = st->bucketCap > 0 ? st->bucketCap * 2 : 32;
        e9ui_text_select_bucket_state_t *next = (e9ui_text_select_bucket_state_t*)alloc_realloc(
            st->bucketStates, (size_t)nextCap * sizeof(*next));
        if (!next) {
            return NULL;
        }
        st->bucketStates = next;
        st->bucketCap = nextCap;
    }
    state = &st->bucketStates[st->bucketCount++];
    memset(state, 0, sizeof(*state));
    state->bucket = bucket;
    state->prevHash = 0;
    state->currHash = 0;
    state->prevCount = 0;
    state->currCount = 0;
    state->prevMinY = 0;
    state->prevMaxY = 0;
    state->currMinY = 0;
    state->currMaxY = 0;
    state->lastFrame = 0;
    return state;
}

static e9ui_text_select_bucket_state_t *
text_select_bucketStateTouch(e9ui_text_select_state_t *st, void *bucket)
{
    e9ui_text_select_bucket_state_t *state = text_select_bucketStateEnsure(st, bucket);
    if (!state) {
        return NULL;
    }
    if (state->lastFrame != st->frameId) {
        state->lastFrame = st->frameId;
        state->currHash = 2166136261u;
        state->currCount = 0;
        state->currMinY = INT_MAX;
        state->currMaxY = INT_MIN;
    }
    return state;
}

static int
text_select_indexToX(e9ui_text_select_state_t *st, const e9ui_text_select_run_t *run, int index)
{
    if (!st || !run || !run->font) {
        return 0;
    }
    if (index <= 0) {
        return 0;
    }
    if (index >= run->textLen) {
        return run->textW;
    }
    text_select_ensureScratch(st, index);
    if (!st->scratch) {
        return 0;
    }
    memcpy(st->scratch, run->text, (size_t)index);
    st->scratch[index] = '\0';
    int width = 0;
    if (TTF_SizeText(run->font, st->scratch, &width, NULL) != 0) {
        return 0;
    }
    return width;
}

static int
text_select_indexFromX(e9ui_text_select_state_t *st, const e9ui_text_select_run_t *run, int x)
{
    if (!st || !run || !run->font) {
        return 0;
    }
    if (run->textLen <= 0) {
        return 0;
    }
    if (x <= 0) {
        return 0;
    }
    if (x >= run->textW) {
        return run->textLen;
    }
    text_select_ensureScratch(st, run->textLen);
    if (!st->scratch) {
        return 0;
    }
    for (int i = 1; i <= run->textLen; ++i) {
        memcpy(st->scratch, run->text, (size_t)i);
        st->scratch[i] = '\0';
        int width = 0;
        if (TTF_SizeText(run->font, st->scratch, &width, NULL) != 0) {
            return 0;
        }
        if (width >= x) {
            return i;
        }
    }
    return run->textLen;
}

static int
text_select_addRun(e9ui_text_select_state_t *st,
                   e9ui_component_t *owner,
                   void *bucket,
                   TTF_Font *font,
                   const char *text,
                   int dragOnly,
                   int textW,
                   int textH,
                   int textX,
                   int textY,
                   int lineY,
                   int lineHeight,
                   int hitX,
                   int hitW)
{
    if (!st || !font || !text) {
        return -1;
    }
    if (st->runCount >= st->runCap) {
        int nextCap = st->runCap > 0 ? st->runCap * 2 : 64;
        e9ui_text_select_run_t *next = (e9ui_text_select_run_t*)alloc_realloc(
            st->runs, (size_t)nextCap * sizeof(*next));
        if (!next) {
            return -1;
        }
        st->runs = next;
        st->runCap = nextCap;
    }
    int len = (int)strlen(text);
    char *copy = (char*)alloc_calloc((size_t)len + 1, 1);
    if (!copy) {
        return -1;
    }
    if (len > 0) {
        memcpy(copy, text, (size_t)len);
    }
    e9ui_text_select_run_t *run = &st->runs[st->runCount];
    run->text = copy;
    run->textLen = len;
    run->font = font;
    run->owner = owner;
    run->bucket = bucket ? bucket : owner;
    run->dragOnly = dragOnly ? 1 : 0;
    run->textX = textX;
    run->textY = textY;
    run->textW = textW;
    run->textH = textH;
    run->lineY = lineY;
    run->lineHeight = lineHeight;
    run->hitX = hitX;
    run->hitW = hitW;
    return st->runCount++;
}

static int
text_select_bucketPosForRun(const e9ui_text_select_state_t *st, int runIndex, void *bucket)
{
    if (!st || runIndex < 0 || runIndex >= st->runCount) {
        return -1;
    }
    if (!bucket) {
        return runIndex;
    }
    int pos = -1;
    for (int i = 0; i <= runIndex; ++i) {
        if (st->runs[i].bucket == bucket) {
            pos++;
        }
    }
    return pos;
}

static int
text_select_normalizeSelection(const e9ui_text_select_state_t *st,
                               int *outStartIndex,
                               int *outEndIndex,
                               int *outStartPos,
                               int *outEndPos)
{
    if (!st || !outStartIndex || !outEndIndex) {
        return 0;
    }
    int aPos = st->anchorPos;
    int bPos = st->activePos;
    int aIndex = st->anchorIndex;
    int bIndex = st->activeIndex;
    if (aPos < 0 || bPos < 0) {
        return 0;
    }
    if (aPos > bPos || (aPos == bPos && aIndex > bIndex)) {
        int tmpIndex = aIndex;
        int tmpPos = aPos;
        aIndex = bIndex;
        aPos = bPos;
        bIndex = tmpIndex;
        bPos = tmpPos;
    }
    *outStartIndex = aIndex;
    *outEndIndex = bIndex;
    if (outStartPos) {
        *outStartPos = aPos;
    }
    if (outEndPos) {
        *outEndPos = bPos;
    }
    return 1;
}

static void
text_select_drawHighlight(e9ui_context_t *ctx, int runIndex, e9ui_text_select_run_t *run)
{
    if (!ctx || !ctx->renderer || !run) {
        return;
    }
    if (!text_select_hasSelection(&text_select_state)) {
        return;
    }
    int startIndex = 0;
    int endIndex = 0;
    int startPos = 0;
    int endPos = 0;
    if (!text_select_normalizeSelection(&text_select_state, &startIndex,
                                        &endIndex, &startPos, &endPos)) {
        return;
    }
    int runPos = text_select_bucketPosForRun(&text_select_state, runIndex,
                                             text_select_state.activeBucket);
    if (runPos < startPos || runPos > endPos) {
        return;
    }
    if (text_select_state.activeBucket && run->bucket != text_select_state.activeBucket) {
        return;
    }
    int a = 0;
    int b = run->textLen;
    if (runPos == startPos) {
        a = startIndex;
    }
    if (runPos == endPos) {
        b = endIndex;
    }
    if (a < 0) {
        a = 0;
    }
    if (b > run->textLen) {
        b = run->textLen;
    }
    if (a == b) {
        return;
    }
    int x1 = run->textX + text_select_indexToX(&text_select_state, run, a);
    int x2 = run->textX + text_select_indexToX(&text_select_state, run, b);
    if (x2 < x1) {
        int tmp = x1;
        x1 = x2;
        x2 = tmp;
    }
    if (x2 <= x1) {
        return;
    }
    SDL_Rect sel = { x1, run->lineY, x2 - x1, run->lineHeight };
    SDL_SetRenderDrawColor(ctx->renderer, 70, 120, 180, 255);
    SDL_RenderFillRect(ctx->renderer, &sel);
}

static int
text_select_findRunAt(e9ui_text_select_state_t *st, int x, int y, void *bucket, int *outIndex)
{
    if (!st || st->runCount <= 0) {
        return -1;
    }
    for (int i = st->runCount - 1; i >= 0; --i) {
        e9ui_text_select_run_t *run = &st->runs[i];
        if (bucket && run->bucket != bucket) {
            continue;
        }
        if (y < run->lineY || y >= run->lineY + run->lineHeight) {
            continue;
        }
        if (x < run->hitX || x >= run->hitX + run->hitW) {
            continue;
        }
        int relX = x - run->textX;
        if (relX < 0) {
            relX = 0;
        }
        if (relX > run->textW) {
            relX = run->textW;
        }
        if (outIndex) {
            *outIndex = text_select_indexFromX(st, run, relX);
        }
        return i;
    }
    return -1;
}

static int
text_select_findClosestRun(e9ui_text_select_state_t *st, int y, void *bucket, int *outDist)
{
    if (!st || st->runCount <= 0) {
        if (outDist) {
            *outDist = 0;
        }
        return -1;
    }
    int best = -1;
    int bestDist = 0;
    for (int i = 0; i < st->runCount; ++i) {
        e9ui_text_select_run_t *run = &st->runs[i];
        if (bucket && run->bucket != bucket) {
            continue;
        }
        int dist = 0;
        if (y < run->lineY) {
            dist = run->lineY - y;
        } else if (y > run->lineY + run->lineHeight) {
            dist = y - (run->lineY + run->lineHeight);
        }
        if (best < 0 || dist < bestDist) {
            best = i;
            bestDist = dist;
        }
    }
    if (outDist) {
        *outDist = bestDist;
    }
    return best;
}

void
e9ui_text_select_beginFrame(e9ui_context_t *ctx)
{
    (void)ctx;
    text_select_state.frameId += 1;
    if (text_select_state.frameId == 0) {
        text_select_state.frameId = 1;
        for (int i = 0; i < text_select_state.bucketCount; ++i) {
            text_select_state.bucketStates[i].lastFrame = 0;
        }
    }
    text_select_clearRuns(&text_select_state);
}

void
e9ui_text_select_endFrame(e9ui_context_t *ctx)
{
    (void)ctx;
    e9ui_text_select_state_t *st = &text_select_state;
    if (st->activeBucket) {
        e9ui_text_select_bucket_state_t *bucket = text_select_bucketStateFind(st, st->activeBucket);
        int invalidate = 0;
        if (!bucket || bucket->lastFrame != st->frameId) {
            invalidate = 1;
        } else if (bucket->prevHash != bucket->currHash ||
                   bucket->prevCount != bucket->currCount ||
                   bucket->prevMinY != bucket->currMinY ||
                   bucket->prevMaxY != bucket->currMaxY) {
            invalidate = 1;
        }
        if (invalidate) {
            text_select_clearInteraction(st);
        }
    }
    for (int i = 0; i < st->bucketCount; ++i) {
        e9ui_text_select_bucket_state_t *bucket = &st->bucketStates[i];
        if (bucket->lastFrame != st->frameId) {
            continue;
        }
        bucket->prevHash = bucket->currHash;
        bucket->prevCount = bucket->currCount;
        bucket->prevMinY = bucket->currMinY;
        bucket->prevMaxY = bucket->currMaxY;
    }
}

int
e9ui_text_select_handleEvent(e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    (void)ctx;
    if (!ev) {
        return 0;
    }
    if (text_select_state.runCount <= 0) {
        if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
            text_select_resetSelection(&text_select_state);
            text_select_state.pendingRun = -1;
            text_select_state.pendingBucket = NULL;
            text_select_state.pendingAny = 0;
        }
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        int index = 0;
        int runIndex = text_select_findRunAt(&text_select_state, mx, my, NULL, &index);
        void *bucket = NULL;
        if (runIndex >= 0) {
            bucket = text_select_state.runs[runIndex].bucket;
        }
        Uint32 now = SDL_GetTicks();
        int slop = ctx ? e9ui_scale_px(ctx, 4) : 4;
        int dx = mx - text_select_state.lastClickX;
        int dy = my - text_select_state.lastClickY;
        int isDouble = 0;
        if (bucket && text_select_state.lastClickBucket == bucket &&
            text_select_state.lastClickMs > 0 &&
            now - text_select_state.lastClickMs <= 350 &&
            dx * dx + dy * dy <= slop * slop) {
            isDouble = 1;
        }
        text_select_state.lastClickMs = now;
        text_select_state.lastClickX = mx;
        text_select_state.lastClickY = my;
        text_select_state.lastClickBucket = bucket;
        if (isDouble && runIndex >= 0) {
            if (text_select_selectWord(&text_select_state, runIndex, index)) {
                text_select_clearPending(&text_select_state);
                return 1;
            }
        }
        text_select_resetSelection(&text_select_state);
        text_select_state.pendingAny = 1;
        text_select_state.pendingX = mx;
        text_select_state.pendingY = my;
        if (runIndex < 0) {
            text_select_state.pendingRun = -1;
            text_select_state.pendingBucket = NULL;
            return 0;
        }
        text_select_state.pendingRun = runIndex;
        text_select_state.pendingIndex = index;
        text_select_state.pendingBucket = text_select_state.runs[runIndex].bucket;
        return 1;
    }
    if (ev->type == SDL_MOUSEMOTION) {
        int mx = ev->motion.x;
        int my = ev->motion.y;
        if (text_select_state.pendingAny && !text_select_state.selecting) {
            int slop = ctx ? e9ui_scale_px(ctx, 4) : 4;
            int dx = mx - text_select_state.pendingX;
            int dy = my - text_select_state.pendingY;
            if (dx * dx + dy * dy >= slop * slop) {
                int index = 0;
                int runIndex = text_select_findRunAt(&text_select_state, mx, my, NULL, &index);
                if (runIndex < 0) {
                    return 0;
                }
                text_select_state.selecting = 1;
                text_select_state.activeBucket = text_select_state.runs[runIndex].bucket;
                text_select_state.pendingAny = 0;
            }
        }
        if (text_select_state.selecting) {
            int index = 0;
            int runIndex = text_select_findRunAt(&text_select_state, mx, my, NULL, &index);
            void *bucket = text_select_state.activeBucket;
            if (runIndex >= 0) {
                bucket = text_select_state.runs[runIndex].bucket;
            }
            if (!bucket) {
                return 0;
            }
            if (runIndex < 0 || text_select_state.runs[runIndex].bucket != bucket) {
                runIndex = text_select_findClosestRun(&text_select_state, my, bucket, NULL);
                if (runIndex >= 0) {
                    e9ui_text_select_run_t *run = &text_select_state.runs[runIndex];
                    int relX = mx - run->textX;
                    if (relX < 0) {
                        relX = 0;
                    }
                    if (relX > run->textW) {
                        relX = run->textW;
                    }
                    index = text_select_indexFromX(&text_select_state, run, relX);
                }
            }
            if (runIndex < 0) {
                return 0;
            }
            int anchorRun = text_select_findClosestRun(&text_select_state,
                                                       text_select_state.pendingY,
                                                       bucket, NULL);
            if (anchorRun < 0) {
                anchorRun = runIndex;
            }
            int anchorIndex = 0;
            if (anchorRun >= 0) {
                e9ui_text_select_run_t *run = &text_select_state.runs[anchorRun];
                int relX = text_select_state.pendingX - run->textX;
                if (relX < 0) {
                    relX = 0;
                }
                if (relX > run->textW) {
                    relX = run->textW;
                }
                anchorIndex = text_select_indexFromX(&text_select_state, run, relX);
            }
            int anchorPos = text_select_bucketPosForRun(&text_select_state, anchorRun, bucket);
            int activePos = text_select_bucketPosForRun(&text_select_state, runIndex, bucket);
            if (anchorPos < 0 || activePos < 0) {
                return 0;
            }
            text_select_state.activeBucket = bucket;
            text_select_state.anchorRun = anchorRun;
            text_select_state.anchorIndex = anchorIndex;
            text_select_state.anchorPos = anchorPos;
            text_select_state.activeRun = runIndex;
            text_select_state.activeIndex = index;
            text_select_state.activePos = activePos;
            return 1;
        }
    }
    if (ev->type == SDL_MOUSEBUTTONUP && ev->button.button == SDL_BUTTON_LEFT) {
        text_select_state.selecting = 0;
        text_select_state.pendingRun = -1;
        text_select_state.pendingBucket = NULL;
        text_select_state.pendingAny = 0;
        return 1;
    }
    return 0;
}

int
e9ui_text_select_hasSelection(void)
{
    return text_select_hasSelection(&text_select_state);
}

int
e9ui_text_select_isSelecting(void)
{
    return text_select_state.selecting || text_select_state.pendingAny;
}

void
e9ui_text_select_copyToClipboard(void)
{
    if (!text_select_hasSelection(&text_select_state)) {
        return;
    }
    int startIndex = 0;
    int endIndex = 0;
    int startPos = 0;
    int endPos = 0;
    if (!text_select_normalizeSelection(&text_select_state, &startIndex,
                                        &endIndex, &startPos, &endPos)) {
        return;
    }
    size_t total = 0;
    for (int i = 0; i < text_select_state.runCount; ++i) {
        e9ui_text_select_run_t *run = &text_select_state.runs[i];
        int runPos = text_select_bucketPosForRun(&text_select_state, i,
                                                 text_select_state.activeBucket);
        if (text_select_state.activeBucket && run->bucket != text_select_state.activeBucket) {
            continue;
        }
        if (runPos < startPos || runPos > endPos) {
            continue;
        }
        int a = 0;
        int b = run->textLen;
        if (runPos == startPos) {
            a = startIndex;
        }
        if (runPos == endPos) {
            b = endIndex;
        }
        if (a < 0) {
            a = 0;
        }
        if (b > run->textLen) {
            b = run->textLen;
        }
        if (b < a) {
            int tmp = a;
            a = b;
            b = tmp;
        }
        total += (size_t)(b - a);
        if (runPos != endPos) {
            total += 1;
        }
    }
    char *buf = (char*)alloc_calloc(total + 1, 1);
    if (!buf) {
        return;
    }
    size_t pos = 0;
    for (int i = 0; i < text_select_state.runCount; ++i) {
        e9ui_text_select_run_t *run = &text_select_state.runs[i];
        int runPos = text_select_bucketPosForRun(&text_select_state, i,
                                                 text_select_state.activeBucket);
        if (text_select_state.activeBucket && run->bucket != text_select_state.activeBucket) {
            continue;
        }
        if (runPos < startPos || runPos > endPos) {
            continue;
        }
        int a = 0;
        int b = run->textLen;
        if (runPos == startPos) {
            a = startIndex;
        }
        if (runPos == endPos) {
            b = endIndex;
        }
        if (a < 0) {
            a = 0;
        }
        if (b > run->textLen) {
            b = run->textLen;
        }
        if (b < a) {
            int tmp = a;
            a = b;
            b = tmp;
        }
        int len = b - a;
        if (len > 0) {
            memcpy(&buf[pos], &run->text[a], (size_t)len);
            pos += (size_t)len;
        }
        if (runPos != endPos) {
            buf[pos++] = '\n';
        }
    }
    SDL_SetClipboardText(buf);
    alloc_free(buf);
}

void
e9ui_text_select_clear(void)
{
    text_select_clearInteraction(&text_select_state);
}

void
e9ui_text_select_shutdown(void)
{
    text_select_clearRuns(&text_select_state);
    if (text_select_state.runs) {
        alloc_free(text_select_state.runs);
    }
    text_select_state.runs = NULL;
    text_select_state.runCap = 0;
    if (text_select_state.scratch) {
        alloc_free(text_select_state.scratch);
    }
    text_select_state.scratch = NULL;
    text_select_state.scratchCap = 0;
    if (text_select_state.bucketStates) {
        alloc_free(text_select_state.bucketStates);
    }
    text_select_state.bucketStates = NULL;
    text_select_state.bucketCount = 0;
    text_select_state.bucketCap = 0;
    text_select_state.frameId = 0;
    text_select_clearInteraction(&text_select_state);
}

void
e9ui_text_select_drawText(e9ui_context_t *ctx,
                          e9ui_component_t *owner,
                          TTF_Font *font,
                          const char *text,
                          SDL_Color color,
                          int x,
                          int y,
                          int lineHeight,
                          int hitW,
                          void *bucket,
                          int dragOnly,
                          int selectable)
{
    if (!ctx || !ctx->renderer || !font || !text) {
        return;
    }
    if (lineHeight <= 0) {
        lineHeight = TTF_FontHeight(font);
        if (lineHeight <= 0) {
            lineHeight = 16;
        }
    }
    int tw = 0;
    int th = 0;
    SDL_Texture *tex = NULL;
    int hasText = text[0] ? 1 : 0;
    if (hasText) {
        tex = e9ui_text_cache_getText(ctx->renderer, font, text, color, &tw, &th);
    }
    if (hitW <= 0) {
        hitW = tw;
    }
    if (hitW < 0) {
        hitW = 0;
    }
    if (selectable) {
        int runIndex = text_select_addRun(&text_select_state, owner, bucket, font, text, dragOnly,
                                          tw, th,
                                          x, y, y, lineHeight, x, hitW);
        if (runIndex >= 0) {
            e9ui_text_select_run_t *run = &text_select_state.runs[runIndex];
            e9ui_text_select_bucket_state_t *state = text_select_bucketStateTouch(&text_select_state, run->bucket);
            if (state) {
                uint32_t runHash = 2166136261u;
                if (run->textLen > 0) {
                    runHash = text_select_hashBytes(runHash, run->text, (size_t)run->textLen);
                }
                runHash = text_select_hashInt(runHash, run->textLen);
                runHash = text_select_hashInt(runHash, run->textX);
                runHash = text_select_hashInt(runHash, run->lineY);
                runHash = text_select_hashInt(runHash, run->lineHeight);
                runHash = text_select_hashInt(runHash, run->hitW);
                state->currHash = text_select_hashInt(state->currHash, (int)runHash);
                state->currCount += 1;
                if (run->lineY < state->currMinY) {
                    state->currMinY = run->lineY;
                }
                int bottom = run->lineY + run->lineHeight;
                if (bottom > state->currMaxY) {
                    state->currMaxY = bottom;
                }
            }
            text_select_drawHighlight(ctx, runIndex, &text_select_state.runs[runIndex]);
        }
    }
    if (tex) {
        SDL_Rect dst = { x, y, tw, th };
        SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
    }
}
