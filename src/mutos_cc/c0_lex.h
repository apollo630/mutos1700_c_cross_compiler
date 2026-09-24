/*
 * c0_lex.h - tokenizer for mutos_c0.
 *
 * A from-scratch, modern-C11 lexer for K&R-era C source (mutos_c0
 * normally reads the output of mutos_cpp, i.e. already
 * macro-expanded/comment-stripped text, but this lexer also
 * tolerates comments/whitespace directly so it can be pointed at a
 * raw .c file too). Matches this project's established approach
 * (mutos_cpp/mutos_as): clean-room implementation, not a literal
 * port of v7/cc's pointer-arithmetic character-class tables.
 */

#ifndef MUTOS_C0_LEX_H
#define MUTOS_C0_LEX_H

#include <stdio.h>

#define LEX_IDENT_MAX 128  /* generous; NCPS-significant-character
                             * truncation is the symbol table's job
                             * (c0_sym.c), not the lexer's - a longer
                             * name must still be scanned in full so
                             * later characters don't get mis-parsed
                             * as separate tokens. */

typedef enum {
    T_EOF = 0,
    T_IDENT, T_ICON, T_LCON, T_FCON, T_STRING, T_CCON,

    /* keywords (v7/cc/c00.c's kwtab, same set MUTOS inherits) */
    T_KW_INT, T_KW_CHAR, T_KW_FLOAT, T_KW_DOUBLE, T_KW_STRUCT,
    T_KW_LONG, T_KW_UNSIGNED, T_KW_UNION, T_KW_SHORT, T_KW_AUTO,
    T_KW_EXTERN, T_KW_STATIC, T_KW_REGISTER, T_KW_GOTO, T_KW_RETURN,
    T_KW_IF, T_KW_WHILE, T_KW_ELSE, T_KW_SWITCH, T_KW_CASE,
    T_KW_BREAK, T_KW_CONTINUE, T_KW_DO, T_KW_DEFAULT, T_KW_FOR,
    T_KW_SIZEOF, T_KW_TYPEDEF, T_KW_ENUM,

    /* punctuation / operators */
    T_LBRACE, T_RBRACE, T_LBRACK, T_RBRACK, T_LPAREN, T_RPAREN,
    T_COLON, T_COMMA, T_SEMI, T_DOT, T_ARROW, T_QUEST,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_SHL, T_SHR, T_AMP, T_PIPE, T_CARET, T_TILDE, T_BANG,
    T_ANDAND, T_OROR,
    T_ASSIGN, T_PLUSEQ, T_MINUSEQ, T_STAREQ, T_SLASHEQ, T_PERCENTEQ,
    T_SHLEQ, T_SHREQ, T_ANDEQ, T_OREQ, T_XOREQ,
    T_EQ, T_NE, T_LE, T_LT, T_GE, T_GT,
    T_INCR, T_DECR,

    T_UNKNOWN
} TokKind;

typedef struct {
    TokKind kind;
    int     line;              /* physical line the token STARTED on */
    char    ident[LEX_IDENT_MAX]; /* T_IDENT text, NUL-terminated */
    long    ival;               /* T_ICON / T_LCON / T_CCON value */
    int     is_long;            /* T_ICON: had an 'l'/'L' suffix */
    int     is_unsigned;        /* T_ICON: had a 'u'/'U' suffix */
    char   *sval;                /* T_STRING: owned, malloc'd, escapes
                                   * already processed (v7/cc/c00.c's
                                   * mapch() rules - see c0_lex.c's
                                   * lex_escape()); NUL-terminated for
                                   * convenience, but may also contain
                                   * embedded NULs ("a\0b"), so slen,
                                   * not strlen(), is its length */
    size_t  slen;                /* T_STRING: number of bytes in sval,
                                   * excluding the convenience NUL */
} Token;

typedef struct {
    FILE *fp;
    const char *filename;   /* for diagnostics only */
    int   line;
    int   peek[2];           /* small LIFO pushback stack (most
                               * recently pushed-back char last) - 2
                               * slots because skip_space_and_comments()
                               * needs to push back up to 2 characters
                               * at once (a '/' that turns out not to
                               * start a comment, plus the character
                               * after it that was peeked to find
                               * that out) */
    int   npeek;              /* 0, 1, or 2 characters currently
                               * pushed back */
    int   at_eof;
} Lexer;

void  lex_init(Lexer *lx, FILE *fp, const char *filename);
Token lex_next(Lexer *lx);
const char *tok_kind_name(TokKind k);

#endif /* MUTOS_C0_LEX_H */
