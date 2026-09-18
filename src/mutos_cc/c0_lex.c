/*
 * c0_lex.c - tokenizer implementation for mutos_c0.
 */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "mutos_cc.h"
#include "c0_lex.h"
#include "c0_diag.h"

#define LEX_PUSHBACK_MAX 2

void lex_init(Lexer *lx, FILE *fp, const char *filename)
{
    lx->fp = fp;
    lx->filename = filename;
    lx->line = 1;
    lx->npeek = 0;
    lx->at_eof = 0;
}

static int lex_rawgetc(Lexer *lx)
{
    int c;
    if (lx->npeek > 0) {
        c = lx->peek[--lx->npeek];
    } else {
        c = getc(lx->fp);
    }
    if (c == '\n')
        lx->line++;
    if (c == EOF)
        lx->at_eof = 1;
    return c;
}

/* Pushes `c` back so the next lex_rawgetc() returns it. A small LIFO
 * stack (LEX_PUSHBACK_MAX deep), not a single slot: two consecutive
 * ungetc calls with no intervening read - e.g.
 * skip_space_and_comments()'s "not a comment after all, put back
 * both characters I peeked" case below - must both survive, most
 * recently pushed first out, matching a real character stream being
 * rewound. */
static void lex_ungetc(Lexer *lx, int c)
{
    if (c == EOF)
        return;
    if (c == '\n')
        lx->line--;
    if (lx->npeek >= LEX_PUSHBACK_MAX)
        c0_error_at(lx->line, "internal: lexer pushback buffer overflow");
    else
        lx->peek[lx->npeek++] = c;
    lx->at_eof = 0;
}

/*
 * Skip whitespace and slash-star comments (K&R C has no "//" line
 * comments - a lone "//" is two ordinary DIVIDE tokens, matching
 * this project's K&R-only scope). Backslash-newline splicing is not
 * needed here since mutos_c0 normally consumes mutos_cpp's already
 * -spliced output (see mutos_cpp/README.md), but a stray one is
 * harmless to also treat as whitespace.
 */
static void skip_space_and_comments(Lexer *lx)
{
    for (;;) {
        int c = lex_rawgetc(lx);
        if (c == EOF)
            return;
        if (isspace(c))
            continue;
        if (c == '/') {
            int c2 = lex_rawgetc(lx);
            if (c2 == '*') {
                int prev = 0;
                for (;;) {
                    int cc = lex_rawgetc(lx);
                    if (cc == EOF) {
                        c0_error_at(lx->line, "Unterminated comment");
                        return;
                    }
                    if (prev == '*' && cc == '/')
                        break;
                    prev = cc;
                }
                continue;
            }
            lex_ungetc(lx, c2);
        }
        lex_ungetc(lx, c);
        return;
    }
}

static int is_ident_start(int c) { return isalpha(c) || c == '_'; }
static int is_ident_cont(int c)  { return isalnum(c) || c == '_'; }

struct kw { const char *name; TokKind kind; };
static const struct kw keywords[] = {
    {"int",      T_KW_INT},
    {"char",     T_KW_CHAR},
    {"float",    T_KW_FLOAT},
    {"double",   T_KW_DOUBLE},
    {"struct",   T_KW_STRUCT},
    {"long",     T_KW_LONG},
    {"unsigned", T_KW_UNSIGNED},
    {"union",    T_KW_UNION},
    {"short",    T_KW_SHORT},
    {"auto",     T_KW_AUTO},
    {"extern",   T_KW_EXTERN},
    {"static",   T_KW_STATIC},
    {"register", T_KW_REGISTER},
    {"goto",     T_KW_GOTO},
    {"return",   T_KW_RETURN},
    {"if",       T_KW_IF},
    {"while",    T_KW_WHILE},
    {"else",     T_KW_ELSE},
    {"switch",   T_KW_SWITCH},
    {"case",     T_KW_CASE},
    {"break",    T_KW_BREAK},
    {"continue", T_KW_CONTINUE},
    {"do",       T_KW_DO},
    {"default",  T_KW_DEFAULT},
    {"for",      T_KW_FOR},
    {"sizeof",   T_KW_SIZEOF},
    {"typedef",  T_KW_TYPEDEF},
    {"enum",     T_KW_ENUM},
    {NULL, T_EOF}
};

static TokKind keyword_lookup(const char *ident)
{
    for (const struct kw *k = keywords; k->name; k++)
        if (strcmp(k->name, ident) == 0)
            return k->kind;
    return T_IDENT;
}

static Token lex_ident(Lexer *lx, int first, int line)
{
    Token t = {0};
    t.line = line;
    char buf[LEX_IDENT_MAX];
    size_t n = 0;
    buf[n++] = (char)first;
    for (;;) {
        int c = lex_rawgetc(lx);
        if (!is_ident_cont(c)) {
            lex_ungetc(lx, c);
            break;
        }
        if (n < LEX_IDENT_MAX - 1)
            buf[n++] = (char)c;
        /* characters beyond LEX_IDENT_MAX are still consumed above,
         * just not stored - an absurdly long identifier will be
         * truncated for diagnostics, but this scanner never mis-reads
         * the token boundary. */
    }
    buf[n] = '\0';
    memcpy(t.ident, buf, n + 1); /* n <= LEX_IDENT_MAX-1, buf[n]=='\0' -
                                   * exact-length copy, always terminated. */
    t.kind = keyword_lookup(buf);
    return t;
}

/* Integer constant: K&R lexical rules - 0x/0X hex, leading 0 octal,
 * otherwise decimal; optional trailing l/L and u/U suffixes (order-
 * independent, at most one of each). */
static Token lex_number(Lexer *lx, int first, int line)
{
    Token t = {0};
    t.line = line;
    char buf[64];
    size_t n = 0;
    buf[n++] = (char)first;

    int is_float = 0;
    int base = 10;
    if (first == '0') {
        int c2 = lex_rawgetc(lx);
        if (c2 == 'x' || c2 == 'X') {
            base = 16;
            buf[n++] = (char)c2;
            for (;;) {
                int c = lex_rawgetc(lx);
                if (!isxdigit(c)) { lex_ungetc(lx, c); break; }
                if (n < sizeof buf - 1) buf[n++] = (char)c;
            }
        } else {
            lex_ungetc(lx, c2);
            base = 8;
        }
    }
    if (base != 16) {
        for (;;) {
            int c = lex_rawgetc(lx);
            if (isdigit(c)) {
                if (n < sizeof buf - 1) buf[n++] = (char)c;
                continue;
            }
            if (c == '.' && !is_float) {
                is_float = 1;
                base = 10; /* a leading-0 float like 0.5 is decimal */
                if (n < sizeof buf - 1) buf[n++] = (char)c;
                continue;
            }
            lex_ungetc(lx, c);
            break;
        }
    }
    buf[n] = '\0';

    if (is_float) {
        /* Float constants are lexed (so the token stream stays well-
         * formed for 08_float's corpus), but mutos_c0's current
         * grammar coverage does not yet consume T_FCON anywhere. */
        t.kind = T_FCON;
        return t;
    }

    long v = strtol(buf, NULL, base);
    int is_long = 0, is_unsigned = 0;
    for (;;) {
        int c = lex_rawgetc(lx);
        if (c == 'l' || c == 'L') { is_long = 1; continue; }
        if (c == 'u' || c == 'U') { is_unsigned = 1; continue; }
        lex_ungetc(lx, c);
        break;
    }
    t.kind = is_long ? T_LCON : T_ICON;
    t.ival = v;
    t.is_long = is_long;
    t.is_unsigned = is_unsigned;
    return t;
}

static int lex_escape(Lexer *lx)
{
    int c = lex_rawgetc(lx);
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'b': return '\b';
    case 'r': return '\r';
    case 'f': return '\f';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    case '0': return '\0';
    default:  return c;
    }
}

static Token lex_string(Lexer *lx, int line)
{
    Token t = {0};
    t.kind = T_STRING;
    t.line = line;
    size_t cap = 32, n = 0;
    char *s = malloc(cap);
    for (;;) {
        int c = lex_rawgetc(lx);
        if (c == EOF || c == '\n') {
            c0_error_at(line, "Unterminated string literal");
            break;
        }
        if (c == '"')
            break;
        if (c == '\\')
            c = lex_escape(lx);
        if (n + 1 >= cap) { cap *= 2; s = realloc(s, cap); }
        s[n++] = (char)c;
    }
    s[n] = '\0';
    t.sval = s;
    return t;
}

static Token lex_char(Lexer *lx, int line)
{
    Token t = {0};
    t.kind = T_CCON;
    t.line = line;
    int c = lex_rawgetc(lx);
    if (c == '\\')
        c = lex_escape(lx);
    t.ival = c;
    int close = lex_rawgetc(lx);
    if (close != '\'')
        c0_error_at(line, "Malformed character constant");
    return t;
}

/* Two/three-character operator table, longest-match-first per group. */
static Token lex_operator(Lexer *lx, int c, int line)
{
    Token t = {0};
    t.line = line;
#define TWO(a, b, k) if (c == (a)) { int n = lex_rawgetc(lx); \
        if (n == (b)) { t.kind = (k); return t; } lex_ungetc(lx, n); }

    switch (c) {
    case '{': t.kind = T_LBRACE; return t;
    case '}': t.kind = T_RBRACE; return t;
    case '[': t.kind = T_LBRACK; return t;
    case ']': t.kind = T_RBRACK; return t;
    case '(': t.kind = T_LPAREN; return t;
    case ')': t.kind = T_RPAREN; return t;
    case ':': t.kind = T_COLON;  return t;
    case ',': t.kind = T_COMMA;  return t;
    case ';': t.kind = T_SEMI;   return t;
    case '~': t.kind = T_TILDE;  return t;
    case '?': t.kind = T_QUEST;  return t;

    case '.': t.kind = T_DOT; return t;

    case '+':
        TWO('+', '+', T_INCR);
        TWO('+', '=', T_PLUSEQ);
        t.kind = T_PLUS; return t;
    case '-':
        TWO('-', '-', T_DECR);
        TWO('-', '=', T_MINUSEQ);
        TWO('-', '>', T_ARROW);
        t.kind = T_MINUS; return t;
    case '*':
        TWO('*', '=', T_STAREQ);
        t.kind = T_STAR; return t;
    case '/':
        TWO('/', '=', T_SLASHEQ);
        t.kind = T_SLASH; return t;
    case '%':
        TWO('%', '=', T_PERCENTEQ);
        t.kind = T_PERCENT; return t;
    case '=':
        TWO('=', '=', T_EQ);
        t.kind = T_ASSIGN; return t;
    case '!':
        TWO('!', '=', T_NE);
        t.kind = T_BANG; return t;
    case '<': {
        int n = lex_rawgetc(lx);
        if (n == '<') {
            int n2 = lex_rawgetc(lx);
            if (n2 == '=') { t.kind = T_SHLEQ; return t; }
            lex_ungetc(lx, n2);
            t.kind = T_SHL; return t;
        }
        if (n == '=') { t.kind = T_LE; return t; }
        lex_ungetc(lx, n);
        t.kind = T_LT; return t;
    }
    case '>': {
        int n = lex_rawgetc(lx);
        if (n == '>') {
            int n2 = lex_rawgetc(lx);
            if (n2 == '=') { t.kind = T_SHREQ; return t; }
            lex_ungetc(lx, n2);
            t.kind = T_SHR; return t;
        }
        if (n == '=') { t.kind = T_GE; return t; }
        lex_ungetc(lx, n);
        t.kind = T_GT; return t;
    }
    case '&':
        TWO('&', '&', T_ANDAND);
        TWO('&', '=', T_ANDEQ);
        t.kind = T_AMP; return t;
    case '|':
        TWO('|', '|', T_OROR);
        TWO('|', '=', T_OREQ);
        t.kind = T_PIPE; return t;
    case '^':
        TWO('^', '=', T_XOREQ);
        t.kind = T_CARET; return t;
    default:
        t.kind = T_UNKNOWN;
        return t;
    }
#undef TWO
}

Token lex_next(Lexer *lx)
{
    skip_space_and_comments(lx);
    int line = lx->line;
    int c = lex_rawgetc(lx);
    if (c == EOF) {
        Token t = {0};
        t.kind = T_EOF;
        t.line = line;
        return t;
    }
    if (is_ident_start(c))
        return lex_ident(lx, c, line);
    if (isdigit(c))
        return lex_number(lx, c, line);
    if (c == '"')
        return lex_string(lx, line);
    if (c == '\'')
        return lex_char(lx, line);
    return lex_operator(lx, c, line);
}

const char *tok_kind_name(TokKind k)
{
    switch (k) {
    case T_EOF: return "end of file";
    case T_IDENT: return "identifier";
    case T_ICON: return "integer constant";
    case T_LCON: return "long constant";
    case T_FCON: return "floating constant";
    case T_STRING: return "string literal";
    case T_CCON: return "character constant";
    case T_LBRACE: return "'{'";
    case T_RBRACE: return "'}'";
    case T_LPAREN: return "'('";
    case T_RPAREN: return "')'";
    case T_SEMI: return "';'";
    case T_KW_RETURN: return "'return'";
    default: return "token";
    }
}
