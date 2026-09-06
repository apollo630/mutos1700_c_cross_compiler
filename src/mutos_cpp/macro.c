/*
 * macro.c - macro table, #define/#undef processing, and macro-call
 * expansion.
 *
 * Object-like and function-like macro bodies are stored as a linked
 * list of MacroBodyPiece nodes (literal text runs interleaved with
 * formal-parameter references), so expansion is a simple linear walk:
 * copy literal pieces verbatim, splice in the matching actual
 * argument's raw (unexpanded) text for parameter pieces. The result is
 * pushed back onto the source stack and re-scanned exactly like
 * ordinary source text, which is what lets nested macro calls and
 * further expansion "just happen" for free, and is also how self-
 * referential macros are protected against infinite recursion: the
 * macro being expanded is marked `hidden` for exactly the lifetime of
 * its own pushback buffer (standard "blue paint" / hide-set technique),
 * matching the *effect* (no infinite loop) of the original cpp's
 * maclvl/macforw recursion counter without replicating its incidental
 * fixed-budget mechanics.
 */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "mutos_cpp.h"

/* ------------------------------------------------------------------ */
/* Tiny growable byte buffer, used for literal-text runs and for
 * collecting actual-argument text during a macro call. */

typedef struct {
    char  *data;
    size_t len;
    size_t cap;
} DynBuf;

static void db_init(DynBuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

static void db_putc(DynBuf *b, char c) {
    if (b->len + 1 > b->cap) {
        size_t ncap = b->cap ? b->cap * 2 : 32;
        b->data = realloc(b->data, ncap);
        if (!b->data) { fprintf(stderr, "mutos_cpp: out of memory\n"); exit(8); }
        b->cap = ncap;
    }
    b->data[b->len++] = c;
}

static void db_puts(DynBuf *b, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) db_putc(b, s[i]);
}

static char *db_take(DynBuf *b) {
    db_putc(b, '\0');
    char *r = b->data;
    b->data = NULL; b->len = 0; b->cap = 0;
    return r;
}

/* ------------------------------------------------------------------ */
/* Macro hash table */

unsigned macro_hash(const char *name, size_t table_size) {
    unsigned h = 0;
    for (const unsigned char *p = (const unsigned char *)name; *p; p++)
        h = h * 131u + *p;
    return h % (unsigned)table_size;
}

void macro_table_init(CppState *cs, size_t size) {
    cs->macros = calloc(size, sizeof(Macro *));
    cs->macro_table_size = size;
}

Macro *macro_lookup(CppState *cs, const char *name) {
    unsigned h = macro_hash(name, cs->macro_table_size);
    for (Macro *m = cs->macros[h]; m; m = m->next)
        if (strcmp(m->name, name) == 0) return m;
    return NULL;
}

static void macro_body_free(MacroBodyPiece *b) {
    while (b) {
        MacroBodyPiece *n = b->next;
        free(b->literal);
        free(b);
        b = n;
    }
}

static void macro_free_one(Macro *m) {
    free(m->name);
    for (int i = 0; i < m->nparams; i++) free(m->params[i]);
    macro_body_free(m->body);
    free(m);
}

void macro_undef(CppState *cs, const char *name) {
    unsigned h = macro_hash(name, cs->macro_table_size);
    Macro **pp = &cs->macros[h];
    while (*pp) {
        if (strcmp((*pp)->name, name) == 0) {
            Macro *dead = *pp;
            *pp = dead->next;
            macro_free_one(dead);
            return;
        }
        pp = &(*pp)->next;
    }
    /* undefining a never-defined name is not an error */
}

void macro_free_all(CppState *cs) {
    for (size_t i = 0; i < cs->macro_table_size; i++) {
        Macro *m = cs->macros[i];
        while (m) {
            Macro *n = m->next;
            macro_free_one(m);
            m = n;
        }
    }
    free(cs->macros);
}

static void macro_install(CppState *cs, Macro *m) {
    /* Redefinition: replace, matching cpp's "redefined" warning
     * behavior loosely (we don't need byte-identical old/new body
     * comparison for this project's goldens; a plain warning on any
     * redefinition is close enough and never fatal). */
    Macro *old = macro_lookup(cs, m->name);
    if (old) {
        cpp_warn(cs, "%s redefined", m->name);
        macro_undef(cs, m->name);
    }
    unsigned h = macro_hash(m->name, cs->macro_table_size);
    m->next = cs->macros[h];
    cs->macros[h] = m;
}

/* ------------------------------------------------------------------ */
/* Low-level scanning helpers shared by body/argument collection.
 * These operate directly on the source stack via source_getc/peekc. */

static void skip_comment_body(CppState *cs, DynBuf *out_newlines_as_blanks, bool active) {
    /* Called with the opening "/ *" already consumed. Consumes through
     * the closing "* /". Embedded newlines are ALWAYS individually
     * emitted directly to output, regardless of `active` - this
     * matches the real cpp's comment-skip loop, which writes each
     * embedded newline unconditionally, bypassing the normal
     * flslvl-gated suppression entirely (see scan.c's skip_comment for
     * the full explanation; confirmed against mch.c's multi-line
     * comments inside a false "#ifdef M1834" block). The `active`
     * parameter is accepted for call-site symmetry but is no longer
     * consulted here. */
    (void)out_newlines_as_blanks;
    (void)active;
    int prev = 0;
    for (;;) {
        int c = source_getc(cs);
        if (c == EOF) { cpp_error(cs, "unterminated comment"); return; }
        if (c == '\n') fputc('\n', cs->out);
        if (prev == '*' && c == '/') break;
        prev = c;
    }
}

/* Reads one string/char literal (the opening quote already consumed;
 * `quote` is '"' or '\''). Appends the full raw text (including both
 * quote characters) to `buf`. An unescaped raw newline terminates the
 * literal early (matching the real cpp's documented behavior) without
 * being consumed as part of it. */
static void scan_literal_raw(CppState *cs, char quote, DynBuf *buf) {
    db_putc(buf, quote);
    for (;;) {
        int c = source_getc(cs);
        if (c == EOF) { cpp_error(cs, "unterminated string/char literal"); return; }
        if (c == '\n') { source_ungetc(cs, c); return; }
        if (c == quote) { db_putc(buf, (char)c); return; }
        if (c == '\\') {
            db_putc(buf, (char)c);
            int c2 = source_getc(cs);
            if (c2 == EOF) return;
            db_putc(buf, (char)c2);
            continue;
        }
        db_putc(buf, (char)c);
    }
}

/* ------------------------------------------------------------------ */
/* #define body accumulation: reads raw directive text (formals already
 * consumed by the caller for function-like macros) up through - but
 * not including - the terminating raw '\n', building a MacroBodyPiece
 * list. Comments are stripped; if a comment spans multiple physical
 * lines (rare, but real - see e.g. v30ide.h's CTL_DEF), each embedded
 * newline is individually emitted to output (when `active`) exactly
 * like a comment embedded in ordinary text would be - the #define
 * directive as a whole still only contributes its own single trailing
 * blank line via the normal directive-line accounting in directive.c;
 * these are the *additional* lines the multi-line comment itself
 * consumes. String/char literals are captured as opaque literal text
 * (formal names embedded inside quotes are NOT substituted - a known,
 * narrow deviation from the original cpp's quote-aware formal
 * scanning; see README). */
static MacroBodyPiece *read_macro_body(CppState *cs, char **params, int nparams, bool active) {
    MacroBodyPiece *head = NULL, *tail = NULL;
    DynBuf lit; db_init(&lit);

    #define FLUSH_LITERAL() \
        do { \
            if (lit.len > 0) { \
                MacroBodyPiece *pc = calloc(1, sizeof *pc); \
                pc->literal = db_take(&lit); \
                pc->param_index = -1; \
                if (tail) tail->next = pc; else head = pc; \
                tail = pc; \
                db_init(&lit); \
            } \
        } while (0)

    for (;;) {
        int c = source_peekc(cs);
        if (c == EOF || c == '\n') break;

        if (c == '/' && source_peekc2(cs) == '*') {
            source_getc(cs); source_getc(cs);
            skip_comment_body(cs, NULL, active);
            /* Comments vanish entirely (zero output characters), but
             * still act as a token boundary: "foo", comment, "bar"
             * tokenizes as two separate identifiers, each independently
             * eligible for macro lookup, even though nothing separates
             * them textually once the comment is gone - matching the
             * real cpp's documented "foo", comment, "bar" -> "foobar"
             * (concatenated, no inserted space) behavior. */
            continue;
        }

        if (c == '"' || c == '\'') {
            source_getc(cs);
            scan_literal_raw(cs, (char)c, &lit);
            continue;
        }

        if (is_ident_start(c)) {
            /* An identifier immediately follows: flush any pending
             * literal run first is NOT needed for correctness here
             * (literal text and identifier text both land in the same
             * DynBuf), but formal-parameter matching needs the
             * identifier isolated regardless of whether a comment
             * immediately preceded it. */
            char *id = read_identifier_from_source(cs);
            int pidx = -1;
            for (int i = 0; i < nparams; i++) {
                if (strcmp(params[i], id) == 0) { pidx = i; break; }
            }
            if (pidx >= 0) {
                FLUSH_LITERAL();
                MacroBodyPiece *pc = calloc(1, sizeof *pc);
                pc->literal = NULL;
                pc->param_index = pidx;
                if (tail) tail->next = pc; else head = pc;
                tail = pc;
            } else {
                db_puts(&lit, id, strlen(id));
            }
            free(id);
            continue;
        }

        /* ordinary character (including whitespace/punctuation) */
        db_putc(&lit, (char)source_getc(cs));
    }

    FLUSH_LITERAL();
    #undef FLUSH_LITERAL
    return head;
}

/* ------------------------------------------------------------------ */
/* #define directive: `name` has already been read. This function reads
 * everything else - optional formal-parameter list, then the body -
 * directly from the source stream. `active` is the directive's
 * pre-state (see directive.c's file header comment for the output-
 * line-accounting rule); it's only needed here to correctly emit
 * blank lines for any embedded multi-line comment within the
 * definition (the definition itself is only actually *installed* by
 * the caller when active, but the body must always be fully parsed
 * either way to correctly consume the source).
 */
void macro_do_define(CppState *cs, const char *name, bool active) {
    Macro *m = calloc(1, sizeof *m);
    m->name = xstrdup(name);

    int c = source_peekc(cs);
    if (c == '(') {
        source_getc(cs); /* consume '(' */
        m->is_function_like = true;
        m->nparams = 0;
        for (;;) {
            skip_hspace_only(cs);
            int pc = source_peekc(cs);
            if (pc == ')') { source_getc(cs); break; }
            if (pc == '\n' || pc == EOF) {
                cpp_error(cs, "%s: missing )", name);
                break;
            }
            if (pc == ',') { source_getc(cs); continue; }
            if (!is_ident_start(pc)) {
                cpp_error(cs, "%s: bad formal parameter", name);
                source_getc(cs);
                continue;
            }
            if (m->nparams >= MCPP_MAX_MACRO_PARAMS) {
                cpp_error(cs, "%s: too many formals", name);
                free(read_identifier_from_source(cs));
                continue;
            }
            m->params[m->nparams++] = read_identifier_from_source(cs);
        }
    } else if (c == '\n' || c == EOF) {
        /* Empty object-like macro, e.g. "#define EXIT" - body stays
         * empty; do not consume the newline here. */
        m->is_function_like = false;
        m->nparams = 0;
    } else {
        /* Object-like macro: exactly one separating character
         * (typically a space or tab) is discarded before the body
         * starts, matching the reference cpp's behavior. */
        source_getc(cs);
        m->is_function_like = false;
        m->nparams = 0;
    }

    m->body = read_macro_body(cs, m->params, m->nparams, active);
    if (active) {
        macro_install(cs, m);
    } else {
        macro_free_one(m);
    }
}

/* ------------------------------------------------------------------ */
/* Macro call expansion at the point `name` (already consumed from the
 * input) was found as an identifier during normal scanning. */

/* Skips whitespace, comments, and raw newlines while recording every
 * consumed raw character into `saved` (so it can be pushed back
 * verbatim on failure). Returns the first non-skipped character
 * without consuming it (still recorded in `saved` only if it was
 * itself whitespace/comment/newline, which it is not on return). */
static int lookahead_skip_blanks(CppState *cs, DynBuf *saved) {
    for (;;) {
        int c = source_getc(cs);
        if (c == EOF) return EOF;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r') {
            db_putc(saved, (char)c);
            continue;
        }
        if (c == '/' && source_peekc(cs) == '*') {
            db_putc(saved, (char)c);
            source_getc(cs); db_putc(saved, '*');
            int prev = 0;
            for (;;) {
                int cc = source_getc(cs);
                if (cc == EOF) break;
                db_putc(saved, (char)cc);
                if (prev == '*' && cc == '/') break;
                prev = cc;
            }
            continue;
        }
        /* Found a real token: push it back is the caller's job (we
         * return it unconsumed-from-caller's-perspective by ungetc'ing
         * here so callers uniformly re-peek). */
        source_ungetc(cs, c);
        return c;
    }
}

static void pushback_saved(CppState *cs, DynBuf *saved) {
    /* Push back in reverse order so replay order matches original. */
    for (size_t i = saved->len; i > 0; i--)
        source_ungetc(cs, (unsigned char)saved->data[i - 1]);
}

/* Collects raw actual-argument text for a function-like macro call.
 * The opening '(' has already been consumed. Fills `args` with up to
 * `max_args` newly-allocated raw strings and sets *nargs. Comments
 * within arguments are stripped (matching general comment removal);
 * strings/char literals are captured verbatim so embedded ,()  don't
 * confuse argument splitting. */
static void collect_actual_args(CppState *cs, const char *macro_name,
                                 char **args, int max_args, int *nargs) {
    int depth = 1;
    DynBuf cur; db_init(&cur);
    *nargs = 0;
    bool any_char_seen = false; /* to distinguish FOO() -> 0 args */

    for (;;) {
        int c = source_getc(cs);
        if (c == EOF) {
            cpp_error(cs, "%s: unterminated macro call", macro_name);
            break;
        }
        if (c == '/' && source_peekc(cs) == '*') {
            source_getc(cs);
            skip_comment_body(cs, NULL, false);
            continue;
        }
        if (c == '"' || c == '\'') {
            scan_literal_raw(cs, (char)c, &cur);
            any_char_seen = true;
            continue;
        }
        if (c == '(') {
            depth++;
            db_putc(&cur, (char)c);
            any_char_seen = true;
            continue;
        }
        if (c == ')') {
            depth--;
            if (depth == 0) {
                if (any_char_seen || *nargs > 0) {
                    if (*nargs < max_args) args[*nargs] = db_take(&cur);
                    else { free(db_take(&cur)); }
                    (*nargs)++;
                } else {
                    db_take(&cur); /* discard empty buffer, 0 args total */
                }
                return;
            }
            db_putc(&cur, (char)c);
            any_char_seen = true;
            continue;
        }
        if (c == ',' && depth == 1) {
            if (*nargs < max_args) args[*nargs] = db_take(&cur);
            else free(db_take(&cur));
            (*nargs)++;
            db_init(&cur);
            any_char_seen = false;
            continue;
        }
        db_putc(&cur, (char)c);
        any_char_seen = true;
    }
    db_take(&cur);
}

bool macro_try_expand(CppState *cs, const char *name) {
    Macro *m = macro_lookup(cs, name);
    if (!m || m->hidden) return false;

    char *args[MCPP_MAX_MACRO_PARAMS];
    int nargs = 0;

    if (m->is_function_like) {
        DynBuf saved; db_init(&saved);
        int c = lookahead_skip_blanks(cs, &saved);
        if (c != '(') {
            /* Not a call: restore everything we peeked and bail out,
             * leaving the identifier to be emitted as plain text. */
            pushback_saved(cs, &saved);
            free(saved.data);
            return false;
        }
        free(saved.data);
        source_getc(cs); /* consume '(' */
        collect_actual_args(cs, name, args, MCPP_MAX_MACRO_PARAMS, &nargs);

        if (nargs != m->nparams) {
            /* Special-case: a macro declared with zero formals,
             * "#define FOO() ...", called as "FOO()" yields nargs==0,
             * nparams==0 - fine. Otherwise mismatches just warn, then
             * pad/truncate. */
            cpp_warn(cs, "%s: argument mismatch", name);
        }
    }

    /* Build the substituted text. */
    DynBuf out; db_init(&out);
    for (MacroBodyPiece *bp = m->body; bp; bp = bp->next) {
        if (bp->param_index < 0) {
            db_puts(&out, bp->literal, strlen(bp->literal));
        } else {
            if (bp->param_index < nargs) {
                const char *a = args[bp->param_index];
                db_puts(&out, a, strlen(a));
            }
            /* missing actual -> substitutes as empty string */
        }
    }
    for (int i = 0; i < nargs; i++) free(args[i]);

    size_t len = out.len;
    char *text = db_take(&out);

    m->hidden = true;
    source_push_buffer(cs, text, len, m);
    return true;
}
