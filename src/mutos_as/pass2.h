/*
 * pass2.h - expression tree + operand classification (Step 2).
 *
 * Builds on top of the Statement/Operand token runs produced by
 * parser.c. This layer does two jobs:
 *
 *   1. Parses an operand's flat token run into a proper expression
 *      tree with correct operator precedence (unary +/- tightest,
 *      then * and /, then + and -). This is where the '*'/'#'
 *      prefix-vs-infix ambiguity gets resolved: a leading '*' or '#'
 *      is consumed by the OPERAND classifier below as a size marker
 *      (see mutos_as.h), never reaching the expression parser as an
 *      operator; a '*' appearing anywhere else in the token run is a
 *      genuine multiply operator (confirmed in real code: mch.s has
 *      "mov ax,/01*4+2", computing an interrupt-vector byte offset).
 *
 *   2. Classifies the operand's overall syntactic shape into one of
 *      the addressing-mode forms actually observed in real
 *      hardware-linked code (see the consolidated Milestone 2 notes):
 *        - bare register                  "ax", "bp", "di", ...
 *        - register-indirect, no disp     "(bp)", "(di)"
 *        - register-indirect, with disp   "*-6.(bp)"   (byte, '*')
 *                                          "#N.(bp)"    (word, '#' - symmetric, not yet directly observed)
 *        - immediate                      "*4."  (byte)   "#1024." (word)
 *        - direct/bare expression         "_global_", "monssvec+2",
 *                                          ". - _icode", jump targets
 *                                          like "cret", "L5", "3f"
 *
 *   What this layer deliberately does NOT do yet: resolve symbol
 *   values (needs a multi-pass symbol table + address assignment),
 *   or pick an actual opcode/encoding (needs a per-mnemonic table).
 *   Both are later steps built on top of this one.
 */

#ifndef MUTOS_AS_PASS2_H
#define MUTOS_AS_PASS2_H

#include "mutos_as.h"

/* ------------------------------------------------------------------ */
/* Expression trees                                                     */
/* ------------------------------------------------------------------ */

typedef enum {
    EX_NUM,       /* constant, value in num */
    EX_SYM,       /* symbol reference, name in sym/sym_len */
    EX_LOCCTR,    /* bare '.' - current location counter */
    EX_LOCALREF,  /* numeric local label reference, e.g. "1f"/"1b" */
    EX_NEG,       /* unary minus, lhs = operand */
    EX_ADD, EX_SUB, EX_MUL, EX_DIV  /* binary, lhs/rhs = operands */
} ExprOp;

typedef struct ExprNode {
    ExprOp op;
    long        num;          /* EX_NUM */
    const char *sym;          /* EX_SYM: not NUL-terminated, use sym_len */
    size_t      sym_len;
    int         local_digit;  /* EX_LOCALREF: 0-9 */
    char        local_dir;    /* EX_LOCALREF: 'f' or 'b' */
    struct ExprNode *lhs;
    struct ExprNode *rhs;      /* NULL for unary/leaf nodes */
} ExprNode;

/* Parses tokens[*idx .. ntokens) as an arithmetic expression with
 * standard precedence, advancing *idx past what it consumed. Returns
 * NULL and leaves *idx unchanged on a syntax error (caller decides how
 * to report it). Returned nodes are heap-allocated (see expr_free). */
ExprNode *expr_parse(const Token *tokens, int ntokens, int *idx);

void expr_free(ExprNode *e);

/* Renders an expression back to a human-readable string (for
 * diagnostics/testing), writing into buf (size bytes, NUL-terminated,
 * truncated if necessary). */
void expr_to_string(const ExprNode *e, char *buf, size_t size);

/* Evaluates an expression to a constant, IF every leaf is a plain
 * number (no EX_SYM/EX_LOCCTR/EX_LOCALREF, all of which need a symbol
 * table / address assignment that doesn't exist yet). Returns true and
 * sets *out on success; returns false if the expression is not yet
 * constant-foldable at this stage. */
bool expr_try_eval_const(const ExprNode *e, long *out);

/* ------------------------------------------------------------------ */
/* Operand classification                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    SZ_NONE = 0,  /* no explicit '*'/'#' marker present */
    SZ_BYTE,      /* '*' marker */
    SZ_WORD       /* '#' marker */
} SizeMarker;

typedef enum {
    ADDR_REGISTER,   /* bare register operand, e.g. "ax" */
    ADDR_INDIRECT,   /* "(reg)" or "<marker><disp>(reg)" */
    ADDR_IMMEDIATE,  /* "<marker><expr>", no parens */
    ADDR_DIRECT      /* bare expression: direct address / jump target / symbol arithmetic */
} AddrMode;

/* Register classes, needed later to pick opcode forms (e.g. AL/AX only
 * get the short A0-A3 direct-address encodings - see consolidated
 * Milestone 2 notes). */
typedef enum {
    REG_NONE = 0,
    REG_WORD,     /* ax cx dx bx sp bp si di */
    REG_BYTE,     /* al ah cl ch dl dh bl bh */
    REG_SEGMENT   /* cs ds es ss */
} RegClass;

typedef struct {
    AddrMode   mode;
    SizeMarker size;         /* explicit '*'/'#' marker, if any (ADDR_INDIRECT/ADDR_IMMEDIATE) */

    /* ADDR_REGISTER: the register itself. ADDR_INDIRECT: the base
     * register inside the parens. */
    Token      reg;
    RegClass   reg_class;

    /* ADDR_INDIRECT: optional displacement (NULL if none, e.g. "(bp)").
     * ADDR_IMMEDIATE / ADDR_DIRECT: the expression itself. */
    ExprNode  *expr;

    bool       ok;           /* false if classification/parsing failed */
} ParsedOperand;

/* Looks up a register name; returns REG_NONE if `text`/`len` is not a
 * known register. */
RegClass reg_lookup(const char *text, size_t len);

/* Classifies one Operand (as produced by the parser.c token-run form)
 * into its addressing mode + expression. */
ParsedOperand classify_operand(const Operand *op);

void parsed_operand_free(ParsedOperand *po);

#endif /* MUTOS_AS_PASS2_H */
