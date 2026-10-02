/*
 * c0_parser.c - mutos_c0 front-end driver.
 *
 * Current grammar coverage (deliberately narrow - see
 * src/mutos_cc/README.md for the expansion plan):
 *
 * (This summary shows the expression core; parse_extdef(),
 * parse_param_decls(), parse_statement() and each parse_*_stmt() carry
 * their own, fuller productions - prototypes, file-scope variables
 * (parse_global_var(); a string-initialized "char name[]" -
 * parse_global_chararray()), parameters, nested blocks with their own
 * scope (parse_nested_block()), control flow, calls.)
 *
 *   translation-unit  := extdef*
 *   extdef            := IDENT '(' ')' compound-stmt
 *   compound-stmt     := '{' decl* stmt* '}'
 *   decl              := ('int'|'char'|'long'|'float'|'double') declarator
 *                        (',' declarator)* ';'
 *   declarator        := '*'* IDENT ('[' ICON ']' ('[' ICON ']')?)?
 *                       | '(' '*' IDENT ')' '(' ')'
 *   stmt              := assign-stmt | star-assign-stmt | call-stmt
 *                       | return-stmt
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
 *   UNARY             := ('-'|'+'|'~'|'!'|'*') UNARY | ('++'|'--') IDENT
 *                       | '&' IDENT ('[' expr ']')? | POSTFIX
 *   POSTFIX           := PRIMARY ('++'|'--')?
 *   PRIMARY           := ICON | CCON | FCON | IDENT | STRING | cast-expr
 *                       | sizeof-expr
 *                       | '(' comma-item (',' comma-item)* ')'
 *   cast-expr         := '(' ('int'|'char'|'long'|'float'|'double') ')' IDENT
 *   sizeof-expr       := 'sizeof' '(' ('int'|'char'|'long'|IDENT) ')'
 *   comma-item        := (IDENT '=' expr) | expr
 *
 * i.e. every declared local is 'int', 'int *' (one pointer degree),
 * 'int' '[' N ']' or 'int' '[' N ']' '[' M ']' (one or two array
 * dimensions - see emit_subscript_2d()), 'char', or 'long' (the
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

/* Function names registered so far in this translation unit (by
 * parse_extdef()'s own definitions and by parse_top_prototype()'s
 * forward declarations) - a GLOBAL, whole-file table, unlike
 * Parser::syms (reset per-function) - matching v7/cc's own single,
 * persistent hshtab: a function's own definition/prototype earlier
 * in the file makes its name a recognized function for the rest of
 * the file. The only current consumer is parse_primary()'s "bare
 * function name used as a value" fallback (see its own comment) -
 * confirmed necessary against 07_funcptr.1.golden's "fp = square;"
 * (main() is the LAST function in that file, so "square"/"cube" are
 * already registered by the time it's parsed). Sized generously;
 * 07_funcptr.c only ever registers 3. */
#define MCC_MAXFUNCS 64

/* A typedef name - v7/cc's TYPEDEF-class symbol (c03.c's getkeywords()
 * takes one as the declaration's type): the type code it stands for and,
 * for a struct type, which struct. */
typedef struct TypedefEnt {
    char   name[MCC_NCPS + 1];
    int    type;
    StructDef *sdef;
    struct TypedefEnt *next;
} TypedefEnt;

/* An enumeration constant - v7's ENUMCON symbol (c03.c's decl1(), skw ==
 * ENUM): a name for an int constant, file-wide in this front end. */
typedef struct EnumCon {
    char   name[MCC_NCPS + 1];
    int    value;
    struct EnumCon *next;
} EnumCon;

typedef struct {
    Lexer  lx;
    FILE  *t2;      /* the temp2 stream - string-literal data only (see
                      * putstr()); every other opcode goes to temp1,
                      * which is still passed around explicitly as `t1`
                      * since parse_for_stmt() temporarily redirects it
                      * to a memory buffer. */
    Token  cur;
    Token  la;      /* one token of lookahead beyond cur, valid iff
                       * have_la - see peek2_kind()'s comment below */
    int    have_la;
    int    isn;    /* next free intermediate-code label number - v7/cc/
                     * c00.c's global `isn`, initialized to 1 per
                     * translation unit. */
    SymTab syms;    /* current function's parameters and locals
                      * (block-structured - see c0_sym.h) - reset at
                      * the start of each cfunc(). */
    SymTab globals; /* the translation unit's file-scope variables
                      * (hclass SC_EXTERN - see parse_global_var()),
                      * searched after `syms`, so a local shadows a
                      * global of the same name (lookup_var()). */
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
    char   funcnames[MCC_MAXFUNCS][LEX_IDENT_MAX]; /* see MCC_MAXFUNCS's
                                                      * own comment above. */
    int    functypes[MCC_MAXFUNCS]; /* parallel to funcnames[] - each
                      * function's own return type (TY_INT/TY_CHAR/
                      * TY_LONG), as declared at its first prototype or
                      * definition - see register_func()/
                      * lookup_func_type(). */
    int    nfuncnames;
    int    cur_ret_type; /* the CURRENT function's own declared return
                      * type - set once per cfunc(), consulted by
                      * do_return_stmt() (OP_RFORCE's type argument,
                      * previously hardcoded TY_INT - see
                      * 02_long/03_retval.c). */
    int    regvar;  /* v7/cc's global `regvar`: how many more
                      * 'register'-class local slots remain claimable
                      * in the CURRENT function - reset to
                      * MCC_INIT_REGVAR at the start of each cfunc().
                      * See try_claim_register()/04_funcs/06_regclass.c. */
    StructDef  *structs;   /* every struct/union tag (and anonymous
                            * struct) of the translation unit - see
                            * parse_type_spec(); tags are file-wide here
                            * (v7 scopes them by block, which no corpus
                            * file needs) */
    TypedefEnt *typedefs;  /* typedef names, file-wide */
    EnumCon    *enumcons;  /* enumeration constants, file-wide */
    int    struct_value_ok; /* set while parsing the right-hand side of a
                            * whole-struct assignment - the one place a
                            * struct-typed value is accepted (see
                            * parse_struct_assign()) */
} Parser;

/* One pointer-to-int degree, matching mutos_cc.h's XTYPE bit layout
 * (base type in the low 3 bits, degree in bits 3-4) - confirmed
 * against 05_incdec.1.golden's NAME(p)/ASSIGN(p=a) nodes, which both
 * use type value 8 (TY_INT | 010). Only pointer-to-int is supported
 * in this grammar scope - see src/mutos_cc/README.md. */
#define TY_PTR_INT (TY_INT | 010)

/* "function returning int" - v7/cc/c0.h's FUNC(020) derived-type tag,
 * used only for the NAME leaf that names a CALLED function (see
 * parse_call() below) - confirmed against every 04_funcs .1.golden's
 * "NAME hclass=SC_EXTERN type=FUNC.TY_INT(16)" callee reference.
 * Unlike TY_PTR_INT (a single degree-of-reference step, v7/cc's PTR
 * tag alone), this is one FUNC step - see v7/cc/c04.c's incref()
 * (mutos_c0 does not need a general incref() implementation, since
 * this grammar scope only ever derives exactly this one fixed
 * type). */
#define TY_FUNC_INT (TY_INT | 020)

/* "pointer to function returning int" - v7/cc/c04.c's incref()
 * applied to TY_FUNC_INT: incref(t) = ((t & ~TYPE) << TYLEN) |
 * (t & TYPE) | PTR (TYPE=7, TYLEN=2, PTR=8 - v7/cc/c0.h), giving
 * incref(16) = 72 - confirmed against 07_funcptr.1.golden's "int
 * (*f)()"/"int (*fp)()" declarator (every ANAME/NAME/AMPER/ASSIGN
 * touching `f`/`fp` uses type 72). A literal constant rather than a
 * general incref() implementation since this grammar scope only
 * ever derives this one specific type (a plain function-pointer
 * variable/parameter - no pointer-to-pointer-to-function or any
 * other multi-level derived type is exercised). */
#define TY_PTR_FUNC_INT 72

/* General degree-of-reference chaining, matching v7/cc/c04.c's own
 * incref()/decref() exactly (TYPE=7 the base-type mask, TYLEN=2,
 * PTR=010=8 - see TY_PTR_FUNC_INT's comment above for the derivation
 * this generalizes): incref_tag(t, PTR) adds one more pointer degree
 * on top of whatever `t` already has - confirmed against
 * 05_arrptr/06_ptrptr.1.golden's "int **pp": incref_tag(TY_PTR_INT,
 * PTR) = 40, NOT a naive "16" (TY_PTR_INT + one more degree is NOT
 * TY_PTR_INT*2) - `pp`'s own NAME/AMPER/ASSIGN nodes all use type 40.
 * ty_decref() is its exact inverse (strips exactly one degree,
 * confirmed by round-tripping ty_decref(40)==TY_PTR_INT via the same
 * golden's "**pp = 6;": the first STAR's own type is TY_PTR_INT(8),
 * the second's is TY_INT(0)). Only the PTR tag is ever chained by
 * mutos_c0 today (ARRAY/FUNC are only ever a single fixed degree -
 * see TY_PTR_FUNC_INT/TY_FUNC_INT above); a general ty_ptr_of() atop
 * ty_incref_tag() covers every pointer-degree case this grammar scope
 * exercises (05_arrptr's 03_ptrbasic/04_ptrarreq/06_ptrptr). */
#define TY_TYPE_MASK  7    /* v7/cc's TYPE - the base-type field */
#define TY_XTYPE_MASK 030  /* v7/cc's XTYPE - the outermost degree tag */

static int ty_incref_tag(int t, int tag)
{
    return (((t & ~TY_TYPE_MASK) << 2) | (t & TY_TYPE_MASK) | tag);
}

static int ty_ptr_of(int t) { return ty_incref_tag(t, 010 /* PTR */); }

/* One ARRAY degree (v7/cc/c0.h's ARRAY=030) - only used to derive the
 * "pointer to array of int" type (ty_ptr_of(ty_ary_of(TY_INT)) = 104)
 * a 2-D subscript's outer OP_ITOP carries - see emit_subscript(). */
static int ty_ary_of(int t) { return ty_incref_tag(t, 030 /* ARRAY */); }

static int ty_decref(int t)
{
    int base = t & TY_TYPE_MASK;
    int cleared = t & ~TY_TYPE_MASK & ~TY_XTYPE_MASK;
    return base | (cleared >> 2);
}

/* 1 iff `t`'s outermost derived-type degree is PTR (010) - "pointer to
 * anything", whatever it points to. */
static int ty_is_ptr(int t) { return (t & TY_XTYPE_MASK) == 010; }

/* "pointer to char" (9) - the type every string literal decays to. */
#define TY_PTR_CHAR (TY_CHAR | 010)

/* Maximum K&R-style parameters a single function definition can
 * declare - sized generously; 04_funcs/02_manyargs.c's six-parameter
 * sum6() is the largest confirmed case in the current corpus. */
#define MCC_MAXPARAMS 16

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

/* SZFLOAT/SZDOUB - v7/cc/c0.h's own values (4 and 8), the frame slots
 * 08_float's goldens confirm: "float a, b, c;" at -8/-12/-16, "double
 * d;" at -12 (01_floatbas.1.golden, 02_dblconv.1.golden's ANAMEs). */
#define MCC_SZFLOAT 4
#define MCC_SZDOUB  8

/* A floating type - float or double. v7's build() computes in double
 * whatever the operands ("if (t==FLOAT) t = DOUBLE"), so an operator
 * or assignment over them is always typed TY_DOUBLE; only a NAME keeps
 * TY_FLOAT, which tells mutos_c1 the variable's size. */
static int ty_is_float(int t) { return t == TY_FLOAT || t == TY_DOUBLE; }

/* The VALUE size (not the padded stack-slot size - see MCC_SZCHAR's
 * own comment) of a scalar or pointer type - the scale factor array
 * subscripting/pointer arithmetic steps a pointer by. Every pointer
 * degree is word-sized regardless of what it points to (matches
 * MCC_SZINT, same as a plain int - see sizeof's own already-
 * established "a pointer is word-sized" rule above). Only int/char/
 * long/pointer element types are exercised by any current golden. */
/* The line of the token most recently read - for a diagnostic from a
 * helper that has no Parser at hand (size_of_type()). */
static int diag_line;

static int size_of_type(int t)
{
    if (t == TY_CHAR)
        return MCC_SZCHAR;
    if (t == TY_LONG)
        return MCC_SZLONG;
    if (t == TY_FLOAT)
        return MCC_SZFLOAT;
    if (t == TY_DOUBLE)
        return MCC_SZDOUB;
    if (t == TY_STRUCT) {
        /* A struct's size is its StructDef's (type_size()); the paths
         * that reach here - pointer arithmetic, "++" on a pointer, the
         * plain subscript code - have none, and no golden shows pointer
         * arithmetic on a struct pointer. */
        c0_error_at(diag_line, "pointer arithmetic on a pointer to a struct is "
                               "not yet supported - see src/mutos_cc/README.md");
        return MCC_SZINT;
    }
    return MCC_SZINT;
}

/* v7/cc/c04.c's length(): the size of an object of type `t` (any degree
 * of reference) - `sdef` the struct when t's base is TY_STRUCT, `nelem`
 * the element count when t's outermost degree is ARRAY. */
static int type_size(int t, const StructDef *sdef, int nelem)
{
    if ((t & 030) == 030)                     /* ARRAY */
        return nelem * type_size((t & 07) | ((t >> 2) & ~07), sdef, 0);
    if (t & 030)                              /* PTR or FUNC */
        return MCC_SZINT;
    if (t == TY_STRUCT)
        return sdef ? sdef->size : MCC_SZINT;
    if (t == TY_CHAR)
        return MCC_SZCHAR;
    if (t == TY_LONG)
        return MCC_SZLONG;
    if (t == TY_FLOAT)
        return MCC_SZFLOAT;
    if (t == TY_DOUBLE)
        return MCC_SZDOUB;
    return MCC_SZINT;                         /* int, unsigned */
}

static void advance(Parser *p)
{
    /* A T_STRING token's text is malloc'd by the lexer and owned by
     * the token (c0_lex.h). It is released here, as the token is
     * discarded - parse_primary()'s string-literal case writes it out
     * (putstr()) before advancing, and any other production that meets
     * one (a syntax error) simply drops it. Without this every string
     * literal leaked (found by an ASan run over the corpus). */
    free(p->cur.sval);
    p->cur.sval = NULL;
    diag_line = p->cur.line;
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

/* A variable name as an expression sees it: the innermost local
 * declaration, else a file-scope one (07_scope/02_shadow.c's inner "x"
 * hides the outer one; 07_scope/01_globstat.c's "counter" is found
 * among the globals). NULL if neither declares it. */
static SymEntry *lookup_var(Parser *p, const char *name)
{
    SymEntry *e = symtab_lookup(&p->syms, name);
    return e ? e : symtab_lookup(&p->globals, name);
}

/* A variable reference - treeout()'s NAME case (v7/cc/c04.c): a
 * file-scope variable (SC_EXTERN) is named by its symbol ("BNNS" -
 * outcode()'s 'S' adds the leading '_'), every other class carries its
 * numeric "offset" (a frame offset, a static's label, a register slot)
 * - "BNNN". `type` is usually the symbol's own; a 2-D array's NAME
 * carries its element type instead (see emit_subscript_2d()).
 * Confirmed for SC_EXTERN against 07_scope/01_globstat.1.golden
 * (NAME(12, 0, "_counter")) and 03_externdef.1.golden ("_total"). */
static void emit_name(FILE *t1, const SymEntry *sym, int type)
{
    if (sym->hclass == SC_EXTERN)
        outcode(t1, "BNNS", OP_NAME, SC_EXTERN, type, sym->name);
    else
        outcode(t1, "BNNN", OP_NAME, sym->hclass, type, sym->offset);
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
    int  char_obj; /* 1 iff this is a 'char' object as written - a char
                     * variable's NAME or a char element/dereference's
                     * STAR, with no operator or conversion on top (set
                     * by ev_char_obj() only). A relational or equality
                     * operator compares such an operand with a small
                     * constant as a char, with no ITOC - see
                     * char_const_compare(). */
    StructDef *sdef; /* the struct when `type`'s base is TY_STRUCT - a
                     * pointer to one ("cur"), or (only where
                     * Parser.struct_value_ok allows it) a whole struct
                     * value - see parse_postfix_chain(). */
} ExprVal;

static ExprVal ev_const(long v)  { ExprVal e; e.is_const = 1; e.is_long = 0; e.value = v; e.type = TY_INT; e.char_obj = 0; e.sdef = NULL; return e; }
static ExprVal ev_const_long(long v) { ExprVal e; e.is_const = 1; e.is_long = 1; e.value = v; e.type = TY_LONG; e.char_obj = 0; e.sdef = NULL; return e; }
static ExprVal ev_dynamic(void)  { ExprVal e; e.is_const = 0; e.is_long = 0; e.value = 0; e.type = TY_INT; e.char_obj = 0; e.sdef = NULL; return e; }
static ExprVal ev_dynamic_typed(int ty) { ExprVal e = ev_dynamic(); e.type = ty; return e; }
/* A value of type `ty` just emitted as a NAME or STAR leaf: marked as a
 * char object when `ty` is TY_CHAR (see ExprVal's char_obj). */
static ExprVal ev_char_obj(int ty) { ExprVal e = ev_dynamic_typed(ty); e.char_obj = (ty == TY_CHAR); return e; }

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

/* ------------------------------------------------------------------ */
/* 'char' conversions - opcode 109 (OP_ITOC), its type argument the
 * RESULT type.
 *
 * Vanilla v7 treats char and int alike when inserting conversions
 * (v7/cc/c01.c's lintyp() maps both to the same row of cvtab[]), and
 * lets the PDP-11's sign-extending MOVB do the widening. The MUTOS
 * front end does not: its goldens show explicit conversions, in
 * exactly the places v7's build() applies cvtab[] conversions:
 *
 *   buf[0] = 1;             ... STAR(1) CON(1) ITOC(1) ASSIGN(1)
 *   buf[0] + buf[79]        ... STAR(1) ITOC(0) ... STAR(1) ITOC(0) PLUS(0)
 *   return buf[0];          ... STAR(1) ITOC(0) RFORCE(0)
 *   t = *a;  (both char)    ... STAR(1) ASSIGN(1)            (none)
 *   c = (char) i;           NAME(i) ITOC(1)
 *   l = (long) c;           NAME(c) CTOL(6)
 *
 * (09_abiprobe/0N_frameNNN, 06_struct/06_union, 10_integ/04_strrev,
 * 01_expr/08_castsize .1.goldens.) So:
 *
 * - an operand of a binary arithmetic, shift, bitwise, relational or
 *   equality operator that is a char is widened first - BOTH operands
 *   of "c + c" (promote_char()) - except a char object compared with a
 *   constant in 0..127, where the constant is typed char instead
 *   (char_compare_rhs() - 10_integ/01_wordcount.1.golden);
 * - an assignment converts its right-hand side to the target's type
 *   (convert_assign()): int -> char is ITOC(TY_CHAR), char -> int
 *   ITOC(TY_INT), char -> long CTOL, char -> char nothing. v7's build()
 *   runs the SAME conversion code for a cast and an assignment ("if
 *   (dope&ASSGOP || op==CAST)"), which is why "(char) i" and "(long) c"
 *   confirm the assignment conversions too, and doret() returns through
 *   an assignment to the function's type ("return buf[0];" in an int
 *   function: ITOC(TY_INT)).
 *
 * Where v7 applies no conversion at all - a condition, an operand of
 * '&&'/'||'/'!'/'~'/'?:', a call argument - the MUTOS stream shows none
 * in any golden either way, and mutos_c1 would need a byte-sized test
 * or push no golden shows; char_value_refused() stops there with an
 * explicit "not yet supported" instead of guessing. */

/* Widens a char operand to int (OP_ITOC with type TY_INT), written right
 * after the operand's own bytes - so for a binary operator's LEFT
 * operand it must be called before the right operand is parsed. A no-op
 * for anything that is not a char. */
static ExprVal promote_char(FILE *t1, ExprVal v)
{
    if (!v.is_const && ty_is_float(v.type)) {
        /* Every int-class operator's operand passes through here (the
         * shifts, bitwise and relational operators, '%', a subscript
         * index, a compound assignment); a floating one has no
         * conversion, instruction shape or runtime call confirmed by
         * any golden there - see "Floating point" below. */
        c0_error_at(diag_line, "a 'float'/'double' operand of this operator is "
                               "not yet supported (only '+', '-', '*', '/', "
                               "'=', casts and 'return') - see "
                               "src/mutos_cc/README.md");
        return ev_dynamic();
    }
    if (v.is_const || v.type != TY_CHAR)
        return v;
    outcode(t1, "BN", OP_ITOC, TY_INT);
    return ev_dynamic();
}

/* The type of an int-class binary operator's result - v7/cc/c01.c's
 * build(): long if either operand is, else unsigned if either operand is
 * ("if ((t==INT||t==CHAR) && (t1==UNSIGN||t2==UNSIGN)) t = UNSIGN;"),
 * else int. The only unsigned values this front end makes are bit-field
 * members (06_struct/07_bitfield.1.golden: "f.ready + f.mode" is
 * PLUS(7)); an operator whose code depends on the signedness - '/',
 * '%', '>>', an ordered comparison (v7's LESSP...) - refuses one
 * (unsigned_refused()). */
static int arith_type(ExprVal a, ExprVal b)
{
    if (a.type == TY_LONG || b.type == TY_LONG)
        return TY_LONG;
    if (a.type == TY_UNSIGN || b.type == TY_UNSIGN)
        return TY_UNSIGN;
    return TY_INT;
}

/* A 'long' combined with an int-class VARIABLE operand: v7's build()
 * converts that operand (ITOL - zero-extending an unsigned one), which
 * this front end does not write yet - so mutos_c1 read the int as the
 * first word of a long (found 2026-10-02: "l + i" added the two words at
 * i's address). A constant operand is fine (mutos_c1 widens it). */
static int long_mix_refused(ExprVal a, ExprVal b, int line)
{
    int al = (a.type == TY_LONG), bl = (b.type == TY_LONG);
    if (al == bl)
        return 0;
    ExprVal o = al ? b : a;
    if (o.is_const)
        return 0;
    c0_error_at(line, "a 'long' combined with an int, char or unsigned "
                      "variable is not yet supported - see "
                      "src/mutos_cc/README.md");
    return 1;
}

static int unsigned_refused(ExprVal a, ExprVal b, int line, const char *op)
{
    if (a.type != TY_UNSIGN && b.type != TY_UNSIGN)
        return 0;
    c0_error_at(line, "'%s' with an unsigned operand is not yet supported - "
                      "see src/mutos_cc/README.md", op);
    return 1;
}

/* Reports a char value in a context whose conversion (if any) no golden
 * confirms - see this section's header - and, likewise, a floating one
 * (see "Floating point" below). Returns 1 if it did. */
static int char_value_refused(ExprVal v, int line, const char *ctx)
{
    if (!v.is_const && ty_is_float(v.type)) {
        c0_error_at(line, "a 'float'/'double' value used as %s is not yet "
                          "supported - see src/mutos_cc/README.md", ctx);
        return 1;
    }
    if (v.is_const || v.type != TY_CHAR)
        return 0;
    c0_error_at(line, "a 'char' value used as %s is not yet supported - see "
                      "src/mutos_cc/README.md", ctx);
    return 1;
}

/* A char used where v7's build() converts nothing - a condition, an
 * operand of '&&'/'||'/'!', a call argument (COMMA and CALL are among
 * build()'s "no-conversion operators"): written unconverted, as v7 does,
 * when it is a char OBJECT as written (a char variable's NAME, a char
 * element's or dereference's STAR - ExprVal's char_obj), which mutos_c1
 * tests or widens as a byte from memory - "cmpb *1.(si),*0", "movb
 * ax,*-8.(bp)" / "cbw" / "push ax" in tests/mutos_as/kernel_nonopt/ (the
 * real compiler's output; no golden has either shape). Any other
 * char-typed value - a "(char) i" cast's, a char assignment's - would be
 * a byte in a register, whose high half a word test or push would read:
 * still refused. Returns 1 if it reported one. */
static int char_nonobj_refused(ExprVal v, int line, const char *ctx)
{
    if (v.char_obj)
        return 0;
    return char_value_refused(v, line, ctx);
}

/* ------------------------------------------------------------------ */
/* Floating point (tests/mutos_cc/08_float).
 *
 * 'float'/'double' local variables (4 and 8 bytes in the frame),
 * floating constants, '+', '-', '*', '/', assignment, casts and
 * 'return', as v7/cc/c01.c's build() writes them - confirmed byte-for-
 * byte against 08_float/01_floatbas.1.golden and 02_dblconv.1.golden:
 *
 *   a = 3.5;          NAME(a, FLOAT) FCON(DOUBLE, "3.5") ASSIGN(DOUBLE)
 *   c = a + b;        NAME(c) NAME(a) NAME(b) PLUS(DOUBLE) ASSIGN(DOUBLE)
 *   return (int) c;   NAME(c, FLOAT) FTOI(INT) RFORCE(INT)
 *   d = i;            NAME(d, DOUBLE) NAME(i, INT) ITOF(DOUBLE) ASSIGN(DOUBLE)
 *   d = d / 2.0;      NAME(d) NAME(d) FCON(DOUBLE, "2.0") DIVIDE(DOUBLE) ...
 *   l = (long) d;     NAME(l) NAME(d) FTOL(LONG) ASSIGN(LONG)
 *   i = (int) d;      NAME(i) NAME(d) FTOI(INT) ASSIGN(INT)
 *
 * - An operator or assignment over floating values is TY_DOUBLE, even
 *   between two floats and even into a float ("if (t==FLOAT) t =
 *   DOUBLE"); only a NAME keeps TY_FLOAT. float and double convert to
 *   each other with no node at all (v7's cvtab[] has one "double" row
 *   for both, lintyp()).
 * - FCON carries the constant's source text ("BNF" - c0_outcode.h's
 *   'F') and is never folded: v7's fold() folds integer CONs only.
 * - The conversions come from cvtab[] (v7/cc/c05.c): an int becomes
 *   floating with ITOF, a long with LTOF, a floating value an int with
 *   FTOI and a long with FTOL. The new node's type is the one convert()
 *   is given: an assignment's or cast's target type (so "a = i;" into a
 *   float writes ITOF(FLOAT), and "d = i;" ITOF(DOUBLE) - the golden),
 *   or, for '+'..., the floating operand's own type.
 * - doret() returns through an assignment to the function's type, so
 *   "return c;" in an int function writes FTOI(INT) - the same stream as
 *   the golden's "return (int) c;".
 *
 * Everything else with a floating operand - comparisons, conditions,
 * '%', unary operators, '++'/'--', compound assignment, a call
 * argument, a char operand, a floating parameter, global, array,
 * pointer or struct member - is refused explicitly (promote_char(),
 * char_value_refused()): no golden shows the real compiler's shape. */

/* Converts an already-emitted int or long value `v` to the floating type
 * `ft` (TY_FLOAT or TY_DOUBLE - see this section's header for which);
 * nothing for a value that is floating already. */
static void emit_to_float(FILE *t1, int ft, ExprVal v, int line)
{
    if (ty_is_float(v.type) && !v.is_const)
        return;
    if (v.type == TY_LONG) {
        outcode(t1, "BN", OP_LTOF, ft);
        return;
    }
    if (v.type == TY_INT) {
        outcode(t1, "BN", OP_ITOF, ft);
        return;
    }
    if (v.type == TY_CHAR && !v.is_const) {
        /* A char widened first, as for any int operator (ITOC(INT)),
         * then converted: fltprobe/p5_call.1.golden's "d = c" - NAME c
         * (CHAR), ITOC(INT), ITOF(DOUBLE). */
        outcode(t1, "BN", OP_ITOC, TY_INT);
        outcode(t1, "BN", OP_ITOF, ft);
        return;
    }
    if (v.type == TY_UNSIGN && !v.is_const) {
        /* An unsigned value: ITOF too - fltprobe/p16_open2.1.golden's "d =
         * u" (NAME u typed UNSIGN, ITOF(DOUBLE)); v7's c1 makes it
         * LTOF(ITOL) (see mutos_c1's gen_itof()). */
        outcode(t1, "BN", OP_ITOF, ft);
        return;
    }
    c0_error_at(line, "converting a '%s' value to 'float'/'double' is not yet "
                      "supported - see src/mutos_cc/README.md",
                v.type == TY_CHAR ? "char" : v.type == TY_UNSIGN ? "unsigned"
                                                               : "non-integer");
}

/* Converts an already-emitted floating value to the integer type
 * `target`: FTOI to int, FTOL to long. */
static void emit_from_float(FILE *t1, int target, int line)
{
    if (target == TY_INT) {
        outcode(t1, "BN", OP_FTOI, TY_INT);
    } else if (target == TY_LONG) {
        outcode(t1, "BN", OP_FTOL, TY_LONG);
    } else if (target == TY_CHAR) {
        /* Through an int: fltprobe/p5_call.1.golden's "c = d" - FTOI(INT),
         * ITOC(CHAR). */
        outcode(t1, "BN", OP_FTOI, TY_INT);
        outcode(t1, "BN", OP_ITOC, TY_CHAR);
    } else {
        c0_error_at(line, "converting a 'float'/'double' value to this type "
                          "is not yet supported (only to 'int', 'long' and "
                          "'char') - see src/mutos_cc/README.md");
    }
}

/* The type an assignment of type `t` is written with: a float target's
 * ASSIGN is TY_DOUBLE (see this section's header). */
static int assign_wire_type(int t)
{
    return ty_is_float(t) ? TY_DOUBLE : t;
}

/* Converts an already-emitted right-hand side to the type of the
 * assignment target `lhs_type` - see the "'char' conversions" section
 * above and this one. `rhs` must already be materialized. The '=' form
 * only: a compound assignment ("c += 1", "i += c") is left alone by the
 * caller. */
static void convert_assign(FILE *t1, int lhs_type, ExprVal rhs, int line)
{
    if (ty_is_float(lhs_type)) {
        emit_to_float(t1, lhs_type, rhs, line);
        return;
    }
    if (ty_is_float(rhs.type) && !rhs.is_const) {
        emit_from_float(t1, lhs_type, line);
        return;
    }
    if (lhs_type == TY_CHAR) {
        if (rhs.type == TY_CHAR && !rhs.is_const)
            return;                          /* char = char: none */
        if (rhs.type == TY_LONG || ty_is_ptr(rhs.type)) {
            c0_error_at(line, "storing a '%s' value into a 'char' is not yet "
                              "supported - see src/mutos_cc/README.md",
                        rhs.type == TY_LONG ? "long" : "pointer");
            return;
        }
        outcode(t1, "BN", OP_ITOC, TY_CHAR);
        return;
    }
    if (lhs_type == TY_LONG) {
        /* An int-typed value assigned to a 'long' needs an explicit
         * widening conversion first - confirmed against 02_long/
         * 01_addsub.1.golden's "b = 23456;" (CON, then ITOL(TY_LONG),
         * then ASSIGN); a char one goes straight to long with the
         * MUTOS-specific CTOL, as "(long) c" does (08_castsize). */
        if (rhs.type == TY_LONG)
            return;
        if (rhs.type == TY_UNSIGN && !rhs.is_const) {
            /* v7's ITOL of an unsigned clears the high word (p16_open2's
             * "mov si,u" / "sub di,di"); mutos_c1's ITOL sign-extends. */
            c0_error_at(line, "assigning an 'unsigned' value to a 'long' is "
                              "not yet supported - see src/mutos_cc/README.md");
            return;
        }
        if (rhs.type == TY_CHAR && !rhs.is_const)
            outcode(t1, "BN", OP_CTOL, TY_LONG);
        else
            outcode(t1, "BN", OP_ITOL, TY_LONG);
        return;
    }
    if (ty_is_ptr(lhs_type)) {
        (void)char_value_refused(rhs, line, "a pointer's new value");
        return;
    }
    if (rhs.type == TY_LONG) {
        /* An int or unsigned target of a long value: v7's build() converts
         * it, LTOI - the conversion "(int) l" writes (08_castsize.1.golden:
         * NAME l, LTOI(0)). A long constant ("x = 40000;") has no golden:
         * refused (mutos_c1 cannot store one into a word). Until
         * 2026-10-02 neither was converted at all - "x = l" stored l's
         * high word. */
        if (rhs.is_const) {
            c0_error_at(line, "assigning a 'long' constant to an int or "
                              "unsigned variable is not yet supported - see "
                              "src/mutos_cc/README.md");
            return;
        }
        outcode(t1, "BN", OP_LTOI, lhs_type == TY_UNSIGN ? TY_UNSIGN : TY_INT);
        return;
    }
    (void)promote_char(t1, rhs);             /* int = char */
}

/* ------------------------------------------------------------------ */
/* Keeping a pending constant LEFT operand on the left.
 *
 * temp1 is postfix: a binary node's left operand's bytes, then its
 * right operand's, then the node. A left operand that is still an
 * unmaterialized constant (kept so it can fold with a constant right
 * operand - see ExprVal) has written nothing yet when the right operand
 * is parsed, and the right operand writes its bytes as it is parsed.
 * Materializing the constant only once the operator is reached would
 * put it AFTER the right operand - "7 - x" as "x - 7" - which is what
 * this file did for every binary operator until 2026-09-24 (found by
 * the semantic fuzz check - see docs/DEVLOG.md). v7 never reorders
 * operands in c0 (v7/cc/c01.c's fold() folds only when BOTH are
 * constants), so the real stream for "7 - x" is CON 7, NAME x, MINUS.
 *
 * So while a constant left operand is pending, the right operand is
 * parsed into a memory buffer instead (rhs_begin()); rhs_end() then
 * either leaves everything to the caller's fold (the right operand is
 * a constant too, and wrote nothing), or writes the left constant's CON
 * first and the buffered right operand after it. The same
 * open_memstream() technique parse_for_stmt()'s deferred increment
 * uses. */
typedef struct {
    FILE  *mem;   /* NULL: no capture (the left operand was not constant) */
    char  *buf;
    size_t len;
} RhsCapture;

/* Returns the stream the right operand must be parsed into. */
static FILE *rhs_begin(RhsCapture *c, Parser *p, FILE *t1, ExprVal lhs)
{
    c->mem = NULL;
    c->buf = NULL;
    c->len = 0;
    if (!lhs.is_const)
        return t1;
    c->mem = open_memstream(&c->buf, &c->len);
    if (!c->mem) {
        c0_error_at(p->cur.line, "internal: could not buffer an operand "
                    "(out of memory)");
        return t1;
    }
    return c->mem;
}

/* Ends rhs_begin()'s capture and returns the left operand as the caller
 * must now treat it: unchanged when the right operand is a constant too
 * (the caller folds the pair), otherwise already emitted - its CON is
 * written ahead of the right operand's buffered bytes. */
static ExprVal rhs_end(RhsCapture *c, Parser *p, FILE *t1, ExprVal lhs,
                       ExprVal rhs)
{
    if (!c->mem)
        return lhs;
    fclose(c->mem);
    if (rhs.is_const) {
        /* A constant writes nothing (see ExprVal) - anything else would
         * be a pending-constant bookkeeping bug in this file. */
        if (c->len != 0)
            c0_error_at(p->cur.line, "internal: a constant operand wrote "
                        "intermediate code");
        free(c->buf);
        return lhs;
    }
    emit_materialize(t1, lhs);
    fwrite(c->buf, 1, c->len, t1);
    free(c->buf);
    lhs.is_const = 0; /* emitted - keeps its type */
    return lhs;
}

/* Ends a capture by writing whatever it buffered to `t1` as is - for
 * '?:', which places its pending constants itself (parse_expr()). */
static void capture_flush(RhsCapture *c, FILE *t1)
{
    if (!c->mem)
        return;
    fclose(c->mem);
    fwrite(c->buf, 1, c->len, t1);
    free(c->buf);
    c->mem = NULL;
}

/* Starts buffering an operand unconditionally (rhs_begin() does so only
 * behind a pending constant) - for char_compare_rhs(), which decides
 * what goes in front of the right operand only once it is parsed.
 * Ended with capture_flush(). */
static FILE *capture_begin(RhsCapture *c, Parser *p, FILE *t1)
{
    c->buf = NULL;
    c->len = 0;
    c->mem = open_memstream(&c->buf, &c->len);
    if (!c->mem) {
        c0_error_at(p->cur.line, "internal: could not buffer an operand "
                    "(out of memory)");
        return t1;
    }
    return c->mem;
}

/* rhs_begin() for '+', '-', '*' and '/', whose right operand may turn out
 * floating: then an integer left operand needs its conversion (ITOF/
 * LTOF) written right after its own bytes, ahead of the right operand's
 * - v7's build() converts the left operand ("leftc") for int + double.
 * So the right operand is buffered behind any non-floating left operand,
 * not only a pending constant; float_arith() ends the capture. For an
 * all-integer operator the bytes written are the same either way. */
static FILE *arith_rhs_begin(RhsCapture *c, Parser *p, FILE *t1, ExprVal lhs)
{
    if (lhs.is_const || ty_is_float(lhs.type))
        return rhs_begin(c, p, t1, lhs);
    return capture_begin(c, p, t1);
}

/* '+', '-', '*' or '/' (`op`) once its right operand `r` has been parsed
 * into arith_rhs_begin()'s stream. When either operand is floating,
 * writes what is still missing - the left operand's pending constant,
 * the integer operand's conversion (to the floating operand's own type:
 * v7's "t = leftc? t2: t1"), the buffered right operand, the operator
 * node typed DOUBLE - and returns 1 with `*v` the result (see "Floating
 * point"). Otherwise ends the capture as rhs_end() does, updating `*v`
 * the same way, and returns 0: the caller carries on with its integer
 * code. `lchar`: the left operand was a char before promote_char(). */
static int float_arith(Parser *p, FILE *t1, RhsCapture *cap, ExprVal *v,
                       ExprVal r, int lchar, int op, int line)
{
    int lf = ty_is_float(v->type) && !v->is_const;
    int rf = ty_is_float(r.type) && !r.is_const;
    if (!lf && !rf) {
        *v = rhs_end(cap, p, t1, *v, r);
        return 0;
    }
    if ((rf && lchar) || (lf && r.type == TY_CHAR && !r.is_const))
        c0_error_at(line, "a 'char' operand combined with a 'float'/'double' "
                          "one is not yet supported - see "
                          "src/mutos_cc/README.md");
    if (rf && !lf) {
        /* The left operand is an integer: its CON (if still pending),
         * its conversion, then the buffered right operand. */
        if (cap->mem) {
            fclose(cap->mem);
            cap->mem = NULL;
        }
        emit_materialize(t1, *v);
        emit_to_float(t1, r.type, *v, line);
        if (cap->buf) {
            fwrite(cap->buf, 1, cap->len, t1);
            free(cap->buf);
            cap->buf = NULL;
        }
    } else if (lf && !rf) {
        /* The right operand is an integer, written (or still pending)
         * straight after the left one - nothing was buffered. */
        emit_materialize(t1, r);
        emit_to_float(t1, v->type, r, line);
    }
    outcode(t1, "BN", op, TY_DOUBLE);
    *v = ev_dynamic_typed(TY_DOUBLE);
    return 1;
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
        /* The scale is the pointed-to object's size (v7/cc/c01.c's
         * build(): convert(..., ITP, plength(p1))) - MCC_SZINT for
         * every pointer 05_incdec confirms; 1 for a "char *", which
         * mutos_c1 does not accept yet. */
        outcode(t1, "BNN", OP_CON, TY_INT, size_of_type(ty_decref(type)));
        outcode(t1, "BN", OP_ITOP, type);
    }
    outcode(t1, "BN", optag, type);
}

/*
 * putstr() - v7/cc/c00.c's function of the same name, in both of its
 * forms, writing to `dst`:
 *
 * - a string literal in an expression (a non-zero `lab`): the bytes go
 *   to TEMP2, never temp1 - v7's putstr() sets `strflg`, which makes
 *   outcode() write to the string file for the duration - as
 *
 *     LABEL <lab>  BDATA  (1 <byte>)...  (1 0)  0
 *
 * - the initializer of a file-scope "char name[] = \"...\";" (`lab` 0 -
 *   v7's cinit() calls putstr(0, flex ? 10000 : nel)): the same runs
 *   without the LABEL, written where the declaration is, into temp1
 *   (strflg is not set) - BDATA (1 <byte>)... (1 0) 0 - confirmed
 *   byte-for-byte against 10_integ/01_wordcount.1.golden's "char
 *   text[] = \"the quick brown fox\\njumps over the lazy dog\\n\";":
 *   three runs of 14, 15 and 16 values (the last one ending in the
 *   NUL) straight after NLABEL "_text". Only the flexible "[]" form is
 *   supported (see parse_global_chararray()), so `max` is always
 *   v7's 10000 here too.
 *
 * Returns the number of bytes written, the NUL included (v7's
 * `nchstr` - the array's size in the initializer form).
 *
 * In both forms each byte is the pair (1, value) and the terminating
 * NUL one more pair (1, 0), the lone 0 ending the BDATA run. The
 * labelled form is confirmed byte-for-byte against
 * 05_arrptr/05_arrofptr.2.golden ("one", "two", "three" - three
 * consecutive runs, labels 4/5/6) and 05_arrptr/07_strlibc.2.golden
 * ("hello, mutos"), plus 10_integ/04_strrev.2.golden ("mutos1700").
 *
 * Before the 15th, 30th, ... byte the run is closed and a new one
 * opened ("0 BDATA") - "if (nchstr%15 == 0) outcode(\"0B\", BDATA);" -
 * so a run holds 14 bytes, then 15 per run; the NUL is appended without
 * that check. This came from v7's source first; the real MUTOS c1
 * output in tests/mutos_as/kernel_nonopt/ and kernel_opt/ confirmed it
 * indirectly (all 208 string blocks there have exactly the .byte line
 * layout this split predicts - see mutos_c1's put_bdata_run()), and
 * 01_wordcount.1.golden now confirms it directly (runs of 14, 15, 16).
 * Bytes beyond the 10000th are dropped (v7's `max`), the NUL too once
 * that limit is reached - from v7's source only. Each byte is masked to
 * 0..255 ("c & 0377") - the lexer already did that (c0_lex.c's
 * lex_string()).
 */
#define MCC_STRRUN 15      /* v7 putstr()'s run length */
#define MCC_STRMAX 10000   /* v7 putstr()'s `max` for a labelled string
                            * or a flexible "char name[]" initializer */
static size_t putstr(FILE *dst, int lab, const char *str, size_t len)
{
    if (lab)
        outcode(dst, "BNB", OP_LABEL, lab, OP_BDATA);
    else
        outcode(dst, "B", OP_BDATA);
    size_t nch = 0;
    for (size_t i = 0; i < len; i++) {
        if (nch >= MCC_STRMAX)
            continue;
        nch++;
        if (nch % MCC_STRRUN == 0)
            outcode(dst, "0B", OP_BDATA);
        outcode(dst, "1N", (int)(unsigned char)str[i]);
    }
    if (nch < MCC_STRMAX) {
        nch++;
        outcode(dst, "10");
    }
    outcode(dst, "0");
    return nch;
}

static ExprVal parse_expr(Parser *p, FILE *t1);
static ExprVal parse_unary(Parser *p, FILE *t1);
static void parse_statement(Parser *p, FILE *t1, int retlab);
static void parse_compound_stmt(Parser *p, FILE *t1, int retlab);
static void parse_nested_block(Parser *p, FILE *t1, int retlab);

/*
 * The 2-D case of emit_subscript() below: "m[i][j]" on a local
 * declared "int m[N][M];" (SymEntry.dim2 = M). Called with the first
 * '[' already consumed; consumes "idx1 ']' '[' idx2 ']'" and emits
 * the full two-step address computation plus the final dereference.
 * Confirmed byte-for-byte against 05_arrptr/02_array2d.1.golden (both
 * indices runtime) and 10_integ/05_matmul.1.golden (both indices
 * constant, e.g. "a[0][1] = 2;"):
 *
 *   NAME(auto, TY_INT, m)  AMPER(8)  <idx1>  CON(M*2)  ITOP(104)
 *   PLUS(8)  STAR(0)  AMPER(8)  <idx2>  CON(2)  ITOP(8)  PLUS(8)  STAR(0)
 *
 * - i.e. exactly two ordinary 1-D subscript steps, the first one's
 * result (a row, "int[M]") immediately decayed again by an AMPER.
 * The one irregular-looking value, the first ITOP's type 104
 * ("pointer to array of int" = ty_ptr_of(ty_ary_of(TY_INT))) while
 * every AMPER/PLUS/STAR around it is flat TY_PTR_INT/TY_INT, is fully
 * explained by v7/cc/c01.c: build() types the first step as a genuine
 * pointer-to-row (AMPER/ITOP/PLUS all 104, STAR 24 = "array of
 * int"), then disarray() - decaying that row for the second '[' -
 * calls setype(), which retypes the node chain it walks (STAR -> PLUS
 * -> AMPER -> NAME, always via tr1, the LEFT operand) down to the
 * element level, but never visits PLUS's right operand, so the ITOP
 * conversion node keeps its original 104. The same derivation
 * predicts every further dimension's ITOP carries "pointer to the
 * remaining sub-array", but no golden confirms a 3-D shape - see
 * parse_decl()'s rejection of a third dimension.
 *
 * Only the fully-subscripted form is supported: a bare "m" or a
 * half-subscripted "m[i]" (both decay to a pointer to a row, a type
 * this grammar scope cannot carry further - see parse_primary()) is an
 * explicit error.
 */
static int emit_subscript_2d(Parser *p, FILE *t1, SymEntry *sym)
{
    int rowptr = ty_ptr_of(ty_ary_of(TY_INT)); /* 104 */
    emit_name(t1, sym, TY_INT);
    outcode(t1, "BN", OP_AMPER, TY_PTR_INT);
    ExprVal row = parse_expr(p, t1);
    expect(p, T_RBRACK, "']'");
    emit_materialize(t1, row);
    (void)promote_char(t1, row);
    outcode(t1, "BNN", OP_CON, TY_INT, sym->dim2 * MCC_SZINT);
    outcode(t1, "BN", OP_ITOP, rowptr);
    outcode(t1, "BN", OP_PLUS, TY_PTR_INT);
    outcode(t1, "BN", OP_STAR, TY_INT);
    if (p->cur.kind != T_LBRACK) {
        c0_error_at(p->cur.line, "a partially-subscripted 2-D array "
                                  "(\"m[i]\" used as a pointer to a row) is "
                                  "not yet supported - see "
                                  "src/mutos_cc/README.md");
        return TY_INT;
    }
    advance(p); /* second '[' */
    outcode(t1, "BN", OP_AMPER, TY_PTR_INT);
    ExprVal col = parse_expr(p, t1);
    expect(p, T_RBRACK, "']'");
    emit_materialize(t1, col);
    (void)promote_char(t1, col);
    outcode(t1, "BNN", OP_CON, TY_INT, MCC_SZINT);
    outcode(t1, "BN", OP_ITOP, TY_PTR_INT);
    outcode(t1, "BN", OP_PLUS, TY_PTR_INT);
    outcode(t1, "BN", OP_STAR, TY_INT);
    return TY_INT;
}

/*
 * A 'long' element read or written through a subscript or a pointer is
 * refused, although the address computation itself is known: no golden
 * shows a two-word load or store through an address, nor which
 * conversions the real front end wraps around one. (A 'char' element
 * used to be refused here too, until its conversions - see
 * promote_char()/convert_assign() - and mutos_c1's byte loads and
 * stores were implemented against the 09_abiprobe frame goldens.) A
 * 'long *' element is a word and is not affected. */
static void refuse_long_access(Parser *p)
{
    c0_error_at(p->cur.line, "reading or writing a 'long' through a subscript "
                              "or pointer is not yet supported - see "
                              "src/mutos_cc/README.md");
}

/*
 * Emits the address computation and final dereference for exactly
 * one subscript step "sym[idx-expr]" - `sym` must be an array or a
 * plain pointer variable, and its own NAME node must NOT have been
 * emitted yet (this function emits it). Assumes p->cur.kind ==
 * T_LBRACK on entry and consumes '[' idx-expr ']'.
 *
 * Two shapes, matching K&R's array-vs-pointer subscript semantics
 * exactly - confirmed against 05_arrptr/01_arrbasic.1.golden ("a[i]",
 * `a` an array: NAME/AMPER decay first, same as a bare array-as-
 * rvalue - see parse_primary()'s existing is_array case above) and
 * 05_arrptr/04_ptrarreq.1.golden ("*(a + i)", `a` a plain pointer
 * parameter: no AMPER at all, its NAME's own value IS the pointer -
 * this second shape is also reached directly by parse_deref() below
 * for the explicit-'*' spelling, not just via this function). A 2-D
 * array (SymEntry.dim2 > 0) is handed off to emit_subscript_2d()
 * above, which consumes both subscripts.
 */
static int emit_subscript(Parser *p, FILE *t1, SymEntry *sym)
{
    advance(p); /* '[' */
    int elemtype, ptrtype;
    if (sym->is_array && sym->dim2 > 0)
        return emit_subscript_2d(p, t1, sym);
    if (sym->is_array) {
        emit_name(t1, sym, sym->type);
        elemtype = sym->type;
        ptrtype = ty_ptr_of(elemtype);
        outcode(t1, "BN", OP_AMPER, ptrtype);
    } else {
        emit_name(t1, sym, sym->type);
        ptrtype = sym->type;
        elemtype = ty_decref(ptrtype);
    }
    if (elemtype == TY_LONG)
        refuse_long_access(p);
    ExprVal idx = parse_expr(p, t1);
    expect(p, T_RBRACK, "']'");
    emit_materialize(t1, idx);
    (void)promote_char(t1, idx); /* v7's build(PLUS): a char index is
                                   * widened like any other operand */
    outcode(t1, "BNN", OP_CON, TY_INT, size_of_type(elemtype));
    outcode(t1, "BN", OP_ITOP, ptrtype);
    outcode(t1, "BN", OP_PLUS, ptrtype);
    outcode(t1, "BN", OP_STAR, elemtype);
    return elemtype;
}

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
        SymEntry *sym = lookup_var(p, name);
        advance(p); /* consume IDENT */
        advance(p); /* consume '=' */

        if (!sym) {
            c0_error_at(line, "'%s' undeclared", name);
        } else {
            emit_name(t1, sym, sym->type);
        }

        ExprVal rhs = parse_expr(p, t1);
        emit_materialize(t1, rhs);
        if (sym)
            convert_assign(t1, sym->type, rhs, line);
        outcode(t1, "BN", OP_ASSIGN, sym ? assign_wire_type(sym->type) : TY_INT);
        /* The assignment's value has the target's type (v7's build():
         * "t = t1" for an assignment operator). */
        return ev_dynamic_typed(sym ? sym->type : TY_INT);
    }
    if (p->cur.kind == T_IDENT) {
        /* An embedded floating compound assignment whose value is used,
         * "e = (d *= e)" (fltprobe/p2_arith.1.golden: NAME d, NAME e,
         * ASTIMES(DOUBLE), then the outer ASSIGN) - a float or double
         * variable as the target, as parse_assign_stmt() takes one. */
        TokKind k2 = peek2_kind(p);
        int optag = k2 == T_STAREQ ? OP_ASTIMES : k2 == T_SLASHEQ ? OP_ASDIV :
                    k2 == T_PLUSEQ ? OP_ASPLUS : k2 == T_MINUSEQ ? OP_ASMINUS : 0;
        SymEntry *sym = optag ? lookup_var(p, p->cur.ident) : NULL;
        if (sym && ty_is_float(sym->type) && !sym->is_array) {
            int line = p->cur.line;
            advance(p); /* consume IDENT */
            advance(p); /* consume the operator */
            emit_name(t1, sym, sym->type);
            ExprVal rhs = parse_expr(p, t1);
            emit_materialize(t1, rhs);
            emit_to_float(t1, sym->type, rhs, line);
            outcode(t1, "BN", optag, TY_DOUBLE);
            return ev_dynamic_typed(sym->type);
        }
    }
    return parse_expr(p, t1);
}

/*
 * call-expr := IDENT '(' (expr (',' expr)*)? ')'
 *
 * A direct function call. The callee is referenced by a
 * NAME(SC_EXTERN, TY_FUNC_INT, name) leaf - K&R's implicit "extern
 * function returning int" declaration: no prior declaration or
 * prototype is required (matching real K&R semantics), and this
 * grammar scope never looks the name up in the local (AUTO) symbol
 * table for a call - confirmed against every 04_funcs .1.golden's
 * callee NAME. The argument list is built exactly like the
 * parenthesized comma-list in parse_primary()'s T_LPAREN case below
 * (a left-associative chain of COMMA(TY_INT) nodes, one per argument
 * beyond the first), EXCEPT that every argument is materialized
 * immediately as it is parsed (never left as a still-foldable
 * ExprVal constant, unlike that comma-list's own final item) - each
 * argument must become a genuine part of the call's tree the moment
 * it is parsed, matching real K&R's per-argument build() during
 * call parsing. Confirmed against 02_manyargs.1.golden's six-CON-
 * plus-five-COMMA argument list and against 03_recfact.1.golden's
 * "fact(6)" (a single, immediately-materialized CON(6), no COMMA at
 * all). Zero arguments emits a single NULLOP leaf - v7/cc/c04.c's
 * treeout(NULL) shape (an empty argument-list tree is a null
 * pointer there) - confirmed against 05_staticvar.1.golden's
 * "counter()". The call's own result type is always TY_INT - no
 * function with a different return type is exercised by this
 * grammar scope yet (every function definition is still implicitly
 * int-returning - see cfunc()).
 */

/* Registers `name` as a known function (a definition or a
 * prototype), so a later bare reference to it (parse_primary()'s
 * T_IDENT fallback below) can be recognized as "the address of this
 * function" rather than an undeclared-identifier error - see
 * MCC_MAXFUNCS's own comment on the Parser struct. A no-op (not an
 * error) if already registered - a function's own definition and an
 * earlier prototype for it both call this, and registering twice is
 * harmless. */
static void register_func(Parser *p, const char *name, int type)
{
    for (int i = 0; i < p->nfuncnames; i++)
        if (strncmp(p->funcnames[i], name, LEX_IDENT_MAX) == 0)
            return; /* already registered - keep its original type;
                      * no golden exercises a conflicting re-
                      * declaration, matching v7's own "first mention
                      * wins" behavior. */
    if (p->nfuncnames >= MCC_MAXFUNCS) {
        c0_error_at(p->cur.line,
            "too many distinct function names (internal limit %d)",
            MCC_MAXFUNCS);
        return;
    }
    snprintf(p->funcnames[p->nfuncnames], LEX_IDENT_MAX, "%s", name);
    p->functypes[p->nfuncnames] = type;
    p->nfuncnames++;
}

static int is_known_func(Parser *p, const char *name)
{
    for (int i = 0; i < p->nfuncnames; i++)
        if (strncmp(p->funcnames[i], name, LEX_IDENT_MAX) == 0)
            return 1;
    return 0;
}

/* Returns a previously-register_func()'d function's own return type,
 * or TY_INT (K&R's implicit-int default) for a name not yet seen -
 * a forward call to a function defined later in the file, or a
 * genuinely undeclared one. Consulted by parse_call() so a call to a
 * 'long'-returning function (02_long/03_retval.c's "addlong") emits
 * OP_CALL with the right type instead of the previous hardcoded
 * TY_INT. */
static int lookup_func_type(Parser *p, const char *name)
{
    for (int i = 0; i < p->nfuncnames; i++)
        if (strncmp(p->funcnames[i], name, LEX_IDENT_MAX) == 0)
            return p->functypes[i];
    return TY_INT;
}

/*
 * arg-list-and-call := '(' (expr (',' expr)*)? ')'
 *
 * Shared tail end of both parse_call() (direct call, callee already
 * emitted as a NAME) and parse_indirect_call() (indirect call,
 * callee already emitted as NAME+STAR) - see each of their own
 * comments for the confirmed argument-list/NULLOP/CALL shape this
 * implements. The caller has already consumed the opening '(' and
 * emitted the callee expression; this function consumes everything
 * from the first argument (if any) through the closing ')' and the
 * trailing CALL opcode itself.
 */
static void parse_call_args_and_emit(Parser *p, FILE *t1, int ret_type)
{
    if (p->cur.kind == T_RPAREN) {
        outcode(t1, "B", OP_NULLOP);
    } else {
        /* A char argument: v7 converts no call argument (the argument
         * list is built with COMMA, a no-conversion operator), so a char
         * object is written as is and mutos_c1 widens it as it pushes it
         * ("movb ax,*-8.(bp)" / "cbw" / "push ax" - the real compiler's
         * output in tests/mutos_as/kernel_nonopt/lp_AC.s); any other
         * char-typed value is refused - see char_nonobj_refused(). */
        /* A floating argument is written as it is too - v7 converts
         * none, and mutos_c1 pushes it as a double (see "Floating
         * point"). */
        int line = p->cur.line;
        ExprVal v = parse_expr(p, t1);
        emit_materialize(t1, v);
        if (!ty_is_float(v.type))
            (void)char_nonobj_refused(v, line, "a call argument");
        while (p->cur.kind == T_COMMA) {
            advance(p);
            line = p->cur.line;
            ExprVal rhs = parse_expr(p, t1);
            emit_materialize(t1, rhs);
            if (!ty_is_float(rhs.type))
                (void)char_nonobj_refused(rhs, line, "a call argument");
            outcode(t1, "BN", OP_COMMA, TY_INT);
        }
    }
    expect(p, T_RPAREN, "')'");
    outcode(t1, "BN", OP_CALL, ret_type);
}

/* `cast_type` >= 0: the call is the operand of a cast to that pointer
 * type, which v7's build(CAST) applies by retyping the operand's top
 * node ("p2->type = t") - the CALL is written with it, the callee's NAME
 * keeps the function's own type: 10_integ/03_linklist.1.golden's "(struct
 * node *) malloc(...)" -> NAME(_malloc, FUNC.PTR.CHAR 49) ... CALL(12).
 * -1: an ordinary call. */
static ExprVal parse_call(Parser *p, FILE *t1, int cast_type)
{
    char name[LEX_IDENT_MAX];
    strncpy(name, p->cur.ident, sizeof name - 1);
    name[sizeof name - 1] = '\0';
    advance(p); /* consume IDENT */
    advance(p); /* consume '(' */

    int ret_type = lookup_func_type(p, name);

    /* The callee's own NAME leaf carries "function returning
     * ret_type" - one FUNC degree on top of ret_type via the same
     * incref() formula as every other derived type - confirmed against
     * 02_long/03_retval.1.golden's "_addlong" callee NAME using type
     * 22 (TY_LONG(6) | 020), every int-returning callee's
     * TY_FUNC_INT(16), and 05_arrptr/07_strlibc.1.golden's "_strcpy"
     * (declared "char *strcpy();"): type 49 = ((9 & ~7) << 2) | 1 |
     * 020, "function returning pointer to char" - the case a plain
     * "ret_type | 020" would get wrong (25). */
    outcode(t1, "BNNS", OP_NAME, SC_EXTERN, ty_incref_tag(ret_type, 020), name);

    int call_type = cast_type >= 0 ? cast_type : ret_type;
    parse_call_args_and_emit(p, t1, call_type);
    return ev_dynamic_typed(call_type);
}

/*
 * indirect-call-expr := '(' '*' IDENT ')' '(' (expr (',' expr)*)? ')'
 *
 * A call through a function-pointer VARIABLE (as opposed to
 * parse_call()'s direct call by name) - e.g. 07_funcptr.c's
 * "(*f)(x)". `f` (a local of type TY_PTR_FUNC_INT - see parse_decl()/
 * parse_param_decls()) is referenced as an ordinary NAME leaf, then
 * OP_STAR(TY_FUNC_INT) dereferences it (v7/cc/c04.c's decref(72)=16,
 * mirroring parse_call()'s TY_FUNC_INT - not a general decref()
 * implementation, since this grammar scope only ever needs this one
 * specific dereference), and the result feeds the same argument-list-
 * plus-CALL shape parse_call() itself uses - confirmed against
 * 07_funcptr.1.golden's "return (*f)(x);" (NAME(f), STAR, NAME(x),
 * CALL). Only a plain pointer VARIABLE is supported as the callee
 * expression (not a general pointer-typed expression) - matching
 * this grammar scope's equally narrow treatment of star-assign-stmt
 * elsewhere.
 */
static ExprVal parse_indirect_call(Parser *p, FILE *t1)
{
    advance(p); /* consume '(' */
    advance(p); /* consume '*' */
    if (p->cur.kind != T_IDENT) {
        c0_error_at(p->cur.line,
            "expected a function-pointer variable name after '(*'");
        return ev_dynamic();
    }
    int line = p->cur.line;
    SymEntry *sym = lookup_var(p, p->cur.ident);
    if (!sym) {
        c0_error_at(line, "'%s' undeclared", p->cur.ident);
        advance(p);
        return ev_dynamic();
    }
    advance(p); /* consume IDENT */
    if (!expect(p, T_RPAREN, "')'"))
        return ev_dynamic();

    if (sym->type != TY_PTR_FUNC_INT) {
        c0_error_at(line,
            "'%s' is not a function-pointer variable - an indirect "
            "call through anything else is not yet supported - see "
            "src/mutos_cc/README.md", sym->name);
        return ev_dynamic();
    }
    emit_name(t1, sym, sym->type);
    outcode(t1, "BN", OP_STAR, TY_FUNC_INT);

    if (!expect(p, T_LPAREN, "'('"))
        return ev_dynamic();
    parse_call_args_and_emit(p, t1, TY_INT); /* only an int-returning
                                               * function pointer is
                                               * supported so far - see
                                               * this function's own
                                               * comment above. */
    return ev_dynamic();
}

/* ------------------------------------------------------------------ */
/* Types beyond int/char/long: struct, union, enum, typedef, bit-fields
 * (tests/mutos_cc/06_struct, 10_integ/03_linklist).
 *
 * The wire format has one base type for every struct and union,
 * TY_STRUCT (4) - v7/cc/c0.h's UNION is "adjusted later to struct" - with
 * the usual PTR/FUNC/ARRAY degrees on top (a "struct point *" is 12, a
 * "struct node **" 44); WHICH struct a type code means travels beside
 * it, as a StructDef (c0_sym.h), the way v7 keeps a tree node's `strp`.
 * A union is a struct whose members all sit at offset 0. An enumeration
 * is an int, its constants plain int constants; a typedef name stands for
 * its type wherever a type keyword may. None of these writes anything of
 * its own to temp1: a declaration only shapes the NAME/AMPER/PLUS/STAR
 * trees that use it (see "Member and subscript chains" below). */

typedef struct {
    int        type;   /* base type code: TY_INT, TY_CHAR, TY_LONG,
                        * TY_UNSIGN or TY_STRUCT - or, from a typedef,
                        * one with pointer degrees on top */
    StructDef *sdef;   /* the struct when the base is TY_STRUCT */
} TypeSpec;

static int name_eq(const char *a, const char *b)
{
    return strncmp(a, b, MCC_NCPS) == 0;
}

/* Copies `src` into a MCC_NCPS + 1 byte name field, truncated to its
 * significant characters (CLAUDE.md's identifier-length rule). */
static void copy_ncps(char *dst, const char *src)
{
    size_t n = strlen(src);
    if (n > MCC_NCPS)
        n = MCC_NCPS;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static TypedefEnt *find_typedef(Parser *p, const char *name)
{
    for (TypedefEnt *t = p->typedefs; t; t = t->next)
        if (name_eq(t->name, name))
            return t;
    return NULL;
}

static EnumCon *find_enumcon(Parser *p, const char *name)
{
    for (EnumCon *e = p->enumcons; e; e = e->next)
        if (name_eq(e->name, name))
            return e;
    return NULL;
}

static StructDef *find_struct(Parser *p, const char *tag)
{
    for (StructDef *s = p->structs; s; s = s->next)
        if (s->tag[0] && name_eq(s->tag, tag))
            return s;
    return NULL;
}

static void *xcalloc(size_t n)
{
    void *q = calloc(1, n);
    if (!q)
        abort(); /* out of memory - nothing sane left to report */
    return q;
}

static StructDef *new_struct(Parser *p, const char *tag, int is_union)
{
    StructDef *s = xcalloc(sizeof *s);
    copy_ncps(s->tag, tag);
    s->is_union = is_union;
    s->next = p->structs;
    p->structs = s;
    return s;
}

static void free_types(Parser *p)
{
    while (p->structs) {
        StructDef *s = p->structs;
        p->structs = s->next;
        while (s->members) {
            Member *m = s->members;
            s->members = m->next;
            free(m);
        }
        free(s);
    }
    while (p->typedefs) {
        TypedefEnt *t = p->typedefs;
        p->typedefs = t->next;
        free(t);
    }
    while (p->enumcons) {
        EnumCon *e = p->enumcons;
        p->enumcons = e->next;
        free(e);
    }
}

/* Whether the current token begins a declaration's type - a type
 * keyword, or a typedef name followed by a declarator (an identifier or
 * '*': "POINT p;"), which is what tells "INTEGER n;" apart from an
 * expression statement starting with a name. */
static int at_type_spec(Parser *p)
{
    switch (p->cur.kind) {
    case T_KW_INT: case T_KW_CHAR: case T_KW_LONG: case T_KW_UNSIGNED:
    case T_KW_FLOAT: case T_KW_DOUBLE:
    case T_KW_STRUCT: case T_KW_UNION: case T_KW_ENUM:
        return 1;
    case T_IDENT:
        if (!find_typedef(p, p->cur.ident))
            return 0;
        return peek2_kind(p) == T_IDENT || peek2_kind(p) == T_STAR;
    default:
        return 0;
    }
}

static TypeSpec parse_type_spec(Parser *p);

/* v7/cc/c03.c's align(): the padding needed in front of a member of type
 * `type` at byte offset `offset` - a field of `flen` bits, or 0 - and,
 * through *bitoffs, the bits already used in the current word:
 * anything but a char (or an array of chars) starts on a word boundary,
 * a non-field member first moves past the bytes the preceding fields
 * used, and a field that does not fit into the rest of the word starts
 * the next one. Only int and unsigned fields are accepted (v7 also
 * packs char fields into bytes; no golden has one). */
static int st_align(int type, int offset, int flen, int *bitoffs, int line)
{
    int a = offset;
    if (flen == 0) {
        a += (8 + *bitoffs - 1) / 8;
        *bitoffs = 0;
    }
    int t = type;
    while ((t & 030) == 030)
        t = ty_decref(t);
    if (t != TY_CHAR) {
        a = (a + 1) & ~1;
        if (a > offset)
            *bitoffs = 0;
    }
    if (flen) {
        if (type != TY_INT && type != TY_UNSIGN) {
            c0_error_at(line, "a bit-field of a type other than int or "
                              "unsigned is not yet supported");
        } else {
            if (flen > 16)
                c0_error_at(line, "Field too long");
            if (flen + *bitoffs > 16) {
                *bitoffs = 0;
                a += 2;
            }
        }
    }
    return a - offset;
}

/* A struct or union body, '{' already current: the members, laid out as
 * v7/cc/c03.c's declist()/declare()/decl1() lay out MOS/MOU members -
 * a member at the aligned offset after the previous one (see
 * st_align()), a bit-field in the word its predecessors' bits leave
 * room in, every union member at 0 - and the size: the end, rounded up
 * to a whole word ("offset+align(INT, offset, 0)"). Confirmed against
 * the goldens' frames: "struct point { int x; int y; }" is 4 bytes
 * (06_struct/01_stbasic.1.golden: p at -8), "struct rect" of two of them
 * 8 (05_nestst: r at -12, "r.botright.x" at offset 4), "union number {
 * int i; char b[2]; }" 2 (06_union: n at -6), and the four fields of
 * 07_bitfield's "struct flags" (1+1+2+4 bits) one word, FSEL bit offsets
 * 0, 1, 2 and 4. */
static void parse_struct_body(Parser *p, StructDef *sd)
{
    int line = p->cur.line;
    advance(p); /* '{' */
    int offset = 0, bitoffs = 0;
    Member **tail = &sd->members;
    while (p->cur.kind != T_RBRACE && p->cur.kind != T_EOF) {
        if (!at_type_spec(p) && p->cur.kind != T_IDENT) {
            c0_error_at(p->cur.line, "expected a member declaration");
            advance(p);
            continue;
        }
        TypeSpec ts = parse_type_spec(p);
        for (;;) {
            int ptr_degree = 0;
            while (p->cur.kind == T_STAR) {
                ptr_degree++;
                advance(p);
            }
            if (p->cur.kind != T_IDENT) {
                c0_error_at(p->cur.line, "expected a member name (an unnamed "
                                          "filler field is not yet supported)");
                break;
            }
            Member *m = xcalloc(sizeof *m);
            copy_ncps(m->name, p->cur.ident);
            int mline = p->cur.line;
            advance(p);
            for (Member *o = sd->members; o; o = o->next)
                if (name_eq(o->name, m->name))
                    c0_error_at(mline, "member '%s' redeclared", m->name);
            int t = ts.type;
            for (int k = 0; k < ptr_degree; k++)
                t = ty_ptr_of(t);
            m->sdef = ts.sdef;
            if (p->cur.kind == T_LBRACK) {
                advance(p);
                if (p->cur.kind != T_ICON || p->cur.ival <= 0) {
                    c0_error_at(p->cur.line, "expected a positive array size");
                    m->nelem = 1;
                } else {
                    m->nelem = (int)p->cur.ival;
                    advance(p);
                }
                expect(p, T_RBRACK, "']'");
                if (p->cur.kind == T_LBRACK)
                    c0_error_at(mline, "a member array of more than one "
                                       "dimension is not yet supported");
                t = ty_ary_of(t);
            }
            if (t == TY_STRUCT && (!ts.sdef || !ts.sdef->complete))
                c0_error_at(mline, "member '%s' has an incomplete struct type",
                            m->name);
            if (t == TY_LONG || (t & 07) == TY_LONG)
                c0_error_at(mline, "a 'long' struct member is not yet supported "
                                   "- see src/mutos_cc/README.md");
            int flen = 0;
            if (p->cur.kind == T_COLON) {
                advance(p);
                if (p->cur.kind != T_ICON || p->cur.ival <= 0) {
                    c0_error_at(p->cur.line, "expected a field width");
                } else {
                    flen = (int)p->cur.ival;
                    advance(p);
                }
                if (sd->is_union)
                    c0_error_at(mline, "a bit-field in a union is not yet "
                                       "supported");
                m->is_field = (flen > 0);
            }
            m->type = t;
            int base = sd->is_union ? 0 : offset;
            int a = st_align(t, base, flen, &bitoffs, mline);
            m->offset = base + a;
            int elsize;
            if (m->is_field) {
                m->bitoffs = bitoffs;
                m->flen = flen;
                bitoffs += flen;
                elsize = a;
            } else {
                elsize = type_size(t, m->sdef, m->nelem) + a;
            }
            if (sd->is_union) {
                int o = elsize;
                o += st_align(TY_CHAR, o, 0, &bitoffs, mline);
                if (o > offset)
                    offset = o;
            } else {
                offset += elsize;
            }
            *tail = m;
            tail = &m->next;
            if (p->cur.kind == T_COMMA) {
                advance(p);
                continue;
            }
            break;
        }
        expect(p, T_SEMI, "';'");
    }
    if (!sd->members)
        c0_error_at(line, "a struct or union without members");
    sd->size = offset + st_align(TY_INT, offset, 0, &bitoffs, line);
    sd->complete = 1;
    expect(p, T_RBRACE, "'}'");
}

/* "struct" or "union", an optional tag, an optional body - v7/cc/c03.c's
 * strdec(). A tag seen for the first time is registered before its body
 * is read, so a member may point to the struct being defined ("struct
 * node *next;" - 10_integ/03_linklist). */
static TypeSpec parse_struct_spec(Parser *p)
{
    int is_union = (p->cur.kind == T_KW_UNION);
    int line = p->cur.line;
    advance(p); /* 'struct'/'union' */
    StructDef *sd = NULL;
    if (p->cur.kind == T_IDENT) {
        sd = find_struct(p, p->cur.ident);
        if (sd && sd->is_union != is_union)
            c0_error_at(line, "'%s' redeclared as a different kind of tag",
                        p->cur.ident);
        if (!sd)
            sd = new_struct(p, p->cur.ident, is_union);
        advance(p);
    } else if (p->cur.kind != T_LBRACE) {
        c0_error_at(line, "expected a struct/union tag or '{'");
    }
    if (p->cur.kind == T_LBRACE) {
        if (!sd)
            sd = new_struct(p, "", is_union);
        if (sd->complete)
            c0_error_at(line, "'%s' redeclared", sd->tag);
        parse_struct_body(p, sd);
    }
    TypeSpec ts = { TY_STRUCT, sd };
    return ts;
}

/* "enum", an optional tag (not kept - an enum type is int), an optional
 * list of constants - v7/cc/c03.c's strdec() for ENUM, then decl1() per
 * constant: each one's value is the previous one's plus 1, from 0, or
 * the constant expression after '=' ("hoffset = offset; ... elsize =
 * hoffset-offset+1"). Confirmed against 06_struct/08_enum.1.golden:
 * "c = GREEN;" is CON 1. */
static TypeSpec parse_enum_spec(Parser *p)
{
    advance(p); /* 'enum' */
    if (p->cur.kind == T_IDENT)
        advance(p); /* the tag */
    if (p->cur.kind == T_LBRACE) {
        advance(p);
        long value = 0;
        while (p->cur.kind == T_IDENT) {
            char name[LEX_IDENT_MAX];
            snprintf(name, sizeof name, "%s", p->cur.ident);
            int line = p->cur.line;
            advance(p);
            if (p->cur.kind == T_ASSIGN) {
                advance(p);
                /* A constant writes nothing; anything else is refused,
                 * and whatever it wrote is dropped with the buffer. */
                char *junk = NULL;
                size_t jlen = 0;
                FILE *mem = open_memstream(&junk, &jlen);
                if (!mem)
                    abort();
                ExprVal v = parse_expr(p, mem);
                fclose(mem);
                free(junk);
                if (!v.is_const || v.is_long)
                    c0_error_at(line, "an enumeration constant's value must be "
                                      "an int constant");
                else
                    value = v.value;
            }
            if (find_enumcon(p, name))
                c0_error_at(line, "'%s' redeclared", name);
            EnumCon *e = xcalloc(sizeof *e);
            copy_ncps(e->name, name);
            e->value = (int)trunc16(value);
            e->next = p->enumcons;
            p->enumcons = e;
            value++;
            if (p->cur.kind != T_COMMA)
                break;
            advance(p);
        }
        expect(p, T_RBRACE, "'}'");
    }
    TypeSpec ts = { TY_INT, NULL };
    return ts;
}

/* A declaration's type: a type keyword ("long int" and "unsigned int"
 * included), a struct/union/enum specifier, or a typedef name - v7/cc/
 * c03.c's getkeywords(). */
static TypeSpec parse_type_spec(Parser *p)
{
    TypeSpec ts = { TY_INT, NULL };
    switch (p->cur.kind) {
    case T_KW_INT:
        advance(p);
        return ts;
    case T_KW_CHAR:
        advance(p);
        ts.type = TY_CHAR;
        return ts;
    case T_KW_LONG:
        advance(p);
        if (p->cur.kind == T_KW_INT)
            advance(p);
        else if (p->cur.kind == T_KW_FLOAT) {
            /* "long float" is double - v7/cc/c03.c's getkeywords() */
            advance(p);
            ts.type = TY_DOUBLE;
            return ts;
        }
        ts.type = TY_LONG;
        return ts;
    case T_KW_FLOAT:
        advance(p);
        ts.type = TY_FLOAT;
        return ts;
    case T_KW_DOUBLE:
        advance(p);
        ts.type = TY_DOUBLE;
        return ts;
    case T_KW_UNSIGNED:
        advance(p);
        if (p->cur.kind == T_KW_INT)
            advance(p);
        ts.type = TY_UNSIGN;
        return ts;
    case T_KW_STRUCT:
    case T_KW_UNION:
        return parse_struct_spec(p);
    case T_KW_ENUM:
        return parse_enum_spec(p);
    case T_IDENT: {
        TypedefEnt *td = find_typedef(p, p->cur.ident);
        if (td) {
            advance(p);
            ts.type = td->type;
            ts.sdef = td->sdef;
            return ts;
        }
    }
    /* fall through */
    default:
        c0_error_at(p->cur.line, "expected a type");
        return ts;
    }
}

/* "typedef" type declarator (',' declarator)* ';' at file scope: each
 * name stands for the type from then on - v7's TYPEDEF class. Pointer
 * declarators ('*'s) are accepted, array and function ones are not (no
 * golden has one). 06_struct/09_typedef.c: "typedef struct point { ... }
 * POINT; typedef int INTEGER;". */
static void parse_typedef(Parser *p)
{
    advance(p); /* 'typedef' */
    TypeSpec ts = parse_type_spec(p);
    for (;;) {
        int t = ts.type;
        while (p->cur.kind == T_STAR) {
            t = ty_ptr_of(t);
            advance(p);
        }
        if (p->cur.kind != T_IDENT) {
            c0_error_at(p->cur.line, "expected a typedef name");
            break;
        }
        if (find_typedef(p, p->cur.ident))
            c0_error_at(p->cur.line, "typedef '%s' redeclared", p->cur.ident);
        TypedefEnt *td = xcalloc(sizeof *td);
        copy_ncps(td->name, p->cur.ident);
        td->type = t;
        td->sdef = ts.sdef;
        td->next = p->typedefs;
        p->typedefs = td;
        advance(p);
        if (p->cur.kind == T_LBRACK || p->cur.kind == T_LPAREN)
            c0_error_at(p->cur.line, "an array or function typedef is not yet "
                                      "supported");
        if (p->cur.kind != T_COMMA)
            break;
        advance(p);
    }
    expect(p, T_SEMI, "';'");
}

/* ------------------------------------------------------------------ */
/* Member and subscript chains.
 *
 * A struct member reference is built by v7/cc/c01.c's build() as
 *
 *   a.b   ->  (&a)->b                       (DOT: *cp++ = p1; build(AMPER))
 *   p->b  ->  *(p + offsetof(b))            (ARROW: setype(p1, incref(t2));
 *                                            PLUS(t, p1, CON off); STAR)
 *
 * and ARROW first RETYPES the left operand's spine with setype(): the
 * node it is given and every node below it through tr1, as long as they
 * are AMPER/STAR/PLUS (AMPER passing decref(t) on, STAR incref(t)), the
 * first other node (the NAME) included. So "p.x" on "struct point p" is
 * NOT NAME(p, struct) AMPER(12) ... but
 *
 *   NAME(p, INT) AMPER(8) CON 0 PLUS(8) STAR(INT)      (01_stbasic.1.golden)
 *
 * and "pts[i].x" retypes the subscript's whole spine too - while the
 * subscript's ITOP (PLUS's RIGHT operand, never visited) keeps the type
 * it was built with, PTR.STRUCT:
 *
 *   NAME(pts, 0) AMPER(8) NAME(i) CON 4 ITOP(12) PLUS(8) STAR(0)
 *   AMPER(8) CON 0 PLUS(8) STAR(0)                     (03_starray.1.golden)
 *
 * mutos_c0 writes temp1 as it parses, but these types are only known once
 * the chain's LAST member is: so a chain starting at a variable is
 * collected first - its spine nodes with their types, each PLUS's right
 * operand (a subscript's index, already parsed, or a member's offset
 * constant) as the bytes it will write - retyped as build() would, and
 * written out when it ends. disarray() (an array value decaying to a
 * pointer: setype() to the element type, then AMPER) retypes the same
 * way, which is how "n.b[0]" on a "char b[2]" member becomes NAME(n,
 * CHAR) AMPER(9) CON 0 PLUS(9) STAR(1) AMPER(9) CON 0 CON 1 ITOP(9)
 * PLUS(9) STAR(1) (06_union.1.golden). A bit-field member's STAR is
 * followed by FSEL(UNSIGN, bitoffs, flen) - treeout()'s FSEL case, the
 * "unsigned" of build()'s ARROW for a field - 07_bitfield.1.golden. */

#define MCC_CHAINMAX 32

typedef struct {
    int    op;       /* OP_NAME (always n[0]), OP_AMPER, OP_PLUS, OP_STAR */
    int    type;
    char  *rbuf;     /* OP_PLUS: its right operand's bytes */
    size_t rlen;
} CNode;

typedef struct {
    CNode  n[MCC_CHAINMAX];
    int    len;
    const SymEntry *sym;
    int    type;           /* the value's type (the top node's) */
    StructDef *sdef;       /* the struct when `type`'s base is TY_STRUCT */
    int    nelem;          /* an array member's size, when `type` has an
                            * ARRAY degree (it is decayed before use) */
    const Member *field;   /* the bit-field the last member step selected */
} Chain;

static void chain_push(Chain *c, int op, int type, char *rbuf, size_t rlen)
{
    if (c->len >= MCC_CHAINMAX) {
        c0_error_at(diag_line, "member/subscript chain too long (internal "
                               "limit %d)", MCC_CHAINMAX);
        free(rbuf);
        return;
    }
    CNode *nd = &c->n[c->len++];
    nd->op = op;
    nd->type = type;
    nd->rbuf = rbuf;
    nd->rlen = rlen;
}

/* v7/cc/c01.c's setype() on the chain's top node - see above. */
static void chain_setype(Chain *c, int t)
{
    for (int j = c->len - 1; j >= 0; j--) {
        c->n[j].type = t;
        if (c->n[j].op == OP_AMPER)
            t = ty_decref(t);
        else if (c->n[j].op == OP_STAR)
            t = ty_ptr_of(t);
        else if (c->n[j].op != OP_PLUS)
            break;
    }
}

/* v7's disarray() for a chain whose value is an array (an array member):
 * the spine retyped to the element type, then its address taken. */
static void chain_disarray(Chain *c)
{
    if ((c->type & 030) != 030)
        return;
    int elem = ty_decref(c->type);
    chain_setype(c, elem);
    chain_push(c, OP_AMPER, ty_ptr_of(elem), NULL, 0);
    c->type = ty_ptr_of(elem);
    c->nelem = 0;
}

/* The bytes of a PLUS node's constant right operand, CON(INT, v). */
static char *chain_con(long v, size_t *len)
{
    char *buf = NULL;
    FILE *mem = open_memstream(&buf, len);
    if (!mem)
        abort();
    outcode(mem, "BNN", OP_CON, TY_INT, (int)v);
    fclose(mem);
    return buf;
}

static void chain_start(Chain *c, const SymEntry *sym)
{
    c->len = 0;
    c->sym = sym;
    c->sdef = sym->sdef;
    c->nelem = 0;
    c->field = NULL;
    chain_push(c, OP_NAME, sym->type, NULL, 0);
    c->type = sym->type;
    if (sym->is_array) {
        /* An array variable is its NAME typed as the element, and the
         * AMPER that decays it (disarray() on a NAME - the array
         * decay parse_primary() already writes for "int a[4]") */
        c->type = ty_ptr_of(sym->type);
        chain_push(c, OP_AMPER, c->type, NULL, 0);
    }
}

/* '[' index ']' on the chain - v7's build(LBRACK): build(PLUS) of the
 * (decayed) pointer and the index scaled by the element's size (ITOP,
 * typed as the pointer), then STAR. */
static void chain_subscript(Parser *p, Chain *c)
{
    int line = p->cur.line;
    advance(p); /* '[' */
    chain_disarray(c);
    if (!ty_is_ptr(c->type)) {
        c0_error_at(line, "a subscript of something that is not an array or "
                          "pointer");
        c->type = TY_INT;
    }
    int ptype = c->type;
    int elem = ty_decref(ptype);
    if ((elem & 030) == 030 || (elem & 030) == 020)
        c0_error_at(line, "this subscript's element type is not yet supported "
                          "- see src/mutos_cc/README.md");
    if (elem == TY_LONG)
        refuse_long_access(p);
    if (elem == TY_STRUCT && (!c->sdef || !c->sdef->complete))
        c0_error_at(line, "a subscript of a pointer to an incomplete struct");
    char *buf = NULL;
    size_t len = 0;
    FILE *mem = open_memstream(&buf, &len);
    if (!mem)
        abort();
    ExprVal ix = parse_expr(p, mem);
    expect(p, T_RBRACK, "']'");
    emit_materialize(mem, ix);
    (void)promote_char(mem, ix);
    if (ix.type == TY_LONG || ty_is_ptr(ix.type) || ix.type == TY_STRUCT)
        c0_error_at(line, "this subscript's index type is not yet supported");
    outcode(mem, "BNN", OP_CON, TY_INT, type_size(elem, c->sdef, 0));
    outcode(mem, "BN", OP_ITOP, ptype);
    fclose(mem);
    chain_push(c, OP_PLUS, ptype, buf, len);
    chain_push(c, OP_STAR, elem, NULL, 0);
    c->type = elem;
}

/* '.' member or '->' member on the chain - v7's build(DOT)/build(ARROW)
 * (see above). */
static void chain_member(Parser *p, Chain *c)
{
    int line = p->cur.line;
    int arrow = (p->cur.kind == T_ARROW);
    advance(p); /* '.'/'->' */
    if (c->field) {
        c0_error_at(line, "a member of a bit-field");
        return;
    }
    if (!arrow) {
        if (c->type != TY_STRUCT) {
            c0_error_at(line, "'.' applied to something that is not a struct");
            return;
        }
        c->type = ty_ptr_of(TY_STRUCT);
        chain_push(c, OP_AMPER, c->type, NULL, 0);
    } else if (!ty_is_ptr(c->type) || ty_decref(c->type) != TY_STRUCT) {
        c0_error_at(line, "'->' applied to something that is not a pointer to "
                          "a struct");
        return;
    }
    if (p->cur.kind != T_IDENT) {
        c0_error_at(p->cur.line, "expected a member name");
        return;
    }
    const Member *m = NULL;
    if (!c->sdef || !c->sdef->complete)
        c0_error_at(line, "a member of an incomplete struct");
    else
        for (m = c->sdef->members; m && !name_eq(m->name, p->cur.ident); m = m->next)
            ;
    if (!m) {
        if (c->sdef && c->sdef->complete)
            c0_error_at(p->cur.line, "'%s' is not a member of this struct",
                        p->cur.ident);
        advance(p);
        return;
    }
    advance(p); /* the member name */
    int t2 = m->type;
    if (m->is_field && t2 == TY_INT)
        t2 = TY_UNSIGN;                   /* build(ARROW): "t2 = UNSIGN" */
    int t = ty_ptr_of(t2);
    chain_setype(c, t);
    size_t len = 0;
    char *buf = chain_con(m->offset, &len);
    chain_push(c, OP_PLUS, t, buf, len);
    chain_push(c, OP_STAR, t2, NULL, 0);
    c->type = t2;
    c->sdef = m->sdef;
    c->nelem = m->nelem;
    c->field = m->is_field ? m : NULL;
}

/* Writes the finished chain: the spine in postfix order, each PLUS
 * after its right operand's bytes, FSEL after a bit-field's STAR. An
 * array value decays first. Returns the value it leaves. */
static ExprVal chain_emit(Chain *c, FILE *t1, int *is_field)
{
    if (is_field)
        *is_field = (c->field != NULL);
    chain_disarray(c);
    emit_name(t1, c->sym, c->n[0].type);
    for (int j = 1; j < c->len; j++) {
        CNode *nd = &c->n[j];
        if (nd->op == OP_PLUS) {
            fwrite(nd->rbuf, 1, nd->rlen, t1);
            free(nd->rbuf);
            nd->rbuf = NULL;
        }
        outcode(t1, "BN", nd->op, nd->type);
    }
    int type = c->type;
    if (c->field) {
        outcode(t1, "BNNN", OP_FSEL, TY_UNSIGN, c->field->bitoffs,
                c->field->flen);
        type = TY_UNSIGN;
    }
    ExprVal v = ev_dynamic_typed(type);
    v.char_obj = (type == TY_CHAR);
    v.sdef = c->sdef;
    return v;
}

/* Whether a variable reference must be parsed as a chain: a member
 * access follows, or a subscript of an array of (or pointer to) structs
 * - every other subscript keeps emit_subscript()'s own shapes. */
static int starts_chain(Parser *p, const SymEntry *sym)
{
    TokKind k = p->cur.kind;
    return k == T_DOT || k == T_ARROW ||
           (k == T_LBRACK && sym->sdef && !sym->dim2);
}

/* A variable reference with its member accesses and subscripts, the
 * variable's name already consumed - written to t1 once complete (see
 * above). *is_field (if not NULL): whether it selects a bit-field. */
static ExprVal parse_postfix_chain(Parser *p, FILE *t1, const SymEntry *sym,
                                   int *is_field)
{
    Chain c;
    chain_start(&c, sym);
    for (;;) {
        if (p->cur.kind == T_LBRACK)
            chain_subscript(p, &c);
        else if (p->cur.kind == T_DOT || p->cur.kind == T_ARROW)
            chain_member(p, &c);
        else
            break;
    }
    return chain_emit(&c, t1, is_field);
}

/* The right-hand side of a whole-struct assignment, "p2 = p1;" - a
 * struct variable, or a chain ending in a struct: v7's build(ASSIGN) for
 * two operands of the same struct type (no conversion), and treeout()'s
 * STRASG after the top node of a struct type - "BNN", STRASG, STRUCT,
 * the struct's size:
 *
 *   NAME(p2, 4) NAME(p1, 4) ASSIGN(4) STRASG(4, 4)     (04_stassign.1.golden)
 *
 * Only such a variable or chain is accepted on the right (a struct-valued
 * call or a '?:' has no golden). */
static void parse_struct_assign(Parser *p, FILE *t1, const StructDef *sd,
                                int line)
{
    if (p->cur.kind != T_IDENT) {
        c0_error_at(p->cur.line, "the right-hand side of a struct assignment "
                                  "must be a struct variable or member so far");
        return;
    }
    SymEntry *rs = lookup_var(p, p->cur.ident);
    if (!rs) {
        c0_error_at(p->cur.line, "'%s' undeclared", p->cur.ident);
        advance(p);
        return;
    }
    advance(p);
    ExprVal v;
    if (starts_chain(p, rs)) {
        v = parse_postfix_chain(p, t1, rs, NULL);
    } else {
        emit_name(t1, rs, rs->type);
        v = ev_dynamic_typed(rs->is_array ? -1 : rs->type);
        v.sdef = rs->sdef;
    }
    if (v.type != TY_STRUCT || v.sdef != sd)
        c0_error_at(line, "assigning a value of a different type to a struct");
    outcode(t1, "BN", OP_ASSIGN, TY_STRUCT);
    outcode(t1, "BNN", OP_STRASG, TY_STRUCT, sd ? sd->size : 0);
}

/* '(' type-name ')' unary-expr with a pointer (or struct/typedef) type -
 * v7's build(CAST) for pointer to pointer: no conversion, the operand's
 * top node retyped ("p2->type = t"). Two operands are accepted: a call
 * (its CALL written with the cast's type - see parse_call()), and a
 * pointer variable (its NAME written with it). "(struct node *)
 * malloc(sizeof(struct node))" - 10_integ/03_linklist.1.golden. */
static ExprVal parse_pointer_cast(Parser *p, FILE *t1)
{
    int line = p->cur.line;
    advance(p); /* '(' */
    TypeSpec ts = parse_type_spec(p);
    int t = ts.type;
    while (p->cur.kind == T_STAR) {
        t = ty_ptr_of(t);
        advance(p);
    }
    if (!expect(p, T_RPAREN, "')'"))
        return ev_dynamic();
    ExprVal v;
    if (!ty_is_ptr(t)) {
        c0_error_at(line, "a cast to this type is not yet supported - see "
                          "src/mutos_cc/README.md");
        return parse_unary(p, t1);
    }
    if (p->cur.kind == T_IDENT && peek2_kind(p) == T_LPAREN) {
        v = parse_call(p, t1, t);
    } else if (p->cur.kind == T_IDENT && lookup_var(p, p->cur.ident) &&
               lookup_var(p, p->cur.ident)->is_ptr &&
               peek2_kind(p) != T_LBRACK && peek2_kind(p) != T_DOT &&
               peek2_kind(p) != T_ARROW && peek2_kind(p) != T_INCR &&
               peek2_kind(p) != T_DECR) {
        emit_name(t1, lookup_var(p, p->cur.ident), t);
        advance(p);
        v = ev_dynamic_typed(t);
    } else {
        c0_error_at(line, "a pointer cast of anything but a call or a pointer "
                          "variable is not yet supported - see "
                          "src/mutos_cc/README.md");
        return parse_unary(p, t1);
    }
    v.sdef = ts.sdef;
    return v;
}

static ExprVal parse_primary(Parser *p, FILE *t1)
{
    if (p->cur.kind == T_STRING) {
        /* A string literal - v7/cc/c00.c tree()'s STRING case ("fake a
         * static char array"): the literal's bytes are written to
         * temp2 under a fresh label (putstr()), and the expression is a
         * NAME of that unnamed static "char[]", which - being an array
         * - decays to its address: NAME(SC_STATIC, TY_CHAR, <label>)
         * AMPER(TY_PTR_CHAR). Confirmed byte-for-byte against
         * 05_arrptr/05_arrofptr.1.golden ("names[0] = \"one\";" ...),
         * 07_strlibc.1.golden (a call argument) and 10_integ/
         * 04_strrev.1.golden - the NAME's "offset" is the label, the
         * same convention a local static's NAME uses.
         *
         * The label is taken from the function-wide counter when the
         * literal is PARSED. v7 takes it ("cval = isn++") when the
         * token is LEXED, which with its one-token peeks is the same
         * moment in every realistic program; the one theoretical
         * difference is a statement that itself begins with a string
         * literal right where v7 peeks past a construct (after an
         * "if (...)" condition, after an if-statement without
         * "else", or as the first token of a for-increment) - there v7
         * would number the literal before that construct's own label.
         * The same goes for the order of temp2's runs: the order in
         * which literals are parsed, which parse_for_stmt()'s buffered
         * increment does not change (it is parsed before the body, as
         * in v7). */
        int lab = p->isn++;
        (void)putstr(p->t2, lab, p->cur.sval, p->cur.slen);
        advance(p);
        outcode(t1, "BNNN", OP_NAME, SC_STATIC, TY_CHAR, lab);
        outcode(t1, "BN", OP_AMPER, TY_PTR_CHAR);
        return ev_dynamic_typed(TY_PTR_CHAR);
    }
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
        int octhex = p->cur.is_octhex;
        advance(p);
        /* v7/cc/c00.c's getnum(): an octal or hex constant is long only
         * above 0177777 ("(lcval>>1)>MAXINT"); 0100000 ... 0177777 are
         * ints, their bits as written - "ip->i_mode = 0100000" and "x &
         * 0170000" in the 11_kernel sources (K&R sect. 2.4.1). Until
         * 2026-10-02 such a constant was made long. */
        if (raw > 32767 && !(octhex && raw <= 0177777))
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
    if (p->cur.kind == T_FCON) {
        /* A floating constant: FCON(DOUBLE, its source text) - v7/cc/
         * c00.c's tree() makes every one a DOUBLE fblock(), and c04.c's
         * treeout() writes it "BNF" - written at once, since nothing
         * folds it (see "Floating point"). 08_float/01_floatbas.1.golden:
         * "a = 3.5;" -> FCON(3, "3.5"). */
        outcode(t1, "BNF", OP_FCON, TY_DOUBLE, p->cur.ident);
        advance(p);
        return ev_dynamic_typed(TY_DOUBLE);
    }
    if (p->cur.kind == T_CCON) {
        /* A character constant is an ordinary int constant - v7/cc/
         * c00.c's getcc() returns CON with the character's value, so it
         * folds and materializes (CON(TY_INT, v)) like any integer
         * literal. getcc() keeps a one-character constant as a signed
         * char ("realc = cval; cval = realc;"), so a byte above 127 is
         * negative ('\377' is -1) - from v7's source; no golden has one.
         * Confirmed for 0..127 against 10_integ/01_wordcount.1.golden's
         * '\0', '\n' and ' ' (CON values 0, 10, 32 - typed char there,
         * see char_compare_rhs()). c0_lex.c's lex_char() reads one
         * character or escape only (a multi-character constant, which
         * v7 packs two to a word, is its "Malformed character
         * constant"). */
        long v = (int8_t)(uint8_t)(p->cur.ival & 0377);
        advance(p);
        return ev_const(v);
    }
    if (p->cur.kind == T_IDENT) {
        if (peek2_kind(p) == T_LPAREN)
            return parse_call(p, t1, -1);
        SymEntry *sym = lookup_var(p, p->cur.ident);
        if (!sym) {
            EnumCon *ec = find_enumcon(p, p->cur.ident);
            if (ec) {
                /* An enumeration constant is an int constant (v7's
                 * ENUMCON: "CON, INT, value") - 06_struct/08_enum.1.
                 * golden's "c = GREEN;" -> CON 1. */
                advance(p);
                return ev_const(ec->value);
            }
            if (is_known_func(p, p->cur.ident)) {
                /* A bare function name used as a value (not called) -
                 * e.g. 07_funcptr.c's "fp = square;". K&R's implicit
                 * function-address rule: the name is the same
                 * NAME(SC_EXTERN, TY_FUNC_INT, name) leaf parse_call()
                 * uses for a callee, but taken by address (AMPER)
                 * instead of called - confirmed against
                 * 07_funcptr.1.golden's "fp = square;" (NAME(_square,
                 * EXTERN, FUNC), AMPER(TY_PTR_FUNC_INT)). Only
                 * recognized for a name this translation unit has
                 * already seen defined/prototyped (see
                 * MCC_MAXFUNCS's own comment) - an otherwise-
                 * undeclared identifier still falls through to the
                 * ordinary "undeclared" error below. */
                char name[LEX_IDENT_MAX];
                strncpy(name, p->cur.ident, sizeof name - 1);
                name[sizeof name - 1] = '\0';
                advance(p);
                outcode(t1, "BNNS", OP_NAME, SC_EXTERN, TY_FUNC_INT, name);
                outcode(t1, "BN", OP_AMPER, TY_PTR_FUNC_INT);
                return ev_dynamic_typed(TY_PTR_FUNC_INT);
            }
            c0_error_at(p->cur.line, "'%s' undeclared", p->cur.ident);
            advance(p);
            return ev_const(0);
        }
        advance(p);

        if (starts_chain(p, sym)) {
            /* A struct member or a subscript of a struct array - see
             * "Member and subscript chains". */
            int line = p->cur.line;
            ExprVal v = parse_postfix_chain(p, t1, sym, NULL);
            if (v.type == TY_STRUCT && !p->struct_value_ok)
                c0_error_at(line, "a whole struct used as a value is not yet "
                                  "supported (only assigned, or its address "
                                  "taken) - see src/mutos_cc/README.md");
            if (p->cur.kind == T_INCR || p->cur.kind == T_DECR)
                c0_error_at(p->cur.line, "'++'/'--' on a struct member or "
                                          "element is not yet supported - see "
                                          "src/mutos_cc/README.md");
            return v;
        }
        if (sym->type == TY_STRUCT && !sym->is_array && !p->struct_value_ok) {
            c0_error_at(p->cur.line, "a whole struct used as a value is not yet "
                                      "supported (only assigned, or its address "
                                      "taken) - see src/mutos_cc/README.md");
        }

        if (p->cur.kind == T_LBRACK && (sym->is_array || sym->is_ptr)) {
            /* Array/pointer subscript used as an rvalue ("sum = sum +
             * a[i];") - see emit_subscript()'s own comment for the
             * two confirmed shapes. */
            int elemtype = emit_subscript(p, t1, sym);
            return ev_char_obj(elemtype);
        }

        /* treeout()'s NAME case - see emit_name(): a numeric offset
         * for a local, the symbol itself for a file-scope variable. */

        if (sym->is_array && sym->dim2 > 0) {
            /* A bare 2-D array name decays to "pointer to a row"
             * (v7/cc's disarray(): NAME retyped to "array of int",
             * AMPER type 104) - a type nothing in this grammar scope
             * can consume, and not confirmed by any golden. */
            c0_error_at(p->cur.line, "a 2-D array used without both "
                                      "subscripts is not yet supported - "
                                      "see src/mutos_cc/README.md");
            return ev_dynamic();
        }

        if (sym->is_array) {
            /* Array-name-as-rvalue decay ("p = a;", "strlen(buf)"):
             * the NAME node itself carries the array's ELEMENT type
             * (v7's disarray() retypes it), followed by AMPER of
             * "pointer to element" to take its address - confirmed
             * for "int a[4]" against 05_incdec.1.golden (NAME 0, AMPER
             * 8; "lea di,*-16.(bp)") and for "char buf[20]" against
             * 05_arrptr/07_strlibc.1.golden and 10_integ/04_strrev.
             * 1.golden (NAME 1, AMPER 9). The result is typed as that
             * pointer, so it combines like one ("a + i" is pointer
             * arithmetic). An array is never a modifiable lvalue, so no
             * postfix '++'/'--' check follows. */
            int pt = ty_ptr_of(sym->type);
            emit_name(t1, sym, sym->type);
            outcode(t1, "BN", OP_AMPER, pt);
            return ev_dynamic_typed(pt);
        }

        emit_name(t1, sym, sym->type);

        if (p->cur.kind == T_INCR || p->cur.kind == T_DECR) {
            /* Postfix '++'/'--' - INCAFT/DECAFT. See emit_incdec()'s
             * comment for the CON/ITOP scaling shape; confirmed
             * against 05_incdec.1.golden's "j = i++;" (plain int) and
             * "*p++ = 1;" (pointer) trees. */
            int optag = (p->cur.kind == T_INCR) ? OP_INCAFT : OP_DECAFT;
            if (ty_is_float(sym->type))
                c0_error_at(p->cur.line, "'++'/'--' on a 'float'/'double' "
                                          "variable is not yet supported - see "
                                          "src/mutos_cc/README.md");
            advance(p);
            emit_incdec(t1, optag, sym->type, sym->is_ptr);
            return ev_dynamic_typed(sym->type);
        }
        ExprVal nv = ev_char_obj(sym->type);
        nv.sdef = sym->sdef;           /* a pointer to a struct ("cur") */
        return nv;
    }
    if (p->cur.kind == T_LPAREN) {
        if (peek2_kind(p) == T_STAR)
            return parse_indirect_call(p, t1);
        TokKind k2 = peek2_kind(p);
        if (k2 == T_KW_STRUCT || k2 == T_KW_UNION || k2 == T_KW_UNSIGNED ||
            k2 == T_KW_ENUM ||
            (k2 == T_IDENT && find_typedef(p, p->la.ident) &&
             !lookup_var(p, p->la.ident)))
            return parse_pointer_cast(p, t1);
        if (peek2_kind(p) == T_KW_INT || peek2_kind(p) == T_KW_CHAR ||
            peek2_kind(p) == T_KW_LONG || peek2_kind(p) == T_KW_FLOAT ||
            peek2_kind(p) == T_KW_DOUBLE) {
            /* cast-expr := '(' type ')' IDENT, type one of int, char,
             * long, float, double
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
             * mutos_cc.h); char->int is ITOC with type TY_INT (see
             * promote_char()'s section). A floating source or target
             * takes the conversions of "Floating point" above: FTOI/
             * FTOL from float/double (08_float's "(int) c", "(long) d",
             * "(int) d"), ITOF/LTOF to it, typed with the cast's own
             * type, and nothing between float and double. Any other
             * (source, target) pair - int->int, int->long, long->char,
             * char->char, long->long, char<->float - is not yet
             * supported; none is exercised by the corpus. */
            advance(p); /* consume '(' */
            TypeSpec cts = parse_type_spec(p); /* "long float" too */
            int target = cts.type;
            if (!expect(p, T_RPAREN, "')'"))
                return ev_dynamic();
            if (p->cur.kind == T_LPAREN &&
                (target == TY_INT || target == TY_LONG)) {
                /* A parenthesized floating expression converted to an
                 * integer: fltprobe/p3_global.1.golden's "(int) (gd + gi
                 * + ...)" - the expression's tree, then FTOI. */
                int line = p->cur.line;
                ExprVal v = parse_primary(p, t1);
                emit_materialize(t1, v);
                if (v.is_const || !ty_is_float(v.type)) {
                    c0_error_at(line, "a cast of a parenthesized expression is "
                                      "only supported for a 'float'/'double' "
                                      "one so far - see src/mutos_cc/README.md");
                    return ev_dynamic();
                }
                emit_from_float(t1, target, line);
                return ev_dynamic_typed(target);
            }
            if (p->cur.kind == T_FCON &&
                (target == TY_INT || target == TY_LONG)) {
                /* A floating constant converted to an integer: not folded
                 * - fltprobe/p16_open2.1.golden's "(int) 2.5" -> FCON(3,
                 * "2.5"), FTOI(0) (v7's c0 folds only an int constant). */
                int line = p->cur.line;
                ExprVal v = parse_primary(p, t1);
                emit_materialize(t1, v);
                emit_from_float(t1, target, line);
                return ev_dynamic_typed(target);
            }
            if (p->cur.kind != T_IDENT) {
                c0_error_at(p->cur.line,
                    "a cast's operand must be a plain variable name so "
                    "far - see src/mutos_cc/README.md");
                if (p->cur.kind != T_EOF)
                    advance(p);
                return ev_const(0);
            }
            int line = p->cur.line;
            SymEntry *sym = lookup_var(p, p->cur.ident);
            if (!sym) {
                c0_error_at(line, "'%s' undeclared", p->cur.ident);
                advance(p);
                return ev_const(0);
            }
            advance(p); /* consume IDENT */
            if (sym->is_array) {
                c0_error_at(line, "a cast of an array name is not yet "
                                   "supported - see src/mutos_cc/README.md");
                return ev_dynamic();
            }
            if (ty_is_float(target) || ty_is_float(sym->type)) {
                /* See "Floating point" and the comment above. */
                if (!ty_is_float(sym->type) && sym->type != TY_INT &&
                    sym->type != TY_LONG) {
                    c0_error_at(line, "this cast combination is not yet "
                                       "supported - see src/mutos_cc/README.md");
                    return ev_dynamic();
                }
                emit_name(t1, sym, sym->type);
                if (ty_is_float(sym->type) && ty_is_float(target))
                    return ev_dynamic_typed(sym->type); /* no conversion */
                if (ty_is_float(target)) {
                    emit_to_float(t1, target, ev_dynamic_typed(sym->type), line);
                    return ev_dynamic_typed(target);
                }
                emit_from_float(t1, target, line);
                return ev_dynamic_typed(target);
            }
            if (sym->type == target && target == TY_INT) {
                /* int to int (an enum variable's "(int) c" - 06_struct/
                 * 08_enum.1.golden): no conversion, the NAME alone. */
                emit_name(t1, sym, sym->type);
                return ev_dynamic_typed(target);
            }
            emit_name(t1, sym, sym->type);
            int optag;
            if (sym->type == TY_LONG && target == TY_INT)
                optag = OP_LTOI;
            else if (sym->type == TY_INT && target == TY_CHAR)
                optag = OP_ITOC;
            else if (sym->type == TY_CHAR && target == TY_LONG)
                optag = OP_CTOL;
            else if (sym->type == TY_CHAR && target == TY_INT)
                optag = OP_ITOC; /* type TY_INT: char -> int - the same
                                  * conversion "return c;" gets, which v7
                                  * builds through the very code path a
                                  * cast uses (see convert_assign()) */
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
            /* v7 never folds SEQNC (v7/cc/c01.c's build()), so the
             * last item is a real operand, written BEFORE the SEQNC
             * node - a constant one used to be written after it,
             * which paired SEQNC with the wrong operands
             * ("y + (x = 1, 5)" added y's slot to 1). */
            emit_materialize(t1, rhs);
            /* v7 types SEQNC like its right operand ("t = t2"), so a
             * char last item would make it a char SEQNC - unconfirmed,
             * and this file writes SEQNC(TY_INT) - refused. */
            (void)char_value_refused(rhs, p->cur.line,
                                     "the last operand of a comma operator");
            outcode(t1, "BN", OP_SEQNC, TY_INT);
            v = rhs;
            v.is_const = 0;
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
    if (p->cur.kind == T_AMP) {
        /* '&' IDENT - address-of, only a plain variable name so far
         * (matches 05_arrptr/03_ptrbasic.c's "&x"/"&y" and
         * 06_ptrptr.c's "&p" - every confirmed AMPER operand in this
         * grammar scope). Reuses exactly the same NAME+AMPER shape
         * array-decay already established (parse_primary's is_array
         * case below), generalized via ty_ptr_of() to any base
         * type/degree - confirmed against 06_ptrptr.1.golden's
         * "pp = &p;" (AMPER of an already-pointer-typed operand,
         * giving one more degree, type 40 - see ty_ptr_of()'s own
         * comment above). */
        int line = p->cur.line;
        advance(p);
        if (p->cur.kind != T_IDENT) {
            c0_error_at(line, "'&' is only supported on a plain "
                               "variable name so far - see "
                               "src/mutos_cc/README.md");
            return ev_dynamic();
        }
        SymEntry *sym = lookup_var(p, p->cur.ident);
        if (!sym) {
            c0_error_at(p->cur.line, "'%s' undeclared", p->cur.ident);
            advance(p);
            return ev_const(0);
        }
        {
            TokKind k2 = peek2_kind(p);
            if (k2 == T_DOT || k2 == T_ARROW ||
                (k2 == T_LBRACK && sym->sdef && !sym->dim2)) {
                /* The address of a member or of a struct array's element -
                 * v7's build(AMPER) of the chain's STAR: the chain, then
                 * AMPER("pointer to" its type). No golden has one. */
                advance(p); /* IDENT */
                int is_field;
                ExprVal v = parse_postfix_chain(p, t1, sym, &is_field);
                if (is_field) {
                    c0_error_at(line, "the address of a bit-field");
                    return ev_dynamic();
                }
                int rt = ty_ptr_of(v.type);
                outcode(t1, "BN", OP_AMPER, rt);
                ExprVal r = ev_dynamic_typed(rt);
                r.sdef = v.sdef;
                return r;
            }
        }
        if (peek2_kind(p) == T_LBRACK && (sym->is_array || sym->is_ptr)) {
            /* '&' IDENT '[' expr ']' - the address of one element
             * ("swapch(&s[lo], &s[hi]);" - 10_integ/04_strrev.c): v7's
             * build(AMPER) takes the address of the subscript's STAR
             * node, so the stream is the ordinary element reference
             * followed by AMPER("pointer to element") - confirmed
             * against 04_strrev.1.golden: NAME(s) NAME(lo) CON(1)
             * ITOP(9) PLUS(9) STAR(1) AMPER(9). mutos_c1 cancels the
             * STAR/AMPER pair ("&*x" is "x"). A 2-D array's element
             * address has no golden. */
            if (sym->is_array && sym->dim2 > 0) {
                c0_error_at(line, "'&' of a 2-D array element is not yet "
                                   "supported - see src/mutos_cc/README.md");
                advance(p);
                return ev_dynamic();
            }
            advance(p); /* consume IDENT - emit_subscript() starts at '[' */
            int elem = emit_subscript(p, t1, sym);
            int rt = ty_ptr_of(elem);
            outcode(t1, "BN", OP_AMPER, rt);
            return ev_dynamic_typed(rt);
        }
        if (sym->is_array) {
            /* "&a" on an array: v7/cc/c01.c's build() deliberately
             * skips disarray() for AMPER, so the real tree is NAME
             * typed as the ARRAY itself plus AMPER("pointer to array")
             * - e.g. NAME(24)/AMPER(104) for "int a[N]" - not the
             * NAME(sym->type)/AMPER(ty_ptr_of()) shape below. No golden
             * confirms either, so refuse rather than emit the wrong
             * type. */
            c0_error_at(line, "'&' on an array is not yet supported - see "
                               "src/mutos_cc/README.md");
            advance(p);
            return ev_dynamic();
        }
        advance(p);
        emit_name(t1, sym, sym->type);
        int rt = ty_ptr_of(sym->type);
        outcode(t1, "BN", OP_AMPER, rt);
        /* "&p" of a struct: NAME(p, STRUCT) AMPER(PTR.STRUCT) - 06_struct/
         * 02_stptr.1.golden's "move(&p, 5, 7)" */
        ExprVal r = ev_dynamic_typed(rt);
        r.sdef = sym->sdef;
        return r;
    }
    if (p->cur.kind == T_STAR) {
        /* '*' unary-expr - general pointer dereference used as an
         * RVALUE (as opposed to parse_star_assign_stmt()'s
         * statement-level lvalue form) - "y = *p;" (03_ptrbasic.c)
         * and "*(a + i)" (04_ptrarreq.c, via parse_add()'s own
         * pointer-arithmetic case handling the '+' inside the
         * parens - see its comment). Recurses through parse_unary()
         * itself for the operand, so a chain of leading '*'s
         * ("**pp") decrefs one degree at a time, same as
         * parse_star_assign_stmt()'s own multi-star lvalue loop -
         * confirmed against 06_ptrptr.1.golden's "**pp = 6;" (two
         * chained STAR nodes, TY_PTR_INT(8) then TY_INT(0)). */
        int line = p->cur.line;
        advance(p);
        ExprVal v = parse_unary(p, t1);
        emit_materialize(t1, v);
        if (v.type == TY_INT || v.type == TY_CHAR || v.type == TY_LONG) {
            c0_error_at(line, "'*' on a non-pointer operand is not "
                               "yet supported - see "
                               "src/mutos_cc/README.md");
            return ev_dynamic();
        }
        int elemtype = ty_decref(v.type);
        if (elemtype == TY_LONG)
            refuse_long_access(p);
        outcode(t1, "BN", OP_STAR, elemtype);
        return ev_char_obj(elemtype);
    }
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
        if (p->cur.kind == T_KW_STRUCT || p->cur.kind == T_KW_UNION ||
            p->cur.kind == T_KW_UNSIGNED || p->cur.kind == T_KW_ENUM ||
            p->cur.kind == T_KW_FLOAT || p->cur.kind == T_KW_DOUBLE ||
            (p->cur.kind == T_IDENT && find_typedef(p, p->cur.ident) &&
             !lookup_var(p, p->cur.ident))) {
            /* A type name - v7's length() of it: a struct's size ("sizeof
             * (struct node)" is CON(UNSIGN, 4) in 10_integ/03_linklist.
             * 1.golden), a pointer's a word, a float 4 and a double 8
             * (v7's SZFLOAT/SZDOUB - no golden takes either's size). */
            int line = p->cur.line;
            TypeSpec ts = parse_type_spec(p);
            int t = ts.type;
            while (p->cur.kind == T_STAR) {
                t = ty_ptr_of(t);
                advance(p);
            }
            if (t == TY_STRUCT && (!ts.sdef || !ts.sdef->complete))
                c0_error_at(line, "sizeof an incomplete struct");
            size = type_size(t, ts.sdef, 0);
        } else if (p->cur.kind == T_KW_INT) {
            size = MCC_SZINT;
            advance(p);
        } else if (p->cur.kind == T_KW_CHAR) {
            size = MCC_SZCHAR;
            advance(p);
        } else if (p->cur.kind == T_KW_LONG) {
            size = MCC_SZLONG;
            advance(p);
        } else if (p->cur.kind == T_IDENT) {
            SymEntry *sym = lookup_var(p, p->cur.ident);
            if (!sym) {
                c0_error_at(p->cur.line, "'%s' undeclared", p->cur.ident);
                size = 0;
            } else if (sym->is_array) {
                c0_error_at(p->cur.line, "sizeof of an array is not yet "
                                          "supported - see src/mutos_cc/README.md");
                size = 0;
            } else if (sym->is_ptr) {
                size = MCC_SZINT; /* a pointer is word-sized, same as int */
            } else if (sym->type == TY_STRUCT) {
                size = type_size(TY_STRUCT, sym->sdef, 0);
            } else switch (sym->type) {
                case TY_CHAR: size = MCC_SZCHAR; break;
                case TY_LONG: size = MCC_SZLONG; break;
                case TY_FLOAT: size = MCC_SZFLOAT; break;
                case TY_DOUBLE: size = MCC_SZDOUB; break;
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
        SymEntry *sym = lookup_var(p, p->cur.ident);
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
        if (ty_is_float(sym->type))
            c0_error_at(line, "'++'/'--' on a 'float'/'double' variable is not "
                               "yet supported - see src/mutos_cc/README.md");
        advance(p);
        emit_name(t1, sym, sym->type);
        emit_incdec(t1, optag, sym->type, sym->is_ptr);
        return ev_dynamic();
    }
    if (p->cur.kind == T_MINUS) {
        int line = p->cur.line;
        advance(p);
        ExprVal v = parse_unary(p, t1);
        if (v.is_const)
            return ev_const(trunc16(-v.value));
        if (ty_is_float(v.type)) {
            /* NEG on a floating value (see "Floating point"): v7's build()
             * gives a unary operator its operand's own type - FLOAT for a
             * float variable, DOUBLE for anything else floating; nothing
             * is folded (fold() handles integer CONs only, so "-2.5" is
             * FCON "2.5" and NEG). */
            outcode(t1, "BN", OP_NEG, v.type);
            return ev_dynamic_typed(v.type);
        }
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
        /* v7 converts no unary operand (build()'s non-BINARY path) - a
         * char here would be a char-typed COMPL, which has no golden. */
        (void)char_value_refused(v, line, "the operand of '~'");
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
        (void)char_nonobj_refused(v, line, "the operand of '!'");
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
            int line = p->cur.line;
            advance(p);
            int lchar = (v.type == TY_CHAR && !v.is_const);
            if (!ty_is_float(v.type))
                v = promote_char(t1, v); /* before the right operand's bytes */
            RhsCapture cap;
            ExprVal r = parse_unary(p, arith_rhs_begin(&cap, p, t1, v));
            if (float_arith(p, t1, &cap, &v, r, lchar, OP_TIMES, line))
                continue;
            r = promote_char(t1, r);
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
            (void)long_mix_refused(v, r, p->cur.line);
            int optype = arith_type(v, r);
            outcode(t1, "BN", OP_TIMES, optype);
            v = ev_dynamic_typed(optype);
        } else if (p->cur.kind == T_SLASH) {
            int line = p->cur.line;
            advance(p);
            int lchar = (v.type == TY_CHAR && !v.is_const);
            if (!ty_is_float(v.type))
                v = promote_char(t1, v); /* before the right operand's bytes */
            RhsCapture cap;
            ExprVal r = parse_unary(p, arith_rhs_begin(&cap, p, t1, v));
            if (float_arith(p, t1, &cap, &v, r, lchar, OP_DIVIDE, line))
                continue;
            r = promote_char(t1, r);
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
            (void)unsigned_refused(v, r, line, "/");
            (void)long_mix_refused(v, r, p->cur.line);
            int optype = arith_type(v, r);
            outcode(t1, "BN", OP_DIVIDE, optype);
            v = ev_dynamic_typed(optype);
        } else if (p->cur.kind == T_PERCENT) {
            int line = p->cur.line;
            advance(p);
            v = promote_char(t1, v); /* before the right operand's bytes */
            RhsCapture cap;
            ExprVal r = parse_unary(p, rhs_begin(&cap, p, t1, v));
            v = rhs_end(&cap, p, t1, v, r);
            r = promote_char(t1, r);
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
            (void)unsigned_refused(v, r, line, "%");
            (void)long_mix_refused(v, r, p->cur.line);
            int optype = arith_type(v, r);
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
            int line = p->cur.line;
            advance(p);
            int lchar = (v.type == TY_CHAR && !v.is_const);
            if (!ty_is_float(v.type))
                v = promote_char(t1, v); /* before the right operand's bytes */
            RhsCapture cap;
            ExprVal r = parse_mul(p, arith_rhs_begin(&cap, p, t1, v));
            if (float_arith(p, t1, &cap, &v, r, lchar, OP_PLUS, line))
                continue;
            r = promote_char(t1, r);
            if (v.is_const && r.is_const) {
                v = ev_const(trunc16(v.value + r.value));
                continue;
            }
            if (!r.is_const && ty_is_ptr(r.type)) {
                /* v7's build() scales the LEFT operand here (cvtab[int]
                 * [ptr]'s leftc ITP), whose bytes are already written;
                 * compiled as an int '+' this silently added the
                 * unscaled integer (found 2026-09-26). */
                c0_error_at(p->cur.line, "%s is not yet supported - see "
                            "src/mutos_cc/README.md",
                            ty_is_ptr(v.type) ? "adding two pointers"
                                              : "an integer plus a pointer "
                                                "(the pointer on the right)");
                v = ev_dynamic();
                continue;
            }
            if (!v.is_const && ty_is_ptr(v.type)) {
                /* pointer + int - array/pointer subscript arithmetic
                 * ("*(a + i)" - 05_arrptr/04_ptrarreq.c's confirmed
                 * K&R array/pointer-equivalence idiom; `v` (the
                 * pointer) is already emitted (a NAME, from
                 * parse_mul()/parse_primary()) - only `r` (the int
                 * offset) still needs the CON/ITOP scaling step,
                 * matching emit_subscript()'s own tail exactly. */
                emit_materialize(t1, r);
                int elemtype = ty_decref(v.type);
                outcode(t1, "BNN", OP_CON, TY_INT, size_of_type(elemtype));
                outcode(t1, "BN", OP_ITOP, v.type);
                outcode(t1, "BN", OP_PLUS, v.type);
                v = ev_dynamic_typed(v.type);
                continue;
            }
            emit_materialize(t1, v);
            emit_materialize(t1, r);
            /* TY_LONG iff either operand is 'long' - confirmed
             * against 02_long/01_addsub.1.golden's "c = a + b;" (a, b
             * both 'long' -> OP_PLUS(TY_LONG)), same rule already
             * confirmed for '*'/'/' /'%' in parse_mul() above. */
            (void)long_mix_refused(v, r, p->cur.line);
            int optype = arith_type(v, r);
            outcode(t1, "BN", OP_PLUS, optype);
            v = ev_dynamic_typed(optype);
        } else if (p->cur.kind == T_MINUS) {
            int line = p->cur.line;
            advance(p);
            int lchar = (v.type == TY_CHAR && !v.is_const);
            if (!ty_is_float(v.type))
                v = promote_char(t1, v); /* before the right operand's bytes */
            RhsCapture cap;
            ExprVal r = parse_mul(p, arith_rhs_begin(&cap, p, t1, v));
            if (float_arith(p, t1, &cap, &v, r, lchar, OP_MINUS, line))
                continue;
            r = promote_char(t1, r);
            if (v.is_const && r.is_const) {
                v = ev_const(trunc16(v.value - r.value));
                continue;
            }
            if (ty_is_ptr(v.type) || ty_is_ptr(r.type)) {
                /* "p - 1" must step back one ELEMENT (ITOP scaling, as
                 * for '+'), "q - p" divide the byte difference by the
                 * element size (v7's build(): PTI after the MINUS); as a
                 * plain int '-' both were silently wrong (found
                 * 2026-09-26). */
                c0_error_at(p->cur.line, "pointer subtraction is not yet "
                            "supported - see src/mutos_cc/README.md");
                v = ev_dynamic();
                continue;
            }
            emit_materialize(t1, v);
            emit_materialize(t1, r);
            (void)long_mix_refused(v, r, p->cur.line);
            int optype = arith_type(v, r);
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
        v = promote_char(t1, v); /* before the right operand's bytes */
        RhsCapture cap;
        ExprVal r = parse_add(p, rhs_begin(&cap, p, t1, v));
        v = rhs_end(&cap, p, t1, v, r);
        r = promote_char(t1, r);
        if (v.is_const && r.is_const) {
            /* Folded on the host: the left shift goes through an
             * unsigned intermediate (shifting a negative signed value
             * is undefined behavior in C - found by a UBSan run on
             * "(0 - 7) << 3"; the resulting bits are unchanged), and a
             * count outside 0..15 - undefined in C and meaningless
             * for a 16-bit int - is refused rather than folded to
             * whatever the host happens to produce. */
            if (r.value < 0 || r.value > 15) {
                c0_error_at(p->cur.line, "shift count %ld out of range "
                                          "in a constant expression",
                            r.value);
                v = ev_const(0);
                continue;
            }
            long res = (op == OP_LSHIFT)
                     ? (long)((unsigned long)v.value << r.value)
                     : (v.value >> r.value);
            v = ev_const(trunc16(res));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        if (op == OP_RSHIFT)
            (void)unsigned_refused(v, r, p->cur.line, ">>");
        int stype = (arith_type(v, r) == TY_UNSIGN) ? TY_UNSIGN : TY_INT;
        outcode(t1, "BN", op, stype);
        v = ev_dynamic_typed(stype);
    }
    return v;
}

/* RELATIONAL := SHIFT (('<'|'<='|'>'|'>=') SHIFT)* - a non-constant
 * comparison emits its operator node exactly like any other binary
 * op (OP_LESS/OP_LESSEQ/OP_GREAT/OP_GREATEQ, "BN" shape) - c1 (not
 * c0) is what turns a comparison into a materialized 0/1 value or
 * fuses it into a short-circuit branch; c0's job is only to emit the
 * tree. Confirmed byte-for-byte against 03_rellogic's ".1.golden". */
/*
 * The right operand of a relational or equality operator `op` whose left
 * operand `*l` is parsed (and, unless still a pending constant, emitted);
 * `rhs_parser` parses it at the next-tighter precedence level. Returns
 * 1 when the whole comparison has been written here - a char compared
 * with a small constant, see below - and the caller's result is then an
 * ordinary dynamic int. Otherwise returns 0 with *l and *r the two
 * operands, each char among them widened (promote_char()), for the
 * caller to fold or emit as before.
 *
 * A char OBJECT (a char variable, element or dereference as written -
 * ExprVal's char_obj) compared with a constant in 0..127 is not widened:
 * the constant is typed as a char instead, and the node stays int -
 * 10_integ/01_wordcount.1.golden's "text[i] != '\0'", "text[i] ==
 * '\n'" and "text[i] == ' '":
 *
 *     ... PLUS(9) STAR(1)  CON(1, 0)  NEQUAL(0)        (no ITOC)
 *
 * against "buf[0] + buf[79]"'s ITOC(0) on both operands. This is
 * v7/cc/c12.c optim()'s rule - "if (tree->tr1->type==CHAR &&
 * tree->tr2->op==CON && ... && tree->tr2->value <= 127 &&
 * tree->tr2->value >= 0) tree->tr2->type = CHAR;" for any RELAT
 * operator - applied by the MUTOS front end itself, in temp1 (vanilla
 * v7 widens nothing in c0 at all, so it can leave it to c1). Only the
 * STAR operand is confirmed; a char variable is taken the same way (the
 * rule names no operand shape beyond "addressable or a STAR", and its
 * code, "cmpb *-8.(bp),*97.", is in tests/mutos_as/kernel_nonopt/
 * lp_AC.s). Anything else - a constant outside 0..127, a constant on
 * the left, a char compared with a non-constant - keeps the conversions
 * this file already wrote (and mutos_c1 refuses a widened char in a
 * comparison, as before). Whether ITOC goes in front of the right
 * operand is only known once that is parsed, so the right operand is
 * buffered first (capture_begin()).
 */
static int char_compare_rhs(Parser *p, FILE *t1, int op, ExprVal *l,
                            ExprVal *r,
                            ExprVal (*rhs_parser)(Parser *, FILE *))
{
    RhsCapture cap;
    int line = p->cur.line;
    if (!l->is_const && ty_is_float(l->type)) {
        /* A floating comparison (see "Floating point"): v7's build()
         * converts an int operand to the floating one's type and types
         * the relational node INT ("if (dope&RELAT) t = INT"); nothing
         * else. A floating left operand needs no conversion, so the right
         * one follows it directly. */
        ExprVal rv = rhs_parser(p, t1);
        if (!rv.is_const && rv.type == TY_CHAR)
            c0_error_at(line, "a 'char' operand compared with a 'float'/"
                              "'double' one is not yet supported - see "
                              "src/mutos_cc/README.md");
        emit_materialize(t1, rv);
        emit_to_float(t1, l->type, rv, line);
        outcode(t1, "BN", op, TY_INT);
        return 1;
    }
    if (l->char_obj) {
        ExprVal rv = rhs_parser(p, capture_begin(&cap, p, t1));
        if (rv.is_const && !rv.is_long && rv.value >= 0 && rv.value <= 127) {
            capture_flush(&cap, t1);        /* a constant wrote nothing */
            outcode(t1, "BNN", OP_CON, TY_CHAR, (int)rv.value);
            outcode(t1, "BN", op, TY_INT);
            return 1;
        }
        *l = promote_char(t1, *l);          /* after the left operand */
        capture_flush(&cap, t1);            /* then the right one */
        *r = promote_char(t1, rv);
        return 0;
    }
    ExprVal v = promote_char(t1, *l);       /* before the right operand's bytes */
    /* The right operand is buffered - it may turn out floating, and then
     * an integer left operand's conversion must come between the two
     * (the same capture float_arith() ends for '+ - * /'). For an integer
     * comparison the bytes written are the same either way: rhs_end()'s
     * order - a pending constant's CON, then the right operand. */
    ExprVal rv = rhs_parser(p, capture_begin(&cap, p, t1));
    if (cap.mem) {
        fclose(cap.mem);
        cap.mem = NULL;
    }
    if (!rv.is_const && ty_is_float(rv.type)) {
        if (!v.is_const && v.type == TY_CHAR)
            c0_error_at(line, "a 'char' operand compared with a 'float'/"
                              "'double' one is not yet supported - see "
                              "src/mutos_cc/README.md");
        emit_materialize(t1, v);
        emit_to_float(t1, rv.type, v, line);
        if (cap.buf)
            fwrite(cap.buf, 1, cap.len, t1);
        free(cap.buf);
        outcode(t1, "BN", op, TY_INT);
        return 1;
    }
    if (v.is_const && rv.is_const) {
        /* A constant writes nothing (see ExprVal) - the caller folds. */
        if (cap.len != 0)
            c0_error_at(p->cur.line, "internal: a constant operand wrote "
                        "intermediate code");
    } else {
        emit_materialize(t1, v);
        v.is_const = 0; /* emitted - keeps its type */
        if (cap.buf)
            fwrite(cap.buf, 1, cap.len, t1);
    }
    free(cap.buf);
    *l = v;
    *r = promote_char(t1, rv);
    return 0;
}

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
        ExprVal r;
        if (char_compare_rhs(p, t1, op, &v, &r, parse_shift)) {
            v = ev_dynamic();
            continue;
        }
        (void)unsigned_refused(v, r, p->cur.line, "an ordered comparison");
        (void)long_mix_refused(v, r, p->cur.line);
        if (ty_is_ptr(v.type) || ty_is_ptr(r.type)) {
            /* v7 orders pointers UNSIGNED - build() turns the operator
             * into LESSP/LESSEQP/GREATP/GREATEQP ("op =+ LESSEQP-LESSEQ")
             * - which neither this file nor mutos_c1 has; a signed LESS
             * misorders addresses above 0x7FFF (found 2026-09-26). */
            c0_error_at(p->cur.line, "an ordered comparison of pointers is "
                        "not yet supported - see src/mutos_cc/README.md");
        }
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
        ExprVal r;
        if (char_compare_rhs(p, t1, op, &v, &r, parse_relational)) {
            v = ev_dynamic();
            continue;
        }
        (void)long_mix_refused(v, r, p->cur.line);
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
        v = promote_char(t1, v); /* before the right operand's bytes */
        RhsCapture cap;
        ExprVal r = parse_equality(p, rhs_begin(&cap, p, t1, v));
        v = rhs_end(&cap, p, t1, v, r);
        r = promote_char(t1, r);
        if (v.is_const && r.is_const) {
            v = ev_const(trunc16(v.value & r.value));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        if (v.type == TY_LONG || r.type == TY_LONG)
            c0_error_at(p->cur.line, "'&', '|' or '^' with a 'long' operand is "
                        "not yet supported - see src/mutos_cc/README.md");
        int btype = (arith_type(v, r) == TY_UNSIGN) ? TY_UNSIGN : TY_INT;
        outcode(t1, "BN", OP_AND, btype);
        v = ev_dynamic_typed(btype);
    }
    return v;
}

static ExprVal parse_bitxor(Parser *p, FILE *t1)
{
    ExprVal v = parse_bitand(p, t1);
    while (p->cur.kind == T_CARET) {
        advance(p);
        v = promote_char(t1, v); /* before the right operand's bytes */
        RhsCapture cap;
        ExprVal r = parse_bitand(p, rhs_begin(&cap, p, t1, v));
        v = rhs_end(&cap, p, t1, v, r);
        r = promote_char(t1, r);
        if (v.is_const && r.is_const) {
            v = ev_const(trunc16(v.value ^ r.value));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        if (v.type == TY_LONG || r.type == TY_LONG)
            c0_error_at(p->cur.line, "'&', '|' or '^' with a 'long' operand is "
                        "not yet supported - see src/mutos_cc/README.md");
        int btype = (arith_type(v, r) == TY_UNSIGN) ? TY_UNSIGN : TY_INT;
        outcode(t1, "BN", OP_EXOR, btype);
        v = ev_dynamic_typed(btype);
    }
    return v;
}

static ExprVal parse_bitor(Parser *p, FILE *t1)
{
    ExprVal v = parse_bitxor(p, t1);
    while (p->cur.kind == T_PIPE) {
        advance(p);
        v = promote_char(t1, v); /* before the right operand's bytes */
        RhsCapture cap;
        ExprVal r = parse_bitxor(p, rhs_begin(&cap, p, t1, v));
        v = rhs_end(&cap, p, t1, v, r);
        r = promote_char(t1, r);
        if (v.is_const && r.is_const) {
            v = ev_const(trunc16(v.value | r.value));
            continue;
        }
        emit_materialize(t1, v);
        emit_materialize(t1, r);
        if (v.type == TY_LONG || r.type == TY_LONG)
            c0_error_at(p->cur.line, "'&', '|' or '^' with a 'long' operand is "
                        "not yet supported - see src/mutos_cc/README.md");
        int btype = (arith_type(v, r) == TY_UNSIGN) ? TY_UNSIGN : TY_INT;
        outcode(t1, "BN", OP_OR, btype);
        v = ev_dynamic_typed(btype);
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
        (void)char_nonobj_refused(v, p->cur.line, "an operand of '&&'/'||'");
        RhsCapture cap;
        ExprVal r = parse_bitor(p, rhs_begin(&cap, p, t1, v));
        v = rhs_end(&cap, p, t1, v, r);
        (void)char_nonobj_refused(r, p->cur.line, "an operand of '&&'/'||'");
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
        (void)char_nonobj_refused(v, p->cur.line, "an operand of '&&'/'||'");
        RhsCapture cap;
        ExprVal r = parse_logand(p, rhs_begin(&cap, p, t1, v));
        v = rhs_end(&cap, p, t1, v, r);
        (void)char_nonobj_refused(r, p->cur.line, "an operand of '&&'/'||'");
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
    /* The stream order is cond, true branch, false branch, COLON,
     * QUEST - so, exactly as for a binary operator (see rhs_begin()),
     * a branch is parsed into a buffer while anything before it is
     * still an unwritten constant: the true branch when the condition
     * is one, the false branch when the condition or the true branch
     * is. (Before 2026-09-24 a constant true branch was written after
     * the false one - "c ? 7 : y" selected y - and a constant condition
     * returned one branch while the other's bytes stayed in the
     * stream.) */
    RhsCapture tcap, fcap;
    ExprVal t = parse_logor(p, rhs_begin(&tcap, p, t1, cond));
    if (!expect(p, T_COLON, "':'")) {
        capture_flush(&tcap, t1);
        return ev_dynamic();
    }
    ExprVal pending = cond.is_const ? cond : t;
    ExprVal f = parse_logor(p, rhs_begin(&fcap, p, t1, pending));
    /* v7's QUEST/COLON convert nothing (COLON only balances int/pointer
     * types); a char condition or arm has no golden - refused. */
    (void)(char_nonobj_refused(cond, p->cur.line, "a '?:' condition") ||
           char_value_refused(t, p->cur.line, "a '?:' result") ||
           char_value_refused(f, p->cur.line, "a '?:' result"));

    if (cond.is_const && t.is_const && f.is_const) {
        /* v7/cc/c01.c's fold(QUEST): folded only when the condition
         * AND both branches are constants (nothing was written); any
         * other constant condition is built as a real QUEST tree. */
        capture_flush(&tcap, t1);
        capture_flush(&fcap, t1);
        return (cond.value != 0) ? t : f;
    }
    emit_materialize(t1, cond);
    capture_flush(&tcap, t1);
    emit_materialize(t1, t);
    capture_flush(&fcap, t1);
    emit_materialize(t1, f);
    outcode(t1, "BN", OP_COLON, TY_INT);
    outcode(t1, "BN", OP_QUEST, TY_INT);
    return ev_dynamic();
}

/* ------------------------------------------------------------------ */
/* Declarations */

/*
 * decl := ('int'|'char'|'long') declarator (',' declarator)* ';'
 * declarator := '*'* IDENT ('[' ICON ']' ('[' ICON ']')?)?
 *             | '(' '*' IDENT ')' '(' ')'      ('int' only)
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
 * incrementable lvalue. Multi-level pointers ("int **pp;") and 2-D
 * arrays ("int m[N][M];" - see the declarator loop's own comment)
 * followed later; three or more array dimensions are not supported -
 * see src/mutos_cc/README.md.
 *
 * 'char'/'long' locals - confirmed against 08_castsize.1.golden/
 * .s.golden's "long l;"/"char c;" ANAME offsets - take the same
 * declarator forms as 'int' (except a function pointer or a 2-D
 * array): "char buf[20];", "char *s;" and "char *names[3];" are
 * confirmed by 05_arrptr/07_strlibc.1.golden and 05_arrofptr.1.golden
 * (every size goes through rlength() - see there). Reading or writing
 * a 'long' ELEMENT is refused elsewhere (see refuse_long_access()); a
 * 'char' one is read and written with the conversions promote_char()'s
 * section describes. A 'long' occupies MCC_SZLONG (4) bytes of frame space,
 * matching its real value size. A 'char' occupies MCC_SZINT (2) bytes
 * of frame space DESPITE its real value size being MCC_SZCHAR (1) -
 * confirmed by "long l;" (offset -10) immediately followed by
 * "char c;" landing at offset -12, a 2-byte gap, not 1 - this target
 * always word-aligns an AUTO local's stack slot, matching int's own
 * slot size, even for a byte-sized value (see OP_ITOC's/OP_CTOL's
 * "movb" handling in c1_gen.c for how the 1-byte VALUE is actually
 * read/written within that 2-byte slot).
 *
 * An optional leading 'register' (04_funcs/06_regclass.c) is handled
 * for the plain (non-pointer, non-array) 'int' declarator shape only
 * - see try_claim_register()'s own comment for the allocation
 * algorithm (mirrors v7/cc/c03.c's goodreg() exactly, just with
 * MCC_INIT_REGVAR's smaller slot count) and c1_gen.c's OP_NAME/
 * OP_RNAME handling for how a claimed register variable is rendered.
 * 'register' on a pointer/array declarator, on a 'char'/'long'
 * declarator, or once no more register slots remain, silently falls
 * back to an ordinary AUTO local - exactly matching v7's own
 * goodreg()-fails-so-skw=AUTO fallback, and 06_regclass.c's own
 * comment ("a K&R compiler is free to ignore [register]").
 */
static int try_claim_register(Parser *p)
{
    /* v7/cc/c03.c's goodreg(): fails once fewer than 3 slots remain
     * (MUTOS has exactly 2 claimable "working" registers - di/si -
     * unlike v7's own larger PDP-11 register set; see
     * MCC_INIT_REGVAR's own comment in mutos_cc.h). regvar's own
     * numbering IS the slot's identity, decremented once per
     * successful claim and never reused within a function - see
     * c1_gen.c's regvar-to-physical-register mapping (confirmed only
     * for slot 3 = di, by 06_regclass.1.golden/.s.golden; slot 2 = si
     * is the structurally next slot this same algorithm would hand
     * out, extrapolated but not itself golden-confirmed). */
    if (p->regvar < 3)
        return -1;
    return --p->regvar;
}

/* v7/cc/c04.c's rlength(): an object's size rounded up to a whole
 * number of words ("(length(cs)+ALIGN) & ~ALIGN", ALIGN = 1) - the
 * size an AUTO local actually occupies in the frame. Confirmed for a
 * 1-byte object by 08_castsize.1.golden ("char c;" takes 2 bytes of
 * frame) and for an even-sized array by 05_arrptr/07_strlibc.1.golden
 * ("char src[20];" at -24, then "char dst[20];" at -44); an
 * odd-length char array ("char s[5];" -> 6 bytes) follows the same
 * rule but no golden shows one. */
static int rlength(long bytes)
{
    return (int)((bytes + 1) & ~1L);
}

static void parse_decl(Parser *p, FILE *t1)
{
    int is_register = 0;
    if (p->cur.kind == T_KW_REGISTER) {
        is_register = 1;
        advance(p); /* consume 'register' */
    }

    if (!at_type_spec(p)) {
        c0_error_at(p->cur.line,
            "only 'int'/'char'/'long'/'float'/'double', struct, union, enum "
            "and typedef'd local declarations are supported so far - see "
            "src/mutos_cc/README.md");
        while (p->cur.kind != T_SEMI && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
        return;
    }
    /* The type: a keyword, or (06_struct) a struct/union/enum specifier
     * or a typedef name - see parse_type_spec(). */
    int tline = p->cur.line;
    TypeSpec ts = parse_type_spec(p);
    int basetype = ts.type;
    StructDef *bsdef = ts.sdef;
    /* A plain 'unsigned' local is accepted (fltprobe/p16_open2.1.golden:
     * NAME typed UNSIGN(7), "u = 5;" an ASSIGN(7) of CON 5) - its operators
     * are those of a bit-field's unsigned value (arith_type(),
     * unsigned_refused()); a pointer to one, an array of them or a
     * 'register' one is refused below. */
    if (basetype == TY_UNSIGN && is_register)
        c0_error_at(tline, "a 'register unsigned' variable is not yet "
                           "supported - see src/mutos_cc/README.md");
    if (p->cur.kind == T_SEMI) {
        /* A struct/union/enum declared inside a function, no variable -
         * nothing to allocate or write. */
        advance(p);
        return;
    }

    for (;;) {
        if (p->cur.kind == T_LPAREN) {
            /* Function-pointer declarator: '(' '*' IDENT ')' '(' ')'
             * - e.g. "int (*fp)();" - confirmed against
             * 07_funcptr.1.golden's main()'s "_fp" ANAME. Only this
             * exact shape (an empty parameter list, an 'int' result)
             * is supported - see src/mutos_cc/README.md. */
            int pline = p->cur.line;
            advance(p); /* consume '(' */
            if (basetype != TY_INT) {
                c0_error_at(pline, "a pointer to a function returning "
                                    "'char'/'long' is not yet supported - "
                                    "see src/mutos_cc/README.md");
                break;
            }
            if (!expect(p, T_STAR, "'*'"))
                break;
            if (p->cur.kind != T_IDENT) {
                c0_error_at(p->cur.line,
                    "expected an identifier in a function-pointer "
                    "declaration");
                break;
            }
            char fname[LEX_IDENT_MAX];
            strncpy(fname, p->cur.ident, sizeof fname - 1);
            fname[sizeof fname - 1] = '\0';
            int fline = p->cur.line;
            advance(p); /* consume IDENT */
            if (!expect(p, T_RPAREN, "')'"))
                break;
            if (!expect(p, T_LPAREN, "'('"))
                break;
            if (!expect(p, T_RPAREN, "')'"))
                break;

            SymEntry *fsym = symtab_declare_auto(&p->syms, fname,
                                                  TY_PTR_FUNC_INT, MCC_SZINT);
            if (!fsym) {
                c0_error_at(fline, "'%s' redeclared", fname);
            } else {
                outcode(t1, "BSN", OP_ANAME, fsym->name, fsym->offset);
            }

            if (p->cur.kind == T_COMMA) {
                advance(p);
                continue;
            }
            break;
        }

        /* Zero or more leading '*'s - "int *p;"/"int **pp;" (05_arrptr/
         * 06_ptrptr.c: ty_ptr_of() chained, type 40 for "int **" - see
         * its own comment above), "char *s;" (type 9). */
        int ptr_degree = 0;
        while (p->cur.kind == T_STAR) {
            ptr_degree++;
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

        /* The declared (element) type: the base type plus one PTR
         * degree per '*'. For an array this is its ELEMENT type - an
         * array is recorded by SymEntry.is_array, not by an ARRAY
         * degree in `type` (see c0_sym.h) - so "char *names[3];"
         * (05_arrptr/05_arrofptr.c, an array of pointers - the shape
         * argv has) is a 3-element array of type 9, confirmed by
         * 05_arrofptr.1.golden's NAME(9)/AMPER(41) for "names[i]". */
        int decltype = basetype;
        for (int k = 0; k < ptr_degree; k++)
            decltype = ty_ptr_of(decltype);

        int is_array = 0;
        long arraylen = 0;
        long dim2 = 0;
        if (p->cur.kind == T_LBRACK) {
            advance(p);
            if (p->cur.kind != T_ICON) {
                c0_error_at(p->cur.line, "expected an array size constant");
            } else {
                arraylen = p->cur.ival;
                advance(p);
            }
            expect(p, T_RBRACK, "']'");
            is_array = 1;
            if (arraylen <= 0) {
                c0_error_at(line, "array dimension must be positive");
                arraylen = 1;
            }
            /* A second '[' M ']' - a 2-D array "int m[N][M];"
             * (05_arrptr/02_array2d.c): N*M ints in row-major order,
             * one contiguous block, so its frame slot is simply
             * N*M*MCC_SZINT bytes - confirmed against 02_array2d.
             * 1.golden's "int m[3][4];" ANAME offset -28 (24 bytes
             * below MCC_STAUTO) and 10_integ/05_matmul.1.golden's
             * three "int a[2][2]"-style locals at -12/-20/-28. The
             * inner dimension M is kept (SymEntry.dim2) as the row
             * size emit_subscript() scales the outer index by. Only a
             * plain 'int' element is accepted (emit_subscript_2d()
             * and mutos_c1's 2-D address arithmetic are 'int'-only),
             * and a third dimension is rejected rather than guessed:
             * its wire shape follows from the same v7/cc derivation
             * (see emit_subscript()'s comment), but mutos_c1's
             * combined address arithmetic for it (v7/cc/c12.c's
             * distrib() iterating over three scaled terms) is
             * confirmed by no golden. */
            if (p->cur.kind == T_LBRACK) {
                advance(p);
                if (p->cur.kind != T_ICON) {
                    c0_error_at(p->cur.line, "expected an array size constant");
                } else {
                    dim2 = p->cur.ival;
                    advance(p);
                }
                expect(p, T_RBRACK, "']'");
                if (dim2 <= 0) {
                    c0_error_at(line, "array dimension must be positive");
                    dim2 = 1;
                }
                if (decltype != TY_INT)
                    c0_error_at(line, "a 2-D array of anything but 'int' is "
                                       "not yet supported - see "
                                       "src/mutos_cc/README.md");
                if (p->cur.kind == T_LBRACK) {
                    c0_error_at(line, "arrays of three or more dimensions "
                                       "are not yet supported - see "
                                       "src/mutos_cc/README.md");
                    while (p->cur.kind == T_LBRACK) {
                        while (p->cur.kind != T_RBRACK &&
                               p->cur.kind != T_SEMI && p->cur.kind != T_EOF)
                            advance(p);
                        if (p->cur.kind == T_RBRACK)
                            advance(p);
                    }
                }
            }
        }

        /* The frame slot: rlength() of the whole object - one element
         * (a 'char' still takes a whole word, a 'long' two, any pointer
         * one), or N (x M) elements of the element's own size. */
        if (decltype == TY_STRUCT && (!bsdef || !bsdef->complete))
            c0_error_at(line, "'%s' has an incomplete struct type", name);
        if (ty_is_float(basetype) && ptr_degree > 1)
            c0_error_at(line, "a pointer to a pointer to 'float'/'double' is "
                               "not yet supported - see src/mutos_cc/README.md");
        if (basetype == TY_UNSIGN && (ptr_degree > 0 || is_array))
            c0_error_at(line, "a pointer to 'unsigned' or an array of "
                               "'unsigned' is not yet supported - see "
                               "src/mutos_cc/README.md");
        int size = rlength((is_array ? arraylen * (dim2 ? dim2 : 1) : 1) *
                           (long)type_size(decltype, bsdef, 0));

        /* 'register' is only attempted for a plain 'int' scalar - see
         * this function's own comment above; a pointer, an array, or a
         * 'char'/'long' takes the ordinary AUTO path (goodreg() would
         * accept a pointer, but no golden exercises "register int *p;",
         * so it is not guessed at). */
        int regnum = (is_register && !is_array && decltype == TY_INT)
                   ? try_claim_register(p) : -1;

        if (regnum >= 0) {
            SymEntry *sym = symtab_declare_reg(&p->syms, name, decltype, regnum);
            if (!sym) {
                c0_error_at(line, "'%s' redeclared", name);
            } else {
                /* Confirmed against 06_regclass.1.golden: the new
                 * regvar value is announced via SETREG immediately
                 * before THIS variable's own RNAME (not batched at
                 * the end of all declarations) - see cfunc()'s own
                 * comment for the matching end-of-function restore. */
                outcode(t1, "BN", OP_SETREG, regnum);
                outcode(t1, "BSN", OP_RNAME, sym->name, regnum);
            }
        } else {
            SymEntry *sym = symtab_declare_auto(&p->syms, name, decltype, size);
            if (!sym) {
                c0_error_at(line, "'%s' redeclared", name);
            } else {
                sym->is_ptr = (ty_is_ptr(decltype) && !is_array);
                sym->is_array = is_array;
                sym->dim2 = (int)dim2;
                if ((decltype & TY_TYPE_MASK) == TY_STRUCT)
                    sym->sdef = bsdef;
                outcode(t1, "BSN", OP_ANAME, sym->name, sym->offset);
            }
        }

        if (p->cur.kind == T_COMMA) {
            advance(p);
            continue;
        }
        break;
    }
    expect(p, T_SEMI, "';'");
}

/*
 * static-decl := 'static' ('int'|'char'|'long') IDENT (',' IDENT)* ';'
 *
 * A local STATIC variable - unlike an AUTO local, it is NOT part of
 * the stack frame at all: it lives in a dedicated BSS block (one per
 * declared name), tagged with its own fresh intermediate-code label,
 * and keeps its value across calls. Matches v7/cc/c03.c's declist()
 * STATIC case ("dsym->hoffset = isn; outcode(\"BBNBN\", BSS, LABEL,
 * isn++, SSPACE, rlength(dsym)); outcode(\"B\", PROG);" then prste()'s
 * own "outcode(\"BSN\", SNAME, name, hoffset)"), confirmed
 * byte-for-byte against 04_funcs/05_staticvar.1.golden's "static int
 * n;" (BSS, LABEL(4), SSPACE(2), PROG, SNAME("_n", 4)) - `symtab_
 * declare_static()`'s `offset` is this same label number, not a
 * stack offset (see its own comment in c0_sym.h); every later NAME
 * reference to `n` reuses it unchanged (SC_STATIC, offset=4),
 * confirmed via "n = n + 1;"'s two NAME(n) nodes. Only a plain-IDENT
 * declarator is supported (no '*'/'[' forms) - not exercised by any
 * golden, matching 'char'/'long' locals' own scope restriction in
 * parse_decl() above.
 */
static void parse_static_decl(Parser *p, FILE *t1)
{
    advance(p); /* consume 'static' */

    int symtype, slotsize;
    if (p->cur.kind == T_KW_INT) {
        symtype = TY_INT;
        slotsize = MCC_SZINT;
    } else if (p->cur.kind == T_KW_CHAR) {
        symtype = TY_CHAR;
        slotsize = MCC_SZINT; /* slot size, not value size - see
                                * parse_decl()'s own comment above */
    } else if (p->cur.kind == T_KW_LONG) {
        symtype = TY_LONG;
        slotsize = MCC_SZLONG;
    } else if (p->cur.kind == T_KW_DOUBLE || p->cur.kind == T_KW_FLOAT) {
        /* "static double ld;" - fltprobe/p3_global.1.golden: BSS,
         * LABEL(4), SSPACE(8), PROG, SNAME("_ld", 4). */
        symtype = (p->cur.kind == T_KW_DOUBLE) ? TY_DOUBLE : TY_FLOAT;
        slotsize = (symtype == TY_DOUBLE) ? 8 : 4;
    } else {
        c0_error_at(p->cur.line,
            "only 'int'/'char'/'long'/'float'/'double' static declarations "
            "are supported so far - see src/mutos_cc/README.md");
        while (p->cur.kind != T_SEMI && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
        return;
    }
    advance(p); /* consume 'int'/'char'/'long'/'float'/'double' */

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

        int label = p->isn++;
        outcode(t1, "BBNBN", OP_BSS, OP_LABEL, label, OP_SSPACE, slotsize);
        outcode(t1, "B", OP_PROG);

        SymEntry *sym = symtab_declare_static(&p->syms, name, symtype, label);
        if (!sym) {
            c0_error_at(line, "'%s' redeclared", name);
        } else {
            outcode(t1, "BSN", OP_SNAME, sym->name, sym->offset);
        }

        if (p->cur.kind == T_COMMA) {
            advance(p);
            continue;
        }
        break;
    }
    expect(p, T_SEMI, "';'");
}



/*
 * doret() - matches v7/cc/c04.c's doret() shape: a bare "return;"
 * just branches to the epilogue; "return <expr>;" additionally emits
 * the expression (constant-folded where possible, a real NAME/
 * operator tree otherwise - see the ExprVal comment above) wrapped
 * in RFORCE (convert to the function's own declared return type -
 * p->cur_ret_type, set once per cfunc() - previously always
 * hardcoded TY_INT before 02_long/03_retval.c's 'long'-returning
 * function) and EXPR (statement wrapper carrying the source line),
 * exactly matching the confirmed golden byte sequence.
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
    /* v7/cc/c04.c's doret() returns through an assignment to the
     * function's own type, so an int function returning a char
     * converts it - 10_integ/04_strrev.1.golden's "return buf[0];" ->
     * ... STAR(1) ITOC(0) RFORCE(0). (A char- or long-returning
     * function's conversions are not confirmed; mutos_c1 refuses a
     * char RFORCE either way.) */
    int rtype = p->cur_ret_type;
    if (ty_is_float(rtype)) {
        /* A function returning a double (see "Floating point"): doret()'s
         * assignment to the function's type converts an int value
         * (ITOF(DOUBLE)); a floating one is not converted, and RFORCE
         * takes its own type - FLOAT for a float variable. */
        if (!v.is_const && ty_is_float(v.type))
            rtype = v.type;
        else if (!v.is_const && v.type == TY_CHAR)
            c0_error_at(stmt_line, "returning a 'char' from a function "
                                   "returning a 'double' is not yet supported "
                                   "- see src/mutos_cc/README.md");
        else
            emit_to_float(t1, rtype, v, stmt_line);
    } else if (ty_is_float(v.type) && !v.is_const) {
        /* doret()'s assignment to the function's type converts a
         * floating value: "return c;" in an int function is FTOI(INT)
         * RFORCE(INT), the stream 08_float/01_floatbas.1.golden has for
         * "return (int) c;" (see "Floating point"). */
        emit_from_float(t1, rtype, stmt_line);
    } else if (rtype == TY_INT) {
        (void)promote_char(t1, v);
        /* doret()'s RFORCE takes the converted value's own type, and an
         * unsigned value is not converted to int: 06_struct/
         * 07_bitfield.1.golden's "return f.ready + f.mode + f.count;" ->
         * ... PLUS(7) RFORCE(7). */
        if (v.type == TY_UNSIGN)
            rtype = TY_UNSIGN;
    }

    outcode(t1, "BN", OP_RFORCE, rtype);
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

    SymEntry *sym = lookup_var(p, name);
    advance(p); /* consume IDENT */

    /* "IDENT[expr] = ..." - an array/pointer subscript used as the
     * assignment target ("a[i] = i * i;" - 05_arrptr/01_arrbasic.c).
     * Consumes the full "[expr]" now (before the assignment
     * operator), emitting the same AMPER/ITOP/PLUS/STAR chain
     * emit_subscript() emits for the rvalue case - confirmed
     * byte-for-byte identical shape either way. `subtype` (>= 0) then
     * replaces `sym`'s own type as this statement's ASSIGN type
     * argument, and signals below that sym's plain NAME must NOT be
     * emitted again (the subscript already emitted the full lvalue
     * tree, ending in a STAR). */
    int subtype = -1;
    int is_field = 0;
    const StructDef *target_sdef = NULL;
    if (sym && starts_chain(p, sym)) {
        /* A struct member or struct array element as the target
         * ("p.x = 3;", "pp->y = ...", "pts[i].x = i;", "f.ready = 1;") -
         * written now, like a subscripted target: see "Member and
         * subscript chains". */
        ExprVal tv = parse_postfix_chain(p, t1, sym, &is_field);
        subtype = tv.type;
        target_sdef = tv.sdef;
    } else if (p->cur.kind == T_LBRACK) {
        if (!sym) {
            c0_error_at(line, "'%s' undeclared", name);
        } else if (!sym->is_array && !sym->is_ptr) {
            c0_error_at(line, "'[' applied to a non-array/non-pointer "
                               "variable is not supported");
            sym = NULL;
        }
        if (sym)
            subtype = emit_subscript(p, t1, sym);
        else {
            while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
                advance(p);
            if (p->cur.kind == T_SEMI)
                advance(p);
            return;
        }
    }

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

    if (subtype < 0 && sym && sym->type == TY_STRUCT && !sym->is_array) {
        target_sdef = sym->sdef;
        subtype = TY_STRUCT;
        emit_name(t1, sym, sym->type);
    } else if (subtype < 0) {
        if (!sym) {
            c0_error_at(line, "'%s' undeclared", name);
        } else {
            emit_name(t1, sym, sym->type);
        }
    }
    if (subtype == TY_STRUCT) {
        /* A whole struct: "p2 = p1;" - see parse_struct_assign(). */
        if (optag != OP_ASSIGN)
            c0_error_at(line, "a compound assignment to a struct");
        parse_struct_assign(p, t1, target_sdef, line);
        expect(p, T_SEMI, "';'");
        outcode(t1, "BN", OP_EXPR, line);
        return;
    }
    if (is_field && optag != OP_ASSIGN)
        c0_error_at(line, "a compound assignment to a bit-field is not yet "
                          "supported - see src/mutos_cc/README.md");
    /* else: the "IDENT[expr]" subscript above already emitted the
     * full lvalue tree - nothing more to emit here. */

    ExprVal rhs = parse_expr(p, t1);
    expect(p, T_SEMI, "';'");
    emit_materialize(t1, rhs);

    /* The operator's type argument is the LVALUE's type (TY_INT for
     * every case confirmed so far, but TY_PTR_INT for "p = a;" -
     * confirmed against 05_incdec.1.golden byte 242-243; the ten
     * compound-assignment operators reuse the same convention, though
     * only the TY_INT case is itself golden-confirmed for them - see
     * 06_compasgn.1.golden). For a subscripted target, it is instead
     * the subscript's own ELEMENT type (emit_subscript()'s return
     * value) - confirmed against 01_arrbasic.1.golden's "a[i] = i *
     * i;" (ASSIGN(TY_INT), matching the array's int element type,
     * not the pointer type the AMPER/PLUS/STAR chain computed the
     * address with) and 09_abiprobe/02_frame080.1.golden's "buf[0] =
     * 1;" (ASSIGN(TY_CHAR)). Falls back to TY_INT for the already-
     * reported undeclared-name case above. */
    int assign_type = (subtype >= 0) ? subtype : (sym ? sym->type : TY_INT);

    /* The right-hand side converted to the target's type - see
     * convert_assign() (an int -> long ITOL, "b = 23456;" in 02_long/
     * 01_addsub.1.golden; an int -> char ITOC(1), "buf[0] = 1;"; a char
     * -> int ITOC(0); ...). A compound assignment's conversions are
     * confirmed for none of these types; a char target of one is
     * refused, a char right-hand side widened like any other binary
     * operand (mutos_c1 then refuses it). */
    if (optag == OP_ASSIGN) {
        if (sym)
            convert_assign(t1, assign_type, rhs, line);
    } else if (ty_is_float(assign_type) &&
               (optag == OP_ASTIMES || optag == OP_ASDIV ||
                optag == OP_ASPLUS || optag == OP_ASMINUS) && subtype < 0) {
        /* "a *= b", "a /= b", "a += b", "a -= b" into a float/double
         * variable (see "Floating point"): v7's build() converts the
         * right-hand side to the target's own type, as for '=' (ITOF(FLOAT)
         * into a float), and types the operator DOUBLE - fltprobe/
         * p2_arith.1.golden's "d += 1" (CON, ITOF(DOUBLE), ASPLUS(DOUBLE)),
         * "a -= 0.5" into a float (the FCON as it is); a char widened first
         * (p16_open2.1.golden's "d += c": NAME c, ITOC(INT), ITOF(DOUBLE),
         * ASPLUS(DOUBLE)). */
        emit_to_float(t1, assign_type, rhs, line);
    } else if (!ty_is_float(assign_type) &&
               (optag == OP_ASTIMES || optag == OP_ASDIV ||
                optag == OP_ASPLUS || optag == OP_ASMINUS) &&
               subtype < 0 && (assign_type == TY_INT) &&
               ty_is_float(rhs.type) && !rhs.is_const) {
        /* "i *= e", "i /= e", "i += d", "i -= d", an int target: the
         * right-hand side converted to the target's type first, as for '='
         * (FTOI) - fltprobe/p2_arith.1.golden: NAME i, NAME e, FTOI(INT),
         * ASTIMES(INT); p8_misc.1.golden: NAME i, NAME d, FTOI(INT),
         * ASPLUS(INT) and ASMINUS(INT); p13_open.1.golden: NAME j, NAME e,
         * FTOI(INT), ASDIV(INT). The value is then i * (int)e, not (int)(i
         * * e): v7's own semantics (its build() converts an assignment
         * operator's right operand to the left's type). */
        outcode(t1, "BN", OP_FTOI, TY_INT);
    } else if (ty_is_float(assign_type) ||
               (ty_is_float(rhs.type) && !rhs.is_const)) {
        c0_error_at(line, "a compound assignment with a 'float'/'double' "
                          "operand is not yet supported (only '*=', '/=', "
                          "'+=' and '-=' into a 'float'/'double' or an int "
                          "variable) - see src/mutos_cc/README.md");
    } else if (assign_type == TY_CHAR) {
        c0_error_at(line, "a compound assignment to a 'char' is not yet "
                          "supported - see src/mutos_cc/README.md");
    } else if (rhs.type == TY_LONG && assign_type != TY_LONG) {
        /* v7 converts the right operand to the target's type first (as
         * for "i *= e") - an LTOI no golden shows here. */
        c0_error_at(line, "a compound assignment of a 'long' value to a "
                          "word is not yet supported - see "
                          "src/mutos_cc/README.md");
    } else {
        (void)promote_char(t1, rhs);
    }

    outcode(t1, "BN", optag, assign_wire_type(assign_type));
    outcode(t1, "BN", OP_EXPR, line);
}

/*
 * star-assign-stmt := '*'+ ('++'|'--')? IDENT ('++'|'--')? '=' expr ';'
 *
 * Handles "*p++ = 1;" / "*++p = 2;" (05_arrptr/01_expr/05_incdec.c) -
 * an assignment through a dereferenced pointer, optionally combined
 * with a single prefix OR postfix '++'/'--' on the pointer itself
 * (not both - real C wouldn't parse "*++p++" as this shape either) -
 * and "**pp = 6;" (05_arrptr/06_ptrptr.c) - a CHAIN of two or more
 * leading '*'s on a plain pointer-to-pointer(-to-...) variable, with
 * no incdec support beyond a single '*' (not exercised by any
 * golden). Only a plain pointer variable is supported as the operand
 * so far (not a general pointer expression) - see
 * src/mutos_cc/README.md.
 *
 * The pointer sub-expression's tree (NAME, plus the INCBEF/INCAFT/
 * DECBEF/DECAFT scaling shape from emit_incdec() when an operator is
 * present - only ever paired with exactly one '*') is emitted exactly
 * like the expression-level postfix/prefix cases above, followed by
 * one STAR per leading '*', each one's own type computed via
 * ty_decref() from whatever the previous step left (TY_INT for a
 * single plain pointer - matches the original hardcoded value
 * byte-for-byte - or TY_PTR_INT then TY_INT for a pointer-to-pointer,
 * confirmed against 06_ptrptr.1.golden), then the usual rhs/ASSIGN/
 * EXPR shape, ASSIGN's own type argument being whatever the LAST
 * STAR left.
 */
static void parse_star_assign_stmt(Parser *p, FILE *t1)
{
    int line = p->cur.line;
    int nstars = 0;
    while (p->cur.kind == T_STAR) {
        nstars++;
        advance(p);
    }

    int optag = 0;
    if (nstars == 1 && (p->cur.kind == T_INCR || p->cur.kind == T_DECR)) {
        optag = (p->cur.kind == T_INCR) ? OP_INCBEF : OP_DECBEF;
        advance(p);
    }

    if (p->cur.kind != T_IDENT) {
        c0_error_at(p->cur.line,
            "'*<expr> = ...' is only supported for a plain pointer "
            "variable, optionally with a leading/trailing '++'/'--' "
            "on a single '*' - see src/mutos_cc/README.md");
        while (p->cur.kind != T_SEMI && p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
            advance(p);
        if (p->cur.kind == T_SEMI)
            advance(p);
        return;
    }

    SymEntry *sym = lookup_var(p, p->cur.ident);
    if (!sym) {
        c0_error_at(line, "'%s' undeclared", p->cur.ident);
    } else if (!sym->is_ptr) {
        c0_error_at(line, "'*' applied to a non-pointer variable is "
                           "not yet supported - see src/mutos_cc/README.md");
        sym = NULL; /* best-effort: skip codegen below like undeclared */
    }
    advance(p); /* consume IDENT */

    if (!optag && nstars == 1 &&
        (p->cur.kind == T_INCR || p->cur.kind == T_DECR)) {
        optag = (p->cur.kind == T_INCR) ? OP_INCAFT : OP_DECAFT;
        advance(p);
    }

    int curtype = TY_INT;
    if (sym) {
        emit_name(t1, sym, sym->type);
        curtype = sym->type;
        if (optag)
            emit_incdec(t1, optag, sym->type, sym->is_ptr);
        for (int i = 0; i < nstars; i++) {
            curtype = ty_decref(curtype);
            if (curtype == TY_LONG)
                refuse_long_access(p);
            outcode(t1, "BN", OP_STAR, curtype);
        }
    }

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
    if (sym)
        convert_assign(t1, curtype, rhs, line); /* e.g. "*a = *b;" on
                                                  * two char pointers:
                                                  * none (04_strrev) */

    outcode(t1, "BN", OP_ASSIGN, curtype);
    outcode(t1, "BN", OP_EXPR, line);
}

/*
 * call-stmt := IDENT '(' ... ')' ... ';'
 *
 * An expression statement that starts with a function call, its value
 * discarded - "strcpy(src, \"hello, mutos\");" (05_arrptr/
 * 07_strlibc.c). v7's statement() compiles ANY expression followed by
 * ';' this way (rcexpr(tree()) - the tree, then EXPR with the line);
 * the only other expression statements this grammar has are the
 * assignment forms above, which are separate productions because an
 * assignment is not part of parse_expr()'s precedence chain. The call
 * may be the left operand of a larger expression ("f(x) + 1;" parses,
 * pointless as it is), which parse_expr() handles as usual. Confirmed
 * against 07_strlibc.1.golden: NAME(_strcpy) <args> COMMA CALL, then
 * EXPR with the statement's own line - nothing else, no ASSIGN.
 */
static void parse_call_stmt(Parser *p, FILE *t1)
{
    int line = p->cur.line;
    ExprVal v = parse_expr(p, t1);
    expect(p, T_SEMI, "';'");
    emit_materialize(t1, v);
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
    (void)char_nonobj_refused(cond, p->cur.line, "a condition");
    expect(p, T_RPAREN, "')'");
    emit_materialize(t1, cond);
    int line = p->cur.line;

    /* v7/cc's "simpif" shortcut: an if-body that is exactly a bare
     * 'goto label;' / 'break;' / 'continue;' / 'return;' compiles to a
     * single direct CBRANCH(target, cond=1) - no extra label allocated
     * at all - confirmed against 07_goto.s.golden ('if (i>=10) goto
     * done;'), 05_breakcont.s.golden ('if (j==3) break;' / 'if (i
     * ==j) continue;') and 10_integ/04_strrev ('if (lo >= hi)
     * return;'). Only recognized when nothing but the bare
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
    if (p->cur.kind == T_KW_RETURN && peek2_kind(p) == T_SEMI) {
        /* A bare 'return;' is the same shortcut, branching straight to
         * the function's return label (v7/cc/c02.c: "case RETURN: if
         * (nextchar()==';') { o2 = retlab; goto simpif; }") - confirmed
         * against 10_integ/04_strrev.1.golden's "if (lo >= hi) return;"
         * -> GREATEQ, CBRANCH(retlab, cond=1). */
        advance(p); /* 'return' */
        advance(p); /* ';' */
        outcode(t1, "BNNN", OP_CBRANCH, retlab, 1, line);
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
    (void)char_nonobj_refused(cond, p->cur.line, "a condition");
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
    (void)char_nonobj_refused(cond, p->cur.line, "a condition");
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

    /* Both labels are allocated before anything inside the parentheses
     * is parsed - v7/cc/c02.c statement()'s FOR case does "contlab =
     * isn++; brklab = isn++;" and only then calls forstmt(). That order
     * is invisible unless the init expression allocates a label of its
     * own - a string literal ("for (s = \"abc\"; ...)") - which then
     * gets the number after these two, as in v7. */
    int saved_brklab = p->brklab, saved_contlab = p->contlab;
    int test_lab = p->isn++;
    int brk_lab  = p->isn++;

    expect(p, T_LPAREN, "'('");

    if (p->cur.kind != T_SEMI) {
        int line = p->cur.line;
        ExprVal v = parse_comma_item(p, t1);
        emit_materialize(t1, v);
        outcode(t1, "BN", OP_EXPR, line);
    }
    expect(p, T_SEMI, "';'");

    p->contlab = test_lab;
    p->brklab  = brk_lab;

    label_op(t1, test_lab);

    if (p->cur.kind != T_SEMI) {
        ExprVal cond = parse_expr(p, t1);
        (void)char_nonobj_refused(cond, p->cur.line, "a condition");
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

    /* v7/cc/c02.c statement()'s SWITCH case allocates the break label
     * BEFORE parsing the controlling expression ("brklab = isn++; np =
     * pexpr();") and the dispatch label only afterwards, in pswitch() -
     * observable only when the expression holds a string literal. */
    int sw_brklab = p->isn++;

    expect(p, T_LPAREN, "'('");
    ExprVal cond = parse_expr(p, t1);
    (void)char_value_refused(cond, p->cur.line, "a 'switch' value");
    emit_materialize(t1, cond);
    int line = p->cur.line; /* p->cur is still ')' here - same
                              * last-consumed-token convention as
                              * 'while'/'do'/'for' above. */
    expect(p, T_RPAREN, "')'");
    outcode(t1, "BN", OP_RFORCE, TY_INT);
    outcode(t1, "BN", OP_EXPR, line);

    p->brklab = sw_brklab;
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
        parse_nested_block(p, t1, retlab);
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
    } else if (p->cur.kind == T_IDENT && peek2_kind(p) == T_LPAREN) {
        parse_call_stmt(p, t1);
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
     * K&R block structure - 'int'/'char'/'long' declarations (and
     * 'static int'/'char'/'long' - see parse_static_decl() above, and
     * 'register int'/'char'/'long' - see parse_decl()'s own comment)
     * are recognized as such right now, so the loop condition doubles
     * as "have we reached the first statement yet". A nested compound-
     * stmt (a loop/if body written as "{ ... }") re-enters here through
     * parse_nested_block(), which gives its declarations their own
     * scope; each one's ANAME is written right here, where the block's
     * declaration list is - after whatever code precedes the block
     * (07_scope/02_shadow.1.golden: the inner "x"'s ANAME follows the
     * outer "x = 1;" statement's EXPR). */
    while (at_type_spec(p) || p->cur.kind == T_KW_STATIC ||
           p->cur.kind == T_KW_REGISTER || p->cur.kind == T_KW_TYPEDEF) {
        if (p->cur.kind == T_KW_TYPEDEF) {
            c0_error_at(p->cur.line, "a typedef inside a function is not yet "
                                      "supported - see src/mutos_cc/README.md");
            while (p->cur.kind != T_SEMI && p->cur.kind != T_EOF)
                advance(p);
            if (p->cur.kind == T_SEMI)
                advance(p);
        } else if (p->cur.kind == T_KW_STATIC)
            parse_static_decl(p, t1);
        else
            parse_decl(p, t1);
    }

    while (p->cur.kind != T_RBRACE && p->cur.kind != T_EOF)
        parse_statement(p, t1, retlab);

    expect(p, T_RBRACE, "'}'");
}

/*
 * A compound statement nested inside a function body - v7/cc/c02.c's
 * statement() LBRACE case around blockhead()/blkend(): the block's own
 * declarations get a scope of their own (a name may shadow an outer
 * one and disappears again at the '}'), their AUTO slots continue below
 * the enclosing block's, and the allocation point goes back at the '}'
 * ("sauto = autolen; ... autolen = sauto;"), so a sibling block reuses
 * the same slots while SETSTK still covers the deepest one (maxauto).
 * Confirmed against 07_scope/02_shadow.1.golden: outer "x" at -6, the
 * inner block's "x" at -8 (ANAME "_x" -8, "x = 2;" -> NAME offset -8),
 * "return x;" after the block back at -6, SETSTK 8. A 'register' local
 * claimed inside the block gives its slot back at the '}' too, with the
 * SETREG v7 writes there ("if (sreg!=regvar) outcode("BN", SETREG,
 * sreg);") - the same restore cfunc() writes for the function body's
 * own block (04_funcs/06_regclass.1.golden); no golden has a register
 * local in a nested block. The function body itself is not parsed
 * through here: its declarations share the parameters' scope (v7
 * declares both at blklev 1), so a body local named like a parameter
 * stays a redeclaration error.
 *
 * Before this, a nested block's declarations went into the function's
 * one flat scope: a shadowing declaration was refused ("'x'
 * redeclared"), a new name stayed visible after the '}', and sibling
 * blocks each got fresh slots (a larger frame than the real compiler's).
 */
static void parse_nested_block(Parser *p, FILE *t1, int retlab)
{
    SymBlock saved;
    int sreg = p->regvar;
    symtab_block_enter(&p->syms, &saved);
    parse_compound_stmt(p, t1, retlab);
    if (p->regvar != sreg) {
        outcode(t1, "BN", OP_SETREG, sreg);
        p->regvar = sreg;
    }
    symtab_block_exit(&p->syms, &saved);
}

/* ------------------------------------------------------------------ */
/* Function / external definitions */

/*
 * param-decls := (('int'/'char'/'long') param-declarator (',' param-declarator)* ';')*
 * param-declarator := '*'* IDENT ('[' ICON? ']')? | '(' '*' IDENT ')' '(' ')'
 *
 * The K&R-style parameter TYPE declarations between a function's
 * '(' name-list ')' and its '{' - matches v7/cc/c03.c's own
 * declarator loop shape (reused from parse_decl() above), except
 * each name is bound to one of `param_names[]` (by K&R parameter-
 * LIST position, not by the order these decl statements happen to
 * declare them in - see c0_sym.h's paramlen comment) rather than to
 * a fresh AUTO local. A `param_names[]` entry with no matching decl
 * statement defaults to a plain 'int' (K&R's implicit-int parameter
 * rule) - not exercised by any golden yet (every confirmed 04_funcs
 * function declares every one of its parameters), but a direct,
 * low-risk consequence of the same rule already applied to a called-
 * but-undeclared function's own implicit return type (see
 * parse_call()). Offsets are assigned, and ANAME emitted, in
 * `param_names[]` order (bp+4, bp+6, ... - docs/MUTOS_C_ABI.md sect.
 * 1.3), confirmed byte-for-byte against every 04_funcs .1.golden's
 * ANAME sequence.
 */
static void parse_param_decls(Parser *p, FILE *t1,
                               char param_names[][LEX_IDENT_MAX], int nparams)
{
    if (nparams == 0)
        return;

    int ptype[MCC_MAXPARAMS];
    int pptr[MCC_MAXPARAMS];
    int pfunc[MCC_MAXPARAMS]; /* 1 iff declared "int (*name)()" - a
                                * function-pointer parameter, e.g.
                                * 07_funcptr.c's "int (*f)();" - see
                                * parse_decl()'s matching local-
                                * variable declarator for the wire
                                * shape this mirrors. */
    int pdeclared[MCC_MAXPARAMS];
    StructDef *psdef[MCC_MAXPARAMS];
    for (int i = 0; i < nparams; i++) {
        ptype[i] = TY_INT;
        pptr[i] = 0;
        pfunc[i] = 0;
        pdeclared[i] = 0;
        psdef[i] = NULL;
    }

    while (at_type_spec(p)) {
        /* A keyword type, or a struct/union/enum/typedef one - "struct
         * point *pp;" (06_struct/02_stptr.c), "struct rect *rp;"
         * (05_nestst.c). */
        int tline = p->cur.line;
        TypeSpec ts = parse_type_spec(p);
        int symtype = ts.type;
        if (symtype == TY_UNSIGN)
            c0_error_at(tline, "an 'unsigned' parameter is not yet supported - "
                               "see src/mutos_cc/README.md");
        /* A 'float' parameter is a double: the caller pushes every
         * floating argument as one (K&R), and v7/cc/c02.c's funchead()
         * retypes it ("if (cs->htype==FLOAT) cs->htype = DOUBLE") - 8
         * bytes of the frame, like ecvt.o's "double arg" at 4(bp) with
         * the next parameter at 12(bp) (see "Floating point"). */
        int float_param = ty_is_float(symtype);
        if (float_param)
            symtype = TY_DOUBLE;
        for (;;) {
            if (symtype == TY_INT && p->cur.kind == T_LPAREN) {
                advance(p); /* consume '(' */
                if (!expect(p, T_STAR, "'*'"))
                    break;
                if (p->cur.kind != T_IDENT) {
                    c0_error_at(p->cur.line,
                        "expected an identifier in a function-pointer "
                        "parameter declaration");
                    break;
                }
                int idx = -1;
                for (int i = 0; i < nparams; i++) {
                    if (strncmp(param_names[i], p->cur.ident, LEX_IDENT_MAX) == 0) {
                        idx = i;
                        break;
                    }
                }
                if (idx < 0)
                    c0_error_at(p->cur.line,
                        "'%s' is not one of this function's declared "
                        "parameters", p->cur.ident);
                advance(p); /* consume IDENT */
                if (!expect(p, T_RPAREN, "')'"))
                    break;
                if (!expect(p, T_LPAREN, "'('"))
                    break;
                if (!expect(p, T_RPAREN, "')'"))
                    break;
                if (idx >= 0) {
                    pfunc[idx] = 1;
                    pdeclared[idx] = 1;
                }
                if (p->cur.kind == T_COMMA) {
                    advance(p);
                    continue;
                }
                break;
            }

            /* Leading '*'s - any base type, any degree ("char *a;" -
             * 10_integ/04_strrev.c; "int *a;"). */
            int ptr_degree = 0;
            while (p->cur.kind == T_STAR) {
                ptr_degree++;
                advance(p);
            }
            int ptype_full = symtype;
            for (int k = 0; k < ptr_degree; k++)
                ptype_full = ty_ptr_of(ptype_full);
            if (float_param && ptr_degree > 0)
                c0_error_at(p->cur.line, "a pointer to 'float'/'double' is not "
                                          "yet supported - see "
                                          "src/mutos_cc/README.md");
            if (p->cur.kind != T_IDENT) {
                c0_error_at(p->cur.line,
                    "expected a parameter name in declaration");
                break;
            }
            int idx = -1;
            for (int i = 0; i < nparams; i++) {
                if (strncmp(param_names[i], p->cur.ident, LEX_IDENT_MAX) == 0) {
                    idx = i;
                    break;
                }
            }
            if (idx < 0) {
                c0_error_at(p->cur.line,
                    "'%s' is not one of this function's declared "
                    "parameters", p->cur.ident);
            } else {
                if (ptype_full == TY_STRUCT)
                    c0_error_at(p->cur.line, "a struct parameter passed by "
                                              "value is not yet supported - see "
                                              "src/mutos_cc/README.md");
                ptype[idx] = ptype_full;
                pptr[idx] = ty_is_ptr(ptype_full);
                pdeclared[idx] = 1;
                if ((ptype_full & TY_TYPE_MASK) == TY_STRUCT)
                    psdef[idx] = ts.sdef;
            }
            advance(p); /* consume IDENT */

            if (p->cur.kind == T_LBRACK) {
                /* "int a[];" - an array parameter, K&R's array-decays-
                 * to-pointer rule (05_arrptr/04_ptrarreq.c's
                 * "sumarr(a, n) int a[]; int n; { ... }") - a
                 * parameter declared this way is, for every purpose
                 * this grammar scope needs, indistinguishable from a
                 * plain "int *a" parameter (same type, same lack of
                 * an AMPER-decay step when subscripted or used in
                 * pointer arithmetic - see emit_subscript()'s own
                 * is_ptr branch). Any size between the brackets is
                 * accepted and ignored, matching real K&R semantics
                 * (a parameter's array size is not meaningful). The
                 * element type may itself be a pointer: "char
                 * *argv[];" is "char **argv" - type 41, confirmed by
                 * 09_abiprobe/01_argvmain.1.golden's NAME(argv). */
                advance(p); /* '[' */
                if (p->cur.kind == T_ICON)
                    advance(p);
                expect(p, T_RBRACK, "']'");
                if (float_param)
                    c0_error_at(p->cur.line, "an array of 'float'/'double' "
                                              "is not yet supported - see "
                                              "src/mutos_cc/README.md");
                if (idx >= 0) {
                    ptype[idx] = ty_ptr_of(ptype_full);
                    pptr[idx] = 1;
                }
            }

            if (p->cur.kind == T_COMMA) {
                advance(p);
                continue;
            }
            break;
        }
        expect(p, T_SEMI, "';'");
    }
    (void)pdeclared; /* recorded for a future "warn on undeclared
                       * parameter" diagnostic - not yet needed since
                       * every confirmed golden declares every
                       * parameter explicitly. */

    for (int i = 0; i < nparams; i++) {
        int decltype = pfunc[i] ? TY_PTR_FUNC_INT : ptype[i];
        int size = (pfunc[i] || pptr[i]) ? MCC_SZINT
                 : (ptype[i] == TY_LONG) ? MCC_SZLONG
                 : (ptype[i] == TY_DOUBLE) ? MCC_SZDOUB
                 : MCC_SZINT; /* slot size - a char parameter still
                                * occupies a full word, same as a
                                * char LOCAL (see parse_decl()'s own
                                * comment); not yet exercised by a
                                * golden for a parameter specifically. */
        SymEntry *sym = symtab_declare_param(&p->syms, param_names[i], decltype, size);
        if (!sym) {
            c0_error_at(p->cur.line, "'%s' redeclared", param_names[i]);
            continue;
        }
        sym->is_ptr = pptr[i];
        sym->sdef = psdef[i];
        outcode(t1, "BSN", OP_ANAME, sym->name, sym->offset);
    }
}

/*
 * cfunc() - matches v7/cc/c02.c's cfunc() shape, with the MUTOS-
 * specific deltas documented in mutos_cc.h applied (extra EVEN,
 * STAUTO=-4, initial regvar=4, RETRN's extra type argument).
 * `ret_type` is this function's own declared return type (TY_INT for
 * every plain "name(...) { ... }" definition - K&R's implicit-int
 * default - or whatever parse_extdef() parsed off an explicit
 * 'int'/'char'/'long' prefix, e.g. 02_long/03_retval.c's "long
 * addlong(...)").
 */
static void cfunc(Parser *p, const char *name, FILE *t1,
                   char param_names[][LEX_IDENT_MAX], int nparams,
                   int ret_type)
{
    int sloc = p->isn;
    p->isn += 2;

    outcode(t1, "BBBS", OP_PROG, OP_EVEN, OP_RLABEL, name);

    p->regvar = MCC_INIT_REGVAR; /* v7/cc's global `regvar` - how many
                                   * more 'register'-class local slots
                                   * remain claimable in this function;
                                   * only decremented by
                                   * try_claim_register(), called from
                                   * parse_decl() while parsing the
                                   * body's own local declarations
                                   * below - see 04_funcs/06_regclass.c. */
    p->cur_ret_type = ret_type;

    outcode(t1, "B", OP_SAVE);
    outcode(t1, "BN", OP_SETREG, p->regvar);

    /* Parameter ANAMEs are emitted here - between SETREG and the
     * BRANCH/LABEL pair that brackets the body - NOT after LABEL
     * like a body-local's own ANAME (see parse_decl()/
     * parse_compound_stmt()). This matches v7/cc/c02.c's cfunc()
     * shape structurally (funchead(), which emits each parameter's
     * ANAME, runs before branch(sloc)/label(sloc+1) there too),
     * confirmed byte-for-byte here against every 04_funcs .1.golden:
     * SAVE, SETREG, ANAME(s) (if any params), BRANCH, LABEL, then
     * the body (whose own locals' ANAMEs - if any - come AFTER this
     * LABEL, inside parse_compound_stmt() as always). */
    symtab_init(&p->syms);
    p->brklab = 0;
    p->contlab = 0;
    p->nlabels = 0;
    p->deflab = 0;
    p->in_switch = 0;
    p->ncases = 0;
    parse_param_decls(p, t1, param_names, nparams);

    branch_op(t1, sloc);
    label_op(t1, sloc + 1);

    int retlab = p->isn++;

    parse_compound_stmt(p, t1, retlab);

    /* Matches v7/cc/statement()'s own LBRACE-block-exit restore
     * ("if (sreg!=regvar) outcode(SETREG,sreg); regvar=sreg;") - the
     * function's own top-level compound statement is itself exactly
     * such a block. Only emitted if a 'register'-class local actually
     * changed p->regvar - confirmed against 04_funcs/06_regclass.
     * 1.golden's trailing "SETREG 4" right before this LABEL/RETRN
     * pair (regvar restored from 3 back to MCC_INIT_REGVAR); every
     * function with no register-class locals leaves p->regvar
     * unchanged, so no golden without one shows this. */
    if (p->regvar != MCC_INIT_REGVAR)
        outcode(t1, "BN", OP_SETREG, MCC_INIT_REGVAR);

    outcode(t1, "BNBN", OP_LABEL, retlab, OP_RETRN, ret_type);

    label_op(t1, sloc);
    outcode(t1, "BN", OP_SETSTK, -p->syms.maxauto);
    branch_op(t1, sloc + 1);

    symtab_clear(&p->syms);
}

/*
 * One file-scope variable declarator - the name already consumed, the
 * declaration's storage class (`sclass`: 0 for none, T_KW_STATIC or
 * T_KW_EXTERN) and type known. v7/cc/c02.c's extdef() for a non-
 * function declarator followed by ',' or ';':
 *
 *   no class ("int counter;")  DEFXTRN: CSPACE(name, size) - a common
 *                                block, ".comm _counter,2"
 *   'static'                    BSS, NLABEL(name), SSPACE(size) -
 *                                ".bss" / "_hidden:.blkb 2."
 *   'extern'                    nothing at all
 *
 * size = the object's length rounded up to a whole word ("(length(ds)+
 * ALIGN) & ~ALIGN" - rlength()). Confirmed byte-for-byte against
 * 07_scope/01_globstat.1.golden (CSPACE "_counter" 2, then BSS NLABEL
 * "_hidden" SSPACE 2, both ahead of bump()'s SYMDEF) and 03_externdef.
 * 1.golden ("extern int total;" writes nothing; the later "int total;"
 * writes CSPACE "_total" 2 between addto() and main()). One MUTOS delta
 * from v7: v7 writes SYMDEF("") in front of a static's BSS (outcode(
 * "BSBBSBN", SYMDEF, "", BSS, NLABEL, ...) - an empty 'S' is a lone NUL
 * byte on the wire), the MUTOS front end does not: 01_globstat's
 * SSPACE argument is followed directly by bump()'s SYMDEF, and nothing
 * sits between CSPACE's size and BSS (see mutos_cc.h's delta 5).
 *
 * Every file-scope name is declared hclass SC_EXTERN whatever its
 * storage class (v7's decl1(EXTERN, ...)) and referenced by its symbol
 * - see emit_name(). A second declaration of the same name is accepted
 * when its type and linkage agree ("extern int total;" ... "int
 * total;"), and writes what its own class calls for (CSPACE again for
 * a second tentative definition, as v7 does). Only a plain IDENT of
 * type int, char or long is supported here, plus one array form - a
 * "char name[]" initialized with a string literal (see
 * parse_global_chararray()). Any other pointer, array or initializer,
 * and a 'static' function: no golden shows their shapes (a local
 * 'static' has the same restriction - see parse_static_decl()).
 */
static void parse_global_chararray(Parser *p, FILE *t1, int sclass,
                                   const char *name, int line);

static void parse_global_fvar(Parser *p, FILE *t1, int sclass, int type,
                              int ptr_degree, StructDef *sdef,
                              const char *name, int line);

static void parse_global_var(Parser *p, FILE *t1, int sclass, int type,
                             int ptr_degree, StructDef *sdef,
                             const char *name, int line)
{
    if (ptr_degree == 0 && type == TY_CHAR && p->cur.kind == T_LBRACK &&
        peek2_kind(p) == T_RBRACK) {
        parse_global_chararray(p, t1, sclass, name, line);
        return;
    }
    if (ty_is_float(type) || type == TY_STRUCT) {
        parse_global_fvar(p, t1, sclass, type, ptr_degree, sdef, name, line);
        return;
    }
    if (ptr_degree > 0 || p->cur.kind == T_LBRACK || p->cur.kind == T_ASSIGN) {
        c0_error_at(line, "a file-scope pointer, array or initialized "
                          "variable ('%s') is not yet supported - only "
                          "plain 'int'/'char'/'long' variables and a "
                          "'char name[]' initialized with a string are - "
                          "see src/mutos_cc/README.md", name);
        while (p->cur.kind != T_SEMI && p->cur.kind != T_COMMA &&
               p->cur.kind != T_EOF)
            advance(p);
        return;
    }
    if (is_known_func(p, name)) {
        c0_error_at(line, "'%s' redeclared (already a function)", name);
        return;
    }

    int is_static = (sclass == T_KW_STATIC);
    SymEntry *sym = symtab_lookup(&p->globals, name);
    if (sym) {
        if (sym->type != type || sym->is_static != is_static) {
            /* A different type is an error; 'static' and a non-static
             * declaration of one name are either undefined ("static int
             * x;" then "int x;") or, for a later 'extern', legal C that
             * no golden shows - refused either way. */
            c0_error_at(line, "'%s' redeclared with a different type or "
                              "storage class - not supported", name);
            return;
        }
    } else {
        sym = symtab_declare_global(&p->globals, name, type, is_static);
    }

    int size = rlength(size_of_type(type));
    if (sclass == 0)
        outcode(t1, "BSN", OP_CSPACE, sym->name, size);
    else if (is_static)
        outcode(t1, "BBSBN", OP_BSS, OP_NLABEL, sym->name, OP_SSPACE, size);
    /* 'extern': a declaration only - no wire output. */
}

/*
 * A file-scope floating variable, array of them or pointer to them, or a
 * struct variable (fltprobe/p3_global.1.golden):
 *
 *   "double gd;" / "float gf;"      CSPACE("_gd", 8) / CSPACE("_gf", 4)
 *   "static double sd;"             BSS, NLABEL("_sd"), SSPACE(8)
 *   "extern double xd;"             nothing
 *   "double ga[3];"                 CSPACE("_ga", 24)
 *   "double *gp;"                   CSPACE("_gp", 2)
 *   "struct pt {...} gs;"           CSPACE("_gs", 12)
 *   "double gi = 2.5;"              SYMDEF("_gi"), DATA, NLABEL("_gi"),
 *                                   FCON(DOUBLE, "2.5"), INIT(DOUBLE),
 *                                   EXPR(line) - "float gfi = 1.5;" the
 *                                   same with INIT(FLOAT)
 *
 * - v7/cc/c02.c's extdef(): the tentative definitions as for an int, an
 * initialized one "setinit(ds); if (sclass==EXTERN) outcode(SYMDEF);
 * outcode(DATA, NLABEL)" and cinit()'s rcexpr(block(INIT, type, tree)) -
 * c04.c's rcexpr() writes the tree, INIT with the variable's type, and
 * EXPR. An initializer other than a floating literal (an int one, a
 * negated one, an expression), a pointer's or array's initializer, a
 * struct's, a pointer to a struct, and a floating array of pointers have
 * no golden and are refused. A struct at file scope is accepted whatever
 * its members; mutos_c1 generates only a floating member's access (an
 * int one it refuses, as it refuses an int file-scope array's element).
 */
static void parse_global_fvar(Parser *p, FILE *t1, int sclass, int type,
                              int ptr_degree, StructDef *sdef,
                              const char *name, int line)
{
    int is_static = (sclass == T_KW_STATIC);
    int nelem = 0;
    int refused = 0;
    if (type == TY_STRUCT && (ptr_degree > 0 || !sdef || !sdef->complete))
        refused = 1;
    if (ptr_degree > 1)
        refused = 1;
    if (p->cur.kind == T_LBRACK) {
        advance(p);
        if (p->cur.kind != T_ICON || p->cur.ival <= 0 || ptr_degree > 0 ||
            type == TY_STRUCT) {
            refused = 1;
        } else {
            nelem = (int)p->cur.ival;
            advance(p);
        }
        if (!refused)
            expect(p, T_RBRACK, "']'");
    }
    int init = 0;
    if (!refused && p->cur.kind == T_ASSIGN) {
        TokKind k2 = peek2_kind(p);
        if (!ty_is_float(type) || ptr_degree > 0 || nelem > 0 ||
            sclass == T_KW_EXTERN ||
            (k2 != T_FCON && k2 != T_ICON && k2 != T_MINUS))
            refused = 1;
        else
            init = 1;
    }
    int symtype = type;
    for (int k = 0; k < ptr_degree; k++)
        symtype = ty_ptr_of(symtype);
    SymEntry *sym = symtab_lookup(&p->globals, name);
    /* Declared before: accepted when type, size and linkage agree
     * ("extern double xd;" ... "double xd;" - p3_global's CSPACE("_xd", 8)
     * at the end), as for an int (parse_global_var()). */
    if (sym && (sym->type != symtype || sym->is_static != is_static ||
                sym->is_array != (nelem > 0) || sym->sdef != sdef || init))
        refused = 1;
    if (refused || is_known_func(p, name)) {
        c0_error_at(line, "this file-scope declaration of '%s' is not yet "
                          "supported (a floating variable, array or pointer "
                          "- with at most a floating literal as initializer "
                          "- or a struct variable, declared once) - see "
                          "src/mutos_cc/README.md", name);
        while (p->cur.kind != T_SEMI && p->cur.kind != T_COMMA &&
               p->cur.kind != T_EOF)
            advance(p);
        return;
    }
    if (!sym) {
        sym = symtab_declare_global(&p->globals, name, symtype, is_static);
        sym->sdef = sdef;
        sym->is_ptr = (ptr_degree > 0);
        sym->is_array = (nelem > 0);
    }
    int size = rlength((long)(nelem ? nelem : 1) * type_size(symtype, sdef, 0));
    if (init) {
        advance(p);                               /* '=' */
        if (sclass == 0)
            outcode(t1, "BS", OP_SYMDEF, sym->name);
        outcode(t1, "BBS", OP_DATA, OP_NLABEL, sym->name);
        /* The constant converted to the variable's type, as for '='
         * (v7's cinit(): build(ASSIGN), then INIT of its right operand):
         * a floating literal as it is, "-1.5" FCON then NEG(DOUBLE) (v7's
         * c0 folds a negated int constant, not a floating one), an int
         * constant CON then ITOF(type) - p9_init.1.golden. */
        int neg = 0;
        if (p->cur.kind == T_MINUS) {
            neg = 1;
            advance(p);
        }
        if (p->cur.kind == T_FCON) {
            (void)parse_primary(p, t1);            /* the FCON */
            if (neg)
                outcode(t1, "BN", OP_NEG, TY_DOUBLE);
        } else if (p->cur.kind == T_ICON && p->cur.ival <= 32767) {
            long v = neg ? -p->cur.ival : p->cur.ival;
            advance(p);
            emit_materialize(t1, ev_const(v));
            outcode(t1, "BN", OP_ITOF, type);
        } else {
            c0_error_at(line, "this initializer of '%s' is not yet supported "
                              "(only a floating literal or an int constant, "
                              "either negated) - see src/mutos_cc/README.md",
                        name);
            while (p->cur.kind != T_SEMI && p->cur.kind != T_COMMA &&
                   p->cur.kind != T_EOF)
                advance(p);
            return;
        }
        outcode(t1, "BN", OP_INIT, type);
        outcode(t1, "BN", OP_EXPR, line);
        return;
    }
    if (sclass == 0)
        outcode(t1, "BSN", OP_CSPACE, sym->name, size);
    else if (is_static)
        outcode(t1, "BBSBN", OP_BSS, OP_NLABEL, sym->name, OP_SSPACE, size);
    /* 'extern': a declaration only - no wire output. */
}

/*
 * "char name[] = \"...\";" at file scope (optionally 'static') - v7/cc/
 * c02.c's extdef() for a declarator followed by '=':
 *
 *     setinit(ds);
 *     if (sclass==EXTERN)
 *             outcode("BS", SYMDEF, ds->name);
 *     outcode("BBS", DATA, NLABEL, ds->name);
 *     if (cinit(ds, 1, sclass) & ALIGN)
 *             outcode("B", EVEN);
 *
 * and cinit()'s string case, putstr(0, flex ? 10000 : nel) - the
 * label-less putstr() form, into temp1 (see putstr()); cinit() returns
 * the array's size, which for a flexible "[]" array is the string's
 * length plus its NUL, so an odd size is followed by EVEN. Confirmed
 * byte-for-byte against 10_integ/01_wordcount.1.golden, which starts
 *
 *     SYMDEF "_text"  DATA  NLABEL "_text"
 *     BDATA (14 bytes) 0  BDATA (15 bytes) 0  BDATA (16 bytes) 0  EVEN
 *
 * for "char text[] = \"the quick brown fox\\njumps over the lazy
 * dog\\n\";" (45 bytes with the NUL) - its main() follows.
 *
 * The name is a file-scope char ARRAY from here on (SymEntry.is_array,
 * type TY_CHAR, SC_EXTERN): an element is NAME(SC_EXTERN, TY_CHAR,
 * "_text") AMPER(9) <index> CON(1) ITOP(9) PLUS(9) STAR(1) - the same
 * emit_subscript() shape a local char array has, which is what the
 * golden shows. 'static' writes no SYMDEF (v7's "if (sclass==EXTERN)";
 * unlike the BSS case - mutos_cc.h delta 5 - there is no SYMDEF("") to
 * drop here), which no golden shows but v7 settles. Refused: a sized
 * array ("char s[8] = ...", v7 pads with SSPACE), a brace list, an
 * array without an initializer, 'extern' with an initializer - none has
 * a golden - and a name declared before in any form.
 */
static void parse_global_chararray(Parser *p, FILE *t1, int sclass,
                                   const char *name, int line)
{
    advance(p); /* '[' */
    advance(p); /* ']' */
    if (p->cur.kind != T_ASSIGN || peek2_kind(p) != T_STRING ||
        sclass == T_KW_EXTERN) {
        c0_error_at(line, "a file-scope 'char %s[]' is only supported with a "
                          "string-literal initializer and no 'extern' so far "
                          "- see src/mutos_cc/README.md", name);
        while (p->cur.kind != T_SEMI && p->cur.kind != T_COMMA &&
               p->cur.kind != T_EOF)
            advance(p);
        return;
    }
    advance(p); /* '=' */
    if (is_known_func(p, name) || symtab_lookup(&p->globals, name)) {
        c0_error_at(line, "'%s' redeclared - a second declaration of an "
                          "initialized file-scope array is not supported",
                    name);
        advance(p); /* the string */
        return;
    }
    int is_static = (sclass == T_KW_STATIC);
    SymEntry *sym = symtab_declare_global(&p->globals, name, TY_CHAR, is_static);
    if (!sym) {
        advance(p);
        return;
    }
    sym->is_array = 1;
    if (!is_static)
        outcode(t1, "BS", OP_SYMDEF, sym->name);
    outcode(t1, "BBS", OP_DATA, OP_NLABEL, sym->name);
    /* v7's lexer numbers every string token as it reads it ("cval =
     * isn++" in c00.c's symbol()), an initializer's too, although the
     * label-less putstr() never writes that number: main()'s labels in
     * 01_wordcount.1.golden start at 2 (BRANCH 2, LABEL 3), one later
     * than a file without the initializer (04_strrev's first function:
     * BRANCH 1, LABEL 2). */
    p->isn++;
    size_t n = putstr(t1, 0, p->cur.sval, p->cur.slen);
    advance(p); /* the string */
    if (n & 1)
        outcode(t1, "B", OP_EVEN);
}

/*
 * external definition:
 *   sclass? type? declarator (',' declarator)* ';'
 *   sclass? type? IDENT '(' (IDENT (',' IDENT)*)? ')' param-decls
 *                                                    compound-stmt
 *   sclass     := 'static' | 'extern'
 *   type       := 'int' | 'char' | 'long'
 *   declarator := '*'* IDENT ( '(' ')' )?
 *
 * A declarator with an empty '()' is a K&R forward-declaration
 * PROTOTYPE (04_mutrec.c's "int iseven();", needed before "isodd"
 * calls "iseven" so real K&R source has SOME declaration preceding the
 * use) - parsed and entirely discarded (register_func() only),
 * confirmed against 04_mutrec.1.golden, which contains no additional
 * wire output at all for this line: an int-returning callee's NAME is
 * the same NAME(SC_EXTERN, TY_FUNC_INT, name) leaf whether or not a
 * prototype preceded it (see parse_call()). What a prototype DOES
 * change is a call's types when the function returns something else -
 * "long addlong();"/"char *strcpy();" (02_long/03_retval.c, 05_arrptr/
 * 07_strlibc.c). A prototype may declare a pointer result ('*'s before
 * the name) and may be one of a comma-separated list ("int strlen(),
 * strcmp();"); only an EMPTY parameter list is supported - not
 * exercised by any golden otherwise. A declarator WITHOUT '(' is a
 * file-scope variable - see parse_global_var() (07_scope).
 *
 * The second form is an actual function DEFINITION (a body follows) -
 * an implicit-int K&R definition ("name(params) paramdecls { ... }",
 * this grammar's original shape) or one with an explicit return-type
 * prefix ("long addlong(a, b) long a, b; { ... }" - 02_long/03_retval.
 * c). Both forms start identically (an optional class/type, IDENT,
 * '(', an optional K&R-style bare-identifier parameter-NAME list,
 * ')'), and only once that's all been consumed does the next token
 * decide - ';' or ',' (only after an explicit class or type and an
 * empty parameter list - v7's getkeywords() "isadecl") means a
 * prototype, anything else (the param-type declarations and/or the
 * body's own '{') means a definition, so cfunc() is entered having
 * already fully parsed the K&R parameter-NAME list either way.
 *
 * 'extern' on a function changes nothing (v7 treats it exactly like no
 * class). 'static' on a function is refused: v7 writes SYMDEF("") for
 * it, and 01_globstat.1.golden shows the MUTOS front end drops that
 * for a static variable - what it writes for a static function no
 * golden shows.
 */
/* After a refused file-scope declaration (a struct, unsigned or floating
 * type - see parse_extdef()): skips it up to its ';' - or, for a
 * function definition (a ')' followed by neither ';' nor ','), past its
 * K&R parameter declarations and its whole body, so neither is misread
 * as external definitions. */
static void skip_refused_extdef(Parser *p)
{
    int is_def = 0;
    while (p->cur.kind != T_SEMI && p->cur.kind != T_LBRACE &&
           p->cur.kind != T_EOF) {
        int was_rparen = (p->cur.kind == T_RPAREN);
        advance(p);
        if (was_rparen && p->cur.kind != T_SEMI &&
            p->cur.kind != T_COMMA && p->cur.kind != T_LPAREN &&
            p->cur.kind != T_RPAREN)
            is_def = 1;
        if (is_def)
            while (p->cur.kind != T_LBRACE && p->cur.kind != T_EOF)
                advance(p);
    }
    if (p->cur.kind == T_SEMI) {
        advance(p);
        return;
    }
    if (p->cur.kind == T_LBRACE) {
        int depth = 0;
        while (p->cur.kind != T_EOF) {
            if (p->cur.kind == T_LBRACE)
                depth++;
            else if (p->cur.kind == T_RBRACE && --depth == 0) {
                advance(p);
                break;
            }
            advance(p);
        }
    }
}

static void parse_extdef(Parser *p, FILE *t1)
{
    if (p->cur.kind == T_KW_TYPEDEF) {
        parse_typedef(p);
        return;
    }
    int sclass = 0;
    if (p->cur.kind == T_KW_STATIC || p->cur.kind == T_KW_EXTERN) {
        sclass = p->cur.kind;
        advance(p); /* consume 'static'/'extern' */
    }
    int ret_type = TY_INT;
    int has_type = 0;
    StructDef *base_sdef = NULL;
    if (at_type_spec(p)) {
        /* A type keyword, or a struct/union/enum specifier (with or
         * without a body) or a typedef name - see parse_type_spec(). */
        TypeSpec ts = parse_type_spec(p);
        ret_type = ts.type;
        base_sdef = ts.sdef;
        has_type = 1;
        if (p->cur.kind == T_SEMI) {
            /* "struct point { int x; int y; };", "enum color { RED,
             * GREEN, BLUE };" - a type declaration only: v7 writes
             * nothing for one (06_struct's goldens start with main()'s
             * SYMDEF). */
            advance(p);
            return;
        }
        if ((ret_type == TY_STRUCT &&
             (p->cur.kind == T_STAR || peek2_kind(p) == T_LPAREN)) ||
            ret_type == TY_UNSIGN) {
            c0_error_at(p->cur.line, "a file-scope pointer to a struct/union "
                                      "or 'unsigned' variable, or a function "
                                      "returning a struct/union (or a pointer "
                                      "to one) or an unsigned value, is not "
                                      "yet supported - see "
                                      "src/mutos_cc/README.md");
            skip_refused_extdef(p);
            return;
        }
    }
    /* v7/cc/c03.c's getkeywords() "isadecl": a class or a type keyword
     * was seen, so what follows is a declaration list. */
    int is_decl = has_type || sclass != 0;
    int base_type = ret_type;

    for (int ndecl = 0; ; ndecl++) {
        /* Leading '*'s make the function return a pointer - "char
         * *strcpy();" (05_arrptr/07_strlibc.c, 10_integ/04_strrev.c) -
         * the same incref() chaining a local's declarator uses. */
        int ptr_degree = 0;
        while (is_decl && p->cur.kind == T_STAR) {
            ptr_degree++;
            advance(p);
        }
        ret_type = base_type;
        for (int k = 0; k < ptr_degree; k++)
            ret_type = ty_ptr_of(ret_type);

        if (p->cur.kind != T_IDENT) {
            if (is_decl)
                c0_error_at(p->cur.line,
                    "expected an identifier in top-level declaration");
            else
                c0_error_at(p->cur.line,
                    "external definition syntax (expected a function name - "
                    "mutos_c0's current grammar coverage only handles function "
                    "definitions, prototypes and plain file-scope variables - "
                    "see src/mutos_cc/README.md)");
            advance(p);
            return;
        }

        Token name_tok = p->cur;
        name_tok.sval = NULL; /* an IDENT never owns one; never alias it */
        advance(p); /* consume IDENT */

        /* Floating point at file scope (see "Floating point"): a
         * function returning a double - "double atof();", "double f(x)
         * ..." (libc.a's atof.o) - or a float (fltprobe/p5_call.1.golden:
         * RETRN(FLOAT), its calls CALL(FLOAT)); a floating variable or a
         * function returning a pointer to one has no evidence yet. */
        if (ty_is_float(base_type) && p->cur.kind == T_LPAREN &&
            ptr_degree > 0) {
            c0_error_at(name_tok.line, "a function returning a pointer to a "
                                        "'float'/'double' is not yet "
                                        "supported - see src/mutos_cc/README.md");
            skip_refused_extdef(p);
            return;
        }

        if (p->cur.kind != T_LPAREN) {
            if (!is_decl) {
                /* "x;" at file scope with no class or type: v7 takes it
                 * as an extern declaration and writes nothing - not
                 * exercised, and more likely a typo. */
                expect(p, T_LPAREN, "'('");
                return;
            }
            parse_global_var(p, t1, sclass, base_type, ptr_degree,
                             base_sdef, name_tok.ident, name_tok.line);
            if (p->cur.kind == T_COMMA) {
                advance(p);
                continue;
            }
            expect(p, T_SEMI, "';'");
            return;
        }
        advance(p); /* consume '(' */

        /*
         * K&R-style parameter-NAME list (bare identifiers only - types
         * follow separately, see parse_param_decls() above). An empty
         * '()' (no params) takes the pre-existing, unchanged path.
         */
        char param_names[MCC_MAXPARAMS][LEX_IDENT_MAX];
        int nparams = 0;
        if (p->cur.kind != T_RPAREN) {
            for (;;) {
                if (p->cur.kind != T_IDENT) {
                    c0_error_at(p->cur.line, "expected a parameter name");
                    break;
                }
                if (nparams >= MCC_MAXPARAMS) {
                    c0_error_at(p->cur.line,
                        "too many parameters (internal limit %d)", MCC_MAXPARAMS);
                    break;
                }
                snprintf(param_names[nparams], LEX_IDENT_MAX, "%s", p->cur.ident);
                nparams++;
                advance(p);
                if (p->cur.kind == T_COMMA) {
                    advance(p);
                    continue;
                }
                break;
            }
        }
        if (!expect(p, T_RPAREN, "')'"))
            return;

        if (sclass == T_KW_STATIC) {
            c0_error_at(name_tok.line, "a 'static' function ('%s') is not yet "
                                       "supported - see src/mutos_cc/README.md",
                        name_tok.ident);
        }
        if (symtab_lookup(&p->globals, name_tok.ident))
            c0_error_at(name_tok.line, "'%s' redeclared (already a file-scope "
                                       "variable)", name_tok.ident);

        if (is_decl && nparams == 0 &&
            (p->cur.kind == T_SEMI || p->cur.kind == T_COMMA)) {
            /* "TYPE name();" - a prototype-only declaration, no body,
             * possibly one of a comma-separated list ("int strlen(),
             * strcmp();" - 05_arrptr/07_strlibc.c) - see this
             * function's own comment above. No wire output at all:
             * 07_strlibc.1.golden starts straight with main()'s SYMDEF. */
            register_func(p, name_tok.ident, ret_type);
            if (p->cur.kind == T_COMMA) {
                advance(p);
                continue;
            }
            advance(p); /* consume ';' */
            return;
        }

        if (ndecl > 0) {
            /* "int f(), g() { ... }" - a definition after a prototype
             * in the same declarator list is not C. */
            c0_error_at(p->cur.line, "a function definition cannot follow "
                                      "other declarators in the same "
                                      "declaration");
        }
        outcode(t1, "BS", OP_SYMDEF, name_tok.ident);
        register_func(p, name_tok.ident, ret_type);
        cfunc(p, name_tok.ident, t1, param_names, nparams, ret_type);
        return;
    }
}

/* ------------------------------------------------------------------ */

int c0_compile(FILE *in, FILE *temp1, FILE *temp2)
{
    Parser p;
    memset(&p, 0, sizeof p); /* p.cur.sval must start NULL - advance() frees it */
    lex_init(&p.lx, in, c0_diag_filename);
    p.t2 = temp2;
    p.isn = 1;
    p.have_la = 0;
    symtab_init(&p.syms);
    symtab_init(&p.globals);
    p.nfuncnames = 0;
    advance(&p);

    while (p.cur.kind != T_EOF)
        parse_extdef(&p, temp1);

    outcode(temp1, "B", OP_EOFC);
    outcode(temp2, "B", OP_EOFC);

    symtab_clear(&p.globals);
    free_types(&p);
    free(p.cur.sval);
    if (p.have_la)
        free(p.la.sval);
    return c0_diag_nerrors != 0;
}
