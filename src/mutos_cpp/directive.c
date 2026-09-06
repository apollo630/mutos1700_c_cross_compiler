/*
 * directive.c - dispatches preprocessor control lines.
 *
 * Output-line accounting rule (reverse-engineered from and confirmed
 * byte-for-byte against this project's golden references - see
 * STATUS.md): a directive line contributes exactly ONE blank output
 * line if-and-only-if the conditional-active state was true
 * *immediately before* this specific directive's own effect is
 * applied (its "pre-state"), regardless of what the directive changes
 * that state to. This one rule, applied uniformly to every directive
 * kind, correctly reproduces:
 *   - an active #define/#undef/#include/#ifdef/#ifndef/#if/#line line
 *     contributing a blank line (pre-state true),
 *   - a false #ifdef ... #endif block (no #else) collapsing to
 *     exactly ONE blank line total (only the opening line has
 *     pre-state true; the body and the closing #endif both have
 *     pre-state false, contributing nothing further),
 *   - an #else that flips a false branch to true contributing no
 *     blank line of its own (pre-state false) while every line of the
 *     now-active body that follows is output normally,
 *   - the matching #endif of that now-true #else body contributing a
 *     blank line (pre-state true).
 * Confirmed against tests/mutos_cpp/c/main.c's two "#ifdef MMU ...
 * #endif MMU" blocks (3 and 5 source lines respectively), both of
 * which collapse to exactly one blank golden-output line.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "mutos_cpp.h"

bool cond_active(CppState *cs) {
    if (!cs->cond) return true;
    return cs->cond->parent_active && cs->cond->this_true;
}

/* Consumes raw text up to (but not including) the next unescaped
 * newline or EOF, being comment/string aware so a ',' ')' '#' etc.
 * inside one doesn't confuse anything downstream. Nothing is copied
 * anywhere - this is purely "get past the rest of this directive
 * line" - except that an embedded multi-line comment's own internal
 * newlines are individually emitted (when `active`), exactly as they
 * would be in ordinary text; the comment does not end this scan, it
 * just contributes those extra blank lines along the way. */
static void skip_to_eol(CppState *cs, bool active) {
    (void)active; /* comment newlines below are unconditional (see
                   * scan.c's skip_comment); active still gates the
                   * final blank line emitted by callers via finish_line
                   * and friends, just not this comment-newline quirk. */
    for (;;) {
        int c = source_peekc(cs);
        if (c == EOF || c == '\n') return;
        if (c == '/' && source_peekc2(cs) == '*') {
            source_getc(cs); source_getc(cs);
            int prev = 0;
            for (;;) {
                int cc = source_getc(cs);
                if (cc == EOF) return;
                if (cc == '\n') fputc('\n', cs->out);
                if (prev == '*' && cc == '/') break;
                prev = cc;
            }
            continue;
        }
        if (c == '"' || c == '\'') {
            char quote = (char)source_getc(cs);
            for (;;) {
                int cc = source_getc(cs);
                if (cc == EOF) return;
                if (cc == '\n') { source_ungetc(cs, cc); break; }
                if (cc == quote) break;
                if (cc == '\\') { if (source_getc(cs) == EOF) return; }
            }
            continue;
        }
        source_getc(cs);
    }
}

static void finish_line(CppState *cs, bool pre_active) {
    skip_to_eol(cs, pre_active);
    int nl = source_getc(cs); /* consume the terminating newline (or EOF) */
    (void)nl;
    if (pre_active) fputc('\n', cs->out);
}

/* ------------------------------------------------------------------ */

static void do_define(CppState *cs, bool pre_active) {
    skip_hspace_only(cs);
    int c = source_peekc(cs);
    if (!is_ident_start(c)) {
        cpp_error(cs, "illegal macro name");
        finish_line(cs, pre_active);
        return;
    }
    char *name = read_identifier_from_source(cs);
    macro_do_define(cs, name, pre_active);
    free(name);
    int nl = source_getc(cs);
    (void)nl;
    if (pre_active) fputc('\n', cs->out);
}

static void do_undef(CppState *cs, bool pre_active) {
    skip_hspace_only(cs);
    if (is_ident_start(source_peekc(cs))) {
        char *name = read_identifier_from_source(cs);
        if (pre_active) macro_undef(cs, name);
        free(name);
    } else {
        cpp_error(cs, "illegal undef name");
    }
    finish_line(cs, pre_active);
}

static bool resolve_and_push_include(CppState *cs, const char *path, bool angle) {
    if (cs->include_depth >= MCPP_MAX_INCLUDE_DEPTH) {
        cpp_error(cs, "Unreasonable include nesting");
        return false;
    }
    char full[2 * MCPP_MAX_PATH + 2];

    if (path[0] == '/') {
        if (source_push_file(cs, path, "/")) { cs->include_depth++; return true; }
        return false;
    }

    if (!angle) {
        const char *dir0 = cur_dir(cs);
        if (dir0 && dir0[0]) snprintf(full, sizeof full, "%s/%s", dir0, path);
        else snprintf(full, sizeof full, "%s", path);
        if (source_push_file(cs, full, dir0)) { cs->include_depth++; return true; }
    }

    for (int i = 0; i < cs->n_include_dirs; i++) {
        snprintf(full, sizeof full, "%s/%s", cs->include_dirs[i], path);
        if (source_push_file(cs, full, cs->include_dirs[i])) { cs->include_depth++; return true; }
    }
    return false;
}

static void do_include(CppState *cs, bool pre_active) {
    skip_hspace_only(cs);
    int c = source_getc(cs);
    char path[MCPP_MAX_PATH]; int n = 0;
    bool angle;

    if (c == '<') {
        angle = true;
        for (;;) {
            int cc = source_getc(cs);
            if (cc == '>' || cc == '\n' || cc == EOF) { if (cc == '\n') source_ungetc(cs, cc); break; }
            if (n < MCPP_MAX_PATH - 1) path[n++] = (char)cc;
        }
    } else if (c == '"') {
        angle = false;
        for (;;) {
            int cc = source_getc(cs);
            if (cc == '"' || cc == '\n' || cc == EOF) { if (cc == '\n') source_ungetc(cs, cc); break; }
            if (n < MCPP_MAX_PATH - 1) path[n++] = (char)cc;
        }
    } else {
        cpp_error(cs, "bad include syntax");
        finish_line(cs, pre_active);
        return;
    }
    path[n] = '\0';

    skip_to_eol(cs, pre_active);
    int nl = source_getc(cs);
    (void)nl;

    if (pre_active) {
        if (!resolve_and_push_include(cs, path, angle))
            cpp_error(cs, "Can't find include file %s", path);
        fputc('\n', cs->out);
    }
}

static void push_cond_frame(CppState *cs, bool pre_active, bool this_true) {
    CondFrame *f = calloc(1, sizeof *f);
    f->parent_active = pre_active;
    f->this_true = this_true;
    f->else_seen = false;
    f->prev = cs->cond;
    cs->cond = f;
}

static void do_ifdef(CppState *cs, bool pre_active, bool positive_sense) {
    skip_hspace_only(cs);
    bool defined = false;
    if (is_ident_start(source_peekc(cs))) {
        char *name = read_identifier_from_source(cs);
        defined = (macro_lookup(cs, name) != NULL);
        free(name);
    } else {
        cpp_error(cs, positive_sense ? "ifdef: missing name" : "ifndef: missing name");
    }
    skip_to_eol(cs, pre_active);
    int nl = source_getc(cs);
    (void)nl;

    push_cond_frame(cs, pre_active, positive_sense ? defined : !defined);

    if (pre_active) fputc('\n', cs->out);
}

static void do_if(CppState *cs, bool pre_active) {
    bool ok;
    long v = ifexpr_eval(cs, &ok);
    int nl = source_getc(cs); /* ifexpr_eval stops right at the newline */
    (void)nl;

    push_cond_frame(cs, pre_active, v != 0);

    if (pre_active) fputc('\n', cs->out);
}

static void do_else(CppState *cs, bool pre_active) {
    skip_to_eol(cs, pre_active);
    int nl = source_getc(cs);
    (void)nl;

    if (!cs->cond) {
        cpp_error(cs, "If-less else");
    } else {
        if (cs->cond->else_seen) cpp_error(cs, "duplicate #else");
        cs->cond->else_seen = true;
        cs->cond->this_true = !cs->cond->this_true;
    }

    if (pre_active) fputc('\n', cs->out);
}

static void do_endif(CppState *cs, bool pre_active) {
    skip_to_eol(cs, pre_active);
    int nl = source_getc(cs);
    (void)nl;

    if (!cs->cond) {
        cpp_error(cs, "If-less endif");
    } else {
        CondFrame *f = cs->cond;
        cs->cond = f->prev;
        free(f);
    }

    if (pre_active) fputc('\n', cs->out);
}

static void do_line(CppState *cs, bool pre_active) {
    /* Under -P (this tool's only supported mode - see README) a #line
     * directive never re-emits a "# N \"file\"" marker; it collapses
     * to a blank line exactly like any other directive. */
    finish_line(cs, pre_active);
}

void directive_dispatch(CppState *cs) {
    bool pre_active = cond_active(cs);

    skip_hspace_only(cs);
    int c = source_peekc(cs);

    if (c == '\n') {
        source_getc(cs);
        if (pre_active) fputc('\n', cs->out);
        return;
    }
    if (c == EOF) return;

    if (!is_ident_start(c)) {
        cpp_error(cs, "undefined control");
        finish_line(cs, pre_active);
        return;
    }

    char *kw = read_identifier_from_source(cs);

    if (strcmp(kw, "define") == 0) do_define(cs, pre_active);
    else if (strcmp(kw, "undef") == 0) do_undef(cs, pre_active);
    else if (strcmp(kw, "include") == 0) do_include(cs, pre_active);
    else if (strcmp(kw, "ifdef") == 0) do_ifdef(cs, pre_active, true);
    else if (strcmp(kw, "ifndef") == 0) do_ifdef(cs, pre_active, false);
    else if (strcmp(kw, "if") == 0) do_if(cs, pre_active);
    else if (strcmp(kw, "else") == 0) do_else(cs, pre_active);
    else if (strcmp(kw, "endif") == 0) do_endif(cs, pre_active);
    else if (strcmp(kw, "line") == 0) do_line(cs, pre_active);
    else {
        cpp_warn(cs, "undefined control %s", kw);
        finish_line(cs, pre_active);
    }

    free(kw);
}
