/*
 * scan.c - the main scanning loop.
 *
 * A single flat loop drives the whole translation: it doesn't
 * recursively call itself for #include (directive.c just pushes the
 * new file onto the source stack and returns; the very next iteration
 * of this loop transparently starts pulling characters from it via
 * source_getc/peekc), and it doesn't recurse for macro expansion
 * either (macro_try_expand() pushes the substituted text as a new
 * source buffer, which this same loop then re-scans exactly like
 * ordinary text - which is what makes nested macro calls "just work").
 *
 * '#' is only recognized as introducing a directive when it is the
 * very first character of a physical line (no leading whitespace) -
 * this is a deliberate, faithful reproduction of the real MUTOS/V7
 * fast-cpp's behavior (see cpp.c's cotoken() "prevlf" logic), not a
 * simplification; the test corpus has no indented directives so this
 * is unobservable either way, but it is the historically accurate
 * choice.
 */

#include <stdlib.h>
#include "mutos_cpp.h"

static void copy_literal(CppState *cs, int quote) {
    bool active = cond_active(cs);
    if (active) fputc(quote, cs->out);
    for (;;) {
        int c = source_getc(cs);
        if (c == EOF) { cpp_error(cs, "unterminated string/char literal"); return; }
        if (c == '\n') { source_ungetc(cs, c); return; }
        if (active) fputc(c, cs->out);
        if (c == quote) return;
        if (c == '\\') {
            int c2 = source_getc(cs);
            if (c2 == EOF) return;
            if (active) fputc(c2, cs->out);
        }
    }
}

static void skip_comment(CppState *cs) {
    /* Embedded newlines inside a comment are ALWAYS individually
     * emitted, even inside an inactive (false #ifdef/#ifndef/#if)
     * region - this is a genuine, easily-missed quirk of the real
     * MUTOS/V7 fast-cpp: its comment-skipping loop writes each
     * embedded newline directly, bypassing the normal flslvl-gated
     * output-suppression path entirely. Confirmed against
     * tests/mutos_cpp/c/mch.c, where multi-line comments inside a
     * false "#ifdef M1834" block each still contribute their internal
     * newlines to the golden output. Only the comment's other
     * characters are actually removed. */
    int prev = 0;
    for (;;) {
        int c = source_getc(cs);
        if (c == EOF) { cpp_error(cs, "unterminated comment"); return; }
        if (c == '\n') fputc('\n', cs->out);
        if (prev == '*' && c == '/') return;
        prev = c;
    }
}

void scan_run(CppState *cs) {
    cs->at_line_start = true;

    for (;;) {
        int c = source_peekc(cs);
        if (c == EOF) break;

        if (cs->at_line_start && c == '#') {
            source_getc(cs);
            directive_dispatch(cs);
            cs->at_line_start = true;
            continue;
        }

        if (c == '\n') {
            source_getc(cs);
            if (cond_active(cs)) fputc('\n', cs->out);
            cs->at_line_start = true;
            continue;
        }

        cs->at_line_start = false;

        if (c == '/' && source_peekc2(cs) == '*') {
            source_getc(cs); source_getc(cs);
            skip_comment(cs);
            continue;
        }

        if (c == '"' || c == '\'') {
            source_getc(cs);
            copy_literal(cs, c);
            continue;
        }

        if (is_ident_start(c)) {
            char *name = read_identifier_from_source(cs);
            if (cond_active(cs)) {
                if (!macro_try_expand(cs, name)) {
                    fputs(name, cs->out);
                }
            }
            free(name);
            continue;
        }

        source_getc(cs);
        if (cond_active(cs)) fputc(c, cs->out);
    }

    if (cs->cond) cpp_error(cs, "unterminated #if/#ifdef/#ifndef at end of file");
}
