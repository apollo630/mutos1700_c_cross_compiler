/*
 * mutos_as.h - MUTOS 1700 Cross-Assembler (Milestone 2)
 *
 * Core token/statement data structures shared by the lexer and the
 * statement-level parser. This header intentionally stops short of
 * defining instruction-encoding tables (opcodes, addressing-mode
 * encodings) - that belongs to the Pass 2 encoder, which is out of
 * scope for this skeleton.
 *
 * SYNTAX REFERENCE (from Assembler_as.pdf, cross-checked against real
 * c1-compiler output and real hardware-linked object files):
 *
 *   Statement form:   [<label>].[<prefix>]<mnemonic>[<operand>].[<comment>]
 *   Assignment form:  [<label>].<name> = <expression>
 *   Statements are separated by newline or ';'.
 *
 *   Labels:
 *     - name labels:      "name:"
 *     - numeric local:    single digit 0-9 followed by ':' to define,
 *                         referenced as "<digit>b" (nearest backward)
 *                         or "<digit>f" (nearest forward)
 *     - c1-generated code additionally uses symbolic labels such as
 *       "L4:", "L20001:" - these are ordinary name labels as far as
 *       the lexer/parser are concerned.
 *
 *   Comments:
 *     - '|' introduces a comment that runs to end of line. Compiler-
 *       generated comments such as "|NREG 3" (register-count hint) and
 *       "|RTYP 0" (register-type hint) are purely informational and
 *       carry no semantic weight for the assembler - they are dropped
 *       by the lexer exactly like any other '|' comment.
 *
 *   Numbers (CORRECTED - see below):
 *     - default base is DECIMAL. Confirmed against multiple real,
 *       hardware-linked symbol values (".comm _canonb,256" links to
 *       symbol value 256 decimal / 0x100, not 174 = octal "256";
 *       ".comm _msgbuf,1024" links to 1024 decimal / 0x400, not
 *       532 = octal "1024"; ".comm _dk_numb,12" links to 12 decimal,
 *       not 10 = octal "12") and against real instruction operands
 *       (mch.s "mov cx,#2048  /2" only makes sense as decimal
 *       2048/2=1024, a plausible word-count constant - as octal,
 *       "2048" isn't even a valid octal literal, since '8' is not an
 *       octal digit). Assembler_as.pdf's documented "no suffix means
 *       octal, trailing '.' means decimal" V7/PDP-11 convention does
 *       NOT hold for MUTOS 1700's `as` in practice - the trailing '.'
 *       c1 always emits appears to be a redundant/optional stylistic
 *       habit rather than semantically required. A leading-zero-based
 *       octal convention (as in C) has not been observed either way;
 *       treat true octal literals as an open question until a
 *       disambiguating real-code example turns up.
 *     - a leading '/' marks a HEX constant, e.g. ".byte /44,/6f"; this
 *       is disambiguated from the divide operator by POSITION (prefix
 *       vs. infix), tracked via the lexer's last-emitted-token type -
 *       confirmed necessary by real code: mch.s "mov cx,#2048  /2" is
 *       infix divide (last token before '/' was a NUMBER), while
 *       ".byte /44,/6f" and "mov ax,#/f80" are prefix hex markers
 *       (last token was TOK_COMMA / TOK_HASH respectively).
 *
 *   Operand size markers:
 *     - '*' prefixing an expression marks BYTE-sized storage
 *           (confirmed: group1 opcode 0x83 /digit ib for arithmetic,
 *           0xC6 /0 ib for byte immediate-to-memory)
 *     - '#' prefixing an expression marks WORD-sized storage
 *           (confirmed: 0xB8+reg iw for immediate-to-register,
 *           0xC7 /0 iw for word immediate-to-memory)
 *     - both '*' and '/' are ALSO arithmetic operators (multiply,
 *       divide) when they appear in infix position within an
 *       expression; the parser disambiguates by position, not the
 *       lexer.
 *
 *   Addressing modes seen in real code:
 *     - "(bp)", "(di)", "(si)", "(bx)"            - indirect, no disp
 *     - "*-6.(bp)"                                - indirect, byte disp
 *     - "#16960.(bp)"                             - indirect, word disp (not yet observed, symmetric)
 *     - "#1024."                                  - word immediate
 *     - "_symbol"                                 - direct absolute address
 *
 * This header is deliberately independent of mutos_aout.h - the lexer/
 * parser operate purely on the textual syntax and know nothing about
 * the binary a.out format yet.
 */

#ifndef MUTOS_AS_H
#define MUTOS_AS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/* Tokens                                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    TOK_EOF = 0,
    TOK_NEWLINE,        /* statement separator (also ';') */
    TOK_IDENT,          /* identifier: label name, mnemonic, register, directive word (without leading '.') */
    TOK_DOT_IDENT,      /* ".xxx" - directive, e.g. .globl .comm .text .data .even */
    TOK_DOT,            /* bare '.' - the location-counter symbol, e.g. ".=.+4", "_szicode: . - _icode" */
    TOK_NUMBER,         /* numeric literal; see Number.base/value below */
    TOK_LOCAL_LABEL_DEF,/* single digit immediately followed by ':' , e.g. "1:" */
    TOK_LOCAL_LABEL_REF,/* single digit immediately followed by 'f' or 'b', e.g. "1f" "1b" */
    TOK_COLON,          /* ':' (name-label definition) */
    TOK_COMMA,          /* ',' */
    TOK_LPAREN,         /* '(' */
    TOK_RPAREN,         /* ')' */
    TOK_STAR,           /* '*' - byte-size marker (prefix) or multiply (infix) */
    TOK_HASH,           /* '#' - word-size marker (prefix) or ??? (infix, not observed as operator) */
    TOK_SLASH,          /* '/' - hex-constant marker (prefix) or divide (infix) */
    TOK_PLUS,           /* '+' */
    TOK_MINUS,          /* '-' */
    TOK_EQUALS,         /* '=' - assignment form */
    TOK_SEMI,           /* ';' - explicit statement separator (folded into TOK_NEWLINE by lexer) */
    TOK_AT,             /* '@' - register-indirect jump/call target, e.g. "call @si", "jmp @bx" (mch.s) */
    TOK_UNKNOWN         /* unrecognized character, kept for diagnostics instead of hard-failing */
} TokenType;

typedef enum {
    NUMBASE_OCTAL = 8,      /* default, no suffix */
    NUMBASE_DECIMAL = 10,   /* trailing '.' */
    NUMBASE_HEX = 16        /* leading '/' - value already consumed with the marker by the lexer */
} NumberBase;

typedef struct {
    TokenType   type;
    /* Raw source text of the token (not NUL-terminated copy; use len).
     * Points into the caller-owned source buffer, valid for the
     * lifetime of that buffer. */
    const char *text;
    size_t      len;
    /* For TOK_NUMBER / TOK_LOCAL_LABEL_DEF / TOK_LOCAL_LABEL_REF: */
    long        num_value;     /* parsed integer value (numbers only) */
    NumberBase  num_base;      /* which base was used (numbers only) */
    int         local_digit;   /* 0-9 for local label tokens */
    /* Source position, for diagnostics. */
    int         line;
    int         col;
} Token;

/* ------------------------------------------------------------------ */
/* Lexer                                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *src;       /* full source buffer (NUL-terminated) */
    size_t      pos;       /* current byte offset into src */
    size_t      len;       /* length of src, excluding NUL */
    int         line;      /* current 1-based line number */
    int         col;       /* current 1-based column number */
    const char *filename;  /* for diagnostics only */
    /* Type of the last significant token emitted, used solely to
     * disambiguate '/' as a hex-constant marker (prefix position)
     * vs. divide operator (infix position) - see lexer_next(). */
    TokenType   last_type;
} Lexer;

void  lexer_init(Lexer *lx, const char *src, size_t len, const char *filename);

/* Fetches the next token. Comments ('|' to end of line) are consumed
 * and never returned as tokens - they are invisible to the parser,
 * matching their "zero semantic effect" role in the real toolchain. */
Token lexer_next(Lexer *lx);

/* ------------------------------------------------------------------ */
/* Statements                                                           */
/*                                                                       */
/* A Statement is the parser's output for one line/segment between      */
/* separators. Operands are NOT yet parsed into an expression tree -    */
/* that is Pass 2's job, once the encoder tables exist. For now each    */
/* operand is kept as a flat run of tokens (split on top-level commas), */
/* which is enough to validate that real c1-generated .s files parse    */
/* cleanly end-to-end.                                                  */
/* ------------------------------------------------------------------ */

/* Raised from an original value of 4: real .byte/.word data-directive
 * lists can have many comma-separated entries (e.g. mch.s has a
 * 5-operand ".byte" list of star-hex-marked bytes for a raw long jmp
 * encoding) - true instructions never need more than 2 operands, so
 * this only matters for directives, but the array is shared. */
#define MAX_OPERANDS   32
#define MAX_TOKENS_PER_OPERAND 16

typedef struct {
    Token tokens[MAX_TOKENS_PER_OPERAND];
    int   ntokens;
} Operand;

typedef enum {
    STMT_EMPTY,          /* blank line, nothing to do */
    STMT_DIRECTIVE,      /* .globl / .comm / .text / .data / .even / ... */
    STMT_ASSIGNMENT,     /* [label.]name = expr */
    STMT_INSTRUCTION,    /* [label:] [prefix] mnemonic [operands] */
    STMT_LABEL_ONLY,     /* a bare label with nothing else on the statement */
    STMT_DATA_VALUE      /* [label:] <expr>  - implicit data word, e.g. "_daten_v:/1234" or "_szicode: . - _icode" (no directive/mnemonic name) */
} StmtKind;

typedef struct {
    StmtKind kind;

    /* Label(s), if any. A statement may carry MULTIPLE colon-terminated
     * labels before the actual mnemonic (seen in real c1 output, e.g.
     * "L10:L8:mov dx,di" from malloc.s - two labels on one instruction). */
    Token labels[4];
    int   nlabels;
    bool  has_local_label_def;
    int   local_label_digit; /* valid if has_local_label_def */

    /* For STMT_DIRECTIVE / STMT_INSTRUCTION / STMT_ASSIGNMENT: */
    Token mnemonic_or_name; /* directive name, mnemonic, or assigned name */

    Operand operands[MAX_OPERANDS];
    int     noperands;

    int line; /* source line this statement started on */
} Statement;

/* Small fixed-size lookahead queue: labels require peeking two tokens
 * ahead ("IDENT" then ":" vs. "IDENT" as a mnemonic), so a single-token
 * lookahead is not enough. */
#define PARSER_LOOKAHEAD_MAX 2

typedef struct {
    Lexer   lx;
    Token   queue[PARSER_LOOKAHEAD_MAX];
    int     queued;         /* number of valid tokens currently sitting in queue[] */
    int     error_count;
    const char *filename;
} Parser;

void parser_init(Parser *p, const char *src, size_t len, const char *filename);

/* Parses the next statement. Returns false at end of input. */
bool parser_next_statement(Parser *p, Statement *out);

#endif /* MUTOS_AS_H */
