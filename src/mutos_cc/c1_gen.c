/*
 * c1_gen.c - mutos_c1 code generator.
 *
 * Current opcode coverage (matches mutos_c0's current grammar
 * coverage - see c0_parser.c and src/mutos_cc/README.md): SYMDEF,
 * PROG, EVEN, RLABEL, SAVE, SETREG, BRANCH, LABEL, ANAME, RNAME, BSS,
 * SSPACE, SNAME, NAME, CON, LCON, LTOI, ITOC, CTOL, ITOL, PLUS, MINUS,
 * TIMES, DIVIDE, MOD, AND, OR, EXOR, COMPL, LSHIFT, RSHIFT, AMPER,
 * ITOP, STAR, INCBEF, DECBEF, INCAFT, DECAFT, LESS, LESSEQ, GREAT,
 * GREATEQ, EQUAL, NEQUAL, CBRANCH, LOGAND, LOGOR, EXCLA, COLON, QUEST,
 * SEQNC, COMMA, NULLOP, CALL, ASPLUS, ASMINUS, ASTIMES, ASDIV, ASMOD,
 * ASLSH, ASRSH, ASSAND, ASOR, ASXOR, ASSIGN, RFORCE, EXPR, SWIT,
 * RETRN, SETSTK, EOFC - and, in temp2 (read after temp1, see
 * gen_strings()), LABEL, BDATA and EOFC: string-literal data.
 *
 * Structure: the per-opcode dispatch loop (c1_generate()) and its
 * codegen helpers decide WHAT to emit; every byte of assembly text
 * goes through the small emission layer (put_insn() and friends - see
 * its section below), which alone knows mutos_as's source-line syntax
 * and also runs the register-occupancy guard (note_writes()) that
 * turns a register collision into an explicit "not yet supported"
 * instead of silently wrong code. The ORDER in which the loop visits
 * temp1 is postfix order, except where an expression's operands must
 * be evaluated right operand first - see the "Evaluation order"
 * section (plan_expression()), which then replays temp1's subtrees in
 * that order through the same handlers.
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
#include <stdint.h>
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

/* "pointer to char" (9) - a char array's decayed address, a string
 * literal's, and the result type of '&' on a char element. */
#define TY_PTR_CHAR (TY_CHAR | 010)

/* Matches c0_parser.c's MCC_SZINT exactly - the size (bytes) of a
 * plain int/pointer, and (per docs/MUTOS_C_ABI.md sect. 1.6) the
 * offset from a 'long' local's own base offset to its LOW word. */
#define MCC_SZINT 2

#define DEFERRED_MAX 4

/* OP_SETSTK's two confirmed local-frame allocation shapes (docs/
 * MUTOS_C_ABI.md sect. 1.9): the largest frame (bytes beyond the SAVE
 * prologue's own register-save area) confirmed to use a plain "sub
 * sp,N" - 09_abiprobe/02_frame080.s.golden, N=80 - and the smallest
 * confirmed to use "mov ax,N / call chkstk" instead - 09_abiprobe/
 * 03_frame128.s.golden, N=128. The real threshold lies somewhere in
 * (80,128]; sizes 81..127 stay an explicit "not yet supported". */
#define MCC_SUBSP_MAX  80
#define MCC_CHKSTK_MIN 128

/* A shift of a REGISTER by a compile-time-constant count: plain 8086
 * has no shift-by-immediate opcode, so the real compiler repeats the
 * single-bit form for a count of up to 2 (04_shift.s.golden's "r >> 2"
 * -> two "sar\tdi,*1"; 02_array2d.s.golden's two "sal\tsi,*1"), and
 * from 3 up loads the count into CX and shifts by CL instead - the
 * real, non-optimized MUTOS c1 output in tests/mutos_as/kernel_nonopt/
 * (amx.s's "mov\tcx,*3." / "sar\tdi,cl" for a ">> 3", likewise
 * "*7." and "*11."; no run of three or more single-bit shifts exists
 * anywhere in that corpus). See emit_const_shift(). */
#define MCC_SHIFT_REPEAT_MAX 2

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

/* "pointer to array of int" - v7/cc's incref(incref(INT, ARRAY), PTR)
 * = 104 - the type of a 2-D subscript's OUTER (row-step) OP_ITOP only
 * ("m[i][j]" - 05_arrptr/02_array2d.1.golden, 10_integ/05_matmul.
 * 1.golden; see c0_parser.c's emit_subscript_2d() for why that one
 * node keeps this richer type while every AMPER/PLUS/STAR around it is
 * flat TY_PTR_INT/TY_INT). */
#define TY_PTR_ARY_INT 104

/* Derived-type tests on a wire type code (v7/cc's encoding: base type
 * in the low 3 bits, the OUTERMOST derived degree in bits 3-4 - PTR
 * 010, FUNC 020, ARRAY 030 - see mutos_cc.h's XTYPE comment). */
#define TY_XTYPE_MASK 030
static int ty_is_ptr(int t)  { return (t & TY_XTYPE_MASK) == 010; }
static int ty_is_func(int t) { return (t & TY_XTYPE_MASK) == 020; }
/* One degree less - v7/cc/c04.c's decref(). */
static int ty_decref(int t) { return (t & 07) | ((t >> 2) & ~07); }
/* A 16-bit (word) value: int, unsigned, or ANY pointer - every pointer
 * is one word on this target, whatever it points to, so it loads,
 * stores, pushes and returns exactly like an int (05_arrptr/
 * 05_arrofptr.s.golden's "mov di,(di)" / "push di" for a "char *"
 * element; 07_strlibc.s.golden's "char *"-returning strcpy()). */
static int ty_is_word(int t) { return t == TY_INT || t == TY_UNSIGN || ty_is_ptr(t); }

typedef enum { VK_IMM, VK_MEM, VK_MEM_DIRECT, VK_REG, VK_IND, VK_IND_PENDING, VK_COND, VK_LONG, VK_LCON,
               VK_FUNC, VK_ARGLIST, VK_MEM_CVT, VK_STATIC, VK_FUNCADDR,
               VK_STATICADDR, VK_REGOFF, VK_SCALED, VK_ROWADDR,
               VK_STACKED, VK_CHARX, VK_IDXOFF } ValKind;
/* VK_IDXOFF - an int subscript "var + N" (`cl` the variable - VK_MEM or
 * VK_STATIC - `imm` the constant N), NOT computed: produced by OP_PLUS
 * only when an OP_ITOP scales it next, because the real compiler never
 * adds the constant to the index at all - v7/cc/c12.c's distrib() turns
 * "(j + 1) * 2" into "j * 2 + 2", and the constant ends up in the
 * element's displacement: 10_integ/02_bubsort.s.golden's "a[j + 1]" ->
 * "mov si,*-8.(bp)" / "sal si,*1" / "add si,*4.(bp)" / ... "*2.(si)".
 * OP_ITOP turns it into a VK_REGOFF (the scaled variable in a register,
 * plus N * the element size), which the pointer PLUS and OP_STAR carry
 * on to the displacement; nothing else accepts one (pop_val()). */
/* VK_CHARX - a 'char' in memory READ AS AN INT: opcode 109 (OP_ITOC)
 * with type TY_INT, applied to a byte-sized memory operand, with NO
 * code emitted yet. `cl` holds that memory operand (VK_MEM, VK_STATIC
 * or VK_IND - never a register). Loading it is always the same two
 * instructions, "movb ax,<mem>" / "cbw" (load_charx()): CBW is a fixed-
 * register 8086 instruction, so the value can only ever arrive in AX -
 * 09_abiprobe/02_frame080.s.golden ("movb ax,*-84.(bp)" / "cbw"),
 * 10_integ/04_strrev.s.golden's "return buf[0];", and the real non-
 * optimized compiler output in tests/mutos_as/kernel_nonopt/ ("movb
 * ax,*23.(di)" / "cbw", "movb ax,(bx)" / "cbw", ...). It is kept
 * unloaded because WHEN it is loaded depends on the consumer: in
 * "buf[0] + buf[79]" both operands need AX, and the golden loads the
 * left one, moves it to DI ("mov di,ax"), and only then loads the
 * right one - an eager load at the ITOC would have to overwrite the
 * first value before it was used. v7's own c1 does the same thing in
 * its optim() (c12.c's ITOC case turns "ITOC of a NAME" into a char-
 * typed NAME, loaded by whichever code template consumes it). Only the
 * golden-confirmed consumers accept one - int PLUS of two of them,
 * RFORCE, an int ASSIGN's right-hand side - through pop_val_ex()'s
 * POP_CHARX; every other consumer refuses it (pop_val()). */
/* VK_STACKED - an operand that was evaluated AHEAD of its left-hand
 * sibling and pushed onto the real machine stack ("push di"), so the
 * sibling's own code could use the working registers freely - the
 * evaluation-order section's SEG_SPILL step (plan_spill()) produces
 * it. It carries no register and no operand text: its only consumer
 * is the operator that pops it back ("pop cx"). Confirmed for one
 * operator only, int TIMES (10_integ/05_matmul.s.golden's "a[i][k] *
 * b[k][j]" -> ... "push di" ... "mov ax,di" / "pop cx" / "imul cx");
 * pop_val() and discard_val() refuse it, so any other consumer is an
 * internal error rather than a silently unbalanced machine stack. */
/* VK_REGOFF - "the pointer value `cl` (a memory operand or a
 * register), plus the constant `imm` bytes", neither loaded nor added
 * yet: pointer arithmetic with a compile-time-constant index
 * on a pointer that lives in memory or a register ("argv[1]",
 * "p[2]"), produced by OP_PLUS only when the very next opcode is the
 * OP_STAR that dereferences it - which turns it into a displacement-
 * indirect operand (VK_IND with a displacement, "*2.(di)") instead of
 * an "add". Confirmed against 09_abiprobe/01_argvmain.s.golden's
 * "strlen(argv[1])": "mov di,*6.(bp)" / "mov di,*2.(di)" / "push
 * di" - no "add di,*2.". No other handler ever sees one (pop_val()
 * refuses it). */
/* VK_STATICADDR - the ADDRESS of a static, label-tagged object:
 * OP_AMPER applied to a VK_STATIC operand. In practice a string
 * literal (c0's putstr(): NAME(SC_STATIC, TY_CHAR, <label>) AMPER(9)),
 * `offset` holding the label. Like VK_FUNCADDR it is a link-time
 * constant, so NO code is emitted for the AMPER, and it renders as the
 * word immediate "#L<n>" - confirmed against 05_arrptr/05_arrofptr.s.
 * golden's "names[0] = \"one\";" -> "mov *-10.(bp),#L4" (a direct
 * memory-immediate store) and 07_strlibc.s.golden's string argument ->
 * "mov di,#L4" / "push di" (an immediate, so it goes through DI:
 * PUSH has no immediate form - see gen_call()). Those two consumers
 * (OP_ASSIGN's rhs, a call argument) are the only confirmed ones;
 * pop_operands() refuses one as an arithmetic or comparison operand. */
/* VK_SCALED / VK_ROWADDR - the two unfinished stages of a 2-D
 * subscript address "&m + i*R + j*E" (R = row size, E = element
 * size), kept symbolic - NO code emitted - until both scaled terms are
 * known, because the real compiler does not add them separately: it
 * factors them, exactly as v7/cc/c12.c's distrib() does for any sum
 * of constant multiples ("i*8 + j*2" -> "(i*4 + j)*2" - see OP_PLUS's
 * 2-D case, confirmed against 05_arrptr/02_array2d.s.golden and
 * 10_integ/05_matmul.s.golden).
 *
 * VK_SCALED is a runtime index times a constant scale, the result of
 * an OP_ITOP that is part of a 2-D subscript: `cl` holds the index (a
 * plain memory operand - VK_MEM or VK_STATIC; anything else is
 * refused), `imm` the scale in bytes.
 *
 * VK_ROWADDR is "base + index*scale" for the row step: `reg` names
 * the register holding the base address (always "di", from
 * OP_AMPER's eager "lea"), `cl`/`imm` the row index and row size
 * exactly as in VK_SCALED. It survives the row's own STAR/AMPER pair
 * (cancelled - see OP_STAR) and is consumed by the column step's
 * OP_PLUS.
 *
 * Neither is ever an operand of a real instruction; pop_val() refuses
 * both, so only the handlers written for them (OP_ITOP/OP_PLUS via
 * pop_any(), OP_STAR's cancellation, which does not pop at all) can
 * see one. */
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
 * VK_REG; `imm` a constant displacement ("*2.(di)" - see VK_REGOFF),
 * 0 for none. Every consumer renders it through o_val(), never by
 * rebuilding "(reg)" from `reg` alone, so the displacement is kept. */
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
 * doing this. See OP_STAR's own comment for the exact trigger (what
 * consumes the dereference - scan_consumer() - the wire bytes
 * themselves are unaffected, so this is purely a c1-side code-shape
 * decision). Carries no register (the
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
    int     postfix;  /* see Val's own field of the same name */
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
     * immediately emitting code is what lets a condition compile a
     * comparison straight into one compare and branch
     * (gen_cond_branch()), and "&&"/"||" become short-circuit "jumping
     * code", without ever materializing an intermediate 0/1 - see the
     * OP_LESS.../OP_EXCLA cases below, the "Conditional evaluation"
     * section, and docs/DEVLOG.md's Milestone 4 section for the full
     * derivation against 03_rellogic's goldens. */
    int       true_op;
    SimpleVal cl, cr;
    int       cond_is_long; /* VK_COND only: set when cl/cr are 'long'
     * operands (gen_long_cmp()'s shape below) rather than the
     * ordinary 16-bit shape emit_cmp_and_branch() renders - only
     * a condition is confirmed to consume one (02_long/01_addsub's
     * "if (c > 0L)" - gen_cond_branch()); every other consumer
     * (materialize(), via emit_cmp_and_branch()) gen_fatal()s on it
     * rather than guessing a shape no golden confirms. */
    int       clobbered; /* set by the register-occupancy guard (see
     * note_writes()) when an instruction overwrote a register this
     * still-pending value lives in; pop_val() then refuses to hand it
     * to a consumer - see the "Register-occupancy guard" section. */
    int       rowbase;   /* VK_MEM_DIRECT only: set when this address is
     * a 2-D array ROW (the result of a row step whose STAR/AMPER pair
     * OP_STAR cancelled), not a plain array/variable address - so a
     * following runtime column index ("m[0][j]") can be refused
     * explicitly instead of falling into the 1-D code path, whose
     * instruction order no golden confirms for this shape. */
    int       postfix;   /* VK_REG only: this is a postfix '++'/'--'
     * operand's OLD value, just loaded by gen_incdec() (its fixup still
     * queued). Tested for truth, or compared with 0, as a condition,
     * such a value gets "or reg,reg" rather than "cmp reg,*0" - see
     * gen_cond_branch(). Any operator's result is a fresh Val, so the
     * flag never outlives the value it describes. */
    int       bytev;     /* a memory operand (VK_MEM, VK_STATIC, VK_IND)
     * that holds a 'char' - a NAME of type TY_CHAR, or an OP_STAR of
     * type TY_CHAR. Only a byte instruction ("movb") may read or write
     * it, so it is handed only to the consumers that know that - an
     * assignment target or right-hand side of the same type, OP_ITOC,
     * OP_CTOL, OP_AMPER - through pop_val_ex()'s POP_BYTE; every other
     * consumer refuses it (pop_val()): mutos_c0 wraps a char used as an
     * int in OP_ITOC (-> VK_CHARX), so meeting a bare one anywhere else
     * means a word instruction would read a byte's slot - the silent
     * wrong code this flag exists to stop. */
    int       regvar;    /* VK_REG only: a 'register'-class local's own
     * NAME (OP_NAME's SC_REG case), not a computed value - v7's
     * degree() treats it as the NAME leaf it is, which decides a
     * relational's operand order (see OP_LESS...). */
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

/* One rendered assembly operand ("di", "*-6.(bp)", "#240.", "L7",
 * "(bx)", "@*4.(bp)", ...) - a small fixed-size VALUE type, so an
 * operand can be built inline as a function argument (see the o_*()
 * constructors and the emission layer further below) with no
 * caller-side scratch buffer. OPND_MAX comfortably exceeds the longest
 * text any operand kind renders to (a symbol name is at most MCC_NCPS
 * characters plus a marker); o_fmt() gen_fatal()s rather than
 * silently truncating if that ever stops being true. */
#define OPND_MAX 48
typedef struct { char s[OPND_MAX]; } Opnd;

/* One assembly instruction or operand-taking pseudo-op: `mnem` plus
 * 0, 1 or 2 operands, rendered by put_insn() - the ONE place that
 * knows mutos_as's "mnem<TAB>a,b<NL>" line syntax. Used both for
 * immediate emission (ins0()/ins1()/ins2()), for the const fixed-
 * idiom tables (SEQ_* below - v7/cc table.s-style code templates),
 * and as the storage type of GenState's deferred postfix-++/-- queue
 * (a structured instruction, not pre-formatted text). */
typedef struct {
    const char *mnem;
    int         nops;   /* 0, 1 or 2 */
    Opnd        a, b;
} Insn;

/* One bit per 8086 general register the register-occupancy guard
 * tracks (see note_writes()); SP/BP/segment registers never hold an
 * expression value, so they have no bit. */
enum {
    RB_AX = 1u << 0,
    RB_BX = 1u << 1,
    RB_CX = 1u << 2,
    RB_DX = 1u << 3,
    RB_SI = 1u << 4,
    RB_DI = 1u << 5
};

/* One step of an evaluation-order plan (see the "Evaluation order"
 * and "Conditional evaluation" sections further below): stream a
 * contiguous range of temp1 through the ordinary opcode handlers, or
 * one of the steps a reordered operator or a conditionally evaluated
 * operand needs. `a`/`b`/`c` are per-kind arguments - usually indices
 * into the plan's `slot` array, which holds the label numbers and
 * deferred-fixup marks the steps pass to each other at run time. */
typedef enum {
    SEG_RANGE,   /* stream temp1 [start, end) */
    SEG_SPILL,   /* the value on top goes onto the machine stack */
    SEG_SWAP,    /* exchange the top two values (after a spill) */
    SEG_GOTO,    /* position temp1 at `start` - past the opcodes the plan
                  * itself stands for (LOGAND, LOGOR, COLON, QUEST, SEQNC,
                  * an EXCLA under cbranch, the CBRANCH) */
    SEG_ALLOC,   /* slot[a] = a fresh c1 label */
    SEG_LABEL,   /* place label slot[a] */
    SEG_MARK,    /* open a conditionally evaluated region (see
                  * region_open()); a = its two-slot record */
    SEG_BRANCH,  /* a condition: pop it, branch to slot[a] when its truth
                  * equals b; closes region c */
    SEG_LOGVAL,  /* a value-context &&/||/!: 0/1 into DI; slot[a] is the
                  * label the condition branched to when true */
    SEG_QTRUE,   /* end of a ?: true arm: a = the false label's slot, b =
                  * the end label's slot (allocated here), c = the region */
    SEG_QFALSE,  /* end of a ?: false arm: b = the end label's slot, c =
                  * the region */
    SEG_DISCARD, /* end of a comma operator's left operand: c = the region */
    SEG_PUSHARG, /* a call argument just computed goes onto the machine
                  * stack; slot[a] counts the words pushed so far */
    SEG_CALL     /* the call, its arguments pushed: slot[a] words, b = the
                  * CALL's own type */
} SegKind;
typedef struct {
    SegKind kind;
    long    start, end;   /* SEG_RANGE: temp1 byte offsets, [start, end);
                           * SEG_GOTO: the position (start) */
    int     a, b, c;
} Seg;

/* An expression's opcode-visiting order, when it differs from temp1's
 * own postfix order. Fixed capacity, like GenState's other buffers: a
 * reordered operator needs 5 steps, an "&&"/"||" about 4 plus its
 * operands' own, a "?:" 8 - so this is far beyond any expression a
 * register-occupancy guard would let through anyway. */
#define PLAN_MAX 256
#define PLAN_SLOTS 128
typedef struct {
    int  active;
    int  n, cur;          /* steps, and the one being executed */
    int  entered;         /* the current SEG_RANGE was already seeked to */
    long end;             /* where plain streaming resumes once the plan
                           * is done: the expression's EXPR terminator,
                           * or just past a CBRANCH the plan generated */
    int  nslots;          /* slots handed out while building the plan */
    int  slot[PLAN_SLOTS];
    Seg  seg[PLAN_MAX];
} Plan;

typedef struct {
    FILE *out;                /* the .s output stream - see the emission
                                * layer (put_insn() and friends) */
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
    unsigned reserved;        /* RB_* mask of registers currently owned
                                * by a 'register'-class local: reset at
                                * each OP_SAVE, a bit set once OP_RNAME
                                * claims that register (04_funcs/
                                * 06_regclass.c) - kept for the rest of
                                * the function (its lifetime), since
                                * this grammar scope never frees a
                                * register mid-function. While DI is
                                * reserved it is off-limits as the
                                * generic "working register" - SI is
                                * used instead where that is confirmed
                                * (OP_RFORCE's default path: 06_regclass.
                                * s.golden's "return sum;" -> "mov si,
                                * *-6.(bp) / mov ax,si", not the usual
                                * DI-then-AX shape); every other write
                                * to a reserved register is caught by
                                * note_writes() (explicit "not yet
                                * supported") unless it computes that
                                * variable's own new value. */
    unsigned regvar_dirty;    /* RB_* mask of reserved registers
                                * overwritten, mid-statement, while
                                * computing their own variable's new
                                * value (note_writes()'s exemption) -
                                * cleared by that variable's OP_ASSIGN;
                                * a read of the variable (OP_NAME
                                * SC_REG) while its bit is set would see
                                * a half-computed value, so it is
                                * refused. */
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
    Insn deferred[DEFERRED_MAX];     /* postfix ++/-- fixups (INCAFT/
                                 * DECAFT) queued at the operator's
                                 * own position, flushed at the next
                                 * OP_EXPR - see gen_incdec()'s and
                                 * OP_EXPR's comments; confirmed via
                                 * 05_incdec.s.golden's "inc *-6.(bp)"
                                 * appearing right after the enclosing
                                 * assignment's own "mov", not at
                                 * INCAFT's own position. */
    int  ndeferred;
    int  defer_floor;          /* entries below this index of `deferred`
                                 * belong to an enclosing, unconditionally
                                 * evaluated part of the statement and must
                                 * stay queued until its end - see
                                 * region_open(); 0 outside a plan */
    Plan plan;                 /* the current expression's evaluation-
                                 * order plan, while one is active - see
                                 * the "Evaluation order" section */
} GenState;

_Noreturn static void gen_fatal(const char *fmt, ...)
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

static void fatal_clobbered(void)
{
    gen_fatal("an intermediate value was overwritten in its register "
              "before being used (e.g. by a call, a multiply/divide or "
              "another subexpression evaluated in between) - this "
              "expression shape needs register spilling/reordering that "
              "no golden reference confirms yet, so it is not yet "
              "supported rather than miscompiled - see docs/DEVLOG.md");
}

/* What a consumer is prepared to receive beyond an ordinary value - see
 * Val's `bytev` field and VK_CHARX. */
enum { POP_BYTE = 1, POP_CHARX = 2 };

/* Refuses a 'char' operand a consumer has not been written for (see
 * POP_BYTE/POP_CHARX): generating code from it with a word
 * instruction would silently read or write the wrong bytes. */
static void refuse_char_operand(const Val *v, int allow)
{
    if (v->bytev && !(allow & POP_BYTE))
        gen_fatal("a 'char' value used directly by this operator (not "
                  "through the char-to-int conversion, opcode 109, that "
                  "mutos_c0 inserts for an int operand) is not yet "
                  "supported - see src/mutos_cc/README.md");
    if (v->kind == VK_CHARX && !(allow & POP_CHARX))
        gen_fatal("a 'char' element or variable used as an operand of this "
                  "operator is not yet supported (no golden reference "
                  "confirms its instruction shape; confirmed so far: '+' of "
                  "two chars, 'return', and an int assignment) - see "
                  "src/mutos_cc/README.md");
}

/* Pops a value that is about to be USED (read by the code emitted
 * next). Refuses a value whose register was overwritten while it was
 * pending (see note_writes()) - emitting code from it would silently
 * read garbage - and a 'char' operand `allow` does not admit. */
static Val pop_any_ex(GenState *g, int allow)
{
    if (g->valsp <= 0)
        gen_fatal("expression stack underflow - malformed temp1 stream");
    Val v = g->valstack[--g->valsp];
    if (v.clobbered)
        fatal_clobbered();
    refuse_char_operand(&v, allow);
    return v;
}

static Val pop_any(GenState *g)
{
    return pop_any_ex(g, 0);
}

/* pop_any(), for every consumer that has not been written to handle
 * an unfinished 2-D subscript address (VK_SCALED/VK_ROWADDR - see
 * their ValKind comment): refusing it here is what keeps one from
 * ever being rendered as an instruction operand. */
static Val pop_val_ex(GenState *g, int allow)
{
    Val v = pop_any_ex(g, allow);
    if (v.kind == VK_REGOFF)
        gen_fatal("internal: an unfolded register+offset address reached a "
                  "consumer other than OP_STAR");
    if (v.kind == VK_SCALED || v.kind == VK_ROWADDR)
        gen_fatal("a 2-D array subscript used in a shape other than a "
                  "complete \"m[i][j]\" element reference is not yet "
                  "supported - see src/mutos_cc/README.md");
    if (v.kind == VK_STACKED)
        gen_fatal("internal: a spilled (machine-stack) operand reached a "
                  "consumer other than int TIMES");
    if (v.kind == VK_IDXOFF)
        gen_fatal("internal: an uncomputed \"var + N\" subscript reached a "
                  "consumer other than OP_ITOP");
    return v;
}

static Val pop_val(GenState *g)
{
    return pop_val_ex(g, 0);
}

/* Pops a value whose CONTENT is dropped unread (the comma operator's
 * left operand, a statement's leftover value at OP_EXPR) - a
 * clobbered register there is harmless. */
static void discard_val(GenState *g)
{
    if (g->valsp <= 0)
        gen_fatal("expression stack underflow - malformed temp1 stream");
    if (g->valstack[g->valsp - 1].kind == VK_STACKED)
        gen_fatal("internal: a spilled (machine-stack) operand was "
                  "discarded - the machine stack would be left unbalanced");
    g->valsp--;
}

/* Pops an assignment TARGET. A register lvalue (a 'register' local)
 * names a location, not a value, so its register having been
 * overwritten while computing the right-hand side is expected (e.g.
 * "i = i + 1;" -> "inc di"); every other lvalue kind is checked like
 * pop_val() (an indirect "(di)" target whose address register was
 * overwritten would store through a garbage address). */
static Val pop_lvalue_ex(GenState *g, int allow)
{
    if (g->valsp > 0 && g->valstack[g->valsp - 1].kind == VK_REG) {
        Val v = g->valstack[--g->valsp];
        v.clobbered = 0;
        return v;
    }
    return pop_val_ex(g, allow);
}

static Val val_imm(long v) { Val r = {0}; r.kind = VK_IMM; r.imm = v; return r; }
static Val val_mem(int off) { Val r = {0}; r.kind = VK_MEM; r.offset = off; return r; }
static Val val_mem_direct(int off) { Val r = {0}; r.kind = VK_MEM_DIRECT; r.offset = off; return r; }
static Val val_reg(const char *reg) { Val r = {0}; r.kind = VK_REG; r.reg = reg; return r; }
static Val val_ind(const char *reg) { Val r = {0}; r.kind = VK_IND; r.reg = reg; return r; }
/* A displacement-indirect "*<disp>.(reg)" operand - see VK_IND. */
static Val val_ind_disp(const char *reg, long disp) { Val r = val_ind(reg); r.imm = disp; return r; }
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
    s.postfix = v.postfix;
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
    v.postfix = s.postfix;
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
 * A memory operand's displacement takes the same marker by the same
 * rule: `*` while it fits a signed byte, `#` otherwise - confirmed
 * against 09_abiprobe/03_frame128.s.golden ... 07_frame300.s.golden,
 * whose "char buf[N]" sits at bp-132 ... bp-304: "movb #-132.(bp),*1."
 * and "movb ax,#-132.(bp)", while the element at bp-5 in the same
 * files stays "*-5.(bp)". man/mutos_as.1 says the marker "has no
 * effect" on a displacement's encoding (the assembler picks the
 * displacement size from the value), so this is again a source-text
 * convention only. Every golden before those had displacements within
 * -128..127, which is why this used to be an unconditional `*`. The
 * optimized kernel corpus (tests/mutos_as/kernel_opt/, c2 output)
 * mostly agrees ("#610.(di)", "#620.(di)"), with some "*610.(bx)"
 * forms c2 rewrote; the non-optimized corpus has no displacement
 * outside a byte at all. */
static char disp_marker(long disp)
{
    return (disp >= -128 && disp <= 127) ? '*' : '#';
}

static void render_operand(char *buf, size_t n, Val v)
{
    switch (v.kind) {
    case VK_IMM:
        if (v.imm >= -128 && v.imm <= 127)
            snprintf(buf, n, "*%ld.", v.imm);
        else
            snprintf(buf, n, "#%ld.", v.imm);
        break;
    case VK_MEM:
    case VK_MEM_DIRECT:
    case VK_MEM_CVT:
        snprintf(buf, n, "%c%d.(bp)", disp_marker(v.offset), v.offset);
        break;
    case VK_STATIC: snprintf(buf, n, "L%d", v.offset); break;
    case VK_FUNCADDR: snprintf(buf, n, "#%s", v.reg); break;
    case VK_STATICADDR: snprintf(buf, n, "#L%d", v.offset); break;
    case VK_REG: snprintf(buf, n, "%s", v.reg); break;
    case VK_IND:
        /* A displacement, when there is one, takes its marker like
         * every bp-relative displacement ("*2.(di)" - 09_abiprobe/
         * 01_argvmain.s.golden; the kernel_nonopt corpus has "*1.(si)",
         * "*23.(di)", ...). */
        if (v.imm != 0)
            snprintf(buf, n, "%c%ld.(%s)", disp_marker(v.imm), v.imm, v.reg);
        else
            snprintf(buf, n, "(%s)", v.reg);
        break;
    case VK_REGOFF: snprintf(buf, n, "<unfolded-register-offset>"); break;
    case VK_IND_PENDING: snprintf(buf, n, "<unpopped-ind-pending>"); break;
    case VK_COND: snprintf(buf, n, "<unmaterialized-cond>"); break;
    case VK_LONG: snprintf(buf, n, "<unrendered-long-di:si>"); break;
    case VK_LCON: snprintf(buf, n, "<unmaterialized-long-const>"); break;
    case VK_FUNC: snprintf(buf, n, "<unrendered-func-name>"); break;
    case VK_ARGLIST: snprintf(buf, n, "<unrendered-arglist>"); break;
    case VK_SCALED: snprintf(buf, n, "<unmaterialized-scaled-index>"); break;
    case VK_ROWADDR: snprintf(buf, n, "<unmaterialized-row-address>"); break;
    case VK_STACKED: snprintf(buf, n, "<spilled-operand>"); break;
    case VK_CHARX: snprintf(buf, n, "<unloaded-char>"); break;
    case VK_IDXOFF: snprintf(buf, n, "<uncomputed-index>"); break;
    }
}

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
/* Assembly emission layer.
 *
 * Every byte of assembly text mutos_c1 writes goes through the few
 * functions below - nothing else in this file calls fprintf()/fputs()
 * on the output stream. mutos_as's source-line syntax is therefore
 * decided in exactly one place:
 *   - an instruction/pseudo-op is "mnem", "mnem<TAB>a" or
 *     "mnem<TAB>a,b", newline-terminated (put_insn());
 *   - a label is "L<n>:" with NO trailing newline, so whatever is
 *     emitted next glues onto the same source line ("L4:cmp\t...") -
 *     the convention every golden uses (put_label());
 *   - "|"-comments and other free-form lines ("| _a=-6.", "|NREG 3")
 *     are printf-formatted and newline-terminated (put_line()).
 * Call sites read like the assembly they produce, e.g.
 *     ins2(g, "lea", o_reg("di"), o_val(v));   -> "lea\tdi,*-16.(bp)"
 *     ins2(g, "mov", o_reg(r), o_val(ind));     -> "mov\tdi,(di)"
 * and the confirmed fixed multi-instruction idioms are const Insn
 * tables (SEQ_*), emitted with put_seq(). This layer only centralizes
 * the TEXT - every codegen decision (which register, which shape)
 * stays exactly where it was, so it changes no emitted byte. */

#if defined(__GNUC__) || defined(__clang__)
#define PRINTF_LIKE(fmt_idx, arg_idx) \
    __attribute__((format(printf, fmt_idx, arg_idx)))
#else
#define PRINTF_LIKE(fmt_idx, arg_idx)
#endif

/* printf-style operand constructor - the general escape hatch for an
 * operand whose text is not one of the kinds below (e.g. switch's
 * "@L<n>(bx)" or its hex "#/<n>" literal). Never truncates silently. */
static Opnd o_fmt(const char *fmt, ...) PRINTF_LIKE(1, 2);
static Opnd o_fmt(const char *fmt, ...)
{
    Opnd o;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(o.s, sizeof o.s, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof o.s)
        gen_fatal("internal: operand text longer than %d characters",
                  OPND_MAX - 1);
    return o;
}

/* A register ("di"), by its mutos_as name. */
static Opnd o_reg(const char *reg) { return o_fmt("%s", reg); }
/* A symbol or runtime-helper name ("_main", "lmul", "cret"). */
static Opnd o_sym(const char *name) { return o_fmt("%s", name); }
/* A label reference: "L7". */
static Opnd o_lab(int lab) { return o_fmt("L%d", lab); }
/* Any resolved Val, via render_operand(). */
static Opnd o_val(Val v)
{
    Opnd o;
    render_operand(o.s, sizeof o.s, v);
    return o;
}
/* An ordinary immediate ("*5.", "#240.") - render_operand()'s rule. */
static Opnd o_imm(long v) { return o_val(val_imm(v)); }
/* A bp-relative memory operand ("*-6.(bp)"). */
static Opnd o_mem(int offset) { return o_val(val_mem(offset)); }
/* CMP's own immediate shape - see render_cmp_imm(). */
static Opnd o_cmpimm(long v)
{
    Opnd o;
    render_cmp_imm(o.s, sizeof o.s, v);
    return o;
}
/* The single-bit shift count "*1" - see render_bare_imm(). */
static Opnd o_shift1(void)
{
    Opnd o;
    render_bare_imm(o.s, sizeof o.s, 1);
    return o;
}
/* mutos_as's "@" indirect-operand marker, e.g. "call\t@*4.(bp)". */
static Opnd o_indirect(Opnd x) { return o_fmt("@%s", x.s); }

static Insn insn0(const char *mnem)
{
    Insn i = {0};
    i.mnem = mnem;
    return i;
}
static Insn insn1(const char *mnem, Opnd a)
{
    Insn i = insn0(mnem);
    i.nops = 1;
    i.a = a;
    return i;
}
static Insn insn2(const char *mnem, Opnd a, Opnd b)
{
    Insn i = insn1(mnem, a);
    i.nops = 2;
    i.b = b;
    return i;
}

/* -------------------------------------------------------------- */
/* Register-occupancy guard.
 *
 * This code generator keeps every pending intermediate value on the
 * value stack, tagged with WHERE it lives (VK_REG "di", VK_IND "(di)",
 * VK_LONG = DI:SI, ...), but it does not allocate registers: each
 * operator loads into its own confirmed working register (DI, AX, CX,
 * DX, ...) regardless of what already lives there. Nothing used to
 * check for a collision, so e.g. "f(a) + a * b" silently lost f()'s
 * result (IMUL overwrote AX), "v[a + 1]" lost the array base in DI,
 * and "if (a + b < c)" compared DI with itself. The guard makes every
 * such collision an explicit "not yet supported" instead - no golden
 * yet confirms the spill/reordering a real compiler would emit, so
 * inventing one would violate this project's verification rule.
 *
 * Mechanism: put_insn() looks every instruction up in INSN_FX (which
 * registers it writes - its register destination operand and/or
 * implicit ones like CWD -> DX, CALL -> AX/BX/CX/DX), and
 * note_writes() then
 *   1. marks every value still ON the value stack that lives in a
 *      written register as clobbered; pop_val() refuses to hand a
 *      clobbered value to a consumer (a value that is only discarded -
 *      discard_val() - is harmless), and
 *   2. refuses a write to a register owned by a 'register' local
 *      (GenState.reserved), unless the statement being compiled is
 *      that variable's own assignment (see note_writes()).
 * Operands a handler has already popped but not yet consumed are not
 * on the stack, so handlers that write a register before reading such
 * an operand call require_free() explicitly first. */

static unsigned reg_bit(const char *reg)
{
    static const struct { const char *name; unsigned bit; } regs[] = {
        { "ax", RB_AX }, { "al", RB_AX }, { "bx", RB_BX },
        { "cx", RB_CX }, { "cl", RB_CX }, { "dx", RB_DX }, { "dl", RB_DX },
        { "si", RB_SI }, { "di", RB_DI },
    };
    if (!reg)
        return 0;
    for (size_t i = 0; i < sizeof regs / sizeof regs[0]; i++)
        if (strcmp(regs[i].name, reg) == 0)
            return regs[i].bit;
    return 0; /* sp, bp, cs, a memory operand's text, ... */
}

static const char *reg_name(unsigned bit)
{
    switch (bit) {
    case RB_AX: return "ax";
    case RB_BX: return "bx";
    case RB_CX: return "cx";
    case RB_DX: return "dx";
    case RB_SI: return "si";
    default:    return "di";
    }
}

/* Registers a (simple) operand's value depends on. Only VK_REG/VK_IND
 * keep a register name in `reg` (VK_FUNC/VK_FUNCADDR keep a symbol
 * there, which is not a register). */
static unsigned simple_regs(SimpleVal s)
{
    switch (s.kind) {
    case VK_REG:
    case VK_IND:  return reg_bit(s.reg);
    case VK_LONG: return RB_DI | RB_SI;
    default:      return 0;
    }
}

static unsigned val_regs(const Val *v)
{
    switch (v->kind) {
    case VK_REG:
    case VK_IND:  return reg_bit(v->reg);
    case VK_LONG: return RB_DI | RB_SI;
    case VK_COND: return simple_regs(v->cl) | simple_regs(v->cr);
    case VK_ARGLIST: {
        unsigned m = 0;
        for (int i = 0; i < v->arglist->n; i++)
            m |= val_regs(&v->arglist->items[i]);
        return m;
    }
    case VK_REGOFF:  return simple_regs(v->cl);
    case VK_SCALED:  return simple_regs(v->cl);
    case VK_CHARX:   return simple_regs(v->cl); /* "(bx)" etc. */
    case VK_IDXOFF:  return simple_regs(v->cl);
    case VK_ROWADDR: return reg_bit(v->reg) | simple_regs(v->cl);
    default:      return 0;
    }
}

/* Register effects of every mnemonic mutos_c1 emits. `dst` says which
 * explicit operands are written when they name a register (1 = the
 * first, 2 = both - XCHG); `implicit` lists fixed registers written
 * regardless of operands. A mnemonic missing from this table is a
 * gen_fatal() (see insn_writes()), so the table cannot silently fall
 * out of date as new instruction shapes are added. */
typedef struct {
    const char *mnem;
    unsigned    implicit;
    int         dst;
} InsnFx;

static const InsnFx INSN_FX[] = {
    { "mov",  0, 1 }, { "movb", 0, 1 }, { "lea", 0, 1 },
    { "add",  0, 1 }, { "sub",  0, 1 }, { "adc", 0, 1 }, { "sbb", 0, 1 },
    { "and",  0, 1 }, { "or",   0, 1 }, { "xor", 0, 1 }, { "not", 0, 1 },
    { "sal",  0, 1 }, { "sar",  0, 1 }, { "shl", 0, 1 },
    { "inc",  0, 1 }, { "dec",  0, 1 }, { "pop", 0, 1 },
    { "pop cx", RB_CX, 0 },        /* the literal-space quirk - OP_PLUS */
    { "xchg", 0, 2 },
    { "cbw",  RB_AX, 0 },
    { "cwd",  RB_DX, 0 },
    { "imul", RB_AX | RB_DX, 0 },  /* one-operand forms: DX:AX result */
    { "idiv", RB_AX | RB_DX, 0 },
    /* A call clobbers the caller-saved registers; DI/SI are callee-
     * saved (every function's SAVE prologue pushes them - docs/
     * MUTOS_C_ABI.md sect. 1.2), which is what lets a 'register'
     * local live in DI across calls. */
    { "call", RB_AX | RB_BX | RB_CX | RB_DX, 0 },
    { "push", 0, 0 }, { "cmp", 0, 0 }, { "jmp", 0, 0 }, { "seg", 0, 0 },
    { "blt",  0, 0 }, { "ble", 0, 0 }, { "bgt", 0, 0 }, { "bge", 0, 0 },
    { "beq",  0, 0 }, { "bne", 0, 0 }, { "blos", 0, 0 }, { "bhi", 0, 0 },
    { ".globl", 0, 0 }, { ".text", 0, 0 }, { ".even", 0, 0 },
    { ".bss",   0, 0 }, { ".data", 0, 0 }, { ".blkb", 0, 0 },
};

static unsigned insn_writes(const Insn *in)
{
    for (size_t i = 0; i < sizeof INSN_FX / sizeof INSN_FX[0]; i++) {
        const InsnFx *fx = &INSN_FX[i];
        if (strcmp(fx->mnem, in->mnem) != 0)
            continue;
        unsigned m = fx->implicit;
        if (fx->dst >= 1 && in->nops >= 1)
            m |= reg_bit(in->a.s);
        if (fx->dst >= 2 && in->nops >= 2)
            m |= reg_bit(in->b.s);
        return m;
    }
    gen_fatal("internal: mnemonic '%s' has no INSN_FX register-effect "
              "entry", in->mnem);
    return 0; /* unreached */
}

/* The guard itself - see this section's header. `regvar_store` is set
 * only for OP_ASSIGN's own store into a 'register' local. */
static void note_writes(GenState *g, unsigned mask, int regvar_store)
{
    if (!mask)
        return;
    for (int i = 0; i < g->valsp; i++)
        if (val_regs(&g->valstack[i]) & mask)
            g->valstack[i].clobbered = 1;

    unsigned hit = mask & g->reserved;
    if (!hit || regvar_store)
        return;
    /* Overwriting a 'register' local's register is only legitimate
     * while computing that same variable's new value: the statement's
     * assignment target - pushed first, so at the bottom of the value
     * stack - is the variable itself (e.g. 04_funcs/06_regclass.s.
     * golden's "i = i + 1;" -> "inc di"). The variable must then not
     * be READ again before that assignment completes (regvar_dirty). */
    const Val *bottom = g->valsp > 0 ? &g->valstack[0] : NULL;
    if (bottom && bottom->kind == VK_REG && (reg_bit(bottom->reg) & hit) == hit) {
        g->regvar_dirty |= hit;
        return;
    }
    gen_fatal("this statement would overwrite the 'register' variable held "
              "in %s while it is still live - not yet supported (no golden "
              "reference confirms which other register a real compiler "
              "uses here) - see docs/DEVLOG.md",
              reg_name(hit & (~hit + 1u)));
}

/* For a handler that has already popped `pending` and is about to
 * write `regs` before reading it - see this section's header. */
static void require_free(Val pending, unsigned regs, const char *ctx)
{
    unsigned hit = val_regs(&pending) & regs;
    if (hit)
        gen_fatal("%s: an operand held in %s would be overwritten before it "
                  "is used - not yet supported (no golden reference confirms "
                  "the spill/reordering a real compiler would emit) - see "
                  "docs/DEVLOG.md", ctx, reg_name(hit & (~hit + 1u)));
}

/* THE instruction-line formatter - see this section's header. */
static void put_insn_ex(GenState *g, const Insn *in, int regvar_store)
{
    note_writes(g, insn_writes(in), regvar_store);
    switch (in->nops) {
    case 0:  fprintf(g->out, "%s\n", in->mnem); break;
    case 1:  fprintf(g->out, "%s\t%s\n", in->mnem, in->a.s); break;
    default: fprintf(g->out, "%s\t%s,%s\n", in->mnem, in->a.s, in->b.s); break;
    }
}
static void put_insn(GenState *g, const Insn *in)
{
    put_insn_ex(g, in, 0);
}
static void ins0(GenState *g, const char *mnem)
{
    Insn i = insn0(mnem);
    put_insn(g, &i);
}
static void ins1(GenState *g, const char *mnem, Opnd a)
{
    Insn i = insn1(mnem, a);
    put_insn(g, &i);
}
static void ins2(GenState *g, const char *mnem, Opnd a, Opnd b)
{
    Insn i = insn2(mnem, a, b);
    put_insn(g, &i);
}
/* OP_ASSIGN's store into a 'register' local - the one write to a
 * reserved register that needs no exemption (see note_writes()). */
static void ins2_regvar_store(GenState *g, const char *mnem, Opnd a, Opnd b)
{
    Insn i = insn2(mnem, a, b);
    put_insn_ex(g, &i, 1);
}
/* Emits a NULL-mnem-terminated const Insn table. */
static void put_seq(GenState *g, const Insn *seq)
{
    for (; seq->mnem; seq++)
        put_insn(g, seq);
}
/* "L<n>:" - deliberately NO newline (see this section's header). */
static void put_label(GenState *g, int lab)
{
    fprintf(g->out, "L%d:", lab);
}
/* A free-form, newline-terminated line: "|"-comments ("| _a=-6.",
 * "|NREG 3", "|RTYP 0"), a label definition ("_main:"), or a jump-
 * table word ("L5"). */
static void put_line(GenState *g, const char *fmt, ...) PRINTF_LIKE(2, 3);
static void put_line(GenState *g, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g->out, fmt, ap);
    va_end(ap);
    fputc('\n', g->out);
}

/* -------------------------------------------------------------- */
/* Fixed instruction idioms - each confirmed byte-for-byte against the
 * goldens cited at its point of use below. Operand text is literal
 * here because none of these sequences has a variable part. */

/* The unconditional function prologue (OP_SAVE - docs/MUTOS_C_ABI.md
 * sect. 1.2), followed there by its own "|NREG" comment. */
static const Insn SEQ_PROLOGUE[] = {
    { "push", 1, {"bp"}, {""} },
    { "mov",  2, {"bp"}, {"sp"} },
    { "push", 1, {"di"}, {""} },
    { "push", 1, {"si"}, {""} },
    { NULL,   0, {""},   {""} }
};

/* A 32-bit result in DX(high):AX(low) - a runtime helper's or a long-
 * returning callee's return registers (sect. 1.5), or CWD's own output -
 * moved into the DI(high):SI(low) convention every 'long' value
 * producer in this file hands to its consumer (see VK_LONG). */
static const Insn SEQ_DXAX_TO_DISI[] = {
    { "mov", 2, {"di"}, {"dx"} },
    { "mov", 2, {"si"}, {"ax"} },
    { NULL,  0, {""},   {""} }
};

/* The reverse, for a 'long'-returning function's own "return" (OP_
 * RFORCE's TY_LONG case): DI(high):SI(low) -> AX(low):DX(high). */
static const Insn SEQ_DISI_TO_AXDX[] = {
    { "mov", 2, {"ax"}, {"si"} },
    { "mov", 2, {"dx"}, {"di"} },
    { NULL,  0, {""},   {""} }
};

/* A CWD-widened 'long' pushed as two words, low (AX) then high (DX) -
 * sect. 1.6's argument order (a 'long' call argument, or the in-range
 * 'long'-constant operand of a 'long' +/-). */
static const Insn SEQ_PUSH_AXDX[] = {
    { "push", 1, {"ax"}, {""} },
    { "push", 1, {"dx"}, {""} },
    { NULL,   0, {""},   {""} }
};

/* "mov ax,<src> / cwd" - sign-extends a 16-bit value into DX:AX, the
 * shared first half of every CWD-widening idiom in this file (followed
 * by SEQ_DXAX_TO_DISI or SEQ_PUSH_AXDX at the call site). */
static void emit_cwd_from(GenState *g, Opnd src)
{
    ins2(g, "mov", o_reg("ax"), src);
    ins0(g, "cwd");
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
static Val materialize_long(GenState *g, Val v)
{
    if (v.kind != VK_LCON)
        return v;
    long lo = v.imm, hi = v.offset;
    if (hi == (lo < 0 ? -1 : 0)) {
        emit_cwd_from(g, o_imm(lo));
        put_seq(g, SEQ_DXAX_TO_DISI);
    } else {
        ins2(g, "mov", o_reg("si"), o_imm(lo));
        ins2(g, "mov", o_reg("di"), o_imm(hi));
    }
    return val_long();
}

static void load_into_di(GenState *g, Val v); /* forward decl - defined
                                               * below, needed by
                                               * emit_cmp_and_branch()
                                               * above its own definition */

/* -------------------------------------------------------------- */
/* Relational/logical codegen - see the VK_COND field comment above
 * for the deferred-materialization design this implements. All of
 * it is reverse-engineered byte-for-byte against
 * tests/mutos_cc/01_expr/03_rellogic.s.golden. */

/* One row per relational/equality opcode: the branch-if-true
 * mnemonic (confirmed against 03_rellogic.s.golden), the opcode of
 * the NEGATED relation (see cond_invert() below), and the one that
 * holds with the two operands EXCHANGED (v7/cc/c10.c's maprel[] - see
 * constant_to_right()). */
typedef struct {
    int         op;
    const char *branch;
    int         inverse;
    int         mirror;
} RelOp;

static const RelOp RELOPS[] = {
    { OP_LESS,    "blt", OP_GREATEQ, OP_GREAT   },
    { OP_LESSEQ,  "ble", OP_GREAT,   OP_GREATEQ },
    { OP_GREAT,   "bgt", OP_LESSEQ,  OP_LESS    },
    { OP_GREATEQ, "bge", OP_LESS,    OP_LESSEQ  },
    { OP_EQUAL,   "beq", OP_NEQUAL,  OP_EQUAL   },
    { OP_NEQUAL,  "bne", OP_EQUAL,   OP_NEQUAL  },
};

/* NULL if `op` is not a relational/equality opcode. */
static const RelOp *find_relop(int op)
{
    for (size_t i = 0; i < sizeof RELOPS / sizeof RELOPS[0]; i++)
        if (RELOPS[i].op == op)
            return &RELOPS[i];
    return NULL;
}

/* A constant operand goes to the RIGHT - what v7/cc's c1 does before
 * choosing any code, in two places: c12.c's acommute() re-sorts the
 * operands of a commutative operator (+ * & | ^) by descending
 * degree(), and a constant's degree (-3) is below every other
 * operand's; c12.c's optim() exchanges a relational's operands when
 * degree(left) < degree(right) - always so for a constant on the left
 * - and maps the operator through maprel[] ("7 < x" -> "x > 7").
 * mutos_c0 used to do this itself, by accident and for EVERY operator
 * (it wrote a pending constant left operand after the right one - a
 * bug for - / % << >> < ...; see docs/DEVLOG.md); it now writes v7's
 * source order, so c1 does v7's canonicalization. A constant emits no
 * code (VK_IMM/VK_LCON), so exchanging the two values here is exactly
 * the same as having received them the other way round - the output
 * for every commutative or equality operator with a constant left
 * operand is what it was before that c0 fix. The general relational
 * swap by degree, for two non-constant operands, is done in OP_LESS...
 * for the case where it only changes the comparison's direction - a
 * NAME on the left, something computed on the right (10_integ/
 * 02_bubsort's "i < n - 1"); see is_name_val(). */
static int is_const_val(const Val *v)
{
    return v->kind == VK_IMM || v->kind == VK_LCON;
}

/* A NAME leaf in v7's tree: a variable in memory (a local, a parameter,
 * a static, or an element at a constant index - v7's optim() folds
 * "*(&v + 4)" into a NAME with an offset) or a 'register' local. */
static int is_name_val(const Val *v)
{
    return v->kind == VK_MEM || v->kind == VK_STATIC ||
           (v->kind == VK_REG && v->regvar);
}

/* A computed int value: in a register (not a 'register' local's own) or
 * behind one (a dereference). */
static int is_computed_val(const Val *v)
{
    return (v->kind == VK_REG && !v->regvar) || v->kind == VK_IND;
}

/* For a commutative operator: exchanges a constant left operand with
 * a non-constant right one. */
static void constant_to_right(Val *l, Val *r)
{
    if (is_const_val(l) && !is_const_val(r)) {
        Val t = *l;
        *l = *r;
        *r = t;
    }
}

static const char *cond_true_mnem(int op)
{
    const RelOp *r = find_relop(op);
    if (!r)
        gen_fatal("internal: unknown relational op %d", op);
    return r->branch;
}

/* The branch-condition-code negation used to short-circuit the
 * non-final operand(s) of an "&&" chain (jump to the overall
 * false label when an early conjunct is false, i.e. when its
 * INVERSE condition holds) - confirmed via 03_rellogic's "a < b"
 * rendering as "bge" (LESS's inverse) when it is LOGAND's first
 * operand, vs. plain "blt" when it appears standalone. */
static int cond_invert(int op)
{
    const RelOp *r = find_relop(op);
    if (!r)
        gen_fatal("internal: unknown relational op %d for inversion", op);
    return r->inverse;
}

/* Emits "cmp\t<cl>,<cr-or-di>\n" followed by a branch to L<target>
 * using `branch_op_code`'s mnemonic (the caller passes either
 * cond.true_op directly, for a "branch if true" site, or
 * cond_invert(cond.true_op), for a "branch if false" site - see
 * gen_cond_branch()). The right-hand side is loaded into DI first if
 * it is itself a memory operand (8086 CMP cannot take two memory
 * operands); an immediate right-hand side is rendered directly via
 * render_cmp_imm(), matching 03_rellogic's "cmp *-6.(bp),di" (memory
 * rhs) vs. "cmp *-6.(bp),*0" (immediate rhs) shapes exactly. */
static void emit_cmp_and_branch(GenState *g, Val cond, int branch_op_code, int target_lab)
{
    Val l = val_from_simple(cond.cl);
    Val r = val_from_simple(cond.cr);
    Opnd rhs;
    /* A 'long' comparison has its own shape (gen_long_cmp(), reached
     * only from gen_cond_branch()); rendered here it would print an
     * operand placeholder as if it were code - as a value-context
     * "z = l > 0L;" did until 2026-09-24. */
    if (cond.cond_is_long)
        gen_fatal("a 'long' comparison used as a value (not as an 'if'/"
                  "'while'/'for' condition) is not yet supported - see "
                  "src/mutos_cc/README.md");
    /* An ADDRESS operand ("p == &x", VK_MEM_DIRECT - OP_AMPER's deferred
     * "lea") has no golden; rendered as a memory operand it compared the
     * variable's CONTENTS instead ("mov di,*-6.(bp)" / "cmp *-10.(bp),di"
     * - silently wrong, found 2026-09-26; it predates this function's
     * other shapes). */
    if (l.kind == VK_MEM_DIRECT || r.kind == VK_MEM_DIRECT)
        gen_fatal("comparing with the address of a local variable or array "
                  "is not yet supported - see src/mutos_cc/README.md");
    /* CMP takes neither an immediate first operand nor two memory
     * operands. A left operand already computed into a register - or
     * behind one, a dereference, loaded in place first ("mov di,(di)",
     * the load every dereferenced operand gets) - is compared with the
     * right one directly, whatever it is: 10_integ/02_bubsort.s.golden's
     * "cmp di,*-6.(bp)" (a variable), "cmp di,*2.(si)" (an element) -
     * v7's template computes the left operand into a register and
     * compares it with an addressable right one. Otherwise the confirmed
     * repair - a memory right operand loaded into DI ("lo >= hi" ->
     * "mov di,*8.(bp)" / "cmp *6.(bp),di", 10_integ/04_strrev) - covers
     * every memory kind alike (a local, a constant-index element, a
     * static); a constant, or a memory operand compared with a
     * dereference ("c < *p") - on the left, a shape v7's operand swap
     * (see OP_LESS...) never leaves - is refused - before this, both
     * were emitted as-is and only mutos_as rejected them (found by
     * assembling fuzzed programs). A dereference compared with a
     * constant stays a memory operand ("cmp *16.(di),*0" - tests/
     * mutos_as/kernel_nonopt/lp_AC.s). */
    if (l.kind == VK_IND && r.kind != VK_IMM) {
        require_free(r, reg_bit(l.reg), "comparison");
        ins2(g, "mov", o_reg(l.reg), o_val(l));
        l = val_reg(l.reg);
    }
    if (l.kind == VK_REG) {
        ins2(g, "cmp", o_val(l), r.kind == VK_IMM ? o_cmpimm(r.imm) : o_val(r));
        ins1(g, cond_true_mnem(branch_op_code), o_lab(target_lab));
        return;
    }
    int l_mem = (l.kind == VK_MEM || l.kind == VK_MEM_CVT ||
                 l.kind == VK_MEM_DIRECT || l.kind == VK_STATIC ||
                 l.kind == VK_IND);
    if (l.kind == VK_IMM || l.kind == VK_STATICADDR || l.kind == VK_FUNCADDR ||
        (r.kind == VK_IND && l_mem))
        gen_fatal("this comparison's operand shape (a constant on the left, "
                  "or a dereference compared with another memory operand) is "
                  "not yet supported - see src/mutos_cc/README.md");
    if (r.kind == VK_MEM || r.kind == VK_MEM_CVT ||
        r.kind == VK_MEM_DIRECT || r.kind == VK_STATIC) {
        require_free(l, RB_DI, "comparison");
        load_into_di(g, r);
        rhs = o_reg("di");
    } else if (r.kind == VK_IMM) {
        rhs = o_cmpimm(r.imm);
    } else {
        rhs = o_val(r);
    }
    ins2(g, "cmp", o_val(l), rhs);
    ins1(g, cond_true_mnem(branch_op_code), o_lab(target_lab));
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
static void gen_long_cmp(GenState *g, int op, SimpleVal l,
                          SimpleVal r, int cond_sense, int target_lab)
{
    if (op != OP_GREAT || l.kind != VK_MEM || r.kind != VK_LCON ||
        r.imm != 0 || r.offset != 0 || cond_sense != 0)
        gen_fatal("this 'long' relational comparison shape is not yet "
                  "supported (only 'longvar > 0L' as an 'if'/'while'/"
                  "'for' condition is confirmed - see docs/DEVLOG.md)");

    int true_lab = g->next_lab++;
    /* High word: CMP's own "*0" shape (render_cmp_imm()); low word: the
     * ordinary "*0." immediate - exactly as 01_addsub.s.golden has it. */
    ins2(g, "cmp", o_mem(l.offset), o_cmpimm(0));
    ins1(g, "blt", o_lab(target_lab));
    ins1(g, "bgt", o_lab(true_lab));
    ins2(g, "cmp", o_mem(l.offset + MCC_SZINT), o_imm(0));
    ins1(g, "blos", o_lab(target_lab));
    put_label(g, true_lab);
}

static Val gen_logval(GenState *g, int ltrue); /* see "Conditional
                                                 * evaluation" below */

/* Materializes a single deferred comparison into a real 0/1 value in
 * DI - the "standalone relational" pattern (e.g. plain "r = a < b;"):
 * branch-if-true to a fresh label, then gen_logval()'s "set DI=0 and
 * jump past, or set DI=1 at the true label" (labels printed with no
 * trailing newline, matching OP_LABEL's style, so whatever the caller
 * emits next glues onto the same source line - confirmed against
 * "L10001:mov\t*-10.(bp),di" in the golden). */
static void materialize_cond(GenState *g, Val cond)
{
    int ltrue = g->next_lab++;
    emit_cmp_and_branch(g, cond, cond.true_op, ltrue);
    (void)gen_logval(g, ltrue);
}

/* No-op for anything already resolved; materializes a deferred
 * VK_COND into VK_REG("di"). Called wherever a Val is about to be
 * consumed as an ordinary value (ASSIGN's rhs, RFORCE's operand, and
 * defensively by every other binary/unary operator below, in case a
 * future grammar extension ever feeds a comparison's result into
 * arithmetic). */
static Val materialize(GenState *g, Val v)
{
    if (v.kind != VK_COND)
        return v;
    materialize_cond(g, v);
    return val_reg("di");
}

/* materialize(), applied to a value WHILE it stays on the value
 * stack - see pop_operands(). */
static void materialize_slot(GenState *g, int idx)
{
    if (g->valstack[idx].kind != VK_COND)
        return;
    if (g->valstack[idx].clobbered)
        fatal_clobbered();
    Val m = materialize(g, g->valstack[idx]);
    g->valstack[idx] = m;
}

/* Pops a binary operator's two operands (right on top), materializing
 * each first - right, then left: the same emission order as popping
 * and materializing them one at a time, but done IN PLACE, so while
 * the left operand's comparison writes DI the already-materialized
 * right operand is still on the stack where note_writes() can see it
 * (e.g. "(a < b) + (c < d)", whose two 0/1 results both land in DI). */
static void pop_operands_ex(GenState *g, Val *l, Val *r, int allow)
{
    if (g->valsp < 2)
        gen_fatal("expression stack underflow - malformed temp1 stream");
    materialize_slot(g, g->valsp - 1);
    materialize_slot(g, g->valsp - 2);
    *r = pop_val_ex(g, allow);
    *l = pop_val_ex(g, allow);
    /* A link-time-constant address (a string literal's or a
     * function's) is confirmed only as an assignment's rhs and as a
     * call argument; as an operand of arithmetic or a comparison
     * ("s == \"x\"", "\"abc\" + 1") no golden shows its shape. */
    if (l->kind == VK_STATICADDR || r->kind == VK_STATICADDR ||
        l->kind == VK_FUNCADDR || r->kind == VK_FUNCADDR)
        gen_fatal("the address of a string literal or function used as "
                  "an arithmetic or comparison operand is not yet "
                  "supported - see src/mutos_cc/README.md");
}

static void pop_operands(GenState *g, Val *l, Val *r)
{
    pop_operands_ex(g, l, r, 0);
}

/* Wraps a non-VK_COND value as an implicit "!= 0" truth test - the
 * condition any plain value stands for when a branch tests it (an
 * "if"/"while" condition, an operand of "&&"/"||"/"!", a "?:"
 * condition). Never emits code - purely a description of a
 * comparison to be emitted later by whoever consumes it. */
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

/* Emits "mov\tdi,<v>\n" to load `v` into DI - the confirmed generic
 * "working register" for PLUS/MINUS and for RFORCE's initial step -
 * except when `v` is already sitting in DI, in which case there is
 * nothing to do (not itself exercised by any current golden, but a
 * direct, low-risk consequence of the same confirmed pattern: never
 * emit a no-op "mov di,di"). */
static void load_into_di(GenState *g, Val v)
{
    if (v.kind == VK_REG && strcmp(v.reg, "di") == 0)
        return;
    /* A VK_MEM_DIRECT is an ADDRESS, whose value is loaded with "lea" -
     * as OP_ASSIGN ("p = &x;") and gen_call() do; "mov" would load the
     * variable's contents ("return &x;" returned x, silently, until
     * 2026-09-26). */
    ins2(g, v.kind == VK_MEM_DIRECT ? "lea" : "mov", o_reg("di"), o_val(v));
}

/* Same as load_into_di() above, but for SI - the fallback "working
 * register" for an otherwise-unresolved value while DI is reserved by
 * a live 'register'-class local (GenState's own reserved mask - see its
 * comment). */
static void load_into_si(GenState *g, Val v)
{
    if (v.kind == VK_REG && strcmp(v.reg, "si") == 0)
        return;
    ins2(g, v.kind == VK_MEM_DIRECT ? "lea" : "mov", o_reg("si"), o_val(v));
}

/* Emits "mov\tcx,<v>\n" to load `v` into CX - the confirmed working
 * register for a *variable* shift count specifically (never DI,
 * which holds the value being shifted - see OP_LSHIFT/OP_RSHIFT
 * below), skipping the no-op "mov cx,cx" case exactly like
 * load_into_di() above. Confirmed via 04_shift.s.golden's
 * "mov\tcx,*-8.(bp)" immediately before "sal\tdi,cl". */
static void load_into_cx(GenState *g, Val v)
{
    if (v.kind == VK_REG && strcmp(v.reg, "cx") == 0)
        return;
    ins2(g, "mov", o_reg("cx"), o_val(v));
}

/* Whether a subscript index about to be loaded and scaled must avoid
 * DI, taking SI instead: DI holds a value still needed - the array base
 * "lea"'d into it just before (01_arrbasic.s.golden's "lea di,*-14.
 * (bp)" / "mov si,*-16.(bp)" / "sal si,*1"), a constant-index address
 * that will be "lea"'d into it (VK_MEM_DIRECT on top), or any other
 * pending value (10_integ/02_bubsort.s.golden's "a[j] > a[j + 1]": the
 * left element's value in DI, the right one's address in SI). A
 * 'register' local's own NAME does not count - it is the assignment
 * target a statement computes into (see note_writes()). */
static int di_busy(const GenState *g)
{
    if (g->valsp >= 1 && g->valstack[g->valsp - 1].kind == VK_MEM_DIRECT)
        return 1;
    for (int i = 0; i < g->valsp; i++)
        if (!g->valstack[i].regvar && (val_regs(&g->valstack[i]) & RB_DI))
            return 1;
    return 0;
}

/* Loads a VK_CHARX (a char in memory, read as an int - see its ValKind
 * comment): "movb ax,<mem>" / "cbw", the value then in AX. The one
 * confirmed way the real compiler turns a char into an int (09_abiprobe
 * frame goldens, 10_integ/04_strrev.s.golden's "return buf[0];", and
 * every "cbw" in tests/mutos_as/kernel_nonopt/). */
static Val load_charx(GenState *g, Val v)
{
    if (v.kind != VK_CHARX)
        gen_fatal("internal: load_charx() on a non-char value");
    ins2(g, "movb", o_reg("ax"), o_val(val_from_simple(v.cl)));
    ins0(g, "cbw");
    return val_reg("ax");
}

/* Queues instruction `in` to be emitted later by flush_deferred() -
 * see DEFERRED_MAX's comment on GenState. */
/* Shifts register `reg` by the constant `count` using `mnem`
 * ("sal"/"sar") - the two confirmed shapes described at
 * MCC_SHIFT_REPEAT_MAX's definition. Writes CX in the second shape
 * (the register-occupancy guard sees it through the emission layer
 * like any other write). */
static void emit_const_shift(GenState *g, const char *mnem,
                             const char *reg, long count)
{
    if (count < 0)
        gen_fatal("negative shift count in constant expression");
    if (count <= MCC_SHIFT_REPEAT_MAX) {
        for (long i = 0; i < count; i++)
            ins2(g, mnem, o_reg(reg), o_shift1());
        return;
    }
    ins2(g, "mov", o_reg("cx"), o_imm(count));
    ins2(g, mnem, o_reg(reg), o_reg("cl"));
}

/* log2 of `v` if it is an exact power of two >= 1, else -1. */
static int exact_log2(long v)
{
    int n = 0;
    if (v < 1)
        return -1;
    while ((v & 1) == 0) {
        v >>= 1;
        n++;
    }
    return v == 1 ? n : -1;
}

/* The column step of a complete 2-D subscript "m[i][j]": `row` is the
 * pending VK_ROWADDR (base address in DI, row index i, row size R),
 * `col` the pending VK_SCALED (column index j, element size E). The
 * sum "&m + i*R + j*E" is emitted the way v7/cc/c12.c's distrib()
 * rewrites it - "(i*(R/E) + j)*E", both multiplications shifts - in
 * SI, then added to the base:
 *
 *     mov si,i / sal si,*1 (x log2(R/E)) / add si,j /
 *     sal si,*1 (x log2(E)) / add di,si
 *
 * confirmed byte-for-byte against 05_arrptr/02_array2d.s.golden
 * ("int m[3][4]": R/E = 4, E = 2) and 10_integ/05_matmul.s.golden
 * ("int a[2][2]": R/E = 2). Only the factorable case distrib()
 * itself handles by division is accepted: R a multiple of E with a
 * power-of-two quotient of at least 2, and E a power of two. R == E
 * (a one-column array) takes distrib()'s OTHER branch, which also
 * swaps the operand order, and a non-power-of-two quotient needs a
 * real multiply - neither is confirmed by any golden, so both are
 * refused. Each shift follows emit_const_shift()'s confirmed repeat/
 * CL rule. */
static void gen_subscript_2d(GenState *g, Val row, Val col)
{
    long rs = row.imm, es = col.imm;
    int eshift = exact_log2(es);
    int qshift = (eshift >= 0 && es > 0 && rs % es == 0)
               ? exact_log2(rs / es) : -1;
    if (eshift < 0 || qshift < 1)
        gen_fatal("a 2-D array whose row size (%ld bytes) is not a "
                  "power-of-two multiple (at least 2) of its element "
                  "size (%ld bytes) is not yet supported - see "
                  "src/mutos_cc/README.md", rs, es);
    if (strcmp(row.reg, "di") != 0)
        gen_fatal("internal: 2-D subscript base expected in di");
    ins2(g, "mov", o_reg("si"), o_val(val_from_simple(row.cl)));
    emit_const_shift(g, "sal", "si", qshift);
    ins2(g, "add", o_reg("si"), o_val(val_from_simple(col.cl)));
    emit_const_shift(g, "sal", "si", eshift);
    ins2(g, "add", o_reg("di"), o_reg("si"));
}

static void queue_deferred(GenState *g, Insn in)
{
    if (g->ndeferred >= DEFERRED_MAX)
        gen_fatal("too many deferred postfix ++/-- fixups in one "
                  "statement (internal limit %d)", DEFERRED_MAX);
    g->deferred[g->ndeferred++] = in;
}

/* Emits the queued fixups from index `base` on, in queue order, and
 * drops them: the part of the queue that belongs to the statement or
 * conditionally evaluated region that is now ending (see
 * region_open()). */
static void flush_deferred_from(GenState *g, int base)
{
    for (int i = base; i < g->ndeferred; i++)
        put_insn(g, &g->deferred[i]);
    if (g->ndeferred > base)
        g->ndeferred = base;
}

static void flush_deferred(GenState *g)
{
    flush_deferred_from(g, 0);
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
 * queue_deferred() above) - to the enclosing statement's OP_EXPR, or,
 * inside a condition or a conditionally evaluated operand, to the end
 * of that region (see the "Conditional evaluation" section below). The
 * AFT result is flagged `postfix` for gen_cond_branch(). */
static Val gen_incdec(GenState *g, int op, Val lv, Val amt)
{
    if (lv.kind != VK_MEM)
        gen_fatal("'++'/'--' on a non-memory lvalue is not yet supported");
    if (amt.kind != VK_IMM)
        gen_fatal("internal: '++'/'--' amount did not resolve to a "
                  "constant");

    int is_incr = (op == OP_INCBEF || op == OP_INCAFT);
    Insn fixup = (amt.imm == 1)
        ? insn1(is_incr ? "inc" : "dec", o_val(lv))
        : insn2(is_incr ? "add" : "sub", o_val(lv), o_imm(amt.imm));

    if (op == OP_INCBEF || op == OP_DECBEF) {
        put_insn(g, &fixup);
        load_into_di(g, lv);
    } else {
        load_into_di(g, lv);
        queue_deferred(g, fixup);
        Val old = val_reg("di");
        old.postfix = 1;
        return old;
    }
    return val_reg("di");
}

/* -------------------------------------------------------------- */
/* Conditional evaluation: conditions, "&&", "||", "?:" and ",".
 *
 * v7/cc's code generator never produces a 0/1 value to test it: a
 * condition is compiled by cbranch() (v7/cc/c11.c) straight into
 * branches - "&&"/"||"/"!" recursively into "jumping code", anything
 * else by rcexpr(tree, cctab) plus one conditional branch. A value-
 * context "&&"/"||"/"!"/relational is cbranch() to a true label plus
 * "reg = 0 / jmp end / true: reg = 1 / end:", and "c ? a : b" is
 * cbranch(c, false, 0), a, "jmp end", "false:", b, "end:" (both in
 * c10.c's cexpr()). Every operand is generated AT ITS PLACE in that
 * structure, so C's short-circuit and "?:" rules hold, and code for an
 * operand C does not evaluate is never executed.
 *
 * mutos_c1 streams temp1 in postfix order, where all operands precede
 * their operator. The evaluation-order plan (see that section) fixes
 * this: plan_expression() lays such an expression out as operand
 * ranges interleaved with the steps below, in v7's order. Before that
 * (up to 2026-09-24), the operands' code was emitted ahead of the
 * branches - silently wrong once an operand had a side effect ("z = x ?
 * y++ : 4;" incremented y whatever x was) - and a condition's postfix
 * "++"/"--" was flushed at the next OP_EXPR, inside whichever branch
 * came first ("if (n++ > 9)" incremented n only when the test held).
 *
 * Confirmed shapes. The structure of every value-context form is the
 * one 03_rellogic.s.golden ("&&", "||", "!") and 07_ternary.s.golden
 * ("?:") show; jumping code for "||" in an "if" condition is 10_integ/
 * 01_wordcount.s.golden's "cmpb ...,*32. / beq L10000 / <the second
 * operand's code> / cmpb ...,*10. / bne L9 / L10000:" (v7's LOGOR with
 * cond 0). A postfix "++"/"--" in a condition is the real non-optimized
 * compiler output in tests/mutos_as/kernel_nonopt/ - five instances:
 * "mov R,n / dec n / or R,R / beq L" (a truth test: lp_AC.s twice,
 * sys1.s), "... / or R,R / ble L" (compared with 0: lp_AC.s) and "mov
 * R,n / dec n / cmp R,*2. / blt L" (compared with 2: sys1.s) - the
 * fixup immediately after the load, BEFORE the test (v7's rcexpr() does
 * not delay() a postfix operator under cctab; the regtab fallback's
 * "tst r" is MUTOS's "or r,r"). R is DI in a function without register
 * variables (kernel_opt/delay.s, "while (d--)"). How the fixups are
 * placed:
 *
 * - A condition or conditionally evaluated operand is a REGION
 *   (region_open()/region_close()): fixups its own postfix operators
 *   queue are emitted at its end - for a condition before its compare,
 *   for a "?:" arm before the jump that leaves it, for a comma
 *   operator's left operand before its right one (v7 evaluates that
 *   operand with efftab, where delay() does apply). Fixups queued
 *   before the region stay queued: they belong to every path.
 * - gen_call() emits the current region's fixups before its "call" -
 *   a call is a sequence point, and the real compiler increments an
 *   argument right after pushing it (kernel_nonopt/sys1.s:
 *   "clearseg(a++)" -> "push *-56.(bp) / inc *-56.(bp) / call
 *   _clearse").
 */

/* A region's record is two plan slots: [0] the deferred-queue index at
 * its start, [1] the enclosing region's floor. */
static void region_open(GenState *g, int *rec)
{
    rec[0] = g->ndeferred;
    rec[1] = g->defer_floor;
    g->defer_floor = g->ndeferred;
}

static void region_close(GenState *g, const int *rec)
{
    flush_deferred_from(g, rec[0]);
    g->defer_floor = rec[1];
}

/* One condition, cbranch()'s leaf case: branch to L<lbl> if the truth
 * of `v` equals `cond_sense` (v7's "cond" - 1: branch if true, 0:
 * branch if false; see OP_CBRANCH). Postfix fixups queued at or after
 * `base` - this condition's own - are emitted first: the branch reads
 * the flags, so they cannot come between the compare and the branch.
 * The operand itself was loaded before them, so the compare sees its
 * old value. A postfix operand's own value tested for truth or
 * compared with 0 gets "or reg,reg" (see this section's header);
 * everything else is emit_cmp_and_branch()'s ordinary shape. */
static void gen_cond_branch(GenState *g, Val v, int lbl, int cond_sense, int base)
{
    Val c = as_cond(v);
    if (c.cond_is_long) {
        if (g->ndeferred > base)
            gen_fatal("a postfix '++'/'--' in a 'long' comparison is not yet "
                      "supported - see src/mutos_cc/README.md");
        gen_long_cmp(g, c.true_op, c.cl, c.cr, cond_sense, lbl);
        return;
    }
    int branch_op = cond_sense ? c.true_op : cond_invert(c.true_op);
    flush_deferred_from(g, base);
    if (c.cl.kind == VK_REG && c.cl.postfix &&
        c.cr.kind == VK_IMM && c.cr.imm == 0) {
        ins2(g, "or", o_reg(c.cl.reg), o_reg(c.cl.reg));
        ins1(g, cond_true_mnem(branch_op), o_lab(lbl));
        return;
    }
    emit_cmp_and_branch(g, c, branch_op, lbl);
}

/* The tail of a value-context "&&"/"||"/"!"/relational: the condition
 * has branched to L<ltrue> when true and falls through when false -
 * v7's czero/cone (c10.c's cexpr()), confirmed by 03_rellogic.s.golden:
 *   mov di,*0. / jmp Lend / Ltrue: mov di,*1. / Lend:
 * Lend is allocated here, after the condition's own labels, as v7's
 * "label(isn++)" does. The value is left in DI. */
static Val gen_logval(GenState *g, int ltrue)
{
    int lend = g->next_lab++;
    ins2(g, "mov", o_reg("di"), o_imm(0));
    ins1(g, "jmp", o_lab(lend));
    put_label(g, ltrue);
    ins2(g, "mov", o_reg("di"), o_imm(1));
    put_label(g, lend);
    return val_reg("di");
}

/* A "?:" arm's value, just generated, into DI - unconditionally, even
 * when DI may already hold it: 07_ternary.s.golden reloads "mov di,
 * *-8.(bp)" at the false label although the compare's own set-up had
 * just put that very value there. */
static void gen_arm_load(GenState *g)
{
    Val v = pop_val(g);
    switch (v.kind) {
    case VK_IMM: case VK_MEM: case VK_MEM_CVT: case VK_STATIC:
    case VK_REG: case VK_IND:
        load_into_di(g, v);
        return;
    case VK_COND:
        gen_fatal("a '?:' branch that is itself a bare relational "
                  "comparison is not yet supported");
    default:
        gen_fatal("this '?:' branch value is not yet supported - see "
                  "src/mutos_cc/README.md");
    }
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
static Val gen_long_binop_call(GenState *g, Val l, Val r, const char *helper)
{
    if (l.kind != VK_MEM || r.kind != VK_MEM)
        gen_fatal("'long' %s with a non-memory operand is not yet "
                  "supported", helper);
    /* Push order r_low, r_high, l_low, l_high - see above. */
    const int words[4] = { r.offset + MCC_SZINT, r.offset,
                           l.offset + MCC_SZINT, l.offset };
    for (int i = 0; i < 4; i++) {
        ins2(g, "mov", o_reg("di"), o_mem(words[i]));
        ins1(g, "push", o_reg("di"));
    }
    ins1(g, "call", o_sym(helper));
    ins2(g, "add", o_reg("sp"), o_imm(8));
    put_seq(g, SEQ_DXAX_TO_DISI);
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
/* Pushes one call argument (see gen_call()'s comment above for every
 * confirmed shape) and returns how many words it occupies on the
 * machine stack - 2 for a 'long', else 1. Shared by gen_call()'s
 * right-to-left loop and the evaluation-order plan's SEG_PUSHARG step
 * (plan_call()), which pushes each argument as soon as it is computed. */
static int push_call_arg(GenState *g, Val v)
{
    /* A comparison used as an argument ("f(a < b)") is a 0/1 value in
     * DI first - it used to reach the push below unmaterialized and be
     * rendered as placeholder text. */
    v = materialize(g, v);
    if (v.kind == VK_LCON) {
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
        long lo = v.imm, hi = v.offset;
        if (hi == (lo < 0 ? -1 : 0)) {
            emit_cwd_from(g, o_imm(lo));
            put_seq(g, SEQ_PUSH_AXDX);
        } else {
            ins2(g, "mov", o_reg("di"), o_imm(lo));
            ins1(g, "push", o_reg("di"));
            ins2(g, "mov", o_reg("di"), o_imm(hi));
            ins1(g, "push", o_reg("di"));
        }
        return 2;
    }
    if (v.kind == VK_LONG)
        gen_fatal("a 'long' argument already materialized into DI:SI "
                  "is not yet supported as a call argument - see "
                  "src/mutos_cc/README.md");
    if (v.kind == VK_MEM_DIRECT) {
        /* A deferred address-of argument ("sumarr(v, 4);" - `v`
         * decaying to its address - see OP_AMPER's own comment
         * for why this is deferred at all, specifically to reach
         * this point rather than clobbering DI before its turn)
         * - materialized right here, at its own push, via "lea" -
         * confirmed against 04_ptrarreq.s.golden's "lea\tdi,
         * *-12.(bp)" immediately followed by "push\tdi" (with the
         * OTHER argument's own "mov di,*4."/"push di" already
         * emitted first, since arguments push right-to-left). */
        ins2(g, "lea", o_reg("di"), o_val(v));
        ins1(g, "push", o_reg("di"));
        return 1;
    }
    if (v.kind == VK_IMM || v.kind == VK_STATICADDR || v.kind == VK_FUNCADDR) {
        /* An immediate goes through DI - plain 8086 PUSH has no
         * immediate form (01_call.s.golden's "mov di,*4." / "push
         * di"). A string literal's address is one too:
         * 05_arrptr/07_strlibc.s.golden's "strcpy(src, \"hello,
         * mutos\")" -> "mov di,#L4" / "push di". A function's
         * address takes the same path for the same reason (not
         * itself shown by a golden; before this, it was pushed as
         * "push #_f", which the 8086 cannot encode). */
        load_into_di(g, v);
        ins1(g, "push", o_reg("di"));
        if (v.kind == VK_FUNCADDR)
            free((char *)v.reg);
        return 1;
    }
    if (v.kind == VK_IND) {
        /* A dereferenced value ("strlen(names[i])"): loaded into
         * its own address register first, then pushed from there -
         * 05_arrptr/05_arrofptr.s.golden's "mov di,(di)" / "push
         * di", not the equally legal one-instruction "push (di)"
         * - the same in-place load a dereferenced '+' operand and
         * an assignment's dereferenced rhs get (01_arrbasic,
         * 03_ptrbasic). */
        ins2(g, "mov", o_reg(v.reg), o_val(v));
        ins1(g, "push", o_reg(v.reg));
        return 1;
    }
    ins1(g, "push", o_val(v));
    return 1;
}

/* The call itself, once every argument is on the machine stack: the
 * postfix fixups due first, the "call", the argument words popped
 * again, and the result's Val. Shared by gen_call() and the plan's
 * SEG_CALL step. */
static Val finish_call(GenState *g, Val callee, int nwords, int is_long_ret)
{
    if (callee.kind != VK_FUNC && callee.kind != VK_MEM)
        gen_fatal("this call-callee shape is not yet supported - see "
                  "src/mutos_cc/README.md");

    /* A call is a sequence point: postfix fixups queued while its
     * arguments were computed are done before it - the real compiler
     * increments an argument right after pushing it (kernel_nonopt/
     * sys1.s: "clearseg(a++)" -> "push *-56.(bp) / inc *-56.(bp) / call
     * _clearse"). Only the current region's (see region_open()): a
     * fixup from outside a conditionally evaluated operand the call
     * sits in must still happen on every path. */
    flush_deferred_from(g, g->defer_floor);

    if (callee.kind == VK_FUNC) {
        ins1(g, "call", o_sym(callee.reg));
        free((char *)callee.reg);
    } else {
        ins1(g, "call", o_indirect(o_val(callee)));
    }

    if (nwords > 0)
        ins2(g, "add", o_reg("sp"), o_imm(2L * nwords));
    if (is_long_ret) {
        /* A 'long'-returning callee's result comes back in DX:AX
         * (sect. 1.5's ordinary long-return convention - same as
         * gen_long_binop_call()'s own lmul/ldiv/lrem helper calls
         * above), moved into the DI(high):SI(low) convention every
         * other long-value producer here uses - confirmed against
         * 02_long/03_retval.s.golden's "call _addlong / add sp,*8. /
         * mov di,dx / mov si,ax". */
        put_seq(g, SEQ_DXAX_TO_DISI);
        return val_long();
    }
    return val_reg("ax");
}

static Val gen_call(GenState *g, Val callee, Val args, int is_long_ret)
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
        /* Arguments are pushed right to left, so items[0..i-1] are
         * still waiting in wherever they were computed - none of them
         * may live in a register this argument's own push sequence
         * (push_call_arg()) writes first. (An argument list whose
         * earlier arguments have code of their own is generated
         * through an evaluation-order plan instead - see plan_call() -
         * so this only ever meets values that are still where their
         * NAME/CON/AMPER left them, plus at most a computed last
         * argument.) */
        unsigned w = 0;
        if (items[i].kind == VK_LCON)
            w = (items[i].offset == (items[i].imm < 0 ? -1 : 0))
                ? (RB_AX | RB_DX) : RB_DI;
        else if (items[i].kind == VK_IMM || items[i].kind == VK_MEM_DIRECT ||
                 items[i].kind == VK_STATICADDR || items[i].kind == VK_FUNCADDR ||
                 items[i].kind == VK_COND)
            w = RB_DI;
        else if (items[i].kind == VK_IND)
            w = reg_bit(items[i].reg);
        for (int j = 0; j < i; j++)
            require_free(items[j], w, "call argument");
        nwords += push_call_arg(g, items[i]);
    }
    if (args.kind == VK_ARGLIST)
        free(args.arglist);
    return finish_call(g, callee, nwords, is_long_ret);
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
static void gen_switch_dispatch(GenState *g, int deflab,
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

    ins2(g, "sub", o_reg("ax"), o_fmt("#/%lX", (unsigned long)((uint16_t)min)));
    ins2(g, "cmp", o_reg("ax"), o_imm(range));
    ins1(g, "bhi", o_lab(deflab));
    ins2(g, "shl", o_reg("ax"), o_fmt("#1")); /* '#', no '.' - as confirmed */
    ins2(g, "xchg", o_reg("bx"), o_reg("ax"));
    ins1(g, "seg", o_reg("cs"));
    (void)g->next_lab++; /* confirmed-but-unexplained burned label -
                           * see the derivation comment above. */
    int table_lab = g->next_lab++;
    ins1(g, "jmp", o_indirect(o_fmt("L%d(bx)", table_lab)));
    put_label(g, table_lab);
    for (int i = 0; i < ncases; i++)
        put_line(g, "L%d", cases[i].lab); /* one table word per line */
}

/* Per-opcode table for the arithmetic/bitwise/shift operators and
 * their compound-assignment forms: the wire opcode's name (as used in
 * "not yet supported" diagnostics), the 8086 mnemonic implementing the
 * 16-bit operation (NULL where it is not a single instruction), and -
 * for PLUS/MINUS only - the high-word partner a 32-bit 'long' add/
 * subtract chains the carry/borrow into (see OP_PLUS's TY_LONG case).
 * Indexed directly by opcode (every opcode fits in one byte - see
 * c1_read_op()); aluop() gen_fatal()s on an opcode with no entry. */
typedef struct {
    const char *name;
    const char *mnem;
    const char *mnem_hi;
} AluOp;

static const AluOp ALUOPS[256] = {
    [OP_PLUS]    = { "PLUS",    "add", "adc" },
    [OP_MINUS]   = { "MINUS",   "sub", "sbb" },
    [OP_AND]     = { "AND",     "and", NULL  },
    [OP_OR]      = { "OR",      "or",  NULL  },
    [OP_EXOR]    = { "EXOR",    "xor", NULL  },
    [OP_LSHIFT]  = { "LSHIFT",  "sal", NULL  },
    [OP_RSHIFT]  = { "RSHIFT",  "sar", NULL  },
    [OP_DIVIDE]  = { "DIVIDE",  NULL,  NULL  },
    [OP_MOD]     = { "MOD",     NULL,  NULL  },
    [OP_LOGAND]  = { "LOGAND",  NULL,  NULL  },
    [OP_LOGOR]   = { "LOGOR",   NULL,  NULL  },
    [OP_ASPLUS]  = { "ASPLUS",  "add", NULL  },
    [OP_ASMINUS] = { "ASMINUS", "sub", NULL  },
    [OP_ASSAND]  = { "ASSAND",  "and", NULL  },
    [OP_ASOR]    = { "ASOR",    "or",  NULL  },
    [OP_ASXOR]   = { "ASXOR",   "xor", NULL  },
    [OP_ASLSH]   = { "ASLSH",   "sal", NULL  },
    [OP_ASRSH]   = { "ASRSH",   "sar", NULL  },
    [OP_ASDIV]   = { "ASDIV",   NULL,  NULL  },
    [OP_ASMOD]   = { "ASMOD",   NULL,  NULL  },
};

static const AluOp *aluop(int op)
{
    if (op < 0 || op > 255 || ALUOPS[op].name == NULL)
        gen_fatal("internal: no ALU-op table entry for opcode %d", op);
    return &ALUOPS[op];
}

/* -------------------------------------------------------------- */
/* Consumer lookahead.
 *
 * mutos_c1 generates code opcode by opcode as it reads temp1, without
 * building a tree, but where an address is computed can depend on what
 * will happen to it LATER - OP_AMPER's "lea" is emitted eagerly for a
 * subscript base and deferred otherwise (see its handler). The first
 * version of that decision peeked at ONE opcode ("is the next one a
 * NAME?"), which a string literal broke: in "strcpy(src, \"...\")"
 * the array "src" is followed by the literal's own NAME, yet it is a
 * call argument, not a subscript base (05_arrptr/07_strlibc.s.golden
 * pushes the literal first, then computes "lea di,src"). scan_consumer()
 * answers the real question instead - which operator will consume the
 * value just produced - by walking the following opcodes with a count
 * of the values they push and pop, then restoring temp1's position
 * (plain getc() reads on a seekable FILE*, so nothing else is
 * affected; temp1 is always a regular file, never a pipe). */

typedef enum { AR_LEAF, AR_UNARY, AR_BINARY, AR_STMT, AR_UNKNOWN } Arity;

typedef struct {
    int op;          /* the consuming opcode, or -1 if the scan gave up
                      * at an opcode it does not know */
    int type;        /* the consumer's type argument (an operator's) */
    int as_left;     /* binary consumer: 1 if the value is its LEFT
                      * operand (then everything scanned is the right
                      * operand), 0 if it is the RIGHT one */
    int prev_op;     /* the opcode read just before the consumer */
    int saw_runtime; /* a NAME leaf was read before the consumer, i.e.
                      * (for as_left) the right operand is not a
                      * compile-time constant */
    int nops;        /* how many opcodes were read before the consumer
                      * (for as_left: the right operand's own size) */
} Consumer;

/* Reads `op`'s arguments (the opcode tag itself is already read) and
 * classifies it by how many expression values it pops/pushes. Only the
 * opcodes that can appear inside an expression tree are known; any
 * other yields AR_UNKNOWN (and its arguments are NOT consumed - the
 * caller stops there). */
static Arity scan_op_args(FILE *t1, int op, int *type)
{
    *type = 0;
    switch (op) {
    case OP_NAME: {
        int hclass = c1_read_num(t1, "temp1");
        *type = c1_read_num(t1, "temp1");
        if (hclass == SC_EXTERN)
            free(c1_read_sym(t1, "temp1"));
        else
            (void)c1_read_num(t1, "temp1");
        return AR_LEAF;
    }
    case OP_CON:
        *type = c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        return AR_LEAF;
    case OP_LCON:
        *type = c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        return AR_LEAF;
    case OP_NULLOP:
        return AR_LEAF;
    case OP_AMPER: case OP_STAR: case OP_COMPL: case OP_EXCLA:
    case OP_NEG: case OP_LTOI: case OP_ITOC: case OP_CTOL: case OP_ITOL:
    case OP_RFORCE:
        *type = c1_read_num(t1, "temp1");
        return AR_UNARY;
    case OP_PLUS: case OP_MINUS: case OP_TIMES: case OP_DIVIDE:
    case OP_MOD: case OP_AND: case OP_OR: case OP_EXOR:
    case OP_LSHIFT: case OP_RSHIFT: case OP_ITOP:
    case OP_LESS: case OP_LESSEQ: case OP_GREAT: case OP_GREATEQ:
    case OP_EQUAL: case OP_NEQUAL: case OP_LOGAND: case OP_LOGOR:
    case OP_COLON: case OP_QUEST: case OP_SEQNC: case OP_COMMA:
    case OP_CALL: case OP_ASSIGN:
    case OP_ASPLUS: case OP_ASMINUS: case OP_ASTIMES: case OP_ASDIV:
    case OP_ASMOD: case OP_ASLSH: case OP_ASRSH: case OP_ASSAND:
    case OP_ASOR: case OP_ASXOR:
    case OP_INCBEF: case OP_DECBEF: case OP_INCAFT: case OP_DECAFT:
        *type = c1_read_num(t1, "temp1");
        return AR_BINARY;
    case OP_EXPR:
        (void)c1_read_num(t1, "temp1");
        return AR_STMT;
    case OP_CBRANCH:
        (void)c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        return AR_STMT;
    default:
        return AR_UNKNOWN;
    }
}

/* See this section's header. Call right after the value in question
 * was produced (its own opcode and arguments fully read). */
static Consumer scan_consumer(FILE *t1)
{
    Consumer c = { -1, 0, 0, -1, 0, 0 };
    long savepos = ftell(t1);
    if (savepos < 0)
        gen_fatal("internal: temp1 is not seekable (ftell failed)");
    int depth = 0, prev = -1;
    for (;;) {
        int type;
        int op = c1_read_op(t1, "temp1");
        Arity ar = scan_op_args(t1, op, &type);
        if (ar == AR_UNKNOWN)
            break;
        if (ar == AR_LEAF) {
            depth++;
            if (op == OP_NAME)
                c.saw_runtime = 1;
        } else if (ar == AR_UNARY && depth == 0) {
            c.op = op; c.type = type; c.as_left = 1;
            break;
        } else if (ar == AR_BINARY && depth <= 1) {
            c.op = op; c.type = type; c.as_left = (depth == 1);
            break;
        } else if (ar == AR_BINARY) {
            depth--;
        } else if (ar == AR_STMT) {
            c.op = op;
            break;
        }
        prev = op;
        c.nops++;
    }
    c.prev_op = prev;
    if (fseek(t1, savepos, SEEK_SET) != 0)
        gen_fatal("internal: temp1 is not seekable (fseek failed)");
    return c;
}

/* -------------------------------------------------------------- */
/* Evaluation order ("subtree replay").
 *
 * mutos_c1 generates code opcode by opcode in temp1's own postfix
 * order, which is always left operand first. The real compiler does
 * not: v7/cc's code tables choose, per operator, which operand to
 * evaluate first by how hard each one is. For '*' (v7/cc/table.s,
 * "cr42"), once neither operand is addressable and the right one no
 * longer fits in the registers left over (v7's dcalc() = 24, template
 * "%n,n"), the template is
 *
 *     SS          the RIGHT operand, computed onto the stack
 *     F           then the left one, into the working register
 *     mul (sp)+,R
 *
 * (v7/cc/c10.c's cexpr(): 'S' = the right subtree, a second 'S' =
 * through the stack table). 10_integ/05_matmul.s.golden is the MUTOS
 * version of exactly that, for "sum + a[i][k] * b[k][j]":
 *
 *     lea di,&b / ... / add di,si / mov di,(di) / push di     SS
 *     lea di,&a / ... / add di,si / mov di,(di)                F
 *     mov ax,di / pop cx / imul cx                              the multiply
 *
 * v7's acommute() keeps commutative operands of EQUAL degree in source
 * order (insert() only moves a strictly lower-degree term back), which
 * is why the right operand here is the source's own b[k][j].
 *
 * Because mutos_c1 streams, the left operand's code would already be
 * out by the time OP_TIMES is read. So at the first opcode of every
 * expression (a leaf, with the value stack empty) plan_expression()
 * pre-scans temp1 up to the expression's terminator (EXPR/CBRANCH),
 * using scan_op_args() - the same arity table scan_consumer() uses -
 * to index each node's byte range (postfix: every subtree is one
 * contiguous range). If an operator is to be evaluated right operand
 * first (order_right_first()), it builds a Plan - the order in which
 * to stream those ranges through the ORDINARY opcode handlers:
 *
 *     ... [right subtree] SPILL [left subtree] SWAP [the operator] ...
 *
 * SPILL (plan_spill()) pushes the right operand's value onto the
 * machine stack and leaves VK_STACKED in its place; SWAP puts the two
 * values back in left/right order for the operator's own handler.
 * Nothing else changes: each handler still reads its own opcode and
 * arguments where the plan seeked to, and its lookaheads (OP_AMPER's
 * scan_consumer(), OP_STAR's "&*" cancel, OP_PLUS's displacement
 * peek) still see temp1's real structure, because a subtree is
 * replayed whole. The same mechanism places the operands of "&&",
 * "||", "?:" and "," inside their branch structure, with steps that
 * emit the branches, labels and 0/1 values between the ranges - see
 * the "Conditional evaluation" section and plan_value()/
 * plan_cbranch(). An expression with neither gets no plan at all and
 * streams exactly as before.
 *
 * Only the golden-confirmed decision is taken (order_right_first()):
 * int TIMES whose two operands are both complete 2-D element reads.
 * v7's degree() gives a 1-D element ("a[i]") the same degree, so the
 * real compiler very probably spills "a[i] * b[j]" the same way - but
 * no golden shows it, so it stays refused (by the register-occupancy
 * guard, as before). 02_bubsort's swapped relational and 03_linklist's
 * right-hand-side-first store are the same mechanism with other
 * decisions (see docs/DEVLOG.md); neither is taken here. */

/* One node of a pre-scanned expression. */
typedef struct {
    int  op, type;
    long off;         /* its own opcode tag */
    long start;       /* the first opcode of its subtree */
    long end;         /* just past its own arguments */
    int  kid[2];      /* operand node indices, -1 for none; kid[0] is
                       * the left (or only) operand */
    int  right_first; /* evaluate kid[1] before kid[0] */
    int  planned;     /* this node or a descendant is right_first or a
                       * conditional-evaluation node (is_control()) - its
                       * subtree cannot simply be streamed */
} ENode;

typedef struct {
    ENode *v;
    int    n, cap;
} ETree;

static int etree_add(ETree *t, ENode nd)
{
    if (t->n == t->cap) {
        int cap = t->cap ? 2 * t->cap : 64;
        ENode *v = realloc(t->v, (size_t)cap * sizeof *v);
        if (!v)
            gen_fatal("out of memory");
        t->v = v;
        t->cap = cap;
    }
    t->v[t->n] = nd;
    return t->n++;
}

/* An expression's terminator, as prescan_expr() found it. */
typedef struct {
    int  op;          /* OP_EXPR or OP_CBRANCH */
    long off;         /* its own opcode tag */
    long after;       /* just past its arguments */
    int  lbl, cond;   /* OP_CBRANCH's label and sense */
} Term;

/* Indexes the expression starting at temp1's current position into
 * `t`, restoring the position afterwards. Returns the root's index,
 * or -1 when the scan reaches an opcode scan_op_args() does not know
 * or the stream does not form exactly one tree before its terminator
 * - then no plan is made, which only ever means "stream as before". */
static int prescan_expr(FILE *t1, ETree *t, Term *term)
{
    long savepos = ftell(t1);
    if (savepos < 0)
        gen_fatal("internal: temp1 is not seekable (ftell failed)");
    int *stk = NULL;
    int depth = 0, cap = 0, root = -1;
    for (;;) {
        long off = ftell(t1);
        int type = 0, lbl = 0, cond = 0;
        int op = c1_read_op(t1, "temp1");
        Arity ar;
        if (op == OP_CBRANCH) {
            lbl = c1_read_num(t1, "temp1");
            cond = c1_read_num(t1, "temp1");
            (void)c1_read_num(t1, "temp1");      /* source line */
            ar = AR_STMT;
        } else {
            ar = scan_op_args(t1, op, &type);
        }
        if (ar == AR_UNKNOWN)
            break;
        if (ar == AR_STMT) {
            if (depth == 1) {
                root = stk[0];
                term->op = op;
                term->off = off;
                term->after = ftell(t1);
                term->lbl = lbl;
                term->cond = cond;
            }
            break;
        }
        ENode nd = { op, type, off, off, ftell(t1), { -1, -1 }, 0, 0 };
        int need = (ar == AR_BINARY) ? 2 : (ar == AR_UNARY) ? 1 : 0;
        if (depth < need)
            break;
        if (need == 2) {
            nd.kid[0] = stk[depth - 2];
            nd.kid[1] = stk[depth - 1];
        } else if (need == 1) {
            nd.kid[0] = stk[depth - 1];
        }
        depth -= need;
        if (need > 0)
            nd.start = t->v[nd.kid[0]].start;
        if (depth == cap) {
            cap = cap ? 2 * cap : 32;
            int *s = realloc(stk, (size_t)cap * sizeof *s);
            if (!s)
                gen_fatal("out of memory");
            stk = s;
        }
        stk[depth++] = etree_add(t, nd);
    }
    free(stk);
    if (fseek(t1, savepos, SEEK_SET) != 0)
        gen_fatal("internal: temp1 is not seekable (fseek failed)");
    return root;
}

/* An ITOP node scaling a plain variable by a constant ("i * 4"). */
static int is_var_times_con(const ETree *t, const ENode *itop)
{
    return t->v[itop->kid[0]].op == OP_NAME && t->v[itop->kid[1]].op == OP_CON;
}

/* Node `i` reads one int element of a 2-D int array through two
 * runtime subscripts that are plain variables - exactly the shape
 * mutos_c0's emit_subscript_2d() writes for "m[i][j]":
 *
 *   STAR(0)
 *     PLUS(ptr)
 *       AMPER(8)                   the row, re-decayed ...
 *         STAR(0)                  ... after being dereferenced
 *           PLUS(ptr)
 *             AMPER(8) NAME m
 *             ITOP(104) NAME i CON <row size>
 *       ITOP(ptr) NAME j CON <element size>
 *
 * (whether each index NAME is a variable the 2-D codegen accepts is
 * still checked there, when the element is generated). */
static int is_2d_elem_read(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op != OP_STAR || n->type != TY_INT)
        return 0;
    const ENode *col = &t->v[n->kid[0]];
    if (col->op != OP_PLUS || !ty_is_ptr(col->type))
        return 0;
    const ENode *rowamp = &t->v[col->kid[0]];
    const ENode *colidx = &t->v[col->kid[1]];
    if (rowamp->op != OP_AMPER || rowamp->type != TY_PTR_INT ||
        colidx->op != OP_ITOP || !ty_is_ptr(colidx->type) ||
        !is_var_times_con(t, colidx))
        return 0;
    const ENode *rowstar = &t->v[rowamp->kid[0]];
    if (rowstar->op != OP_STAR || rowstar->type != TY_INT)
        return 0;
    const ENode *row = &t->v[rowstar->kid[0]];
    if (row->op != OP_PLUS || !ty_is_ptr(row->type))
        return 0;
    const ENode *base = &t->v[row->kid[0]];
    const ENode *rowidx = &t->v[row->kid[1]];
    return base->op == OP_AMPER && base->type == TY_PTR_INT &&
           t->v[base->kid[0]].op == OP_NAME &&
           rowidx->op == OP_ITOP && rowidx->type == TY_PTR_ARY_INT &&
           is_var_times_con(t, rowidx);
}

/* The evaluation-order decision - see this section's header. */
static int order_right_first(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    return n->op == OP_TIMES && n->type == TY_INT &&
           is_2d_elem_read(t, n->kid[0]) && is_2d_elem_read(t, n->kid[1]);
}

static void plan_add(Plan *p, Seg s)
{
    if (s.kind == SEG_RANGE) {
        if (s.start == s.end)
            return;
        if (p->n > 0 && p->seg[p->n - 1].kind == SEG_RANGE &&
            p->seg[p->n - 1].end == s.start) {
            p->seg[p->n - 1].end = s.end;
            return;
        }
    }
    if (p->n >= PLAN_MAX)
        gen_fatal("expression needs too many evaluation-order steps "
                  "(internal limit %d)", PLAN_MAX);
    p->seg[p->n++] = s;
}

static void plan_range(Plan *p, long start, long end)
{
    plan_add(p, (Seg){ SEG_RANGE, start, end, 0, 0, 0 });
}

static void plan_op(Plan *p, SegKind kind, int a, int b, int c)
{
    plan_add(p, (Seg){ kind, 0, 0, a, b, c });
}

/* `n` consecutive slots of the plan being built. */
static int plan_slots(Plan *p, int n)
{
    if (p->nslots + n > PLAN_SLOTS)
        gen_fatal("expression needs too many labels (internal limit %d)",
                  PLAN_SLOTS);
    int s = p->nslots;
    p->nslots += n;
    return s;
}

/* Conditional-evaluation nodes - see the "Conditional evaluation"
 * section. Any expression containing one is generated through a plan,
 * so that each operand's code lands inside the branch structure. */
static int is_control(int op)
{
    return op == OP_LOGAND || op == OP_LOGOR || op == OP_QUEST ||
           op == OP_SEQNC;
}

/* A node whose value v7 computes as "cbranch(node, true) + 0/1": an
 * "&&"/"||", or a "!" of one. (A "!" of anything else, and a bare
 * relational, stay lazy VK_CONDs - see OP_EXCLA/OP_LESS... - whose
 * materialize_cond() is the same shape.) */
static int is_logic(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op == OP_LOGAND || n->op == OP_LOGOR)
        return 1;
    return n->op == OP_EXCLA && is_logic(t, n->kid[0]);
}

/* The type checks the streaming handlers of the opcodes a plan stands
 * for would have made - a plan never runs those handlers. */
static void check_int_node(const ENode *n)
{
    if (n->type == TY_INT)
        return;
    switch (n->op) {
    case OP_LOGAND: case OP_LOGOR:
        gen_fatal("%s of type %d not yet supported", aluop(n->op)->name,
                  n->type);
    case OP_EXCLA:
        gen_fatal("EXCLA of type %d not yet supported", n->type);
    case OP_COLON:
        gen_fatal("COLON of type %d not yet supported", n->type);
    case OP_QUEST:
        gen_fatal("QUEST of type %d not yet supported", n->type);
    case OP_SEQNC:
        gen_fatal("SEQNC of type %d not yet supported", n->type);
    default:
        gen_fatal("internal: unexpected type check of opcode %d", n->op);
    }
}

static void plan_value(Plan *p, const ETree *t, int i);

/* v7/cc/c11.c's cbranch(): branch to slot[lbl] when node i's truth
 * equals `cond`. */
static void plan_cbranch(Plan *p, const ETree *t, int i, int lbl, int cond)
{
    const ENode *n = &t->v[i];
    switch (n->op) {
    case OP_LOGAND:
    case OP_LOGOR:
        check_int_node(n);
        /* "a && b" branching when TRUE, or "a || b" when FALSE, needs a
         * label of its own for the other outcome of `a`; the other two
         * cases branch straight to lbl from both operands. */
        if ((n->op == OP_LOGAND) == (cond != 0)) {
            int l1 = plan_slots(p, 1);
            plan_op(p, SEG_ALLOC, l1, 0, 0);
            plan_cbranch(p, t, n->kid[0], l1, !cond);
            plan_cbranch(p, t, n->kid[1], lbl, cond);
            plan_op(p, SEG_LABEL, l1, 0, 0);
        } else {
            plan_cbranch(p, t, n->kid[0], lbl, cond);
            plan_cbranch(p, t, n->kid[1], lbl, cond);
        }
        return;
    case OP_EXCLA:
        check_int_node(n);
        plan_cbranch(p, t, n->kid[0], lbl, !cond);
        return;
    case OP_SEQNC: {
        check_int_node(n);
        int rec = plan_slots(p, 2);
        plan_op(p, SEG_MARK, rec, 0, 0);
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_DISCARD, 0, 0, rec);
        plan_cbranch(p, t, n->kid[1], lbl, cond);
        return;
    }
    default: {
        int rec = plan_slots(p, 2);
        plan_op(p, SEG_MARK, rec, 0, 0);
        plan_value(p, t, i);
        plan_op(p, SEG_BRANCH, lbl, cond, rec);
        return;
    }
    }
}

/* Call arguments, right to left.
 *
 * v7/cc/c10.c's comarg() compiles a call's arguments from the LAST to
 * the first, each one straight onto the stack (the sptab templates)
 * before the next is even started - so a computed argument never has
 * to wait in a register while another is computed. mutos_c1 streams
 * temp1 left to right and used to collect every argument's Val first,
 * pushing them afterwards (gen_call()): correct as long as at most the
 * last argument has code of its own, but "reverse(s, lo + 1, hi - 1)"
 * computed lo + 1 into DI and then hi - 1 over it (refused by the
 * register-occupancy guard). A call with two or more arguments, any
 * but the last of which has code of its own, is therefore generated
 * through a plan (plan_call()): the callee's NAME (no code), then each
 * argument from the last to the first followed by SEG_PUSHARG, then
 * SEG_CALL - 10_integ/04_strrev.s.golden's reverse():
 *
 *     mov di,*8.(bp) / dec di / push di     hi - 1
 *     mov di,*6.(bp) / inc di / push di     lo + 1
 *     push *4.(bp)                           s
 *     call _reverse / add sp,*6.
 *
 * and "swapch(&s[lo], &s[hi])" the same way (each element address
 * computed in DI and pushed). A call that is planned anyway, because
 * an argument contains a conditional-evaluation node, takes the same
 * order. Any other call streams as before - pushing right to left
 * then gives exactly these bytes too. */

/* An argument whose evaluation emits no code before its own push: a
 * leaf (NAME, CON, LCON) or the address of a named object (an array's
 * decay, a string literal, "&x" - AMPER of a NAME, materialized only
 * by push_call_arg()'s "lea"/"mov di,#L4"). */
static int is_simple_arg(const ETree *t, int j)
{
    const ENode *n = &t->v[j];
    if (n->kid[0] < 0)
        return 1;                            /* a leaf */
    return n->op == OP_AMPER && t->v[n->kid[0]].op == OP_NAME;
}

/* The arguments of CALL node `i`, first to last, into args[] (the
 * left-associated COMMA chain mutos_c0's parse_call_args_and_emit()
 * builds - see OP_COMMA). Returns their number. */
static int call_args(const ETree *t, int i, int *args)
{
    int k = t->v[i].kid[1];
    int n = 0;
    if (k < 0 || t->v[k].op == OP_NULLOP)
        return 0;
    /* Walk down the left spine, collecting right operands backwards. */
    int rev[MCC_MAXCALLARGS];
    while (t->v[k].op == OP_COMMA) {
        if (n >= MCC_MAXCALLARGS - 1)
            gen_fatal("too many call arguments (internal limit %d)",
                      MCC_MAXCALLARGS);
        rev[n++] = t->v[k].kid[1];
        k = t->v[k].kid[0];
    }
    rev[n++] = k;
    for (int j = 0; j < n; j++)
        args[j] = rev[n - 1 - j];
    return n;
}

/* Whether CALL node `i` needs plan_call()'s order - see above. */
static int call_needs_plan(const ETree *t, int i)
{
    if (t->v[i].op != OP_CALL)
        return 0;
    int args[MCC_MAXCALLARGS];
    int n = call_args(t, i, args);
    for (int j = 0; j < n - 1; j++)
        if (!is_simple_arg(t, args[j]))
            return 1;
    return 0;
}

static void plan_call(Plan *p, const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    int args[MCC_MAXCALLARGS];
    int nargs = call_args(t, i, args);
    int words = plan_slots(p, 1);
    p->slot[words] = 0;
    plan_value(p, t, n->kid[0]);            /* the callee: no code */
    for (int j = nargs - 1; j >= 0; j--) {
        plan_value(p, t, args[j]);
        plan_op(p, SEG_PUSHARG, words, 0, 0);
    }
    plan_op(p, SEG_CALL, words, n->type, 0);
}

/* Node i's value onto the value stack. */
static void plan_value(Plan *p, const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (!n->planned) {
        plan_range(p, n->start, n->end);
        return;
    }
    if (n->op == OP_CALL) {
        plan_call(p, t, i);
        return;
    }
    if (is_logic(t, i)) {
        /* v7's cexpr(): cbranch(tree, c=isn++, 1), then czero/cone. */
        int ltrue = plan_slots(p, 1);
        plan_op(p, SEG_ALLOC, ltrue, 0, 0);
        plan_cbranch(p, t, i, ltrue, 1);
        plan_op(p, SEG_LOGVAL, ltrue, 0, 0);
        return;
    }
    if (n->op == OP_QUEST) {
        /* v7's cexpr(): cbranch(tr1, c=isn++, 0), the true arm, "jbr
         * r=isn++", "c:", the false arm, "r:". */
        const ENode *colon = &t->v[n->kid[1]];
        if (colon->op != OP_COLON)
            gen_fatal("internal: QUEST without a preceding COLON pair");
        check_int_node(colon);
        check_int_node(n);
        int lfalse = plan_slots(p, 1);
        int lend = plan_slots(p, 1);
        int rec = plan_slots(p, 2);
        plan_op(p, SEG_ALLOC, lfalse, 0, 0);
        plan_cbranch(p, t, n->kid[0], lfalse, 0);
        plan_op(p, SEG_MARK, rec, 0, 0);
        plan_value(p, t, colon->kid[0]);
        plan_op(p, SEG_QTRUE, lfalse, lend, rec);
        plan_op(p, SEG_MARK, rec, 0, 0);
        plan_value(p, t, colon->kid[1]);
        plan_op(p, SEG_QFALSE, 0, lend, rec);
        return;
    }
    if (n->op == OP_SEQNC) {
        /* v7's rcexpr(): the left operand with efftab, then the right. */
        check_int_node(n);
        int rec = plan_slots(p, 2);
        plan_op(p, SEG_MARK, rec, 0, 0);
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_DISCARD, 0, 0, rec);
        plan_value(p, t, n->kid[1]);
        return;
    }
    if (n->right_first) {
        plan_value(p, t, n->kid[1]);
        plan_op(p, SEG_SPILL, 0, 0, 0);
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_SWAP, 0, 0, 0);
    } else {
        for (int k = 0; k < 2; k++)
            if (n->kid[k] >= 0)
                plan_value(p, t, n->kid[k]);
    }
    plan_range(p, n->off, n->end);
}

/* Called at the top of the dispatch loop while no plan is active and
 * the value stack is empty: if the next opcode starts an expression
 * (a leaf) that needs a non-postfix evaluation order - a reordered
 * operator, or any conditional-evaluation node - activates a plan for
 * it. A condition (CBRANCH terminator) containing one is planned as a
 * whole with v7's cbranch(), CBRANCH included; any other expression up
 * to its terminator, which is then read as usual. temp1's position is
 * left unchanged either way. */
static int plan_expression(GenState *g, FILE *t1)
{
    long pos = ftell(t1);
    if (pos < 0)
        gen_fatal("internal: temp1 is not seekable (ftell failed)");
    int op = c1_read_op(t1, "temp1");
    if (fseek(t1, pos, SEEK_SET) != 0)
        gen_fatal("internal: temp1 is not seekable (fseek failed)");
    if (op != OP_NAME && op != OP_CON && op != OP_LCON)
        return 0;

    ETree t = { NULL, 0, 0 };
    Term term = { 0, 0, 0, 0, 0 };
    int root = prescan_expr(t1, &t, &term);
    /* Postfix order: every node's operands come before it, so one
     * forward pass sees a node's children fully marked. */
    for (int i = 0; root >= 0 && i < t.n; i++) {
        ENode *n = &t.v[i];
        n->right_first = order_right_first(&t, i);
        n->planned = n->right_first || is_control(n->op) ||
                     call_needs_plan(&t, i);
        for (int k = 0; k < 2; k++)
            if (n->kid[k] >= 0 && t.v[n->kid[k]].planned)
                n->planned = 1;
    }
    int any = root >= 0 && t.v[root].planned;
    if (any) {
        Plan *p = &g->plan;
        p->n = 0;
        p->nslots = 0;
        if (term.op == OP_CBRANCH) {
            int lbl = plan_slots(p, 1);
            p->slot[lbl] = term.lbl;       /* a temp1 label */
            plan_cbranch(p, &t, root, lbl, term.cond);
            p->end = term.after;
        } else {
            plan_value(p, &t, root);
            p->end = term.off;
        }
        plan_add(p, (Seg){ SEG_GOTO, p->end, 0, 0, 0, 0 });
        p->cur = 0;
        p->entered = 0;
        p->active = 1;
    }
    free(t.v);
    return any;
}

/* SEG_SPILL: the right operand just computed goes onto the machine
 * stack - loaded in place first when it is a dereference, the same
 * "mov di,(di)" / "push di" a dereferenced call argument gets
 * (05_arrptr/05_arrofptr.s.golden) and 10_integ/05_matmul.s.golden
 * shows here. */
static void plan_spill(GenState *g)
{
    Val v = materialize(g, pop_val(g));
    if (v.kind == VK_IND) {
        ins2(g, "mov", o_reg(v.reg), o_val(v));
        v = val_reg(v.reg);
    }
    if (v.kind != VK_REG)
        gen_fatal("internal: a spilled operand is expected in a register "
                  "or behind one");
    ins1(g, "push", o_reg(v.reg));
    Val s = {0};
    s.kind = VK_STACKED;
    push_val(g, s);
}

/* SEG_SWAP: the left operand (generated second) is on top of the
 * spilled right one; the operator's handler pops right, then left. */
static void plan_swap(GenState *g)
{
    if (g->valsp < 2 || g->valstack[g->valsp - 2].kind != VK_STACKED)
        gen_fatal("internal: evaluation-order swap without a spilled "
                  "operand below the top of the value stack");
    Val tmp = g->valstack[g->valsp - 1];
    g->valstack[g->valsp - 1] = g->valstack[g->valsp - 2];
    g->valstack[g->valsp - 2] = tmp;
}

/* The conditional-evaluation steps (and SEG_GOTO) - see SegKind and
 * the "Conditional evaluation" section. */
static void plan_control_step(GenState *g, FILE *t1, const Seg *s)
{
    Plan *p = &g->plan;
    switch (s->kind) {
    case SEG_GOTO:
        if (fseek(t1, s->start, SEEK_SET) != 0)
            gen_fatal("internal: temp1 is not seekable (fseek failed)");
        return;
    case SEG_ALLOC:
        p->slot[s->a] = g->next_lab++;
        return;
    case SEG_LABEL:
        put_label(g, p->slot[s->a]);
        return;
    case SEG_MARK:
        region_open(g, &p->slot[s->a]);
        return;
    case SEG_BRANCH: {
        const int *rec = &p->slot[s->c];
        gen_cond_branch(g, pop_val(g), p->slot[s->a], s->b, rec[0]);
        region_close(g, rec);
        return;
    }
    case SEG_LOGVAL:
        push_val(g, gen_logval(g, p->slot[s->a]));
        return;
    case SEG_QTRUE:
        gen_arm_load(g);
        region_close(g, &p->slot[s->c]);
        p->slot[s->b] = g->next_lab++;
        ins1(g, "jmp", o_lab(p->slot[s->b]));
        put_label(g, p->slot[s->a]);
        return;
    case SEG_QFALSE:
        gen_arm_load(g);
        region_close(g, &p->slot[s->c]);
        put_label(g, p->slot[s->b]);
        push_val(g, val_reg("di"));
        return;
    case SEG_DISCARD:
        discard_val(g);
        region_close(g, &p->slot[s->c]);
        return;
    case SEG_PUSHARG:
        p->slot[s->a] += push_call_arg(g, pop_val(g));
        return;
    case SEG_CALL: {
        /* OP_CALL's own handler, for a call whose arguments a plan
         * already pushed - see plan_call(). */
        if (!ty_is_word(s->b) && s->b != TY_LONG)
            gen_fatal("a call returning type %d is not yet supported "
                      "(only a function returning an int, a long or a "
                      "pointer is covered so far)", s->b);
        Val callee = pop_val(g);
        push_val(g, finish_call(g, callee, p->slot[s->a], s->b == TY_LONG));
        return;
    }
    default:
        gen_fatal("internal: unknown evaluation-order step %d", (int)s->kind);
    }
}

/* Called at the top of the dispatch loop: while a plan is active, runs
 * the non-range steps that are due and positions temp1 at the next
 * opcode to dispatch; once the last range has been streamed, ends the
 * plan at the expression's terminator, where plain streaming resumes. */
static void plan_step(GenState *g, FILE *t1)
{
    Plan *p = &g->plan;
    while (p->active) {
        if (p->cur == p->n) {
            if (ftell(t1) != p->end)
                gen_fatal("internal: evaluation-order plan ended away from "
                          "its expression's end");
            p->active = 0;
            return;
        }
        Seg *s = &p->seg[p->cur];
        if (s->kind == SEG_SPILL) {
            plan_spill(g);
            p->cur++;
            continue;
        }
        if (s->kind == SEG_SWAP) {
            plan_swap(g);
            p->cur++;
            continue;
        }
        if (s->kind != SEG_RANGE) {
            plan_control_step(g, t1, s);
            p->cur++;
            continue;
        }
        if (!p->entered) {
            if (fseek(t1, s->start, SEEK_SET) != 0)
                gen_fatal("internal: temp1 is not seekable (fseek failed)");
            p->entered = 1;
        }
        long pos = ftell(t1);
        if (pos < s->end)
            return;
        if (pos > s->end)
            gen_fatal("internal: an opcode handler read past the end of a "
                      "reordered subtree");
        p->cur++;
        p->entered = 0;
    }
}

/* -------------------------------------------------------------- */
/* temp2 - the string-literal data file (see gen_strings()). */

/* The most values the real compiler puts on one ".byte" line of a
 * string literal - see gen_strings(). */
#define MCC_BYTES_PER_LINE 9

/*
 * Renders temp2 - written by mutos_c0's putstr(), one run per string
 * literal - after the ".data" that ends the code (v7/cc/c10.c's main()
 * "tack[s] on the string file" the same way, after temp1's EOFC). Only
 * what putstr() writes is accepted:
 *
 *     LABEL <n>     -> "L<n>:" (no newline - the data glues on, as
 *                      every label does; put_label())
 *     BDATA (1 v)... 0
 *                   -> ".byte\t/<v>,/<v>,..." - v7/cc/c11.c's BDATA
 *                      case reads the same pairs, but prints them all on
 *                      one line in octal; the real MUTOS c1 prints them
 *                      in hex with mutos_as's '/' prefix (lower case, no
 *                      leading zeros: "/6f", "/a", "/0") and starts a
 *                      new ".byte" line after every 9th value
 *
 * confirmed byte-for-byte against 05_arrptr/05_arrofptr.s.golden
 * ("L4:.byte\t/6f,/6e,/65,/0" - one short line per literal) and
 * 07_strlibc.s.golden ("hello, mutos", 13 values: a line of 9, then
 * ".byte\t/74,/6f,/73,/0"), plus 10_integ/04_strrev.s.golden (10
 * values: 9, then ".byte\t/0"). mutos_c0 starts a new BDATA run
 * before every 15th byte (v7's putstr()); each run starts a new line
 * too. The 9-per-line rule and that split together reproduce the line
 * layout of all 208 string literals in the real compiler's output in
 * tests/mutos_as/kernel_nonopt/ and kernel_opt/ (runs of 14, 15, 15,
 * ... values, each broken 9 + rest).
 *
 * A byte value of 0x80 or above is printed as its 8-bit value
 * ("/e4"): mutos_c0 masks every byte to 0..255 like v7's putstr(), and
 * this renders the word as read. No golden string literal holds such a
 * byte, so that is derived, not confirmed. (The sign-extended "/ff81"
 * values in kernel_nonopt/amx.s are a char-array INITIALIZER's, a
 * different code path with a different format - ".byte /ff81", a space,
 * one value per line - that mutos_c0 does not produce.)
 */
static void gen_strings(GenState *g, FILE *t2)
{
    for (;;) {
        int op = c1_read_op(t2, "temp2");
        if (op == OP_EOFC)
            return;
        if (op == OP_LABEL) {
            put_label(g, c1_read_num(t2, "temp2"));
            continue;
        }
        if (op != OP_BDATA)
            gen_fatal("unsupported temp2 opcode %d (0x%02x) - only string "
                      "literals (LABEL/BDATA) are covered so far", op, op);
        /* v7/cc/c11.c: "if (geti() == 1) { ... }" - a run is (1, value)
         * pairs ended by a word that is not 1 (putstr()'s lone 0). */
        char line[OPND_MAX * 4];
        int nvals = 0;
        size_t len = 0;
        while (c1_read_num(t2, "temp2") == 1) {
            unsigned v = (unsigned)c1_read_num(t2, "temp2") & 0xFFFFu;
            if (nvals == MCC_BYTES_PER_LINE) {
                put_line(g, ".byte\t%s", line);
                nvals = 0;
                len = 0;
            }
            int w = snprintf(line + len, sizeof line - len, "%s/%x",
                             nvals ? "," : "", v);
            if (w < 0 || (size_t)w >= sizeof line - len)
                gen_fatal("internal: .byte line buffer too small");
            len += (size_t)w;
            nvals++;
        }
        if (nvals > 0)
            put_line(g, ".byte\t%s", line);
    }
}

int c1_generate(FILE *temp1, FILE *temp2, FILE *out)
{
    GenState g = {0};
    g.out = out;
    g.next_lab = 10000; /* see GenState's next_lab field comment */

    for (;;) {
        /* Evaluation order - see that section: an active plan decides
         * where the next opcode is read from; otherwise, at the start
         * of each expression, check whether it needs one. */
        plan_step(&g, temp1);
        if (!g.plan.active && g.valsp == 0 && plan_expression(&g, temp1))
            plan_step(&g, temp1);

        int op = c1_read_op(temp1, "temp1");
        if (op == OP_EOFC)
            break;

        switch (op) {

        case OP_SYMDEF: {
            char *name = c1_read_sym(temp1, "temp1");
            ins1(&g, ".globl", o_sym(name));
            free(name);
            break;
        }

        case OP_PROG:
            ins0(&g, ".text");
            break;

        case OP_EVEN:
            ins0(&g, ".even");
            break;

        case OP_RLABEL: {
            char *name = c1_read_sym(temp1, "temp1");
            put_line(&g, "%s:", name);
            free(name);
            break;
        }

        case OP_SAVE:
            /* Fixed, unconditional prologue - see docs/MUTOS_C_ABI.md
             * sect. 1.2: every compiled function saves bp/di/si
             * regardless of actual usage, so "|NREG 3" is a constant,
             * never derived from stream data. */
            put_seq(&g, SEQ_PROLOGUE);
            put_line(&g, "|NREG %d", MCC_NSAVEREG);
            g.setreg_seen = 0; /* one SAVE per function - see
                                 * setreg_seen's own comment. */
            g.reserved = 0;     /* ditto - see reserved's own comment. */
            g.regvar_dirty = 0;
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
                put_line(&g, "|NREG %d", g.regvar - 1);
            g.setreg_seen = 1;
            break;
        }

        case OP_BRANCH: {
            int lab = c1_read_num(temp1, "temp1");
            ins1(&g, "jmp", o_lab(lab));
            break;
        }

        case OP_LABEL: {
            int lab = c1_read_num(temp1, "temp1");
            /* Deliberately NO trailing newline - see put_label(). */
            put_label(&g, lab);
            break;
        }

        case OP_ANAME: {
            char *name = c1_read_sym(temp1, "temp1");
            int offset = c1_read_num(temp1, "temp1");
            /* Confirmed exactly against 01_intarith.s.golden's
             * "| _a=-6." lines (name already includes the leading
             * '_' - see c1_stream.h's c1_read_sym()). */
            put_line(&g, "| %s=%d.", name, offset);
            free(name);
            break;
        }

        case OP_RNAME: {
            char *name = c1_read_sym(temp1, "temp1");
            int regnum = c1_read_num(temp1, "temp1");
            const char *rname = regvar_name(regnum);
            g.reserved |= reg_bit(rname); /* see reserved's own comment */
            /* A 'register'-class local's declaration comment -
             * confirmed against 04_funcs/06_regclass.s.golden's
             * "| _i=di\n": unlike ANAME's "| name=offset." (a plain
             * bp-relative number with a trailing period), this
             * renders the actual physical register name, no trailing
             * period - see regvar_name(). */
            put_line(&g, "| %s=%s", name, rname);
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
            ins0(&g, ".bss");
            break;

        case OP_SSPACE: {
            int size = c1_read_num(temp1, "temp1");
            /* "reserve N bytes" - confirmed against 05_staticvar.s.
             * golden's "L4:.blkb\t2.\n" (a plain int's 2-byte BSS
             * slot); the trailing "." decimal-terminator matches this
             * project's ordinary numeric-immediate convention (see
             * render_operand()). */
            ins1(&g, ".blkb", o_fmt("%d.", size));
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
            put_line(&g, "| %s=L%d", name, label);
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
                 * confirmed for TY_INT (every 04_funcs .1.golden),
                 * since 02_long/03_retval.c TY_LONG (type 22), and
                 * since 05_arrptr/07_strlibc.c a pointer ("char
                 * *strcpy();" - type 49, "function returning pointer
                 * to char"); the result's own type is checked again by
                 * OP_CALL. */
                if (!ty_is_func(type) ||
                    !(ty_is_word(ty_decref(type)) || ty_decref(type) == TY_LONG))
                    gen_fatal("NAME with storage class SC_EXTERN and "
                              "type %d not yet supported (only a called "
                              "function returning an int, a long or a "
                              "pointer is covered so far)", type);
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
                /* A string literal's NAME is this same shape, typed
                 * TY_CHAR (the element type of its unnamed "char[]")
                 * and always followed by the AMPER that decays it - see
                 * VK_STATICADDR. */
                if (!ty_is_word(type) && type != TY_CHAR && type != TY_LONG)
                    gen_fatal("NAME of type %d not yet supported (only "
                              "int/char/long/pointer statics are "
                              "covered so far)", type);
                int label = c1_read_num(temp1, "temp1");
                Val sv = val_static(label);
                sv.bytev = (type == TY_CHAR); /* see Val's `bytev` */
                push_val(&g, sv);
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
                const char *rname = regvar_name(regnum);
                if (g.regvar_dirty & reg_bit(rname))
                    gen_fatal("the 'register' variable held in %s is read "
                              "after being overwritten earlier in the same "
                              "statement (while computing its own new "
                              "value) - not yet supported - see "
                              "docs/DEVLOG.md", rname);
                Val rv = val_reg(rname);
                rv.regvar = 1;
                push_val(&g, rv);
                break;
            }
            if (hclass != SC_AUTO)
                gen_fatal("NAME with storage class %d not yet supported "
                          "(only AUTO locals, a local STATIC, a "
                          "'register' local, and a called function's own "
                          "SC_EXTERN name are covered so far)", hclass);
            /* Any pointer is accepted (it is just a word in memory - see
             * ty_is_word()): an int/char/pointer array's own NAME carries
             * the element type (05_arrptr/05_arrofptr.1.golden's "char
             * *names[3]" - NAME type 9 - and 07_strlibc.1.golden's
             * "char src[20]" - type 1), a plain pointer local its
             * pointer type. What a consumer may then DO with it
             * (dereference, subscript, ...) is checked by that
             * consumer. */
            if (!ty_is_word(type) && type != TY_CHAR && type != TY_LONG)
                gen_fatal("NAME of type %d not yet supported (only "
                          "int/char/long/pointer locals are covered so "
                          "far)", type);
            int offset = c1_read_num(temp1, "temp1");
            Val mv = val_mem(offset);
            /* A char array's NAME carries the element type too, but is
             * always consumed by the AMPER that decays it, which
             * accepts a byte operand. */
            mv.bytev = (type == TY_CHAR);
            push_val(&g, mv);
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
            /* Opcode 109 converts to or from 'char'; its type argument
             * is the RESULT type (mutos_c0 inserts it wherever the real
             * front end does - see c0_parser.c's promote_char()/
             * convert_assign()):
             *
             * - TY_CHAR, int -> char ("(char) i", or an int stored into
             *   a char, "buf[0] = 1;" -> CON(1) ITOC(1) ASSIGN(1)).
             *   A constant is folded - its low byte, sign-extended, as
             *   v7/cc/c12.c optim()'s ITOC case does ("p->value << 8 >>
             *   8") - so the store is a single "movb *-84.(bp),*1."
             *   (09_abiprobe/02_frame080.s.golden). Anything else is
             *   loaded into DX, confirmed against 08_castsize.s.golden's
             *   "c = (char) i;": "mov dx,*-6.(bp)" then "movb
             *   *-12.(bp),dx" - DI/SI have no byte-addressable half, so
             *   it could never have been DI; OP_ASSIGN's TY_CHAR case
             *   stores the low byte with "movb". No masking instruction
             *   - the truncation is the "movb" only ever touching DL.
             * - TY_INT, char -> int ("buf[0] + buf[79]", "return
             *   buf[0];"): a byte in memory becomes a VK_CHARX, loaded
             *   (movb/cbw) only by its consumer - see VK_CHARX. */
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_INT) {
                Val v = pop_val_ex(&g, POP_BYTE);
                if (v.kind == VK_IMM) {
                    push_val(&g, val_imm((long)(int8_t)(uint8_t)(v.imm & 0xFF)));
                    break;
                }
                if (!v.bytev || (v.kind != VK_MEM && v.kind != VK_STATIC &&
                                 v.kind != VK_IND))
                    gen_fatal("ITOC to int of this operand shape is not yet "
                              "supported (only a char in memory is covered "
                              "so far)");
                Val c = {0};
                c.kind = VK_CHARX;
                v.bytev = 0;
                c.cl = simple_of(v);
                push_val(&g, c);
                break;
            }
            if (type != TY_CHAR)
                gen_fatal("ITOC to type %d not yet supported (only TY_CHAR "
                          "and TY_INT are covered so far)", type);
            Val v = materialize(&g, pop_val(&g));
            if (v.kind == VK_IMM) {
                push_val(&g, val_imm((long)(int8_t)(uint8_t)(v.imm & 0xFF)));
                break;
            }
            ins2(&g, "mov", o_reg("dx"), o_val(v));
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
            Val v = pop_val_ex(&g, POP_BYTE);
            if (v.kind != VK_MEM)
                gen_fatal("CTOL of a non-memory char operand is not yet "
                          "supported");
            v.bytev = 0;
            ins2(&g, "movb", o_reg("ax"), o_val(v));
            ins0(&g, "cbw");
            ins0(&g, "cwd");
            put_seq(&g, SEQ_DXAX_TO_DISI);
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
            Val v = materialize(&g, pop_val(&g));
            emit_cwd_from(&g, o_val(v));
            put_seq(&g, SEQ_DXAX_TO_DISI);
            push_val(&g, val_long());
            break;
        }

        case OP_PLUS:
        case OP_MINUS: {
            int type = c1_read_num(temp1, "temp1");
            if (op == OP_PLUS && ty_is_ptr(type)) {
                /* Pointer + scaled-index arithmetic - a single
                 * 1-D subscript step ("a[i]"; a 2-D "m[i][j]" takes
                 * the VK_SCALED/VK_ROWADDR path just below instead)
                 * or an explicit "*(a + i)" - confirmed against
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
                Val r = pop_any(&g);
                Val l = pop_any(&g);
                if (l.kind == VK_STATICADDR || r.kind == VK_STATICADDR ||
                    l.kind == VK_FUNCADDR || r.kind == VK_FUNCADDR)
                    gen_fatal("pointer arithmetic on the address of a string "
                              "literal or function is not yet supported - see "
                              "src/mutos_cc/README.md");
                if (l.kind == VK_REGOFF || r.kind == VK_REGOFF) {
                    /* A scaled "var + N" index (OP_ITOP's VK_REGOFF, the
                     * variable already scaled in a register, N * size
                     * pending) added to a pointer VARIABLE: the pointer
                     * is added to the register, and N * size stays
                     * pending for the dereference's displacement - or,
                     * for any other consumer, is added last: 10_integ/
                     * 02_bubsort.s.golden's "a[j + 1]" -> "add si,*4.
                     * (bp)" ... "*2.(si)" and "&a[j + 1]" -> "add di,
                     * *4.(bp)" / "add di,*2." (the '&' cancels the
                     * dereference - see OP_STAR). An array base is
                     * refused: v7's acommute() would fold N * size into
                     * its address instead ("lea di,<a + 2>"), which no
                     * golden shows. */
                    Val ro = (r.kind == VK_REGOFF) ? r : l;
                    Val base = (r.kind == VK_REGOFF) ? l : r;
                    if (ro.cl.kind != VK_REG || base.kind != VK_MEM)
                        gen_fatal("a subscript of the form \"i + N\" on anything "
                                  "but a pointer variable is not yet supported - "
                                  "see src/mutos_cc/README.md");
                    const char *reg = ro.cl.reg;
                    ins2(&g, "add", o_reg(reg), o_val(base));
                    long pos = ftell(temp1);
                    int next = c1_read_op(temp1, "temp1");
                    fseek(temp1, pos, SEEK_SET);
                    if (next == OP_STAR) {
                        push_val(&g, ro);
                        break;
                    }
                    ins2(&g, "add", o_reg(reg), o_imm(ro.imm));
                    push_val(&g, val_reg(reg));
                    break;
                }
                /* The two steps of a 2-D subscript "m[i][j]" (see
                 * VK_SCALED/VK_ROWADDR's comment): the row step
                 * (base in DI + scaled row index) only records the
                 * pending row address; the column step emits the whole
                 * combined address computation at once. Any other
                 * pairing involving either kind is an unconfirmed
                 * shape. */
                if (l.kind == VK_SCALED || l.kind == VK_ROWADDR ||
                    r.kind == VK_SCALED || r.kind == VK_ROWADDR) {
                    if (r.kind == VK_SCALED && l.kind == VK_REG &&
                        strcmp(l.reg, "di") == 0) {
                        Val ra = {0};
                        ra.kind = VK_ROWADDR;
                        ra.reg = "di";
                        ra.cl = r.cl;
                        ra.imm = r.imm;
                        push_val(&g, ra);
                        break;
                    }
                    if (r.kind == VK_SCALED && l.kind == VK_ROWADDR) {
                        gen_subscript_2d(&g, l, r);
                        push_val(&g, val_reg("di"));
                        break;
                    }
                    gen_fatal("a 2-D array subscript mixing a constant and "
                              "a runtime index is not yet supported - see "
                              "src/mutos_cc/README.md");
                }
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
                /* A constant index on a pointer VALUE (a pointer
                 * variable/parameter in memory, or already in a
                 * register) that is dereferenced right away: the
                 * pointer is loaded and the offset becomes the
                 * dereference's displacement - see VK_REGOFF
                 * (09_abiprobe/01_argvmain.s.golden's "argv[1]" ->
                 * "mov di,*6.(bp)" / "mov di,*2.(di)"). One opcode of
                 * lookahead, the same save/restore technique as
                 * OP_STAR's own peeks; any other consumer keeps the
                 * "add" below (no golden shows one). */
                if (r.kind == VK_IMM &&
                    (l.kind == VK_MEM || l.kind == VK_REG)) {
                    long pos = ftell(temp1);
                    int next = c1_read_op(temp1, "temp1");
                    fseek(temp1, pos, SEEK_SET);
                    if (next == OP_STAR) {
                        Val ro = {0};
                        ro.kind = VK_REGOFF;
                        ro.cl = simple_of(l);   /* loaded by OP_STAR */
                        ro.imm = r.imm;
                        push_val(&g, ro);
                        break;
                    }
                }
                if (l.kind == VK_MEM_DIRECT) {
                    require_free(r, RB_DI, "pointer PLUS");
                    ins2(&g, "lea", o_reg("di"), o_val(l));
                    l = val_reg("di");
                }
                if (r.kind == VK_MEM_DIRECT) {
                    require_free(l, RB_DI, "pointer PLUS");
                    ins2(&g, "lea", o_reg("di"), o_val(r));
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
                    require_free(r, RB_DI, "pointer PLUS");
                    load_into_di(&g, l);
                    dst = "di"; other = r;
                }
                ins2(&g, "add", o_reg(dst), o_val(other));
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
                if (op == OP_PLUS)
                    constant_to_right(&l, &r);
                if (l.kind != VK_MEM)
                    gen_fatal("'long' %s with a non-memory left operand "
                              "is not yet supported",
                              op == OP_PLUS ? "addition" : "subtraction");
                const AluOp *a = aluop(op);
                if (r.kind == VK_MEM) {
                    ins2(&g, "mov", o_reg("si"), o_mem(l.offset + MCC_SZINT));
                    ins2(&g, "mov", o_reg("di"), o_mem(l.offset));
                    ins2(&g, a->mnem, o_reg("si"), o_mem(r.offset + MCC_SZINT));
                    ins2(&g, a->mnem_hi, o_reg("di"), o_mem(r.offset));
                } else if (r.kind == VK_LCON && r.offset == (r.imm < 0 ? -1 : 0)) {
                    emit_cwd_from(&g, o_imm(r.imm));
                    put_seq(&g, SEQ_PUSH_AXDX);
                    ins2(&g, "mov", o_reg("si"), o_mem(l.offset + MCC_SZINT));
                    ins2(&g, "mov", o_reg("di"), o_mem(l.offset));
                    ins1(&g, "pop", o_reg("bx"));
                    ins0(&g, "pop cx"); /* confirmed literal space, not a
                                          * tab - see comment above - so
                                          * emitted as one operand-less
                                          * "mnemonic" on purpose. */
                    ins2(&g, a->mnem, o_reg("si"), o_reg("cx"));
                    ins2(&g, a->mnem_hi, o_reg("di"), o_reg("bx"));
                } else {
                    gen_fatal("'long' %s with this right-operand shape "
                              "is not yet supported",
                              op == OP_PLUS ? "addition" : "subtraction");
                }
                push_val(&g, val_long());
                break;
            }
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val l, r;
            pop_operands_ex(&g, &l, &r, POP_CHARX);
            if (l.kind == VK_MEM_DIRECT || r.kind == VK_MEM_DIRECT)
                gen_fatal("an address used in int %s is not yet supported - "
                          "see src/mutos_cc/README.md", aluop(op)->name);
            if (l.kind == VK_CHARX || r.kind == VK_CHARX) {
                /* A char operand read as an int (VK_CHARX): each one
                 * can only be loaded into AX (movb/cbw), so of two of
                 * them the left one is moved out of the way into DI -
                 * the ordinary working register - before the right one
                 * is loaded, and the sum is formed there. Confirmed for
                 * PLUS of two chars only: 09_abiprobe/0N_frameNNN.s.
                 * golden's "buf[0] + buf[N - 1]" and 06_struct/06_union.
                 * s.golden's "n.b[0] + n.b[1]" -> "movb ax,<a>" / "cbw"
                 * / "mov di,ax" / "movb ax,<b>" / "cbw" / "add di,ax".
                 * With ONE char operand the real compiler very probably
                 * computes in AX instead (the kernel_nonopt corpus has
                 * "movb ax,*23.(bx)" / "cbw" / "and ax,*-2." for a char
                 * masked by a constant), and MINUS's operand order is
                 * unconfirmed - both refused until a golden shows them. */
                if (op != OP_PLUS || l.kind != VK_CHARX || r.kind != VK_CHARX)
                    gen_fatal("%s with a 'char' operand is not yet supported "
                              "(confirmed so far: the sum of two chars) - "
                              "see src/mutos_cc/README.md", aluop(op)->name);
                require_free(r, RB_AX | RB_DI, "PLUS");
                Val la = load_charx(&g, l);
                load_into_di(&g, la);
                Val ra = load_charx(&g, r);
                ins2(&g, "add", o_reg("di"), o_val(ra));
                push_val(&g, val_reg("di"));
                break;
            }
            if (op == OP_PLUS)
                constant_to_right(&l, &r);
            if (op == OP_PLUS && r.kind == VK_IMM &&
                (l.kind == VK_MEM || l.kind == VK_STATIC)) {
                /* "var + N" about to be scaled as a subscript: left
                 * uncomputed - see VK_IDXOFF. */
                Consumer cons = scan_consumer(temp1);
                if (cons.op == OP_ITOP && cons.as_left) {
                    Val x = {0};
                    x.kind = VK_IDXOFF;
                    x.cl = simple_of(l);
                    x.imm = r.imm;
                    push_val(&g, x);
                    break;
                }
            }
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
                require_free(other, reg_bit(ind.reg), "PLUS");
                ins2(&g, "mov", o_reg(ind.reg), o_val(ind));
                if (other.kind == VK_IMM && other.imm == 1)
                    ins1(&g, "inc", o_reg(ind.reg));
                else
                    ins2(&g, "add", o_reg(ind.reg), o_val(other));
                push_val(&g, val_reg(ind.reg));
                break;
            }
            if (op == OP_PLUS) {
                /* One operand is already sitting in AX (a multiply's
                 * product or a call's result) and the other is a plain
                 * memory operand: the sum is formed in AX itself, in
                 * place - never moved to DI first - whichever side of
                 * the '+' the AX value was on. Confirmed against
                 * 05_arrptr/02_array2d.s.golden's "i * 10 + j" ->
                 * "imul\tcx" / "add\tax,*-32.(bp)" and 10_integ/
                 * 05_matmul.s.golden's "sum + a[i][k] * b[k][j]" ->
                 * "imul\tcx" / "add\tax,*-36.(bp)" (the product on
                 * the RIGHT). The table-driven real code generator
                 * only sees where the value lives, so this covers a
                 * call result the same way. PLUS only - MINUS is not
                 * commutative and no golden shows its AX shape; an
                 * immediate or register other operand is left to the
                 * paths below, unconfirmed either way. */
                int l_ax = (l.kind == VK_REG && strcmp(l.reg, "ax") == 0);
                int r_ax = (r.kind == VK_REG && strcmp(r.reg, "ax") == 0);
                Val other = l_ax ? r : l;
                if ((l_ax != r_ax) &&
                    (other.kind == VK_MEM || other.kind == VK_MEM_CVT ||
                     other.kind == VK_STATIC)) {
                    ins2(&g, "add", o_reg("ax"), o_val(other));
                    push_val(&g, val_reg("ax"));
                    break;
                }
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
                ins2(&g, "mov", o_reg("si"), o_val(l));
                ins2(&g, aluop(op)->mnem, o_reg("si"), o_reg("di"));
                push_val(&g, val_reg("si"));
                break;
            }
            if (!(l.kind == VK_REG && strcmp(l.reg, "di") == 0))
                require_free(r, RB_DI, aluop(op)->name);
            load_into_di(&g, l);
            if (r.kind == VK_IMM && r.imm == 1 && op == OP_PLUS) {
                /* "+ 1" specifically compiles to a plain INC, not
                 * "add di,*1." - confirmed against 07_ternary.s.golden's
                 * "a = a + 1;" -> "inc\tdi" (never an "add"). */
                ins1(&g, "inc", o_reg("di"));
            } else if (r.kind == VK_IMM && r.imm == 1 && op == OP_MINUS) {
                /* The symmetric "- 1" -> DEC case, now confirmed
                 * against 04_funcs/03_recfact.s.golden's "n - 1" ->
                 * "dec\tdi" (never a "sub") and 04_mutrec.s.golden's
                 * identical "n - 1" in both isodd()/iseven(). */
                ins1(&g, "dec", o_reg("di"));
            } else {
                ins2(&g, aluop(op)->mnem, o_reg("di"), o_val(r));
            }
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_AND:
        case OP_OR:
        case OP_EXOR: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val l, r;
            pop_operands(&g, &l, &r);
            constant_to_right(&l, &r);
            if (!(l.kind == VK_REG && strcmp(l.reg, "di") == 0))
                require_free(r, RB_DI, aluop(op)->name);
            load_into_di(&g, l);
            ins2(&g, aluop(op)->mnem, o_reg("di"), o_val(r));
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_LSHIFT:
        case OP_RSHIFT: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val l, r;
            pop_operands(&g, &l, &r);
            if (!(l.kind == VK_REG && strcmp(l.reg, "di") == 0))
                require_free(r, RB_DI, aluop(op)->name);
            load_into_di(&g, l);
            const char *mnem = aluop(op)->mnem;
            if (r.kind == VK_IMM) {
                /* Constant shift count: plain 8086 has no
                 * shift-by-immediate-count opcode (that's an
                 * 80186-only extension - see docs/DEVLOG.md's CPU
                 * reference - and 04_shift.c's own header comment:
                 * this compiler targets plain 8086 only), so the
                 * real compiler repeats the single-bit-shift form
                 * (opcode D1 /4 or /7, count implicitly 1) for a
                 * count of up to 2 - confirmed via 04_shift.s.golden's
                 * "r >> 2" emitting two consecutive "sar\tdi,*1"
                 * lines, and "a << 1" emitting exactly one "sal\tdi,
                 * *1" - and from 3 up shifts by CL instead (see
                 * emit_const_shift()/MCC_SHIFT_REPEAT_MAX). */
                emit_const_shift(&g, mnem, "di", r.imm);
            } else {
                /* Variable shift count: must be loaded into CL (the
                 * only register the 8086's "shift by CL" opcode
                 * shape accepts) - confirmed via 04_shift.s.golden's
                 * "mov\tcx,*-8.(bp)" immediately before
                 * "sal\tdi,cl". */
                load_into_cx(&g, r);
                ins2(&g, mnem, o_reg("di"), o_reg("cl"));
            }
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_COMPL: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("COMPL of type %d not yet supported", type);
            Val v = materialize(&g, pop_val(&g));
            load_into_di(&g, v);
            ins1(&g, "not", o_reg("di"));
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_AMPER: {
            /* Address-of. Three confirmed shapes: a function's address
             * and a static object's address (both link-time constants,
             * no code), and a bp-relative local's (a real "lea"). */
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
            if (!ty_is_ptr(type))
                gen_fatal("AMPER of type %d not yet supported (only a "
                          "pointer result is covered so far)", type);
            /* The address of a char is an ordinary word - taking it
             * reads no byte (a char array's or string literal's NAME is
             * typed with its char element - see OP_NAME). */
            Val v = pop_val_ex(&g, POP_BYTE);
            v.bytev = 0;
            if (v.kind == VK_STATIC) {
                /* The address of a static object - a string literal's
                 * (c0's putstr() NAME(SC_STATIC, TY_CHAR, <label>) +
                 * AMPER(9)): a link-time constant, no code - see
                 * VK_STATICADDR. */
                Val r = {0};
                r.kind = VK_STATICADDR;
                r.offset = v.offset;
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
            if (v.kind != VK_MEM)
                gen_fatal("'&' on a non-memory operand is not yet supported");
            /* WHEN the "lea" is emitted depends on what consumes the
             * address (scan_consumer()):
             *
             * - the base of pointer arithmetic with a RUNTIME index -
             *   a pointer PLUS whose right operand ends in an ITOP and
             *   contains a variable ("a[i]", emit_subscript()'s
             *   NAME/AMPER/<index>/CON/ITOP/PLUS/STAR, including a 2-D
             *   subscript's row step): emitted right here, eagerly,
             *   BEFORE the index is loaded and scaled - 01_arrbasic.s.
             *   golden's "lea di,*-14.(bp)" / "mov si,*-16.(bp)" /
             *   "sal si,*1" / "add di,si", 05_arrofptr.s.golden's
             *   identical "names[i]", 02_array2d.s.golden's rows;
             * - anything else is DEFERRED (VK_MEM_DIRECT - see its own
             *   comment): a compile-time-constant index folds into one
             *   bp-relative operand with no "lea" at all (04_ptrarreq.
             *   s.golden's "v[0] = 1;" -> "mov *-12.(bp),*1."); a call
             *   argument gets its "lea" only when gen_call() pushes it,
             *   right to left (04_ptrarreq.s.golden's "sumarr(v, 4)",
             *   07_strlibc.s.golden's "strcpy(src, \"...\")" and
             *   "strcpy(dst, src)" - two deferred arrays in one call);
             *   an assignment's rhs gets it in OP_ASSIGN ("p = a;",
             *   "pp = &p;" - same output either way).
             *
             * (This replaced a one-opcode peek - "is the next opcode a
             * NAME?" - which string-literal arguments broke; see
             * scan_consumer().) */
            Consumer cons = scan_consumer(temp1);
            int subscript_base = (cons.op == OP_PLUS && cons.as_left &&
                                  ty_is_ptr(cons.type) &&
                                  cons.prev_op == OP_ITOP && cons.saw_runtime);
            if (!subscript_base) {
                push_val(&g, val_mem_direct(v.offset));
                break;
            }
            ins2(&g, "lea", o_reg("di"), o_val(v));
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
            /* Any pointer type: TY_PTR_INT ("int *"/"int []"), the
             * 2-D row step's TY_PTR_ARY_INT, and e.g. 05_arrptr/
             * 05_arrofptr.1.golden's type 41 ("pointer to pointer to
             * char" - subscripting "char *names[3]"). The scale factor
             * itself arrives as the CON operand, so the type only
             * matters to tell a 2-D row step apart. */
            if (!ty_is_ptr(type))
                gen_fatal("ITOP of type %d not yet supported (only a "
                          "pointer type is covered so far)", type);
            Val size = pop_val(&g);
            Val amt  = (g.valsp > 0 && g.valstack[g.valsp - 1].kind == VK_IDXOFF)
                       ? pop_any(&g) : pop_val(&g);
            if (size.kind != VK_IMM)
                gen_fatal("ITOP with a non-constant scale factor is "
                          "not yet supported");
            if (amt.kind == VK_IDXOFF) {
                /* "var + N" as a 1-D subscript (see VK_IDXOFF): the
                 * variable scaled into DI - or SI, when DI holds a value
                 * still needed (di_busy()) - and N scaled into a pending
                 * displacement, carried as a VK_REGOFF to the pointer
                 * PLUS and OP_STAR: 10_integ/02_bubsort.s.golden's
                 * "a[j + 1]" -> "mov si,*-8.(bp)" / "sal si,*1" / "add
                 * si,*4.(bp)" / ... "*2.(si)", and "&a[j + 1]" -> "mov
                 * di,*-8.(bp)" / "sal di,*1" / "add di,*4.(bp)" / "add
                 * di,*2.". A 2-D subscript's index is refused (no
                 * golden). */
                if (type == TY_PTR_ARY_INT ||
                    (g.valsp >= 1 && (g.valstack[g.valsp - 1].kind == VK_ROWADDR ||
                                      g.valstack[g.valsp - 1].rowbase)))
                    gen_fatal("a 2-D array subscript of the form \"i + N\" is "
                              "not yet supported - see src/mutos_cc/README.md");
                long sz = size.imm;
                if (sz != 1 && sz != 2 && sz != 4)
                    gen_fatal("ITOP scaling by a non-power-of-two size "
                              "(%ld) is not yet supported", sz);
                const char *reg = di_busy(&g) ? "si" : "di";
                ins2(&g, "mov", o_reg(reg), o_val(val_from_simple(amt.cl)));
                for (long k = sz; k > 1; k /= 2)
                    ins2(&g, "sal", o_reg(reg), o_shift1());
                Val ro = {0};
                ro.kind = VK_REGOFF;
                ro.cl = simple_of(val_reg(reg));
                ro.imm = amt.imm * sz;
                push_val(&g, ro);
                break;
            }
            if (amt.kind == VK_IMM) {
                /* Also a 2-D subscript's constant row or column index
                 * ("a[0][1] = 2;" - 10_integ/05_matmul.s.golden's lone
                 * "mov\t*-10.(bp),*2."): folds exactly like the 1-D
                 * "v[0]" case, the row's STAR/AMPER pair in between
                 * cancelling (see OP_STAR). */
                push_val(&g, val_imm(amt.imm * size.imm));
                break;
            }
            /* A runtime index inside a 2-D subscript "m[i][j]": the
             * row step (this ITOP's own type is TY_PTR_ARY_INT) or the
             * column step (the value below is the row step's pending
             * VK_ROWADDR). Nothing is emitted yet - the scaled term
             * stays symbolic (VK_SCALED) until OP_PLUS has both terms,
             * see gen_subscript_2d(). Only a plain variable is
             * accepted as either index, the only shape any golden
             * confirms (a computed index would already occupy a
             * register the combined arithmetic has to share), and a
             * runtime index next to a constant one ("m[0][j]",
             * "m[i][2]") is refused too: v7/cc's acommute() would
             * fold the constant into the base address first, a shape
             * no golden shows. */
            const Val *below = g.valsp >= 1 ? &g.valstack[g.valsp - 1] : NULL;
            int row_step = (type == TY_PTR_ARY_INT);
            int col_step = (below && below->kind == VK_ROWADDR);
            if (below && below->kind == VK_MEM_DIRECT &&
                (row_step || below->rowbase))
                gen_fatal("a 2-D array subscript mixing a constant and a "
                          "runtime index is not yet supported - see "
                          "src/mutos_cc/README.md");
            if (row_step || col_step) {
                if (amt.kind != VK_MEM && amt.kind != VK_STATIC)
                    gen_fatal("a 2-D array subscript whose index is not a "
                              "plain variable is not yet supported - see "
                              "src/mutos_cc/README.md");
                if (row_step && !(below && below->kind == VK_REG &&
                                  strcmp(below->reg, "di") == 0))
                    gen_fatal("internal: 2-D subscript row step without "
                              "its base address in di");
                Val sc = {0};
                sc.kind = VK_SCALED;
                sc.cl = simple_of(amt);
                sc.imm = size.imm;
                push_val(&g, sc);
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
            /* Scaling by 1 - the index of a char array or char pointer
             * - leaves nothing to compute: v7/cc/c12.c optim() folds a
             * multiplication by 1 away before any code is chosen, so a
             * plain variable index stays an ordinary memory operand for
             * the PLUS to add straight in - 10_integ/04_strrev.s.golden's
             * reverse(): "&s[hi]" -> "mov di,*4.(bp)" / "add di,*8.(bp)"
             * (the pointer loaded, the index added from memory; loading
             * the index here first would give "mov di,*8.(bp)" / "add
             * di,*4.(bp)"). A computed index already in a register takes
             * the path below, which emits nothing for a scale of 1. */
            if (size.imm == 1 && (amt.kind == VK_MEM || amt.kind == VK_STATIC)) {
                push_val(&g, amt);
                break;
            }
            int use_si = di_busy(&g);
            const char *reg = use_si ? "si" : "di";
            if (use_si)
                load_into_si(&g, amt);
            else
                load_into_di(&g, amt);
            long sz = size.imm;
            if (sz != 1 && sz != 2 && sz != 4)
                gen_fatal("ITOP scaling by a non-power-of-two size "
                          "(%ld) is not yet supported", sz);
            for (long s = sz; s > 1; s /= 2)
                ins2(&g, "sal", o_reg(reg), o_shift1());
            push_val(&g, val_reg(reg));
            break;
        }

        case OP_STAR: {
            /* Pointer dereference - two confirmed shapes: */
            int type = c1_read_num(temp1, "temp1");
            /* "&*x" is just "x" - v7/cc/c12.c optim()'s very first
             * rule (AMPER whose operand is a STAR returns the STAR's
             * own operand). mutos_c0 emits the pair in exactly one
             * place: a 2-D subscript's row, dereferenced and then
             * immediately decayed again for the column subscript
             * (c0_parser.c's emit_subscript_2d()). Recognized here by
             * one opcode of lookahead (the same save/restore-file-
             * position technique as the peeks further below), the
             * AMPER consumed, and the operand left on the stack
             * untouched - confirmed by 02_array2d.s.golden/05_matmul.
             * s.golden, whose element addresses show no trace of the
             * row ever being loaded or stored. Only the two operand
             * kinds a row step can produce are accepted: a pending
             * runtime row address (VK_ROWADDR) or a fully constant one
             * (VK_MEM_DIRECT, then marked as a row - see its rowbase
             * field). */
            {
                long cancel_pos = ftell(temp1);
                int cancel_next = c1_read_op(temp1, "temp1");
                if (cancel_next == OP_AMPER) {
                    int atype = c1_read_num(temp1, "temp1");
                    if (g.valsp < 1)
                        gen_fatal("expression stack underflow - "
                                  "malformed temp1 stream");
                    Val *top = &g.valstack[g.valsp - 1];
                    int is_char = (type == TY_CHAR && atype == TY_PTR_CHAR);
                    int is_int = (type == TY_INT && atype == TY_PTR_INT);
                    if (!is_char && !is_int)
                        gen_fatal("'&*' with types %d/%d is not yet "
                                  "supported", type, atype);
                    if (top->kind == VK_REGOFF) {
                        /* "&a[j + 1]" (a pointer variable, see OP_PLUS):
                         * the pending constant is added now - 10_integ/
                         * 02_bubsort.s.golden's "add di,*4.(bp)" / "add
                         * di,*2." / "push di". A constant index on a
                         * pointer VARIABLE ("&p[2]", nothing in a
                         * register yet) has no golden. */
                        if (top->cl.kind != VK_REG)
                            gen_fatal("'&p[N]' with a constant index on a "
                                      "pointer variable is not yet "
                                      "supported - see src/mutos_cc/README.md");
                        Val ro = pop_any(&g);
                        ins2(&g, "add", o_reg(ro.cl.reg), o_imm(ro.imm));
                        push_val(&g, val_reg(ro.cl.reg));
                        break;
                    }
                    if (top->kind == VK_REG) {
                        /* "&s[i]" - an element's address already computed
                         * into a register: the address itself, nothing
                         * loaded - 10_integ/04_strrev.s.golden's
                         * "swapch(&s[lo], &s[hi]);" -> "mov di,*4.(bp)" /
                         * "add di,*8.(bp)" / "push di", 02_bubsort.s.
                         * golden's "&a[j]" -> ... "add di,*4.(bp)" /
                         * "push di". (A 2-D row step never leaves one -
                         * see below.) */
                        break;
                    }
                    if (is_char && top->kind == VK_MEM_DIRECT)
                        break;   /* "&buf[3]": the address, lea'd later */
                    if (is_int && top->kind == VK_MEM_DIRECT) {
                        top->rowbase = 1;
                        break;
                    }
                    if (is_int && top->kind == VK_ROWADDR)
                        break;
                    gen_fatal("'&' of this subscripted element is not yet "
                              "supported - see src/mutos_cc/README.md");
                }
                fseek(temp1, cancel_pos, SEEK_SET);
            }
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
            /* Any WORD-sized result (int or any pointer - e.g. 05_arrptr/
             * 05_arrofptr.1.golden's STAR(9), fetching a "char *" out of
             * "char *names[3]"): the same one-word load either way. A
             * 'char' result is a byte operand (see the TY_CHAR case
             * below); a 'long' one (a two-word load) is a different
             * instruction shape no golden shows yet. */
            if (!ty_is_word(type) && type != TY_CHAR)
                gen_fatal("STAR of type %d not yet supported (only an "
                          "int, char, pointer or function result is covered "
                          "so far)", type);
            Val ptr = pop_any(&g);
            /* Is this dereference the TARGET of a plain assignment, and
             * does the right-hand side need registers of its own (i.e.
             * is it anything but a lone constant)? Then the target's
             * address is pushed now and popped into BX for the store -
             * see VK_IND_PENDING and the comment further below. Decided
             * by what CONSUMES this STAR's value (scan_consumer()): an
             * OP_ASSIGN taking it as its left operand. An earlier
             * version keyed this on "nothing else on the value stack"
             * plus a peek for CON/ASSIGN, which also fired for a
             * dereference that is a whole condition or return value -
             * "if (*p)", "return *p;" - and silently emitted
             * "cmp <unpopped-ind-pending>,*0" (found while testing this
             * change; it predates it). A compound assignment through a
             * pointer or subscript ("a[i] += 2;") has no golden. */
            Consumer cons = scan_consumer(temp1);
            int is_target = (cons.op == OP_ASSIGN && cons.as_left && g.valsp == 0);
            int trivial_rhs = (cons.nops == 1 && cons.prev_op == OP_CON);
            if (cons.as_left && cons.op >= OP_ASPLUS && cons.op <= OP_ASXOR)
                gen_fatal("a compound assignment through a pointer or "
                          "subscript is not yet supported - see "
                          "src/mutos_cc/README.md");
            if (type == TY_CHAR) {
                /* A char element or a char through a pointer: a BYTE
                 * operand (Val's `bytev`), read or written only by a
                 * "movb" in its consumer - OP_ITOC (movb ax/cbw - see
                 * VK_CHARX) or a char OP_ASSIGN (via DX). Where its
                 * address comes from decides the operand:
                 *
                 * - a compile-time-known address ("buf[0]", "buf[80 -
                 *   1]"): that stack location itself - 09_abiprobe/
                 *   02_frame080.s.golden's "movb *-84.(bp),*1." / "movb
                 *   ax,*-5.(bp)";
                 * - a pointer VARIABLE ("*a"): loaded into DX, then moved
                 *   to BX, the byte addressed as "(bx)" - 10_integ/
                 *   04_strrev.s.golden's swapch(): "mov dx,*4.(bp)" /
                 *   "mov bx,dx" / "movb dx,(bx)", and the same pair
                 *   ahead of "movb ax,(bx)" / "cbw" in tests/mutos_as/
                 *   kernel_nonopt/lp_AC.s. (DX, the byte working
                 *   register, is where the pointer lands; it is no base
                 *   register, so it is copied to BX.) The int case
                 *   loads into DI instead ("mov di,p" / "(di)");
                 * - an address already in a base register (a runtime
                 *   subscript's DI): "(di)" - kernel_nonopt/amx.s's
                 *   "movb ax,(di)";
                 * - the target of an assignment whose right-hand side
                 *   has code of its own: pushed like the int case, the
                 *   store popping it into BX - swapch()'s "*a = *b;" and
                 *   "*b = t;" -> "push *4.(bp)" ... "pop bx" / "movb
                 *   (bx),dx". A constant stored through a char pointer
                 *   ("*p = 'x';") has no golden, nor has a constant index
                 *   on a char pointer ("p[2]"); both are refused. */
                if (ptr.kind == VK_MEM_DIRECT) {
                    Val m = val_mem(ptr.offset);
                    m.bytev = 1;
                    push_val(&g, m);
                    break;
                }
                int basereg = (ptr.kind == VK_REG &&
                               (strcmp(ptr.reg, "di") == 0 ||
                                strcmp(ptr.reg, "si") == 0 ||
                                strcmp(ptr.reg, "bx") == 0));
                if (is_target) {
                    if (!cons.saw_runtime)
                        gen_fatal("storing a constant through a 'char' pointer "
                                  "is not yet supported - see "
                                  "src/mutos_cc/README.md");
                    if (ptr.kind != VK_MEM && !basereg)
                        gen_fatal("storing through this 'char' pointer shape "
                                  "is not yet supported - see "
                                  "src/mutos_cc/README.md");
                    ins1(&g, "push", o_val(ptr));
                    push_val(&g, val_ind_pending());
                    break;
                }
                Val ind;
                if (ptr.kind == VK_MEM) {
                    ins2(&g, "mov", o_reg("dx"), o_val(ptr));
                    ins2(&g, "mov", o_reg("bx"), o_reg("dx"));
                    ind = val_ind("bx");
                } else if (basereg) {
                    ind = val_ind(ptr.reg);
                } else {
                    gen_fatal("reading a 'char' through this pointer shape is "
                              "not yet supported - see src/mutos_cc/README.md");
                }
                ind.bytev = 1;
                push_val(&g, ind);
                break;
            }
            if (ptr.kind == VK_REGOFF) {
                /* See VK_REGOFF. The pointer operand is loaded (or, as a
                 * non-trivial assignment target, pushed) only here, so
                 * nothing is emitted for an unsupported shape. The
                 * pushed form is confirmed for a zero offset only:
                 * 10_integ/03_linklist.s.golden's "cur->val = i;"
                 * (the member at offset 0 - NAME CON(0) PLUS STAR) ->
                 * "push *-8.(bp)" / "mov di,*-10.(bp)" / "pop bx" /
                 * "mov (bx),di". With a non-zero offset that golden
                 * shows a different, right-operand-first shape
                 * ("cur->next = head;" -> "mov di,head" / "mov si,cur"
                 * / "mov *2.(si),di"), so that is refused. */
                if (is_target && !trivial_rhs) {
                    if (type != TY_INT || ptr.imm != 0)
                        gen_fatal("storing a computed value through a pointer "
                                  "plus a non-zero constant offset is not "
                                  "yet supported - see src/mutos_cc/README.md");
                    Val base = val_from_simple(ptr.cl);
                    ins1(&g, "push", o_val(base));
                    push_val(&g, val_ind_pending());
                    break;
                }
                Val base = val_from_simple(ptr.cl);
                const char *reg = "di";
                if (base.kind == VK_REG)
                    reg = base.reg;
                else
                    load_into_di(&g, base);
                push_val(&g, val_ind_disp(reg, ptr.imm));
                break;
            }
            if (ptr.kind == VK_SCALED || ptr.kind == VK_ROWADDR)
                gen_fatal("a 2-D array subscript used in a shape other than a "
                          "complete \"m[i][j]\" element reference is not yet "
                          "supported - see src/mutos_cc/README.md");
            if (ptr.kind == VK_STATICADDR || ptr.kind == VK_FUNCADDR)
                gen_fatal("dereferencing the address of a string literal "
                          "or function is not yet supported - see "
                          "src/mutos_cc/README.md");
            if (ptr.kind == VK_MEM_DIRECT) {
                /* Dereferencing a compile-time-fully-known address
                 * ("v[0]" - see VK_MEM_DIRECT's own comment) is just
                 * that memory location itself - no load, no
                 * indirection, not even the deferred-address-spill
                 * logic below (nothing was ever computed into a
                 * register in the first place, so there is nothing
                 * to protect from the right-hand side's own
                 * register use). The result is an ordinary memory
                 * operand (VK_MEM), NOT the VK_MEM_DIRECT it came
                 * from: that kind means "the ADDRESS of this location,
                 * not yet computed", and the consumers that meet one
                 * materialize it with "lea" (OP_ASSIGN's rhs,
                 * gen_call()). Passing the address kind through
                 * unchanged made "a = v[1];" store &v[1] and "f(v[2])"
                 * push &v[2] - silently, with no golden exercising
                 * either (found by fuzzing "strcpy(s, tab[1])"; the
                 * bug predates string literals). As an assignment
                 * TARGET ("v[0] = 1;", 04_ptrarreq.s.golden's "mov
                 * *-12.(bp),*1.") the two kinds render identically. */
                push_val(&g, val_mem(ptr.offset));
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
             * simply "push\tdi"), and 10_integ/02_bubsort.s.golden's
             * swap(): "*a = *b;" and "*b = t;" both push. A lone
             * constant right-hand side does NOT push - "*p = 20;"/
             * "*p++ = 1;" (05_incdec.s.golden) store straight through
             * DI. is_target/trivial_rhs (above) make that decision; a
             * dereference that is not an assignment target - a
             * condition, a return value, an operand, the first half of
             * a chained "**pp" - always takes the plain VK_IND path. */
            if (is_target && !trivial_rhs) {
                /* Confirmed only for an 'int' target (every golden
                 * above); a pointer-typed one ("names[i] = s;") has no
                 * golden. */
                if (type != TY_INT)
                    gen_fatal("storing a computed value through a pointer to "
                              "a pointer is not yet supported - see "
                              "src/mutos_cc/README.md");
                ins1(&g, "push", o_val(ptr));
                push_val(&g, val_ind_pending());
                break;
            }
            load_into_di(&g, ptr);
            if (cons.as_left && find_relop(cons.op) && cons.nops > 1) {
                /* The left operand of a comparison whose right operand
                 * has code of its own: the value is loaded now, before
                 * that code runs - v7's template computes the left
                 * operand into its register first - and the right
                 * operand's address then goes through SI (see OP_ITOP):
                 * 10_integ/02_bubsort.s.golden's "a[j] > a[j + 1]" ->
                 * ... "add di,*4.(bp)" / "mov di,(di)" / "mov si,
                 * *-8.(bp)" / ... / "cmp di,*2.(si)". (With a lone
                 * variable or constant on the right, the load - if any -
                 * happens at the compare, emit_cmp_and_branch(), the
                 * same bytes.) */
                ins2(&g, "mov", o_reg("di"), o_val(val_ind("di")));
                push_val(&g, val_reg("di"));
                break;
            }
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
            push_val(&g, gen_incdec(&g, op, lv, amt));
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
             * context, or gen_cond_branch(), for a condition - an "if",
             * or an operand of "&&"/"||"/"?:") decides how to compile
             * it. Both
             * operands are run through materialize() first only to
             * cover the (unconfirmed by any golden) chained-relational
             * edge case "a < b < c", where a nested comparison could
             * otherwise flow in here as an operand. */
            Val l, r;
            pop_operands(&g, &l, &r);
            int relop = op;
            if (is_const_val(&l) && !is_const_val(&r)) {
                /* v7's optim(): exchanged, relation mirrored - see
                 * constant_to_right(). */
                Val t = l;
                l = r;
                r = t;
                relop = find_relop(op)->mirror;
            } else if (is_name_val(&l) && is_computed_val(&r)) {
                /* The same rule for two non-constant operands: v7/cc/
                 * c12.c optim() exchanges them when degree(left) <
                 * degree(right), or when the degrees are equal and the
                 * left one is a NAME and the right one is not - so a
                 * variable compared with anything computed ends up on
                 * the right: 10_integ/02_bubsort.s.golden's "i < n - 1"
                 * -> "mov di,*6.(bp)" / "dec di" / "cmp di,*-6.(bp)" /
                 * "ble" (n - 1 > i, branching when false), and "j < n -
                 * 1 - i" alike. A NAME's degree is 0, and so is that of
                 * anything built from NAMEs and constants with one
                 * register ("n - 1", "*p"), so a computed right operand
                 * always wins the tie; neither operand's code moves
                 * (the NAME has none), only the comparison's direction.
                 * Two NAMEs ("lo >= hi") and a computed left operand
                 * stay as they are. */
                Val t = l;
                l = r;
                r = t;
                relop = find_relop(op)->mirror;
            }
            Val c = {0};
            c.kind = VK_COND;
            c.true_op = relop;
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
             * "simpif" shortcut - see c0_parser.c). Only a condition
             * with no "&&"/"||"/"?:"/"," gets here: one with any of
             * them is planned as a whole, CBRANCH included (see
             * plan_expression()). The whole statement is the
             * condition, so every queued postfix fixup is its own -
             * see gen_cond_branch(). */
            int lbl = c1_read_num(temp1, "temp1");
            int cond_sense = c1_read_num(temp1, "temp1");
            (void)c1_read_num(temp1, "temp1"); /* source line - not
                                                 * rendered into the
                                                 * .s output, same as
                                                 * OP_EXPR's. */
            gen_cond_branch(&g, pop_val(&g), lbl, cond_sense, g.defer_floor);
            break;
        }

        case OP_LOGAND:
        case OP_LOGOR:
        case OP_COLON:
        case OP_QUEST:
        case OP_SEQNC:
            /* Always generated through an evaluation-order plan (see
             * plan_expression() and the "Conditional evaluation"
             * section), which never streams these opcodes themselves. */
            gen_fatal("internal: opcode %d reached outside an evaluation-"
                      "order plan", op);

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

        case OP_COMMA: {
            /* An OP_CALL argument-list separator (NOT the comma
             * operator - that is the entirely distinct OP_SEQNC, see
             * plan_value()). Builds a VK_ARGLIST left-associatively, exactly
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
            /* A pointer result comes back in AX exactly like an int
             * (docs/MUTOS_C_ABI.md sect. 1.5) - 05_arrptr/07_strlibc's
             * "char *strcpy();" calls, CALL type 9, whose (discarded)
             * results need no code at all. */
            if (!ty_is_word(type) && type != TY_LONG)
                gen_fatal("a call returning type %d is not yet supported "
                          "(only a function returning an int, a long or a "
                          "pointer is covered so far)", type);
            Val args = pop_val(&g);
            Val callee = pop_val(&g);
            push_val(&g, gen_call(&g, callee, args, type == TY_LONG));
            break;
        }

        case OP_TIMES: {
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_LONG) {
                Val r = pop_val(&g);
                Val l = pop_val(&g);
                constant_to_right(&l, &r);
                push_val(&g, gen_long_binop_call(&g, l, r, "lmul"));
                break;
            }
            if (type != TY_INT)
                gen_fatal("TIMES of type %d not yet supported", type);
            if (g.valsp >= 1 && g.valstack[g.valsp - 1].kind == VK_STACKED) {
                /* The right operand was evaluated first and spilled (see
                 * the "Evaluation order" section): the left one, just
                 * computed, goes from its working register into AX, the
                 * spilled one is popped into CX, and CX is the IMUL
                 * operand - 10_integ/05_matmul.s.golden's "mov\tdi,(di)"
                 * / "mov\tax,di" / "pop\tcx" / "imul\tcx" (v7/cc/
                 * table.s's "%n,n": SS, F, "mul (sp)+,R"). A left
                 * operand behind a pointer is loaded in place first, as
                 * every dereferenced operand is. */
                (void)pop_any(&g);
                Val l = materialize(&g, pop_val(&g));
                if (l.kind == VK_IND) {
                    ins2(&g, "mov", o_reg(l.reg), o_val(l));
                    l = val_reg(l.reg);
                }
                if (l.kind != VK_REG || strcmp(l.reg, "ax") == 0)
                    gen_fatal("internal: the left operand of a multiply with "
                              "a spilled right operand is expected in a "
                              "working register other than ax");
                ins2(&g, "mov", o_reg("ax"), o_reg(l.reg));
                ins1(&g, "pop", o_reg("cx"));
                ins1(&g, "imul", o_reg("cx"));
                push_val(&g, val_reg("ax"));
                break;
            }
            Val l, r;
            pop_operands(&g, &l, &r);
            constant_to_right(&l, &r);
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
            if (imul_side.kind == VK_IMM) {
                /* A constant multiplier: 8086 IMUL has no immediate
                 * form, so the constant is loaded into CX first and
                 * multiplied from there - confirmed against 05_arrptr/
                 * 02_array2d.s.golden's "i * 10" -> "mov\tax,*-30.
                 * (bp)" / "mov\tcx,*10." / "imul\tcx". Only for a
                 * multiplier that is not a power of two (and not 0):
                 * v7/cc/c12.c's optim() turns a power-of-two TIMES
                 * into a shift and acommute() drops a "* 1", shapes
                 * no golden confirms for OP_TIMES itself (06_compasgn's
                 * "a *= 2" -> "sal" is the compound-assignment
                 * analogue only). */
                if (imul_side.imm == 0 || exact_log2(imul_side.imm) >= 0)
                    gen_fatal("multiplying by the constant %ld (zero or a "
                              "power of two) is not yet supported - no "
                              "golden reference confirms the strength-"
                              "reduced shape a real compiler would emit",
                              imul_side.imm);
                if (ax_side.kind == VK_IMM)
                    gen_fatal("multiplying two constants at run time is "
                              "not expected (mutos_c0 folds them)");
                if (!(ax_side.kind == VK_REG && strcmp(ax_side.reg, "ax") == 0))
                    require_free(imul_side, RB_AX, "TIMES");
                ins2(&g, "mov", o_reg("ax"), o_val(ax_side));
                ins2(&g, "mov", o_reg("cx"), o_val(imul_side));
                ins1(&g, "imul", o_reg("cx"));
                push_val(&g, val_reg("ax"));
                break;
            }
            if (!(ax_side.kind == VK_REG && strcmp(ax_side.reg, "ax") == 0))
                require_free(imul_side, RB_AX, "TIMES");
            ins2(&g, "mov", o_reg("ax"), o_val(ax_side));
            ins1(&g, "imul", o_val(imul_side));
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
                push_val(&g, gen_long_binop_call(&g, l, r, helper));
                break;
            }
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val l, r;
            pop_operands(&g, &l, &r);
            if (r.kind == VK_IMM)
                gen_fatal("dividing by an immediate is not yet supported "
                          "(8086 IDIV takes a reg/mem operand, never an "
                          "immediate directly - no golden reference "
                          "confirms the alternate sequence a real "
                          "compiler would need here)");
            require_free(r, RB_AX | RB_DX, aluop(op)->name);
            emit_cwd_from(&g, o_val(l));
            ins1(&g, "idiv", o_val(r));
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
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val rhs = materialize(&g, pop_val(&g));
            Val lhs = pop_val(&g);
            if (lhs.kind != VK_MEM)
                gen_fatal("compound assignment to a non-memory lvalue is "
                          "not yet supported");
            if (rhs.kind != VK_IMM)
                gen_fatal("compound assignment with a non-constant "
                          "right-hand side is not yet supported (no "
                          "golden reference confirms the register-operand "
                          "sequence a real compiler would need here)");
            ins2(&g, aluop(op)->mnem, o_val(lhs), o_val(rhs));
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
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val rhs = materialize(&g, pop_val(&g));
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
            if (rhs.imm > MCC_SHIFT_REPEAT_MAX)
                gen_fatal("compound shift-assignment by a constant count "
                          "above %d is not yet supported (real compiler "
                          "output shifts a REGISTER by CL from a count of "
                          "3 up - see MCC_SHIFT_REPEAT_MAX - but no golden "
                          "confirms the shape for a memory operand)",
                          MCC_SHIFT_REPEAT_MAX);
            for (long i = 0; i < rhs.imm; i++)
                ins2(&g, aluop(op)->mnem, o_val(lhs), o_shift1());
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
            Val rhs = materialize(&g, pop_val(&g));
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
            if (shift > MCC_SHIFT_REPEAT_MAX)
                gen_fatal("'*=' by a power of two above %d is not yet "
                          "supported (its shift count exceeds %d - see "
                          "MCC_SHIFT_REPEAT_MAX; no golden confirms the "
                          "shift-by-CL shape for a memory operand)",
                          1 << MCC_SHIFT_REPEAT_MAX, MCC_SHIFT_REPEAT_MAX);
            for (int i = 0; i < shift; i++)
                ins2(&g, "sal", o_val(lhs), o_shift1());
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
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val rhs = materialize(&g, pop_val(&g));
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
            emit_cwd_from(&g, o_val(lhs));
            ins2(&g, "mov", o_reg("cx"), o_val(rhs));
            ins1(&g, "idiv", o_reg("cx"));
            ins2(&g, "mov", o_val(lhs), o_reg(op == OP_ASDIV ? "ax" : "dx"));
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
                rhs = materialize_long(&g, rhs); /* VK_LCON -> VK_LONG,
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
                ins2(&g, "mov", o_mem(lhs.offset + MCC_SZINT), o_reg("si"));
                ins2(&g, "mov", o_mem(lhs.offset), o_reg("di"));
                push_val(&g, rhs);
                break;
            }
            /* int, char, or any pointer (TY_PTR_FUNC_INT, "int **",
             * 05_arrptr/05_arrofptr.1.golden's "char *" element ASSIGN(9)
             * - one word, whatever it points to). */
            if (!ty_is_word(type) && type != TY_CHAR)
                gen_fatal("ASSIGN of type %d not yet supported", type);
            Val rhs = pop_val_ex(&g, type == TY_CHAR ? POP_BYTE : POP_CHARX);
            if (rhs.kind == VK_CHARX) {
                /* A char stored into an int (mutos_c0's ITOC(TY_INT) on
                 * the right-hand side): loaded into AX and stored from
                 * there - the real non-optimized compiler's "movb ax,
                 * *23.(di)" / "cbw" / "mov *-14.(bp),ax" (tests/mutos_as/
                 * kernel_nonopt/amx.s; likewise "mov *-32.(bp),ax",
                 * "mov *-20.(bp),ax", "mov *-30.(bp),ax" there). */
                rhs = load_charx(&g, rhs);
            }
            if (rhs.bytev) {
                /* char = char (no conversion on the wire): the 8086 has
                 * no memory-to-memory MOVB, so the source byte goes
                 * through DX - the byte working register - first, ahead
                 * of the "pop bx" of a pushed target: 10_integ/04_strrev.
                 * s.golden's swapch(), "t = *a;" -> ... "movb dx,(bx)" /
                 * "movb *-6.(bp),dx", "*a = *b;" -> ... "movb dx,(bx)" /
                 * "pop bx" / "movb (bx),dx", "*b = t;" -> "push *6.(bp)" /
                 * "movb dx,*-6.(bp)" / "pop bx" / "movb (bx),dx". */
                rhs.bytev = 0;
                ins2(&g, "movb", o_reg("dx"), o_val(rhs));
                rhs = val_reg("dx");
            }
            rhs = materialize(&g, rhs);
            if (rhs.kind == VK_MEM_DIRECT) {
                /* A deferred address-of, finally materialized here -
                 * "p = &x;"/"p = a;"/"pp = &p;" (see OP_AMPER's own
                 * comment for why this is deferred at all) - confirmed
                 * against 05_incdec.s.golden's "lea\tdi,*-16.(bp)" and
                 * 06_ptrptr.s.golden's two "lea"s. */
                ins2(&g, "lea", o_reg("di"), o_val(rhs));
                rhs = val_reg("di");
            }
            Val lhs = pop_lvalue_ex(&g, type == TY_CHAR ? POP_BYTE : 0);
            if (type == TY_CHAR && !lhs.bytev && lhs.kind != VK_IND_PENDING)
                gen_fatal("internal: a char assignment whose target is not a "
                          "char in memory");
            lhs.bytev = 0;
            if (lhs.kind == VK_IND_PENDING &&
                (rhs.kind == VK_IND || rhs.kind == VK_MEM ||
                 rhs.kind == VK_MEM_CVT || rhs.kind == VK_STATIC)) {
                /* The store will be "mov (bx),<rhs>", and the 8086 has
                 * no memory-to-memory MOV: a right-hand side that is
                 * still a memory operand is loaded into a register
                 * first - BEFORE the "pop bx" - a dereference in place
                 * ("mov di,(di)"), anything else into DI. Confirmed by
                 * 10_integ/02_bubsort.s.golden's swap(): "*a = *b;" ->
                 * "push *4.(bp)" / "mov di,*6.(bp)" / "mov di,(di)" /
                 * "pop bx" / "mov (bx),di", and "*b = t;" -> "push
                 * *6.(bp)" / "mov di,*-6.(bp)" / "pop bx" / "mov
                 * (bx),di" (before, the first emitted the impossible
                 * "mov (bx),(di)" and the second was refused). */
                if (rhs.kind == VK_IND) {
                    ins2(&g, "mov", o_reg(rhs.reg), o_val(rhs));
                    rhs = val_reg(rhs.reg);
                } else {
                    load_into_di(&g, rhs);
                    rhs = val_reg("di");
                }
            }
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
                require_free(rhs, RB_BX, "ASSIGN");
                ins1(&g, "pop", o_reg("bx"));
                lhs = val_ind("bx");
            }
            if (lhs.kind != VK_MEM && lhs.kind != VK_MEM_DIRECT &&
                lhs.kind != VK_IND && lhs.kind != VK_STATIC &&
                lhs.kind != VK_REG)
                gen_fatal("assignment to a non-lvalue is not yet supported");
            if (rhs.kind == VK_IND && (lhs.kind == VK_MEM || lhs.kind == VK_MEM_DIRECT ||
                                       lhs.kind == VK_STATIC || lhs.kind == VK_IND)) {
                /* The rhs is itself a dereferenced pointer ("y =
                 * *p;" - 05_arrptr/03_ptrbasic.c) and the lhs is a
                 * plain memory operand - 8086 MOV cannot take two
                 * memory operands ("*-8.(bp)" and "(di)" both are),
                 * so the rhs must be materialized into a real
                 * register first - confirmed against 03_ptrbasic.
                 * s.golden's "y = *p;": "mov di,*-10.(bp)" (the STAR
                 * itself, loading the pointer) / "mov di,(di)" (THIS
                 * step) / "mov *-8.(bp),di". */
                require_free(lhs, reg_bit(rhs.reg), "ASSIGN");
                ins2(&g, "mov", o_reg(rhs.reg), o_val(rhs));
                rhs = val_reg(rhs.reg);
            }
            if (rhs.kind == VK_MEM_CVT) {
                /* A type-converted memory operand (currently only
                 * OP_LTOI's result - see VK_MEM_CVT's own comment
                 * above) - confirmed to go through DI first, unlike a
                 * bare-NAME VK_MEM rhs (still unconfirmed, see just
                 * below). */
                require_free(lhs, RB_DI, "ASSIGN");
                load_into_di(&g, rhs);
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
                g.regvar_dirty &= ~reg_bit(lhs.reg); /* assigned: valid again */
                push_val(&g, rhs);
                break;
            }
            /* TY_CHAR uses "movb" instead of "mov" - confirmed against
             * 08_castsize.s.golden's "c = (char) i;" -> "movb
             * *-12.(bp),dx". */
            if (lhs.kind == VK_REG) {
                /* A store INTO a 'register' local - the one write to a
                 * reserved register note_writes() must not refuse. */
                ins2_regvar_store(&g, type == TY_CHAR ? "movb" : "mov",
                                  o_val(lhs), o_val(rhs));
                g.regvar_dirty &= ~reg_bit(lhs.reg); /* assigned: valid again */
            } else {
                ins2(&g, type == TY_CHAR ? "movb" : "mov", o_val(lhs), o_val(rhs));
            }
            if (rhs.kind == VK_FUNCADDR)
                free((char *)rhs.reg); /* only after it was rendered */
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
                Val v = materialize_long(&g, pop_val(&g));
                if (v.kind != VK_LONG)
                    gen_fatal("RFORCE to 'long' from a non-'long' value is "
                              "not yet supported (no golden reference "
                              "confirms the widening sequence a real "
                              "compiler would need here)");
                put_seq(&g, SEQ_DISI_TO_AXDX);
                break;
            }
            if (type != TY_INT)
                gen_fatal("RFORCE to type %d not yet supported (only "
                          "int/long-returning functions are covered so "
                          "far)", type);
            Val v = pop_val_ex(&g, POP_CHARX);
            if (v.kind == VK_CHARX) {
                /* A char returned from an int function (mutos_c0's
                 * ITOC(TY_INT) before RFORCE, as v7/cc/c04.c's doret()
                 * converts through an assignment to the function's
                 * type): movb/cbw already leave it in AX, the return
                 * register - 10_integ/04_strrev.s.golden's "return
                 * buf[0];" -> "movb ax,*-24.(bp)" / "cbw" / "jmp L9". */
                v = load_charx(&g, v);
            }
            v = materialize(&g, v);
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
                if (g.reserved & RB_DI) {
                    /* DI is reserved by a live 'register'-class local
                     * for the rest of this function (see reserved's
                     * own comment) - SI is used as the fallback
                     * working register instead, confirmed against
                     * 04_funcs/06_regclass.s.golden's "return sum;" ->
                     * "mov si,*-6.(bp) / mov ax,si" (never DI). */
                    load_into_si(&g, v);
                    ins2(&g, "mov", o_reg("ax"), o_reg("si"));
                } else {
                    load_into_di(&g, v);
                    ins2(&g, "mov", o_reg("ax"), o_reg("di"));
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
                discard_val(&g);
            /* Flush any postfix ++/-- fixup queued by gen_incdec()
             * during this statement - see DEFERRED_MAX's comment on
             * GenState. */
            flush_deferred(&g);
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
            gen_switch_dispatch(&g, deflab, cases, ncases);
            break;
        }

        case OP_RETRN: {
            int type = c1_read_num(temp1, "temp1");
            put_line(&g, "|RTYP %d", type);
            ins1(&g, "jmp", o_sym("cret"));
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
            } else if (extra <= MCC_SUBSP_MAX) {
                /* A plain "sub sp,N" - confirmed against 01_intarith.
                 * s.golden's "L1:sub\tsp,*6.\njmp\tL2" and, as the
                 * largest confirmed size, 09_abiprobe/02_frame080.s.
                 * golden's "L1:sub\tsp,*80." (see MCC_SUBSP_MAX). N is
                 * an ordinary immediate (render_operand()'s rule). */
                ins2(&g, "sub", o_reg("sp"), o_imm(extra));
            } else if (extra >= MCC_CHKSTK_MIN) {
                /* "mov ax,N / call chkstk" - confirmed against every
                 * 09_abiprobe/03..07 golden (N = 128/176/224/256/300),
                 * e.g. 07_frame300.s.golden's "L1:mov\tax,#300.":
                 * N is an ordinary immediate, so every N here (all >=
                 * 128, too big for a signed byte) takes the word-sized
                 * '#' marker. (Before these goldens existed this path
                 * hard-coded '*' - an unverified guess, now corrected -
                 * and was reachable only above 256 bytes.) */
                ins2(&g, "mov", o_reg("ax"), o_imm(extra));
                ins1(&g, "call", o_sym("chkstk"));
            } else {
                gen_fatal("local-frame size %d bytes falls in the "
                          "unconfirmed %d..%d-byte gap between the largest "
                          "confirmed plain \"sub sp,N\" and the smallest "
                          "confirmed \"call chkstk\" (see "
                          "docs/MUTOS_C_ABI.md sect. 1.9 and "
                          "tests/mutos_cc/09_abiprobe/) - not yet "
                          "supported rather than guessing", extra,
                          MCC_SUBSP_MAX + 1, MCC_CHKSTK_MIN - 1);
            }
            break;
        }

        default:
            gen_fatal("unsupported temp1 opcode %d (0x%02x) - not yet "
                      "covered by mutos_c1; see src/mutos_cc/README.md "
                      "for its current opcode coverage",
                      op, op);
        }
    }

    /* The code ends with ".data" unconditionally - every golden has
     * it, string literals or not (v7/cc/c10.c's main() prints
     * ".globl\n.data" there; the MUTOS c1 only the ".data") - and the
     * string file follows. */
    ins0(&g, ".data");
    gen_strings(&g, temp2);
    return 0;
}
