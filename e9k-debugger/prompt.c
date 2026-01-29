/*
 * COPYRIGHT © 2026 Enable Software Pty Ltd - All Rights Reserved
 *
 * https://github.com/alpine9000/engine9000-public
 *
 * See COPYING for license details
 */

#include <stdio.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <ctype.h>
#include <string.h>
#include <sys/stat.h>

#include "e9ui.h" 
#include "prompt.h"
#include "debugger.h"
#include "console_cmd.h"

#define PROMPT_HISTORY_MAX 10000

static int
prompt_appendHistoryLine(const char *path, const char *line)
{
    if (!path || !*path || !line) {
        return 0;
    }
    FILE *fp = fopen(path, "a");
    if (!fp) {
        return 0;
    }
    // History expects one command per line.
    // Strip newlines defensively so we don't corrupt the file format.
    for (const char *p = line; *p; ++p) {
        if (*p == '\n' || *p == '\r') {
            fputc(' ', fp);
        } else {
            fputc(*p, fp);
        }
    }
    fputc('\n', fp);
    fclose(fp);
    return 1;
}

static int font_line_height_local(TTF_Font *font) {
    if (font) {
        int h = TTF_FontHeight(font);
        if (h > 0) {
            return h;
        }
    }
    return 16;
}

 
typedef struct prompt_state {
    char killBuf[PROMPT_MAX];
    char historyPath[1024];
    int histNavActive;
    int histNavIndex;
    char histSavedLine[PROMPT_MAX];
    int histSavedLen;
    int histSavedCursor;
    e9ui_component_t *textbox;
    char **cmpl;
    int cmplCount;
    int cmplCap;
    int cmplVisible;
    int cmplSel;
    int cmplPrefixLen;
    char cmplPrefix[PROMPT_MAX];
    char cmplRest[PROMPT_MAX];
    int cmplPageStart;
    int cmplPageCycleDone;
} prompt_state_t;

static const char *
prompt_getText(prompt_state_t *st)
{
    if (!st || !st->textbox) {
        return "";
    }
    const char *txt = e9ui_textbox_getText(st->textbox);
    return txt ? txt : "";
}

static int
prompt_getCursor(prompt_state_t *st)
{
    if (!st || !st->textbox) {
        return 0;
    }
    return e9ui_textbox_getCursor(st->textbox);
}

static void
prompt_setTextCursor(prompt_state_t *st, const char *text, int cursor)
{
    if (!st || !st->textbox) {
        return;
    }
    e9ui_textbox_setText(st->textbox, text ? text : "");
    e9ui_textbox_setCursor(st->textbox, cursor);
}

static int
prompt_applyCompletionChoice(prompt_state_t *st, const char *choiceText, int addSpace)
{
    if (!st || !choiceText) {
        return 0;
    }
    int prelen = st->cmplPrefixLen;
    char choice[PROMPT_MAX];
    size_t clen = strlen(choiceText);
    if (clen >= sizeof(choice)) {
        clen = sizeof(choice) - 1;
    }
    memcpy(choice, choiceText, clen);
    choice[clen] = '\0';
    char newline[PROMPT_MAX];
    newline[0] = '\0';
    int nl = 0;
    if (prelen > 0) {
        if (prelen >= PROMPT_MAX - 1) {
            prelen = PROMPT_MAX - 1;
        }
        memcpy(newline, st->cmplPrefix, (size_t)prelen);
        nl = prelen;
        newline[nl] = '\0';
    }
    size_t sl = strlen(choice);
    if (nl + (int)sl >= PROMPT_MAX - 1) {
        sl = (size_t)((PROMPT_MAX - 1) - nl);
    }
    memcpy(newline + nl, choice, sl);
    nl += (int)sl;
    newline[nl] = '\0';
    if (addSpace && st->cmplRest[0] == '\0') {
        if (nl < PROMPT_MAX - 1) {
            newline[nl++] = ' ';
            newline[nl] = '\0';
        }
    }
    size_t rl = strlen(st->cmplRest);
    if (nl + (int)rl >= PROMPT_MAX - 1) {
        rl = (size_t)((PROMPT_MAX - 1) - nl);
    }
    memcpy(newline + nl, st->cmplRest, rl);
    nl += (int)rl;
    newline[nl] = '\0';
    int cursorPos = st->cmplPrefixLen + (int)strlen(choice);
    if (addSpace && st->cmplRest[0] == '\0' && cursorPos < PROMPT_MAX - 1) {
        cursorPos += 1;
    }
    prompt_setTextCursor(st, newline, cursorPos);
    return 1;
}

static size_t
prompt_commonPrefixLen(const char * const *cands, int count)
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
        while (j < commonLen && cands[0][j] == cand[j]) {
            if (cands[0][j] == '.') {
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

static prompt_state_t *
prompt_state_from_component(e9ui_component_t *comp)
{
    if (!comp || !comp->name) {
        return NULL;
    }
    if (strcmp(comp->name, "prompt") == 0) {
        return (prompt_state_t*)comp->state;
    }
    if (strcmp(comp->name, "e9ui_textbox") == 0) {
        return (prompt_state_t*)e9ui_textbox_getUser(comp);
    }
    return NULL;
}

static int
prompt_preferredHeight(e9ui_component_t *self, e9ui_context_t *ctx, int availW)
{
    (void)availW;
    prompt_state_t *st = (prompt_state_t*)self->state;
    if (st && st->textbox && st->textbox->preferredHeight) {
        return st->textbox->preferredHeight(st->textbox, ctx, availW);
    }
    TTF_Font *useFont = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    int lh = useFont ? TTF_FontHeight(useFont) : 16;
    if (lh <= 0) {
        lh = 16;
    }
    return lh + 10;
}

static void
prompt_layoutComp(e9ui_component_t *self, e9ui_context_t *ctx, e9ui_rect_t bounds)
{
    self->bounds = bounds;
    prompt_state_t *st = (prompt_state_t*)self->state;
    if (!st || !st->textbox || !st->textbox->layout) {
        return;
    }
    const char *promptStr = self->disabled ? "" : "> ";
    int pad = 10;
    int prefixW = 0;
    TTF_Font *useFont = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
    if (useFont && promptStr[0]) {
        TTF_SizeText(useFont, promptStr, &prefixW, NULL);
    }
    int x = bounds.x + pad + prefixW;
    int w = bounds.w - (x - bounds.x) - pad;
    if (w < 0) {
        w = 0;
    }
    e9ui_rect_t tb = { x, bounds.y, w, bounds.h };
    st->textbox->layout(st->textbox, ctx, tb);
}

static void
prompt_render(e9ui_component_t *self, e9ui_context_t *ctx)
{
  prompt_state_t *st = (prompt_state_t*)self->state;
  TTF_Font *useFont = e9ui->theme.text.prompt ? e9ui->theme.text.prompt : ctx->font;
  int lh = font_line_height_local(useFont);
  SDL_Color colc = {200,200,200,255};
  SDL_Color pcol = {160,200,255,255};
  SDL_Rect area = { self->bounds.x, self->bounds.y, self->bounds.w, self->bounds.h };
  int xoff = area.x + 10; int baseY = area.y + (area.h - lh);
  if (st && st->textbox) {
      e9ui_textbox_setEditable(st->textbox, self->disabled ? 0 : 1);
  }
  if (self->disabled && st) {
      st->cmplVisible = 0;
  }

  if (st->cmplVisible && st->cmplCount > 0 && useFont) {
    int rows = debugger.opts.completionListRows > 0 ? debugger.opts.completionListRows : 30;
    int maxw = 0; int start = st->cmplPageStart;
    if (start < 0) {
      start = 0;
    }
    if (start >= st->cmplCount) {
      start = st->cmplCount - 1;
    }
    int remaining = st->cmplCount - start;
    if (remaining < 0) {
      remaining = 0;
    }
    int vis = remaining;
    if (vis > rows) {
      vis = rows;
    }
    for (int i=0;i<vis;i++) {
      int tw=0, th=0;
      const char *it = st->cmpl[start + i];
      if (it) {
	TTF_SizeText(useFont, it, &tw, &th);
      }
      if (tw > maxw) {
	maxw = tw;
      }
    }
    int showMore = (start + vis < st->cmplCount);
    int pad = 8;

    int moreW = 0;
    if (showMore) {
      char more[64]; snprintf(more, sizeof(more), "(+%d more)", st->cmplCount - (start + vis));
      int tw=0, th=0;
      TTF_SizeText(useFont, more, &tw, &th);
      moreW = tw;
      if (moreW > maxw) {
	maxw = moreW;
      }
    }
    int boxW = maxw + pad*2;
    int boxH = lh*vis + pad*2 + (showMore ? lh : 0);
    int bx = area.x + 10; int by = area.y - boxH - 4;
    if (by < 0) {
      by = 0;
    }
    SDL_Rect bg = { bx, by, boxW, boxH };
    SDL_SetRenderDrawColor(ctx->renderer, 30, 30, 30, 230);
    SDL_RenderFillRect(ctx->renderer, &bg);
    SDL_SetRenderDrawColor(ctx->renderer, 80, 120, 160, 255);
    SDL_RenderDrawRect(ctx->renderer, &bg);
    int y = by + pad;
    for (int i=0;i<vis;i++) {
      const char *txt = st->cmpl[start + i] ? st->cmpl[start + i] : "";
      SDL_Color lncol = (SDL_Color){220,220,220,255};
      if (st->cmplSel >= start && st->cmplSel < start + vis && st->cmplSel == (start + i)) {
	SDL_Rect hl = { bx+1, y-1, boxW-2, lh+2 };
	SDL_SetRenderDrawColor(ctx->renderer, 60, 80, 100, 255);
	SDL_RenderFillRect(ctx->renderer, &hl);
	lncol = (SDL_Color){240,240,255,255};
      }
      int tw = 0, th = 0;
      SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, useFont, txt, lncol, &tw, &th);
      if (t) {
	SDL_Rect tr = { bx + pad, y, tw, th };
	SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
      }
      y += lh;
    }
    if (showMore) {
      char more[64]; snprintf(more, sizeof(more), "(+%d more)", st->cmplCount - (start + vis));
      SDL_Color mc = {180,180,180,255};
      int tw = 0, th = 0;
      SDL_Texture *t = e9ui_text_cache_getText(ctx->renderer, useFont, more, mc, &tw, &th);
      if (t) {
	SDL_Rect tr = { bx + pad, y, tw, th };
	SDL_RenderCopy(ctx->renderer, t, NULL, &tr);
      }
    }
  }
    if (useFont) {
        const char* promptStr = self->disabled ? "" : "> ";
        if (self->disabled) {
            const char *disabled = "  PAUSE DEBUGGER FOR CONSOLE";
            int dw = 0, dh = 0;
            SDL_Texture *dt = e9ui_text_cache_getText(ctx->renderer, useFont, disabled, colc, &dw, &dh);
            if (dt) {
                SDL_Rect dr = { xoff, baseY, dw, dh };
                SDL_RenderCopy(ctx->renderer, dt, NULL, &dr);
            }
            return;
        }
        if (!*machine_getRunningState(debugger.machine)) {
            e9ui_component_t *focus = e9ui_getFocus(ctx);
            if (focus != self && focus != st->textbox) {
                const char *inactive = "  USE TAB OR MOUSE ACTIVATE CONSOLE";
                int iw = 0, ih = 0;
                SDL_Texture *it = e9ui_text_cache_getText(ctx->renderer, useFont, inactive, colc, &iw, &ih);
                if (it) {
                    SDL_Rect ir = { xoff, baseY, iw, ih };
                    SDL_RenderCopy(ctx->renderer, it, NULL, &ir);
                }
                return;
            }
        }
        int pw = 0, ph = 0;
        SDL_Texture *t1 = e9ui_text_cache_getText(ctx->renderer, useFont, promptStr, pcol, &pw, &ph);
        if (t1) {
      SDL_Rect r1 = {xoff, baseY, pw, ph};
      SDL_RenderCopy(ctx->renderer, t1, NULL, &r1);
      xoff += pw;
    }
  }
  if (st && st->textbox && st->textbox->render) {
      st->textbox->render(st->textbox, ctx);
  }
}

static int
prompt_handleEventComp(e9ui_component_t *self, e9ui_context_t *ctx, const e9ui_event_t *ev)
{
    if (!self || !ctx || !ev) {
        return 0;
    }
    prompt_state_t *st = (prompt_state_t*)self->state;
    if (!st || !st->textbox) {
        return 0;
    }
    if (ev->type == SDL_MOUSEBUTTONDOWN && ev->button.button == SDL_BUTTON_LEFT) {
        int mx = ev->button.x;
        int my = ev->button.y;
        if (mx >= self->bounds.x && mx < self->bounds.x + self->bounds.w &&
            my >= self->bounds.y && my < self->bounds.y + self->bounds.h) {
            if (mx < st->textbox->bounds.x || mx >= st->textbox->bounds.x + st->textbox->bounds.w ||
                my < st->textbox->bounds.y || my >= st->textbox->bounds.y + st->textbox->bounds.h) {
                e9ui_setFocus(ctx, st->textbox);
                e9ui_textbox_setCursor(st->textbox, 0);
                return 1;
            }
        }
    }
    return 0;
}

static void
prompt_dtor(e9ui_component_t *self, e9ui_context_t *ctx)
{
  (void)ctx;
  if (self && self->state) {
    prompt_state_t *st = (prompt_state_t*)self->state;
    if (st->cmpl) {
      for (int i=0;i<st->cmplCount;i++) {
	alloc_free(st->cmpl[i]);
      }
      alloc_free(st->cmpl);
    }
  }
}

void
prompt_applyCompletion(e9ui_context_t *ctx, int prefixLen, const char *insert)
{
    if (!ctx || !insert || !*insert) {
        return;
    }
    e9ui_component_t *self = e9ui_getFocus(ctx);
    prompt_state_t *st = prompt_state_from_component(self);
    if (!st) {
        return;
    }
    const char *text = prompt_getText(st);
    int inputLen = (int)strlen(text);
    if (prefixLen < 0) {
        prefixLen = 0;
    }
    if (prefixLen > inputLen) {
        prefixLen = inputLen;
    }
    size_t insLen = strlen(insert);
    if (insLen == 0) {
        return;
    }

    if (inputLen + (int)insLen >= PROMPT_MAX - 1) {
        insLen = (size_t)((PROMPT_MAX - 1) - inputLen);
    }
    if (insLen == 0) {
        return;
    }
    char buf[PROMPT_MAX];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    memmove(&buf[prefixLen + insLen], &buf[prefixLen], (size_t)(inputLen - prefixLen + 1));
    memcpy(&buf[prefixLen], insert, insLen);
    prompt_setTextCursor(st, buf, prefixLen + (int)insLen);
    st->histNavActive = 0;
    st->histNavIndex = -1;
}

static void
prompt_onSubmit(e9ui_context_t *ctx, void *user)
{
    prompt_state_t *st = (prompt_state_t*)user;
    if (!st) {
        return;
    }
    st->cmplVisible = 0;
    const char *text = prompt_getText(st);
    const char *run = text;
    if (run) {
        while (*run && isspace((unsigned char)*run)) {
            ++run;
        }
    }
    if (!run || !*run) {
        extern int history_base;
        if (history_length > 0) {
            HIST_ENTRY *he = history_get(history_base + history_length - 1);
            if (he && he->line && *he->line) {
                run = he->line;
            }
        }
    }
    if (run && *run && run == text) {
        add_history(run);
        if (st->historyPath[0]) {
            // Avoid rewriting potentially huge history files on every command.
            // Prefer appending the latest entry; fall back to write_history if needed.
            if (!prompt_appendHistoryLine(st->historyPath, run)) {
                write_history(st->historyPath);
            }
        }
    }
    if (ctx && ctx->sendLine) {
        ctx->sendLine(run ? run : "");
    }
    prompt_setTextCursor(st, "", 0);
}

static void
prompt_onChange(e9ui_context_t *ctx, void *user)
{
    (void)ctx;
    prompt_state_t *st = (prompt_state_t*)user;
    if (!st) {
        return;
    }
    st->cmplVisible = 0;
    st->histNavActive = 0;
    st->histNavIndex = -1;
}

static int
prompt_keyHandler(e9ui_context_t *ctx, SDL_Keycode kc, SDL_Keymod km, void *user)
{
    prompt_state_t *st = (prompt_state_t*)user;
    if (!st) {
        return 0;
    }
    if (kc != SDLK_TAB) {
        st->cmplVisible = 0;
    }
    if ((km & KMOD_CTRL) && kc == SDLK_c) {
        if (ctx && ctx->sendInterrupt) {
            ctx->sendInterrupt();
        }
        return 1;
    }
    if ((km & KMOD_CTRL) && kc == SDLK_a) {
        prompt_setTextCursor(st, prompt_getText(st), 0);
        return 1;
    }
    if ((km & KMOD_CTRL) && kc == SDLK_e) {
        const char *text = prompt_getText(st);
        prompt_setTextCursor(st, text, (int)strlen(text));
        return 1;
    }
    if (kc == SDLK_TAB) {
        const char *text = prompt_getText(st);
        int cursor = prompt_getCursor(st);
        int inputLen = (int)strlen(text);
        if (st->cmplVisible && st->cmplCount > 0) {
            int total = st->cmplCount;
            int visRows = debugger.opts.completionListRows > 0 ? debugger.opts.completionListRows : 30;
            if (visRows > total) {
                visRows = total;
            }
            int start = st->cmplPageStart;
            if (start < 0) {
                start = 0;
            }
            if (start >= total) {
                start = (total > 0) ? total - 1 : 0;
            }
            int pageHasMore = (start + visRows < total);
            if (!st->cmplPageCycleDone && pageHasMore) {
                st->cmplPageStart = start + visRows;
                return 1;
            }
            if (!st->cmplPageCycleDone) {
                st->cmplPageCycleDone = 1;
                st->cmplSel = -1;
                st->cmplPageStart = 0;
            }
            st->cmplSel = (st->cmplSel + 1) % total;
            int sel = st->cmplSel;
            int rows = debugger.opts.completionListRows > 0 ? debugger.opts.completionListRows : 30;
            int desiredPage = (rows > 0) ? ((sel / rows) * rows) : 0;
            if (st->cmplPageStart != desiredPage) {
                st->cmplPageStart = desiredPage;
            }
            const char *cand = st->cmpl[sel] ? st->cmpl[sel] : "";
            return prompt_applyCompletionChoice(st, cand, 0);
        }
        st->cmplVisible = 0;
        st->cmplSel = -1;
        int token_start = cursor;
        if (token_start < 0) {
            token_start = 0;
        }
        if (token_start > inputLen) {
            token_start = inputLen;
        }
        while (token_start > 0 && !isspace((unsigned char)text[token_start - 1])) {
            token_start--;
        }
        st->cmplPrefixLen = token_start;
        if (token_start > 0) {
            size_t pl = (size_t)token_start;
            if (pl >= sizeof(st->cmplPrefix)) {
                pl = sizeof(st->cmplPrefix) - 1;
            }
            memcpy(st->cmplPrefix, text, pl);
            st->cmplPrefix[pl] = '\0';
        } else {
            st->cmplPrefix[0] = '\0';
        }
        {
            const char *rest = &text[cursor];
            size_t rl = strlen(rest);
            if (rl >= sizeof(st->cmplRest)) {
                rl = sizeof(st->cmplRest) - 1;
            }
            memcpy(st->cmplRest, rest, rl);
            st->cmplRest[rl] = '\0';
        }
        char **cands = NULL;
        int count = 0;
        int prefix_pos = 0;
        if (console_cmd_complete(text, cursor, &cands, &count, &prefix_pos) && count > 0) {
            int fragmentLen = cursor - token_start;
            if (fragmentLen < 0) {
                fragmentLen = 0;
            }
            if (count == 1) {
                prompt_hideCompletions(ctx);
                st->cmplVisible = 0;
                st->cmplSel = -1;
                st->cmplPageStart = 0;
                st->cmplPageCycleDone = 0;
                prompt_applyCompletionChoice(st, cands[0], 1);
            } else {
                size_t commonLen = prompt_commonPrefixLen((const char * const *)cands, count);
                if ((int)commonLen > fragmentLen) {
                    char common[PROMPT_MAX];
                    size_t clen = commonLen;
                    if (clen >= sizeof(common)) {
                        clen = sizeof(common) - 1;
                    }
                    memcpy(common, cands[0], clen);
                    common[clen] = '\0';
                    prompt_hideCompletions(ctx);
                    st->cmplVisible = 0;
                    st->cmplSel = -1;
                    st->cmplPageStart = 0;
                    st->cmplPageCycleDone = 0;
                    prompt_applyCompletionChoice(st, common, 0);
                } else if (ctx && ctx->showCompletions) {
                    ctx->showCompletions(ctx, (const char * const *)cands, count);
                }
            }
        } else {
            prompt_hideCompletions(ctx);
        }
        console_cmd_freeCompletions(cands, count);
        return 1;
    }
    if ((km & KMOD_CTRL) && kc == SDLK_u) {
        int cursor = prompt_getCursor(st);
        if (cursor > 0) {
            char buf[PROMPT_MAX];
            const char *text = prompt_getText(st);
            strncpy(buf, text, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            size_t k = (size_t)cursor;
            if (k >= sizeof(st->killBuf)) {
                k = sizeof(st->killBuf) - 1;
            }
            memcpy(st->killBuf, buf, k);
            st->killBuf[k] = '\0';
            memmove(&buf[0], &buf[cursor], strlen(buf) - (size_t)cursor + 1);
            prompt_setTextCursor(st, buf, 0);
        }
        st->histNavActive = 0;
        st->histNavIndex = -1;
        return 1;
    }
    if ((km & KMOD_CTRL) && kc == SDLK_k) {
        int cursor = prompt_getCursor(st);
        const char *text = prompt_getText(st);
        size_t len = strlen(text);
        if (cursor < (int)len) {
            char buf[PROMPT_MAX];
            strncpy(buf, text, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            size_t rem = len - (size_t)cursor;
            if (rem >= sizeof(st->killBuf)) {
                rem = sizeof(st->killBuf) - 1;
            }
            memcpy(st->killBuf, &buf[cursor], rem);
            st->killBuf[rem] = '\0';
            buf[cursor] = '\0';
            prompt_setTextCursor(st, buf, cursor);
        }
        st->histNavActive = 0;
        st->histNavIndex = -1;
        return 1;
    }
    if ((km & KMOD_CTRL) && kc == SDLK_y) {
        const char *text = prompt_getText(st);
        int cursor = prompt_getCursor(st);
        size_t klen = strlen(st->killBuf);
        if (klen > 0) {
            char buf[PROMPT_MAX];
            strncpy(buf, text, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            size_t len = strlen(buf);
            if (len + klen >= sizeof(buf)) {
                klen = sizeof(buf) - len - 1;
            }
            if (klen > 0) {
                memmove(&buf[cursor + (int)klen], &buf[cursor], len - (size_t)cursor + 1);
                memcpy(&buf[cursor], st->killBuf, klen);
                prompt_setTextCursor(st, buf, cursor + (int)klen);
            }
        }
        st->histNavActive = 0;
        st->histNavIndex = -1;
        return 1;
    }
    if (kc == SDLK_UP) {
        extern int history_base;
        if (!st->histNavActive) {
            if (history_length > 0) {
                const char *text = prompt_getText(st);
                strncpy(st->histSavedLine, text, sizeof(st->histSavedLine) - 1);
                st->histSavedLine[sizeof(st->histSavedLine) - 1] = '\0';
                st->histSavedLen = (int)strlen(st->histSavedLine);
                st->histSavedCursor = prompt_getCursor(st);
                st->histNavActive = 1;
                st->histNavIndex = history_length - 1;
            }
        } else if (st->histNavIndex > 0) {
            st->histNavIndex--;
        }
        if (st->histNavIndex >= 0) {
            HIST_ENTRY *he = history_get(history_base + st->histNavIndex);
            if (he && he->line) {
                prompt_setTextCursor(st, he->line, (int)strlen(he->line));
            }
        }
        return 1;
    }
    if (kc == SDLK_DOWN) {
        extern int history_base;
        if (st->histNavActive) {
            if (st->histNavIndex < history_length - 1) {
                st->histNavIndex++;
                HIST_ENTRY *he = history_get(history_base + st->histNavIndex);
                if (he && he->line) {
                    prompt_setTextCursor(st, he->line, (int)strlen(he->line));
                }
            } else {
                prompt_setTextCursor(st, st->histSavedLine, st->histSavedCursor);
                st->histNavActive = 0;
                st->histNavIndex = -1;
            }
        }
        return 1;
    }
    return 0;
}

e9ui_component_t *
	prompt_makeComponent(void)
	{
	    e9ui_component_t *c = (e9ui_component_t*)alloc_calloc(1, sizeof(*c));
	    c->name = "prompt";
	    prompt_state_t *st = (prompt_state_t*)alloc_calloc(1, sizeof(prompt_state_t));
	    st->killBuf[0] = '\0'; st->histNavActive = 0; st->histNavIndex = -1; st->histSavedLen = 0; st->histSavedCursor = 0; st->historyPath[0] = '\0';
	    st->cmpl = NULL; st->cmplCount = 0; st->cmplCap = 0; st->cmplVisible = 0; st->cmplSel = -1; st->cmplPageStart = 0; st->cmplPageCycleDone = 0;
	    using_history(); const char *home = getenv("HOME"); if (home && *home) {
	        snprintf(st->historyPath, sizeof(st->historyPath), "%s/.e9k_history", home);

	        // Cap in-memory history to avoid unbounded growth, and truncate the file
	        // before reading it so we don't load multi-million line histories.
	        stifle_history(PROMPT_HISTORY_MAX);
	        struct stat stbuf;
	        if (stat(st->historyPath, &stbuf) == 0) {
	            if (stbuf.st_size > (off_t)(8 * 1024 * 1024)) {
	                (void)history_truncate_file(st->historyPath, PROMPT_HISTORY_MAX);
	            }
	        }
	        read_history(st->historyPath);
	    }
	    st->textbox = e9ui_textbox_make(PROMPT_MAX - 1, prompt_onSubmit, prompt_onChange, st);
	    if (st->textbox) {
	        e9ui_textbox_setFrameVisible(st->textbox, 0);
	        e9ui_textbox_setKeyHandler(st->textbox, prompt_keyHandler, st);
        e9ui_child_add(c, st->textbox, NULL);
        e9ui_setDisableVariable(st->textbox, &c->disabled, 1);
    }
    c->state = st;
    c->focusable = 0;
    c->preferredHeight = prompt_preferredHeight;
    c->layout = prompt_layoutComp;
    c->render = prompt_render;
    c->handleEvent = prompt_handleEventComp;
    c->dtor = prompt_dtor;
    return c;
}

static void prompt_clearCompletions_internal(prompt_state_t *st)
{
    if (!st) return;
    for (int i=0;i<st->cmplCount;i++) alloc_free(st->cmpl[i]);
    alloc_free(st->cmpl); st->cmpl=NULL; st->cmplCount=0; st->cmplCap=0; st->cmplVisible=0; st->cmplSel=-1;
}

void
prompt_showCompletions(e9ui_context_t *ctx, const char * const *cands, int count)
{
    if (!ctx || !cands || count <= 0) return;
    e9ui_component_t *self = e9ui_getFocus(ctx);
    prompt_state_t *st = prompt_state_from_component(self);
    if (!st) {
        return;
    }
    prompt_clearCompletions_internal(st);
    st->cmpl = (char**)alloc_calloc((size_t)count, sizeof(char*));
    st->cmplCap = count; st->cmplCount = 0;
    for (int i=0;i<count;i++) {
        const char *s = cands[i]; if (!s) s=""; size_t l = strlen(s); st->cmpl[i] = (char*)alloc_alloc(l+1); memcpy(st->cmpl[i], s, l+1); st->cmplCount++;
    }
    st->cmplVisible = 1; st->cmplSel = -1; st->cmplPageStart = 0; st->cmplPageCycleDone = 0;
}

void
prompt_hideCompletions(e9ui_context_t *ctx)
{
    if (!ctx) return;
    e9ui_component_t *self = e9ui_getFocus(ctx);
    prompt_state_t *st = prompt_state_from_component(self);
    if (!st) {
        return;
    }
    st->cmplVisible = 0;
}

void
prompt_focus(e9ui_context_t *ctx, e9ui_component_t *prompt)
{
    if (!ctx || !prompt) {
        return;
    }
    prompt_state_t *st = prompt_state_from_component(prompt);
    if (!st || !st->textbox) {
        return;
    }
    e9ui_setFocus(ctx, st->textbox);
}

int
prompt_isFocused(e9ui_context_t *ctx, e9ui_component_t *prompt)
{
    if (!ctx || !prompt) {
        return 0;
    }
    prompt_state_t *st = prompt_state_from_component(prompt);
    if (!st || !st->textbox) {
        return 0;
    }
    e9ui_component_t *focus = e9ui_getFocus(ctx);
    return (focus == prompt || focus == st->textbox) ? 1 : 0;
}
