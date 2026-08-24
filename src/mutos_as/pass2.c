/*
 * pass2.c - see pass2.h for the overall design.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pass2.h"

/* ------------------------------------------------------------------ */
/* Expression parsing (recursive descent, standard precedence)          */
/* ------------------------------------------------------------------ */

static ExprNode *node_new(ExprOp op)
{
    ExprNode *n = calloc(1, sizeof(*n));
    n->op = op;
    return n;
}

static TokenType peek_type(const Token *tokens, int ntokens, int idx)
{
    if (idx >= ntokens)
        return TOK_EOF;
    return tokens[idx].type;
}

static ExprNode *parse_primary(const Token *tokens, int ntokens, int *idx)
{
    if (*idx >= ntokens)
        return NULL;
    Token t = tokens[*idx];

    switch (t.type) {
        case TOK_NUMBER: {
            ExprNode *n = node_new(EX_NUM);
            n->num = t.num_value;
            (*idx)++;
            return n;
        }
        case TOK_DOT: {
            ExprNode *n = node_new(EX_LOCCTR);
            (*idx)++;
            return n;
        }
        case TOK_LOCAL_LABEL_REF: {
            ExprNode *n = node_new(EX_LOCALREF);
            n->local_digit = t.local_digit;
            n->local_dir = t.text[t.len - 1];
            (*idx)++;
            return n;
        }
        case TOK_IDENT: {
            ExprNode *n = node_new(EX_SYM);
            n->sym = t.text;
            n->sym_len = t.len;
            (*idx)++;
            return n;
        }
        default:
            return NULL; /* unsupported primary (e.g. stray '(' - addressing
                          * parens are stripped by classify_operand before
                          * we ever get here) */
    }
}

static ExprNode *parse_unary(const Token *tokens, int ntokens, int *idx)
{
    TokenType tt = peek_type(tokens, ntokens, *idx);
    if (tt == TOK_MINUS) {
        (*idx)++;
        ExprNode *operand = parse_unary(tokens, ntokens, idx);
        if (!operand) return NULL;
        ExprNode *n = node_new(EX_NEG);
        n->lhs = operand;
        return n;
    }
    if (tt == TOK_PLUS) {
        (*idx)++; /* unary plus: no-op, just parse through */
        return parse_unary(tokens, ntokens, idx);
    }
    return parse_primary(tokens, ntokens, idx);
}

static ExprNode *parse_term(const Token *tokens, int ntokens, int *idx)
{
    ExprNode *lhs = parse_unary(tokens, ntokens, idx);
    if (!lhs) return NULL;

    for (;;) {
        TokenType tt = peek_type(tokens, ntokens, *idx);
        if (tt != TOK_STAR && tt != TOK_SLASH)
            break;
        (*idx)++;
        ExprNode *rhs = parse_unary(tokens, ntokens, idx);
        if (!rhs) { expr_free(lhs); return NULL; }
        ExprNode *n = node_new(tt == TOK_STAR ? EX_MUL : EX_DIV);
        n->lhs = lhs;
        n->rhs = rhs;
        lhs = n;
    }
    return lhs;
}

ExprNode *expr_parse(const Token *tokens, int ntokens, int *idx)
{
    int save = *idx;
    ExprNode *lhs = parse_term(tokens, ntokens, idx);
    if (!lhs) { *idx = save; return NULL; }

    for (;;) {
        TokenType tt = peek_type(tokens, ntokens, *idx);
        if (tt != TOK_PLUS && tt != TOK_MINUS)
            break;
        (*idx)++;
        ExprNode *rhs = parse_term(tokens, ntokens, idx);
        if (!rhs) { expr_free(lhs); *idx = save; return NULL; }
        ExprNode *n = node_new(tt == TOK_PLUS ? EX_ADD : EX_SUB);
        n->lhs = lhs;
        n->rhs = rhs;
        lhs = n;
    }
    return lhs;
}

void expr_free(ExprNode *e)
{
    if (!e) return;
    expr_free(e->lhs);
    expr_free(e->rhs);
    free(e);
}

static void sb_append(char *buf, size_t size, size_t *len, const char *s, size_t n)
{
    if (*len >= size) return;
    size_t room = size - *len - 1; /* leave room for NUL */
    if (n > room) n = room;
    memcpy(buf + *len, s, n);
    *len += n;
    buf[*len] = '\0';
}

static void expr_to_string_r(const ExprNode *e, char *buf, size_t size, size_t *len)
{
    char tmp[32];
    if (!e) { sb_append(buf, size, len, "?", 1); return; }
    switch (e->op) {
        case EX_NUM: {
            int n = snprintf(tmp, sizeof(tmp), "%ld", e->num);
            sb_append(buf, size, len, tmp, (size_t)n);
            return;
        }
        case EX_SYM:
            sb_append(buf, size, len, e->sym, e->sym_len);
            return;
        case EX_LOCCTR:
            sb_append(buf, size, len, ".", 1);
            return;
        case EX_LOCALREF: {
            int n = snprintf(tmp, sizeof(tmp), "%d%c", e->local_digit, e->local_dir);
            sb_append(buf, size, len, tmp, (size_t)n);
            return;
        }
        case EX_NEG:
            sb_append(buf, size, len, "-", 1);
            expr_to_string_r(e->lhs, buf, size, len);
            return;
        case EX_ADD: case EX_SUB: case EX_MUL: case EX_DIV: {
            const char *op = e->op == EX_ADD ? "+" : e->op == EX_SUB ? "-" :
                              e->op == EX_MUL ? "*" : "/";
            sb_append(buf, size, len, "(", 1);
            expr_to_string_r(e->lhs, buf, size, len);
            sb_append(buf, size, len, op, 1);
            expr_to_string_r(e->rhs, buf, size, len);
            sb_append(buf, size, len, ")", 1);
            return;
        }
    }
}

void expr_to_string(const ExprNode *e, char *buf, size_t size)
{
    size_t len = 0;
    if (size) buf[0] = '\0';
    expr_to_string_r(e, buf, size, &len);
}

bool expr_try_eval_const(const ExprNode *e, long *out)
{
    if (!e) return false;
    switch (e->op) {
        case EX_NUM:
            *out = e->num;
            return true;
        case EX_SYM:
        case EX_LOCCTR:
        case EX_LOCALREF:
            return false; /* needs a symbol table / address assignment (later step) */
        case EX_NEG: {
            long v;
            if (!expr_try_eval_const(e->lhs, &v)) return false;
            *out = -v;
            return true;
        }
        case EX_ADD: case EX_SUB: case EX_MUL: case EX_DIV: {
            long a, b;
            if (!expr_try_eval_const(e->lhs, &a)) return false;
            if (!expr_try_eval_const(e->rhs, &b)) return false;
            switch (e->op) {
                case EX_ADD: *out = a + b; return true;
                case EX_SUB: *out = a - b; return true;
                case EX_MUL: *out = a * b; return true;
                case EX_DIV:
                    if (b == 0) return false;
                    *out = a / b;
                    return true;
                default: return false;
            }
        }
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Register table                                                       */
/* ------------------------------------------------------------------ */

typedef struct { const char *name; RegClass cls; } RegEntry;

static const RegEntry REG_TABLE[] = {
    { "ax", REG_WORD }, { "cx", REG_WORD }, { "dx", REG_WORD }, { "bx", REG_WORD },
    { "sp", REG_WORD }, { "bp", REG_WORD }, { "si", REG_WORD }, { "di", REG_WORD },
    { "al", REG_BYTE }, { "cl", REG_BYTE }, { "dl", REG_BYTE }, { "bl", REG_BYTE },
    { "ah", REG_BYTE }, { "ch", REG_BYTE }, { "dh", REG_BYTE }, { "bh", REG_BYTE },
    { "cs", REG_SEGMENT }, { "ds", REG_SEGMENT }, { "es", REG_SEGMENT }, { "ss", REG_SEGMENT },
};
#define REG_TABLE_N (sizeof(REG_TABLE) / sizeof(REG_TABLE[0]))

RegClass reg_lookup(const char *text, size_t len)
{
    for (size_t i = 0; i < REG_TABLE_N; i++) {
        if (strlen(REG_TABLE[i].name) == len && strncmp(REG_TABLE[i].name, text, len) == 0)
            return REG_TABLE[i].cls;
    }
    return REG_NONE;
}

/* ------------------------------------------------------------------ */
/* Operand classification                                               */
/* ------------------------------------------------------------------ */

ParsedOperand classify_operand(const Operand *op)
{
    ParsedOperand po;
    memset(&po, 0, sizeof(po));

    int n = op->ntokens;
    if (n == 0) {
        po.ok = false;
        return po;
    }

    /* "@reg" - register-indirect jump/call target (confirmed real
     * usage: "call @si", "jmp @bx" in mch.s - distinct notation from
     * the "(reg)" data-indirect form, same underlying addressing mode
     * (FF /2 or FF /4 with mod=11, register-direct ModRM - no memory
     * dereference the way "(reg)" implies for data access). */
    if (op->tokens[0].type == TOK_AT) {
        if (n == 2 && op->tokens[1].type == TOK_IDENT) {
            RegClass rc = reg_lookup(op->tokens[1].text, op->tokens[1].len);
            po.mode = ADDR_INDIRECT;
            po.reg = op->tokens[1];
            po.reg_class = rc;
            po.expr = NULL;
            po.ok = (rc == REG_WORD);
            return po;
        }

        /* "@<disp-expr>(reg)" - indirect jump/call THROUGH a computed
         * memory address (the WORD STORED at disp+reg is the real
         * target, e.g. a dispatch/jump table lookup) - CONFIRMED real
         * via tty.s "call @8.+_cdevsw(bx)" and "br @L10001(bx)". Same
         * displacement/ModRM machinery as any other ADDR_INDIRECT
         * memory operand below - only the leading '@' differs
         * syntactically, and carries no separate encoding meaning
         * beyond "this is genuinely indirect" (which ADDR_INDIRECT
         * already conveys on its own for jmp/call). A '*'/'#' size
         * marker can additionally appear right after '@', before the
         * displacement - CONFIRMED real via tty.s "call @*12.(di)" -
         * skipped here exactly like the general indirect-operand case
         * below skips it before its own displacement, since a
         * computed jump/call target is always a word address
         * regardless of the marker. */
        if (n >= 4 &&
            op->tokens[n - 3].type == TOK_LPAREN &&
            op->tokens[n - 2].type == TOK_IDENT &&
            op->tokens[n - 1].type == TOK_RPAREN) {
            RegClass rc = reg_lookup(op->tokens[n - 2].text, op->tokens[n - 2].len);
            po.mode = ADDR_INDIRECT;
            po.reg = op->tokens[n - 2];
            po.reg_class = rc;
            int disp_end = n - 3; /* exclusive */
            int idx = 1; /* skip '@' */
            if (idx < disp_end && (op->tokens[idx].type == TOK_STAR || op->tokens[idx].type == TOK_HASH))
                idx++; /* skip an optional size marker too */
            if (disp_end > idx) {
                po.expr = expr_parse(op->tokens, disp_end, &idx);
                po.ok = (po.expr != NULL) && (idx == disp_end) && (rc == REG_WORD);
            } else {
                po.expr = NULL; /* e.g. "@(bx)" - no displacement */
                po.ok = (rc == REG_WORD);
            }
            return po;
        }

        po.ok = false;
        return po;
    }

    int start = 0;
    SizeMarker marker = SZ_NONE;
    if (op->tokens[0].type == TOK_STAR) { marker = SZ_BYTE; start = 1; }
    else if (op->tokens[0].type == TOK_HASH) { marker = SZ_WORD; start = 1; }

    /* Trailing "(base)(index)" => base+index indirect addressing, e.g.
     * "*20.+2(bx)(di)" - confirmed real via kernel_opt/subr.s. Only
     * the four combinations the 8086 ModRM table actually encodes are
     * valid: (bx)(si) (bx)(di) (bp)(si) (bp)(di) - checked by
     * base_index_rm() in encode.c; everything else is a
     * classification failure. Must be checked BEFORE the single-
     * register "(reg)" form below, since it also ends in "...(ident)"
     * and would otherwise false-match on just the trailing "(di)" pair. */
    if (n >= start + 6 &&
        op->tokens[n - 6].type == TOK_LPAREN &&
        op->tokens[n - 5].type == TOK_IDENT &&
        op->tokens[n - 4].type == TOK_RPAREN &&
        op->tokens[n - 3].type == TOK_LPAREN &&
        op->tokens[n - 2].type == TOK_IDENT &&
        op->tokens[n - 1].type == TOK_RPAREN) {

        RegClass rc1 = reg_lookup(op->tokens[n - 5].text, op->tokens[n - 5].len);
        RegClass rc2 = reg_lookup(op->tokens[n - 2].text, op->tokens[n - 2].len);

        po.mode = ADDR_INDIRECT;
        po.size = marker;
        po.reg = op->tokens[n - 5];
        po.reg_class = rc1;
        po.has_index = true;
        po.reg2 = op->tokens[n - 2];
        po.reg2_class = rc2;

        int disp_end = n - 6; /* exclusive */
        if (disp_end > start) {
            int idx = start;
            po.expr = expr_parse(op->tokens, disp_end, &idx);
            po.ok = (po.expr != NULL) && (idx == disp_end) && (rc1 == REG_WORD) && (rc2 == REG_WORD);
        } else {
            po.expr = NULL;
            po.ok = (rc1 == REG_WORD) && (rc2 == REG_WORD);
        }
        return po;
    }

    /* Trailing "(reg)" => indirect addressing, with everything from
     * `start` up to the '(' being an optional displacement expression. */
    if (n >= start + 3 &&
        op->tokens[n - 3].type == TOK_LPAREN &&
        op->tokens[n - 2].type == TOK_IDENT &&
        op->tokens[n - 1].type == TOK_RPAREN) {

        RegClass rc = reg_lookup(op->tokens[n - 2].text, op->tokens[n - 2].len);

        po.mode = ADDR_INDIRECT;
        po.size = marker;
        po.reg = op->tokens[n - 2];
        po.reg_class = rc;

        int disp_end = n - 3; /* exclusive */
        if (disp_end > start) {
            int idx = start;
            po.expr = expr_parse(op->tokens, disp_end, &idx);
            po.ok = (po.expr != NULL) && (idx == disp_end) && (rc == REG_WORD);
        } else {
            po.expr = NULL; /* e.g. "(bp)" - no displacement */
            po.ok = (rc == REG_WORD);
        }
        return po;
    }

    if (marker != SZ_NONE) {
        po.mode = ADDR_IMMEDIATE;
        po.size = marker;
        int idx = start;
        po.expr = expr_parse(op->tokens, n, &idx);
        po.ok = (po.expr != NULL) && (idx == n);
        return po;
    }

    if (n == 1 && op->tokens[0].type == TOK_IDENT) {
        RegClass rc = reg_lookup(op->tokens[0].text, op->tokens[0].len);
        if (rc != REG_NONE) {
            po.mode = ADDR_REGISTER;
            po.reg = op->tokens[0];
            po.reg_class = rc;
            po.ok = true;
            return po;
        }
    }

    /* Fall through: bare expression (direct address, jump target,
     * symbol arithmetic like "monssvec+2", location-counter arithmetic
     * like ". - _icode", etc). */
    po.mode = ADDR_DIRECT;
    {
        int idx = 0;
        po.expr = expr_parse(op->tokens, n, &idx);
        po.ok = (po.expr != NULL) && (idx == n);
    }
    return po;
}

void parsed_operand_free(ParsedOperand *po)
{
    if (!po) return;
    expr_free(po->expr);
    po->expr = NULL;
}
