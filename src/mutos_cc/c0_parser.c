/*
 * c0_parser.c - mutos_c0 front-end driver.
 *
 * Current grammar coverage (deliberately narrow - see
 * src/mutos_cc/README.md for the expansion plan):
 *
 *   translation-unit  := extdef*
 *   extdef            := IDENT '(' ')' compound-stmt
 *   compound-stmt     := '{' decl* stmt* '}'
 *   decl              := ('int' declarator (',' declarator)*
 *                        | ('char'|'long') IDENT (',' IDENT)*) ';'
 *   declarator        := '*' IDENT | IDENT ('[' ICON ']')?
 *   stmt              := assign-stmt | star-assign-stmt | return-stmt
 *   assign-stmt       := IDENT assign-op expr ';'
 *   assign-op         := '=' | '+=' | '-=' | '*=' | '/=' | '%='
 *                       | '<<=' | '>>=' | '&=' | '|=' | '^='
 *   star-assign-stmt  := '*' ('++'|'--')? IDENT ('++'|'--')? '=' expr ';'
 *   return-stmt       := 'return' expr? ';'
 *   expr              := LOGOR ('?' LOGOR ':' LOGOR)?
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
 *   PRIMARY           := ICON | IDENT | cast-expr | sizeof-expr
 *                       | '(' comma-item (',' comma-item)* ')'
 *   cast-expr         := '(' ('int'|'char'|'long') ')' IDENT
 *   sizeof-expr       := 'sizeof' '(' ('int'|'char'|'long'|IDENT) ')'
 *   comma-item        := (IDENT '=' expr) | expr
 *
 * i.e. every declared local is 'int', 'int *' (one pointer degree),
 * 'int' '[' N ']' (one array dimension), 'char', or 'long' (the
 * latter two: plain IDENT declarators only, no '*'/'[' forms), with
 * no initializer; every
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
 * emit_incdec()). A parenthesized comma-list may embed a plain '='
 * assignment per comma-item (see parse_comma_item()'s
 * peek2_kind()-based lookahead) - not otherwise reachable from
 * expression context, since assign-stmt above is a distinct,
 * statement-level-only production; only '=' is supported there, not
 * any of the ten compound-assignment operators. cast-expr is
 * narrowly scoped to a bare-variable operand (not a general
 * unary-expr), picking one of three confirmed conversion opcodes
 * (LTOI/ITOC/the MUTOS-specific CTOL - see mutos_cc.h) by the
 * operand's declared type and the parsed target type - see
 * parse_primary()'s T_LPAREN handling. sizeof-expr folds entirely at
 * parse time to a CON(TY_UNSIGN, <size>) leaf, never a wire opcode of
 * its own, and never evaluates an IDENT operand's own NAME - see
 * parse_unary()'s T_KW_SIZEOF handling. This is
 * exactly tests/mutos_cc/00_smoke's three programs plus all of
 * tests/mutos_cc/01_expr: 01_intarith.c, 02_bitwise.c, 03_rellogic.c,
 * 04_shift.c, 05_incdec.c, 06_compasgn.c, 07_ternary.c and
 * 08_castsize.c. Array
 * subscripting, multi-level pointers/multi-dimensional arrays, and
 * 'long' arithmetic are
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

#define _POSIX_C_SOURCE 200809L /* for open_memstream() under -std=c11 -
                                  * see parse_for_stmt()'s deferred-
                                  * increment-emission comment. */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mutos_cc.h"
#include "c0_lex.h"
#include "c0_diag.h"
#include "c0_outcode.h"
#include "c0_sym.h"
#include "c0_parser.h"

/* Function-scoped goto-label table: name -> intermediate-code label
 * number, allocated on first mention (a 'name:' definition or a
 * 'goto name;' reference, whichever comes first - see
 * label_for_name()) - matches K&R C's own function-scope label
 * namespace (separate from ordinary variable names, so no interaction
 * with SymTab). Sized generously; 07_goto.c only ever uses 2. */
#define MCC_NLABELS 32
typedef struct {
    char name[LEX_IDENT_MAX];
    int  lab;
} LabelEntry;

/* One 'switch' case's (label, constant-value) pair - v7/cc's own
 * struct swtab (c0.h), collected while parsing a switch's body and
 * written out verbatim as OP_SWIT's trailing table - see
 * parse_switch_stmt(). Sized generously; 06_switch.c only ever uses
 * 4. Cases from a still-open ENCLOSING switch stay in this same
 * array (matching v7/cc's own shared, cursor-delimited `swtab`) so a
 * nested switch (not exercised by any golden) would not need its own
 * separate storage - parse_switch_stmt() only ever iterates its own
 * [case_base, p->ncases) slice. */
#define MCC_NCASES 32
typedef struct {
    int lab;
    long val;
} CaseEntry;

typedef struct {
    Lexer  lx;
    Token  cur;
    Token  la;      /* one token of lookahead beyond cur, valid iff
                       * have_la - see peek2_kind()'s comment below */
    int    have_la;
    int    isn;    /* next free intermediate-code label number - v7/cc/
                     * c00.c's global `isn`, initialized to 1 per
                     * translation unit. */
    SymTab syms;    /* current function's local (AUTO) variables -
                      * reset at the start of each cfunc(). */
    int    brklab;  /* v7/cc/c02.c's global `brklab`: the label a bare
                      * 'break;' branches to - the innermost enclosing
                      * loop's or switch's end label, 0 (never a valid
                      * label - isn starts at 1) when not inside one.
                      * Saved/restored by each loop/switch parser
                      * function around its own body, so nesting falls
                      * naturally out of C's own call stack - matching
                      * v7/cc's own save-a-local/restore-the-global
                      * pattern (e.g. WHILE's "o2 = brklab; ...;
                      * brklab = o2;") without needing an explicit
                      * stack structure. Reset to 0 at the start of
                      * each cfunc(). */
    int    contlab; /* v7/cc's global `contlab`: the label a bare
                      * 'continue;' branches to. Same save/restore
                      * discipline as brklab; FOR additionally
                      * reassigns it mid-construct once its own
                      * increment clause is known - see
                      * parse_for_stmt(). */
    LabelEntry labels[MCC_NLABELS]; /* goto-label table - see its
                      * typedef comment above. Reset (nlabels = 0) at
                      * the start of each cfunc(), matching labels'
                      * function-scoped namespace. */
    int    nlabels;
    int    prev_line; /* the line of the token most recently consumed
                      * (i.e. what p->cur held just before its last
                      * advance() away) - needed wherever a construct's
                      * own line has to be read back out AFTER a
                      * recursive parse_statement() call already moved
                      * p->cur past it (OP_SWIT's line - see
                      * parse_switch_stmt()); every other construct's
                      * own line-capture (if/while/do/for's CBRANCH)
                      * captures p->cur.line directly at the right
                      * moment instead and has no need for this. */
    int    deflab;  /* v7/cc's global `deflab`: the label a 'default:'
                      * inside the current switch was given, or 0 if
                      * none has been seen yet - saved/restored around
                      * a switch's own body the same way brklab/
                      * contlab are (see parse_switch_stmt()). */
    int    in_switch; /* depth counter - >0 while parsing a switch's
                      * body, so parse_case_stmt()/parse_default_stmt()
                      * can tell a stray 'case'/'default' apart from
                      * one genuinely inside a switch (not exercised by
                      * any golden, but the natural, symmetric check
                      * v7/cc's own "swp==0" test makes). */
    CaseEntry cases[MCC_NCASES]; /* see CaseEntry's own comment above. */
    int    ncases;
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

/* SZCHAR/SZLONG - char's and long's VALUE sizes (1 and 4 bytes
 * respectively - confirmed against 08_castsize.s.golden's "s2 =
 * matching its real value size). A char
 * local's STACK SLOT is 2 bytes regardless (see parse_decl()'s
 * declarator loop) - these two constants are the C-visible value
 * sizes sizeof() reports, not necessarily the allocation size. */
#define MCC_SZCHAR 1
#define MCC_SZLONG 4

static void advance(Parser *p)
{
    p->prev_line = p->cur.line; /* the line of the token we're about
                                  * to move past - see the Parser
                                  * field's own comment (needed by
                                  * parse_switch_stmt()'s OP_SWIT, the
                                  * one confirmed consumer so far). */
    if (p->have_la) {
        p->cur = p->la;
        p->have_la = 0;
    } else {
        p->cur = lex_next(&p->lx);
    }
}

/* Returns the token kind after p->cur, without consuming it -
 * buffered in p->la so a subsequent advance() returns it rather than
 * re-reading from the lexer. Only needed by parse_comma_item() below,
 * to tell "IDENT '=' expr" (an embedded assignment) apart from a bare
 * IDENT starting a larger expression - both look identical for the
 * first token, and assignment sits at a lower precedence than
 * anything else parse_expr() otherwise handles, so a single token of
 * lookahead resolves it without backtracking or speculative
 * emission. */
static TokKind peek2_kind(Parser *p)
{
    if (!p->have_la) {
        p->la = lex_next(&p->lx);
        p->have_la = 1;
    }
    return p->la.kind;
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
    int  is_long;  /* valid iff is_const - set by an integer literal
                     * too large for a plain 16-bit int, or by an
                     * explicit 'l'/'L' suffix (see parse_primary()'s
                     * T_ICON/T_LCON handling). */
    long value;   /* valid iff is_const - the FULL, untruncated value
                    * when is_long (see emit_materialize()'s LCON
                    * case), otherwise the trunc16()'d int value */
    int  type;    /* TY_INT or TY_LONG - valid always (both for a
                     * still-const value and for an already-emitted
                     * dynamic one, e.g. a NAME of a 'long' local).
                     * Mirrors is_long for the const case; only
                     * consulted so far by parse_mul()'s '*'/'/' /'%'
                     * to pick OP_TIMES/OP_DIVIDE/OP_MOD's TY_LONG vs.
                     * TY_INT operand - confirmed against
                     * 02_long/02_muldiv.1.golden's "c = a * b;" (a, b,
                     * c all 'long'), the only golden exercising
                     * long-typed arithmetic so far. Not propagated by
                     * '+'/'-' (parse_add()) or any other combinator -
                     * no golden yet confirms those. */
} ExprVal;

static ExprVal ev_const(long v)  { ExprVal e; e.is_const = 1; e.is_long = 0; e.value = v; e.type = TY_INT; return e; }
static ExprVal ev_const_long(long v) { ExprVal e; e.is_const = 1; e.is_long = 1; e.value = v; e.type = TY_LONG; return e; }
static ExprVal ev_dynamic(void)  { ExprVal e; e.is_const = 0; e.is_long = 0; e.value = 0; e.type = TY_INT; return e; }
static ExprVal ev_dynamic_typed(int ty) { ExprVal e = ev_dynamic(); e.type = ty; return e; }

/* If `v` is still an unmaterialized constant, emits it now as a real
 * CON leaf (treeout()'s CON case: outcode("BNN", CON, type, value)) -
 * called whenever a constant is about to be combined with a
 * non-constant sibling and so can no longer stay folded away. A
 * no-op if `v` was already emitted (is_const == 0). */
static void emit_materialize(FILE *t1, ExprVal v)
{
    if (!v.is_const)
        return;
    if (v.is_long) {
        /* LCON's wire format mirrors the confirmed "long" memory/ABI
         * convention (docs/MUTOS_C_ABI.md sect. 1.6: high word at the
         * LOWER address) even at this constant-encoding level, not
         * just for stack layout: two 16-bit words, high word first -
         * confirmed against 08_castsize.1.golden's "l = 70000;"
         * (0x00011170), whose LCON node's two N-fields decode to 1
         * (high) then 4464 (low, =0x1170). */
        long uv = (uint32_t)v.value;
        long hi = (int16_t)(uint16_t)((uv >> 16) & 0xFFFFL);
        long lo = (int16_t)(uint16_t)(uv & 0xFFFFL);
        outcode(t1, "BNNN", OP_LCON, TY_LONG, hi, lo);
    } else {
        outcode(t1, "BNN", OP_CON, TY_INT, (int)trunc16(v.value));
    }
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
static void parse_statement(Parser *p, FILE *t1, int retlab);
static void parse_compound_stmt(Parser *p, FILE *t1, int retlab);

/*
 * comma-item := (IDENT '=' expr) | expr
 *
 * Only reachable from inside a parenthesized comma-list (see
 * parse_primary()'s T_LPAREN case below) - a bare assignment
 * expression is not otherwise reachable from parse_expr()'s
 * precedence chain (assign-stmt, handling '=' and the ten compound-
 * assignment operators, is a distinct, statement-level-only
 * production - see parse_assign_stmt()). Only plain '=' is supported
 * here, not any compound-assignment operator - not exercised by any
 * confirmed golden in this position (07_ternary.c's own
 * "(a = a + 1, b = b + 1, a + b)" only ever uses '='). The NAME node
 * for the identifier is emitted the same way either way (see
 * parse_assign_stmt()'s identical emission), so peek2_kind() decides
 * which continuation to take before anything is emitted - no
 * speculative emission or backtracking needed.
 */
static ExprVal parse_comma_item(Parser *p, FILE *t1)
{
    if (p->cur.kind == T_IDENT && peek2_kind(p) == T_ASSIGN) {
        char name[LEX_IDENT_MAX];
        strncpy(name, p->cur.ident, sizeof name - 1);
        name[sizeof name - 1] = '\0';
        int line = p->cur.line;
        SymEntry *sym = symtab_lookup(&p->syms, name);
        advance(p); /* consume IDENT */
        advance(p); /* consume '=' */

        if (!sym) {
            c0_error_at(line, "'%s' undeclared", name);
        } else {
            outcode(t1, "BNNN", OP_NAME, sym->hclass, sym->type, sym->offset);
        }

        ExprVal rhs = parse_expr(p, t1);
        emit_materialize(t1, rhs);
        outcode(t1, "BN", OP_ASSIGN, sym ? sym->type : TY_INT);
        return ev_dynamic();
    }
    return parse_expr(p, t1);
}

static ExprVal parse_primary(Parser *p, FILE *t1)
{
    if (p->cur.kind == T_ICON) {
        /* An integer literal too large for a plain (16-bit, signed)
         * int is automatically 'long' - standard K&R/C89 integer-
         * constant promotion (K&R2 sect. A2.5.1), confirmed against
         * 08_castsize.1.golden's "l = 70000;" (70000 > 32767) using
         * LCON rather than a truncated CON. Only a bare literal is
         * covered - no golden exercises long-typed arithmetic (a
         * long literal combined with '+'/'-'/etc.), so is_long is
         * never propagated by any combinator below; only
         * emit_materialize() (a direct assignment's rhs) ever reads
         * it. */
        long raw = p->cur.ival;
        advance(p);
        if (raw > 32767)
            return ev_const_long(raw);
        return ev_const(trunc16(raw));
    }
    if (p->cur.kind == T_LCON) {
        /* An integer literal with an explicit 'l'/'L' suffix - always
         * 'long', regardless of magnitude (unlike a plain T_ICON,
         * which is only promoted to 'long' when too big for a plain
         * int - see above). Confirmed against 02_long/02_muldiv.
         * 1.golden's "b = 37L;": 37 fits a plain int, but the
         * explicit suffix still produces the same LCON(TY_LONG, hi,
         * lo) shape as a magnitude-promoted literal (see
         * emit_materialize()'s LCON case) - byte-identical wire
         * output to "a = 123456L;" immediately before it, whose
         * magnitude alone would already force LCON either way. */
        long raw = p->cur.ival;
        advance(p);
        return ev_const_long(raw);
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
        return ev_dynamic_typed(sym->type);
    }
    if (p->cur.kind == T_LPAREN) {
        if (peek2_kind(p) == T_KW_INT || peek2_kind(p) == T_KW_CHAR ||
            peek2_kind(p) == T_KW_LONG) {
            /* cast-expr := '(' ('int'|'char'|'long') ')' IDENT
             *
             * Only a plain variable name as the operand - not a
             * general unary-expr - matching 08_castsize.c's only
             * confirmed uses ("(int) l", "(char) i", "(long) c").
             * The source type comes from the identifier's own
             * declared type (no general type-tracking exists on
             * ExprVal - see sizeof's comment above), which together
             * with the parsed target type picks one of the three
             * conversion opcodes confirmed against
             * 08_castsize.1.golden: long->int is LTOI, int->char is
             * ITOC, char->long is the MUTOS-specific CTOL (see
             * mutos_cc.h). Any other (source, target) pair - int-
             * >int, int->long, char->int, long->char, char->char,
             * long->long - is not yet supported; none is exercised
             * by this file. */
            advance(p); /* consume '(' */
            int target = (p->cur.kind == T_KW_INT) ? TY_INT
                        : (p->cur.kind == T_KW_CHAR) ? TY_CHAR : TY_LONG;
            advance(p); /* consume the type keyword */
            if (!expect(p, T_RPAREN, "')'"))
                return ev_dynamic();
            if (p->cur.kind != T_IDENT) {
                c0_error_at(p->cur.line,
                    "a cast's operand must be a plain variable name so "
                    "far - see src/mutos_cc/README.md");
                if (p->cur.kind != T_EOF)
                    advance(p);
                return ev_const(0);
            }
            int line = p->cur.line;
            SymEntry *sym = symtab_lookup(&p->syms, p->cur.ident);
            if (!sym) {
                c0_error_at(line, "'%s' undeclared", p->cur.ident);
                advance(p);
                return ev_const(0);
            }
            advance(p); /* consume IDENT */
            outcode(t1, "BNNN", OP_NAME, sym->hclass, sym->type, sym->offset);
            int optag;
            if (sym->type == TY_LONG && target == TY_INT)
                optag = OP_LTOI;
            else if (sym->type == TY_INT && target == TY_CHAR)
                optag = OP_ITOC;
            else if (sym->type == TY_CHAR && target == TY_LONG)
                optag = OP_CTOL;
            else {
                c0_error_at(line, "this cast combination is not yet "
                                   "supported - see src/mutos_cc/README.md");
                return ev_dynamic();
            }
            outcode(t1, "BN", optag, target);
            return ev_dynamic_typed(target);
        }
        advance(p);
        /* '(' comma-item (',' comma-item)* ')' - the ',' handling
         * (SEQNC) evaluates every item but the last purely for its
         * side effects, discarding its value and keeping only the
         * final item's - confirmed against 07_ternary.s.golden's
         * "(a = a + 1, b = b + 1, a + b)" (no code at all is emitted
         * for SEQNC itself; c1's OP_SEQNC just drops the earlier
         * value off its stack - see c1_gen.c). A lone parenthesized
         * expression (no comma) takes this same path with the loop
         * never running, identical to the plain "'(' expr ')'"
         * behavior every prior session already confirmed. */
        ExprVal v = parse_comma_item(p, t1);
        while (p->cur.kind == T_COMMA) {
            advance(p);
            emit_materialize(t1, v);
            ExprVal rhs = parse_comma_item(p, t1);
            outcode(t1, "BN", OP_SEQNC, TY_INT);
            v = rhs;
        }
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
    if (p->cur.kind == T_KW_SIZEOF) {
        /* sizeof '(' ('int'|'char'|'long'|IDENT) ')' - confirmed
         * against 08_castsize.1.golden: every sizeof(...) folds
         * directly to a CON(TY_UNSIGN, <size>) leaf at parse time -
         * NEVER an OP_SIZEOF wire node, and (for "sizeof(i)")
         * without so much as emitting i's own NAME - sizeof's
         * operand is never evaluated, only its type inspected. This
         * is emitted immediately here (not deferred as a further-
         * foldable ExprVal constant like every other constant
         * elsewhere in this file) since ExprVal carries no type tag
         * to remember "this constant must render as TY_UNSIGN if
         * ever materialized" - not exercised by any golden anyway,
         * since every sizeof(...) in 08_castsize.c is immediately
         * assigned, never combined further at parse time. Real C
         * also allows a general "sizeof unary-expr" form and other
         * type names (short/float/double/struct/pointer types/...) -
         * none of that is exercised here, so only this one
         * parenthesized-type-or-plain-variable form is supported. */
        advance(p);
        if (!expect(p, T_LPAREN, "'('"))
            return ev_const(0);
        long size;
        if (p->cur.kind == T_KW_INT) {
            size = MCC_SZINT;
            advance(p);
        } else if (p->cur.kind == T_KW_CHAR) {
            size = MCC_SZCHAR;
            advance(p);
        } else if (p->cur.kind == T_KW_LONG) {
            size = MCC_SZLONG;
            advance(p);
        } else if (p->cur.kind == T_IDENT) {
            SymEntry *sym = symtab_lookup(&p->syms, p->cur.ident);
            if (!sym) {
                c0_error_at(p->cur.line, "'%s' undeclared", p->cur.ident);
                size = 0;
            } else if (sym->is_array) {
                c0_error_at(p->cur.line, "sizeof of an array is not yet "
                                          "supported - see src/mutos_cc/README.md");
                size = 0;
            } else if (sym->is_ptr) {
                size = MCC_SZINT; /* a pointer is word-sized, same as int */
            } else switch (sym->type) {
                case TY_CHAR: size = MCC_SZCHAR; break;
                case TY_LONG: size = MCC_SZLONG; break;
                default:      size = MCC_SZINT;  break;
            }
            advance(p);
        } else {
            c0_error_at(p->cur.line,
                "sizeof's operand must be 'int'/'char'/'long' or a plain "
                "variable name so far - see src/mutos_cc/README.md");
            size = 0;
        }
        expect(p, T_RPAREN, "')'");
        outcode(t1, "BNN", OP_CON, TY_UNSIGN, size);
        return ev_dynamic();
    }
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
            /* TY_LONG iff either operand is 'long' - confirmed
             * against 02_long/02_muldiv.1.golden's "c = a * b;" (a, b
             * both 'long' -> OP_TIMES(TY_LONG)); a mixed long/int
             * case is not exercised by any golden but follows the
             * same ordinary-C-promotion reasoning. */
            int optype = (v.type == TY_LONG || r.type == TY_LONG) ? TY_LONG : TY_INT;
            outcode(t1, "BN", OP_TIMES, optype);
            v = ev_dynamic_typed(optype);
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
            int optype = (v.type == TY_LONG || r.type == TY_LONG) ? TY_LONG : TY_INT;
            outcode(t1, "BN", OP_DIVIDE, optype);
            v = ev_dynamic_typed(optype);
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
            int optype = (v.type == TY_LONG || r.type == TY_LONG) ? TY_LONG : TY_INT;
            outcode(t1, "BN", OP_MOD, optype);
            v = ev_dynamic_typed(optype);
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
            /* TY_LONG iff either operand is 'long' - confirmed
             * against 02_long/01_addsub.1.golden's "c = a + b;" (a, b
             * both 'long' -> OP_PLUS(TY_LONG)), same rule already
             * confirmed for '*'/'/' /'%' in parse_mul() above. */
            int optype = (v.type == TY_LONG || r.type == TY_LONG) ? TY_LONG : TY_INT;
            outcode(t1, "BN", OP_PLUS, optype);
            v = ev_dynamic_typed(optype);
        } else if (p->cur.kind == T_MINUS) {
            advance(p);
            ExprVal r = parse_mul(p, t1);
            if (v.is_const && r.is_const) {
                v = ev_const(trunc16(v.value - r.value));
                continue;
            }
            emit_materialize(t1, v);
            emit_materialize(t1, r);
            int optype = (v.type == TY_LONG || r.type == TY_LONG) ? TY_LONG : TY_INT;
            outcode(t1, "BN", OP_MINUS, optype);
            v = ev_dynamic_typed(optype);
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

/* expr := LOGOR ('?' LOGOR ':' LOGOR)? - standard C precedence,
 * restricted to the levels this grammar scope currently supports:
 * '?:' (loosest, confirmed against 07_ternary.c) > '||' > '&&' > '|'
 * > '^' > '&' > '=='/'!=' > '<'/'<='/'>'/'>=' > '<<'/'>>' > '+'/'-' >
 * '*'/'/'/'%' > unary > primary. Both branches of '?:' are
 * themselves restricted to LOGOR (not a nested ternary, and not a
 * comma-list) - the only shape 07_ternary.c's "a > b ? a : b"
 * confirms; real C allows a full expression for the true-branch and
 * a full conditional-expression (right-associative chaining) for the
 * false-branch, neither of which is exercised here. Assignment
 * operators are not part of this chain either (except inside a
 * parenthesized comma-list - see parse_comma_item()) - see
 * src/mutos_cc/README.md. */
static ExprVal parse_expr(Parser *p, FILE *t1)
{
    ExprVal cond = parse_logor(p, t1);
    if (p->cur.kind != T_QUEST)
        return cond;
    advance(p); /* consume '?' */
    ExprVal t = parse_logor(p, t1);
    if (!expect(p, T_COLON, "':'"))
        return ev_dynamic();
    ExprVal f = parse_logor(p, t1);

    if (cond.is_const) {
        /* Compile-time-constant condition - matches this file's
         * established "fold whenever every operand is constant"
         * policy elsewhere (real K&R cc's build() folds this too),
         * though not itself exercised by 07_ternary.c (its condition
         * is always a real, non-constant comparison). */
        return (cond.value != 0) ? t : f;
    }

    /* cond is already fully emitted here (parse_relational()/etc.
     * never leave a non-constant result unmaterialized - only a
     * still-foldable compile-time constant ever does, and that case
     * already returned above), so only the two branches might still
     * need materializing. */
    emit_materialize(t1, t);
    emit_materialize(t1, f);
    outcode(t1, "BN", OP_COLON, TY_INT);
    outcode(t1, "BN", OP_QUEST, TY_INT);
    return ev_dynamic();
}

/* ------------------------------------------------------------------ */
/* Declarations */

/*
 * decl := ('int' declarator (',' declarator)* | 'char' IDENT (',' IDENT)* | 'long' IDENT (',' IDENT)*) ';'
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
 * supported - "int **pp;", multi-dimensional arrays are not yet -
 * see src/mutos_cc/README.md.
 *
 * 'char'/'long' locals - confirmed against 08_castsize.1.golden/
 * .s.golden's "long l;"/"char c;" ANAME offsets - only support the
 * plain-IDENT declarator (no '*'/'[' forms; not exercised by any
 * golden yet). A 'long' occupies MCC_SZLONG (4) bytes of frame space,
 * matching its real value size. A 'char' occupies MCC_SZINT (2) bytes
 * of frame space DESPITE its real value size being MCC_SZCHAR (1) -
 * confirmed by "long l;" (offset -10) immediately followed by
 * "char c;" landing at offset -12, a 2-byte gap, not 1 - this target
 * always word-aligns an AUTO local's stack slot, matching int's own
 * slot size, even for a byte-sized value (see OP_ITOC's/OP_CTOL's
 * "movb" handling in c1_gen.c for how the 1-byte VALUE is actually
 * read/written within that 2-byte slot).
 */
static void parse_decl(Parser *p, FILE *t1)
{
    int symtype, slotsize;
    if (p->cur.kind == T_KW_INT) {
        symtype = TY_INT;
        slotsize = MCC_SZINT;
    } else if (p->cur.kind == T_KW_CHAR) {
        symtype = TY_CHAR;
        slotsize = MCC_SZINT; /* slot size, not value size - see above */
    } else if (p->cur.kind == T_KW_LONG) {
        symtype = TY_LONG;
        slotsize = MCC_SZLONG;
    } else {
        c0_error_at(p->cur.line,
            "only 'int'/'char'/'long' local declarations are supported "
            "so far - see src/mutos_cc/README.md");
        while (p->cur.kind != T_SEMI && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
        return;
    }
    advance(p); /* consume 'int'/'char'/'long' */

    if (symtype != TY_INT) {
        /* 'char'/'long': plain IDENT declarators only (no '*'/'['
         * forms - not exercised by any golden yet). */
        for (;;) {
            if (p->cur.kind != T_IDENT) {
                c0_error_at(p->cur.line, "expected an identifier in declaration");
                break;
            }
            char name[LEX_IDENT_MAX];
            strncpy(name, p->cur.ident, sizeof name - 1);
            name[sizeof name - 1] = '\0';
            int line = p->cur.line;
            advance(p);

            SymEntry *sym = symtab_declare_auto(&p->syms, name, symtype, slotsize);
            if (!sym) {
                c0_error_at(line, "'%s' redeclared", name);
            } else {
                outcode(t1, "BSN", OP_ANAME, sym->name, sym->offset);
            }

            if (p->cur.kind == T_COMMA) {
                advance(p);
                continue;
            }
            break;
        }
        expect(p, T_SEMI, "';'");
        return;
    }

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
        int decltype = is_ptr ? TY_PTR_INT : TY_INT;

        SymEntry *sym = symtab_declare_auto(&p->syms, name, decltype, size);
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

    /* An int-typed value assigned to a 'long' lvalue needs an
     * explicit widening conversion first - a plain int is only 2
     * bytes wide, and a 'long' lvalue's ASSIGN case expects a real
     * VK_LONG-producing value (see c1_gen.c) - confirmed against
     * 02_long/01_addsub.1.golden's "b = 23456;" (23456 fits a plain
     * int, so it takes the ordinary CON path, not LCON's), which
     * wraps the CON in OP_ITOL(TY_LONG) before ASSIGN. Only applies
     * to plain '=' - the ten compound-assignment operators are not
     * confirmed for a 'long' lvalue by any golden. */
    if (optag == OP_ASSIGN && sym && sym->type == TY_LONG && rhs.type != TY_LONG)
        outcode(t1, "BN", OP_ITOL, TY_LONG);

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

/* ------------------------------------------------------------------ */
/* Control flow: if/else, while, do/while, for, break, continue,
 * goto/labels - see docs/DEVLOG.md's 03_ctrlflow section for the
 * full byte-level derivation against tests/mutos_cc/03_ctrlflow's
 * goldens. All of it reuses OP_CBRANCH (v7/cc/c04.c's cbranch(t,lbl,
 * cond): branch to lbl if the tree's truth value equals `cond` -
 * cond=1 is branch-if-true, cond=0 is branch-if-false) plus the
 * already-existing OP_BRANCH/OP_LABEL, and p->isn as the same
 * function-wide label counter cfunc() already seeds (sloc/sloc+1/
 * retlab). */

/* Function-scoped goto-label lookup: returns the existing label
 * number for `name`, or allocates a fresh one (p->isn++) on first
 * mention - whichever comes first, a 'name:' definition or a 'goto
 * name;' reference. Matches K&R C's function-scoped, separate-from-
 * ordinary-identifiers label namespace; not exercised beyond 2
 * distinct names by any current golden (07_goto.c's "loop"/"done"),
 * so MCC_NLABELS is sized generously rather than tightly. */
static int label_for_name(Parser *p, const char *name, int line)
{
    for (int i = 0; i < p->nlabels; i++)
        if (strcmp(p->labels[i].name, name) == 0)
            return p->labels[i].lab;
    if (p->nlabels >= MCC_NLABELS) {
        c0_error_at(line, "too many labels in one function (internal "
                    "limit %d)", MCC_NLABELS);
        return p->isn; /* best-effort: doesn't consume a real slot */
    }
    int lab = p->isn++;
    strncpy(p->labels[p->nlabels].name, name, LEX_IDENT_MAX - 1);
    p->labels[p->nlabels].name[LEX_IDENT_MAX - 1] = '\0';
    p->labels[p->nlabels].lab = lab;
    p->nlabels++;
    return lab;
}

/*
 * labeled-stmt := IDENT ':' statement
 *
 * Confirmed against 07_goto.1.golden's "loop:" - emits the label,
 * then parses the statement it prefixes as an ordinary recursive
 * parse_statement() call (matching v7/cc's own "label(...); goto
 * stmt;" - looping back to re-enter statement() is equivalent to a
 * plain recursive call here, since nothing on the stack needs
 * unwinding first).
 */
static void parse_label_stmt(Parser *p, FILE *t1, int retlab)
{
    int line = p->cur.line;
    char name[LEX_IDENT_MAX];
    strncpy(name, p->cur.ident, sizeof name - 1);
    name[sizeof name - 1] = '\0';
    advance(p); /* consume IDENT */
    advance(p); /* consume ':' */
    int lab = label_for_name(p, name, line);
    label_op(t1, lab);
    parse_statement(p, t1, retlab);
}

/*
 * goto-stmt := 'goto' IDENT ';'
 *
 * The general (not-inside-an-if) case: matches v7/cc's
 * "if (o1=simplegoto()) branch(o1);" - a plain unconditional BRANCH
 * to the label's number (allocated now if this is a forward
 * reference - see label_for_name()). Confirmed against 07_goto.
 * 1.golden's "goto loop;" -> "BRANCH(4)" (loop's own, already-
 * allocated label).
 */
static void parse_goto_stmt(Parser *p, FILE *t1)
{
    int line = p->cur.line;
    advance(p); /* consume 'goto' */
    if (p->cur.kind != T_IDENT) {
        c0_error_at(p->cur.line, "expected a label name after 'goto'");
        while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
        return;
    }
    int lab = label_for_name(p, p->cur.ident, line);
    advance(p); /* consume IDENT */
    branch_op(t1, lab);
    expect(p, T_SEMI, "';'");
}

/*
 * break-stmt := 'break' ';'
 *
 * The general (not-inside-an-if) case - matches v7/cc's
 * "chconbrk(brklab); branch(brklab);". p->brklab is 0 (never a valid
 * label - isn starts at 1) outside any loop/switch, matching
 * chconbrk()'s "not in a loop" check.
 */
static void parse_break_stmt(Parser *p, FILE *t1)
{
    int line = p->cur.line;
    advance(p); /* consume 'break' */
    if (p->brklab == 0)
        c0_error_at(line, "'break' outside a loop or switch is not supported");
    else
        branch_op(t1, p->brklab);
    expect(p, T_SEMI, "';'");
}

/* continue-stmt := 'continue' ';' - mirrors parse_break_stmt() above,
 * targeting p->contlab instead. */
static void parse_continue_stmt(Parser *p, FILE *t1)
{
    int line = p->cur.line;
    advance(p); /* consume 'continue' */
    if (p->contlab == 0)
        c0_error_at(line, "'continue' outside a loop is not supported");
    else
        branch_op(t1, p->contlab);
    expect(p, T_SEMI, "';'");
}

/*
 * if-stmt := 'if' '(' expr ')' statement ('else' statement)?
 *
 * General shape confirmed against 01_ifelse.1.golden: CBRANCH(
 * false_lab, cond=0) skips the true-branch on a false condition;
 * when an 'else' follows, the true-branch additionally BRANCHes past
 * it to a second, end_lab, allocated only once 'else' is actually
 * seen (matching v7/cc's "o1=isn++" for false_lab up front, "o2=isn++"
 * for end_lab only inside the 'if (...==ELSE)' branch) - so a chain
 * of "else if"s allocates its labels in the same left-to-right,
 * two-per-level order confirmed there (4,5 outer; 6,7 inner).
 *
 * `line`'s value is confirmed to be the line of whatever token
 * follows the condition's ')' - NOT the 'if' statement's own line -
 * a direct consequence of v7/cc's own one-token lookahead there (done
 * to check for the shortcut below), which our parser reproduces for
 * free since p->cur already sits on that next token once the ')' is
 * consumed.
 */
static void parse_if_stmt(Parser *p, FILE *t1, int retlab)
{
    advance(p); /* consume 'if' */
    expect(p, T_LPAREN, "'('");
    ExprVal cond = parse_expr(p, t1);
    expect(p, T_RPAREN, "')'");
    emit_materialize(t1, cond);
    int line = p->cur.line;

    /* v7/cc's "simpif" shortcut: an if-body that is exactly a bare
     * 'goto label;' / 'break;' / 'continue;' compiles to a single
     * direct CBRANCH(target, cond=1) - no extra label allocated at
     * all - confirmed against 07_goto.s.golden ('if (i>=10) goto
     * done;') and 05_breakcont.s.golden ('if (j==3) break;' / 'if (i
     * ==j) continue;'). Only recognized when nothing but the bare
     * keyword (+ target, for goto) + ';' follows; anything else
     * (including a trailing 'else', not exercised by any golden)
     * falls through to the general shape below. */
    if (p->cur.kind == T_KW_GOTO && peek2_kind(p) == T_IDENT) {
        advance(p); /* 'goto' */
        char name[LEX_IDENT_MAX];
        strncpy(name, p->cur.ident, sizeof name - 1);
        name[sizeof name - 1] = '\0';
        advance(p); /* IDENT */
        if (p->cur.kind == T_SEMI) {
            advance(p);
            int lab = label_for_name(p, name, line);
            outcode(t1, "BNNN", OP_CBRANCH, lab, 1, line);
        }
        return;
    }
    if (p->cur.kind == T_KW_BREAK || p->cur.kind == T_KW_CONTINUE) {
        int is_break = (p->cur.kind == T_KW_BREAK);
        advance(p);
        if (p->cur.kind == T_SEMI) {
            advance(p);
            int target = is_break ? p->brklab : p->contlab;
            if (target == 0)
                c0_error_at(line, "'%s' outside a loop%s is not supported",
                            is_break ? "break" : "continue",
                            is_break ? " or switch" : "");
            else
                outcode(t1, "BNNN", OP_CBRANCH, target, 1, line);
        }
        return;
    }

    int false_lab = p->isn++;
    outcode(t1, "BNNN", OP_CBRANCH, false_lab, 0, line);
    parse_statement(p, t1, retlab);
    if (p->cur.kind == T_KW_ELSE) {
        advance(p);
        int end_lab = p->isn++;
        branch_op(t1, end_lab);
        label_op(t1, false_lab);
        parse_statement(p, t1, retlab);
        label_op(t1, end_lab);
    } else {
        label_op(t1, false_lab);
    }
}

/*
 * while-stmt := 'while' '(' expr ')' statement
 *
 * Confirmed against 02_while.1.golden: LABEL(top) before the test
 * (this doubles as 'continue''s target), CBRANCH(end,cond=0) after
 * it, body, BRANCH(top), LABEL(end) ('break''s target). `line` is the
 * line of the condition's own closing ')' - unlike 'if' above, no
 * extra lookahead happens here in v7/cc, so nothing shifts it to the
 * following token.
 */
static void parse_while_stmt(Parser *p, FILE *t1, int retlab)
{
    advance(p); /* consume 'while' */
    int saved_brklab = p->brklab, saved_contlab = p->contlab;

    int top_lab = p->isn++;
    p->contlab = top_lab;
    label_op(t1, top_lab);

    expect(p, T_LPAREN, "'('");
    ExprVal cond = parse_expr(p, t1);
    emit_materialize(t1, cond);
    int line = p->cur.line; /* p->cur is still ')' here */
    expect(p, T_RPAREN, "')'");

    int end_lab = p->isn++;
    p->brklab = end_lab;
    outcode(t1, "BNNN", OP_CBRANCH, end_lab, 0, line);

    parse_statement(p, t1, retlab);
    branch_op(t1, p->contlab);
    label_op(t1, end_lab);

    p->brklab = saved_brklab;
    p->contlab = saved_contlab;
}

/*
 * do-stmt := 'do' statement 'while' '(' expr ')' ';'
 *
 * Confirmed against 03_dowhile.1.golden: THREE labels allocated up
 * front, in this exact order - contlab, brklab, then the loop's own
 * top-of-body label - but contlab is only LABELed after the body
 * (right before the condition test), while the top label is LABELed
 * first (right after 'do'). CBRANCH branches back to the top label
 * on a TRUE condition (cond=1), falling through to brklab otherwise.
 */
static void parse_do_stmt(Parser *p, FILE *t1, int retlab)
{
    advance(p); /* consume 'do' */
    int saved_brklab = p->brklab, saved_contlab = p->contlab;

    int cont_lab = p->isn++;
    int brk_lab  = p->isn++;
    int top_lab  = p->isn++;
    p->contlab = cont_lab;
    p->brklab  = brk_lab;

    label_op(t1, top_lab);
    parse_statement(p, t1, retlab);
    label_op(t1, cont_lab);

    if (!expect(p, T_KW_WHILE, "'while'")) {
        p->brklab = saved_brklab;
        p->contlab = saved_contlab;
        return;
    }
    expect(p, T_LPAREN, "'('");
    ExprVal cond = parse_expr(p, t1);
    emit_materialize(t1, cond);
    int line = p->cur.line; /* p->cur is still ')' here - same
                              * last-consumed-token convention as
                              * 'while' above. */
    expect(p, T_RPAREN, "')'");
    outcode(t1, "BNNN", OP_CBRANCH, top_lab, 1, line);
    label_op(t1, brk_lab);
    expect(p, T_SEMI, "';'");

    p->brklab = saved_brklab;
    p->contlab = saved_contlab;
}

/*
 * for-stmt := 'for' '(' expr? ';' expr? ';' expr? ')' statement
 *
 * Confirmed against 04_for.1.golden (and, for the nested case, 05_
 * breakcont.1.golden): the init clause compiles in place, immediately
 * before the loop; the test clause compiles right after LABEL(test),
 * CBRANCH(brk,cond=0); but the INCREMENT clause is genuinely special
 * - matching v7/cc/c02.c's forstmt() exactly, it is PARSED (to keep
 * the token stream in order) before the body, but its EMITTED CODE is
 * deferred until after the body, and its own OP_EXPR keeps the source
 * LINE where it was originally written (the for-header's own line),
 * not wherever body-parsing left off - confirmed via 04_for's
 * increment ("i = i + 1", on the for-header's line 9) carrying
 * EXPR(9) even though it appears in the byte stream after the body
 * (whose own statement is on line 10), and equivalently for both the
 * outer and inner loops of 05_breakcont's nested case. Since c0
 * streams wire bytes as it parses (no AST to re-emit later the way
 * real cc's rcexpr(st) does), the increment's bytes are buffered via
 * open_memstream() instead and flushed verbatim after the body.
 * 'continue' inside the body must target a NEW label placed right
 * before this deferred increment, not the original test label - so
 * contlab is reassigned here exactly once the increment's presence is
 * known, matching v7's "l = contlab; contlab = isn++;".
 */
static void parse_for_stmt(Parser *p, FILE *t1, int retlab)
{
    advance(p); /* consume 'for' */
    expect(p, T_LPAREN, "'('");

    if (p->cur.kind != T_SEMI) {
        int line = p->cur.line;
        ExprVal v = parse_comma_item(p, t1);
        emit_materialize(t1, v);
        outcode(t1, "BN", OP_EXPR, line);
    }
    expect(p, T_SEMI, "';'");

    int saved_brklab = p->brklab, saved_contlab = p->contlab;
    int test_lab = p->isn++;
    int brk_lab  = p->isn++;
    p->contlab = test_lab;
    p->brklab  = brk_lab;

    label_op(t1, test_lab);

    if (p->cur.kind != T_SEMI) {
        ExprVal cond = parse_expr(p, t1);
        emit_materialize(t1, cond);
        int line = p->cur.line; /* p->cur is still ';' here */
        outcode(t1, "BNNN", OP_CBRANCH, brk_lab, 0, line);
    }
    expect(p, T_SEMI, "';'");

    if (p->cur.kind != T_RPAREN) {
        int new_cont_lab = p->isn++;
        p->contlab = new_cont_lab;

        int incr_line = p->cur.line;
        char *buf = NULL;
        size_t bufsz = 0;
        FILE *mem = open_memstream(&buf, &bufsz);
        if (!mem) {
            c0_error_at(incr_line, "internal: could not buffer the "
                        "'for' increment (out of memory)");
        } else {
            ExprVal v = parse_comma_item(p, mem);
            emit_materialize(mem, v);
            outcode(mem, "BN", OP_EXPR, incr_line);
            fclose(mem);
        }
        expect(p, T_RPAREN, "')'");

        parse_statement(p, t1, retlab);

        label_op(t1, new_cont_lab);
        if (buf) {
            fwrite(buf, 1, bufsz, t1);
            free(buf);
        }
        branch_op(t1, test_lab);
    } else {
        expect(p, T_RPAREN, "')'");
        parse_statement(p, t1, retlab);
        branch_op(t1, p->contlab);
    }

    label_op(t1, brk_lab);

    p->brklab = saved_brklab;
    p->contlab = saved_contlab;
}

/*
 * switch-stmt := 'switch' '(' expr ')' statement
 *
 * Matches v7/cc/c02.c's SWITCH case + pswitch() exactly - confirmed
 * against 06_switch.1.golden: the controlling expression is RFORCE'd
 * (the same "force into the return-value convention" wrapper 'return'
 * itself uses - see do_return_stmt()) and emitted as its own
 * expression-statement, THEN a BRANCH jumps past the whole body to a
 * fresh dispatch label (swlab), the body is parsed inline (case/
 * default labels and their values are collected into p->cases[] as
 * they're encountered - see parse_case_stmt()/parse_default_stmt()),
 * and finally OP_SWIT itself - deflab, then the line of the body's
 * own closing '}' (captured via p->prev_line, since parse_statement()
 * already consumed past it by the time control returns here), then
 * every collected (label, value) pair, then a zero-word terminator -
 * is emitted at the dispatch label, immediately followed by brklab
 * (both 'break' and a falling-off-the-end body land here).
 */
static void parse_switch_stmt(Parser *p, FILE *t1, int retlab)
{
    advance(p); /* consume 'switch' */
    int saved_brklab = p->brklab;
    int saved_deflab = p->deflab;
    int saved_in_switch = p->in_switch;
    int case_base = p->ncases;

    expect(p, T_LPAREN, "'('");
    ExprVal cond = parse_expr(p, t1);
    emit_materialize(t1, cond);
    int line = p->cur.line; /* p->cur is still ')' here - same
                              * last-consumed-token convention as
                              * 'while'/'do'/'for' above. */
    expect(p, T_RPAREN, "')'");
    outcode(t1, "BN", OP_RFORCE, TY_INT);
    outcode(t1, "BN", OP_EXPR, line);

    p->brklab = p->isn++;
    int swlab = p->isn++;
    branch_op(t1, swlab);

    p->deflab = 0;
    p->in_switch++;
    parse_statement(p, t1, retlab); /* the switch body */
    p->in_switch = saved_in_switch;

    branch_op(t1, p->brklab);
    label_op(t1, swlab);
    if (p->deflab == 0)
        p->deflab = p->brklab;
    outcode(t1, "BNN", OP_SWIT, p->deflab, p->prev_line);
    for (int i = case_base; i < p->ncases; i++)
        outcode(t1, "NN", p->cases[i].lab, (int)p->cases[i].val);
    outcode(t1, "0");
    label_op(t1, p->brklab);

    p->ncases = case_base;
    p->deflab = saved_deflab;
    p->brklab = saved_brklab;
}

/*
 * case-stmt := 'case' constant-expr ':' statement
 *
 * Matches v7/cc's CASE case exactly: a fresh label per case,
 * collected into p->cases[] (written out by parse_switch_stmt()'s own
 * OP_SWIT once the whole body is parsed) - confirmed against 06_
 * switch.1.golden. Only a directly-foldable constant expression (a
 * bare integer literal, or simple constant arithmetic - anything
 * parse_expr() itself constant-folds) is supported, matching this
 * grammar's existing constant-expression handling elsewhere (e.g.
 * array bounds are not yet a thing here at all). Falls through to the
 * statement it prefixes exactly like a labeled-stmt (see
 * parse_label_stmt()) - real K&R grammar treats 'case'/'default' as
 * label forms.
 */
static void parse_case_stmt(Parser *p, FILE *t1, int retlab)
{
    int line = p->cur.line;
    advance(p); /* consume 'case' */
    ExprVal v = parse_expr(p, t1);
    if (!expect(p, T_COLON, "':'")) {
        while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
            advance(p);
        return;
    }
    if (!v.is_const) {
        c0_error_at(line, "a 'case' label must be a constant expression");
        return;
    }
    if (p->in_switch == 0) {
        c0_error_at(line, "'case' not inside a 'switch'");
        return;
    }
    if (p->ncases >= MCC_NCASES) {
        c0_error_at(line, "too many 'case' labels in one 'switch' "
                    "(internal limit %d)", MCC_NCASES);
        return;
    }
    int lab = p->isn++;
    p->cases[p->ncases].lab = lab;
    p->cases[p->ncases].val = v.value;
    p->ncases++;
    label_op(t1, lab);
    parse_statement(p, t1, retlab);
}

/*
 * default-stmt := 'default' ':' statement
 *
 * Mirrors parse_case_stmt() above, but records the label into
 * p->deflab instead of the case table - confirmed against 06_switch.
 * 1.golden's OP_SWIT, whose deflab argument is the 'default:' clause's
 * own label (10) exactly.
 */
static void parse_default_stmt(Parser *p, FILE *t1, int retlab)
{
    int line = p->cur.line;
    advance(p); /* consume 'default' */
    if (!expect(p, T_COLON, "':'")) {
        while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
            advance(p);
        return;
    }
    if (p->in_switch == 0) {
        c0_error_at(line, "'default' not inside a 'switch'");
        return;
    }
    if (p->deflab != 0) {
        c0_error_at(line, "more than one 'default' in a 'switch'");
        return;
    }
    p->deflab = p->isn++;
    label_op(t1, p->deflab);
    parse_statement(p, t1, retlab);
}

/*
 * statement := compound-stmt | if-stmt | while-stmt | do-stmt |
 *              for-stmt | break-stmt | continue-stmt | goto-stmt |
 *              labeled-stmt | return-stmt | assign-stmt |
 *              star-assign-stmt
 *
 * The single recursive-descent dispatcher every statement-accepting
 * position (a compound-stmt's body, an if/while/do/for's own body)
 * now goes through - see each parse_<x>_stmt() above for its own
 * confirmed wire shape. "IDENT ':'" (a label definition) is checked
 * with one token of lookahead before falling back to a plain
 * assignment, since both start identically.
 */
static void parse_statement(Parser *p, FILE *t1, int retlab)
{
    if (p->cur.kind == T_LBRACE) {
        parse_compound_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_KW_RETURN) {
        do_return_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_KW_IF) {
        parse_if_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_KW_WHILE) {
        parse_while_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_KW_DO) {
        parse_do_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_KW_FOR) {
        parse_for_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_KW_SWITCH) {
        parse_switch_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_KW_CASE) {
        parse_case_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_KW_DEFAULT) {
        parse_default_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_KW_BREAK) {
        parse_break_stmt(p, t1);
    } else if (p->cur.kind == T_KW_CONTINUE) {
        parse_continue_stmt(p, t1);
    } else if (p->cur.kind == T_KW_GOTO) {
        parse_goto_stmt(p, t1);
    } else if (p->cur.kind == T_IDENT && peek2_kind(p) == T_COLON) {
        parse_label_stmt(p, t1, retlab);
    } else if (p->cur.kind == T_IDENT) {
        parse_assign_stmt(p, t1);
    } else if (p->cur.kind == T_STAR) {
        parse_star_assign_stmt(p, t1);
    } else {
        c0_error_at(p->cur.line,
            "unsupported statement (mutos_c0's current grammar "
            "coverage handles 'if'/'else', 'while', 'do'/'while', "
            "'for', 'switch'/'case'/'default', 'break', 'continue', "
            "'goto'/labels, simple 'name = expr;' assignment, and "
            "'return' statements - "
            "see src/mutos_cc/README.md for the expansion plan)");
        while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
    }
}

static void parse_compound_stmt(Parser *p, FILE *t1, int retlab)
{
    if (!expect(p, T_LBRACE, "'{'"))
        return;

    /* Declarations must precede statements within a block, matching
     * K&R block structure - 'int'/'char'/'long' declarations are
     * recognized as such right now, so the loop condition doubles as
     * "have we reached the first statement yet". A nested compound-
     * stmt (a loop/if body written as "{ ... }") re-enters here too;
     * none of the current corpus's nested blocks declare their own
     * locals, so this decl-loop simply finds none and falls straight
     * through - a block-scoped declaration is not yet supported (see
     * src/mutos_cc/README.md). */
    while (p->cur.kind == T_KW_INT || p->cur.kind == T_KW_CHAR ||
           p->cur.kind == T_KW_LONG)
        parse_decl(p, t1);

    while (p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
        parse_statement(p, t1, retlab);

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
    p->brklab = 0;
    p->contlab = 0;
    p->nlabels = 0;
    p->deflab = 0;
    p->in_switch = 0;
    p->ncases = 0;
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
    p.have_la = 0;
    p.syms.head = NULL;
    advance(&p);

    while (p.cur.kind != T_EOF)
        parse_extdef(&p, temp1);

    outcode(temp1, "B", OP_EOFC);
    outcode(temp2, "B", OP_EOFC);

    return c0_diag_nerrors != 0;
}
