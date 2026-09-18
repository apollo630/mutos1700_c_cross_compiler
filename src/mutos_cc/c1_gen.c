/*
 * c1_gen.c - mutos_c1 code generator.
 *
 * Current opcode coverage (matches mutos_c0's current grammar
 * coverage - see c0_parser.c and src/mutos_cc/README.md): SYMDEF,
 * PROG, EVEN, RLABEL, SAVE, SETREG, BRANCH, LABEL, ANAME, NAME, CON,
 * PLUS, MINUS, TIMES, DIVIDE, MOD, AND, OR, EXOR, COMPL, LSHIFT,
 * RSHIFT, LESS, LESSEQ, GREAT, GREATEQ, EQUAL, NEQUAL, LOGAND, LOGOR,
 * EXCLA, AMPER, ITOP, STAR, INCBEF, DECBEF, INCAFT, DECAFT, ASPLUS,
 * ASMINUS, ASTIMES, ASDIV, ASMOD, ASLSH, ASRSH, ASSAND, ASOR, ASXOR,
 * ASSIGN, RFORCE, EXPR, RETRN, SETSTK, EOFC.
 *
 * Unlike the constant-folding-only version of this file (which only
 * ever needed a stack of plain numbers), generating real code for
 * NAME/arithmetic operators requires tracking WHAT KIND of x86
 * operand each intermediate value is - an immediate constant, a
 * bp-relative memory location, or a value already sitting in a
 * specific register - since that determines which instruction shape
 * is legal/correct next (e.g. 8086's single-operand IMUL/IDIV
 * cannot take an immediate operand directly). The `Val`/value-stack
 * below is that tracking; every instruction shape it emits (which
 * register each operator uses, the "*N.(bp)"/"*N." operand syntax,
 * the "go through DI, then move to AX" RFORCE pattern) is
 * transcribed directly from a byte-level ("od -c") inspection of
 * tests/mutos_cc/01_expr/01_intarith.s.golden - see docs/DEVLOG.md's
 * Milestone 4 section for the full derivation. This is deliberately
 * NOT a general table-driven register allocator (v7/cc's own
 * table.s/c10.c machinery) - it covers exactly the operand shapes
 * 01_intarith's goldens confirm (a single level of NAME/CON
 * operands per operator), and reports "not yet supported" for
 * anything beyond that rather than guessing.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mutos_cc.h"
#include "c1_stream.h"
#include "c1_gen.h"

#define VALSTACK_MAX 64

/* One pointer-to-int degree - matches c0_parser.c's TY_PTR_INT
 * exactly (see mutos_cc.h's XTYPE comment); only pointer-to-int is
 * supported in this grammar scope. */
#define TY_PTR_INT (TY_INT | 010)

#define DEFERRED_MAX 4

typedef enum { VK_IMM, VK_MEM, VK_REG, VK_IND, VK_COND } ValKind;
/* VK_IND - an indirect "(reg)" memory operand, the result of
 * dereferencing a pointer (OP_STAR) - confirmed against
 * 05_incdec.s.golden's "mov\t(di),*1." (the STAR-dereferenced
 * assignment target). `reg` holds the register name, same as
 * VK_REG. */

/* A fully-resolved (never itself VK_COND) operand - used to hold the
 * two sides of a deferred comparison inside a VK_COND Val without
 * making the Val type self-referential. */
typedef struct {
    ValKind kind;   /* VK_IMM, VK_MEM or VK_REG only */
    long    imm;
    int     offset;
    const char *reg;
} SimpleVal;

typedef struct {
    ValKind kind;
    long    imm;    /* VK_IMM */
    int     offset;  /* VK_MEM: bp-relative offset */
    const char *reg;  /* VK_REG: a static string ("ax", "di", "dx") */
    /* VK_COND: a deferred, not-yet-materialized relational result -
     * "cl <true_op> cr" (true_op is one of OP_LESS/OP_LESSEQ/
     * OP_GREAT/OP_GREATEQ/OP_EQUAL/OP_NEQUAL). Deferring instead of
     * immediately emitting code is what lets OP_LOGAND/OP_LOGOR fuse
     * two comparisons into short-circuit "jumping code" without ever
     * materializing an intermediate 0/1 - see the OP_LESS.../
     * OP_LOGAND/OP_LOGOR/OP_EXCLA cases below and
     * docs/DEVLOG.md's Milestone 4 section for the full derivation
     * against 03_rellogic's goldens. */
    int       true_op;
    SimpleVal cl, cr;
} Val;

typedef struct {
    int  regvar;              /* consumed from SETREG but not yet
                                * acted on - no register-variable
                                * allocation is implemented yet. */
    Val  valstack[VALSTACK_MAX];
    int  valsp;
    int  next_lab;             /* c1's own internal label counter for
                                 * relational/logical codegen - distinct
                                 * from temp1's own label numbers (which
                                 * start at 1). Confirmed starting value
                                 * 10000 via 03_rellogic.s.golden (its
                                 * first internally-generated label is
                                 * "L10000"); the threshold headroom
                                 * below temp1's own label space is
                                 * otherwise unconfirmed/arbitrary. */
    char deferred[DEFERRED_MAX][72]; /* postfix ++/-- fixups (INCAFT/
                                 * DECAFT) queued at the operator's
                                 * own position, flushed at the next
                                 * OP_EXPR - see gen_incdec()'s and
                                 * OP_EXPR's comments; confirmed via
                                 * 05_incdec.s.golden's "inc *-6.(bp)"
                                 * appearing right after the enclosing
                                 * assignment's own "mov", not at
                                 * INCAFT's own position. */
    int  ndeferred;
} GenState;

static void gen_fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "mutos_c1: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(1);
}

static void push_val(GenState *g, Val v)
{
    if (g->valsp >= VALSTACK_MAX)
        gen_fatal("expression stack overflow (internal limit %d)", VALSTACK_MAX);
    g->valstack[g->valsp++] = v;
}

static Val pop_val(GenState *g)
{
    if (g->valsp <= 0)
        gen_fatal("expression stack underflow - malformed temp1 stream");
    return g->valstack[--g->valsp];
}

static Val val_imm(long v) { Val r = {0}; r.kind = VK_IMM; r.imm = v; return r; }
static Val val_mem(int off) { Val r = {0}; r.kind = VK_MEM; r.offset = off; return r; }
static Val val_reg(const char *reg) { Val r = {0}; r.kind = VK_REG; r.reg = reg; return r; }
static Val val_ind(const char *reg) { Val r = {0}; r.kind = VK_IND; r.reg = reg; return r; }

/* Demotes an already-resolved (non-VK_COND) Val down to a SimpleVal,
 * for storage inside a VK_COND's cl/cr fields. */
static SimpleVal simple_of(Val v)
{
    SimpleVal s;
    s.kind = v.kind;
    s.imm = v.imm;
    s.offset = v.offset;
    s.reg = v.reg;
    return s;
}

/* Promotes a SimpleVal back to a plain Val so it can be run through
 * the existing render_operand()/load_into_di() helpers unchanged. */
static Val val_from_simple(SimpleVal s)
{
    Val v = {0};
    v.kind = s.kind;
    v.imm = s.imm;
    v.offset = s.offset;
    v.reg = s.reg;
    return v;
}

/* Renders `v` as mutos_as-syntax operand text into `buf` (caller-
 * supplied, at least 32 bytes).
 *
 * Immediates use `*value.` (mutos_as's byte-sized-operand marker)
 * when the value fits in a signed byte (-128..127), or `#value.`
 * (word-sized marker) otherwise - confirmed against
 * 02_bitwise.s.golden's "mov *-8.(bp),*15." (15 fits a signed byte)
 * vs. "mov *-6.(bp),#240." (240 does not: sign-extending it from a
 * byte would corrupt it to -16). man/mutos_as.1's "Operand size
 * markers" section confirms this marker is honored literally for an
 * immediate regardless of the instruction - real disassembly (`objdump`
 * on `mutos_as`'s own output for both lines) shows both actually
 * assemble to the identical 5-byte "C7 /0 iw" MOV-word-immediate
 * encoding either way (8086's plain MOV has no byte-immediate form),
 * so the choice is a source-text convention the real compiler's code
 * generator applies uniformly, not something that changes the
 * generated machine code for MOV specifically - but matching it
 * exactly is still necessary for byte-for-byte `.s` parity.
 *
 * Memory-operand displacements always use `*` regardless of
 * magnitude - man/mutos_as.1 confirms the marker "has no effect" on
 * a displacement's encoding, and every confirmed golden (offsets
 * -6/-8/-10 so far) uses `*` uniformly. */
static void render_operand(char *buf, size_t n, Val v)
{
    switch (v.kind) {
    case VK_IMM:
        if (v.imm >= -128 && v.imm <= 127)
            snprintf(buf, n, "*%ld.", v.imm);
        else
            snprintf(buf, n, "#%ld.", v.imm);
        break;
    case VK_MEM: snprintf(buf, n, "*%d.(bp)", v.offset); break;
    case VK_REG: snprintf(buf, n, "%s", v.reg); break;
    case VK_IND: snprintf(buf, n, "(%s)", v.reg); break;
    case VK_COND: snprintf(buf, n, "<unmaterialized-cond>"); break;
    }
}

static void load_into_di(FILE *out, Val v); /* forward decl - defined
                                               * below, needed by
                                               * emit_cmp_and_branch()
                                               * above its own definition */

/* CMP's (and, confirmed later, SAL/SAR's) immediate operand omits
 * the trailing "." decimal-terminator that render_operand() uses
 * everywhere else - confirmed via 03_rellogic.s.golden's
 * "cmp\t*-6.(bp),*0" and 04_shift.s.golden's "sar\tdi,*1" (neither
 * has a period) vs. every "mov\t...,*N." elsewhere (with period).
 * Per man/mutos_as.1 the period is "a stylistic decimal terminator"
 * with zero effect on the assembled value, so this is a source-text
 * quirk shared by these non-MOV immediate-rendering paths
 * specifically - only rendering, not semantics. Confirmed at values
 * 0 (CMP) and 1 (SAL/SAR); the *N/#N byte-vs-word marker threshold
 * below is extrapolated from render_operand()'s (unconfirmed at
 * magnitudes outside these two files' own "*0"/"*1" cases). */
static void render_bare_imm(char *buf, size_t n, long v)
{
    if (v >= -128 && v <= 127)
        snprintf(buf, n, "*%ld", v);
    else
        snprintf(buf, n, "#%ld", v);
}

/* -------------------------------------------------------------- */
/* Relational/logical codegen - see the VK_COND field comment above
 * for the deferred-materialization design this implements. All of
 * it is reverse-engineered byte-for-byte against
 * tests/mutos_cc/01_expr/03_rellogic.s.golden. */

static const char *cond_true_mnem(int op)
{
    switch (op) {
    case OP_LESS:    return "blt";
    case OP_LESSEQ:  return "ble";
    case OP_GREAT:   return "bgt";
    case OP_GREATEQ: return "bge";
    case OP_EQUAL:   return "beq";
    case OP_NEQUAL:  return "bne";
    default:
        gen_fatal("internal: unknown relational op %d", op);
        return NULL;
    }
}

/* The branch-condition-code negation used to short-circuit the
 * non-final operand(s) of an OP_LOGAND chain (jump to the overall
 * false label when an early conjunct is false, i.e. when its
 * INVERSE condition holds) - confirmed via 03_rellogic's "a < b"
 * rendering as "bge" (LESS's inverse) when it is LOGAND's first
 * operand, vs. plain "blt" when it appears standalone. */
static int cond_invert(int op)
{
    switch (op) {
    case OP_LESS:    return OP_GREATEQ;
    case OP_GREATEQ: return OP_LESS;
    case OP_LESSEQ:  return OP_GREAT;
    case OP_GREAT:   return OP_LESSEQ;
    case OP_EQUAL:   return OP_NEQUAL;
    case OP_NEQUAL:  return OP_EQUAL;
    default:
        gen_fatal("internal: unknown relational op %d for inversion", op);
        return -1;
    }
}

/* Emits "cmp\t<cl>,<cr-or-di>\n" followed by a branch to L<target>
 * using `branch_op_code`'s mnemonic (the caller passes either
 * cond.true_op directly, for a "branch if true" site, or
 * cond_invert(cond.true_op), for a "branch if false" site - see
 * OP_LOGAND below). The right-hand side is loaded into DI first if
 * it is itself a memory operand (8086 CMP cannot take two memory
 * operands); an immediate right-hand side is rendered directly via
 * render_cmp_imm(), matching 03_rellogic's "cmp *-6.(bp),di" (memory
 * rhs) vs. "cmp *-6.(bp),*0" (immediate rhs) shapes exactly. */
static void emit_cmp_and_branch(FILE *out, Val cond, int branch_op_code, int target_lab)
{
    Val l = val_from_simple(cond.cl);
    Val r = val_from_simple(cond.cr);
    char lbuf[32], rbuf[32];
    render_operand(lbuf, sizeof lbuf, l);
    if (r.kind == VK_MEM) {
        load_into_di(out, r);
        snprintf(rbuf, sizeof rbuf, "di");
    } else if (r.kind == VK_IMM) {
        render_bare_imm(rbuf, sizeof rbuf, r.imm);
    } else {
        render_operand(rbuf, sizeof rbuf, r);
    }
    fprintf(out, "cmp\t%s,%s\n", lbuf, rbuf);
    fprintf(out, "%s\tL%d\n", cond_true_mnem(branch_op_code), target_lab);
}

/* Materializes a single deferred comparison into a real 0/1 value in
 * DI - the "standalone relational" pattern (e.g. plain "r = a < b;"):
 * branch-if-true to a fresh label, set DI=0 and jump past, or set
 * DI=1 at the true label, then fall through (label printed with no
 * trailing newline, matching OP_LABEL's style, so whatever the
 * caller emits next glues onto the same source line - confirmed
 * against "L10001:mov\t*-10.(bp),di" in the golden). */
static void materialize_cond(FILE *out, GenState *g, Val cond)
{
    int ltrue = g->next_lab++;
    int lend  = g->next_lab++;
    emit_cmp_and_branch(out, cond, cond.true_op, ltrue);
    fprintf(out, "mov\tdi,*0.\n");
    fprintf(out, "jmp\tL%d\n", lend);
    fprintf(out, "L%d:mov\tdi,*1.\n", ltrue);
    fprintf(out, "L%d:", lend);
}

/* No-op for anything already resolved; materializes a deferred
 * VK_COND into VK_REG("di"). Called wherever a Val is about to be
 * consumed as an ordinary value (ASSIGN's rhs, RFORCE's operand, and
 * defensively by every other binary/unary operator below, in case a
 * future grammar extension ever feeds a comparison's result into
 * arithmetic). */
static Val materialize(FILE *out, GenState *g, Val v)
{
    if (v.kind != VK_COND)
        return v;
    materialize_cond(out, g, v);
    return val_reg("di");
}

/* Wraps a non-VK_COND value as an implicit "!= 0" truth test -
 * OP_LOGAND/OP_LOGOR's fallback for an operand that is not itself a
 * relational comparison (not exercised by any current golden, since
 * both 03_rellogic's "&&"/"||" operands are always direct
 * comparisons, but the natural, zero-risk generalization of "any
 * nonzero value is true"). Never emits code - purely a description
 * of a comparison to be emitted later by whoever consumes it. */
static Val as_cond(Val v)
{
    if (v.kind == VK_COND)
        return v;
    Val c = {0};
    c.kind = VK_COND;
    c.true_op = OP_NEQUAL;
    c.cl = simple_of(v);
    c.cr = simple_of(val_imm(0));
    return c;
}

/* OP_LOGAND: fuses two (already as_cond()'d) comparisons into
 * classic short-circuit "jumping code" - confirmed against
 * 03_rellogic's "(a < b) && (b > 0)":
 *   cmp a,b; bge Lfalse     (INVERTED first condition -> false label)
 *   cmp b,0; bgt Ltrue      (direct second condition -> true label)
 *   Lfalse: di=0; jmp Lend
 *   Ltrue:  di=1
 *   Lend:
 * Label allocation order true,false,end matches the golden's
 * L10012(true)/L10013(false)/L10014(end) exactly (deduced from
 * which label each mnemonic branches to, not from textual order in
 * the .s output - the false label is referenced, hence printed,
 * before it is themselves allocated-lowest). */
static Val gen_logand(FILE *out, GenState *g, Val l, Val r)
{
    Val cl = as_cond(l);
    Val cr = as_cond(r);
    int ltrue  = g->next_lab++;
    int lfalse = g->next_lab++;
    int lend   = g->next_lab++;
    emit_cmp_and_branch(out, cl, cond_invert(cl.true_op), lfalse);
    emit_cmp_and_branch(out, cr, cr.true_op, ltrue);
    fprintf(out, "L%d:mov\tdi,*0.\n", lfalse);
    fprintf(out, "jmp\tL%d\n", lend);
    fprintf(out, "L%d:mov\tdi,*1.\n", ltrue);
    fprintf(out, "L%d:", lend);
    return val_reg("di");
}

/* OP_LOGOR: mirrors gen_logand() above but both conditions branch
 * directly (not inverted) to the SAME true label, and the false case
 * is a pure fallthrough (no separate false label needed, since
 * nothing needs to jump there) - confirmed against 03_rellogic's
 * "(a < 0) || (b > 0)":
 *   cmp a,0; blt Ltrue
 *   cmp b,0; bgt Ltrue
 *   di=0; jmp Lend
 *   Ltrue: di=1
 *   Lend:
 * matching the golden's L10015(true)/L10016(end) exactly. */
static Val gen_logor(FILE *out, GenState *g, Val l, Val r)
{
    Val cl = as_cond(l);
    Val cr = as_cond(r);
    int ltrue = g->next_lab++;
    int lend  = g->next_lab++;
    emit_cmp_and_branch(out, cl, cl.true_op, ltrue);
    emit_cmp_and_branch(out, cr, cr.true_op, ltrue);
    fprintf(out, "mov\tdi,*0.\n");
    fprintf(out, "jmp\tL%d\n", lend);
    fprintf(out, "L%d:mov\tdi,*1.\n", ltrue);
    fprintf(out, "L%d:", lend);
    return val_reg("di");
}

/* Emits "mov\tdi,<v>\n" to load `v` into DI - the confirmed generic
 * "working register" for PLUS/MINUS and for RFORCE's initial step -
 * except when `v` is already sitting in DI, in which case there is
 * nothing to do (not itself exercised by any current golden, but a
 * direct, low-risk consequence of the same confirmed pattern: never
 * emit a no-op "mov di,di"). */
static void load_into_di(FILE *out, Val v)
{
    if (v.kind == VK_REG && strcmp(v.reg, "di") == 0)
        return;
    char buf[32];
    render_operand(buf, sizeof buf, v);
    fprintf(out, "mov\tdi,%s\n", buf);
}

/* Emits "mov\tcx,<v>\n" to load `v` into CX - the confirmed working
 * register for a *variable* shift count specifically (never DI,
 * which holds the value being shifted - see OP_LSHIFT/OP_RSHIFT
 * below), skipping the no-op "mov cx,cx" case exactly like
 * load_into_di() above. Confirmed via 04_shift.s.golden's
 * "mov\tcx,*-8.(bp)" immediately before "sal\tdi,cl". */
static void load_into_cx(FILE *out, Val v)
{
    if (v.kind == VK_REG && strcmp(v.reg, "cx") == 0)
        return;
    char buf[32];
    render_operand(buf, sizeof buf, v);
    fprintf(out, "mov\tcx,%s\n", buf);
}

/* Queues `text` (a complete, newline-terminated instruction line) to
 * be emitted later by flush_deferred() - see DEFERRED_MAX's comment
 * on GenState. */
static void queue_deferred(GenState *g, const char *text)
{
    if (g->ndeferred >= DEFERRED_MAX)
        gen_fatal("too many deferred postfix ++/-- fixups in one "
                  "statement (internal limit %d)", DEFERRED_MAX);
    snprintf(g->deferred[g->ndeferred], sizeof g->deferred[0], "%s", text);
    g->ndeferred++;
}

static void flush_deferred(FILE *out, GenState *g)
{
    for (int i = 0; i < g->ndeferred; i++)
        fputs(g->deferred[i], out);
    g->ndeferred = 0;
}

/* Shared codegen for OP_INCBEF/OP_DECBEF/OP_INCAFT/OP_DECAFT -
 * confirmed byte-for-byte against 05_incdec.s.golden. `lv` must be
 * VK_MEM (a bp-relative lvalue - the only lvalue shape this grammar
 * scope's NAME ever produces); `amt` must be VK_IMM, already scaled
 * by OP_ITOP's own handling below for a pointer operand (so this
 * function itself never needs to know whether the lvalue is a
 * pointer). A unit amount (1) uses the dedicated inc/dec opcode
 * ("inc\t<lv>"); any other amount (a scaled pointer step) uses
 * add/sub with the immediate ("add\t<lv>,*2."). BEF commits
 * immediately, then loads the NEW value into DI; AFT loads the OLD
 * value into DI first, then defers the fixup instruction (see
 * queue_deferred() above) until the enclosing statement's OP_EXPR. */
static Val gen_incdec(FILE *out, GenState *g, int op, Val lv, Val amt)
{
    if (lv.kind != VK_MEM)
        gen_fatal("'++'/'--' on a non-memory lvalue is not yet supported");
    if (amt.kind != VK_IMM)
        gen_fatal("internal: '++'/'--' amount did not resolve to a "
                  "constant");

    int is_incr = (op == OP_INCBEF || op == OP_INCAFT);
    char lbuf[32];
    render_operand(lbuf, sizeof lbuf, lv);
    char instr[72];
    if (amt.imm == 1) {
        snprintf(instr, sizeof instr, "%s\t%s\n", is_incr ? "inc" : "dec", lbuf);
    } else {
        char abuf[32];
        render_operand(abuf, sizeof abuf, val_imm(amt.imm));
        snprintf(instr, sizeof instr, "%s\t%s,%s\n", is_incr ? "add" : "sub",
                 lbuf, abuf);
    }

    if (op == OP_INCBEF || op == OP_DECBEF) {
        fputs(instr, out);
        load_into_di(out, lv);
    } else {
        load_into_di(out, lv);
        queue_deferred(g, instr);
    }
    return val_reg("di");
}

int c1_generate(FILE *temp1, FILE *temp2, FILE *out)
{
    (void)temp2; /* string-literal (SNAME/temp2) support is not
                  * needed by mutos_c0's current grammar coverage -
                  * see src/mutos_cc/README.md. */

    GenState g = {0};
    g.next_lab = 10000; /* see GenState's next_lab field comment */

    for (;;) {
        int op = c1_read_op(temp1, "temp1");
        if (op == OP_EOFC)
            break;

        switch (op) {

        case OP_SYMDEF: {
            char *name = c1_read_sym(temp1, "temp1");
            fprintf(out, ".globl\t%s\n", name);
            free(name);
            break;
        }

        case OP_PROG:
            fprintf(out, ".text\n");
            break;

        case OP_EVEN:
            fprintf(out, ".even\n");
            break;

        case OP_RLABEL: {
            char *name = c1_read_sym(temp1, "temp1");
            fprintf(out, "%s:\n", name);
            free(name);
            break;
        }

        case OP_SAVE:
            /* Fixed, unconditional prologue - see docs/MUTOS_C_ABI.md
             * sect. 1.2: every compiled function saves bp/di/si
             * regardless of actual usage, so "|NREG 3" is a constant,
             * never derived from stream data. */
            fprintf(out, "push\tbp\nmov\tbp,sp\npush\tdi\npush\tsi\n"
                          "|NREG %d\n", MCC_NSAVEREG);
            break;

        case OP_SETREG:
            g.regvar = c1_read_num(temp1, "temp1");
            /* No visible text - register-variable allocation is not
             * implemented yet (no grammar coverage produces a
             * non-default value here yet). */
            break;

        case OP_BRANCH: {
            int lab = c1_read_num(temp1, "temp1");
            fprintf(out, "jmp\tL%d\n", lab);
            break;
        }

        case OP_LABEL: {
            int lab = c1_read_num(temp1, "temp1");
            /* Deliberately NO trailing newline - see file header
             * comment. */
            fprintf(out, "L%d:", lab);
            break;
        }

        case OP_ANAME: {
            char *name = c1_read_sym(temp1, "temp1");
            int offset = c1_read_num(temp1, "temp1");
            /* Confirmed exactly against 01_intarith.s.golden's
             * "| _a=-6." lines (name already includes the leading
             * '_' - see c1_stream.h's c1_read_sym()). */
            fprintf(out, "| %s=%d.\n", name, offset);
            free(name);
            break;
        }

        case OP_NAME: {
            int hclass = c1_read_num(temp1, "temp1");
            int type   = c1_read_num(temp1, "temp1");
            if (hclass != SC_AUTO)
                gen_fatal("NAME with storage class %d not yet supported "
                          "(only AUTO locals are covered so far)", hclass);
            if (type != TY_INT && type != TY_PTR_INT)
                gen_fatal("NAME of type %d not yet supported (only "
                          "TY_INT/TY_PTR_INT are covered so far)", type);
            int offset = c1_read_num(temp1, "temp1");
            push_val(&g, val_mem(offset));
            break;
        }

        case OP_CON: {
            int type = c1_read_num(temp1, "temp1");
            int value = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("CON of type %d not yet supported (only TY_INT "
                          "constants are covered so far)", type);
            push_val(&g, val_imm(value));
            break;
        }

        case OP_PLUS:
        case OP_MINUS: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported",
                          op == OP_PLUS ? "PLUS" : "MINUS", type);
            Val r = materialize(out, &g, pop_val(&g));
            Val l = materialize(out, &g, pop_val(&g));
            load_into_di(out, l);
            char rbuf[32];
            render_operand(rbuf, sizeof rbuf, r);
            fprintf(out, "%s\tdi,%s\n", op == OP_PLUS ? "add" : "sub", rbuf);
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_AND:
        case OP_OR:
        case OP_EXOR: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported",
                          op == OP_AND ? "AND" : op == OP_OR ? "OR" : "EXOR",
                          type);
            Val r = materialize(out, &g, pop_val(&g));
            Val l = materialize(out, &g, pop_val(&g));
            load_into_di(out, l);
            char rbuf[32];
            render_operand(rbuf, sizeof rbuf, r);
            const char *mnem = op == OP_AND ? "and" : op == OP_OR ? "or" : "xor";
            fprintf(out, "%s\tdi,%s\n", mnem, rbuf);
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_LSHIFT:
        case OP_RSHIFT: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported",
                          op == OP_LSHIFT ? "LSHIFT" : "RSHIFT", type);
            Val r = materialize(out, &g, pop_val(&g));
            Val l = materialize(out, &g, pop_val(&g));
            load_into_di(out, l);
            const char *mnem = op == OP_LSHIFT ? "sal" : "sar";
            if (r.kind == VK_IMM) {
                /* Constant shift count: plain 8086 has no
                 * shift-by-immediate-count opcode (that's an
                 * 80186-only extension - see docs/DEVLOG.md's CPU
                 * reference - and 04_shift.c's own header comment:
                 * this compiler targets plain 8086 only), so the
                 * real compiler repeats the single-bit-shift form
                 * (opcode D1 /4 or /7, count implicitly 1) N times -
                 * confirmed via 04_shift.s.golden's "r >> 2" emitting
                 * two consecutive "sar\tdi,*1" lines, and "a << 1"
                 * emitting exactly one "sal\tdi,*1". */
                if (r.imm < 0)
                    gen_fatal("negative shift count in constant "
                              "expression");
                char cbuf[32];
                render_bare_imm(cbuf, sizeof cbuf, 1);
                for (long i = 0; i < r.imm; i++)
                    fprintf(out, "%s\tdi,%s\n", mnem, cbuf);
            } else {
                /* Variable shift count: must be loaded into CL (the
                 * only register the 8086's "shift by CL" opcode
                 * shape accepts) - confirmed via 04_shift.s.golden's
                 * "mov\tcx,*-8.(bp)" immediately before
                 * "sal\tdi,cl". */
                load_into_cx(out, r);
                fprintf(out, "%s\tdi,cl\n", mnem);
            }
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_COMPL: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("COMPL of type %d not yet supported", type);
            Val v = materialize(out, &g, pop_val(&g));
            load_into_di(out, v);
            fprintf(out, "not\tdi\n");
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_AMPER: {
            /* Address-of - so far only reached via array-to-pointer
             * decay ("p = a;", c0_parser.c's parse_primary()): the
             * operand is always a plain bp-relative NAME, rendered as
             * a real "lea" - confirmed against 05_incdec.s.golden's
             * "lea\tdi,*-16.(bp)". */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_PTR_INT)
                gen_fatal("AMPER of type %d not yet supported (only "
                          "TY_PTR_INT is covered so far)", type);
            Val v = pop_val(&g);
            if (v.kind != VK_MEM)
                gen_fatal("'&' on a non-memory operand is not yet supported");
            char buf[32];
            render_operand(buf, sizeof buf, v);
            fprintf(out, "lea\tdi,%s\n", buf);
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_ITOP: {
            /* Scales a literal "1" (the syntactic '++'/'--' amount)
             * up to a real byte count for pointer arithmetic - both
             * operands are always compile-time constants in this
             * grammar scope (see emit_incdec()'s comment in
             * c0_parser.c), so c1 just folds CON(amount)*CON(size)
             * into a single immediate rather than emitting a runtime
             * multiply; confirmed against 05_incdec.s.golden's
             * "add\t*-18.(bp),*2." (never an "imul"). */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_PTR_INT)
                gen_fatal("ITOP of type %d not yet supported (only "
                          "TY_PTR_INT is covered so far)", type);
            Val size = pop_val(&g);
            Val amt  = pop_val(&g);
            if (size.kind != VK_IMM || amt.kind != VK_IMM)
                gen_fatal("ITOP of a non-constant operand is not yet "
                          "supported");
            push_val(&g, val_imm(amt.imm * size.imm));
            break;
        }

        case OP_STAR: {
            /* Pointer dereference - loads the pointer value into DI
             * (a no-op if it is already there, e.g. straight off a
             * preceding INCAFT/INCBEF - see load_into_di()) and
             * produces an indirect "(di)" operand. Only pointer-to-
             * int is supported, so the dereferenced type is always
             * TY_INT - confirmed against every STAR node in
             * 05_incdec.1.golden. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("STAR of type %d not yet supported (only "
                          "TY_INT is covered so far)", type);
            Val ptr = pop_val(&g);
            load_into_di(out, ptr);
            push_val(&g, val_ind("di"));
            break;
        }

        case OP_INCBEF:
        case OP_DECBEF:
        case OP_INCAFT:
        case OP_DECAFT: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT && type != TY_PTR_INT)
                gen_fatal("'++'/'--' of type %d not yet supported "
                          "(only TY_INT/TY_PTR_INT are covered so far)",
                          type);
            Val amt = pop_val(&g);
            Val lv  = pop_val(&g);
            push_val(&g, gen_incdec(out, &g, op, lv, amt));
            break;
        }

        case OP_LESS:
        case OP_LESSEQ:
        case OP_GREAT:
        case OP_GREATEQ:
        case OP_EQUAL:
        case OP_NEQUAL: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("relational/equality op of type %d not yet "
                          "supported", type);
            /* Deliberately does NOT emit anything here - see the
             * VK_COND field comment: the comparison is deferred until
             * whoever consumes it (materialize(), for a plain value
             * context, or gen_logand()/gen_logor(), for a fused
             * short-circuit context) decides how to compile it. Both
             * operands are run through materialize() first only to
             * cover the (unconfirmed by any golden) chained-relational
             * edge case "a < b < c", where a nested comparison could
             * otherwise flow in here as an operand. */
            Val r = materialize(out, &g, pop_val(&g));
            Val l = materialize(out, &g, pop_val(&g));
            Val c = {0};
            c.kind = VK_COND;
            c.true_op = op;
            c.cl = simple_of(l);
            c.cr = simple_of(r);
            push_val(&g, c);
            break;
        }

        case OP_LOGAND:
        case OP_LOGOR: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported",
                          op == OP_LOGAND ? "LOGAND" : "LOGOR", type);
            Val r = pop_val(&g);
            Val l = pop_val(&g);
            Val result = (op == OP_LOGAND) ? gen_logand(out, &g, l, r)
                                            : gen_logor(out, &g, l, r);
            push_val(&g, result);
            break;
        }

        case OP_EXCLA: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("EXCLA of type %d not yet supported", type);
            /* "!x" == the negation of x's truth test - reuses
             * as_cond() to get x's condition (synthesizing "x != 0"
             * if x isn't already one) and inverts its branch sense,
             * WITHOUT materializing - confirmed against 03_rellogic's
             * "r = !r;", which emits no code at all until the
             * following ASSIGN materializes the result as a single
             * "cmp *-10.(bp),*0 / beq ..." (EQUAL, i.e. NEQUAL
             * inverted) sequence, never a separate negation step. */
            Val v = pop_val(&g);
            Val c = as_cond(v);
            Val neg = {0};
            neg.kind = VK_COND;
            neg.true_op = cond_invert(c.true_op);
            neg.cl = c.cl;
            neg.cr = c.cr;
            push_val(&g, neg);
            break;
        }

        case OP_TIMES: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("TIMES of type %d not yet supported", type);
            Val r = materialize(out, &g, pop_val(&g));
            Val l = materialize(out, &g, pop_val(&g));
            if (r.kind == VK_IMM)
                gen_fatal("multiplying by an immediate is not yet "
                          "supported (8086 IMUL takes a reg/mem operand, "
                          "never an immediate directly - no golden "
                          "reference confirms the alternate sequence a "
                          "real compiler would need here)");
            char lbuf[32], rbuf[32];
            render_operand(lbuf, sizeof lbuf, l);
            render_operand(rbuf, sizeof rbuf, r);
            fprintf(out, "mov\tax,%s\nimul\t%s\n", lbuf, rbuf);
            push_val(&g, val_reg("ax"));
            break;
        }

        case OP_DIVIDE:
        case OP_MOD: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported",
                          op == OP_DIVIDE ? "DIVIDE" : "MOD", type);
            Val r = materialize(out, &g, pop_val(&g));
            Val l = materialize(out, &g, pop_val(&g));
            if (r.kind == VK_IMM)
                gen_fatal("dividing by an immediate is not yet supported "
                          "(8086 IDIV takes a reg/mem operand, never an "
                          "immediate directly - no golden reference "
                          "confirms the alternate sequence a real "
                          "compiler would need here)");
            char lbuf[32], rbuf[32];
            render_operand(lbuf, sizeof lbuf, l);
            render_operand(rbuf, sizeof rbuf, r);
            fprintf(out, "mov\tax,%s\ncwd\nidiv\t%s\n", lbuf, rbuf);
            /* Quotient in AX, remainder in DX - confirmed via
             * 01_intarith.s.golden's "c = a / b;" (takes ax) vs.
             * "c = a %% b;" (takes dx) immediately after the same
             * mov/cwd/idiv sequence. */
            push_val(&g, val_reg(op == OP_DIVIDE ? "ax" : "dx"));
            break;
        }

        case OP_ASPLUS:
        case OP_ASMINUS:
        case OP_ASSAND:
        case OP_ASOR:
        case OP_ASXOR: {
            /* += -= &= |= ^= with a constant right-hand side - all five
             * confirmed against 06_compasgn.s.golden to compile to a
             * single in-place "<mnem> <lvalue>,<imm>" instruction, never
             * routed through DI the way a non-compound binary operator
             * is (e.g. OP_PLUS above) - there is no separate ASSIGN
             * node following these in temp1, so the memory-operand
             * write has to be this node's own job. */
            int type = c1_read_num(temp1, "temp1");
            const char *name = op == OP_ASPLUS ? "ASPLUS" :
                                op == OP_ASMINUS ? "ASMINUS" :
                                op == OP_ASSAND ? "ASSAND" :
                                op == OP_ASOR ? "ASOR" : "ASXOR";
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported", name, type);
            Val rhs = materialize(out, &g, pop_val(&g));
            Val lhs = pop_val(&g);
            if (lhs.kind != VK_MEM)
                gen_fatal("compound assignment to a non-memory lvalue is "
                          "not yet supported");
            if (rhs.kind != VK_IMM)
                gen_fatal("compound assignment with a non-constant "
                          "right-hand side is not yet supported (no "
                          "golden reference confirms the register-operand "
                          "sequence a real compiler would need here)");
            const char *mnem = op == OP_ASPLUS ? "add" :
                                op == OP_ASMINUS ? "sub" :
                                op == OP_ASSAND ? "and" :
                                op == OP_ASOR ? "or" : "xor";
            char lbuf[32], rbuf[32];
            render_operand(lbuf, sizeof lbuf, lhs);
            render_operand(rbuf, sizeof rbuf, rhs);
            fprintf(out, "%s\t%s,%s\n", mnem, lbuf, rbuf);
            break;
        }

        case OP_ASLSH:
        case OP_ASRSH: {
            /* <<= >>= with a constant right-hand side - confirmed
             * against 06_compasgn.s.golden's "a <<= 1;"/"a >>= 1;",
             * both a single "sal"/"sar <lvalue>,*1" directly on the
             * memory operand (no DI). Only a count of exactly 1 is
             * golden-confirmed, but plain 8086 having no
             * shift-by-immediate-count opcode is a hardware fact (not
             * a codegen choice) already established by OP_LSHIFT/
             * OP_RSHIFT above and confirmed via 04_shift.s.golden's
             * multi-repetition case - so the same "repeat the
             * single-bit form N times" generalization is applied here
             * too, just against the memory operand directly instead of
             * DI. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported",
                          op == OP_ASLSH ? "ASLSH" : "ASRSH", type);
            Val rhs = materialize(out, &g, pop_val(&g));
            Val lhs = pop_val(&g);
            if (lhs.kind != VK_MEM)
                gen_fatal("compound assignment to a non-memory lvalue is "
                          "not yet supported");
            if (rhs.kind != VK_IMM)
                gen_fatal("compound shift-assignment with a non-constant "
                          "shift count is not yet supported (no golden "
                          "reference confirms the CX-loading sequence a "
                          "real compiler would need here)");
            if (rhs.imm < 0)
                gen_fatal("negative shift count in constant expression");
            char lbuf[32], cbuf[32];
            render_operand(lbuf, sizeof lbuf, lhs);
            render_bare_imm(cbuf, sizeof cbuf, 1);
            const char *mnem = op == OP_ASLSH ? "sal" : "sar";
            for (long i = 0; i < rhs.imm; i++)
                fprintf(out, "%s\t%s,%s\n", mnem, lbuf, cbuf);
            break;
        }

        case OP_ASTIMES: {
            /* *= with a constant right-hand side - confirmed against
             * 06_compasgn.s.golden's "a *= 2;", which compiles to a
             * single "sal <lvalue>,*1", NOT an imul: 8086 IMUL cannot
             * take an immediate operand directly (the same restriction
             * OP_TIMES above already enforces), so multiplying by a
             * power of two is strength-reduced to a shift instead -
             * confirmed real behavior, not a guess, since that's
             * exactly what the golden contains. Generalized to any
             * power-of-two >= 2 via the same N-times-repeat reasoning
             * as OP_ASLSH/OP_ASRSH above (only the single-bit case,
             * *2, is itself golden-confirmed). Any other constant (not
             * a power of two, including 0 and 1) falls back to the
             * same explicit "not yet supported" OP_TIMES already gives
             * for an immediate operand, rather than guessing the real
             * compiler's actual strength-reduction thresholds. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("ASTIMES of type %d not yet supported", type);
            Val rhs = materialize(out, &g, pop_val(&g));
            Val lhs = pop_val(&g);
            if (lhs.kind != VK_MEM)
                gen_fatal("compound assignment to a non-memory lvalue is "
                          "not yet supported");
            int shift = 0;
            if (rhs.kind == VK_IMM && rhs.imm >= 2) {
                long v = rhs.imm;
                while (v > 1 && (v & 1) == 0) { v >>= 1; shift++; }
                if (v != 1)
                    shift = 0; /* not a power of two */
            }
            if (shift == 0)
                gen_fatal("multiplying by an immediate is not yet "
                          "supported except for a power-of-two constant "
                          "(8086 IMUL takes a reg/mem operand, never an "
                          "immediate directly, and no golden reference "
                          "confirms the general strength-reduction "
                          "sequence a real compiler would need here)");
            char lbuf[32], cbuf[32];
            render_operand(lbuf, sizeof lbuf, lhs);
            render_bare_imm(cbuf, sizeof cbuf, 1);
            for (int i = 0; i < shift; i++)
                fprintf(out, "sal\t%s,%s\n", lbuf, cbuf);
            break;
        }

        case OP_ASDIV:
        case OP_ASMOD: {
            /* /= %= with a constant right-hand side - confirmed against
             * 06_compasgn.s.golden's "a /= 4;"/"a %= 3;": since 8086
             * IDIV cannot take an immediate operand either (the same
             * restriction OP_DIVIDE/OP_MOD above already enforce for
             * their own right operand), the immediate has to be loaded
             * into a register first - CX specifically, confirmed via
             * "mov\tcx,*4."/"mov\tcx,*3." immediately before "idiv\tcx"
             * (not reusing the shift operators' load_into_cx() helper,
             * since that skips loading when the value is already in
             * CX, which an immediate literal never is). Quotient (AX)
             * vs. remainder (DX) matches OP_DIVIDE/OP_MOD's own
             * confirmed convention exactly. Unlike every other compound-
             * assignment op above, this one needs an explicit store
             * back into the lvalue afterward - IDIV's result lands in
             * AX/DX, never directly in memory. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported",
                          op == OP_ASDIV ? "ASDIV" : "ASMOD", type);
            Val rhs = materialize(out, &g, pop_val(&g));
            Val lhs = pop_val(&g);
            if (lhs.kind != VK_MEM)
                gen_fatal("compound assignment to a non-memory lvalue is "
                          "not yet supported");
            if (rhs.kind != VK_IMM)
                gen_fatal("compound division/modulo-assignment with a "
                          "non-constant right-hand side is not yet "
                          "supported (no golden reference confirms the "
                          "register-operand sequence a real compiler "
                          "would need here)");
            char lbuf[32], rbuf[32];
            render_operand(lbuf, sizeof lbuf, lhs);
            render_operand(rbuf, sizeof rbuf, rhs);
            fprintf(out, "mov\tax,%s\ncwd\nmov\tcx,%s\nidiv\tcx\n",
                    lbuf, rbuf);
            fprintf(out, "mov\t%s,%s\n", lbuf, op == OP_ASDIV ? "ax" : "dx");
            break;
        }

        case OP_ASSIGN: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT && type != TY_PTR_INT)
                gen_fatal("ASSIGN of type %d not yet supported", type);
            Val rhs = materialize(out, &g, pop_val(&g));
            Val lhs = pop_val(&g);
            if (lhs.kind != VK_MEM && lhs.kind != VK_IND)
                gen_fatal("assignment to a non-lvalue is not yet supported");
            if (rhs.kind == VK_MEM)
                gen_fatal("direct memory-to-memory assignment (\"x = y;\") "
                          "is not yet supported - 8086 MOV cannot take two "
                          "memory operands, and no golden reference "
                          "confirms which intermediate register a real "
                          "compiler would route this through");
            char dbuf[32], sbuf[32];
            render_operand(dbuf, sizeof dbuf, lhs);
            render_operand(sbuf, sizeof sbuf, rhs);
            fprintf(out, "mov\t%s,%s\n", dbuf, sbuf);
            /* No push: nothing in this grammar scope consumes an
             * assignment expression's own value (every assignment is
             * a full statement, immediately followed by EXPR - see
             * c0_parser.c's parse_assign_stmt()). */
            break;
        }

        case OP_RFORCE: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("RFORCE to type %d not yet supported (only "
                          "int-returning functions are covered so far)", type);
            Val v = materialize(out, &g, pop_val(&g));
            /* Matches the confirmed golden pattern exactly, for both
             * an immediate (00_smoke's "return 42;") and a memory
             * operand (01_intarith's "return c;"): load into DI
             * first, then move DI into AX (sect. 1.5's return-value
             * register) - kept as the same two-instruction shape
             * rather than "optimized" to a direct "mov ax,<v>" since
             * that would no longer match real hardware output. */
            load_into_di(out, v);
            fprintf(out, "mov\tax,di\n");
            break;
        }

        case OP_EXPR: {
            (void)c1_read_num(temp1, "temp1"); /* source line - not
                                                 * rendered into the
                                                 * .s output anywhere
                                                 * in the confirmed
                                                 * goldens. */
            /* Flush any postfix ++/-- fixup queued by gen_incdec()
             * during this statement - see DEFERRED_MAX's comment on
             * GenState. */
            flush_deferred(out, &g);
            break;
        }

        case OP_RETRN: {
            int type = c1_read_num(temp1, "temp1");
            fprintf(out, "|RTYP %d\njmp\tcret\n", type);
            break;
        }

        case OP_SETSTK: {
            int bytes = c1_read_num(temp1, "temp1");
            int extra = bytes - (-MCC_STAUTO); /* bytes beyond what the
                                                 * SAVE prologue's own
                                                 * push di/push si
                                                 * already reserves -
                                                 * see mutos_cc.h. */
            if (extra < 0)
                gen_fatal("SETSTK value %d is smaller than the fixed "
                          "register-save area (%d bytes) - malformed "
                          "temp1 stream", bytes, -MCC_STAUTO);
            if (extra == 0) {
                /* Nothing to emit - matches every 00_smoke golden's
                 * "L1:jmp\tL2" with no "sub sp" in between. */
            } else if (extra <= 76) {
                /* Confirmed against docs/MUTOS_C_ABI.md sect. 1.9 and
                 * 01_intarith.s.golden's "L1:sub\tsp,*6.\njmp\tL2":
                 * the largest real-hardware example using a plain
                 * "sub sp,N" (not "call chkstk") has N=76. */
                fprintf(out, "sub\tsp,*%d.\n", extra);
            } else if (extra > 256) {
                /* Confirmed lower bound for "call chkstk" (see
                 * docs/MUTOS_C_ABI.md sect. 1.9): the smallest real
                 * example using it has N=256. */
                fprintf(out, "mov\tax,*%d.\ncall\tchkstk\n", extra);
            } else {
                gen_fatal("local-frame size %d bytes falls in the "
                          "unconfirmed (76,256] gap between a plain "
                          "\"sub sp,N\" and \"call chkstk\" (see "
                          "docs/MUTOS_C_ABI.md sect. 1.9 and "
                          "tests/mutos_cc/09_abiprobe/) - not yet "
                          "supported rather than guessing", extra);
            }
            break;
        }

        default:
            gen_fatal("unsupported temp1 opcode %d (0x%02x) - mutos_c1's "
                      "current coverage is limited to the 00_smoke/"
                      "01_intarith subset; see src/mutos_cc/README.md",
                      op, op);
        }
    }

    fprintf(out, ".data\n");
    return 0;
}
