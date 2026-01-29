/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include "e9ui.h"
#include <ctype.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#endif

typedef struct textbox_state {
    char               *text;
    int                len;
    int                cursor;
    int                sel_start;
    int                sel_end;
    int                selecting;
    Uint32             last_click_ms;
    int                double_click_active;
    list_t            *undo;
    list_t            *redo;
    int                maxLen;
    int                scrollX;
    int                editable;
    int                numeric_only;
    char               *placeholder;
    char               *scratch;
    e9ui_textbox_submit_cb_t submit;
    e9ui_textbox_change_cb_t change;
    e9ui_textbox_key_cb_t key_cb;
    void               *key_user;
    void               *user;
    int                frame_visible;
    e9ui_textbox_completion_mode_t completionMode;
    char               **completionList;
    int                completionCount;
    int                completionCap;
    int                completionSel;
    int                completionPrefixLen;
    char               *completionPrefix;
    char               *completionRest;
} textbox_state_t;

typedef struct textbox_snapshot {
    char *text;
    int len;
    int cursor;
    int sel_start;
    int sel_end;
} textbox_snapshot_t;

static void
textbox_recordUndo(textbox_state_t *st);

static void
textbox_fillScratch(textbox_state_t *st, int count)
{
    if (!st || !st->scratch) {
        return;
    }
    if (count < 0) {
        count = 0;
    }
    if (count > st->len) {
        count = st->len;
    }
    if (count > 0) {
        memcpy(st->scratch, st->text, (size_t)count);
    }
    st->scratch[count] = '\0';
}

static void
textbox_updateScroll(textbox_state_t *st, TTF_Font *font, int viewW)
{
    if (!st || !font || viewW <= 0) {
        return;
    }
    int cursorX = 0;
    if (st->cursor > 0) {
        textbox_fillScratch(st, st->cursor);
        TTF_SizeText(font, st->scratch, &cursorX, NULL);
    }
    int totalW = 0;
    textbox_fillScratch(st, st->len);
    TTF_SizeText(font, st->scratch, &totalW, NULL);
    if (totalW < viewW) {
        st->scrollX = 0;
        return;
    }
    int maxOffset = totalW - viewW;
    int desired = cursorX;
    if (desired < st->scrollX) {
        st->scrollX = desired;
    } else if (desired > st->scrollX + viewW) {
        st->scrollX = desired - viewW;
    }
    if (st->scrollX < 0) {
        st->scrollX = 0;
    }
    if (st->scrollX > maxOffset) {
        st->scrollX = maxOffset;
    }
}

static void
textbox_notifyChange(textbox_state_t *st, e9ui_context_t *ctx)
{
    if (!st || !st->change) {
        return;
    }
    st->change(ctx, st->user);
}

static int
textbox_hasSelection(const textbox_state_t *st)
{
    if (!st) {
        return 0;
    }
    return st->sel_start != st->sel_end;
}

static void
textbox_clearSelection(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    st->sel_start = st->cursor;
    st->sel_end = st->cursor;
    st->selecting = 0;
}

static void
textbox_normalizeSelection(const textbox_state_t *st, int *out_a, int *out_b)
{
    int a = st ? st->sel_start : 0;
    int b = st ? st->sel_end : 0;
    if (a > b) {
        int tmp = a;
        a = b;
        b = tmp;
    }
    if (out_a) {
        *out_a = a;
    }
    if (out_b) {
        *out_b = b;
    }
}

static int
textbox_deleteSelection(textbox_state_t *st)
{
    if (!st || !textbox_hasSelection(st)) {
        return 0;
    }
    int a = 0;
    int b = 0;
    textbox_normalizeSelection(st, &a, &b);
    if (a < 0) {
        a = 0;
    }
    if (b > st->len) {
        b = st->len;
    }
    if (b <= a) {
        textbox_clearSelection(st);
        return 0;
    }
    memmove(&st->text[a], &st->text[b], (size_t)(st->len - b + 1));
    st->len -= (b - a);
    st->cursor = a;
    textbox_clearSelection(st);
    return 1;
}

static void
textbox_snapshot_free(textbox_snapshot_t *snap)
{
    if (!snap) {
        return;
    }
    if (snap->text) {
        alloc_free(snap->text);
    }
    alloc_free(snap);
}

static void
textbox_history_clear(list_t **list)
{
    if (!list) {
        return;
    }
    list_t *ptr = *list;
    while (ptr) {
        list_t *next = ptr->next;
        textbox_snapshot_free((textbox_snapshot_t*)ptr->data);
        alloc_free(ptr);
        ptr = next;
    }
    *list = NULL;
}

static textbox_snapshot_t *
textbox_history_pop(list_t **list)
{
    if (!list || !*list) {
        return NULL;
    }
    list_t *last = list_last(*list);
    if (!last) {
        return NULL;
    }
    textbox_snapshot_t *snap = (textbox_snapshot_t*)last->data;
    list_remove(list, snap, 0);
    return snap;
}

static void
textbox_history_push(list_t **list, textbox_snapshot_t *snap)
{
    if (!list || !snap) {
        return;
    }
    list_append(list, snap);
}

static textbox_snapshot_t *
textbox_snapshot_create(const textbox_state_t *st)
{
    if (!st) {
        return NULL;
    }
    textbox_snapshot_t *snap = (textbox_snapshot_t*)alloc_calloc(1, sizeof(*snap));
    if (!snap) {
        return NULL;
    }
    snap->len = st->len;
    snap->cursor = st->cursor;
    snap->sel_start = st->sel_start;
    snap->sel_end = st->sel_end;
    snap->text = (char*)alloc_calloc((size_t)st->len + 1, 1);
    if (!snap->text) {
        alloc_free(snap);
        return NULL;
    }
    memcpy(snap->text, st->text, (size_t)st->len);
    snap->text[st->len] = '\0';
    return snap;
}

static void
textbox_snapshot_apply(textbox_state_t *st, const textbox_snapshot_t *snap)
{
    if (!st || !snap) {
        return;
    }
    int len = snap->len;
    if (len > st->maxLen) {
        len = st->maxLen;
    }
    memcpy(st->text, snap->text, (size_t)len);
    st->text[len] = '\0';
    st->len = len;
    st->cursor = snap->cursor;
    if (st->cursor < 0) st->cursor = 0;
    if (st->cursor > st->len) st->cursor = st->len;
    st->sel_start = snap->sel_start;
    st->sel_end = snap->sel_end;
    if (st->sel_start < 0) st->sel_start = 0;
    if (st->sel_end < 0) st->sel_end = 0;
    if (st->sel_start > st->len) st->sel_start = st->len;
    if (st->sel_end > st->len) st->sel_end = st->len;
}

static void
textbox_completionClearList(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    if (st->completionList) {
        for (int i = 0; i < st->completionCount; ++i) {
            alloc_free(st->completionList[i]);
        }
        alloc_free(st->completionList);
    }
    st->completionList = NULL;
    st->completionCount = 0;
    st->completionCap = 0;
    st->completionSel = -1;
}

static void
textbox_completionClear(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    textbox_completionClearList(st);
    st->completionPrefixLen = 0;
    if (st->completionPrefix) {
        st->completionPrefix[0] = '\0';
    }
    if (st->completionRest) {
        st->completionRest[0] = '\0';
    }
}

static int
textbox_pathJoin(char *out, size_t cap, const char *dir, const char *name)
{
    if (!out || cap == 0 || !dir || !*dir || !name || !*name) {
        return 0;
    }
    size_t dlen = strlen(dir);
    size_t nlen = strlen(name);
    int needSep = (dlen > 0 && dir[dlen - 1] != '/' && dir[dlen - 1] != '\\');
    size_t total = dlen + (needSep ? 1 : 0) + nlen;
    if (total + 1 > cap) {
        return 0;
    }
    memcpy(out, dir, dlen);
    size_t pos = dlen;
    if (needSep) {
#ifdef _WIN32
        out[pos++] = '\\';
#else
        out[pos++] = '/';
#endif
    }
    memcpy(out + pos, name, nlen);
    out[pos + nlen] = '\0';
    return 1;
}

static int
textbox_expandTilde(const char *in, char *out, size_t cap)
{
    if (!in || !out || cap == 0) {
        return 0;
    }
    if (in[0] != '~' || (in[1] != '\0' && in[1] != '/' && in[1] != '\\')) {
        strncpy(out, in, cap - 1);
        out[cap - 1] = '\0';
        return 1;
    }
#ifdef _WIN32
    const char *home = getenv("USERPROFILE");
    if (!home || !*home) {
        home = getenv("APPDATA");
    }
#else
    const char *home = getenv("HOME");
#endif
    if (!home || !*home) {
        strncpy(out, in, cap - 1);
        out[cap - 1] = '\0';
        return 1;
    }
    size_t hlen = strlen(home);
    const char *rest = in + 1;
    if (*rest == '/' || *rest == '\\') {
        rest++;
    }
    size_t rlen = strlen(rest);
    size_t needSep = (hlen > 0 && home[hlen - 1] != '/' && home[hlen - 1] != '\\') ? 1 : 0;
    if (hlen + needSep + rlen + 1 > cap) {
        return 0;
    }
    memcpy(out, home, hlen);
    size_t pos = hlen;
    if (needSep) {
#ifdef _WIN32
        out[pos++] = '\\';
#else
        out[pos++] = '/';
#endif
    }
    memcpy(out + pos, rest, rlen);
    out[pos + rlen] = '\0';
    return 1;
}

static int
textbox_startsWith(const char *s, const char *prefix, int caseInsensitive)
{
    if (!s || !prefix) {
        return 0;
    }
    size_t plen = strlen(prefix);
    if (plen == 0) {
        return 1;
    }
    if (strlen(s) < plen) {
        return 0;
    }
    if (!caseInsensitive) {
        return strncmp(s, prefix, plen) == 0;
    }
    for (size_t i = 0; i < plen; ++i) {
        unsigned char a = (unsigned char)s[i];
        unsigned char b = (unsigned char)prefix[i];
        if (tolower(a) != tolower(b)) {
            return 0;
        }
    }
    return 1;
}

static int
textbox_isDirPath(const char *path)
{
    if (!path || !*path) {
        return 0;
    }
#ifdef _WIN32
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }
    return (attrs & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
#else
    struct stat sb;
    if (stat(path, &sb) != 0) {
        return 0;
    }
    return S_ISDIR(sb.st_mode) ? 1 : 0;
#endif
}

static size_t
textbox_commonPrefixLen(const char * const *cands, int count, int caseInsensitive)
{
    if (!cands || count <= 0 || !cands[0]) {
        return 0;
    }
    size_t commonLen = strlen(cands[0]);
    for (int i = 1; i < count; ++i) {
        const char *cand = cands[i] ? cands[i] : "";
        size_t j = 0;
        size_t limit = strlen(cand);
        if (limit < commonLen) {
            commonLen = limit;
        }
        while (j < commonLen) {
            unsigned char a = (unsigned char)cands[0][j];
            unsigned char b = (unsigned char)cand[j];
            if (caseInsensitive) {
                a = (unsigned char)tolower(a);
                b = (unsigned char)tolower(b);
            }
            if (a != b) {
                break;
            }
            ++j;
        }
        commonLen = j;
        if (commonLen == 0) {
            break;
        }
    }
    return commonLen;
}

static int
textbox_completionCompare(const void *a, const void *b)
{
    const char *sa = *(const char * const *)a;
    const char *sb = *(const char * const *)b;
    if (!sa) {
        sa = "";
    }
    if (!sb) {
        sb = "";
    }
#ifdef _WIN32
    while (*sa && *sb) {
        unsigned char ca = (unsigned char)tolower((unsigned char)*sa);
        unsigned char cb = (unsigned char)tolower((unsigned char)*sb);
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
        ++sa;
        ++sb;
    }
    if (*sa == *sb) {
        return 0;
    }
    return *sa ? 1 : -1;
#else
    return strcmp(sa, sb);
#endif
}

static int
textbox_buildFilenameCompletions(textbox_state_t *st, const char *dirPath, const char *fragment, int foldersOnly)
{
    if (!st || !dirPath) {
        return 0;
    }
    textbox_completionClearList(st);

    const char *dir = dirPath;
    if (!*dir) {
        dir = ".";
    }
    int caseInsensitive = 0;
#ifdef _WIN32
    caseInsensitive = 1;
#endif

#ifdef _WIN32
    char pattern[PATH_MAX];
    if (!textbox_pathJoin(pattern, sizeof(pattern), dir, "*")) {
        return 0;
    }
    WIN32_FIND_DATAA data;
    HANDLE h = FindFirstFileA(pattern, &data);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        const char *name = data.cFileName;
        if (!name || !*name) {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        int isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        if (foldersOnly && !isDir) {
            continue;
        }
        if (fragment && *fragment && !textbox_startsWith(name, fragment, caseInsensitive)) {
            continue;
        }
        if (st->completionCount >= st->completionCap) {
            int next = st->completionCap ? st->completionCap * 2 : 64;
            char **tmp = (char**)alloc_realloc(st->completionList, (size_t)next * sizeof(char*));
            if (!tmp) {
                break;
            }
            st->completionList = tmp;
            st->completionCap = next;
        }
        if (isDir) {
            size_t nlen = strlen(name);
            char *cand = (char*)alloc_alloc(nlen + 2);
            if (!cand) {
                continue;
            }
            memcpy(cand, name, nlen);
            cand[nlen] = '\\';
            cand[nlen + 1] = '\0';
            st->completionList[st->completionCount++] = cand;
        } else {
            st->completionList[st->completionCount++] = alloc_strdup(name);
        }
    } while (FindNextFileA(h, &data));
    FindClose(h);
#else
    DIR *dp = opendir(dir);
    if (!dp) {
        return 0;
    }
    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        const char *name = ent->d_name;
        if (!name || !*name) {
            continue;
        }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }
        if (fragment && *fragment && !textbox_startsWith(name, fragment, caseInsensitive)) {
            continue;
        }
        int isDir = 0;
        {
            char full[PATH_MAX];
            if (!textbox_pathJoin(full, sizeof(full), dir, name)) {
                continue;
            }
            isDir = textbox_isDirPath(full) ? 1 : 0;
        }
        if (foldersOnly && !isDir) {
            continue;
        }
        if (st->completionCount >= st->completionCap) {
            int next = st->completionCap ? st->completionCap * 2 : 64;
            char **tmp = (char**)alloc_realloc(st->completionList, (size_t)next * sizeof(char*));
            if (!tmp) {
                break;
            }
            st->completionList = tmp;
            st->completionCap = next;
        }
        if (isDir) {
            size_t nlen = strlen(name);
            char *cand = (char*)alloc_alloc(nlen + 2);
            if (!cand) {
                continue;
            }
            memcpy(cand, name, nlen);
            cand[nlen] = '/';
            cand[nlen + 1] = '\0';
            st->completionList[st->completionCount++] = cand;
        } else {
            st->completionList[st->completionCount++] = alloc_strdup(name);
        }
    }
    closedir(dp);
#endif

    if (st->completionCount <= 0) {
        textbox_completionClear(st);
        return 0;
    }
    qsort(st->completionList, (size_t)st->completionCount, sizeof(char*), textbox_completionCompare);
    return 1;
}

static int
textbox_applyFilenameCompletionChoice(textbox_state_t *st, e9ui_context_t *ctx, TTF_Font *font, int viewW, const char *choiceText)
{
    if (!st || !choiceText || !st->completionPrefix || !st->completionRest) {
        return 0;
    }
    int prelen = st->completionPrefixLen;
    if (prelen < 0) {
        prelen = 0;
    }
    int maxLen = st->maxLen;
    if (maxLen <= 0) {
        return 0;
    }
    st->scratch[0] = '\0';
    int nl = 0;

    size_t prefixLen = strlen(st->completionPrefix);
    if ((int)prefixLen > maxLen) {
        prefixLen = (size_t)maxLen;
    }
    memcpy(st->scratch, st->completionPrefix, prefixLen);
    nl = (int)prefixLen;
    st->scratch[nl] = '\0';

    size_t clen = strlen(choiceText);
    if (nl + (int)clen > maxLen) {
        clen = (size_t)(maxLen - nl);
    }
    memcpy(st->scratch + nl, choiceText, clen);
    nl += (int)clen;
    st->scratch[nl] = '\0';

    int addSep = 0;
    if (nl < maxLen) {
        char full[PATH_MAX];
        const char *dirForCheck = (st->completionPrefix[0] != '\0') ? st->completionPrefix : ".";
        if (textbox_pathJoin(full, sizeof(full), dirForCheck, choiceText) && textbox_isDirPath(full)) {
            char last = (nl > 0) ? st->scratch[nl - 1] : '\0';
            if (last != '/' && last != '\\') {
#ifdef _WIN32
                st->scratch[nl++] = '\\';
#else
                st->scratch[nl++] = '/';
#endif
                st->scratch[nl] = '\0';
                addSep = 1;
            }
        }
    }

    size_t rl = strlen(st->completionRest);
    if (nl + (int)rl > maxLen) {
        rl = (size_t)(maxLen - nl);
    }
    memcpy(st->scratch + nl, st->completionRest, rl);
    nl += (int)rl;
    st->scratch[nl] = '\0';

    textbox_recordUndo(st);
    memcpy(st->text, st->scratch, (size_t)nl + 1);
    st->len = nl;
    st->cursor = prelen + (int)strlen(choiceText) + addSep;
    if (st->cursor > st->len) {
        st->cursor = st->len;
    }
    textbox_clearSelection(st);
    textbox_notifyChange(st, ctx);
    textbox_updateScroll(st, font, viewW);
    return 1;
}

static int
textbox_filenameCompletion(textbox_state_t *st, e9ui_context_t *ctx, TTF_Font *font, int viewW, int reverse)
{
    if (!st || !ctx || !font) {
        return 0;
    }
    if (st->completionMode == e9ui_textbox_completion_none) {
        return 0;
    }

    if (st->completionList && st->completionCount > 0) {
        int total = st->completionCount;
        if (st->completionSel < 0) {
            st->completionSel = reverse ? total - 1 : 0;
        } else {
            int next = st->completionSel + (reverse ? -1 : 1);
            if (next < 0) {
                next = total - 1;
            }
            if (next >= total) {
                next = 0;
            }
            st->completionSel = next;
        }
        const char *cand = st->completionList[st->completionSel] ? st->completionList[st->completionSel] : "";
        return textbox_applyFilenameCompletionChoice(st, ctx, font, viewW, cand);
    }

    const char *text = st->text ? st->text : "";
    int cursor = st->cursor;
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > st->len) {
        cursor = st->len;
    }

    int tokenStart = cursor;
    while (tokenStart > 0) {
        char ch = text[tokenStart - 1];
        if (ch == '/' || ch == '\\') {
            break;
        }
        tokenStart--;
    }
    int fragmentLen = cursor - tokenStart;
    if (fragmentLen < 0) {
        fragmentLen = 0;
    }
    if (tokenStart < 0) {
        tokenStart = 0;
    }
    if (tokenStart > st->len) {
        tokenStart = st->len;
    }

    if (!st->completionPrefix || !st->completionRest) {
        return 1;
    }

    char prefixRaw[PATH_MAX];
    size_t pl = (size_t)tokenStart;
    if (pl >= sizeof(prefixRaw)) {
        pl = sizeof(prefixRaw) - 1;
    }
    memcpy(prefixRaw, text, pl);
    prefixRaw[pl] = '\0';

    strncpy(st->completionPrefix, prefixRaw, (size_t)st->maxLen);
    st->completionPrefix[st->maxLen] = '\0';
    st->completionPrefixLen = (int)strlen(st->completionPrefix);

    const char *rest = &text[cursor];
    strncpy(st->completionRest, rest, (size_t)st->maxLen);
    st->completionRest[st->maxLen] = '\0';

    char fragment[PATH_MAX];
    size_t fl = (size_t)fragmentLen;
    if (fl >= sizeof(fragment)) {
        fl = sizeof(fragment) - 1;
    }
    memcpy(fragment, &text[tokenStart], fl);
    fragment[fl] = '\0';

    char dirExpanded[PATH_MAX];
    if (!textbox_expandTilde(prefixRaw, dirExpanded, sizeof(dirExpanded))) {
        strncpy(dirExpanded, prefixRaw, sizeof(dirExpanded) - 1);
        dirExpanded[sizeof(dirExpanded) - 1] = '\0';
    }
    const char *dirToOpen = dirExpanded;
    if (!dirToOpen || !*dirToOpen) {
        dirToOpen = ".";
    }
    int foldersOnly = (st->completionMode == e9ui_textbox_completion_folder) ? 1 : 0;
    if (!textbox_buildFilenameCompletions(st, dirToOpen, fragment, foldersOnly)) {
        return 1;
    }

    int count = st->completionCount;
    int caseInsensitive = 0;
#ifdef _WIN32
    caseInsensitive = 1;
#endif
    if (count == 1) {
        const char *cand = st->completionList[0] ? st->completionList[0] : "";
        textbox_applyFilenameCompletionChoice(st, ctx, font, viewW, cand);
        textbox_completionClear(st);
        return 1;
    }
    size_t commonLen = textbox_commonPrefixLen((const char * const *)st->completionList, count, caseInsensitive);
    if ((int)commonLen > fragmentLen) {
        char common[PATH_MAX];
        size_t clen = commonLen;
        if (clen >= sizeof(common)) {
            clen = sizeof(common) - 1;
        }
        memcpy(common, st->completionList[0], clen);
        common[clen] = '\0';
        textbox_applyFilenameCompletionChoice(st, ctx, font, viewW, common);
        textbox_completionClear(st);
        return 1;
    }

    st->completionSel = reverse ? count - 1 : 0;
    const char *cand = st->completionList[st->completionSel] ? st->completionList[st->completionSel] : "";
    textbox_applyFilenameCompletionChoice(st, ctx, font, viewW, cand);
    return 1;
}

static void
textbox_recordUndo(textbox_state_t *st)
{
    if (!st) {
        return;
    }
    textbox_snapshot_t *snap = textbox_snapshot_create(st);
    if (!snap) {
        return;
    }
    textbox_history_push(&st->undo, snap);
    textbox_history_clear(&st->redo);
}

static void
textbox_doUndo(textbox_state_t *st, e9ui_context_t *ctx, TTF_Font *font, int viewW)
{
    if (!st) {
        return;
    }
    textbox_snapshot_t *snap = textbox_history_pop(&st->undo);
    if (!snap) {
        return;
    }
    textbox_snapshot_t *cur = textbox_snapshot_create(st);
    if (cur) {
        textbox_history_push(&st->redo, cur);
    }
    textbox_snapshot_apply(st, snap);
    textbox_snapshot_free(snap);
    textbox_notifyChange(st, ctx);
    textbox_updateScroll(st, font, viewW);
}

static void
textbox_doRedo(textbox_state_t *st, e9ui_context_t *ctx, TTF_Font *font, int viewW)
{
    if (!st) {
        return;
    }
    textbox_snapshot_t *snap = textbox_history_pop(&st->redo);
    if (!snap) {
        return;
    }
    textbox_snapshot_t *cur = textbox_snapshot_create(st);
    if (cur) {
        textbox_history_push(&st->undo, cur);
    }
    textbox_snapshot_apply(st, snap);
    textbox_snapshot_free(snap);
    textbox_notifyChange(st, ctx);
    textbox_updateScroll(st, font, viewW);
}

static void
textbox_insertText(textbox_state_t *st, const char *text, int len)
{
    if (!st || !text || len <= 0) {
        return;
    }
    const char *src = text;
    if (st->numeric_only) {
        if (!st->scratch) {
            return;
        }
        int out = 0;
        for (int i = 0; i < len; ++i) {
            char c = text[i];
            if (c >= '0' && c <= '9') {
                st->scratch[out++] = c;
            }
        }
        if (out <= 0) {
            return;
        }
        st->scratch[out] = '\0';
        src = st->scratch;
        len = out;
    }
    int space = st->maxLen - st->len;
    if (space <= 0) {
        return;
    }
    if (len > space) {
        len = space;
    }
    memmove(&st->text[st->cursor + len], &st->text[st->cursor], (size_t)(st->len - st->cursor + 1));
    memcpy(&st->text[st->cursor], src, (size_t)len);
    st->len += len;
    st->cursor += len;
    textbox_clearSelection(st);
}
static int
textbox_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)self;
    (void)availW;
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : (ctx ? ctx->font : NULL);
    int lh = font ? TTF_FontHeight(font) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    return lh + 12;
}

static void
textbox_layoutComp(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    (void)ctx;
    self->bounds = bounds;
}

static void
textbox_renderComp(e9ui_component_t *self, e9ui_context_t *ctx)
{
    if (!self || !ctx || !ctx->renderer) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st) {
        return;
    }
    SDL_Rect area = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
    if (st->frame_visible) {
        SDL_SetRenderDrawColor(ctx->renderer, 30, 30, 34, 255);
        SDL_RenderFillRect(ctx->renderer, &area);
        SDL_Color borderCol = (e9ui_getFocus(ctx) == self) ? (SDL_Color){96,148,204,255} : (SDL_Color){80,80,90,255};
        SDL_SetRenderDrawColor(ctx->renderer, borderCol.r, borderCol.g, borderCol.b, borderCol.a);
        SDL_RenderDrawRect(ctx->renderer, &area);
    }
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    if (!font) {
        return;
    }
    const int padPx = 8;
    const int viewW = area.w - padPx * 2;
    if (viewW <= 0) {
        return;
    }
    const char *display = (st->len > 0) ? st->text : (st->placeholder ? st->placeholder : "");
    SDL_Color textCol = st->len > 0 ? (SDL_Color){230,230,230,255} : (SDL_Color){150,150,170,255};
    if (!st->editable) {
        textCol = (SDL_Color){110,110,130,255};
    }
    if (st->len > 0) {
        textbox_updateScroll(st, font, viewW);
        if (textbox_hasSelection(st)) {
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            if (a < 0) a = 0;
            if (b > st->len) b = st->len;
            if (b > a) {
                textbox_fillScratch(st, a);
                int startPx = 0;
                TTF_SizeText(font, st->scratch, &startPx, NULL);
                textbox_fillScratch(st, b);
                int endPx = 0;
                TTF_SizeText(font, st->scratch, &endPx, NULL);
                int selX1 = area.x + padPx + startPx - st->scrollX;
                int selX2 = area.x + padPx + endPx - st->scrollX;
                if (selX2 < selX1) {
                    int tmp = selX1;
                    selX1 = selX2;
                    selX2 = tmp;
                }
                int clipL = area.x + padPx;
                int clipR = area.x + padPx + viewW;
                if (selX1 < clipL) selX1 = clipL;
                if (selX2 > clipR) selX2 = clipR;
                if (selX2 > selX1) {
                    int lh = TTF_FontHeight(font);
                    if (lh <= 0) lh = 16;
                    int selY = area.y + (area.h - lh) / 2;
                    SDL_Rect sel = { selX1, selY, selX2 - selX1, lh };
                    SDL_SetRenderDrawColor(ctx->renderer, 70, 120, 180, 255);
                    SDL_RenderFillRect(ctx->renderer, &sel);
                }
            }
        }
        textbox_fillScratch(st, st->len);
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, st->scratch, textCol, &tw, &th);
        if (tex) {
            SDL_Rect src = { st->scrollX, 0, viewW, th };
            if (src.w > tw - src.x) {
                src.w = tw - src.x;
            }
            if (src.w < 0) {
                src.w = 0;
            }
            SDL_Rect dst = { area.x + padPx, area.y + (area.h - th) / 2, src.w, th };
            if (src.w > 0) {
                SDL_RenderCopy(ctx->renderer, tex, &src, &dst);
            }
        }
    } else if (display && *display) {
        int tw = 0;
        int th = 0;
        SDL_Texture *tex = e9ui_text_cache_getText(ctx->renderer, font, display, textCol, &tw, &th);
        if (tex) {
            SDL_Rect dst = { area.x + padPx, area.y + (area.h - th) / 2, tw, th };
            SDL_RenderCopy(ctx->renderer, tex, NULL, &dst);
        }
    }
    if (e9ui_getFocus(ctx) == self && st->editable) {
        textbox_fillScratch(st, st->cursor);
        int caretPx = 0;
        TTF_SizeText(font, st->scratch, &caretPx, NULL);
        int caretX = area.x + padPx + caretPx - st->scrollX;
        if (caretX < area.x + padPx) {
            caretX = area.x + padPx;
        }
        if (caretX > area.x + area.w - padPx) {
            caretX = area.x + area.w - padPx;
        }
        int lh = TTF_FontHeight(font);
        if (lh <= 0) {
            lh = 16;
        }
        SDL_SetRenderDrawColor(ctx->renderer, 230, 230, 230, 255);
        SDL_RenderDrawLine(ctx->renderer, caretX, area.y + (area.h - lh) / 2,
                           caretX, area.y + (area.h + lh) / 2);
    }
}

static void
textbox_repositionCursor(textbox_state_t *st, e9ui_component_t *self, TTF_Font *font, int mouseX)
{
    if (!st || !self || !font) {
        return;
    }
    const int padPx = 8;
    int target = mouseX - (self->bounds.x + padPx) + st->scrollX;
    if (target < 0) {
        target = 0;
    }
    int best = 0;
    for (int i = 0; i <= st->len; ++i) {
        textbox_fillScratch(st, i);
        int width = 0;
        TTF_SizeText(font, st->scratch, &width, NULL);
        if (width >= target) {
            st->cursor = i;
            best = 1;
            break;
        }
    }
    if (!best) {
        st->cursor = st->len;
    }
    int viewW = self->bounds.w - padPx * 2;
    textbox_updateScroll(st, font, viewW);
}

static void
textbox_onMouseDown(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st || !st->editable) {
        return;
    }
    if (ev->button != E9UI_MOUSE_BUTTON_LEFT) {
        return;
    }
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    Uint32 now = SDL_GetTicks();
    if (st->double_click_active) {
        if (now - st->last_click_ms <= 350) {
            st->last_click_ms = now;
            return;
        }
        st->double_click_active = 0;
    }
    if (now - st->last_click_ms <= 350) {
        st->sel_start = 0;
        st->sel_end = st->len;
        st->cursor = st->len;
        st->selecting = 0;
        st->last_click_ms = now;
        st->double_click_active = 1;
        textbox_updateScroll(st, font, self->bounds.w - 8 * 2);
        return;
    }
    st->last_click_ms = now;
    textbox_repositionCursor(st, self, font, ev->x);
    st->sel_start = st->cursor;
    st->sel_end = st->cursor;
    st->selecting = 1;
}

static void
textbox_onMouseMove(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st || !st->editable || !st->selecting) {
        return;
    }
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    textbox_repositionCursor(st, self, font, ev->x);
    st->sel_end = st->cursor;
}

static void
textbox_onMouseUp(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_mouse_event_t *ev)
{
    (void)ctx;
    (void)ev;
    if (!self) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st) {
        return;
    }
    st->selecting = 0;
}

static int
textbox_handleEventComp(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ev) {
        return 0;
    }
    textbox_state_t *st = (textbox_state_t*)self->state;
    if (!st) {
        return 0;
    }
    if (!ctx || e9ui_getFocus(ctx) != self || !st->editable) {
        return 0;
    }
    TTF_Font *font = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    int viewW = self->bounds.w - 8 * 2;
    if (ev->type == SDL_TEXTINPUT) {
        textbox_completionClear(st);
        if (!font) {
            return 1;
        }
        const char *text = ev->text.text;
        int len = (int)strlen(text);
        if (len <= 0) {
            return 1;
        }
        int hadSelection = textbox_hasSelection(st);
        int space = st->maxLen - st->len;
        if (!hadSelection && space <= 0) {
            return 1;
        }
        textbox_recordUndo(st);
        if (hadSelection) {
            textbox_deleteSelection(st);
        }
        space = st->maxLen - st->len;
        if (space <= 0) {
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (len > space) {
            len = space;
        }
        memmove(&st->text[st->cursor + len], &st->text[st->cursor], (size_t)(st->len - st->cursor + 1));
        memcpy(&st->text[st->cursor], text, (size_t)len);
        st->len += len;
        st->cursor += len;
        textbox_clearSelection(st);
        textbox_notifyChange(st, ctx);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (ev->type != SDL_KEYDOWN) {
        return 0;
    }
    SDL_Keycode kc = ev->key.keysym.sym;
    SDL_Keymod mods = ev->key.keysym.mod;
    if (kc != SDLK_TAB) {
        textbox_completionClear(st);
    }
    int accel = (mods & KMOD_GUI) || (mods & KMOD_CTRL);
    int shift = (mods & KMOD_SHIFT);
    if (st->key_cb && st->key_cb(ctx, kc, mods, st->key_user)) {
        return 1;
    }
    if (!accel && kc == SDLK_TAB && st->completionMode != e9ui_textbox_completion_none) {
        return textbox_filenameCompletion(st, ctx, font, viewW, shift ? 1 : 0);
    }
    if (accel && kc == SDLK_z) {
        if (shift) {
            textbox_doRedo(st, ctx, font, viewW);
        } else {
            textbox_doUndo(st, ctx, font, viewW);
        }
        return 1;
    }
    if (accel && kc == SDLK_a) {
        st->cursor = 0;
        textbox_clearSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (accel && kc == SDLK_e) {
        st->cursor = st->len;
        textbox_clearSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    }
    if (accel && kc == SDLK_b) {
        if (st->cursor > 0) {
            st->cursor--;
            textbox_clearSelection(st);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    }
    if (accel && kc == SDLK_f) {
        if (st->cursor < st->len) {
            st->cursor++;
            textbox_clearSelection(st);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    }
    if (accel && kc == SDLK_d) {
        if (textbox_hasSelection(st)) {
            textbox_recordUndo(st);
            textbox_deleteSelection(st);
            textbox_notifyChange(st, ctx);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor < st->len) {
            textbox_recordUndo(st);
            memmove(&st->text[st->cursor], &st->text[st->cursor + 1], (size_t)(st->len - st->cursor));
            st->len--;
            textbox_notifyChange(st, ctx);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    }
    if (accel && kc == SDLK_k) {
        if (st->cursor < st->len) {
            size_t rem = (size_t)(st->len - st->cursor);
            char *buf = (char*)alloc_calloc(rem + 1, 1);
            if (buf) {
                memcpy(buf, &st->text[st->cursor], rem);
                SDL_SetClipboardText(buf);
                alloc_free(buf);
            }
            textbox_recordUndo(st);
            st->text[st->cursor] = '\0';
            st->len = st->cursor;
            textbox_clearSelection(st);
            textbox_notifyChange(st, ctx);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    }
    if (accel && kc == SDLK_y) {
        if (SDL_HasClipboardText()) {
            char *clip = SDL_GetClipboardText();
            if (clip && *clip) {
                textbox_recordUndo(st);
                if (textbox_hasSelection(st)) {
                    textbox_deleteSelection(st);
                }
                textbox_insertText(st, clip, (int)strlen(clip));
                textbox_notifyChange(st, ctx);
                textbox_updateScroll(st, font, viewW);
            }
            if (clip) {
                SDL_free(clip);
            }
        }
        return 1;
    }
    if (accel && kc == SDLK_c) {
        if (textbox_hasSelection(st)) {
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            if (b > a) {
                char *buf = (char*)alloc_calloc((size_t)(b - a + 1), 1);
                if (buf) {
                    memcpy(buf, &st->text[a], (size_t)(b - a));
                    SDL_SetClipboardText(buf);
                    alloc_free(buf);
                }
            }
        }
        return 1;
    }
    if (accel && kc == SDLK_x) {
        if (textbox_hasSelection(st)) {
            textbox_recordUndo(st);
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            if (b > a) {
                char *buf = (char*)alloc_calloc((size_t)(b - a + 1), 1);
                if (buf) {
                    memcpy(buf, &st->text[a], (size_t)(b - a));
                    SDL_SetClipboardText(buf);
                    alloc_free(buf);
                }
                if (textbox_deleteSelection(st)) {
                    textbox_notifyChange(st, ctx);
                    textbox_updateScroll(st, font, viewW);
                }
            }
        }
        return 1;
    }
    if (accel && kc == SDLK_v) {
        if (SDL_HasClipboardText()) {
            char *clip = SDL_GetClipboardText();
            if (clip && *clip) {
                textbox_recordUndo(st);
                if (textbox_deleteSelection(st)) {
                    textbox_notifyChange(st, ctx);
                }
                int len = (int)strlen(clip);
                textbox_insertText(st, clip, len);
                textbox_notifyChange(st, ctx);
                textbox_updateScroll(st, font, viewW);
            }
            if (clip) {
                SDL_free(clip);
            }
        }
        return 1;
    }
    switch (kc) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        if (st->submit) {
            st->submit(ctx, st->user);
        }
        return 1;
    case SDLK_LEFT:
        if (textbox_hasSelection(st)) {
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            st->cursor = a;
            textbox_clearSelection(st);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor > 0) {
            st->cursor--;
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    case SDLK_RIGHT:
        if (textbox_hasSelection(st)) {
            int a = 0;
            int b = 0;
            textbox_normalizeSelection(st, &a, &b);
            st->cursor = b;
            textbox_clearSelection(st);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor < st->len) {
            st->cursor++;
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    case SDLK_HOME:
        st->cursor = 0;
        textbox_clearSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    case SDLK_END:
        st->cursor = st->len;
        textbox_clearSelection(st);
        textbox_updateScroll(st, font, viewW);
        return 1;
    case SDLK_BACKSPACE:
        if (textbox_hasSelection(st)) {
            textbox_recordUndo(st);
            textbox_deleteSelection(st);
            textbox_notifyChange(st, ctx);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor > 0) {
            textbox_recordUndo(st);
            memmove(&st->text[st->cursor - 1], &st->text[st->cursor], (size_t)(st->len - st->cursor + 1));
            st->cursor--;
            st->len--;
            textbox_notifyChange(st, ctx);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    case SDLK_DELETE:
        if (textbox_hasSelection(st)) {
            textbox_recordUndo(st);
            textbox_deleteSelection(st);
            textbox_notifyChange(st, ctx);
            textbox_updateScroll(st, font, viewW);
            return 1;
        }
        if (st->cursor < st->len) {
            textbox_recordUndo(st);
            memmove(&st->text[st->cursor], &st->text[st->cursor + 1], (size_t)(st->len - st->cursor));
            st->len--;
            textbox_notifyChange(st, ctx);
            textbox_updateScroll(st, font, viewW);
        }
        return 1;
    default:
        break;
    }
    return 0;
}

static void
textbox_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  if (!self) {
    return;
  }
  textbox_state_t *st = (textbox_state_t*)self->state;
  if (st) {
    textbox_completionClear(st);
    textbox_history_clear(&st->undo);
    textbox_history_clear(&st->redo);
    alloc_free(st->text);
    alloc_free(st->placeholder);
    alloc_free(st->scratch);
    alloc_free(st->completionPrefix);
    alloc_free(st->completionRest);
  }
}


e9ui_component_t *
e9ui_textbox_make(int maxLen, e9ui_textbox_submit_cb_t onSubmit, e9ui_textbox_change_cb_t onChange, void *user)
{
    if (maxLen <= 0) {
        return NULL;
    }
    e9ui_component_t *comp = (e9ui_component_t*)alloc_calloc(1, sizeof(*comp));
    if (!comp) {
        return NULL;
    }
    textbox_state_t *st = (textbox_state_t*)alloc_calloc(1, sizeof(textbox_state_t));
    if (!st) {
        alloc_free(comp);
        return NULL;
    }
    st->maxLen = maxLen;
    st->text = (char*)alloc_calloc((size_t)maxLen + 1, 1);
    st->scratch = (char*)alloc_calloc((size_t)maxLen + 1, 1);
    st->completionPrefix = (char*)alloc_calloc((size_t)maxLen + 1, 1);
    st->completionRest = (char*)alloc_calloc((size_t)maxLen + 1, 1);
    st->editable = 1;
    st->sel_start = 0;
    st->sel_end = 0;
    st->selecting = 0;
    st->last_click_ms = 0;
    st->double_click_active = 0;
    st->submit = onSubmit;
    st->change = onChange;
    st->user = user;
    st->frame_visible = 1;
    st->completionMode = e9ui_textbox_completion_none;
    st->completionList = NULL;
    st->completionCount = 0;
    st->completionCap = 0;
    st->completionSel = -1;
    st->completionPrefixLen = 0;
    if (!st->text || !st->scratch || !st->completionPrefix || !st->completionRest) {
        alloc_free(st->text);
        alloc_free(st->scratch);
        alloc_free(st->completionPrefix);
        alloc_free(st->completionRest);
        alloc_free(st);
        alloc_free(comp);
        return NULL;
    }
    comp->name = "e9ui_textbox";
    comp->state = st;
    comp->focusable = 1;
    comp->preferredHeight = textbox_preferredHeight;
    comp->layout = textbox_layoutComp;
    comp->render = textbox_renderComp;
    comp->handleEvent = textbox_handleEventComp;
    comp->dtor = textbox_dtor;
    comp->onMouseDown = textbox_onMouseDown;
    comp->onMouseMove = textbox_onMouseMove;
    comp->onMouseUp = textbox_onMouseUp;
    return comp;
}

void
e9ui_textbox_setText(e9ui_component_t *comp, const char *text)
{
    if (!comp || !comp->state || !text) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    int len = 0;
    if (st->numeric_only) {
        for (const char *p = text; *p && len < st->maxLen; ++p) {
            if (*p >= '0' && *p <= '9') {
                st->text[len++] = *p;
            }
        }
    } else {
        len = (int)strlen(text);
        if (len > st->maxLen) {
            len = st->maxLen;
        }
        memcpy(st->text, text, (size_t)len);
    }
    st->text[len] = '\0';
    st->len = len;
    st->cursor = len;
    textbox_clearSelection(st);
    st->scrollX = 0;
    textbox_completionClear(st);
    textbox_history_clear(&st->undo);
    textbox_history_clear(&st->redo);
}

const char *
e9ui_textbox_getText(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    return st->text;
}

int
e9ui_textbox_getCursor(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return 0;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    return st->cursor;
}

void
e9ui_textbox_setCursor(e9ui_component_t *comp, int cursor)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    if (cursor < 0) {
        cursor = 0;
    }
    if (cursor > st->len) {
        cursor = st->len;
    }
    st->cursor = cursor;
    textbox_clearSelection(st);
}

void
e9ui_textbox_setKeyHandler(e9ui_component_t *comp, e9ui_textbox_key_cb_t cb, void *user)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->key_cb = cb;
    st->key_user = user;
}

void *
e9ui_textbox_getUser(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return NULL;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    return st->user;
}

void
e9ui_textbox_setPlaceholder(e9ui_component_t *comp, const char *placeholder)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    alloc_free(st->placeholder);
    if (placeholder && *placeholder) {
        st->placeholder = alloc_strdup(placeholder);
    } else {
        st->placeholder = NULL;
    }
}

void
e9ui_textbox_setFrameVisible(e9ui_component_t *comp, int visible)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->frame_visible = visible ? 1 : 0;
}

void
e9ui_textbox_setEditable(e9ui_component_t *comp, int editable)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->editable = editable ? 1 : 0;
}

int
e9ui_textbox_isEditable(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return 0;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    return st->editable;
}

void
e9ui_textbox_setNumericOnly(e9ui_component_t *comp, int numeric_only)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->numeric_only = numeric_only ? 1 : 0;
    if (st->numeric_only && st->text) {
        int len = 0;
        for (int i = 0; i < st->len; ++i) {
            char c = st->text[i];
            if (c >= '0' && c <= '9') {
                st->text[len++] = c;
            }
        }
        st->text[len] = '\0';
        st->len = len;
        if (st->cursor > st->len) {
            st->cursor = st->len;
        }
        textbox_clearSelection(st);
    }
}

void
e9ui_textbox_setCompletionMode(e9ui_component_t *comp, e9ui_textbox_completion_mode_t mode)
{
    if (!comp || !comp->state) {
        return;
    }
    textbox_state_t *st = (textbox_state_t*)comp->state;
    st->completionMode = mode;
    textbox_completionClear(st);
}

e9ui_textbox_completion_mode_t
e9ui_textbox_getCompletionMode(const e9ui_component_t *comp)
{
    if (!comp || !comp->state) {
        return e9ui_textbox_completion_none;
    }
    const textbox_state_t *st = (const textbox_state_t*)comp->state;
    return st->completionMode;
}
