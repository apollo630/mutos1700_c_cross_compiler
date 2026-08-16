/*
 * lexer.c - tokenizer for the native MUTOS 1700 `as` syntax.
 *
 * Deliberately simple and hand-written (no lex/flex) to keep full
 * control over the size-marker ('*', '#') and hex-marker ('/')
 * ambiguities documented in mutos_as.h.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "mutos_as.h"

void lexer_init(Lexer *lx, const char *src, size_t len, const char *filename)
{
    lx->src = src;
    lx->pos = 0;
    lx->len = len;
    lx->line = 1;
    lx->col = 1;
    lx->filename = filename;
    lx->last_type = TOK_NEWLINE; /* start-of-input counts as prefix position */
}

static int peek_at(const Lexer *lx, size_t off)
{
    size_t p = lx->pos + off;
    if (p >= lx->len)
        return -1;
    return (unsigned char)lx->src[p];
}

static int cur(const Lexer *lx)
{
    return peek_at(lx, 0);
}

static void advance(Lexer *lx)
{
    if (lx->pos >= lx->len)
        return;
    if (lx->src[lx->pos] == '\n') {
        lx->line++;
        lx->col = 1;
    } else {
        lx->col++;
    }
    lx->pos++;
}

static bool is_ident_start(int c)
{
    return c == '_' || isalpha(c);
}

static bool is_ident_cont(int c)
{
    return c == '_' || isalnum(c);
}

static bool is_hex_digit(int c)
{
    return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/* Skips spaces/tabs (not newlines) and '|'-comments (to end of line).
 * Per mutos_as.h, '|' comments (including compiler-internal hints like
 * "|NREG 3" / "|RTYP 0") carry zero semantic weight and are simply
 * discarded here - they never reach the parser as tokens. */
static void skip_insignificant(Lexer *lx)
{
    for (;;) {
        int c = cur(lx);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lx);
            continue;
        }
        if (c == '|') {
            while (cur(lx) != -1 && cur(lx) != '\n')
                advance(lx);
            continue;
        }
        break;
    }
}

/* Tokens that "end in a value" - if the previously emitted token was
 * one of these, a following '/' is in INFIX position (divide). Any
 * other previous token means PREFIX position (hex-constant marker).
 *
 * Deliberately does NOT include TOK_IDENT: an identifier is either a
 * mnemonic (in which case what follows starts a fresh operand, e.g.
 * "in /C2", "out /D6" in mch.s - '/' here MUST be the hex marker) or a
 * symbol reference inside an ongoing expression - the lexer alone
 * cannot tell those apart (that needs the parser's grammatical
 * position, which isn't available at this layer). Every real example
 * of a bare identifier directly followed by '/' in the corpus is the
 * mnemonic case, so treating IDENT as "does not end a value" (i.e.
 * still prefix/hex position) matches the evidence; the only confirmed
 * infix-division sample is NUMBER-then-'/' (mch.s "mov cx,#2048 /2"). */
static bool ends_in_value(TokenType tt)
{
    return tt == TOK_NUMBER || tt == TOK_RPAREN ||
           tt == TOK_LOCAL_LABEL_REF || tt == TOK_DOT;
}

static Token lexer_next_raw(Lexer *lx)
{
    Token t;
    memset(&t, 0, sizeof(t));

    skip_insignificant(lx);

    t.line = lx->line;
    t.col = lx->col;

    int c = cur(lx);
    if (c == -1) {
        t.type = TOK_EOF;
        t.text = lx->src + lx->pos;
        t.len = 0;
        return t;
    }

    if (c == '\n') {
        advance(lx);
        t.type = TOK_NEWLINE;
        t.text = "\n";
        t.len = 1;
        return t;
    }

    if (c == ';') {
        advance(lx);
        t.type = TOK_SEMI;
        t.text = ";";
        t.len = 1;
        return t;
    }

    /* Numbers and the two local-label forms (single digit + ':'/'f'/'b'). */
    if (isdigit(c)) {
        size_t start = lx->pos;
        int digit_count = 0;
        while (isdigit(cur(lx))) {
            advance(lx);
            digit_count++;
        }
        size_t run_len = lx->pos - start;

        if (digit_count == 1) {
            int nc = cur(lx);
            if (nc == ':') {
                advance(lx); /* consume ':' */
                t.type = TOK_LOCAL_LABEL_DEF;
                t.text = lx->src + start;
                t.len = lx->pos - start;
                t.local_digit = lx->src[start] - '0';
                return t;
            }
            if ((nc == 'f' || nc == 'b') && !is_ident_cont(peek_at(lx, 1))) {
                advance(lx); /* consume 'f' or 'b' */
                t.type = TOK_LOCAL_LABEL_REF;
                t.text = lx->src + start;
                t.len = lx->pos - start;
                t.local_digit = lx->src[start] - '0';
                /* text[digit_count] is 'f' or 'b'; caller can inspect via text[len-1] */
                return t;
            }
        }

        /* Trailing '.' is accepted but no longer changes the base (see
         * mutos_as.h - real hardware-linked symbol values prove the
         * default base is decimal, not octal; the '.' appears to be a
         * redundant stylistic habit in c1's output). Both branches now
         * parse in base 10; kept separate so num_base still records
         * which spelling was used, for diagnostics. */
        if (cur(lx) == '.') {
            advance(lx);
            t.type = TOK_NUMBER;
            t.num_base = NUMBASE_DECIMAL;
            t.text = lx->src + start;
            t.len = lx->pos - start;
            t.num_value = strtol(lx->src + start, NULL, 10); /* stops at '.' */
            return t;
        }

        t.type = TOK_NUMBER;
        t.num_base = NUMBASE_DECIMAL;
        t.text = lx->src + start;
        t.len = run_len;
        {
            char buf[32];
            size_t n = run_len < sizeof(buf) - 1 ? run_len : sizeof(buf) - 1;
            memcpy(buf, lx->src + start, n);
            buf[n] = '\0';
            t.num_value = strtol(buf, NULL, 10);
        }
        return t;
    }

    /* '/' - hex-constant marker in PREFIX position (directly following
     * a token that does NOT end in a value - see ends_in_value()),
     * when followed by hex digits, e.g. ".byte /44,/6f" or "mov
     * ax,#/f80". In INFIX position (following a NUMBER/IDENT/')'/local
     * label ref/'.') it is instead the divide operator - confirmed by
     * real code: mch.s "mov cx,#2048  /2" is 2048/2, not two adjacent
     * numbers (2048 decimal, then a stray hex "/2"). */
    if (c == '/' && !ends_in_value(lx->last_type) && is_hex_digit(peek_at(lx, 1))) {
        advance(lx); /* consume '/' */
        size_t start = lx->pos;
        while (is_hex_digit(cur(lx)))
            advance(lx);
        t.type = TOK_NUMBER;
        t.num_base = NUMBASE_HEX;
        t.text = lx->src + start - 1; /* include the '/' in the raw text */
        t.len = (lx->pos - start) + 1;
        {
            char buf[32];
            size_t n = lx->pos - start;
            if (n > sizeof(buf) - 1) n = sizeof(buf) - 1;
            memcpy(buf, lx->src + start, n);
            buf[n] = '\0';
            t.num_value = strtol(buf, NULL, 16);
        }
        return t;
    }

    /* '.' followed by a letter => directive, e.g. .globl .comm .text
     * .data .even. ('.' followed by a digit never occurs standalone -
     * that case is consumed as part of a decimal NUMBER above.) */
    if (c == '.' && is_ident_start(peek_at(lx, 1))) {
        size_t start = lx->pos;
        advance(lx); /* consume '.' */
        while (is_ident_cont(cur(lx)))
            advance(lx);
        t.type = TOK_DOT_IDENT;
        t.text = lx->src + start;
        t.len = lx->pos - start;
        return t;
    }

    /* Bare '.' - the location-counter symbol (e.g. ".=.+4",
     * "_szicode: . - _icode"). Only reached when '.' is NOT followed by
     * an identifier-start (that case became TOK_DOT_IDENT above) and
     * NOT preceded by a digit (trailing-'.' decimal numbers are
     * consumed inside the digit-scanning branch above). */
    if (c == '.') {
        advance(lx);
        t.type = TOK_DOT;
        t.text = ".";
        t.len = 1;
        return t;
    }

    if (is_ident_start(c)) {
        size_t start = lx->pos;
        while (is_ident_cont(cur(lx)))
            advance(lx);
        t.type = TOK_IDENT;
        t.text = lx->src + start;
        t.len = lx->pos - start;
        return t;
    }

    /* Single-character punctuation tokens. */
    switch (c) {
        case ':': t.type = TOK_COLON;  advance(lx); t.text=":"; t.len=1; return t;
        case ',': t.type = TOK_COMMA;  advance(lx); t.text=","; t.len=1; return t;
        case '(': t.type = TOK_LPAREN; advance(lx); t.text="("; t.len=1; return t;
        case ')': t.type = TOK_RPAREN; advance(lx); t.text=")"; t.len=1; return t;
        case '*': t.type = TOK_STAR;   advance(lx); t.text="*"; t.len=1; return t;
        case '#': t.type = TOK_HASH;   advance(lx); t.text="#"; t.len=1; return t;
        case '/': t.type = TOK_SLASH;  advance(lx); t.text="/"; t.len=1; return t;
        case '+': t.type = TOK_PLUS;   advance(lx); t.text="+"; t.len=1; return t;
        case '-': t.type = TOK_MINUS;  advance(lx); t.text="-"; t.len=1; return t;
        case '=': t.type = TOK_EQUALS; advance(lx); t.text="="; t.len=1; return t;
        case '@': t.type = TOK_AT;     advance(lx); t.text="@"; t.len=1; return t;
        default:
            t.type = TOK_UNKNOWN;
            t.text = lx->src + lx->pos;
            t.len = 1;
            advance(lx);
            return t;
    }
}

Token lexer_next(Lexer *lx)
{
    Token t = lexer_next_raw(lx);
    lx->last_type = t.type;
    return t;
}
