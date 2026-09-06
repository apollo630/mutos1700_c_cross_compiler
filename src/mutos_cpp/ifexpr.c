/*
 * ifexpr.c - constant-expression evaluator for "#if EXPR".
 *
 * Grammar and precedence match v7/cpp/cpy.y exactly (comma lowest,
 * then ?:, ||, &&, |, ^, &, ==/!=, relational, shift, +/-, * / %, then
 * unary -/!/~ and primary). Not exercised by this project's current
 * golden-test corpus (which only uses #ifdef/#ifndef), but implemented
 * for completeness since #if is a real, documented part of the tool.
 *
 * IMPORTANT, non-obvious behavior faithfully replicated from the
 * reference: a bare identifier in a #if expression evaluates to 1 if
 * it is currently #define'd and 0 otherwise - it is a defined-ness
 * test, NOT macro-value substitution. "defined(NAME)"/"defined NAME"
 * evaluate to that exact same 0/1 (matching cpy.y's grammar, where the
 * DEFINED production just returns the same numeric result a bare
 * identifier would). This is a genuine deviation from ANSI C's #if
 * (which macro-expands identifiers before evaluating), inherited
 * directly from the real MUTOS/V7 fast-cpp's yylex().
 */

#include <stdlib.h>
#include <string.h>
#include "mutos_cpp.h"

typedef enum { TK_EOF, TK_NUM, TK_IDENT, TK_OP } TokKind;

typedef struct {
    TokKind kind;
    long value;
    char ident[256];
    char op[3];
} Tok;

static CppState *g_cs;
static Tok g_tok;
static bool g_error;

static long parse_number(const char *s) {
    int base = 10;
    if (s[0] == '0') {
        if (s[1] == 'x' || s[1] == 'X') { base = 16; s += 2; }
        else { base = 8; s += 1; }
    }
    long n = 0;
    for (; *s; s++) {
        int c = (unsigned char)*s;
        if (c == 'l' || c == 'L') continue;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        if (d >= base) break;
        n = n * base + d;
    }
    return n;
}

static void skip_comment_inline(void) {
    source_getc(g_cs); source_getc(g_cs); /* consume "/ *" */
    int prev = 0;
    for (;;) {
        int c = source_getc(g_cs);
        if (c == EOF) { g_error = true; return; }
        if (prev == '*' && c == '/') break;
        prev = c;
    }
}

static int char_escape_value(int c) {
    switch (c) {
        case 'b': return '\b';
        case 't': return '\t';
        case 'n': return '\n';
        case 'f': return '\f';
        case 'r': return '\r';
        case '\\': return '\\';
        case '\'': return '\'';
        case '"': return '"';
        default:
            if (c >= '0' && c <= '7') return c - '0';
            return c;
    }
}

static void next_tok(void) {
    for (;;) {
        skip_hspace_only(g_cs);
        int c = source_peekc(g_cs);
        if (c == EOF || c == '\n') { g_tok.kind = TK_EOF; return; }
        if (c == '/' && source_peekc2(g_cs) == '*') { skip_comment_inline(); continue; }
        break;
    }
    int c = source_getc(g_cs);

    if ((c == '|' && source_peekc(g_cs) == '|') ||
        (c == '&' && source_peekc(g_cs) == '&') ||
        (c == '>' && source_peekc(g_cs) == '>') ||
        (c == '<' && source_peekc(g_cs) == '<') ||
        (c == '>' && source_peekc(g_cs) == '=') ||
        (c == '<' && source_peekc(g_cs) == '=') ||
        (c == '!' && source_peekc(g_cs) == '=') ||
        (c == '=' && source_peekc(g_cs) == '=')) {
        int c2 = source_getc(g_cs);
        g_tok.kind = TK_OP; g_tok.op[0] = (char)c; g_tok.op[1] = (char)c2; g_tok.op[2] = 0;
        return;
    }

    if (c >= '0' && c <= '9') {
        char buf[128]; int n = 0;
        buf[n++] = (char)c;
        while (n < 120) {
            int p = source_peekc(g_cs);
            if ((p >= '0' && p <= '9') || (p >= 'a' && p <= 'f') || (p >= 'A' && p <= 'F') ||
                p == 'x' || p == 'X' || p == 'l' || p == 'L') {
                buf[n++] = (char)source_getc(g_cs);
            } else break;
        }
        buf[n] = 0;
        g_tok.kind = TK_NUM;
        g_tok.value = parse_number(buf);
        return;
    }

    if (c == '\'') {
        int v;
        int c1 = source_getc(g_cs);
        if (c1 == '\\') v = char_escape_value(source_getc(g_cs));
        else v = c1;
        source_getc(g_cs); /* closing quote */
        g_tok.kind = TK_NUM;
        g_tok.value = v;
        return;
    }

    if (is_ident_start(c)) {
        char buf[256]; int n = 0;
        buf[n++] = (char)c;
        while (is_ident_cont(source_peekc(g_cs)) && n < 250) buf[n++] = (char)source_getc(g_cs);
        buf[n] = 0;
        strcpy(g_tok.ident, buf);
        g_tok.kind = TK_IDENT;
        return;
    }

    g_tok.kind = TK_OP; g_tok.op[0] = (char)c; g_tok.op[1] = 0;
}

static bool tok_is(const char *s) { return g_tok.kind == TK_OP && strcmp(g_tok.op, s) == 0; }

static long parse_comma(void);

static long parse_primary(void) {
    if (g_tok.kind == TK_NUM) { long v = g_tok.value; next_tok(); return v; }
    if (tok_is("(")) {
        next_tok();
        long v = parse_comma();
        if (tok_is(")")) next_tok();
        else cpp_error(g_cs, "#if: missing )");
        return v;
    }
    if (g_tok.kind == TK_IDENT) {
        if (strcmp(g_tok.ident, "defined") == 0) {
            next_tok();
            bool paren = false;
            if (tok_is("(")) { paren = true; next_tok(); }
            if (g_tok.kind != TK_IDENT) {
                cpp_error(g_cs, "#if: defined() expects an identifier");
                return 0;
            }
            long v = macro_lookup(g_cs, g_tok.ident) ? 1 : 0;
            next_tok();
            if (paren) {
                if (tok_is(")")) next_tok();
                else cpp_error(g_cs, "#if: defined(...): missing )");
            }
            return v;
        }
        long v = macro_lookup(g_cs, g_tok.ident) ? 1 : 0;
        next_tok();
        return v;
    }
    cpp_error(g_cs, "#if: syntax error");
    g_error = true;
    if (g_tok.kind != TK_EOF) next_tok();
    return 0;
}

static long parse_unary(void) {
    if (g_tok.kind == TK_OP && g_tok.op[1] == 0 &&
        (g_tok.op[0] == '-' || g_tok.op[0] == '!' || g_tok.op[0] == '~')) {
        char op = g_tok.op[0];
        next_tok();
        long v = parse_unary();
        if (op == '-') return -v;
        if (op == '!') return !v;
        return ~v;
    }
    return parse_primary();
}

static long parse_mul(void) {
    long v = parse_unary();
    for (;;) {
        if (tok_is("*")) { next_tok(); v = v * parse_unary(); }
        else if (tok_is("/")) {
            next_tok(); long r = parse_unary();
            if (r == 0) { cpp_error(g_cs, "#if: divide by zero"); v = 0; }
            else v = v / r;
        } else if (tok_is("%")) {
            next_tok(); long r = parse_unary();
            if (r == 0) { cpp_error(g_cs, "#if: divide by zero"); v = 0; }
            else v = v % r;
        } else break;
    }
    return v;
}

static long parse_add(void) {
    long v = parse_mul();
    for (;;) {
        if (tok_is("+")) { next_tok(); v = v + parse_mul(); }
        else if (tok_is("-")) { next_tok(); v = v - parse_mul(); }
        else break;
    }
    return v;
}

static long parse_shift(void) {
    long v = parse_add();
    for (;;) {
        if (tok_is("<<")) { next_tok(); v = v << parse_add(); }
        else if (tok_is(">>")) { next_tok(); v = v >> parse_add(); }
        else break;
    }
    return v;
}

static long parse_rel(void) {
    long v = parse_shift();
    for (;;) {
        if (tok_is("<")) { next_tok(); v = v < parse_shift(); }
        else if (tok_is(">")) { next_tok(); v = v > parse_shift(); }
        else if (tok_is("<=")) { next_tok(); v = v <= parse_shift(); }
        else if (tok_is(">=")) { next_tok(); v = v >= parse_shift(); }
        else break;
    }
    return v;
}

static long parse_eq(void) {
    long v = parse_rel();
    for (;;) {
        if (tok_is("==")) { next_tok(); v = v == parse_rel(); }
        else if (tok_is("!=")) { next_tok(); v = v != parse_rel(); }
        else break;
    }
    return v;
}

static long parse_band(void) {
    long v = parse_eq();
    while (tok_is("&")) { next_tok(); v = v & parse_eq(); }
    return v;
}

static long parse_bxor(void) {
    long v = parse_band();
    while (tok_is("^")) { next_tok(); v = v ^ parse_band(); }
    return v;
}

static long parse_bor(void) {
    long v = parse_bxor();
    while (tok_is("|")) { next_tok(); v = v | parse_bxor(); }
    return v;
}

static long parse_and(void) {
    long v = parse_bor();
    while (tok_is("&&")) { next_tok(); long r = parse_bor(); v = v && r; }
    return v;
}

static long parse_or(void) {
    long v = parse_and();
    while (tok_is("||")) { next_tok(); long r = parse_and(); v = v || r; }
    return v;
}

static long parse_ternary(void) {
    long v = parse_or();
    if (tok_is("?")) {
        next_tok();
        long a = parse_comma();
        if (tok_is(":")) next_tok();
        else cpp_error(g_cs, "#if: missing : in ?:");
        long b = parse_comma();
        return v ? a : b;
    }
    return v;
}

static long parse_comma(void) {
    long v = parse_ternary();
    while (tok_is(",")) { next_tok(); v = parse_ternary(); }
    return v;
}

long ifexpr_eval(CppState *cs, bool *ok) {
    g_cs = cs;
    g_error = false;
    next_tok();
    long v = parse_comma();
    if (g_tok.kind != TK_EOF) {
        cpp_error(cs, "#if: garbage after expression");
        g_error = true;
    }
    *ok = !g_error;
    return v;
}
