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
 * COLON, QUEST, SEQNC, LCON, LTOI, ITOC, CTOL, ASSIGN, RFORCE, EXPR,
 * RETRN, SETSTK, EOFC.
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

/* Matches c0_parser.c's MCC_SZINT exactly - the size (bytes) of a
 * plain int/pointer, and (per docs/MUTOS_C_ABI.md sect. 1.6) the
 * offset from a 'long' local's own base offset to its LOW word. */
#define MCC_SZINT 2

#define DEFERRED_MAX 4

/* Matches c0_parser.c's own MCC_NCASES - the max number of 'case'
 * labels OP_SWIT's handler below will collect for one 'switch'. */
#define MCC_NCASES 32

/* Matches c0_parser.c's own MCC_MAXPARAMS - the max arguments a
 * single OP_CALL's argument list (see VK_ARGLIST below) will
 * collect. */
#define MCC_MAXCALLARGS 16

/* "function returning int" - matches c0_parser.c's TY_FUNC_INT
 * exactly (see its own comment there); only used here to validate an
 * incoming NAME's type when its storage class is SC_EXTERN (a called
 * function - see OP_NAME's handler below). */
#define TY_FUNC_INT (TY_INT | 020)

/* "pointer to function returning int" - matches c0_parser.c's
 * TY_PTR_FUNC_INT exactly (see its own comment there: v7/cc's
 * incref(TY_FUNC_INT) = 72). */
#define TY_PTR_FUNC_INT 72

/* "pointer to pointer to int" - matches c0_parser.c's ty_ptr_of()
 * applied to TY_PTR_INT (the same incref() chaining TY_PTR_FUNC_INT's
 * own comment derives, generalized) - confirmed against
 * 05_arrptr/06_ptrptr.1.golden's "int **pp;" (every NAME/AMPER/ASSIGN
 * touching `pp` uses type 40, NOT a naive "16"). */
#define TY_PTR_PTR_INT 40

typedef enum { VK_IMM, VK_MEM, VK_MEM_DIRECT, VK_REG, VK_IND, VK_IND_PENDING, VK_COND, VK_PAIR, VK_LONG, VK_LCON,
               VK_FUNC, VK_ARGLIST, VK_MEM_CVT, VK_STATIC, VK_FUNCADDR } ValKind;
/* VK_FUNC - a called function's own NAME, not yet an OP_CALL - a
 * pure compile-time reference (the callee's symbol text, owned/
 * malloc'd - see OP_NAME's SC_EXTERN case), never an operand of any
 * real instruction. Only OP_CALL itself ever pops one. */
/* VK_ARGLIST - an OP_CALL argument list under construction (see
 * OP_COMMA's/OP_NULLOP's handlers below): a fixed-capacity,
 * heap-allocated (owned) array of already-resolved argument Vals, in
 * left-to-right (source) order. A call with exactly one argument
 * never produces one of these (the lone argument's own Val is used
 * directly - see gen_call()) - only zero arguments (OP_NULLOP) or
 * two-or-more (chained OP_COMMA) do. */
/* VK_MEM_CVT - a plain memory operand (rendered identically to
 * VK_MEM - see render_operand()) produced by a TYPE CONVERSION
 * (currently only OP_LTOI - see its handler below) rather than
 * directly by a NAME reference, kept as a distinct kind purely so
 * OP_ASSIGN's plain-type case can tell it apart from a genuine bare-
 * NAME memory operand: this project's real "x = y;" direct mem-to-
 * mem assignment shape is still unconfirmed by any golden (8086 MOV
 * cannot take two memory operands, and no golden shows which
 * intermediate register a real compiler would route it through), so
 * OP_ASSIGN still gen_fatal()s on a bare VK_MEM rhs - but "i = (int)
 * l;" (08_castsize.s.golden's "mov di,*-8.(bp) / mov *-6.(bp),di")
 * confirms THIS specific case (a narrowing conversion's result used
 * as an assignment's rhs) DOES go through DI first. Every other
 * consumer (a binary operator's operand - confirmed against
 * 02_long/04_params.s.golden's "fd + (int) offset" -> "add di,*8.
 * (bp)", the memory operand used directly with no intervening move
 * at all) treats VK_MEM_CVT exactly like VK_MEM, needing no special
 * handling of its own. */
/* VK_STATIC - a local STATIC variable's own dedicated-label memory
 * reference (`offset` holds the internal LABEL number, not a
 * bp-relative stack offset - see OP_NAME's SC_STATIC case below and
 * c0_sym.h's symtab_declare_static() comment). Renders as a bare
 * "L<n>" operand (render_operand()) - confirmed against
 * 04_funcs/05_staticvar.s.golden's "mov di,L4"/"mov L4,di": a
 * different SOURCE-TEXT shape from VK_MEM's "*N.(bp)" but otherwise
 * usable identically (both are just an addressable memory operand -
 * so unlike VK_MEM_CVT, no OP_ASSIGN special-casing is needed: an
 * ordinary MOV between two real memory operands is still illegal
 * either way, but that restriction is orthogonal to which of VK_MEM/
 * VK_STATIC is involved and is not exercised by any golden for
 * VK_STATIC specifically). */
/* VK_FUNCADDR - "the address of a function", OP_AMPER applied to a
 * VK_FUNC operand (a bare function name used as a value - see
 * c0_parser.c's parse_primary() T_IDENT fallback). `reg` holds the
 * function's own malloc'd symbol text, TAKEN OVER from the VK_FUNC
 * it was built from (owned - freed by whichever consumer renders
 * it; currently only OP_ASSIGN's plain-type case). Renders as
 * "#<name>" (render_operand()) - a function's address is a
 * compile-time (link-time-relocatable) constant, so unlike the
 * array-decay AMPER case (a real bp-relative runtime address,
 * needing an actual "lea"), NO code is emitted for this AMPER at
 * all - confirmed against 07_funcptr.s.golden's "fp = square;" ->
 * "mov *-6.(bp),#_square" (a single, direct memory-immediate MOV,
 * with no preceding "lea" or "mov di,..." of any kind). */
/* VK_IND - an indirect "(reg)" memory operand, the result of
 * dereferencing a pointer (OP_STAR) - confirmed against
 * 05_incdec.s.golden's "mov\t(di),*1." (the STAR-dereferenced
 * assignment target). `reg` holds the register name, same as
 * VK_REG. */
/* VK_MEM_DIRECT - a pending or resolved compile-time-constant
 * ADDRESS-OF a plain memory location, `offset` being that address's
 * own bp-relative offset - the deferred/folded form of an OP_AMPER
 * whose result is about to be combined with a purely compile-time-
 * constant scaled index (a subscript with a literal-constant index,
 * "v[0]" - see OP_AMPER's own peek and OP_PLUS's fold below), used
 * so the "lea"+"add" pair a runtime index would need can be skipped
 * entirely when everything folds to a single known offset - confirmed
 * against 05_arrptr/04_ptrarreq.s.golden's "v[0] = 1;" -> a single
 * "mov\t*-12.(bp),*1." (no lea/add at all, contrast 01_arrbasic.s.
 * golden's "a[i] = ...", a genuinely runtime index, which does need
 * the full "lea"+"mov si,...;sal;add" shape). OP_STAR reuses it
 * completely unchanged (no "mov di,..." load at all - dereferencing
 * a compile-time-known address is just that memory location itself),
 * and OP_ASSIGN accepts it as an ordinary lvalue, rendered identically
 * to plain VK_MEM. */
/* VK_IND_PENDING - an indirect assignment target whose own address
 * computation has already been PUSHED onto the real machine stack
 * (not left in a register) because it is about to be followed by a
 * non-trivial right-hand side that itself needs working registers -
 * confirmed against 05_arrptr/01_arrbasic.s.golden's "a[i] = i * i;"
 * and 05_arrptr/03_ptrbasic.s.golden's "*p = *p + 1;": both show the
 * just-computed address pushed ("push\tdi"/"push\t*-10.(bp)") BEFORE
 * the right-hand side's own code runs, then popped into BX
 * ("pop\tbx") immediately before the final store ("mov\t(bx),..."),
 * rather than the ordinary VK_IND shape's eager "mov (di),<rhs>" -
 * confirmed by 05_incdec.s.golden's "*p = 20;"/"*p++ = 1;" (a bare
 * constant right-hand side - no code of its own to collide with) NOT
 * doing this. See OP_STAR's own comment for the exact trigger (a
 * one-opcode-of-lookahead peek, via a save/restore of temp1's own
 * file position - the wire bytes themselves are unaffected, so this
 * is purely a c1-side code-shape decision). Carries no register (the
 * value is on the hardware stack, not in one) - OP_ASSIGN is the
 * only confirmed consumer, via a "pop\tbx" first. */
/* VK_LONG - a MATERIALIZED 32-bit 'long' result already sitting in
 * registers: HIGH word in DI, LOW word in SI, matching
 * docs/MUTOS_C_ABI.md sect. 1.6's "high word at the lower address"
 * convention carried into registers - confirmed as the fixed output
 * convention of OP_CTOL and of materialize_long() (see VK_LCON just
 * below) below, and the fixed input convention OP_ASSIGN's
 * long-target case (also below) requires. A pure marker - no extra
 * fields needed since the DI/SI registers themselves are the only
 * confirmed location. */
/* VK_LCON - an UNMATERIALIZED 'long' constant: OP_LCON's raw (hi,lo)
 * pair (stored in `imm`=lo, `offset`=hi below), with NO code emitted
 * yet. Deliberately deferred, unlike every other value-producing
 * opcode here: a 'long' constant used as a relational-comparison
 * operand (confirmed against 02_long/01_addsub.s.golden's
 * "if (c > 0L)") never materializes into DI/SI at all - the
 * comparison's own codegen (gen_long_cmp() below) folds it directly
 * into a bare immediate operand instead. Whoever actually needs a
 * real DI:SI value (OP_ASSIGN's long-target case, so far the only
 * other confirmed consumer) calls materialize_long() first, which
 * reproduces the same two shapes this constant used to emit eagerly
 * (still byte-identical for every already-confirmed case, since
 * ASSIGN was already always the very next opcode after LCON with
 * nothing in between). */
/* VK_PAIR - OP_COLON's result: an unmaterialized pair of "?:"
 * branches, consumed only by the immediately following OP_QUEST (see
 * their handlers below and docs/DEVLOG.md's Milestone 4 section for
 * the 07_ternary derivation). Reuses the Val struct's cl/cr fields
 * (see the VK_COND comment below) purely as convenient storage for
 * two already-resolved SimpleVals - true-branch in cl, false-branch
 * in cr - not as an actual comparison. */

/* A fully-resolved (never itself VK_COND) operand - used to hold the
 * two sides of a deferred comparison inside a VK_COND Val without
 * making the Val type self-referential. kind is usually VK_IMM,
 * VK_MEM or VK_REG, but can also be VK_LCON for a 'long' comparison's
 * (unmaterialized) constant operand - see gen_long_cmp(). */
typedef struct {
    ValKind kind;
    long    imm;
    int     offset;
    const char *reg;
} SimpleVal;

/* Forward-declared (as just a pointer target) so Val below can hold
 * an ArgList* - the full definition (which embeds a Val array, and
 * so needs Val itself complete first) follows right after Val. */
typedef struct ArgList ArgList;

typedef struct {
    ValKind kind;
    long    imm;    /* VK_IMM; VK_LCON's low word */
    int     offset;  /* VK_MEM: bp-relative offset; VK_LCON's high word */
    const char *reg;  /* VK_REG: a static string ("ax", "di", "dx");
                        * VK_FUNC: the callee's own malloc'd symbol
                        * text (owned - see OP_NAME's SC_EXTERN case) */
    ArgList *arglist; /* VK_ARGLIST only - see its own typedef comment
                        * above (owned - see ArgList's own comment
                        * just below Val) */
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
    int       cond_is_long; /* VK_COND only: set when cl/cr are 'long'
     * operands (gen_long_cmp()'s shape below) rather than the
     * ordinary 16-bit shape emit_cmp_and_branch() renders - only
     * OP_CBRANCH is confirmed to consume one (03_ctrlflow/01_addsub's
     * "if (c > 0L)"); every other consumer (materialize(), OP_LOGAND/
     * OP_LOGOR, OP_QUEST) gen_fatal()s on it rather than guessing a
     * shape no golden confirms. */
} Val;

/* VK_ARGLIST's owned backing store - see its ValKind comment above.
 * Always heap-allocated (malloc'd once, at the first OP_COMMA or at
 * OP_NULLOP for a call - see their handlers below) and freed by
 * whichever OP_CALL consumes it (see gen_call()). Fixed-capacity
 * (MCC_MAXCALLARGS), not grown dynamically - matches this codebase's
 * other fixed-capacity buffers (e.g. GenState's own `deferred`,
 * `cases`). */
struct ArgList {
    int n;
    Val items[MCC_MAXCALLARGS];
};

typedef struct {
    int  regvar;              /* the most recently seen SETREG value -
                                * consulted nowhere yet (OP_NAME's
                                * SC_REG case maps a symbol's own
                                * offset, not this, to a physical
                                * register - see regvar_name() below);
                                * kept for parity/future use. */
    int  setreg_seen;         /* reset to 0 at each OP_SAVE (one per
                                * function); set to 1 the first time
                                * OP_SETREG is then seen. A function's
                                * FIRST SETREG (funchead()'s own,
                                * always MCC_INIT_REGVAR, unconditional)
                                * renders no visible text; every
                                * SUBSEQUENT one (only emitted by
                                * c0_parser.c when regvar actually
                                * changed - a 'register' local claimed,
                                * or the end-of-function restore) DOES,
                                * regardless of its value - confirmed
                                * against 04_funcs/06_regclass.s.
                                * golden's "|NREG 3" restore comment,
                                * whose SETREG value (MCC_INIT_REGVAR=4)
                                * is numerically identical to the
                                * silent initial one, so only POSITION
                                * (not value) distinguishes them - see
                                * OP_SETREG's own handler below. */
    int  di_reserved;         /* reset to 0 at each OP_SAVE; set to 1
                                * once OP_RNAME claims DI for a
                                * 'register'-class local (04_funcs/
                                * 06_regclass.c) - stays 1 for the rest
                                * of the function (its lifetime), since
                                * this grammar scope never frees a
                                * register mid-function. While set, DI
                                * is off-limits as the generic "working
                                * register" an otherwise-unresolved
                                * value gets loaded into - SI is used
                                * instead. Only OP_RFORCE's default
                                * path is confirmed to need this so far
                                * (06_regclass.s.golden's "return sum;"
                                * -> "mov si,*-6.(bp) / mov ax,si", not
                                * the usual DI-then-AX shape); every
                                * other "go through DI" site is left
                                * unchanged since none is exercised
                                * with a live register variable by any
                                * golden yet. */
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
static Val val_mem_direct(int off) { Val r = {0}; r.kind = VK_MEM_DIRECT; r.offset = off; return r; }
static Val val_reg(const char *reg) { Val r = {0}; r.kind = VK_REG; r.reg = reg; return r; }
static Val val_ind(const char *reg) { Val r = {0}; r.kind = VK_IND; r.reg = reg; return r; }
static Val val_ind_pending(void) { Val r = {0}; r.kind = VK_IND_PENDING; return r; }
static Val val_long(void) { Val r = {0}; r.kind = VK_LONG; return r; }
/* `name` is taken over (owned) by the returned Val - freed by
 * whichever OP_CALL pops it (see gen_call()). */
static Val val_func(char *name) { Val r = {0}; r.kind = VK_FUNC; r.reg = name; return r; }
static Val val_static(int label) { Val r = {0}; r.kind = VK_STATIC; r.offset = label; return r; }

/* Maps a claimed register-variable's own slot number (c0_parser.c's
 * try_claim_register()'s return value, the SAME number RNAME/a later
 * SC_REG NAME both carry as their "offset") to its physical register
 * name - confirmed only for slot 3 = "di" (04_funcs/06_regclass.
 * s.golden's "| _i=di"); slot 2 = "si" is the structurally next slot
 * this same algorithm hands out (see try_claim_register()'s own
 * comment) but is not itself exercised by any golden, so it is
 * included as a direct, low-risk extension of the confirmed mapping
 * rather than a guess about codegen shape - any actual CODEGEN
 * combining two live register variables at once remains unconfirmed
 * and gen_fatal()s elsewhere (see OP_PLUS/OP_MINUS/OP_ASSIGN below). */
static const char *regvar_name(int regnum)
{
    switch (regnum) {
    case 3: return "di";
    case 2: return "si";
    default:
        gen_fatal("register-variable slot %d has no confirmed physical "
                  "register mapping (only slots 3/2 = di/si are covered "
                  "so far)", regnum);
    }
    return NULL; /* unreached - gen_fatal() never returns */
}

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
    case VK_MEM_DIRECT: snprintf(buf, n, "*%d.(bp)", v.offset); break;
    case VK_MEM_CVT: snprintf(buf, n, "*%d.(bp)", v.offset); break;
    case VK_STATIC: snprintf(buf, n, "L%d", v.offset); break;
    case VK_FUNCADDR: snprintf(buf, n, "#%s", v.reg); break;
    case VK_REG: snprintf(buf, n, "%s", v.reg); break;
    case VK_IND: snprintf(buf, n, "(%s)", v.reg); break;
    case VK_IND_PENDING: snprintf(buf, n, "<unpopped-ind-pending>"); break;
    case VK_COND: snprintf(buf, n, "<unmaterialized-cond>"); break;
    case VK_PAIR: snprintf(buf, n, "<unmaterialized-pair>"); break;
    case VK_LONG: snprintf(buf, n, "<unrendered-long-di:si>"); break;
    case VK_LCON: snprintf(buf, n, "<unmaterialized-long-const>"); break;
    case VK_FUNC: snprintf(buf, n, "<unrendered-func-name>"); break;
    case VK_ARGLIST: snprintf(buf, n, "<unrendered-arglist>"); break;
    }
}

/* Materializes a VK_LCON (OP_LCON's deferred raw (hi,lo) pair - see
 * its ValKind comment above) into a real VK_LONG sitting in DI:SI -
 * pass-through for anything already resolved (VK_LONG from OP_CTOL
 * or a runtime-helper call, in particular). The two shapes are the
 * same ones OP_LCON itself used to emit eagerly, now emitted lazily
 * at the point of actual use: an int-range value whose high word is
 * just its low word's sign-extension (e.g. 37L) uses the CWD idiom;
 * a genuinely 32-bit value (e.g. 123456L, or 70000) direct-splits
 * into SI/DI - confirmed against 02_long/02_muldiv.s.golden and
 * 08_castsize.s.golden respectively (both still byte-identical under
 * this lazier scheme, since OP_ASSIGN's long-target case - the only
 * confirmed consumer needing a real materialized value - was already
 * always the very next opcode after LCON in every one of those
 * cases, with nothing in between to observe the difference). */
static Val materialize_long(FILE *out, Val v)
{
    if (v.kind != VK_LCON)
        return v;
    long lo = v.imm, hi = v.offset;
    if (hi == (lo < 0 ? -1 : 0)) {
        char buf[32];
        render_operand(buf, sizeof buf, val_imm(lo));
        fprintf(out, "mov\tax,%s\n", buf);
        fprintf(out, "cwd\n");
        fprintf(out, "mov\tdi,dx\n");
        fprintf(out, "mov\tsi,ax\n");
    } else {
        char lobuf[32], hibuf[32];
        render_operand(lobuf, sizeof lobuf, val_imm(lo));
        render_operand(hibuf, sizeof hibuf, val_imm(hi));
        fprintf(out, "mov\tsi,%s\n", lobuf);
        fprintf(out, "mov\tdi,%s\n", hibuf);
    }
    return val_long();
}

static void load_into_di(FILE *out, Val v); /* forward decl - defined
                                               * below, needed by
                                               * emit_cmp_and_branch()
                                               * above its own definition */

/* SAL/SAR's immediate shift-count operand omits the trailing "."
 * decimal-terminator that render_operand() uses everywhere else -
 * confirmed via 04_shift.s.golden's "sar\tdi,*1" (no period) vs.
 * every "mov\t...,*N." elsewhere (with period). Per man/mutos_as.1
 * the period is "a stylistic decimal terminator" with zero effect on
 * the assembled value, so this is a source-text quirk of this one
 * instruction shape specifically - only rendering, not semantics.
 * Confirmed only at value 1 (04_shift's only confirmed constant-count
 * shift); the *N/#N byte-vs-word marker threshold below is
 * extrapolated from render_operand()'s own (unconfirmed at
 * magnitudes outside that single case). CMP's own immediate operand
 * turned out NOT to share this shape in general - see
 * render_cmp_imm() below - so this function is no longer used for
 * CMP. */
static void render_bare_imm(char *buf, size_t n, long v)
{
    if (v >= -128 && v <= 127)
        snprintf(buf, n, "*%ld", v);
    else
        snprintf(buf, n, "#%ld", v);
}

/* CMP's immediate right-hand side: unlike SAL/SAR's above, a
 * genuinely non-zero CMP immediate DOES carry the trailing "."
 * decimal-terminator, matching render_operand()'s ordinary
 * convention exactly - confirmed against 03_ctrlflow's goldens
 * ("cmp\t*-6.(bp),*10.", "*5.", "*3."). The ONE exception is a bare
 * comparison against 0 specifically, which omits it ("cmp\t*-6.(bp),
 * *0", confirmed against 01_expr/03_rellogic.s.golden) - every CMP
 * immediate confirmed there is value 0, so that was the only
 * magnitude an earlier session's "CMP's immediate never has a
 * period" conclusion actually verified; it did not generalize to
 * other values, as 03_ctrlflow's non-zero cases now show. The *N/#N
 * byte-vs-word marker threshold is render_operand()'s own,
 * unconfirmed at a magnitude needing '#' for CMP specifically. */
static void render_cmp_imm(char *buf, size_t n, long v)
{
    if (v == 0)
        snprintf(buf, n, "*0");
    else if (v >= -128 && v <= 127)
        snprintf(buf, n, "*%ld.", v);
    else
        snprintf(buf, n, "#%ld.", v);
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
    if (r.kind == VK_MEM || r.kind == VK_MEM_CVT) {
        load_into_di(out, r);
        snprintf(rbuf, sizeof rbuf, "di");
    } else if (r.kind == VK_IMM) {
        render_cmp_imm(rbuf, sizeof rbuf, r.imm);
    } else {
        render_operand(rbuf, sizeof rbuf, r);
    }
    fprintf(out, "cmp\t%s,%s\n", lbuf, rbuf);
    fprintf(out, "%s\tL%d\n", cond_true_mnem(branch_op_code), target_lab);
}

/* Codegen for a 'long' relational comparison consumed by OP_CBRANCH -
 * see Val's cond_is_long field comment. A 32-bit signed comparison on
 * a 16-bit ALU genuinely needs a different branch shape per operator
 * (compare high words SIGNED first - that alone decides the answer
 * whenever they differ; only when they're equal does the low word's
 * UNSIGNED comparison matter), so only the single shape
 * 02_long/01_addsub.c's "if (c > 0L)" confirms is implemented here:
 * a 'long' lvalue (VK_MEM) OP_GREAT a literal 0L (VK_LCON with hi==0
 * && lo==0), consumed at a "branch if FALSE" CBRANCH site (cond_sense
 * ==0, e.g. an 'if' with no matching goto/break/continue shortcut -
 * see parse_if_stmt()). Confirmed byte-for-byte against
 * 01_addsub.s.golden:
 *   cmp <l.high>,*0 / blt <target> / bgt <fresh-true-label> /
 *   cmp <l.low>,*0. / blos <target> / <fresh-true-label>:
 * Every other combination - a different operator, a non-zero or
 * non-constant right operand, cond_sense==1 (a "branch if true" site)
 * - is an explicit "not yet supported" rather than a guess, per this
 * project's verification rule. */
static void gen_long_cmp(FILE *out, GenState *g, int op, SimpleVal l,
                          SimpleVal r, int cond_sense, int target_lab)
{
    if (op != OP_GREAT || l.kind != VK_MEM || r.kind != VK_LCON ||
        r.imm != 0 || r.offset != 0 || cond_sense != 0)
        gen_fatal("this 'long' relational comparison shape is not yet "
                  "supported (only 'longvar > 0L' as an 'if'/'while'/"
                  "'for' condition is confirmed - see docs/DEVLOG.md)");

    int true_lab = g->next_lab++;
    char hibuf[32], lobuf[32];
    render_operand(hibuf, sizeof hibuf, val_mem(l.offset));
    render_operand(lobuf, sizeof lobuf, val_mem(l.offset + MCC_SZINT));
    fprintf(out, "cmp\t%s,*0\n", hibuf);
    fprintf(out, "blt\tL%d\n", target_lab);
    fprintf(out, "bgt\tL%d\n", true_lab);
    fprintf(out, "cmp\t%s,*0.\n", lobuf);
    fprintf(out, "blos\tL%d\n", target_lab);
    fprintf(out, "L%d:", true_lab);
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

/* OP_QUEST: the "?:" ternary select, given the (already-computed)
 * condition `cond` and the VK_PAIR of branch values (`pair.cl`=true,
 * `pair.cr`=false) OP_COLON packaged - confirmed against
 * 07_ternary's "a > b ? a : b":
 *   cmp a,b; ble Lfalse    (INVERTED condition branches to the false
 *                            label; fallthrough is the TRUE branch -
 *                            the opposite polarity from
 *                            materialize_cond()'s bare-comparison-as-
 *                            0/1-value pattern above, which branches
 *                            to a TRUE label instead. "?:" selects
 *                            between two arbitrary values rather than
 *                            producing a fixed 0/1, so each branch's
 *                            own codegen has to live inline inside
 *                            its own arm rather than behind a shared
 *                            "set di=1"/"set di=0" instruction)
 *   mov di,a                 (true branch, inline at the fallthrough)
 *   jmp Lend
 *   Lfalse: mov di,b          (false branch)
 *   Lend:
 * Label allocation order false,end matches the golden's
 * L10000(false)/L10001(end) exactly - only two labels, unlike
 * gen_logand()'s three, since there is no separate "true" label to
 * jump to (the true branch's code sits directly at the fallthrough
 * point, not behind its own jump target). Each branch is reloaded
 * into DI unconditionally, with no attempt to notice DI might
 * already hold the right value from the comparison's own setup (e.g.
 * the false branch here happens to equal what the "cmp"'s own
 * right-hand-side load already put in DI) - confirmed by the golden
 * itself re-doing "mov di,*-8.(bp)" at the false label rather than
 * omitting it. */
static Val gen_quest(FILE *out, GenState *g, Val cond, Val pair)
{
    Val c = as_cond(cond);
    int lfalse = g->next_lab++;
    int lend   = g->next_lab++;
    emit_cmp_and_branch(out, c, cond_invert(c.true_op), lfalse);
    load_into_di(out, val_from_simple(pair.cl));
    fprintf(out, "jmp\tL%d\n", lend);
    fprintf(out, "L%d:", lfalse);
    load_into_di(out, val_from_simple(pair.cr));
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

/* Same as load_into_di() above, but for SI - the fallback "working
 * register" for an otherwise-unresolved value while DI is reserved by
 * a live 'register'-class local (GenState's own di_reserved - see its
 * comment). */
static void load_into_si(FILE *out, Val v)
{
    if (v.kind == VK_REG && strcmp(v.reg, "si") == 0)
        return;
    char buf[32];
    render_operand(buf, sizeof buf, v);
    fprintf(out, "mov\tsi,%s\n", buf);
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

/* Shared codegen for a 'long' '*'/'/'/'%' - the 8086 has no 32x32
 * hardware multiply/divide, so this goes through one of the
 * compiler's own internal runtime helpers (docs/MUTOS_C_ABI.md sect.
 * 1.8) via `helper` ("lmul"/"ldiv"/"lrem"). Confirmed byte-for-byte
 * against 02_long/02_muldiv.s.golden's "c = a * b;"/"c = a / b;"/
 * "c = a %% b;": both operands are passed flat, as two ordinary
 * two-word 'long's (never the ABI doc's own prose-described pointer/
 * lvalue-and-result convention, which this specific golden does not
 * use) - right-to-left per sect. 1.1, so `r` (the second/right
 * operand) is pushed whole before `l`, and each operand's own two
 * words are pushed low-then-high per sect. 1.6 - i.e. push order is
 * r_low, r_high, l_low, l_high. The result comes back in DX:AX (high:
 * low) per sect. 1.5's ordinary long-return convention, then moved
 * into the DI(high):SI(low) convention every other long-value
 * producer here uses (OP_LCON/OP_CTOL) for OP_ASSIGN's long-target
 * case to consume. Only a plain memory long (a bare NAME reference)
 * is confirmed for either operand - not a value still sitting in
 * DI:SI (VK_LONG) from a preceding long op, which this one golden
 * (each operand used exactly once, straight from its own local) does
 * not exercise. */
static Val gen_long_binop_call(FILE *out, Val l, Val r, const char *helper)
{
    if (l.kind != VK_MEM || r.kind != VK_MEM)
        gen_fatal("'long' %s with a non-memory operand is not yet "
                  "supported", helper);
    char buf[32];
    render_operand(buf, sizeof buf, val_mem(r.offset + MCC_SZINT));
    fprintf(out, "mov\tdi,%s\n", buf);
    fprintf(out, "push\tdi\n");
    render_operand(buf, sizeof buf, val_mem(r.offset));
    fprintf(out, "mov\tdi,%s\n", buf);
    fprintf(out, "push\tdi\n");
    render_operand(buf, sizeof buf, val_mem(l.offset + MCC_SZINT));
    fprintf(out, "mov\tdi,%s\n", buf);
    fprintf(out, "push\tdi\n");
    render_operand(buf, sizeof buf, val_mem(l.offset));
    fprintf(out, "mov\tdi,%s\n", buf);
    fprintf(out, "push\tdi\n");
    fprintf(out, "call\t%s\n", helper);
    fprintf(out, "add\tsp,*8.\n");
    fprintf(out, "mov\tdi,dx\n");
    fprintf(out, "mov\tsi,ax\n");
    return val_long();
}

/* Codegen for OP_CALL - confirmed byte-for-byte against every
 * 04_funcs .1.golden/.s.golden pair with a direct call (01_call,
 * 02_manyargs, 03_recfact, 04_mutrec), against 02_long/04_params.s.
 * golden's "myseek(3, 90000L, 1)" (a 'long' constant argument - see
 * below), and against 07_funcptr.s.golden's two indirect-call sites
 * (see below). `callee` is either VK_FUNC (a direct call - see
 * OP_NAME's SC_EXTERN handler) or VK_MEM (an INDIRECT call through a
 * function-pointer variable's own memory location - see OP_STAR's
 * TY_FUNC_INT case above, which deliberately leaves such an operand
 * as a plain, still-unresolved VK_MEM rather than dereferencing it
 * into a register): confirmed against 07_funcptr.s.golden's
 * "return (*f)(x);" -> "call\t@*4.(bp)" (`f`'s own parameter slot,
 * used directly - no preceding "mov"/"lea" of any kind) - the "@"
 * prefix is mutos_as's indirect-call marker. `args` is the
 * already-resolved argument-tree Val: VK_ARGLIST (built by
 * OP_NULLOP/OP_COMMA below) for zero or 2+ arguments, or any other
 * kind directly for exactly one argument (see parse_call()'s own
 * comment in c0_parser.c for why a lone argument never goes through
 * VK_ARGLIST).
 *
 * docs/MUTOS_C_ABI.md sect. 1.1: every argument is pushed
 * right-to-left (the LAST-declared argument first), and the CALLER
 * cleans up afterward via "add sp,N" (N = 2 bytes per argument WORD -
 * not per argument: a 'long' argument occupies two words, so N tracks
 * total words pushed, `nwords`, not `nargs` - see 02_long/04_params.
 * s.golden's "add sp,*8." for 3 arguments/4 words). An ordinary
 * (single-word) argument is pushed AS-IS whenever 8086's PUSH can
 * take it directly (a register or any addressable memory operand -
 * VK_MEM/VK_MEM_CVT/VK_STATIC/VK_IND/VK_REG alike): confirmed against
 * 07_funcptr.s.golden's "apply(fp, 5)" -> "...push\t*-6.(bp)" (`fp`,
 * a plain local, pushed directly with NO preceding "mov" at all) and
 * "return (*f)(x);" -> "push\t*6.(bp)" (`x`, similarly direct) -
 * genuinely different from every PRIOR confirmed call site, which
 * happened to push only an immediate (needing DI first, since 8086's
 * PUSH has no immediate form - "mov di,<val>" then "push di") or an
 * already-DI value (03_recfact's "n - 1", where load_into_di()'s own
 * no-op case already produced the identical bytes either way - so
 * this is a strict generalization of the earlier "always go through
 * DI" understanding, not a behavior change for any already-confirmed
 * site). A 'long' CONSTANT argument (VK_LCON) has two confirmed
 * shapes - see the "if (hi == ...)" branch in the loop below for the
 * full derivation; either shape still pushes low-word-then-high-word
 * overall (sect. 1.6: "pushing a long argument is exactly equivalent
 * to ... push the low word first, then the high word"). A 'long'
 * value already materialized into DI:SI (VK_LONG) is not exercised by
 * any golden and is left unsupported here rather than guessed. The
 * call's own result is always left in AX (sect. 1.5) - pushed back as
 * val_reg("ax") for whatever consumes the call expression next
 * (OP_RFORCE's now-confirmed "already in ax, skip the move" case, or
 * an enclosing operator like OP_TIMES, which renders it via the
 * ordinary "mov ax,<operand>" path either way - see 03_recfact.s.
 * golden's "mov ax,ax" self-move for the latter) - UNLESS the callee
 * itself is 'long'-returning (`is_long_ret`), in which case the
 * result instead comes back in DX:AX and is moved into the
 * DI(high):SI(low) convention every other long-value producer here
 * uses, exactly like gen_long_binop_call()'s own lmul/ldiv/lrem calls
 * (see below).
 */
static Val gen_call(FILE *out, Val callee, Val args, int is_long_ret)
{
    if (callee.kind != VK_FUNC && callee.kind != VK_MEM)
        gen_fatal("this call-callee shape is not yet supported - see "
                  "src/mutos_cc/README.md");

    Val single[1];
    Val *items;
    int nargs;
    if (args.kind == VK_ARGLIST) {
        items = args.arglist->items;
        nargs = args.arglist->n;
    } else {
        single[0] = args;
        items = single;
        nargs = 1;
    }

    int nwords = 0;
    for (int i = nargs - 1; i >= 0; i--) {
        if (items[i].kind == VK_LCON) {
            /* Two confirmed shapes - the SAME distinction
             * materialize_long() itself makes: a genuinely 32-bit
             * value (hi is NOT its low word's own sign-extension)
             * direct-splits, low word then high word - confirmed
             * against 02_long/04_params.s.golden's "myseek(3, 90000L,
             * 1)" -> "mov di,#24464./push di/mov di,*1./push di". An
             * int-range value that merely carries a 'long' suffix (hi
             * IS its low word's sign-extension) instead uses the CWD
             * sign-extension idiom, pushing AX(low) then DX(high) -
             * confirmed against 02_long/03_retval.s.golden's
             * "addlong(100000L, 5L)" -> the "5L" argument rendering
             * as "mov ax,*5. / cwd / push ax / push dx", never the
             * direct-split shape even though both take the same
             * VK_LCON wire path. */
            long lo = items[i].imm, hi = items[i].offset;
            char buf[32];
            if (hi == (lo < 0 ? -1 : 0)) {
                render_operand(buf, sizeof buf, val_imm(lo));
                fprintf(out, "mov\tax,%s\n", buf);
                fprintf(out, "cwd\n");
                fprintf(out, "push\tax\n");
                fprintf(out, "push\tdx\n");
            } else {
                render_operand(buf, sizeof buf, val_imm(lo));
                fprintf(out, "mov\tdi,%s\n", buf);
                fprintf(out, "push\tdi\n");
                render_operand(buf, sizeof buf, val_imm(hi));
                fprintf(out, "mov\tdi,%s\n", buf);
                fprintf(out, "push\tdi\n");
            }
            nwords += 2;
            continue;
        }
        if (items[i].kind == VK_LONG)
            gen_fatal("a 'long' argument already materialized into DI:SI "
                      "is not yet supported as a call argument - see "
                      "src/mutos_cc/README.md");
        if (items[i].kind == VK_MEM_DIRECT) {
            /* A deferred address-of argument ("sumarr(v, 4);" - `v`
             * decaying to its address - see OP_AMPER's own comment
             * for why this is deferred at all, specifically to reach
             * this point rather than clobbering DI before its turn)
             * - materialized right here, at its own push, via "lea" -
             * confirmed against 04_ptrarreq.s.golden's "lea\tdi,
             * *-12.(bp)" immediately followed by "push\tdi" (with the
             * OTHER argument's own "mov di,*4."/"push di" already
             * emitted first, since arguments push right-to-left). */
            char buf[32];
            render_operand(buf, sizeof buf, items[i]);
            fprintf(out, "lea\tdi,%s\n", buf);
            fprintf(out, "push\tdi\n");
            nwords += 1;
            continue;
        }
        if (items[i].kind == VK_IMM) {
            load_into_di(out, items[i]);
            fprintf(out, "push\tdi\n");
        } else {
            char buf[32];
            render_operand(buf, sizeof buf, items[i]);
            fprintf(out, "push\t%s\n", buf);
        }
        nwords += 1;
    }

    if (callee.kind == VK_FUNC) {
        fprintf(out, "call\t%s\n", callee.reg);
        free((char *)callee.reg);
    } else {
        char buf[32];
        render_operand(buf, sizeof buf, callee);
        fprintf(out, "call\t@%s\n", buf);
    }
    if (args.kind == VK_ARGLIST)
        free(args.arglist);

    if (nwords > 0) {
        char buf[32];
        render_operand(buf, sizeof buf, val_imm(2L * nwords));
        fprintf(out, "add\tsp,%s\n", buf);
    }
    if (is_long_ret) {
        /* A 'long'-returning callee's result comes back in DX:AX
         * (sect. 1.5's ordinary long-return convention - same as
         * gen_long_binop_call()'s own lmul/ldiv/lrem helper calls
         * above), moved into the DI(high):SI(low) convention every
         * other long-value producer here uses - confirmed against
         * 02_long/03_retval.s.golden's "call _addlong / add sp,*8. /
         * mov di,dx / mov si,ax". */
        fprintf(out, "mov\tdi,dx\n");
        fprintf(out, "mov\tsi,ax\n");
        return val_long();
    }
    return val_reg("ax");
}

/* One case's (label, value) pair, as collected by OP_SWIT's own
 * handler from its trailing table before handing off to
 * gen_switch_dispatch() below. */
typedef struct { int lab; int val; } CaseSwitchEntry;

/* Codegen for OP_SWIT's jump table - confirmed byte-for-byte against
 * 03_ctrlflow/06_switch.s.golden. Only the single confirmed shape is
 * implemented: `cases` (already collected by OP_SWIT's own handler)
 * covers a DENSE, CONTIGUOUS run of case values with no gaps - a
 * genuinely sparse switch would need a linear compare-chain instead,
 * which no golden confirms, so that's an explicit "not yet supported"
 * rather than a guess. AX already holds the switch's controlling
 * value (via OP_RFORCE - c0_parser.c's parse_switch_stmt() always
 * wraps it in one) at this handler's entry point:
 *   sub ax,#/<min>         normalizes the value to a 0-based index -
 *                          the ONE confirmed immediate rendered in
 *                          HEX (man/mutos_as.1's leading-'/' literal)
 *                          rather than this project's usual decimal
 *                          '*N.'/'#N.' - unexplained but confirmed.
 *   cmp ax,<range>.        range = max-min (ordinary decimal here)
 *   bhi L<deflab>          out of range (unsigned-above) -> default
 *   shl ax,#1               scale the index to a word offset
 *   xchg bx,ax               move it into bx (the indirect-jump's
 *                            index register)
 *   seg cs                   segment-override prefix
 *   jmp @L<table>(bx)         indirect jump through the table
 *   L<table>:L<case0> / L<case1> / ...   the table itself, one
 *                            case-body label per word, ascending by
 *                            case value - confirmed via the exact
 *                            "L<table>:L<first-entry>" no-newline
 *                            join every OP_LABEL elsewhere also uses.
 * `table`'s own label number is confirmed to burn one extra internal
 * label first (06_switch.s.golden's table sits at L10001, not
 * L10000, even though it is the only internal label this file uses)
 * - the reason isn't derivable from this one example, so it's
 * reproduced as an observed constant rather than explained. */
static void gen_switch_dispatch(FILE *out, GenState *g, int deflab,
                                 CaseSwitchEntry *cases, int ncases)
{
    if (ncases == 0)
        gen_fatal("an empty 'switch' is not yet supported");

    /* Insertion sort by value ascending - ncases is always small. */
    for (int i = 1; i < ncases; i++) {
        CaseSwitchEntry key = cases[i];
        int j = i - 1;
        while (j >= 0 && cases[j].val > key.val) {
            cases[j + 1] = cases[j];
            j--;
        }
        cases[j + 1] = key;
    }
    for (int i = 1; i < ncases; i++)
        if (cases[i].val != cases[i - 1].val + 1)
            gen_fatal("a 'switch' whose case values are not a dense, "
                      "contiguous run is not yet supported (no golden "
                      "confirms the sparse/compare-chain shape a real "
                      "compiler would need here)");

    long min = cases[0].val;
    long range = cases[ncases - 1].val - min;

    char buf[32];
    fprintf(out, "sub\tax,#/%lX\n", (unsigned long)((uint16_t)min));
    render_operand(buf, sizeof buf, val_imm(range));
    fprintf(out, "cmp\tax,%s\n", buf);
    fprintf(out, "bhi\tL%d\n", deflab);
    fprintf(out, "shl\tax,#1\n");
    fprintf(out, "xchg\tbx,ax\n");
    fprintf(out, "seg\tcs\n");
    (void)g->next_lab++; /* confirmed-but-unexplained burned label -
                           * see the derivation comment above. */
    int table_lab = g->next_lab++;
    fprintf(out, "jmp\t@L%d(bx)\n", table_lab);
    fprintf(out, "L%d:", table_lab);
    for (int i = 0; i < ncases; i++)
        fprintf(out, "L%d\n", cases[i].lab);
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
            g.setreg_seen = 0; /* one SAVE per function - see
                                 * setreg_seen's own comment. */
            g.di_reserved = 0; /* ditto - see di_reserved's own comment. */
            break;

        case OP_SETREG: {
            g.regvar = c1_read_num(temp1, "temp1");
            /* A function's first SETREG (funchead()'s own,
             * unconditional) renders nothing; every later one (only
             * ever emitted by c0_parser.c when regvar actually
             * changed) renders "|NREG n" - confirmed n = regvar-1
             * against both 04_funcs/06_regclass.s.golden occurrences
             * ("|NREG 2" for regvar=3 when 'i' claims a register,
             * "|NREG 3" for the regvar=4 end-of-function restore) -
             * see setreg_seen's own comment for why position, not
             * value, is what distinguishes the two. */
            if (g.setreg_seen)
                fprintf(out, "|NREG %d\n", g.regvar - 1);
            g.setreg_seen = 1;
            break;
        }

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

        case OP_RNAME: {
            char *name = c1_read_sym(temp1, "temp1");
            int regnum = c1_read_num(temp1, "temp1");
            const char *rname = regvar_name(regnum);
            if (strcmp(rname, "di") == 0)
                g.di_reserved = 1; /* see di_reserved's own comment */
            /* A 'register'-class local's declaration comment -
             * confirmed against 04_funcs/06_regclass.s.golden's
             * "| _i=di\n": unlike ANAME's "| name=offset." (a plain
             * bp-relative number with a trailing period), this
             * renders the actual physical register name, no trailing
             * period - see regvar_name(). */
            fprintf(out, "| %s=%s\n", name, rname);
            free(name);
            break;
        }

        case OP_BSS:
            /* Opens a local STATIC variable's own dedicated BSS
             * block - confirmed against 04_funcs/05_staticvar.s.
             * golden's "L2:.bss\nL4:.blkb\t2.\n.text\n" (see OP_SSPACE/
             * OP_PROG for the rest of that sequence). No arguments of
             * its own on the wire - the LABEL and SSPACE that always
             * immediately follow (see c0_parser.c's
             * parse_static_decl()) carry the block's own label number
             * and size. */
            fprintf(out, ".bss\n");
            break;

        case OP_SSPACE: {
            int size = c1_read_num(temp1, "temp1");
            /* "reserve N bytes" - confirmed against 05_staticvar.s.
             * golden's "L4:.blkb\t2.\n" (a plain int's 2-byte BSS
             * slot); the trailing "." decimal-terminator matches this
             * project's ordinary numeric-immediate convention (see
             * render_operand()). */
            fprintf(out, ".blkb\t%d.\n", size);
            break;
        }

        case OP_SNAME: {
            char *name = c1_read_sym(temp1, "temp1");
            int label = c1_read_num(temp1, "temp1");
            /* A local STATIC variable's declaration comment -
             * confirmed against 05_staticvar.s.golden's "| _n=L4\n":
             * unlike ANAME's "| name=offset." (a plain bp-relative
             * number with a trailing period), this is "| name=L<n>"
             * (the BSS block's own label, prefixed "L", no trailing
             * period) - unsurprising, since a STATIC's own "offset"
             * (see VK_STATIC's comment above) is a label number, not
             * a stack displacement. */
            fprintf(out, "| %s=L%d\n", name, label);
            free(name);
            break;
        }

        case OP_NAME: {
            int hclass = c1_read_num(temp1, "temp1");
            int type   = c1_read_num(temp1, "temp1");
            if (hclass == SC_EXTERN) {
                /* A called function's own name - c0_outcode's 'S'
                 * shape (a symbol name), not the numeric bp-relative
                 * offset every SC_AUTO NAME uses - matches
                 * v7/cc/c04.c's treeout() NAME case's own hclass==
                 * EXTERN branch exactly. `type` is "function
                 * returning X" (X's own type code | 020, the FUNC-
                 * degree bit TY_FUNC_INT already uses for X=TY_INT) -
                 * confirmed for TY_INT (every 04_funcs .1.golden) and,
                 * since 02_long/03_retval.c, TY_LONG (type 22) too;
                 * no other base return type is exercised by this
                 * grammar scope yet. */
                if (type != TY_FUNC_INT && type != (TY_LONG | 020))
                    gen_fatal("NAME with storage class SC_EXTERN and "
                              "type %d not yet supported (only a called "
                              "function's own TY_INT|FUNC or TY_LONG|FUNC "
                              "type is covered so far)", type);
                char *name = c1_read_sym(temp1, "temp1");
                push_val(&g, val_func(name));
                break;
            }
            if (hclass == SC_STATIC) {
                /* A local STATIC variable's own reference - see
                 * VK_STATIC's own comment above. `offset` here is
                 * actually the label number (not read differently on
                 * the wire - still a plain N field, exactly like an
                 * AUTO's numeric offset; only its MEANING differs,
                 * decided entirely by hclass). */
                if (type != TY_INT && type != TY_PTR_INT &&
                    type != TY_CHAR && type != TY_LONG)
                    gen_fatal("NAME of type %d not yet supported (only "
                              "TY_INT/TY_PTR_INT/TY_CHAR/TY_LONG are "
                              "covered so far)", type);
                int label = c1_read_num(temp1, "temp1");
                push_val(&g, val_static(label));
                break;
            }
            if (hclass == SC_REG) {
                /* A 'register'-class local's own reference (04_funcs/
                 * 06_regclass.c) - `offset` here is the register-
                 * allocator's own slot number (RNAME's own comment),
                 * not a stack offset; mapped straight to the physical
                 * register it lives in, so every later use (a
                 * comparison, an arithmetic operand, an ASSIGN target)
                 * goes through the exact same VK_REG machinery
                 * already used for an ordinary intermediate value
                 * sitting in DI - see regvar_name()'s own comment. */
                if (type != TY_INT)
                    gen_fatal("a 'register'-class NAME of type %d is not "
                              "yet supported (only TY_INT is covered so "
                              "far)", type);
                int regnum = c1_read_num(temp1, "temp1");
                push_val(&g, val_reg(regvar_name(regnum)));
                break;
            }
            if (hclass != SC_AUTO)
                gen_fatal("NAME with storage class %d not yet supported "
                          "(only AUTO locals, a local STATIC, a "
                          "'register' local, and a called function's own "
                          "SC_EXTERN name are covered so far)", hclass);
            if (type != TY_INT && type != TY_PTR_INT &&
                type != TY_CHAR && type != TY_LONG && type != TY_PTR_FUNC_INT &&
                type != TY_PTR_PTR_INT)
                gen_fatal("NAME of type %d not yet supported (only "
                          "TY_INT/TY_PTR_INT/TY_CHAR/TY_LONG/"
                          "TY_PTR_FUNC_INT/TY_PTR_PTR_INT are covered "
                          "so far)", type);
            int offset = c1_read_num(temp1, "temp1");
            push_val(&g, val_mem(offset));
            break;
        }

        case OP_CON: {
            int type = c1_read_num(temp1, "temp1");
            int value = c1_read_num(temp1, "temp1");
            if (type != TY_INT && type != TY_UNSIGN)
                gen_fatal("CON of type %d not yet supported (only "
                          "TY_INT/TY_UNSIGN constants are covered so "
                          "far)", type);
            push_val(&g, val_imm(value));
            break;
        }

        case OP_LCON: {
            /* A 'long' constant. Deliberately deferred (unlike every
             * other value-producing opcode here) - pushes a raw
             * VK_LCON(hi,lo) pair with NO code emitted yet; see its
             * ValKind comment above for why (a comparison operand
             * never materializes at all) and materialize_long() for
             * the two confirmed shapes an actual consumer (OP_
             * ASSIGN's long-target case) renders this into. */
            int type = c1_read_num(temp1, "temp1");
            int hi = c1_read_num(temp1, "temp1");
            int lo = c1_read_num(temp1, "temp1");
            if (type != TY_LONG)
                gen_fatal("LCON of type %d not yet supported (only "
                          "TY_LONG is covered so far)", type);
            Val v = {0};
            v.kind = VK_LCON;
            v.imm = lo;
            v.offset = hi;
            push_val(&g, v);
            break;
        }

        case OP_LTOI: {
            /* "(int) l" - long-to-int truncation: reads only the LOW
             * word (base offset + 2, per the confirmed "high word at
             * the lower address" convention - docs/MUTOS_C_ABI.md
             * sect. 1.6), discarding the high word entirely. Pushed
             * as VK_MEM_CVT (see its own ValKind comment above) - a
             * plain memory reference, NOT eagerly loaded into a
             * register - since the correct rendering depends on the
             * consumer: confirmed against 08_castsize.s.golden's "i =
             * (int) l;" ("mov di,*-8.(bp)" then "mov *-6.(bp),di" -
             * OP_ASSIGN's own plain-type case does this materializing,
             * not LTOI itself) and against 02_long/04_params.s.
             * golden's "fd + (int) offset" ("add di,*8.(bp)" - used
             * directly as OP_PLUS's memory operand, no separate move
             * at all). Only a plain memory long (a bare NAME
             * reference) is confirmed as LTOI's own operand - not a
             * long value still sitting in DI:SI (VK_LONG) from a
             * preceding LCON/CTOL, which is not exercised by any
             * golden. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("LTOI to type %d not yet supported (only "
                          "TY_INT is covered so far)", type);
            Val v = pop_val(&g);
            if (v.kind != VK_MEM)
                gen_fatal("LTOI of a non-memory long operand is not yet "
                          "supported");
            Val r = {0};
            r.kind = VK_MEM_CVT;
            r.offset = v.offset + MCC_SZINT;
            push_val(&g, r);
            break;
        }

        case OP_ITOC: {
            /* "(char) i" - int-to-char truncation. Confirmed against
             * 08_castsize.s.golden's "c = (char) i;": loads the int
             * operand into DX (not DI - the general "working
             * register" everywhere else - presumably because the
             * following store needs a byte-addressable register, and
             * OP_ASSIGN's TY_CHAR case below just reuses whatever
             * register this produces via "movb"; DI/SI have no
             * byte-addressable half on the 8086, so this could never
             * have been DI regardless). No explicit "mask off the
             * high byte" instruction - the truncation is implicit in
             * ASSIGN's later "movb" only ever touching DL, the low
             * byte of DX. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_CHAR)
                gen_fatal("ITOC to type %d not yet supported (only "
                          "TY_CHAR is covered so far)", type);
            Val v = materialize(out, &g, pop_val(&g));
            char buf[32];
            render_operand(buf, sizeof buf, v);
            fprintf(out, "mov\tdx,%s\n", buf);
            push_val(&g, val_reg("dx"));
            break;
        }

        case OP_CTOL: {
            /* "(long) c" - char-to-long, sign-extending. Confirmed
             * against 08_castsize.s.golden's "l = (long) c;": the
             * char operand is loaded via "movb" into AX specifically
             * (not DX/CX - CBW/CWD are fixed-register 8086
             * instructions, AL/AX only, so this is a hardware
             * necessity, not a style choice), then CBW sign-extends
             * AL into AX, then CWD sign-extends AX into DX:AX, then
             * the result is moved into the DI(high):SI(low)
             * convention OP_LCON above also produces, for OP_ASSIGN's
             * long-target case to store. Only a plain memory char (a
             * bare NAME reference) is confirmed as CTOL's operand. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_LONG)
                gen_fatal("CTOL to type %d not yet supported (only "
                          "TY_LONG is covered so far)", type);
            Val v = pop_val(&g);
            if (v.kind != VK_MEM)
                gen_fatal("CTOL of a non-memory char operand is not yet "
                          "supported");
            char buf[32];
            render_operand(buf, sizeof buf, v);
            fprintf(out, "movb\tax,%s\n", buf);
            fprintf(out, "cbw\n");
            fprintf(out, "cwd\n");
            fprintf(out, "mov\tdi,dx\n");
            fprintf(out, "mov\tsi,ax\n");
            push_val(&g, val_long());
            break;
        }

        case OP_ITOL: {
            /* Implicit 'int'->'long' widening, inserted by
             * c0_parser.c whenever a plain-int-typed value is
             * assigned to a 'long' lvalue (an int-range constant with
             * no explicit 'L' suffix, e.g. "b = 23456;" where b is
             * 'long') - confirmed against 02_long/01_addsub.s.golden:
             * "mov ax,<v> / cwd / mov di,dx / mov si,ax", the exact
             * same CWD sign-extension idiom materialize_long()'s
             * in-range branch and OP_CTOL above both already use,
             * just starting from a plain int value (here, an
             * immediate) instead of a char or a 'long' constant. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_LONG)
                gen_fatal("ITOL to type %d not yet supported (only "
                          "TY_LONG is covered so far)", type);
            Val v = materialize(out, &g, pop_val(&g));
            char buf[32];
            render_operand(buf, sizeof buf, v);
            fprintf(out, "mov\tax,%s\n", buf);
            fprintf(out, "cwd\n");
            fprintf(out, "mov\tdi,dx\n");
            fprintf(out, "mov\tsi,ax\n");
            push_val(&g, val_long());
            break;
        }

        case OP_PLUS:
        case OP_MINUS: {
            int type = c1_read_num(temp1, "temp1");
            if (op == OP_PLUS && type == TY_PTR_INT) {
                /* Pointer + scaled-index arithmetic - a single
                 * subscript step ("a[i]", "m[i][j]"'s own inner
                 * step) or an explicit "*(a + i)" - confirmed against
                 * 01_arrbasic.s.golden ("lea di,&a; ...; add di,si" -
                 * the base address, from a preceding OP_AMPER, stays
                 * in DI, and the scaled index, from a preceding
                 * OP_ITOP, is added in from SI - see OP_ITOP's own
                 * comment) and 04_ptrarreq.s.golden ("mov di,i; sal
                 * di,*1; add di,*4.(bp)" - here the scaled index
                 * itself ends up in DI, since the pointer operand is
                 * a plain parameter with nothing to eagerly load, and
                 * is added in directly as a memory operand). Either
                 * operand may already be resident in a register (DI
                 * from OP_AMPER, or DI/SI from OP_ITOP); whichever
                 * one is stays as the destination, and the OTHER
                 * operand is added in via its own rendered operand
                 * text - a plain memory or immediate right-hand side
                 * is legal for ADD directly on the 8086, no separate
                 * load needed. */
                Val r = pop_val(&g);
                Val l = pop_val(&g);
                /* A compile-time-constant-index subscript ("v[0] =
                 * 1;" - see VK_MEM_DIRECT's own comment): the base
                 * address (from OP_AMPER, deferred) combines with the
                 * fully-folded scaled index (from OP_ITOP, always a
                 * VK_IMM when both its own operands were constant)
                 * into a single new bp-relative address, with NO
                 * "lea"/"add" instruction at all - confirmed against
                 * 04_ptrarreq.s.golden's "v[0] = 1;" -> a lone
                 * "mov\t*-12.(bp),*1.". */
                if (l.kind == VK_MEM_DIRECT && r.kind == VK_IMM) {
                    push_val(&g, val_mem_direct(l.offset + (int)r.imm));
                    break;
                }
                if (r.kind == VK_MEM_DIRECT && l.kind == VK_IMM) {
                    push_val(&g, val_mem_direct(r.offset + (int)l.imm));
                    break;
                }
                /* The index turned out NOT to be a compile-time
                 * constant after all (a VK_MEM_DIRECT base paired
                 * with a genuinely runtime-scaled index, from a
                 * mixed subscript this grammar scope does not
                 * exercise - AMPER's own one-opcode CON-peek already
                 * rules this out for every confirmed construct, but
                 * this materializes the deferred "lea" correctly
                 * rather than silently mishandling it if it ever
                 * does happen) - fall through to the ordinary
                 * register-based path below with the address
                 * finally committed to DI. */
                if (l.kind == VK_MEM_DIRECT) {
                    char lb[32];
                    render_operand(lb, sizeof lb, l);
                    fprintf(out, "lea\tdi,%s\n", lb);
                    l = val_reg("di");
                }
                if (r.kind == VK_MEM_DIRECT) {
                    char rb[32];
                    render_operand(rb, sizeof rb, r);
                    fprintf(out, "lea\tdi,%s\n", rb);
                    r = val_reg("di");
                }
                const char *dst;
                Val other;
                if (l.kind == VK_REG && strcmp(l.reg, "di") == 0) {
                    dst = "di"; other = r;
                } else if (r.kind == VK_REG && strcmp(r.reg, "di") == 0) {
                    dst = "di"; other = l;
                } else if (l.kind == VK_REG) {
                    dst = l.reg; other = r;
                } else if (r.kind == VK_REG) {
                    dst = r.reg; other = l;
                } else {
                    load_into_di(out, l);
                    dst = "di"; other = r;
                }
                char obuf[32];
                render_operand(obuf, sizeof obuf, other);
                fprintf(out, "add\t%s,%s\n", dst, obuf);
                push_val(&g, val_reg(dst));
                break;
            }
            if (type == TY_LONG) {
                /* 32-bit add/subtract - textbook 8086 idiom (ADD/SUB
                 * the low words, then ADC/SBB the high words using
                 * the resulting carry/borrow), but the confirmed
                 * shape genuinely differs by what the right operand
                 * is:
                 *
                 * - A plain memory (NAME) right operand: the LEFT
                 *   operand is loaded into DI(high):SI(low), then
                 *   ADD/SUB/ADC/SBB read the right operand straight
                 *   out of memory - confirmed against "c = a + b;"
                 *   ("mov si,*-6.(bp) / mov di,*-8.(bp) / add si,
                 *   *-10.(bp) / adc di,*-12.(bp)") and "c = a - b;"
                 *   (identical shape, sub/sbb).
                 * - An in-range 'long' CONSTANT right operand (VK_
                 *   LCON, sign-extension-shaped - see materialize_
                 *   long()): confirmed against "c = c + 1L;", a
                 *   genuinely different, more roundabout shape - the
                 *   constant is sign-extended into DX:AX, pushed
                 *   (low then high) to get it off of DX:AX, THEN the
                 *   left operand is loaded into SI:DI, then the
                 *   pushed constant is popped back into BX(high):
                 *   CX(low) (freeing DX:AX first is presumably why -
                 *   materializing the left operand via CWD in the
                 *   general case, per materialize_long(), would
                 *   clobber DX:AX before it's used here), and ADD/
                 *   ADC (or SUB/SBB) combine SI:DI with CX:BX. The
                 *   second `pop` is confirmed to render as "pop cx"
                 *   with a literal space, not a tab, unlike every
                 *   other instruction here - a genuine real-hardware
                 *   asymmetry, not a transcription slip. Neither a
                 *   genuinely 32-bit constant operand (direct-split
                 *   shaped) nor a left operand that is itself
                 *   anything but a plain memory reference is
                 *   confirmed by any golden. */
                Val r = pop_val(&g);
                Val l = pop_val(&g);
                if (l.kind != VK_MEM)
                    gen_fatal("'long' %s with a non-memory left operand "
                              "is not yet supported",
                              op == OP_PLUS ? "addition" : "subtraction");
                char buf[32];
                if (r.kind == VK_MEM) {
                    render_operand(buf, sizeof buf, val_mem(l.offset + MCC_SZINT));
                    fprintf(out, "mov\tsi,%s\n", buf);
                    render_operand(buf, sizeof buf, val_mem(l.offset));
                    fprintf(out, "mov\tdi,%s\n", buf);
                    render_operand(buf, sizeof buf, val_mem(r.offset + MCC_SZINT));
                    fprintf(out, "%s\tsi,%s\n", op == OP_PLUS ? "add" : "sub", buf);
                    render_operand(buf, sizeof buf, val_mem(r.offset));
                    fprintf(out, "%s\tdi,%s\n", op == OP_PLUS ? "adc" : "sbb", buf);
                } else if (r.kind == VK_LCON && r.offset == (r.imm < 0 ? -1 : 0)) {
                    render_operand(buf, sizeof buf, val_imm(r.imm));
                    fprintf(out, "mov\tax,%s\n", buf);
                    fprintf(out, "cwd\n");
                    fprintf(out, "push\tax\n");
                    fprintf(out, "push\tdx\n");
                    render_operand(buf, sizeof buf, val_mem(l.offset + MCC_SZINT));
                    fprintf(out, "mov\tsi,%s\n", buf);
                    render_operand(buf, sizeof buf, val_mem(l.offset));
                    fprintf(out, "mov\tdi,%s\n", buf);
                    fprintf(out, "pop\tbx\n");
                    fprintf(out, "pop cx\n"); /* confirmed literal
                                               * space, not a tab -
                                               * see comment above. */
                    fprintf(out, "%s\tsi,cx\n", op == OP_PLUS ? "add" : "sub");
                    fprintf(out, "%s\tdi,bx\n", op == OP_PLUS ? "adc" : "sbb");
                } else {
                    gen_fatal("'long' %s with this right-operand shape "
                              "is not yet supported",
                              op == OP_PLUS ? "addition" : "subtraction");
                }
                push_val(&g, val_long());
                break;
            }
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported",
                          op == OP_PLUS ? "PLUS" : "MINUS", type);
            Val r = materialize(out, &g, pop_val(&g));
            Val l = materialize(out, &g, pop_val(&g));
            if (op == OP_PLUS && (r.kind == VK_IND || l.kind == VK_IND)) {
                /* One operand is itself a dereferenced pointer
                 * ("sum = sum + a[i];" - 05_arrptr/01_arrbasic.c, or
                 * "s = s + *(a + i);" - 04_ptrarreq.c) - its lazy
                 * "(reg)" form can't just be combined via a single
                 * add (an immediately-following OP_STAR always
                 * leaves the ADDRESS, not yet the value, in that
                 * register - see OP_STAR's own comment), so it is
                 * materialized in place first ("mov reg,(reg)" -
                 * confirmed against 01_arrbasic.s.golden's "mov
                 * di,(di)" and 04_ptrarreq.s.golden's identical
                 * step), then the OTHER operand is added straight in
                 * via its own rendered text - a plain memory or
                 * immediate right-hand side is legal for ADD
                 * directly, no separate load needed. Deliberately
                 * separate from the register-CLASS-variable special
                 * case just below (which also ends up with a VK_REG
                 * "di" operand, but must NOT take this same "add
                 * straight into di" shape - see that case's own
                 * comment) - this branch always fires first, and
                 * always `break`s, so the two never interact. PLUS
                 * only (commutative) - not exercised, and not
                 * generalized, for MINUS, where operand order would
                 * matter and no golden confirms the shape. */
                Val ind = (r.kind == VK_IND) ? r : l;
                Val other = (r.kind == VK_IND) ? l : r;
                fprintf(out, "mov\t%s,(%s)\n", ind.reg, ind.reg);
                char obuf[32];
                render_operand(obuf, sizeof obuf, other);
                if (other.kind == VK_IMM && other.imm == 1)
                    fprintf(out, "inc\t%s\n", ind.reg);
                else
                    fprintf(out, "add\t%s,%s\n", ind.reg, obuf);
                push_val(&g, val_reg(ind.reg));
                break;
            }
            if (r.kind == VK_REG && strcmp(r.reg, "di") == 0 &&
                !(l.kind == VK_REG && strcmp(l.reg, "di") == 0)) {
                /* The right operand already lives in DI (a
                 * 'register'-class local - see OP_NAME's SC_REG
                 * case) and the left doesn't - loading the left into
                 * DI as usual (below) would clobber the right operand
                 * before it's even used, so the left goes into SI
                 * instead and the operation becomes "add/sub si,di" -
                 * confirmed against 04_funcs/06_regclass.s.golden's
                 * "sum = sum + i;" -> "mov si,*-6.(bp) / add si,di".
                 * When the LEFT operand is instead the register
                 * variable (e.g. "i + 1"), it already falls through
                 * to the ordinary path below unchanged - load_into_di()
                 * is a no-op for an operand already sitting in DI, so
                 * no separate case is needed for that shape. */
                char buf[32];
                render_operand(buf, sizeof buf, l);
                fprintf(out, "mov\tsi,%s\n", buf);
                fprintf(out, "%s\tsi,di\n", op == OP_PLUS ? "add" : "sub");
                push_val(&g, val_reg("si"));
                break;
            }
            load_into_di(out, l);
            if (r.kind == VK_IMM && r.imm == 1 && op == OP_PLUS) {
                /* "+ 1" specifically compiles to a plain INC, not
                 * "add di,*1." - confirmed against 07_ternary.s.golden's
                 * "a = a + 1;" -> "inc\tdi" (never an "add"). */
                fprintf(out, "inc\tdi\n");
            } else if (r.kind == VK_IMM && r.imm == 1 && op == OP_MINUS) {
                /* The symmetric "- 1" -> DEC case, now confirmed
                 * against 04_funcs/03_recfact.s.golden's "n - 1" ->
                 * "dec\tdi" (never a "sub") and 04_mutrec.s.golden's
                 * identical "n - 1" in both isodd()/iseven(). */
                fprintf(out, "dec\tdi\n");
            } else {
                char rbuf[32];
                render_operand(rbuf, sizeof rbuf, r);
                fprintf(out, "%s\tdi,%s\n", op == OP_PLUS ? "add" : "sub", rbuf);
            }
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
            /* Address-of. Two confirmed shapes, entirely different
             * codegen: */
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_PTR_FUNC_INT) {
                /* A bare function name used as a value ("fp =
                 * square;" - c0_parser.c's parse_primary() T_IDENT
                 * fallback): its operand is always VK_FUNC (an
                 * OP_NAME with SC_EXTERN/TY_FUNC_INT just emitted it -
                 * see OP_NAME's own handler above). Produces
                 * VK_FUNCADDR, NOT a "lea" - see its own ValKind
                 * comment above for why no code is emitted here at
                 * all. */
                Val v = pop_val(&g);
                if (v.kind != VK_FUNC)
                    gen_fatal("'&' on a non-function operand is not yet "
                              "supported for a TY_PTR_FUNC_INT result");
                Val r = {0};
                r.kind = VK_FUNCADDR;
                r.reg = v.reg; /* ownership transferred - see VK_FUNCADDR's comment */
                push_val(&g, r);
                break;
            }
            /* Array-to-pointer decay ("p = a;" - c0_parser.c's
             * parse_primary()) and general address-of a plain
             * variable ("p = &x;"/"pp = &p;" - parse_unary()'s '&'
             * case): the operand is always a plain bp-relative NAME,
             * rendered as a real "lea" - confirmed against
             * 05_incdec.s.golden's "lea\tdi,*-16.(bp)" and
             * 06_ptrptr.s.golden's "lea\tdi,*-6.(bp)"/"lea\tdi,
             * *-8.(bp)" (the latter for "pp = &p;", type 40 - an
             * ordinary lea either way; only the wire TYPE argument
             * differs by degree, never the codegen shape itself). */
            if (type != TY_PTR_INT && type != TY_PTR_PTR_INT)
                gen_fatal("AMPER of type %d not yet supported (only "
                          "TY_PTR_INT, TY_PTR_PTR_INT and "
                          "TY_PTR_FUNC_INT are covered so far)", type);
            Val v = pop_val(&g);
            if (v.kind != VK_MEM)
                gen_fatal("'&' on a non-memory operand is not yet supported");
            /* If the immediately following wire opcode is OP_NAME,
             * this AMPER is the base of a subscript with a genuinely
             * RUNTIME index ("a[i]" - emit_subscript()'s own
             * NAME/AMPER/NAME/CON/ITOP/PLUS/STAR shape in
             * c0_parser.c) - the "lea" is emitted right here, eagerly,
             * matching 01_arrbasic.s.golden's own instruction order
             * exactly ("lea\tdi,*-14.(bp)" BEFORE the index is loaded
             * and scaled - contrast a deferred materialization, which
             * would land it after, since OP_PLUS's own fold/
             * materialize step only runs once the index side is
             * already done). One opcode of lookahead, via the same
             * save/restore-file-position technique OP_STAR's own
             * VK_IND_PENDING peek uses - see its comment for why this
             * is safe. For every other immediately-following opcode
             * (OP_CON - a compile-time-constant index, "v[0] = 1;",
             * or the start of some other construct entirely, e.g. a
             * bare "p = &x;"/"pp = &p;" assignment or "sumarr(v, 4)"
             * call argument), the "lea" is instead DEFERRED (as a
             * VK_MEM_DIRECT - see its own comment) - see OP_PLUS,
             * OP_ASSIGN and gen_call()'s own matching comments for
             * why each of those needs this. */
            long amper_savepos = ftell(temp1);
            int amper_next = c1_read_op(temp1, "temp1");
            fseek(temp1, amper_savepos, SEEK_SET);
            if (amper_next != OP_NAME) {
                push_val(&g, val_mem_direct(v.offset));
                break;
            }
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
            if (size.kind != VK_IMM)
                gen_fatal("ITOP with a non-constant scale factor is "
                          "not yet supported");
            if (amt.kind == VK_IMM) {
                push_val(&g, val_imm(amt.imm * size.imm));
                break;
            }
            /* A non-constant index ("a[i]"/"*(a + i)" -
             * 05_arrptr/01_arrbasic.c and 04_ptrarreq.c) - scaled via
             * repeated left-shift, matching the *=2-style strength
             * reduction already established for 06_compasgn (plain
             * 8086 has no shift-by-immediate-count opcode - see
             * OP_LSHIFT/RSHIFT's own comment); only a power-of-two
             * size is supported, which every size this grammar scope
             * produces (1/2/4 - char/int-or-pointer/long) is. The
             * destination register is DI, UNLESS DI is already
             * occupied by a value still needed afterward - a pointer
             * address from a preceding OP_AMPER, still sitting on the
             * value stack right below this ITOP's own two operands -
             * in which case SI is used instead, to avoid clobbering
             * it. Confirmed against 01_arrbasic.s.golden's "lea
             * di,*-14.(bp)" (DI now occupied) / "mov si,*-16.(bp)" /
             * "sal si,*1" vs. 04_ptrarreq.s.golden's "mov di,*-6.
             * (bp)" / "sal di,*1" (DI free - the pointer operand `a`
             * is a plain parameter added in later, straight from
             * memory, by OP_PLUS below, never loaded into DI itself
             * beforehand). */
            int use_si = (g.valsp >= 1 &&
                          ((g.valstack[g.valsp - 1].kind == VK_REG &&
                            strcmp(g.valstack[g.valsp - 1].reg, "di") == 0) ||
                           g.valstack[g.valsp - 1].kind == VK_MEM_DIRECT));
            const char *reg = use_si ? "si" : "di";
            if (use_si)
                load_into_si(out, amt);
            else
                load_into_di(out, amt);
            long sz = size.imm;
            if (sz != 1 && sz != 2 && sz != 4)
                gen_fatal("ITOP scaling by a non-power-of-two size "
                          "(%ld) is not yet supported", sz);
            char onebuf[32];
            render_bare_imm(onebuf, sizeof onebuf, 1);
            for (long s = sz; s > 1; s /= 2)
                fprintf(out, "sal\t%s,%s\n", reg, onebuf);
            push_val(&g, val_reg(reg));
            break;
        }

        case OP_STAR: {
            /* Pointer dereference - two confirmed shapes: */
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_FUNC_INT) {
                /* Dereferencing a function-pointer VARIABLE as a call
                 * callee ("(*f)(x)" - c0_parser.c's
                 * parse_indirect_call()) - a pure TYPE-level
                 * operation here: NO code is emitted, and the operand
                 * (always VK_MEM - a plain parameter/local reference)
                 * is left completely unchanged on the stack.
                 * gen_call() is the actual consumer, rendering it as
                 * an indirect "call @<mem>" - confirmed against
                 * 07_funcptr.s.golden's "return (*f)(x);" -> "call
                 * @*4.(bp)" with no preceding "mov"/"lea" of any kind
                 * for `f` itself (contrast the ordinary TY_INT
                 * dereference case below, which DOES eagerly load
                 * into DI - a real runtime indirection, unlike this
                 * purely-static "which function to call" case). */
                if (g.valsp < 1 || g.valstack[g.valsp - 1].kind != VK_MEM)
                    gen_fatal("'(*f)(...)' is only supported when `f` is "
                              "a plain function-pointer variable - see "
                              "src/mutos_cc/README.md");
                break;
            }
            /* Ordinary pointer dereference - loads the pointer value
             * into DI (a no-op if it is already there, e.g. straight
             * off a preceding INCAFT/INCBEF, a preceding OP_AMPER/
             * OP_PLUS pointer-arithmetic result, or - for a chained
             * "**pp" - the PRIOR OP_STAR's own VK_IND result, whose
             * "(di)" text load_into_di() renders and reloads exactly
             * like any other operand - see load_into_di()) and
             * produces an indirect "(di)" operand. The dereferenced
             * type is TY_INT for a plain "int *"/array element
             * (confirmed against every STAR node in 05_incdec.1.
             * golden and 05_arrptr/01_arrbasic.1.golden), or
             * TY_PTR_INT for the FIRST of a chained pair of STARs on
             * a pointer-to-pointer ("**pp" - confirmed against
             * 06_ptrptr.1.golden: the first STAR is type 8, the
             * second - dereferencing what the first produced - is
             * type 0). */
            if (type != TY_INT && type != TY_PTR_INT)
                gen_fatal("STAR of type %d not yet supported (only "
                          "TY_INT, TY_PTR_INT and TY_FUNC_INT are "
                          "covered so far)", type);
            Val ptr = pop_val(&g);
            if (ptr.kind == VK_MEM_DIRECT) {
                /* Dereferencing a compile-time-fully-known address
                 * ("v[0]" - see VK_MEM_DIRECT's own comment) is just
                 * that memory location itself - no load, no
                 * indirection, not even the deferred-address-spill
                 * logic below (nothing was ever computed into a
                 * register in the first place, so there is nothing
                 * to protect from the right-hand side's own
                 * register use). */
                push_val(&g, ptr);
                break;
            }
            /* An indirect-assignment TARGET whose right-hand side is
             * about to need working registers of its own ("a[i] = i
             * * i;", "*p = *p + 1;" - see VK_IND_PENDING's own
             * comment) must not leave its address sitting in DI
             * across that computation - its ORIGINAL operand (`ptr`
             * above - already a plain memory operand or an address
             * already resident in some register, never yet loaded
             * into DI for this purpose) is pushed to the real machine
             * stack directly instead, popped back only once the
             * right-hand side is fully evaluated - confirmed against
             * 03_ptrbasic.s.golden's "*p = *p + 1;": "push\t*-10.
             * (bp)" (the plain pointer VARIABLE's own memory operand,
             * pushed WITHOUT first loading it into DI at all - unlike
             * 01_arrbasic.s.golden's "a[i] = i * i;", where `ptr` is
             * already "di" from the preceding OP_PLUS, so this is
             * simply "push\tdi"). Recognizing when this is needed
             * takes one opcode of lookahead beyond what this dispatch
             * loop otherwise uses: peek (save/restore temp1's own
             * file position - c1_read_op()/c1_read_num() are plain
             * getc() calls, so this is an ordinary seekable FILE*,
             * and the wire bytes read this way are read AGAIN
             * normally afterward, so this changes no other opcode's
             * behavior) whether the immediately following wire is
             * exactly CON then ASSIGN (a bare constant right-hand
             * side with nothing else - "*p = 20;"/"*p++ = 1;" -
             * confirmed by 05_incdec.s.golden to NOT push). Only
             * attempted for a FINAL (TY_INT) dereference - not the
             * first of a chained "**pp" (TY_PTR_INT - an intermediate
             * step that is always immediately re-dereferenced, never
             * itself an assignment target) - and only when nothing
             * else is currently pending on the value stack (g.valsp
             * == 0 right here, i.e. this STAR is the outermost/first
             * construct of its statement - the shape every star-
             * assign-stmt/subscript-assign lvalue tree has - never
             * for a STAR buried inside a larger expression, e.g. the
             * rhs "*p" of "*p = *p + 1;", which reaches here with
             * something already pending below and so skips this
             * entirely, falling through to the ordinary VK_IND
             * shape). */
            if (type == TY_INT && g.valsp == 0) {
                long savepos = ftell(temp1);
                int op1 = c1_read_op(temp1, "temp1");
                int trivial = 0;
                if (op1 == OP_CON) {
                    (void)c1_read_num(temp1, "temp1"); /* CON's type */
                    (void)c1_read_num(temp1, "temp1"); /* CON's value */
                    int op2 = c1_read_op(temp1, "temp1");
                    if (op2 == OP_ASSIGN)
                        trivial = 1;
                }
                fseek(temp1, savepos, SEEK_SET);
                if (!trivial) {
                    char pbuf[32];
                    render_operand(pbuf, sizeof pbuf, ptr);
                    fprintf(out, "push\t%s\n", pbuf);
                    push_val(&g, val_ind_pending());
                    break;
                }
            }
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
            /* A 'long' comparison - NOT signaled by `type` above
             * (confirmed always TY_INT here regardless of operand
             * type: a comparison's own RESULT is always plain int,
             * per ordinary C semantics - see 02_long/01_addsub.1.
             * golden's "c > 0L", whose GREAT node carries type 0).
             * Detected instead from the operands themselves: either
             * side being VK_LCON (an unmaterialized 'long' constant -
             * see OP_LCON above) or VK_LONG (an already-materialized
             * one) means this is a 'long' comparison, needing
             * gen_long_cmp()'s genuinely different codegen rather than
             * emit_cmp_and_branch()'s ordinary 16-bit shape. Only
             * OP_CBRANCH is confirmed to consume one - see its own
             * comment below. */
            if (l.kind == VK_LCON || l.kind == VK_LONG ||
                r.kind == VK_LCON || r.kind == VK_LONG)
                c.cond_is_long = 1;
            push_val(&g, c);
            break;
        }

        case OP_CBRANCH: {
            /* "Branch to lbl if the tree's value is TRUE together
             * with cond" - matches v7/cc/c04.c's cbranch(t,lbl,cond)
             * exactly (see docs/DEVLOG.md): cond=1 means branch-if-
             * true (the condition's own comparison mnemonic, e.g.
             * "blt" for a bare OP_LESS), cond=0 means branch-if-false
             * (the INVERTED mnemonic, e.g. "bge") - confirmed against
             * 03_ctrlflow/01_ifelse.s.golden's "if (a > 0)" using
             * cond=0 (skip the true-branch on false) and 07_goto.
             * s.golden's "if (i >= 10) goto done;" using cond=1 (a
             * direct branch-if-true to the goto's own target, c0's
             * "simpif" shortcut - see c0_parser.c). Reuses
             * gen_logand()/gen_logor()'s own `as_cond()` wrapper so a
             * non-comparison condition (not exercised by any current
             * golden, but the same zero-risk "any nonzero value is
             * true" generalization those two already rely on) is
             * handled uniformly. */
            int lbl = c1_read_num(temp1, "temp1");
            int cond_sense = c1_read_num(temp1, "temp1");
            (void)c1_read_num(temp1, "temp1"); /* source line - not
                                                 * rendered into the
                                                 * .s output, same as
                                                 * OP_EXPR's. */
            Val v = pop_val(&g);
            Val c = as_cond(v);
            if (c.cond_is_long) {
                gen_long_cmp(out, &g, c.true_op, c.cl, c.cr, cond_sense, lbl);
                break;
            }
            int branch_op_code = cond_sense ? c.true_op : cond_invert(c.true_op);
            emit_cmp_and_branch(out, c, branch_op_code, lbl);
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

        case OP_COLON: {
            /* Packages the two "?:" branch values for the
             * immediately following OP_QUEST - emits no code of its
             * own; each branch's own codegen has to live inside
             * QUEST's conditional branches, not run unconditionally
             * here (see gen_quest() above) - confirmed against
             * 07_ternary.s.golden, which has no instruction
             * corresponding to COLON itself. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("COLON of type %d not yet supported", type);
            Val f = pop_val(&g);
            Val t = pop_val(&g);
            if (t.kind == VK_COND || f.kind == VK_COND)
                gen_fatal("a '?:' branch that is itself a bare "
                          "relational comparison is not yet supported");
            Val pair = {0};
            pair.kind = VK_PAIR;
            pair.cl = simple_of(t);
            pair.cr = simple_of(f);
            push_val(&g, pair);
            break;
        }

        case OP_QUEST: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("QUEST of type %d not yet supported", type);
            Val pair = pop_val(&g);
            Val cond = pop_val(&g);
            if (pair.kind != VK_PAIR)
                gen_fatal("internal: QUEST without a preceding COLON pair");
            push_val(&g, gen_quest(out, &g, cond, pair));
            break;
        }

        case OP_SEQNC: {
            /* The comma operator: the left operand's side effects (if
             * any) were already emitted by whichever opcode produced
             * it - its value is simply discarded here, unmaterialized,
             * exactly like any other unused value in this grammar
             * scope (see OP_EXPR's own discard below) - confirmed
             * against 07_ternary.s.golden's "(a = a + 1, b = b + 1,
             * a + b)", where no instruction at all corresponds to
             * either SEQNC node. Only reachable via
             * c0_parser.c's parse_comma_item(), which restricts the
             * left operand to something that was already fully
             * emitted (a plain expression or an embedded assignment -
             * never a still-unmaterialized compile-time constant),
             * so there is never anything here that needs
             * materializing before being dropped. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("SEQNC of type %d not yet supported", type);
            Val rhs = pop_val(&g);
            (void)pop_val(&g); /* lhs - discarded, never materialized */
            push_val(&g, rhs);
            break;
        }

        case OP_COMMA: {
            /* An OP_CALL argument-list separator (NOT the comma
             * operator - that is the entirely distinct OP_SEQNC just
             * above). Builds a VK_ARGLIST left-associatively, exactly
             * mirroring how c0_parser.c's parse_call() built it -
             * confirmed against 02_manyargs.1.golden's six-argument,
             * five-COMMA chain. No code is emitted here - a call
             * argument's own code (if any) was already emitted by
             * whichever opcode produced its Val; OP_COMMA only
             * relocates already-resolved Vals into the growing list. */
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("COMMA of type %d not yet supported", type);
            Val rhs = pop_val(&g);
            Val lhs = pop_val(&g);
            Val out_v = {0};
            out_v.kind = VK_ARGLIST;
            if (lhs.kind == VK_ARGLIST) {
                out_v.arglist = lhs.arglist;
            } else {
                out_v.arglist = malloc(sizeof *out_v.arglist);
                if (!out_v.arglist)
                    gen_fatal("out of memory building a call argument list");
                out_v.arglist->n = 0;
                out_v.arglist->items[out_v.arglist->n++] = lhs;
            }
            if (out_v.arglist->n >= MCC_MAXCALLARGS)
                gen_fatal("too many call arguments (internal limit %d)",
                          MCC_MAXCALLARGS);
            out_v.arglist->items[out_v.arglist->n++] = rhs;
            push_val(&g, out_v);
            break;
        }

        case OP_NULLOP: {
            /* A zero-argument OP_CALL's argument tree - v7/cc/c04.c's
             * treeout(NULL) shape (outcode("B", NULLOP) for a null
             * subtree). Pushes an empty VK_ARGLIST so gen_call() can
             * treat "zero arguments" uniformly with "2+ arguments"
             * (see its own comment) rather than needing a third,
             * separate shape. */
            Val v = {0};
            v.kind = VK_ARGLIST;
            v.arglist = malloc(sizeof *v.arglist);
            if (!v.arglist)
                gen_fatal("out of memory building a call argument list");
            v.arglist->n = 0;
            push_val(&g, v);
            break;
        }

        case OP_CALL: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT && type != TY_LONG)
                gen_fatal("a call returning type %d is not yet supported "
                          "(only an int- or long-returning function is "
                          "covered so far)", type);
            Val args = pop_val(&g);
            Val callee = pop_val(&g);
            push_val(&g, gen_call(out, callee, args, type == TY_LONG));
            break;
        }

        case OP_TIMES: {
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_LONG) {
                Val r = pop_val(&g);
                Val l = pop_val(&g);
                push_val(&g, gen_long_binop_call(out, l, r, "lmul"));
                break;
            }
            if (type != TY_INT)
                gen_fatal("TIMES of type %d not yet supported", type);
            Val r = materialize(out, &g, pop_val(&g));
            Val l = materialize(out, &g, pop_val(&g));
            /* Which operand becomes the "mov ax,<X>" side and which
             * becomes the IMUL operand: ordinarily the LEFT operand
             * goes into AX and the RIGHT is IMUL'd (every previously-
             * confirmed case, where neither operand starts out
             * already living in a register). But when one operand is
             * already sitting in AX - so far only an OP_CALL result
             * (sect. 1.5's return-value register) - the real compiler
             * evidently keeps it there instead of following source
             * position: confirmed against 03_recfact.s.golden's
             * "return n * fact(n - 1);" (source-LEFT is "n", source-
             * RIGHT is the call) rendering as "mov ax,ax" (the
             * call's own result, a redundant self-move, matching this
             * project's no-peephole-optimization ethos exactly - see
             * OP_TIMES's own "mov ax,ax" precedent elsewhere in this
             * codebase) then "imul *4.(bp)" (n) - i.e. the AX-resident
             * operand keeps AX and the OTHER operand is IMUL'd,
             * regardless of which was source-left/-right. Neither
             * operand is ever already in AX in this grammar scope
             * except via a preceding OP_CALL, so this reduces to the
             * original "left into ax" shape whenever it applies. */
            Val ax_side = l, imul_side = r;
            if (r.kind == VK_REG && strcmp(r.reg, "ax") == 0) {
                ax_side = r;
                imul_side = l;
            }
            if (imul_side.kind == VK_IMM)
                gen_fatal("multiplying by an immediate is not yet "
                          "supported (8086 IMUL takes a reg/mem operand, "
                          "never an immediate directly - no golden "
                          "reference confirms the alternate sequence a "
                          "real compiler would need here)");
            char lbuf[32], rbuf[32];
            render_operand(lbuf, sizeof lbuf, ax_side);
            render_operand(rbuf, sizeof rbuf, imul_side);
            fprintf(out, "mov\tax,%s\nimul\t%s\n", lbuf, rbuf);
            push_val(&g, val_reg("ax"));
            break;
        }

        case OP_DIVIDE:
        case OP_MOD: {
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_LONG) {
                /* lrem/ldiv both return their result in DX:AX -
                 * confirmed via 02_muldiv.s.golden's "c = a %% b;"
                 * using the exact same call/add-sp/mov-di,dx/mov-si,ax
                 * shape as "c = a / b;", just calling "lrem" instead
                 * of "ldiv" - unlike the plain-int DIVIDE/MOD split
                 * below (quotient in AX, remainder in DX, no helper
                 * call), there is only one long helper per operator,
                 * not a shared call whose result register differs. */
                Val r = pop_val(&g);
                Val l = pop_val(&g);
                const char *helper = (op == OP_DIVIDE) ? "ldiv" : "lrem";
                push_val(&g, gen_long_binop_call(out, l, r, helper));
                break;
            }
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
            if (type == TY_LONG) {
                /* Confirmed against 08_castsize.s.golden's "l =
                 * 70000;" and "l = (long) c;" - both store LOW
                 * (offset+MCC_SZINT) before HIGH (base offset),
                 * moving si then di, matching every confirmed
                 * long-producing opcode's DI(high):SI(low) convention
                 * (see OP_LCON/OP_CTOL above). A fundamentally
                 * different two-instruction shape from every other
                 * ASSIGN case below, so it is handled first and
                 * separately rather than folded into the single
                 * mov/movb path. */
                Val rhs = pop_val(&g);
                Val lhs = pop_val(&g);
                if (lhs.kind != VK_MEM)
                    gen_fatal("assignment to a non-memory 'long' lvalue "
                              "is not yet supported");
                rhs = materialize_long(out, rhs); /* VK_LCON -> VK_LONG,
                                                    * a no-op for
                                                    * anything already
                                                    * VK_LONG (OP_CTOL,
                                                    * OP_ITOL, a
                                                    * runtime-helper
                                                    * call) - see
                                                    * materialize_long()
                                                    * above. */
                if (rhs.kind != VK_LONG)
                    gen_fatal("assigning a non-'long' value to a 'long' "
                              "lvalue is not yet supported (no golden "
                              "reference confirms the widening sequence "
                              "a real compiler would need here)");
                char lobuf[32], hibuf[32];
                render_operand(lobuf, sizeof lobuf, val_mem(lhs.offset + MCC_SZINT));
                render_operand(hibuf, sizeof hibuf, val_mem(lhs.offset));
                fprintf(out, "mov\t%s,si\n", lobuf);
                fprintf(out, "mov\t%s,di\n", hibuf);
                push_val(&g, rhs);
                break;
            }
            if (type != TY_INT && type != TY_PTR_INT && type != TY_CHAR &&
                type != TY_PTR_FUNC_INT && type != TY_PTR_PTR_INT)
                gen_fatal("ASSIGN of type %d not yet supported", type);
            Val rhs = materialize(out, &g, pop_val(&g));
            if (rhs.kind == VK_MEM_DIRECT) {
                /* A deferred address-of, finally materialized here -
                 * "p = &x;"/"p = a;"/"pp = &p;" (see OP_AMPER's own
                 * comment for why this is deferred at all) - confirmed
                 * against 05_incdec.s.golden's "lea\tdi,*-16.(bp)" and
                 * 06_ptrptr.s.golden's two "lea"s. */
                char rb[32];
                render_operand(rb, sizeof rb, rhs);
                fprintf(out, "lea\tdi,%s\n", rb);
                rhs = val_reg("di");
            }
            Val lhs = pop_val(&g);
            if (lhs.kind == VK_IND_PENDING) {
                /* The lvalue's own address was pushed to the real
                 * machine stack earlier (see OP_STAR's own comment
                 * on VK_IND_PENDING) while the right-hand side above
                 * used the registers instead - retrieved now, via BX
                 * (never DI/SI, both of which may still hold live
                 * pieces of the just-computed rhs) - confirmed
                 * against 01_arrbasic.s.golden's "pop\tbx" / "mov\t
                 * (bx),ax" and 03_ptrbasic.s.golden's "pop\tbx" /
                 * "mov\t(bx),di". */
                fprintf(out, "pop\tbx\n");
                lhs = val_ind("bx");
            }
            if (lhs.kind != VK_MEM && lhs.kind != VK_MEM_DIRECT &&
                lhs.kind != VK_IND && lhs.kind != VK_STATIC &&
                lhs.kind != VK_REG)
                gen_fatal("assignment to a non-lvalue is not yet supported");
            if (rhs.kind == VK_IND && (lhs.kind == VK_MEM || lhs.kind == VK_MEM_DIRECT)) {
                /* The rhs is itself a dereferenced pointer ("y =
                 * *p;" - 05_arrptr/03_ptrbasic.c) and the lhs is a
                 * plain memory operand - 8086 MOV cannot take two
                 * memory operands ("*-8.(bp)" and "(di)" both are),
                 * so the rhs must be materialized into a real
                 * register first - confirmed against 03_ptrbasic.
                 * s.golden's "y = *p;": "mov di,*-10.(bp)" (the STAR
                 * itself, loading the pointer) / "mov di,(di)" (THIS
                 * step) / "mov *-8.(bp),di". */
                fprintf(out, "mov\t%s,(%s)\n", rhs.reg, rhs.reg);
                rhs = val_reg(rhs.reg);
            }
            if (rhs.kind == VK_MEM_CVT) {
                /* A type-converted memory operand (currently only
                 * OP_LTOI's result - see VK_MEM_CVT's own comment
                 * above) - confirmed to go through DI first, unlike a
                 * bare-NAME VK_MEM rhs (still unconfirmed, see just
                 * below). */
                load_into_di(out, rhs);
                rhs = val_reg("di");
            } else if (rhs.kind == VK_MEM) {
                gen_fatal("direct memory-to-memory assignment (\"x = y;\") "
                          "is not yet supported - 8086 MOV cannot take two "
                          "memory operands, and no golden reference "
                          "confirms which intermediate register a real "
                          "compiler would route this through");
            }
            if (lhs.kind == VK_REG && rhs.kind == VK_REG &&
                strcmp(lhs.reg, rhs.reg) == 0) {
                /* Assigning a 'register'-class local's own just-
                 * computed value right back into itself (e.g. "i = i
                 * + 1;", where the "+1" already happened in place via
                 * a plain "inc di" - see OP_PLUS's new VK_REG case
                 * above) - there is nothing left to store, so this
                 * emits NO instruction at all, confirmed against
                 * 06_regclass.s.golden's "L6:inc\tdi\njmp\tL4" (no
                 * "mov di,di" between them). Still pushes the value
                 * back, same as every other ASSIGN case below, for
                 * OP_EXPR to discard. */
                push_val(&g, rhs);
                break;
            }
            char dbuf[32], sbuf[32];
            render_operand(dbuf, sizeof dbuf, lhs);
            render_operand(sbuf, sizeof sbuf, rhs);
            if (rhs.kind == VK_FUNCADDR)
                free((char *)rhs.reg);
            /* TY_CHAR uses "movb" instead of "mov" - confirmed against
             * 08_castsize.s.golden's "c = (char) i;" -> "movb
             * *-12.(bp),dx". */
            fprintf(out, "%s\t%s,%s\n", type == TY_CHAR ? "movb" : "mov",
                    dbuf, sbuf);
            /* Pushes the assigned value back - confirmed necessary
             * (not just harmless) by 07_ternary's embedded
             * assignments inside a parenthesized comma-list ("(a = a
             * + 1, b = b + 1, a + b)": each "a = a + 1" is itself a
             * comma-operand whose value OP_SEQNC needs something real
             * to pop and discard). Every plain top-level assign-stmt/
             * star-assign-stmt (the only other places ASSIGN appears)
             * now has exactly one extra value on the stack at its
             * closing OP_EXPR, which discards it there instead - see
             * OP_EXPR below. Reusing `rhs` costs no extra
             * instructions (it is already sitting in a register or is
             * a plain immediate) and is never read back in any
             * confirmed case anyway, since OP_SEQNC only ever
             * discards it. The ten compound-assignment operators
             * deliberately do NOT push - c0's grammar never lets one
             * appear anywhere but as a statement's sole top-level
             * operator (parse_assign_stmt() is only reachable from
             * the statement dispatcher, never from parse_expr()'s
             * precedence chain or parse_comma_item()), so nothing
             * would ever consume such a value. */
            push_val(&g, rhs);
            break;
        }

        case OP_RFORCE: {
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_LONG) {
                /* A 'long'-returning function's "return <expr>;" -
                 * the expression's materialized DI(high):SI(low)
                 * value (see materialize_long()) is moved into
                 * AX(low):DX(high), the ordinary 'long' return-value
                 * convention (sect. 1.5 - the same registers gen_
                 * call()'s own long-returning-callee case reads FROM)
                 * - confirmed against 02_long/03_retval.s.golden's
                 * "return a + b;" -> "mov ax,si / mov dx,di". Only a
                 * value already resolvable to VK_LONG (a 'long'
                 * arithmetic result, a materialized constant, ...) is
                 * confirmed - anything else (e.g. an already-DX:AX
                 * call result returned straight back out) is not
                 * exercised by any golden. */
                Val v = materialize_long(out, pop_val(&g));
                if (v.kind != VK_LONG)
                    gen_fatal("RFORCE to 'long' from a non-'long' value is "
                              "not yet supported (no golden reference "
                              "confirms the widening sequence a real "
                              "compiler would need here)");
                fprintf(out, "mov\tax,si\n");
                fprintf(out, "mov\tdx,di\n");
                break;
            }
            if (type != TY_INT)
                gen_fatal("RFORCE to type %d not yet supported (only "
                          "int/long-returning functions are covered so "
                          "far)", type);
            Val v = materialize(out, &g, pop_val(&g));
            /* Matches the confirmed golden pattern exactly, for an
             * immediate (00_smoke's "return 42;"), a memory operand
             * (01_intarith's "return c;"), or anything else not
             * already sitting in AX: load into DI first, then move
             * DI into AX (sect. 1.5's return-value register) - kept
             * as the same two-instruction shape rather than
             * "optimized" to a direct "mov ax,<v>" since that would
             * no longer match real hardware output. EXCEPT when the
             * value is already sitting in AX (OP_TIMES/OP_DIVIDE's
             * quotient, or an OP_CALL result - sect. 1.5's own
             * return-value register, so a called function's result
             * needs no extra move to become the CALLER's own return
             * value either) - confirmed against 04_funcs/01_call.s.
             * golden's "return add(3, 4);" (call/add-sp then
             * straight to "jmp L6", no "mov" at all) and 03_recfact.
             * s.golden's "return n * fact(n - 1);" (imul leaves the
             * product in AX already, then straight to "jmp L3") -
             * this mirrors v7/cc/c10.c's real RFORCE case exactly
             * ("if((r=rcexpr(...))!=0) movreg(r,0,tree);" - rcexpr
             * returns 0, skipping movreg entirely, precisely when
             * the value is already in the target register). */
            if (!(v.kind == VK_REG && strcmp(v.reg, "ax") == 0)) {
                if (g.di_reserved) {
                    /* DI is reserved by a live 'register'-class local
                     * for the rest of this function (see di_reserved's
                     * own comment) - SI is used as the fallback
                     * working register instead, confirmed against
                     * 04_funcs/06_regclass.s.golden's "return sum;" ->
                     * "mov si,*-6.(bp) / mov ax,si" (never DI). */
                    load_into_si(out, v);
                    fprintf(out, "mov\tax,si\n");
                } else {
                    load_into_di(out, v);
                    fprintf(out, "mov\tax,di\n");
                }
            }
            break;
        }

        case OP_EXPR: {
            (void)c1_read_num(temp1, "temp1"); /* source line - not
                                                 * rendered into the
                                                 * .s output anywhere
                                                 * in the confirmed
                                                 * goldens. */
            /* Discards the just-completed statement's leftover
             * expression value, if it left one - an expression
             * statement's value is always unused. Confirmed to be
             * exactly 0 or 1 items here, never more, across every
             * statement form this grammar scope has: OP_RFORCE
             * (return-stmt) already consumes its one value without
             * repushing, so there is nothing left; a plain OP_ASSIGN
             * (assign-stmt/star-assign-stmt) now leaves exactly one
             * (see OP_ASSIGN's comment above); the ten compound-
             * assignment operators push nothing, so there is also
             * nothing left after those. gen_fatal on anything else
             * catches a genuine stack-imbalance bug rather than
             * silently discarding more than one stray value. */
            if (g.valsp > 1)
                gen_fatal("internal: %d unconsumed expression value(s) "
                          "at end of statement (expected 0 or 1) - "
                          "malformed temp1 stream or a c1_gen.c bug",
                          g.valsp);
            if (g.valsp == 1)
                pop_val(&g);
            /* Flush any postfix ++/-- fixup queued by gen_incdec()
             * during this statement - see DEFERRED_MAX's comment on
             * GenState. */
            flush_deferred(out, &g);
            break;
        }

        case OP_SWIT: {
            /* deflab, then a source line (not rendered into the .s
             * output, same as OP_EXPR's), then a run of (label,value)
             * pairs, then a single lone zero word terminating the
             * table (outcode("0") in c0_parser.c/v7's own pswitch() -
             * never a (0,0) pair) - confirmed against 06_switch.
             * 1.golden. */
            int deflab = c1_read_num(temp1, "temp1");
            (void)c1_read_num(temp1, "temp1"); /* line */
            CaseSwitchEntry cases[MCC_NCASES];
            int ncases = 0;
            for (;;) {
                int lab = c1_read_num(temp1, "temp1");
                if (lab == 0)
                    break;
                int val = c1_read_num(temp1, "temp1");
                if (ncases >= MCC_NCASES)
                    gen_fatal("too many 'case' labels in one 'switch' "
                              "(internal limit %d)", MCC_NCASES);
                cases[ncases].lab = lab;
                cases[ncases].val = val;
                ncases++;
            }
            gen_switch_dispatch(out, &g, deflab, cases, ncases);
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
