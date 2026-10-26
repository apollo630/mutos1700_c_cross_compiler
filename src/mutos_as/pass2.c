/*
 * pass2.c - see pass2.h for the overall design.
 */

#include <stdint.h>
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

/* One prefix operator at most - "--5", "~~5" and "-~5" are errors in the
 * original - and then a primary. */
static ExprNode *parse_unary(const Token *tokens, int ntokens, int *idx)
{
    TokenType tt = peek_type(tokens, ntokens, *idx);
    if (tt == TOK_MINUS || tt == TOK_TILDE) {
        (*idx)++;
        ExprNode *operand = parse_primary(tokens, ntokens, idx);
        if (!operand) return NULL;
        ExprNode *n = node_new(tt == TOK_MINUS ? EX_NEG : EX_NOT);
        n->lhs = operand;
        return n;
    }
    if (tt == TOK_PLUS) {
        (*idx)++; /* unary plus: no-op, just parse through */
        return parse_primary(tokens, ntokens, idx);
    }
    return parse_primary(tokens, ntokens, idx);
}

static bool binary_op(TokenType tt, ExprOp *op)
{
    switch (tt) {
        case TOK_PLUS:  *op = EX_ADD; return true;
        case TOK_MINUS: *op = EX_SUB; return true;
        case TOK_STAR:  *op = EX_MUL; return true;
        case TOK_SLASH: *op = EX_DIV; return true;
        case TOK_AMP:   *op = EX_AND; return true;
        case TOK_BANG:  *op = EX_OR;  return true;
        case TOK_CARET: *op = EX_MOD; return true;
        case TOK_SHL:   *op = EX_SHL; return true;
        case TOK_SHR:   *op = EX_SHR; return true;
        default: return false;
    }
}

/* The original has no precedence: every binary operator groups from the
 * left, "2+3*4" is 20 (measured 2026-10-25). Parentheses are not part of
 * expressions at all - they mark an address register. */
ExprNode *expr_parse(const Token *tokens, int ntokens, int *idx)
{
    int save = *idx;
    ExprNode *lhs = parse_unary(tokens, ntokens, idx);
    if (!lhs) { *idx = save; return NULL; }

    for (;;) {
        ExprOp op;
        TokenType tt = peek_type(tokens, ntokens, *idx);
        if (!binary_op(tt, &op))
            break;
        (*idx)++;
        ExprNode *rhs = parse_unary(tokens, ntokens, idx);
        if (!rhs) { expr_free(lhs); *idx = save; return NULL; }
        ExprNode *n = node_new(op);
        n->lhs = lhs;
        n->rhs = rhs;
        lhs = n;
    }
    return lhs;
}

int expr_div_errors = 0;

static long wrap16(long v)
{
    return (long)(int16_t)(uint16_t)((unsigned long)v & 0xFFFF);
}

long expr_apply_not(long a)
{
    return wrap16(~wrap16(a));
}

long expr_apply(ExprOp op, long a, long b, bool *ok)
{
    if (ok) *ok = true;
    switch (op) {
        case EX_ADD: return a + b;
        case EX_SUB: return a - b;
        default: break;
    }
    a = wrap16(a);
    b = wrap16(b);
    switch (op) {
        case EX_MUL: return wrap16(a * b);
        case EX_DIV:
        case EX_MOD:
            if (b == 0 || (a == -32768 && b == -1)) {
                if (ok) *ok = false;
                return 0;
            }
            return wrap16(op == EX_DIV ? a / b : a % b);
        case EX_AND: return wrap16(a & b);
        case EX_OR:  return wrap16(a | b);
        case EX_SHL: {
            unsigned c = (unsigned)(b & 0xFFFF);
            return c >= 16 ? 0 : wrap16(a * (1L << c));
        }
        case EX_SHR: {
            unsigned c = (unsigned)(b & 0xFFFF);
            return c >= 16 ? (a < 0 ? -1 : 0) : wrap16(a >> c);
        }
        default: break;
    }
    if (ok) *ok = false;
    return 0;
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
        case EX_NOT:
            sb_append(buf, size, len, "~", 1);
            expr_to_string_r(e->lhs, buf, size, len);
            return;
        default: {
            const char *op =
                e->op == EX_ADD ? "+" : e->op == EX_SUB ? "-" :
                e->op == EX_MUL ? "*" : e->op == EX_DIV ? "/" :
                e->op == EX_AND ? "&" : e->op == EX_OR ? "!" :
                e->op == EX_MOD ? "^" : e->op == EX_SHL ? "<<" : ">>";
            sb_append(buf, size, len, "(", 1);
            expr_to_string_r(e->lhs, buf, size, len);
            sb_append(buf, size, len, op, strlen(op));
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
        case EX_NOT: {
            long v;
            if (!expr_try_eval_const(e->lhs, &v)) return false;
            *out = expr_apply_not(v);
            return true;
        }
        default: {
            long a, b;
            bool ok;
            if (!expr_try_eval_const(e->lhs, &a)) return false;
            if (!expr_try_eval_const(e->rhs, &b)) return false;
            *out = expr_apply(e->op, a, b, &ok);
            return ok;
        }
    }
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
        po.at = true;
        if (n == 1) { po.ok = false; return po; }

        /* "@reg" - the register itself (FF mod=11). */
        if (n == 2 && op->tokens[1].type == TOK_IDENT) {
            RegClass rc = reg_lookup(op->tokens[1].text, op->tokens[1].len);
            if (rc != REG_NONE) {
                po.mode = ADDR_INDIRECT;
                po.reg = op->tokens[1];
                po.reg_class = rc;
                po.at_reg = true;
                po.ok = (rc == REG_WORD || rc == REG_BYTE);
                return po;
            }
        }

        /* "@<disp-expr>(reg)", "@(reg)", "@<disp-expr>(reg)(reg)" -
         * indirect THROUGH a computed memory address (the WORD STORED
         * there is the real target, e.g. a dispatch/jump table lookup) -
         * CONFIRMED real via tty.s "call @8.+_cdevsw(bx)" and "br
         * @L10001(bx)". The same displacement/ModRM machinery as any
         * other ADDR_INDIRECT memory operand below. A '*'/'#' size
         * marker can additionally appear right after '@', before the
         * displacement - CONFIRMED real via tty.s "call @*12.(di)" -
         * skipped here exactly like the general indirect-operand case
         * below skips it before its own displacement, since a
         * computed jump/call target is always a word address
         * regardless of the marker. "@(bx)" with no displacement is the
         * word at [bx], NOT the register (found by asfuzz.py,
         * 2026-10-25: FF 17, where "@bx" is FF D3). */
        bool pair = n >= 7 &&
            op->tokens[n - 6].type == TOK_LPAREN && op->tokens[n - 5].type == TOK_IDENT &&
            op->tokens[n - 4].type == TOK_RPAREN && op->tokens[n - 3].type == TOK_LPAREN &&
            op->tokens[n - 2].type == TOK_IDENT && op->tokens[n - 1].type == TOK_RPAREN;
        bool single = n >= 4 &&
            op->tokens[n - 3].type == TOK_LPAREN && op->tokens[n - 2].type == TOK_IDENT &&
            op->tokens[n - 1].type == TOK_RPAREN;
        if (pair || single) {
            int r1 = pair ? n - 5 : n - 2;
            RegClass rc = reg_lookup(op->tokens[r1].text, op->tokens[r1].len);
            po.mode = ADDR_INDIRECT;
            po.reg = op->tokens[r1];
            po.reg_class = rc;
            bool good = (rc == REG_WORD);
            if (pair) {
                RegClass rc2 = reg_lookup(op->tokens[n - 2].text, op->tokens[n - 2].len);
                po.has_index = true;
                po.reg2 = op->tokens[n - 2];
                po.reg2_class = rc2;
                good = good && (rc2 == REG_WORD);
            }
            int disp_end = pair ? n - 6 : n - 3; /* exclusive */
            int idx = 1; /* skip '@' */
            if (idx < disp_end && (op->tokens[idx].type == TOK_STAR || op->tokens[idx].type == TOK_HASH))
                idx++; /* skip an optional size marker too */
            if (disp_end > idx) {
                po.expr = expr_parse(op->tokens, disp_end, &idx);
                po.ok = good && (po.expr != NULL) && (idx == disp_end);
            } else {
                po.expr = NULL; /* "@(bx)" - no displacement */
                po.ok = good;
            }
            return po;
        }

        /* "@expr" - the word at that address (found by asfuzz.py,
         * 2026-10-25: FF /2 or /4 with mod=00 rm=110 and a relocated
         * disp16). No size marker here: the original refuses "@*x". */
        {
            int idx = 1;
            po.mode = ADDR_INDIRECT;
            po.at_direct = true;
            po.reg_class = REG_NONE;
            po.expr = expr_parse(op->tokens, n, &idx);
            po.ok = (po.expr != NULL) && (idx == n);
            return po;
        }
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
