/*
 * c0_parser.c - mutos_c0 front-end driver.
 *
 * Current grammar coverage (deliberately narrow - see
 * src/mutos_cc/README.md for the expansion plan):
 *
 *   translation-unit  := extdef*
 *   extdef            := IDENT '(' ')' compound-stmt
 *   compound-stmt     := '{' decl* stmt* '}'
 *   decl              := 'int' declarator (',' declarator)* ';'
 *   declarator        := '*' IDENT | IDENT ('[' ICON ']')?
 *   stmt              := assign-stmt | star-assign-stmt | return-stmt
 *   assign-stmt       := IDENT assign-op expr ';'
 *   assign-op         := '=' | '+=' | '-=' | '*=' | '/=' | '%='
 *                       | '<<=' | '>>=' | '&=' | '|=' | '^='
 *   star-assign-stmt  := '*' ('++'|'--')? IDENT ('++'|'--')? '=' expr ';'
 *   return-stmt       := 'return' expr? ';'
 *   expr              := LOGOR
 *   LOGOR             := LOGAND ('||' LOGAND)*
 *   LOGAND            := BITOR ('&&' BITOR)*
 *   BITOR             := BITXOR ('|' BITXOR)*
 *   BITXOR            := BITAND ('^' BITAND)*
 *   BITAND            := EQUALITY ('&' EQUALITY)*
 *   EQUALITY          := RELATIONAL (('=='|'!=') RELATIONAL)*
 *   RELATIONAL        := SHIFT (('<'|'<='|'>'|'>=') SHIFT)*
 *   SHIFT             := ADD (('<<'|'>>') ADD)*
 *   ADD               := MUL (('+'|'-') MUL)*
 *   MUL               := UNARY (('*'|'/'|'%') UNARY)*
 *   UNARY             := ('-'|'+'|'~'|'!') UNARY | ('++'|'--') IDENT | POSTFIX
 *   POSTFIX           := PRIMARY ('++'|'--')?
 *   PRIMARY           := ICON | IDENT | '(' expr ')'
 *
 * i.e. every declared local is 'int', 'int *' (one pointer degree) or
 * 'int' '[' N ']' (one array dimension), with no initializer; every
 * statement is a single-variable assignment (with '=' or any of the
 * ten compound-assignment operators - parse_assign_stmt() handles
 * all eleven identically, emitting the real ASPLUS/ASMINUS/ASTIMES/
 * ASDIV/ASMOD/ASLSH/ASRSH/ASSAND/ASOR/ASXOR opcode in place of
 * ASSIGN), a dereferenced-pointer
 * assignment (star-assign-stmt - '*<ptr-expr> = expr;', <ptr-expr>
 * being a plain pointer variable with an optional leading/trailing
 * '++'/'--'), or a 'return'; every function still takes no
 * parameters. A bare array name used as an rvalue decays to a
 * pointer (see parse_primary()'s SymEntry.is_array handling); '++'/
 * '--' on a pointer operand is scaled by MCC_SZINT (see
 * emit_incdec()). This is
 * exactly tests/mutos_cc/00_smoke's three programs plus
 * tests/mutos_cc/01_expr/01_intarith.c, 02_bitwise.c, 03_rellogic.c,
 * 04_shift.c, 05_incdec.c and 06_compasgn.c. Array
 * subscripting, and multi-level pointers/multi-dimensional arrays are
 * not yet part of this chain - see src/mutos_cc/README.md.
 *
 * Expression handling is still "emit as you parse" (no explicit
 * struct tnode tree - see the note in the previous revision of this
 * file/README.md), extended with the ExprVal representation below:
 * a parsed (sub)expression is EITHER a still-unmaterialized
 * compile-time constant (nothing emitted yet - matches real K&R
 * cc's build()-time constant folding: "return 6 * 7;" still reaches
 * temp1 as a single folded CON(42), never a TIMES opcode) OR has
 * already been fully emitted as a real tree (a NAME leaf, or an
 * operator node over already-emitted/just-materialized operands).
 * A binary operator combinator folds two constants directly (no
 * emission) but, the moment either side is non-constant, first
 * materializes any constant operand as a genuine CON leaf and then
 * emits the operator node - reproducing real cc's per-operation
 * (not whole-expression) folding decision. A postfix/prefix '++'/
 * '--' operand and a bare array name used as an rvalue are never
 * treated as compile-time constants - both always return ev_dynamic()
 * after emitting real code (see emit_incdec()'s and the array-decay
 * comment in parse_primary()).
 *
 * The exact opcode sequence emitted by cfunc()/doret() and the new
 * declaration/assignment/NAME-reference handling below is
 * reverse-engineered byte-for-byte from the real-hardware
 * tests/mutos_cc/00_smoke/, tests/mutos_cc/01_expr/01_intarith and
 * tests/mutos_cc/01_expr/05_incdec
 * ".1.golden" files against v7/cc/c02.c's/c03.c's/c04.c's algorithm
 * shape (per CLAUDE.md Workflow Guideline 3, v7 is used as an
 * algorithmic reference only - the confirmed MUTOS-specific deltas
 * from that reference are documented in mutos_cc.h and in
 * docs/DEVLOG.md).
 */

#include <stdint.h>
#include <string.h>

#include "mutos_cc.h"
#include "c0_lex.h"
#include "c0_diag.h"
#include "c0_outcode.h"
#include "c0_sym.h"
#include "c0_parser.h"

typedef struct {
    Lexer  lx;
    Token  cur;
    int    isn;    /* next free intermediate-code label number - v7/cc/
                     * c00.c's global `isn`, initialized to 1 per
                     * translation unit. */
    SymTab syms;    /* current function's local (AUTO) variables -
                      * reset at the start of each cfunc(). */
} Parser;

/* One pointer-to-int degree, matching mutos_cc.h's XTYPE bit layout
 * (base type in the low 3 bits, degree in bits 3-4) - confirmed
 * against 05_incdec.1.golden's NAME(p)/ASSIGN(p=a) nodes, which both
 * use type value 8 (TY_INT | 010). Only pointer-to-int is supported
 * in this grammar scope - see src/mutos_cc/README.md. */
#define TY_PTR_INT (TY_INT | 010)

/* SZINT - the size (bytes) of a single int/pointer, and the scale
 * factor pointer arithmetic on an "int *" steps by. Matches
 * v7/cc/c0.h's SZINT; this grammar scope has no other element size. */
#define MCC_SZINT 2

static void advance(Parser *p)
{
    p->cur = lex_next(&p->lx);
}

/* Consumes the current token if it matches `k`; otherwise reports an
 * error and leaves the token stream positioned where it was (so a
 * caller can attempt to resynchronize). Returns 1 on match. */
static int expect(Parser *p, TokKind k, const char *what)
{
    if (p->cur.kind != k) {
        c0_error_at(p->cur.line, "syntax error: expected %s, found %s",
                     what, tok_kind_name(p->cur.kind));
        return 0;
    }
    advance(p);
    return 1;
}

static void branch_op(FILE *t1, int lab) { outcode(t1, "BN", OP_BRANCH, lab); }
static void label_op(FILE *t1, int lab)  { outcode(t1, "BN", OP_LABEL, lab); }

/* Truncates a host `long` to the target's 16-bit signed `int` range,
 * matching K&R int arithmetic on the real 16-bit MUTOS 1700 target
 * (silent wraparound on overflow - not UB here since we go through
 * an unsigned intermediate). */
static long trunc16(long v)
{
    return (int16_t)(uint16_t)(v & 0xFFFFL);
}

/* ------------------------------------------------------------------ */
/* Expression values: either an unmaterialized compile-time constant,
 * or a marker that this (sub)expression has already been fully
 * emitted (as a NAME leaf or an operator node) - see file header
 * comment. */

typedef struct {
    int  is_const;
    long value;   /* valid iff is_const */
} ExprVal;

static ExprVal ev_const(long v)  { ExprVal e; e.is_const = 1; e.value = v; return e; }
static ExprVal ev_dynamic(void)  { ExprVal e; e.is_const = 0; e.value = 0; return e; }

/* If `v` is still an unmaterialized constant, emits it now as a real
 * CON leaf (treeout()'s CON case: outcode("BNN", CON, type, value)) -
 * called whenever a constant is about to be combined with a
 * non-constant sibling and so can no longer stay folded away. A
 * no-op if `v` was already emitted (is_const == 0). */
static void emit_materialize(FILE *t1, ExprVal v)
{
    if (v.is_const)
        outcode(t1, "BNN", OP_CON, TY_INT, (int)trunc16(v.value));
}

/* Emits the CON/ITOP scaling sequence plus the final INCBEF/DECBEF/
 * INCAFT/DECAFT node itself, for an lvalue whose NAME has already
 * been emitted by the caller. `optag` is one of OP_INCBEF/OP_DECBEF/
 * OP_INCAFT/OP_DECAFT; `type` is the lvalue's own type (TY_INT or
 * TY_PTR_INT); `is_ptr` selects the extra CON(MCC_SZINT)+ITOP(type)
 * pair that scales the literal "1" up to a real byte count for
 * pointer arithmetic. Confirmed byte-for-byte against
 * 05_incdec.1.golden: a plain int gets "CON(1), <op>"; a pointer gets
 * "CON(1), CON(2), ITOP(type), <op>" (c1 folds the CON(1)*CON(2)
 * subtree into the real "*2." immediate it emits - see
 * src/mutos_cc/README.md). */
static void emit_incdec(FILE *t1, int optag, int type, int is_ptr)
{
    outcode(t1, "BNN", OP_CON, TY_INT, 1);
    if (is_ptr) {
        outcode(t1, "BNN", OP_CON, TY_INT, MCC_SZINT);
        outcode(t1, "BN", OP_ITOP, type);
    }
    outcode(t1, "BN", optag, type);
}

static ExprVal parse_expr(Parser *p, FILE *t1);

static ExprVal parse_primary(Parser *p, FILE *t1)
{
    if (p->cur.kind == T_ICON) {
        long v = trunc16(p->cur.ival);
        advance(p);
        return ev_const(v);
    }
    if (p->cur.kind == T_IDENT) {
        SymEntry *sym = symtab_lookup(&p->syms, p->cur.ident);
        if (!sym) {
            c0_error_at(p->cur.line, "'%s' undeclared", p->cur.ident);
            advance(p);
            return ev_const(0);
        }
        advance(p);
        /* treeout()'s NAME case: outcode("BNN", NAME, hclass, type)
         * then, since hclass is always SC_AUTO (never SC_EXTERN) in
         * this scope, outcode("N", hoffset) rather than a symbol
         * name - merged into one "BNNN" call here since the byte
         * output is identical either way. */

        if (sym->is_array) {
            /* Array-name-as-rvalue decay ("p = a;"): the NAME node
             * itself still uses the array's base element type/offset
             * (the array's first element), followed by AMPER to take
             * its address - confirmed against 05_incdec.s.golden's
             * "lea di,*-16.(bp)". An array is never a modifiable
             * lvalue, so no postfix '++'/'--' check follows. */
            outcode(t1, "BNNN", OP_NAME, sym->hclass, TY_INT, sym->offset);
            outcode(t1, "BN", OP_AMPER, TY_PTR_INT);
            return ev_dynamic();
        }

        outcode(t1, "BNNN", OP_NAME, sym->hclass, sym->type, sym->offset);

        if (p->cur.kind == T_INCR || p->cur.kind == T_DECR) {
            /* Postfix '++'/'--' - INCAFT/DECAFT. See emit_incdec()'s
             * comment for the CON/ITOP scaling shape; confirmed
             * against 05_incdec.1.golden's "j = i++;" (plain int) and
             * "*p++ = 1;" (pointer) trees. */
            int optag = (p->cur.kind == T_INCR) ? OP_INCAFT : OP_DECAFT;
            advance(p);
            emit_incdec(t1, optag, sym->type, sym->is_ptr);
        }
        return ev_dynamic();
    }
    if (p->cur.kind == T_LPAREN) {
        advance(p);
        ExprVal v = parse_expr(p, t1);
        expect(p, T_RPAREN, "')'");
        return v;
    }
    c0_error_at(p->cur.line, "expected an expression, found %s",
                 tok_kind_name(p->cur.kind));
    if (p->cur.kind != T_EOF)
        advance(p); /* avoid looping forever on a bad token */
    return ev_const(0);
}

static ExprVal parse_unary(Parser *p, FILE *t1)
{
    if (p->cur.kind == T_INCR || p->cur.kind == T_DECR) {
        /* Prefix '++'/'--' - INCBEF/DECBEF, only on a plain variable
         * name so far (matching this grammar scope's only confirmed
         * use - 05_incdec.c's "++i"/"--i"/"++p"). */
        int optag = (p->cur.kind == T_INCR) ? OP_INCBEF : OP_DECBEF;
        int line = p->cur.line;
        advance(p);
        if (p->cur.kind != T_IDENT) {
            c0_error_at(line, "prefix '++'/'--' is only supported on a "
                               "plain variable name so far - see "
                               "src/mutos_cc/README.md");
            return ev_dynamic();
        }
        SymEntry *sym = symtab_lookup(&p->syms, p->cur.ident);
        if (!sym) {
            c0_error_at(p->cur.line, "'%s' undeclared", p->cur.ident);
            advance(p);
            return ev_const(0);
        }
        if (sym->is_array) {
            c0_error_at(line, "prefix '++'/'--' on an array is not "
                               "supported (an array is not a "
                               "modifiable lvalue)");
            advance(p);
            return ev_dynamic();
        }
        advance(p);
        outcode(t1, "BNNN", OP_NAME, sym->hclass, sym->type, sym->offset);
        emit_incdec(t1, optag, sym->type, sym->is_ptr);
        return ev_dynamic();
    }
    if (p->cur.kind == T_MINUS) {
        int line = p->cur.line;
        advance(p);
        ExprVal v = parse_unary(p, t1);
        if (v.is_const)
            return ev_const(trunc16(-v.value));
        c0_error_at(line, "unary '-' on a non-constant operand is not "
                           "yet supported - see src/mutos_cc/README.md");
        return ev_dynamic();
    }
    if (p->cur.kind == T_PLUS) {
        advance(p);
        return parse_unary(p, t1);
    }
    if (p->cur.kind == T_TILDE) {
        int line = p->cur.line;
        advance(p);
        ExprVal v = parse_unary(p, t1);
        if (v.is_const)
            return ev_const(trunc16(~v.value));
        (void)line;
        emit_materialize(t1, v); /* no-op: a non-constant operand has
                                   * already been emitted (e.g. as a
                                   * NAME) by the time we get here. */
        outcode(t1, "BN", OP_COMPL, TY_INT);
        return ev_dynamic();
    }
    if (p->cur.kind == T_BANG) {
        int line = p->cur.line;
        advance(p);
        ExprVal v = parse_unary(p, t1);
        if (v.is_const)
            return ev_const(v.value == 0 ? 1 : 0);
        (void)line;
        emit_materialize(t1, v); /* no-op, same as COMPL above */
        outcode(t1, "BN", OP_EXCLA, TY_INT);
        return ev_dynamic();
    }
    return parse_primary(p, t1);
}

static ExprVal parse_mul(Parser *p, FILE *t1)
{
    ExprVal v = parse_unary(p, t1);
    for (;;) {
        if (p->cur.kind == T_STAR) {
            advance(p);
            ExprVal r = parse_unary(p, t1);
            if (v.is_const && r.is_const) {
                v = ev_const(trunc16(v.value * r.value));
                continue;
            }
            emit_materialize(t1, v);
            emit_materialize(t1, r);
            outcode(t1, "BN", OP_TIMES, TY_INT);
            v = ev_dynamic();
        } else if (p->cur.kind == T_SLASH) {
            int line = p->cur.line;
            advance(p);
            ExprVal r = parse_unary(p, t1);
            if (v.is_const && r.is_const) {
                if (r.value == 0) {
                    c0_error_at(line, "Division by zero in constant expression");
                    v = ev_const(0);
                } else {
                    v = ev_const(trunc16(v.value / r.value));
                }
                continue;
            }
            emit_materialize(t1, v);
            emit_materialize(t1, r);
            outcode(t1, "BN", OP_DIVIDE, TY_INT);
            v = ev_dynamic();
        } else if (p->cur.kind == T_PERCENT) {
            int line = p->cur.line;
            advance(p);
            ExprVal r = parse_unary(p, t1);
            if (v.is_const && r.is_const) {
                if (r.value == 0) {
                    c0_error_at(line, "Division by zero in constant expression");
                    v = ev_const(0);
                } else {
                    v = ev_const(trunc16(v.value % r.value));
                }
                continue;
            }
            emit_materialize(t1, v);
            emit_materialize(t1, r);
            outcode(t1, "BN", OP_MOD, TY_INT);
            v = ev_dynamic();
        } else {
            break;
        }
    }
    return v;
}

static ExprVal parse_add(Parser *p, FILE *t1)
{
    ExprVal v = parse_mul(p, t1);
    for (;;) {
        if (p->cur.kind == T_PLUS) {
            advance(p);
            ExprVal r = parse_mul(p, t1);
            if (v.is_const && r.is_const) {
                v = ev_const(trunc16(v.value + r.value));
                continue;
            }
            emit_materialize(t1, v);
            emit_materialize(t1, r);
            outcode(t1, "BN", OP_PLUS, TY_INT);
            v = ev_dynamic();
        } else if (p->cur.kind == T_MINUS) {
            advance(p);
            ExprVal r = parse_mul(p, t1);
            if (v.is_const && r.is_const) {
                v = ev_const(trunc16(v.value - r.value));
                continue;
            }
            emit_materialize(t1, v);
            emit_materialize(t1, r);
            outcode(t1, "BN", OP_MINUS, TY_INT);
            v = ev_dynamic();
        } else {
            break;
        }
    }
    return v;
}

/* SHIFT := ADD (('<<'|'>>') ADD)* - '<<' emits OP_LSHIFT, '>>' emits
 * OP_RSHIFT, same "BN" (tag + type) shape as every other binary op -
 * c1 (not c0) decides between the constant-count "repeat a single-bit
 * shift N times" and variable-count "load the count into CL" codegen
 * shapes; c0's job is only to emit the tree. Confirmed byte-for-byte
 * against 04_shift's ".1.golden". */
static ExprVal parse_shift(Parser *p, FILE *t1)
{
    ExprVal v = parse_add(p, t1);
    for (;;) {
        int op;
        if (p->cur.kind == T_SHL)      op = OP_LSHIFT;
        else if (p->cur.kind == T_SHR) op = OP_RSHIFT;
        else break;
        advance(p);
        ExprVal r = parse_add(p, t1);
        if (v.is_const && r.is_const) {
            long res = (op == OP_LSHIFT) ? (v.value << r.value)
                                          : (v.value >> r.value);
            v = ev_const(trunc16(res));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        outcode(t1, "BN", op, TY_INT);
        v = ev_dynamic();
    }
    return v;
}

/* RELATIONAL := SHIFT (('<'|'<='|'>'|'>=') SHIFT)* - a non-constant
 * comparison emits its operator node exactly like any other binary
 * op (OP_LESS/OP_LESSEQ/OP_GREAT/OP_GREATEQ, "BN" shape) - c1 (not
 * c0) is what turns a comparison into a materialized 0/1 value or
 * fuses it into a short-circuit branch; c0's job is only to emit the
 * tree. Confirmed byte-for-byte against 03_rellogic's ".1.golden". */
static ExprVal parse_relational(Parser *p, FILE *t1)
{
    ExprVal v = parse_shift(p, t1);
    for (;;) {
        int op;
        if (p->cur.kind == T_LT)      op = OP_LESS;
        else if (p->cur.kind == T_LE) op = OP_LESSEQ;
        else if (p->cur.kind == T_GT) op = OP_GREAT;
        else if (p->cur.kind == T_GE) op = OP_GREATEQ;
        else break;
        advance(p);
        ExprVal r = parse_shift(p, t1);
        if (v.is_const && r.is_const) {
            long res;
            switch (op) {
            case OP_LESS:    res = v.value <  r.value; break;
            case OP_LESSEQ:  res = v.value <= r.value; break;
            case OP_GREAT:   res = v.value >  r.value; break;
            default /* GE */: res = v.value >= r.value; break;
            }
            v = ev_const(res);
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        outcode(t1, "BN", op, TY_INT);
        v = ev_dynamic();
    }
    return v;
}

/* EQUALITY := RELATIONAL (('=='|'!=') RELATIONAL)* - same shape as
 * RELATIONAL above (OP_EQUAL/OP_NEQUAL, "BN"). */
static ExprVal parse_equality(Parser *p, FILE *t1)
{
    ExprVal v = parse_relational(p, t1);
    for (;;) {
        int op;
        if (p->cur.kind == T_EQ)      op = OP_EQUAL;
        else if (p->cur.kind == T_NE) op = OP_NEQUAL;
        else break;
        advance(p);
        ExprVal r = parse_relational(p, t1);
        if (v.is_const && r.is_const) {
            long res = (op == OP_EQUAL) ? (v.value == r.value)
                                         : (v.value != r.value);
            v = ev_const(res);
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        outcode(t1, "BN", op, TY_INT);
        v = ev_dynamic();
    }
    return v;
}

static ExprVal parse_bitand(Parser *p, FILE *t1)
{
    ExprVal v = parse_equality(p, t1);
    while (p->cur.kind == T_AMP) {
        advance(p);
        ExprVal r = parse_equality(p, t1);
        if (v.is_const && r.is_const) {
            v = ev_const(trunc16(v.value & r.value));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        outcode(t1, "BN", OP_AND, TY_INT);
        v = ev_dynamic();
    }
    return v;
}

static ExprVal parse_bitxor(Parser *p, FILE *t1)
{
    ExprVal v = parse_bitand(p, t1);
    while (p->cur.kind == T_CARET) {
        advance(p);
        ExprVal r = parse_bitand(p, t1);
        if (v.is_const && r.is_const) {
            v = ev_const(trunc16(v.value ^ r.value));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        outcode(t1, "BN", OP_EXOR, TY_INT);
        v = ev_dynamic();
    }
    return v;
}

static ExprVal parse_bitor(Parser *p, FILE *t1)
{
    ExprVal v = parse_bitxor(p, t1);
    while (p->cur.kind == T_PIPE) {
        advance(p);
        ExprVal r = parse_bitxor(p, t1);
        if (v.is_const && r.is_const) {
            v = ev_const(trunc16(v.value | r.value));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        outcode(t1, "BN", OP_OR, TY_INT);
        v = ev_dynamic();
    }
    return v;
}

/* LOGAND := BITOR ('&&' BITOR)* ; LOGOR := LOGAND ('||' LOGAND)* -
 * standard C precedence puts these two above (looser than) every
 * bitwise operator. Like the relational/equality ops above, a
 * non-constant '&&'/'||' just emits its OP_LOGAND/OP_LOGOR node
 * ("BN" shape) over its two already-emitted operands - c1 owns the
 * short-circuit branch fusion. Confirmed against 03_rellogic's
 * ".1.golden". */
static ExprVal parse_logand(Parser *p, FILE *t1)
{
    ExprVal v = parse_bitor(p, t1);
    while (p->cur.kind == T_ANDAND) {
        advance(p);
        ExprVal r = parse_bitor(p, t1);
        if (v.is_const && r.is_const) {
            v = ev_const((v.value != 0) && (r.value != 0));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        outcode(t1, "BN", OP_LOGAND, TY_INT);
        v = ev_dynamic();
    }
    return v;
}

static ExprVal parse_logor(Parser *p, FILE *t1)
{
    ExprVal v = parse_logand(p, t1);
    while (p->cur.kind == T_OROR) {
        advance(p);
        ExprVal r = parse_logand(p, t1);
        if (v.is_const && r.is_const) {
            v = ev_const((v.value != 0) || (r.value != 0));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        outcode(t1, "BN", OP_LOGOR, TY_INT);
        v = ev_dynamic();
    }
    return v;
}

/* expr := LOGOR - standard C precedence, restricted to the levels
 * this grammar scope currently supports: '||' (loosest) > '&&' >
 * '|' > '^' > '&' > '=='/'!=' > '<'/'<='/'>'/'>=' > '<<'/'>>' >
 * '+'/'-' > '*'/'/'/'%' > unary > primary. Assignment-expression
 * operators are not yet part of this chain - see
 * src/mutos_cc/README.md. */
static ExprVal parse_expr(Parser *p, FILE *t1)
{
    return parse_logor(p, t1);
}

/* ------------------------------------------------------------------ */
/* Declarations */

/*
 * decl := 'int' declarator (',' declarator)* ';'
 * declarator := '*' IDENT | IDENT ('[' ICON ']')?
 *
 * Matches v7/cc/c03.c's AUTO-storage-class declarator loop: each
 * name's offset is assigned by symtab_declare_auto() (autolen -=
 * size; offset = autolen - see c0_sym.c), and each declared name
 * gets an ANAME opcode - outcode("BSN", ANAME, name, offset) -
 * which mutos_c1 renders as a "| _name=offset." comment right after
 * the function's entry label, confirmed byte-for-byte against
 * 01_intarith's goldens (see docs/DEVLOG.md).
 *
 * The '*'-prefixed (pointer) and '['-suffixed (array) declarator
 * forms are new - confirmed against 05_incdec.1.golden/.s.golden's
 * "int *p;"/"int a[4];": a pointer occupies MCC_SZINT bytes (same as
 * a plain int - both are 16-bit on this target) and is declared with
 * type TY_PTR_INT; an array of N ints occupies N*MCC_SZINT bytes and
 * is declared with plain TY_INT (its base element type - see
 * parse_primary()'s array-decay handling), just with sym->is_array
 * set so later references know it isn't itself an assignable/
 * incrementable lvalue. Only these two single-degree forms are
 * supported - "int **pp;", multi-dimensional arrays, and any
 * non-'int' element type are not yet - see src/mutos_cc/README.md.
 */
static void parse_decl(Parser *p, FILE *t1)
{
    if (p->cur.kind != T_KW_INT) {
        c0_error_at(p->cur.line,
            "only 'int' local declarations are supported so far - "
            "see src/mutos_cc/README.md");
        while (p->cur.kind != T_SEMI && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
        return;
    }
    advance(p); /* consume 'int' */

    for (;;) {
        int is_ptr = 0;
        if (p->cur.kind == T_STAR) {
            is_ptr = 1;
            advance(p);
        }

        if (p->cur.kind != T_IDENT) {
            c0_error_at(p->cur.line, "expected an identifier in declaration");
            break;
        }
        char name[LEX_IDENT_MAX];
        strncpy(name, p->cur.ident, sizeof name - 1);
        name[sizeof name - 1] = '\0';
        int line = p->cur.line;
        advance(p);

        int is_array = 0;
        long arraylen = 0;
        if (!is_ptr && p->cur.kind == T_LBRACK) {
            advance(p);
            if (p->cur.kind != T_ICON) {
                c0_error_at(p->cur.line, "expected an array size constant");
            } else {
                arraylen = p->cur.ival;
                advance(p);
            }
            expect(p, T_RBRACK, "']'");
            is_array = 1;
        }

        int size = is_array ? (int)(arraylen * MCC_SZINT) : MCC_SZINT;
        int symtype = is_ptr ? TY_PTR_INT : TY_INT;

        SymEntry *sym = symtab_declare_auto(&p->syms, name, symtype, size);
        if (!sym) {
            c0_error_at(line, "'%s' redeclared", name);
        } else {
            sym->is_ptr = is_ptr;
            sym->is_array = is_array;
            outcode(t1, "BSN", OP_ANAME, sym->name, sym->offset);
        }

        if (p->cur.kind == T_COMMA) {
            advance(p);
            continue;
        }
        break;
    }
    expect(p, T_SEMI, "';'");
}

/* ------------------------------------------------------------------ */
/* Statements */

/*
 * doret() - matches v7/cc/c04.c's doret() shape: a bare "return;"
 * just branches to the epilogue; "return <expr>;" additionally emits
 * the expression (constant-folded where possible, a real NAME/
 * operator tree otherwise - see the ExprVal comment above) wrapped
 * in RFORCE (convert to the function's return type - always TY_INT
 * in this scope) and EXPR (statement wrapper carrying the source
 * line), exactly matching the confirmed golden byte sequence.
 */
static void do_return_stmt(Parser *p, FILE *t1, int retlab)
{
    int stmt_line = p->cur.line; /* line of the 'return' keyword itself -
                                   * see README.md's "Known simplifications"
                                   * for why this is only exact for
                                   * single-physical-line statements
                                   * (the only case in this grammar's
                                   * scope). */
    advance(p); /* consume 'return' */

    if (p->cur.kind == T_SEMI) {
        advance(p);
        branch_op(t1, retlab);
        return;
    }

    ExprVal v = parse_expr(p, t1);
    expect(p, T_SEMI, "';'");
    emit_materialize(t1, v);

    outcode(t1, "BN", OP_RFORCE, TY_INT);
    outcode(t1, "BN", OP_EXPR, stmt_line);
    branch_op(t1, retlab);
}

/*
 * assign-stmt := IDENT assign-op expr ';'
 * assign-op   := '=' | '+=' | '-=' | '*=' | '/=' | '%='
 *              | '<<=' | '>>=' | '&=' | '|=' | '^='
 *
 * Matches treeout()'s general ASSIGN handling: the lvalue NAME is
 * emitted first (left-to-right, matching a real assignment
 * expression's tr1), then the right-hand expression, then the
 * operator node itself, then the EXPR statement wrapper -
 * confirmed byte-for-byte against 01_intarith's "a = 17;"/"c = a +
 * b;"/etc. goldens (see docs/DEVLOG.md). The ten compound-assignment
 * operators share this exact same shape - c0 emits the real
 * ASPLUS/ASMINUS/ASTIMES/ASDIV/ASMOD/ASLSH/ASRSH/ASSAND/ASOR/ASXOR
 * opcode in place of ASSIGN, never a synthesized "a = a + 5"-style
 * tree - confirmed against 06_compasgn.1.golden (see
 * docs/DEVLOG.md).
 */
static void parse_assign_stmt(Parser *p, FILE *t1)
{
    char name[LEX_IDENT_MAX];
    strncpy(name, p->cur.ident, sizeof name - 1);
    name[sizeof name - 1] = '\0';
    int line = p->cur.line;

    SymEntry *sym = symtab_lookup(&p->syms, name);
    advance(p); /* consume IDENT */

    int optag;
    switch (p->cur.kind) {
    case T_ASSIGN:    optag = OP_ASSIGN;  break;
    case T_PLUSEQ:    optag = OP_ASPLUS;  break;
    case T_MINUSEQ:   optag = OP_ASMINUS; break;
    case T_STAREQ:    optag = OP_ASTIMES; break;
    case T_SLASHEQ:   optag = OP_ASDIV;   break;
    case T_PERCENTEQ: optag = OP_ASMOD;   break;
    case T_SHLEQ:     optag = OP_ASLSH;   break;
    case T_SHREQ:     optag = OP_ASRSH;   break;
    case T_ANDEQ:     optag = OP_ASSAND;  break;
    case T_OREQ:      optag = OP_ASOR;    break;
    case T_XOREQ:     optag = OP_ASXOR;   break;
    default:
        c0_error_at(p->cur.line,
            "expected '=' or a compound-assignment operator, found %s",
            tok_kind_name(p->cur.kind));
        while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
        return;
    }
    advance(p); /* consume the assignment operator */

    if (!sym) {
        c0_error_at(line, "'%s' undeclared", name);
    } else {
        outcode(t1, "BNNN", OP_NAME, sym->hclass, sym->type, sym->offset);
    }

    ExprVal rhs = parse_expr(p, t1);
    expect(p, T_SEMI, "';'");
    emit_materialize(t1, rhs);

    /* The operator's type argument is the LVALUE's type (TY_INT for
     * every case confirmed so far, but TY_PTR_INT for "p = a;" -
     * confirmed against 05_incdec.1.golden byte 242-243; the ten
     * compound-assignment operators reuse the same convention, though
     * only the TY_INT case is itself golden-confirmed for them - see
     * 06_compasgn.1.golden). Falls back to TY_INT for the
     * already-reported undeclared-name case above. */
    outcode(t1, "BN", optag, sym ? sym->type : TY_INT);
    outcode(t1, "BN", OP_EXPR, line);
}

/*
 * star-assign-stmt := '*' ('++'|'--')? IDENT ('++'|'--')? '=' expr ';'
 *
 * Handles "*p++ = 1;" / "*++p = 2;" - an assignment through a
 * dereferenced pointer, optionally combined with a single prefix OR
 * postfix '++'/'--' on the pointer itself (not both - real C
 * wouldn't parse "*++p++" as this shape either). Only a plain pointer
 * variable is supported as the operand so far (not a general pointer
 * expression) - see src/mutos_cc/README.md. Confirmed byte-for-byte
 * against 05_incdec.1.golden/.s.golden's two "*p<op> = <rhs>;"
 * statements: the pointer sub-expression's tree (NAME, plus the
 * INCBEF/INCAFT/DECBEF/DECAFT scaling shape from emit_incdec() when
 * an operator is present) is emitted exactly like the expression-
 * level postfix/prefix cases above, followed by STAR (dereference,
 * always TY_INT - only pointer-to-int is supported), then the usual
 * rhs/ASSIGN/EXPR shape.
 */
static void parse_star_assign_stmt(Parser *p, FILE *t1)
{
    int line = p->cur.line;
    advance(p); /* consume '*' */

    int optag = 0;
    if (p->cur.kind == T_INCR || p->cur.kind == T_DECR) {
        optag = (p->cur.kind == T_INCR) ? OP_INCBEF : OP_DECBEF;
        advance(p);
    }

    if (p->cur.kind != T_IDENT) {
        c0_error_at(p->cur.line,
            "'*<expr> = ...' is only supported for a plain pointer "
            "variable, optionally with a leading/trailing '++'/'--' "
            "- see src/mutos_cc/README.md");
        while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
        return;
    }

    SymEntry *sym = symtab_lookup(&p->syms, p->cur.ident);
    if (!sym) {
        c0_error_at(line, "'%s' undeclared", p->cur.ident);
    } else if (!sym->is_ptr) {
        c0_error_at(line, "'*' applied to a non-pointer variable is "
                           "not yet supported - see src/mutos_cc/README.md");
        sym = NULL; /* best-effort: skip codegen below like undeclared */
    }
    advance(p); /* consume IDENT */

    if (!optag && (p->cur.kind == T_INCR || p->cur.kind == T_DECR)) {
        optag = (p->cur.kind == T_INCR) ? OP_INCAFT : OP_DECAFT;
        advance(p);
    }

    if (sym) {
        outcode(t1, "BNNN", OP_NAME, sym->hclass, sym->type, sym->offset);
        if (optag)
            emit_incdec(t1, optag, sym->type, sym->is_ptr);
    }
    outcode(t1, "BN", OP_STAR, TY_INT);

    if (!expect(p, T_ASSIGN, "'='")) {
        while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
        return;
    }

    ExprVal rhs = parse_expr(p, t1);
    expect(p, T_SEMI, "';'");
    emit_materialize(t1, rhs);

    outcode(t1, "BN", OP_ASSIGN, TY_INT);
    outcode(t1, "BN", OP_EXPR, line);
}

static void parse_compound_stmt(Parser *p, FILE *t1, int retlab)
{
    if (!expect(p, T_LBRACE, "'{'"))
        return;

    /* Declarations must precede statements within a block, matching
     * K&R block structure - only 'int' declarations are recognized
     * as such right now, so the loop condition doubles as "have we
     * reached the first statement yet". */
    while (p->cur.kind == T_KW_INT)
        parse_decl(p, t1);

    while (p->cur.kind != T_RBRACE && p->cur.kind != T_EOF) {
        if (p->cur.kind == T_KW_RETURN) {
            do_return_stmt(p, t1, retlab);
        } else if (p->cur.kind == T_IDENT) {
            parse_assign_stmt(p, t1);
        } else if (p->cur.kind == T_STAR) {
            parse_star_assign_stmt(p, t1);
        } else {
            c0_error_at(p->cur.line,
                "unsupported statement (mutos_c0's current grammar "
                "coverage handles only simple 'name = expr;' assignment "
                "and 'return' statements - see src/mutos_cc/README.md "
                "for the expansion plan)");
            while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
                advance(p);
            if (p->cur.kind == T_SEMI)
                advance(p);
        }
    }

    expect(p, T_RBRACE, "'}'");
}

/* ------------------------------------------------------------------ */
/* Function / external definitions */

/*
 * cfunc() - matches v7/cc/c02.c's cfunc() shape, with the MUTOS-
 * specific deltas documented in mutos_cc.h applied (extra EVEN,
 * STAUTO=-4, initial regvar=4, RETRN's extra type argument).
 */
static void cfunc(Parser *p, const char *name, FILE *t1)
{
    int sloc = p->isn;
    p->isn += 2;

    outcode(t1, "BBBS", OP_PROG, OP_EVEN, OP_RLABEL, name);

    int regvar = MCC_INIT_REGVAR; /* no register-class parameters/locals
                                    * are supported yet, so this never
                                    * changes before SETREG is emitted -
                                    * see README.md. */

    outcode(t1, "B", OP_SAVE);
    outcode(t1, "BN", OP_SETREG, regvar);

    branch_op(t1, sloc);
    label_op(t1, sloc + 1);

    int retlab = p->isn++;

    symtab_init(&p->syms);
    parse_compound_stmt(p, t1, retlab);

    outcode(t1, "BNBN", OP_LABEL, retlab, OP_RETRN, TY_INT);

    label_op(t1, sloc);
    outcode(t1, "BN", OP_SETSTK, -p->syms.maxauto);
    branch_op(t1, sloc + 1);

    symtab_clear(&p->syms);
}

static void parse_extdef(Parser *p, FILE *t1)
{
    if (p->cur.kind != T_IDENT) {
        c0_error_at(p->cur.line,
            "external definition syntax (expected a function name - "
            "mutos_c0's current grammar coverage only handles `name() "
            "{ ... }` function definitions - see src/mutos_cc/README.md)");
        advance(p);
        return;
    }

    Token name_tok = p->cur;
    advance(p);

    if (!expect(p, T_LPAREN, "'('"))
        return;
    if (p->cur.kind != T_RPAREN) {
        c0_error_at(p->cur.line,
            "function parameters are not yet supported by mutos_c0 - "
            "see src/mutos_cc/README.md");
        while (p->cur.kind != T_RPAREN && p->cur.kind != T_EOF)
            advance(p);
    }
    if (!expect(p, T_RPAREN, "')'"))
        return;

    outcode(t1, "BS", OP_SYMDEF, name_tok.ident);
    cfunc(p, name_tok.ident, t1);
}

/* ------------------------------------------------------------------ */

int c0_compile(FILE *in, FILE *temp1, FILE *temp2)
{
    Parser p;
    lex_init(&p.lx, in, c0_diag_filename);
    p.isn = 1;
    p.syms.head = NULL;
    advance(&p);

    while (p.cur.kind != T_EOF)
        parse_extdef(&p, temp1);

    outcode(temp1, "B", OP_EOFC);
    outcode(temp2, "B", OP_EOFC);

    return c0_diag_nerrors != 0;
}
