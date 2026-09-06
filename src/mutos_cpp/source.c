/*
 * source.c - the nested source stack: real files (for the top-level
 * input and #include) plus in-memory "pushback" buffers (for macro
 * expansion results that must be re-scanned exactly like ordinary
 * source text).
 *
 * All character input funnels through source_getc(), which:
 *   - transparently pops exhausted buffers/files and resumes the parent
 *     source, exactly like the real cpp's buffer-refill/unfill logic,
 *   - un-hides a macro when the pushback buffer that was hiding it
 *     (to prevent self-recursive expansion) is fully consumed,
 *   - splices "\<newline>" (backslash immediately followed by a raw
 *     newline) out of the stream everywhere, matching standard C
 *     translation-phase-2 line-continuation semantics.
 *
 * Ungetc'd characters (source_ungetc) are scoped to whichever Source
 * frame is on top of the stack AT THE TIME of the ungetc call, not to
 * a single global buffer. This matters: a lookahead peek can reveal a
 * character that belongs to the *current* (about-to-become-parent)
 * source right before a macro expansion pushes a *new* source on top;
 * if that peeked character lived in a global pushback slot instead of
 * one scoped to the (now parent) source, it would incorrectly be
 * served before the newly-pushed source's own text.
 */

#include <stdlib.h>
#include <string.h>
#include "mutos_cpp.h"

static int raw_getc0(CppState *cs) {
    for (;;) {
        Source *s = cs->src;
        if (!s) return EOF;

        if (s->pushback_n > 0) {
            return s->pushback[--s->pushback_n];
        }

        if (s->kind == SRC_BUFFER) {
            if (s->buf_pos < s->buf_len) {
                return (unsigned char) s->buf[s->buf_pos++];
            }
            /* Buffer exhausted: pop it, un-hide its owning macro (if
             * any), and retry with the parent source. */
            source_pop(cs);
            continue;
        } else { /* SRC_FILE */
            int c = fgetc(s->fp);
            if (c != EOF) {
                if (c == '\n') s->lineno++;
                return c;
            }
            /* End of this file. Pop it; if there is a parent, resume
             * it. If not, this is true end-of-input. */
            bool have_parent = (s->parent != NULL);
            source_pop(cs);
            if (!have_parent) return EOF;
            continue;
        }
    }
}

void source_ungetc(CppState *cs, int c) {
    if (c == EOF) return;
    Source *s = cs->src;
    if (!s) {
        fprintf(stderr, "mutos_cpp: internal error: ungetc with no source\n");
        exit(8);
    }
    if (s->pushback_n >= (int)(sizeof s->pushback / sizeof s->pushback[0])) {
        fprintf(stderr, "mutos_cpp: internal error: pushback overflow\n");
        exit(8);
    }
    s->pushback[s->pushback_n++] = c;
}

int source_getc(CppState *cs) {
    int c = raw_getc0(cs);
    while (c == '\\') {
        int c2 = raw_getc0(cs);
        if (c2 == '\n') {
            /* Spliced away entirely - no output, continue scanning. */
            c = raw_getc0(cs);
            continue;
        }
        if (c2 != EOF) {
            source_ungetc(cs, c2);
        }
        return c;
    }
    return c;
}

int source_peekc(CppState *cs) {
    int c = source_getc(cs);
    source_ungetc(cs, c);
    return c;
}

int source_peekc2(CppState *cs) {
    int c1 = source_getc(cs);
    if (c1 == EOF) return EOF;
    int c2 = source_getc(cs);
    /* push back in reverse order so c1 comes out first again */
    source_ungetc(cs, c2);
    source_ungetc(cs, c1);
    return c2;
}

bool source_push_file(CppState *cs, const char *path, const char *dir) {
    FILE *fp = fopen(path, "r");
    if (!fp) return false;

    Source *s = calloc(1, sizeof *s);
    s->kind = SRC_FILE;
    s->fp = fp;
    s->filename = xstrdup(path);
    s->dir = xstrdup(dir ? dir : "");
    s->lineno = 1;
    s->parent = cs->src;
    cs->src = s;
    return true;
}

void source_push_buffer(CppState *cs, char *text, size_t len, Macro *hidden_owner) {
    Source *s = calloc(1, sizeof *s);
    s->kind = SRC_BUFFER;
    s->buf = text;       /* takes ownership */
    s->buf_len = len;
    s->buf_pos = 0;
    s->hidden_macro = hidden_owner;
    s->parent = cs->src;
    cs->src = s;
}

void source_pop(CppState *cs) {
    Source *s = cs->src;
    if (!s) return;
    cs->src = s->parent;

    if (s->kind == SRC_FILE) {
        fclose(s->fp);
        free(s->filename);
        free(s->dir);
    } else {
        free(s->buf);
        if (s->hidden_macro) s->hidden_macro->hidden = false;
    }
    free(s);
}

int cur_lineno(CppState *cs) {
    Source *s = cs->src;
    while (s && s->kind != SRC_FILE) s = s->parent;
    return s ? s->lineno : 0;
}

const char *cur_filename(CppState *cs) {
    Source *s = cs->src;
    while (s && s->kind != SRC_FILE) s = s->parent;
    return s ? s->filename : "<none>";
}
