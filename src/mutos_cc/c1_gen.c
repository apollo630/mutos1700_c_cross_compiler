/*
 * c1_gen.c - mutos_c1 code generator.
 *
 * Current opcode coverage (matches mutos_c0's current grammar
 * coverage - see c0_parser.c and src/mutos_cc/README.md): SYMDEF,
 * PROG, EVEN, RLABEL, SAVE, SETREG, BRANCH, LABEL, ANAME, RNAME, BSS,
 * SSPACE, SNAME, CSPACE, NLABEL, DATA, BDATA, NAME, CON, LCON, FCON,
 * LTOI, ITOC, CTOL, ITOL, ITOF, FTOI, FTOL, PLUS, MINUS,
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
#include "c1_fltdec.h"
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
 * MUTOS_C_ABI.md sect. 1.9): a frame (bytes beyond the SAVE prologue's
 * own register-save area) of up to 98 bytes is allocated with a plain
 * "sub sp,N", one of 100 bytes or more with "mov ax,N / call chkstk".
 * The switch is exactly at 100: fltprobe/p29_frame2.s.golden's f92,
 * f94, f96 and f98 all "sub sp,*N." (p27_frame's 82 and 90, 09_abiprobe/
 * 02_frame080's 80 before them), p27_frame.s.golden's f100 "mov ax,
 * *100." / "call chkstk" (110, 120, 124, 126 and 128 too; 09_abiprobe/
 * 03_frame128's 128 before them). A frame is always even (p27's 127-byte
 * array rounded up to 128), so no size is left between the two. */
#define MCC_CHKSTK_MIN 100

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
               VK_STACKED, VK_CHARX, VK_IDXOFF, VK_SYMIDX, VK_FIELD,
               VK_FMEM, VK_FCON, VK_FACC, VK_FDONE, VK_LOWADD } ValKind;
/* VK_LOWADD - "(int) (l + c)" / "(int) (l - c)", not computed yet: the
 * low word of the long variable at bp offset `offset` plus the int
 * constant `imm`. v7/cc/c12.c's unoptim() distributes an LTOI over a
 * '+'/'-' (LTOI(l) the low word, LTOI(LCON) its low word), so the real
 * compiler adds two words - fltprobe/p19_open3.s.golden's "r = r + (int)
 * (l - 100000);" -> "mov di,*-6.(bp)" / "add di,*-34.(bp)" / "add
 * di,#31072." (see OP_PLUS's int case for where it is consumed). */
/* VK_FMEM/VK_FCON/VK_FACC/VK_FDONE - floating-point values (08_float -
 * see the "Floating point" section). VK_FMEM: a 'float' or 'double'
 * local in memory (`offset` its bp offset, Val's `fdouble` which); VK_FCON:
 * a floating constant, already written to .data under label `offset` as
 * a single-precision ".float"; VK_FACC: the value on top of the runtime's
 * floating-point stack (libc.a's stacks.o - "fpstk"/"fpsp"), where every
 * floating computation happens; VK_FDONE: a floating assignment's value,
 * already stored and popped off that stack ("fstsp"/"fstdp") - it can
 * only be discarded. Only the floating handlers take any of them (pop_
 * val_ex()'s POP_FLOAT); every other consumer refuses one. */
/* VK_FIELD - a bit-field as an assignment TARGET: the word holding it
 * (`cl` - VK_MEM, VK_STATIC or VK_IND) and the field's position in it
 * (Val's `bitoffs`/`flen`), produced by OP_FSEL only when an OP_ASSIGN
 * takes it as its left operand; that ASSIGN is its only consumer
 * (gen_field_store()), pop_val() refuses it anywhere else. */
/* VK_SYMIDX - "the file-scope char array `sym`, indexed by the plain
 * int variable `cl` (VK_MEM or VK_STATIC)", NOT computed yet: OP_PLUS's
 * result for "text[i]" (NAME(SC_EXTERN, TY_CHAR, "_text") AMPER(9)
 * NAME(i) CON(1) ITOP(9) PLUS(9) - the ITOP by 1 leaves `i` itself),
 * produced only when the OP_STAR that dereferences it comes next.
 * OP_STAR then loads the index - the real compiler's F* for an
 * operand "*(&text + i)": the index into DX, the byte working
 * register, and on into BX, since DX can address nothing - and makes
 * the element the byte operand "_text(bx)" (VK_IND, `sym` set):
 * 10_integ/01_wordcount.s.golden's "mov dx,*-6.(bp)" / "mov bx,dx" /
 * "cmpb _text(bx),*10.", and tests/mutos_as/kernel_nonopt/amx.s's
 * "mov dx,*-10.(bp)" / "mov bx,dx" / "movb dx,#_amxcmd(bx)". The
 * address is a link-time constant plus a register, which is why no
 * "lea" or "add" appears at all - unlike a local array's "lea di,
 * <base>" / "add di,<index>". Only OP_STAR accepts one (pop_val()
 * refuses it). */
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
/* VK_STATIC - a variable at a fixed, link-time address: a local
 * STATIC variable's own dedicated-label memory reference (`offset`
 * holds the internal LABEL number, not a bp-relative stack offset -
 * see OP_NAME's SC_STATIC case below and c0_sym.h's
 * symtab_declare_static() comment), or - `sym` set - a FILE-SCOPE
 * variable, named by its symbol (OP_NAME's SC_EXTERN case for a non-
 * function type). Renders as a bare "L<n>" or "_name" operand
 * (render_operand()) - confirmed against 04_funcs/05_staticvar.
 * s.golden's "mov di,L4"/"mov L4,di" and 07_scope/01_globstat.s.
 * golden's "mov di,_counter"/"mov _counter,di" (03_externdef: "mov
 * di,_total"): a different SOURCE-TEXT shape from VK_MEM's "*N.(bp)"
 * but otherwise usable identically - both are just an addressable
 * memory operand, and every golden shows the file-scope variable in
 * exactly the instruction shapes the local one gets ("counter =
 * counter + 1;" -> "mov di,_counter" / "inc di" / "mov _counter,di",
 * as "n = n + 1;" -> "mov di,L4" / "inc di" / "mov L4,di"). The real
 * non-optimized kernel output has the same operand in further shapes
 * ("cmp _amxdebu,*2.", "mov dx,_namx", "mov _cfreeli,dx" in tests/
 * mutos_as/kernel_nonopt/). A memory-to-memory MOV is illegal for it
 * as for VK_MEM, so OP_ASSIGN refuses one as a right-hand side unless
 * the target is a register (see there). */
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
    int     lreg;     /* see Val's own field of the same name */
    int     lpair;    /* see Val's own field of the same name */
    int     fromu;    /* see Val's own field of the same name */
    const char *sym;  /* see Val's own field of the same name */
    int     postfix;  /* see Val's own field of the same name */
    int     flagsv;   /* see Val's own field of the same name */
    long    flags_at; /* see Val's own field of the same name */
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
    const char *sym;  /* VK_STATIC/VK_STATICADDR: a file-scope
                        * variable's symbol ("_counter") - NULL for a
                        * label-numbered local static or string
                        * literal (`offset` is the label then). Never
                        * owned by the Val: the text lives in
                        * GenState's name pool (intern_name()) until
                        * c1_generate() returns, so a Val can be copied
                        * freely. */
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
    int       cond_is_byte; /* VK_COND only: a char in memory (cl - a
     * `bytev` operand, whose flag the SimpleVal does not keep) compared
     * with a char-typed constant (cr - CON(TY_CHAR, 0..127), which
     * mutos_c0 writes for "text[i] == '\n'"; see OP_CON): a BYTE
     * compare, emit_cmp_and_branch()'s "cmpb" / "movb"+"orb" shapes. */
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
    int       charcon;   /* VK_IMM only: a CON typed TY_CHAR - the
     * right operand of a char compared with a small constant (see
     * cond_is_byte); OP_CON checks that it is consumed exactly there. */
    int       flagsv;    /* VK_REG only: the value was just computed by an
     * AND in its own register (see gen_charx_binop() and OP_AND), which
     * left ZF/SF set from it; `flags_at` is GenState.ninsn right after
     * that AND. A truth test of it that follows with no instruction in
     * between branches on those flags directly, with no "cmp reg,*0" -
     * the real compiler's "movb dx,#_amxscd(bx)" / "and dx,*12." / "beq"
     * and "call _inb" / "add sp,*2." / "and ax,*9." / "bne" (tests/
     * mutos_as/kernel_nonopt/amx.s, lp_AC.s); see gen_cond_branch(). */
    long      flags_at;
    int       structv;   /* VK_MEM only: a whole struct in memory (a NAME of
     * type TY_STRUCT) - consumed only by OP_AMPER ("&p") and a struct
     * OP_ASSIGN (see OP_STRASG); pop_val() refuses it elsewhere. */
    int       bitoffs, flen; /* VK_FIELD only: the field's lowest bit and
     * its width - OP_FSEL's two arguments */
    int       fdouble;   /* VK_FMEM only: 1 for a 'double' (8 bytes, the
     * runtime's "...d" entry points), 0 for a 'float' (4 bytes, "...s") */
    int       lpair;     /* VK_LONG only: the value is in SI(high):DX(low),
     * not DI:SI - DI holds a register variable, and v7's register
     * allocation takes the next pair (see emit_dxax_to_long()) */
    int       fmode;     /* VK_FMEM only: where it is - FM_BP (a local or
     * parameter, `offset` from BP), FM_SYM (a file-scope one, `sym`, plus
     * `imm` bytes - an element or member), FM_LAB (a local static, label
     * `offset`) or FM_IND (through the pointer `cl`) - see fp_lea() */
    int       fpushed;   /* VK_FMEM only (with fonstk): a target reached
     * through a pointer whose address SEG_FLOADTP already pushed onto the
     * machine stack ("lea ax,(di)" / "|" / "push ax") before loading it */
    int       fonstk;    /* VK_FMEM only: a compound assignment's target
     * whose value SEG_FLOADT already pushed onto the floating-point stack
     * (see gen_fp_asop()) */
    int       lreg;      /* VK_LONG only: where the long is, when not DI:SI
     * (or `lpair`'s SI:DX) - LREG_DXAX: "mov ax,i" / "cwd" left it in
     * DX:AX for a comparison (OP_ITOL); LREG_CXBX: popped into CX:BX
     * (gen_long_relop()) */
    int       fromu;     /* VK_LONG only: an unsigned value widened ("mov
     * si,u" / "sub di,di" - OP_ITOL); VK_LCON: an int constant widened
     * (ITOL of a CON - its own degree in a comparison, see OP_ITOL) */
    int       memleft;   /* VK_IND only: a comparison's LEFT operand
     * whose address SEG_DEFPOPL popped into BX - compared in place, "cmp
     * (bx),di" (fltprobe/p17_elem2.s.golden), never loaded first */
    int       cond_memleft; /* VK_COND only: cl is such an operand - see
     * emit_cmp_and_branch() */
    int       cond_ortest; /* VK_COND only: cl is an element read through a
     * computed address ("b[j]") and cr the constant 0 - tested, not
     * compared: loaded into its register and "or reg,reg" (see
     * emit_cmp_and_branch() and plan_expression()'s `zelem`) */
    int       ortest;    /* VK_IND only: such an element tested for truth
     * itself ("if (b[i])", "if (ps[i].c)" - plan_expression()'s `telem`):
     * as_cond() makes it a cond_ortest */
    int       lowonly;   /* an int-class value standing for its own ITOL,
     * whose long sum or difference with a constant is only ever truncated
     * again (plan_expression()'s `itollow`) - and that sum, computed as an
     * int, until its LTOI: v7's unoptim() distributes LTOI over '+'/'-'
     * and cancels LTOI(ITOL(x)) (see OP_ITOL) */
    int       lowplus;   /* VK_LOWADD only: from "(int) (l + c)", not "(int)
     * (l - c)" - the other term then comes first (see OP_PLUS) */
    int       ltest;     /* VK_MEM only: a 'long' variable tested for truth
     * by a CBRANCH (plan_expression()'s `ltest`) - see gen_long_test() */
    int       cond_is_ltest; /* VK_COND only: cl is such a 'long' variable,
     * compared with 0 as a whole (true_op NEQUAL, or EQUAL under '!') */
    int       itolw;     /* VK_MEM/VK_STATIC only: an int variable standing
     * for its own ITOL, not widened yet - an operand of a 'long' '*' with
     * a long variable, which v7's acommute() puts first (its degree is
     * the higher), so it is widened and pushed after the variable: see
     * gen_long_binop_call() */
    int       cond_is_float; /* VK_COND only: a floating comparison, its
     * "call fcmp" / "sahf" already written; `flags_at` is GenState.ninsn
     * right after the "sahf" - the branch must follow with nothing in
     * between (see gen_fp_compare() and gen_cond_branch()) */
} Val;

/* VK_LONG's `lreg` - see its comment in Val. */
enum { LREG_DISI = 0, LREG_DXAX = 1, LREG_CXBX = 2 };

/* VK_FMEM's `fmode` - see its comment in Val. */
enum { FM_BP = 0, FM_SYM = 1, FM_LAB = 2, FM_IND = 3 };

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
    SEG_CALL,    /* the call, its arguments pushed: slot[a] words, b = the
                  * CALL's own type */
    SEG_RHSREG,  /* an assignment's right-hand side, computed ahead of its
                  * target, into a register - see plan_rhsreg() */
    SEG_SWAP2,   /* exchange the top two values (no spill involved) */
    SEG_LOADIND, /* a dereference on top loaded into its own register */
    SEG_DEFPUSH, /* the pointer variable on top pushed onto the machine
                  * stack - see is_deferred_ptr() */
    SEG_ADDRPUSH,/* the address of the dereference on top (a register)
                  * pushed onto the machine stack - see is_pushaddr() */
    SEG_DEFPOP,  /* ... and popped into BX, below the value on top: the
                  * operand it points to becomes "(bx)" */
    SEG_KEEPIND, /* the next word dereference is not loaded: its address
                  * is about to be pushed (SEG_ADDRPUSH) - see keep_ind */
    SEG_DEFPOPL, /* ... popped into BX for a comparison: the pushed
                  * element is the LEFT operand, "(bx)", compared in
                  * place - see is_pushleft() */
    SEG_TODI,    /* the value on top, just computed in AX, moved into DI
                  * - see is_leftdi() */
    SEG_SHIFT,   /* the value on top into DI and shifted left by a bits -
                  * a constant multiple distrib() rebuilt (is_distrib()) */
    SEG_ASPOP,   /* a compound assignment's right-hand side into a
                  * register, the target's pushed address popped into BX
                  * below it - see is_aspush() */
    SEG_FDATA,   /* a floating constant's .data block, written now: a =
                  * its index in the plan's `fc` table - see the
                  * "Floating point" section's planner */
    SEG_FLOAD,   /* the floating operand on top (a variable or a
                  * constant) onto the floating-point stack now, ahead
                  * of a computed operand that follows it */
    SEG_FLOADT,  /* the same for a compound assignment's target, which
                  * stays the target (Val's `fonstk`) */
    SEG_FLOADTP, /* the same for a target reached through a pointer: its
                  * address pushed first (Val's `fpushed`) */
    SEG_FSWAP,   /* the next floating comparison's operands were
                  * generated in exchanged order: mirror its relation */
    SEG_FITOF,   /* the register the next int converted to floating is
                  * loaded into: a = FREG_DI or FREG_AX - see fafter() */
    SEG_FLEAF,   /* a floating element or member at a constant offset of
                  * a named object, its opcodes never streamed: `start` its
                  * NAME, a the offset, b the type - see ffold() */
    SEG_FSTREG,  /* the register context the next floating store through a
                  * pointer variable is addressed in: a = FREG_DI or FREG_AX
                  * - see fp_lea() */
    SEG_FDROP    /* a hoisted floating prefix '++'/'--' is done: its value
                  * (the variable) dropped - see is_fpreinc() */
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
#define FCONST_MAX 32

/* One floating constant of the expression being planned - see the
 * "Floating point" section's planner: a written one (OP_FCON) or an int
 * constant converted (OP_ITOF of an OP_CON), `off` the temp1 offset of
 * that opcode, its c1 label and its ".float"/".double" text, decided
 * when the expression is planned; `printed` once its .data block is
 * out. */
typedef struct {
    long off;
    int  label;
    int  dbl;             /* 8 bytes (".double", loaded "fldd") */
    int  printed;
    char text[48];
} FConst;

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
    int    nfc;           /* the expression's floating constants - kept
                           * after the plan ends, until the next
                           * expression is planned */
    FConst fc[FCONST_MAX];
} Plan;

/* One pooled symbol name - see intern_name(). */
struct NameNode {
    struct NameNode *next;
    char            *name;   /* owned */
};

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
    struct NameNode *names;    /* file-scope symbol names referenced so
                                 * far - see intern_name() */
    long ninsn;                /* instructions written so far (put_insn_ex())
                                 * - tells whether the flags an AND set are
                                 * still those of its result (Val's
                                 * `flagsv`) */
    int  nfloat;               /* floating-point code was generated: the
                                 * file ends with ".globl fltused" (v7/cc/
                                 * c10.c's nfloat - see "Floating point") */
    int  fswap;                /* SEG_FSWAP was just run - see
                                 * gen_fp_compare() */
    int  itof_reg;             /* SEG_FITOF's register for the next
                                 * OP_ITOF - see fitof_reg() */
    int  fst_reg;              /* SEG_FSTREG's context for the next store
                                 * through a pointer variable - see fp_lea() */
    long op_off;               /* temp1 offset of the opcode being
                                 * handled - a floating constant finds its
                                 * planned label by it (fc_find()) */
    long lrel[16];             /* temp1 offsets of the current expression's
                                 * comparisons with a 'long' operand - see
                                 * OP_LESS... and plan_expression() */
    int  nlrel;
    long itolu[16];            /* temp1 offsets of the current expression's
                                 * ITOL nodes whose operand is unsigned -
                                 * noted by plan_expression()'s pre-scan
                                 * (a Val keeps no type): see OP_ITOL */
    int  nitolu;
    int  fdepth;               /* the real c1's compile-time model of the
                                 * runtime floating-point stack's depth -
                                 * see fp_track() */
    int  fp_nocheck;           /* the next pop is the argument push or a
                                 * returned value's store into "fac", which
                                 * the model does not check (fp_track()) */
    int  fp_argpop;            /* an assignment's value was stored with a
                                 * pop as a call argument somewhere in this
                                 * file (fp_store()) - the model may be
                                 * below 0 from then on */
    int  fstart;               /* fdepth when the current expression
                                 * started (plan_expression()) */
    int  stmt_argpops;         /* such double pops in the current
                                 * expression - each leaves the model one
                                 * lower at its end */
    long argpop_end[8];        /* the current expression's floating
                                 * assignments passed as an argument of a
                                 * call whose value goes nowhere or straight
                                 * into the statement's own floating store
                                 * (each node's "end" - just past its
                                 * arguments): stored WITH a pop - see
                                 * fp_store() */
    int  nargpop;
    int  cur_line;             /* the source line of the expression being
                                 * generated (its EXPR's or CBRANCH's - see
                                 * prescan_expr()), for c1_error() */
    int  nerrors;              /* c1_error()s reported: exit status 1 */
    long itollow[16];          /* temp1 offsets of the current expression's
                                 * ITOLs whose value only feeds "+/- a long
                                 * constant" under an LTOI - see Val's
                                 * `lowonly` */
    int  nitollow;
    long ltests[8];            /* temp1 offsets of the current expression's
                                 * 'long' NAMEs tested for truth - by a
                                 * CBRANCH (directly or under '!'), as an
                                 * operand of '&&'/'||' or as a '?:''s
                                 * condition - see Val's `ltest` */
    int  nltest;
    long telem[16];            /* temp1 offsets of the current expression's
                                 * STARs reading an element through a
                                 * computed address that is tested for
                                 * truth - see Val's ortest */
    int  ntelem;
    long zelem[16];            /* temp1 offsets of the current expression's
                                 * comparisons of an element read through a
                                 * computed address with 0 - see Val's
                                 * cond_ortest */
    int  nzelem;
    int  keep_ind;             /* SEG_KEEPIND was just run: the next word
                                 * dereference stays "(reg)" even as a
                                 * comparison's left operand (load_now()) -
                                 * its address is pushed (is_pushleft()) */
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

/* Takes over `name` (malloc'd - c1_read_sym()'s result) and returns a
 * pooled copy that stays valid until c1_generate() returns: a file-
 * scope variable's VK_STATIC Val (see Val's `sym`) is copied onto the
 * value stack, into VK_COND/VK_CHARX sub-operands, plan steps, ..., so
 * it must not own its text the way VK_FUNC does. A name seen before is
 * shared (the pool stays as small as the number of distinct globals a
 * file references). */
static const char *intern_name(GenState *g, char *name)
{
    for (struct NameNode *n = g->names; n; n = n->next)
        if (strcmp(n->name, name) == 0) {
            free(name);
            return n->name;
        }
    struct NameNode *n = malloc(sizeof *n);
    if (!n)
        gen_fatal("out of memory");
    n->name = name;
    n->next = g->names;
    g->names = n;
    return n->name;
}

static void free_names(GenState *g)
{
    while (g->names) {
        struct NameNode *n = g->names;
        g->names = n->next;
        free(n->name);
        free(n);
    }
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
enum { POP_BYTE = 1, POP_CHARX = 2, POP_STRUCT = 4, POP_FLOAT = 8,
       POP_LPAIR = 16 };

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
    if (v->structv && !(allow & POP_STRUCT))
        gen_fatal("a whole struct used by this operator is not yet supported "
                  "(only its address, or a struct assignment) - see "
                  "src/mutos_cc/README.md");
    if (v->kind == VK_CHARX && !(allow & POP_CHARX))
        gen_fatal("a 'char' element or variable used as an operand of this "
                  "operator is not yet supported (no golden reference "
                  "confirms its instruction shape; confirmed so far: '+' of "
                  "two chars, 'return', and an int assignment) - see "
                  "src/mutos_cc/README.md");
    if ((v->kind == VK_FMEM || v->kind == VK_FCON || v->kind == VK_FACC ||
         v->kind == VK_FDONE) && !(allow & POP_FLOAT))
        gen_fatal("a 'float'/'double' value used by this operator is not yet "
                  "supported (covered so far: '=', '+', '-', '*', '/' and "
                  "conversions to and from int and long) - see "
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
    if (v.kind == VK_SYMIDX)
        gen_fatal("internal: an unloaded file-scope array element address "
                  "reached a consumer other than OP_STAR");
    if (v.kind == VK_FIELD)
        gen_fatal("internal: a bit-field assignment target reached a "
                  "consumer other than OP_ASSIGN");
    if (v.kind == VK_LONG && v.lpair && !(allow & POP_LPAIR))
        gen_fatal("this use of a 'long' value in a function with a "
                  "register variable in DI is not yet supported (only "
                  "storing it) - see src/mutos_cc/README.md");
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
    if (g->valstack[g->valsp - 1].kind == VK_FACC)
        gen_fatal("a computed 'float'/'double' value whose result is unused "
                  "is not yet supported (it would be left on the runtime's "
                  "floating-point stack; no golden shows how the real "
                  "compiler drops it) - see src/mutos_cc/README.md");
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
    s.lreg = v.lreg;
    s.lpair = v.lpair;
    s.fromu = v.fromu;
    s.sym = v.sym;
    s.postfix = v.postfix;
    s.flagsv = v.flagsv;
    s.flags_at = v.flags_at;
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
    v.lreg = s.lreg;
    v.lpair = s.lpair;
    v.fromu = s.fromu;
    v.sym = s.sym;
    v.postfix = s.postfix;
    v.flagsv = s.flagsv;
    v.flags_at = s.flags_at;
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
    case VK_STATIC:
        if (v.sym)
            snprintf(buf, n, "%s", v.sym);
        else
            snprintf(buf, n, "L%d", v.offset);
        break;
    case VK_FUNCADDR: snprintf(buf, n, "#%s", v.reg); break;
    case VK_STATICADDR:
        /* A file-scope variable's address: "#_proc" - "mov di,#_proc"
         * in tests/mutos_as/kernel_nonopt/ (no corpus golden takes
         * one's address). */
        if (v.sym)
            snprintf(buf, n, "#%s", v.sym);
        else
            snprintf(buf, n, "#L%d", v.offset);
        break;
    case VK_REG: snprintf(buf, n, "%s", v.reg); break;
    case VK_IND:
        /* A file-scope array's element, "_text(bx)" (VK_SYMIDX): the
         * symbol is the displacement, with no size marker - 10_integ/
         * 01_wordcount.s.golden's "cmpb _text(bx),*10.", and "orb
         * _amxscd(bx),*4.", "movb ax,_amxscd(bx)" / "cbw" in tests/
         * mutos_as/kernel_nonopt/amx.s. This is v7's operand text
         * (pname(), template "A1"); where the real compiler loads such
         * an element into a register through its "#1" template instead,
         * the symbol does take the '#' marker - see o_load(). */
        if (v.sym) {
            if (v.imm != 0)
                gen_fatal("internal: a file-scope array element with a "
                          "constant displacement as well");
            snprintf(buf, n, "%s(%s)", v.sym, v.reg);
            break;
        }
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
    case VK_SYMIDX: snprintf(buf, n, "<unloaded-array-element-address>"); break;
    case VK_FIELD: snprintf(buf, n, "<unstored-bit-field>"); break;
    case VK_FMEM: snprintf(buf, n, "<unloaded-float-variable>"); break;
    case VK_FCON: snprintf(buf, n, "<unloaded-float-constant>"); break;
    case VK_FACC: snprintf(buf, n, "<float-stack-top>"); break;
    case VK_FDONE: snprintf(buf, n, "<stored-float-value>"); break;
    case VK_LOWADD: snprintf(buf, n, "<uncomputed-long-low-word-sum>"); break;
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
/* The source operand of an element LOADED into a register by v7's "F*"
 * code template ("F*" computes the address, then "mov R,#1(R)" loads
 * through it): o_val()'s text, except that a file-scope array element's
 * symbol takes the '#' marker - v7/cc/c10.c's '#' case prints the
 * address's constant part there, and the real compiler writes it with
 * the word-size marker: 10_integ/01_wordcount.s.golden's "movb dx,
 * #_text(bx)" (its "text[i] != '\0'"), and tests/mutos_as/kernel_nonopt/
 * amx.s's "movb dx,#_amxi_bu(bx)" / "movb *-10.(bp),dx" (a char element
 * stored into a char local) and "mov dx,#_amxladd(bx)". Without a symbol
 * nothing differs: "movb dx,(bx)" (10_integ/04_strrev.s.golden) - the
 * kernel output has no "#(". Where the element is instead an operand of
 * the instruction itself ("cmpb _text(bx),*10.") or converted to an int
 * ("movb ax,_amxscd(bx)" / "cbw"), o_val()'s plain "_text(bx)" is
 * right. */
static Opnd o_load(Val v)
{
    if (v.kind == VK_IND && v.sym && v.imm == 0)
        return o_fmt("#%s(%s)", v.sym, v.reg);
    return o_val(v);
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
/* The register pair a VK_LONG occupies - see Val's `lreg`/`lpair`. */
static unsigned long_regs(int lreg, int lpair)
{
    if (lreg == LREG_DXAX)
        return RB_DX | RB_AX;
    if (lreg == LREG_CXBX)
        return RB_CX | RB_BX;
    return lpair ? (RB_SI | RB_DX) : (RB_DI | RB_SI);
}

static unsigned simple_regs(SimpleVal s)
{
    switch (s.kind) {
    case VK_REG:
    case VK_IND:  return reg_bit(s.reg);
    case VK_LONG: return long_regs(s.lreg, s.lpair);
    default:      return 0;
    }
}

static unsigned val_regs(const Val *v)
{
    switch (v->kind) {
    case VK_REG:
    case VK_IND:  return reg_bit(v->reg);
    case VK_LONG: return long_regs(v->lreg, v->lpair);
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
    case VK_SYMIDX:  return simple_regs(v->cl);
    case VK_FIELD:   return simple_regs(v->cl);
    case VK_ROWADDR: return reg_bit(v->reg) | simple_regs(v->cl);
    case VK_FMEM:    return v->fmode == FM_IND ? simple_regs(v->cl) : 0;
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
    { "neg",  0, 1 },
    { "orb",  0, 1 },
    { "sal",  0, 1 }, { "sar",  0, 1 }, { "shl", 0, 1 },
    { "rcl",  0, 1 }, { "rcr",  0, 1 },
    { "inc",  0, 1 }, { "dec",  0, 1 }, { "pop", 0, 1 },
    { "pop cx", RB_CX, 0 },        /* the literal-space quirk - OP_PLUS */
    { "xchg", 0, 2 },
    { "cbw",  RB_AX, 0 },
    { "cwd",  RB_DX, 0 },
    { "sahf", 0, 0 },              /* flags from AH - a floating compare */
    { "imul", RB_AX | RB_DX, 0 },  /* one-operand forms: DX:AX result */
    { "idiv", RB_AX | RB_DX, 0 },
    { "loop", RB_CX, 0 },          /* counts CX down - a long shift */
    /* A call clobbers the caller-saved registers; DI/SI are callee-
     * saved (every function's SAVE prologue pushes them - docs/
     * MUTOS_C_ABI.md sect. 1.2), which is what lets a 'register'
     * local live in DI across calls. */
    { "call", RB_AX | RB_BX | RB_CX | RB_DX, 0 },
    { "push", 0, 0 }, { "cmp", 0, 0 }, { "cmpb", 0, 0 }, { "jmp", 0, 0 },
    { "jz",   0, 0 },              /* past a long shift's loop - count 0 */
    { "seg", 0, 0 },
    { "blt",  0, 0 }, { "ble", 0, 0 }, { "bgt", 0, 0 }, { "bge", 0, 0 },
    { "beq",  0, 0 }, { "bne", 0, 0 }, { "blos", 0, 0 }, { "bhi", 0, 0 },
    { "blo",  0, 0 }, { "bhis", 0, 0 },
    { ".globl", 0, 0 }, { ".text", 0, 0 }, { ".even", 0, 0 },
    { ".bss",   0, 0 }, { ".data", 0, 0 }, { ".blkb", 0, 0 },
    { ".comm",  0, 0 },
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

/* An error the REAL compiler's c1 reports and goes on from - v7/cc/
 * c11.c's error(): "<line>: <message>" on stderr, the error counted, the
 * output written to the end; c1 then exits with status 1 (c10.c's
 * "exit(nerror!=0)"), and cc keeps the ".s" of "cc -S" (it removes its
 * temporary one otherwise - v7/cc/cc.c's dexit()). mutos_c1 does the same
 * for the errors it reproduces - only fp_track()'s so far. */
static void c1_error(GenState *g, const char *msg)
{
    fflush(g->out);
    fprintf(stderr, "%d: %s\n", g->cur_line, msg);
    g->nerrors++;
}

/* The real compiler's compile-time model of the runtime floating-point
 * stack (libc.a's stacks.o), reproduced. The MUTOS c1 counts the values
 * its code pushes and pops, and reports a pop below the bottom - with two
 * different messages, so from two places in its code: fltprobe/p21_fltexp
 * (2026-10-03) - "f = half(d = 3.0);" is its own wrong code, the assigned
 * 3.0 stored WITH a pop ("fstdp", see fp_store()) and then pushed as the
 * argument by a second "fstdp" - and "cc -S" printed
 *
 *     56: floating point stack underflow
 *     57: Floating point stack underflow
 *
 * (exit status 1, the ".s" complete - the golden). The model behind them,
 * as far as these two lines and round 1's p1_compare (a floating truth
 * test, docs/DEVLOG.md's "Floating shapes from libc.a's compiled C")
 * show it:
 *
 * - a load, "itof", "ltof", "fdup" and a call's result ("fldd" after it)
 *   push one value; a "fst<s|d>p" store pops one and reports "floating
 *   point stack underflow" (lower case) below 0 - line 56's "fstdp" into
 *   f; "ftoi"/"ftol" pop one and report "Floating point stack underflow"
 *   (upper case) - line 57's "ftoi", whose statement is balanced, but the
 *   model was still one short after line 56;
 * - the push of a floating argument ("sub sp,*8" / "mov ax,sp" / "call
 *   fstdp") pops one WITHOUT a check: line 56 had exactly one message,
 *   for its store into f, not two - and so does a double function's
 *   returned value, stored into "fac" ("lea ax,fac" / "call fstdp" -
 *   fltprobe/p28_fltstk's f5, "return d;" at -5 and no message for
 *   line 74);
 * - the stack-with-stack operators "fadd"/"fsub"/"fmul"/"fdiv" pop one
 *   and "fcmp" two, each checked with the upper-case message - fltprobe/
 *   p28_fltstk (2026-10-04): "50: Floating point stack underflow" twice
 *   for "x = (d + e) * (d - e);" ("fmul", "ftoi"), "65:" twice for "if (d
 *   + e > d - e)" ("fcmp" - the line the lexer has reached after the
 *   ')', the next statement's), "79:" twice for main's "... + f5()"
 *   ("fadd", "ftoi");
 * - the model starts at 0 for the FILE and is never reset - not even
 *   between functions (p28_fltstk: f4's "d = 1.0;" at line 59, a
 *   balanced statement, reports because f3 left the model two short):
 *   after an underflow it stays below 0, which is why line 57 reports.
 *   An unused call result ("half(d = 3.0);") pops nothing: line 48 is
 *   silent, one below the bottom, and line 49 reports its store.
 *
 * A correct program never goes below 0, so mutos_c1 reports nothing for
 * one; the model is checked against every floating golden (make test). */
static void fp_track(GenState *g, const char *fn)
{
    static const struct { const char *fn; int push, pop, store; } FX[] = {
        { "fldd", 1, 0, 0 }, { "flds", 1, 0, 0 }, { "itof", 1, 0, 0 },
        { "ltof", 1, 0, 0 }, { "fdup", 1, 0, 0 },
        { "fstdp", 0, 1, 1 }, { "fstsp", 0, 1, 1 },
        { "ftoi", 0, 1, 0 }, { "ftol", 0, 1, 0 },
        { "fadd", 0, 1, 0 }, { "fsub", 0, 1, 0 }, { "fmul", 0, 1, 0 },
        { "fdiv", 0, 1, 0 }, { "fcmp", 0, 2, 0 },
    };
    for (size_t i = 0; i < sizeof FX / sizeof FX[0]; i++) {
        if (strcmp(FX[i].fn, fn) != 0)
            continue;
        g->fdepth += FX[i].push;
        for (int k = 0; k < FX[i].pop; k++) {
            if (--g->fdepth < 0 && !g->fp_nocheck)
                c1_error(g, FX[i].store ? "floating point stack underflow"
                                        : "Floating point stack underflow");
        }
        g->fp_nocheck = 0;
        return;
    }
}

/* THE instruction-line formatter - see this section's header. */
static void put_insn_ex(GenState *g, const Insn *in, int regvar_store)
{
    /* A value kind render_operand() cannot write as an operand renders
     * as "<...>" - never real assembly, so never written: such a slip
     * used to reach the output unnoticed ("cmp *-8.(bp),<unmaterialized-
     * long-const>", 2026-09-26) - it is an internal error instead. */
    if ((in->nops >= 1 && in->a.s[0] == '<') ||
        (in->nops >= 2 && in->b.s[0] == '<'))
        gen_fatal("internal: an operand mutos_c1 cannot render (%s) - an "
                  "unsupported value reached an instruction",
                  in->a.s[0] == '<' ? in->a.s : in->b.s);
    note_writes(g, insn_writes(in), regvar_store);
    g->ninsn++;
    if (in->nops == 1 && strcmp(in->mnem, "call") == 0)
        fp_track(g, in->a.s);
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
/* "_name:" - a named data label (OP_NLABEL), glued onto what follows
 * like put_label()'s: 07_scope/01_globstat.s.golden's "_hidden:.blkb
 * 2.". A function's own entry label (OP_RLABEL) is a line of its own
 * instead ("_bump:" - put_line()). */
static void put_name_label(GenState *g, const char *name)
{
    fprintf(g->out, "%s:", name);
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

/* Moves DX(high):AX(low) into the register pair a 'long' value is
 * handed on in - DI:SI (SEQ_DXAX_TO_DISI) or, while DI holds a register
 * variable, SI(high):DX(low), the next pair v7's register allocation
 * takes: fltprobe/p5_call.s.golden's "l = 7;" -> "mov ax,*7." / "cwd" /
 * "mov si,dx" / "mov dx,ax" / "mov *-22.(bp),dx" / "mov *-24.(bp),si",
 * and "call ftol" / "mov si,dx" / "mov dx,ax" the same way. Returns the
 * VK_LONG. */
static Val emit_dxax_to_long(GenState *g)
{
    Val v = val_long();
    if (g->reserved & RB_DI) {
        ins2(g, "mov", o_reg("si"), o_reg("dx"));
        ins2(g, "mov", o_reg("dx"), o_reg("ax"));
        v.lpair = 1;
    } else {
        put_seq(g, SEQ_DXAX_TO_DISI);
    }
    return v;
}

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

/* A char in memory compared with a char-typed constant - a VK_COND with
 * cond_is_byte (see OP_LESS...). v7/cc/table.s's cctab has two
 * templates for a byte operand and a constant, and the real compiler
 * keeps them apart:
 *
 * - "%a,z" / "%nb*,ab" ([move1]/[move6]): the operand is compared in
 *   memory, "cmpb <char>,<constant>" - an addressable char (a local, a
 *   global) as it stands: "cmpb *-8.(bp),*97." (tests/mutos_as/
 *   kernel_nonopt/lp_AC.s), "cmpb 111.+_u,*0" (sys1.s), "cmpb *1.(si),
 *   *0" (amx.s); an element whose address the operand's own code put in
 *   BX (OP_STAR's F*) likewise, whatever the constant is not 0:
 *   10_integ/01_wordcount.s.golden's "mov dx,*-6.(bp)" / "mov bx,dx" /
 *   "cmpb _text(bx),*10." for "text[i] == '\n'". A constant 0 renders
 *   as "*0", CMP's own rule (render_cmp_imm()).
 * - "%n*,z" ([move2]: "F*" / "tstb #1(R)" on the PDP-11): such a
 *   computed element compared with 0 is loaded into DX through the "#1"
 *   operand and tested there, the 8086 having no memory TST: "movb
 *   dx,#_text(bx)" / "orb dx,dx" (01_wordcount.s.golden's "text[i] !=
 *   '\0'"; see o_load() for the '#'). ORB leaves OF clear and sets SF/ZF
 *   from the byte, so every signed branch reads it as a compare with 0.
 *
 * A char element addressed through DI or SI (a runtime subscript on a
 * local char array, "lea di,..." / "add di,...") has no golden in
 * either shape and is refused. */
static void emit_byte_cmp_and_branch(GenState *g, Val l, Val r,
                                     int branch_op_code, int target_lab)
{
    if (r.kind != VK_IMM)
        gen_fatal("internal: a byte comparison without a constant right "
                  "operand");
    int in_bx = (l.kind == VK_IND && strcmp(l.reg, "bx") == 0);
    if (l.kind != VK_MEM && l.kind != VK_STATIC && !in_bx)
        gen_fatal("comparing a 'char' reached through this address shape "
                  "with a constant is not yet supported - see "
                  "src/mutos_cc/README.md");
    if (in_bx && r.imm == 0) {
        ins2(g, "movb", o_reg("dx"), o_load(l));
        ins2(g, "orb", o_reg("dx"), o_reg("dx"));
    } else {
        ins2(g, "cmpb", o_val(l), o_cmpimm(r.imm));
    }
    ins1(g, cond_true_mnem(branch_op_code), o_lab(target_lab));
}

/* Emits "cmp\t<cl>,<cr-or-di>\n" followed by a branch to L<target>
 * using `branch_op_code`'s mnemonic (the caller passes either
 * cond.true_op directly, for a "branch if true" site, or
 * cond_invert(cond.true_op), for a "branch if false" site - see
 * gen_cond_branch()). The right-hand side is loaded into DI first if
 * it is itself a memory operand (8086 CMP cannot take two memory
 * operands); an immediate right-hand side is rendered directly via
 * render_cmp_imm(), matching 03_rellogic's "cmp *-6.(bp),di" (memory
 * rhs) vs. "cmp *-6.(bp),*0" (immediate rhs) shapes exactly. A byte
 * comparison (cond_is_byte) has its own shapes - see
 * emit_byte_cmp_and_branch(). */
static void gen_long_cmp(GenState *g, int op_true, SimpleVal l,
                         SimpleVal r, int cond_sense, int target_lab);

static void emit_cmp_and_branch(GenState *g, Val cond, int branch_op_code, int target_lab)
{
    Val l = val_from_simple(cond.cl);
    Val r = val_from_simple(cond.cr);
    Opnd rhs;
    /* A 'long' comparison has its own shape (gen_long_cmp(), reached
     * only from gen_cond_branch()); rendered here it would print an
     * operand placeholder as if it were code - as a value-context
     * "z = l > 0L;" did until 2026-09-24. */
    if (cond.cond_is_long) {
        /* As a value ("r + (u > 39999)" - fltprobe/p19_open3.s.golden):
         * the same compare and branches, branching when true. */
        gen_long_cmp(g, branch_op_code, cond.cl, cond.cr, 1, target_lab);
        return;
    }
    if (cond.cond_is_byte) {
        emit_byte_cmp_and_branch(g, l, r, branch_op_code, target_lab);
        return;
    }
    /* An element read through a computed address compared with 0 - v7's
     * rcexpr() turns "x > 0" in cctab into a test of x, and cctab's
     * "rest" entry for a STAR (H) computes it into a register, which the
     * MUTOS compiler then tests with "or": fltprobe/p23_elem3.s.golden's
     * "b[j] > 0" -> "lea di,*-36.(bp)" / ... / "add di,si" / "mov
     * di,(di)" / "or di,di" / "ble L6". (Through a pointer variable or at
     * a constant offset the dereference stays an operand of "cmp" - the
     * kernel's "cmp (di),*0", "cmp *16.(di),*0" - see plan_expression().) */
    if (cond.cond_ortest) {
        if (l.kind != VK_IND || r.kind != VK_IMM || r.imm != 0)
            gen_fatal("internal: an element tested against 0 lost its "
                      "operand shape");
        ins2(g, "mov", o_reg(l.reg), o_val(l));
        ins2(g, "or", o_reg(l.reg), o_reg(l.reg));
        ins1(g, cond_true_mnem(branch_op_code), o_lab(target_lab));
        return;
    }
    /* An ADDRESS operand ("p == &x", VK_MEM_DIRECT - OP_AMPER's deferred
     * "lea") has no golden; rendered as a memory operand it compared the
     * variable's CONTENTS instead ("mov di,*-6.(bp)" / "cmp *-10.(bp),di"
     * - silently wrong, found 2026-09-26; it predates this function's
     * other shapes). */
    if (l.kind == VK_MEM_DIRECT || r.kind == VK_MEM_DIRECT)
        gen_fatal("comparing with the address of a local variable or array "
                  "is not yet supported - see src/mutos_cc/README.md");
    /* Two elements, the left one's address popped into BX (SEG_DEFPOPL):
     * compared in place, "cmp (bx),di" - fltprobe/p17_elem2.s.golden. */
    if (cond.cond_memleft) {
        if (l.kind != VK_IND || r.kind != VK_REG)
            gen_fatal("internal: a comparison through a popped address "
                      "lost its operand shape");
        ins2(g, "cmp", o_val(l), o_val(r));
        ins1(g, cond_true_mnem(branch_op_code), o_lab(target_lab));
        return;
    }
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

/* A 'long' comparison - see Val's cond_is_long and gen_long_relop(). A
 * 32-bit comparison on a 16-bit ALU: the high words compared signed
 * first, which decides whenever they differ, then the low words
 * unsigned. The branches are v7/cc/c11.c's longrel()/xlongrel() with its
 * table lrtab[0] (a comparison not with an ITOL of 0, which takes
 * lrtab[1] and is refused before getting here): for the relation `op`
 * that is to branch to `target`,
 *
 *     cmp <high words>
 *     b<first>   target         (if any)
 *     b<second>  L<new>         (if any - the "decided against" exit)
 *     cmp <low words>
 *     b<third>   target         (unsigned)
 *   L<new>:
 *
 * Confirmed: 02_long/01_addsub.s.golden's "if (c > 0L)" (branch when
 * false, LESSEQ: "blt" / "bgt" / "blos"), fltprobe/p19_open3.s.golden's
 * "if (l > i)" (swapped to "i < l", branch when false - GREATEQ: "bgt
 * L4" / "blt L10000" / "bhis L4" / "L10000:"), "if (l == i)" (NEQUAL:
 * "bne" / "bne") and "r + (u > 39999)" (a value: "39999 < u" branching
 * when true - LESS: "blt L10001" / "bgt L10002" / "blo L10001" /
 * "L10002:"). `l`/`r` are a long variable in memory (VK_MEM - high word
 * at its offset), a long constant (VK_LCON) or a register pair (VK_LONG,
 * its `lreg`/`lpair`). */
static const struct {
    int op;
    int first, second;        /* signed, on the high words; 0: none */
    const char *third;        /* unsigned, on the low words */
} LONG_RELS[] = {
    { OP_EQUAL,   0,          OP_NEQUAL, "beq"  },
    { OP_NEQUAL,  OP_NEQUAL,  0,         "bne"  },
    { OP_LESSEQ,  OP_LESS,    OP_GREAT,  "blos" },
    { OP_LESS,    OP_LESS,    OP_GREAT,  "blo"  },
    { OP_GREATEQ, OP_GREAT,   OP_LESS,   "bhis" },
    { OP_GREAT,   OP_GREAT,   OP_LESS,   "bhi"  },
};

/* One half of a long comparison operand - see gen_long_cmp(). The high
 * word of a constant takes CMP's own immediate shape ("cmp *-8.(bp),*0"),
 * the low word the ordinary one ("cmp *-6.(bp),*0.") - 01_addsub. */
static Opnd long_half(SimpleVal v, int high)
{
    switch (v.kind) {
    case VK_MEM:
        return o_mem(v.offset + (high ? 0 : MCC_SZINT));
    case VK_LCON:
        return high ? o_cmpimm(v.offset) : o_imm(v.imm);
    case VK_LONG:
        if (v.lreg == LREG_DXAX)
            return o_reg(high ? "dx" : "ax");
        if (v.lreg == LREG_CXBX)
            return o_reg(high ? "cx" : "bx");
        if (v.lpair)
            return o_reg(high ? "si" : "dx");
        return o_reg(high ? "di" : "si");
    default:
        gen_fatal("internal: a 'long' comparison operand of kind %d", v.kind);
    }
    return o_reg("di");                  /* unreached */
}

static void gen_long_cmp(GenState *g, int op_true, SimpleVal l,
                         SimpleVal r, int cond_sense, int target_lab)
{
    int op = cond_sense ? op_true : cond_invert(op_true);
    size_t k = 0;
    while (k < sizeof LONG_RELS / sizeof LONG_RELS[0] && LONG_RELS[k].op != op)
        k++;
    if (k == sizeof LONG_RELS / sizeof LONG_RELS[0])
        gen_fatal("internal: unknown relational op %d", op);
    ins2(g, "cmp", long_half(l, 1), long_half(r, 1));
    if (LONG_RELS[k].first)
        ins1(g, cond_true_mnem(LONG_RELS[k].first), o_lab(target_lab));
    int xlab = 0;
    if (LONG_RELS[k].second) {
        xlab = g->next_lab++;
        ins1(g, cond_true_mnem(LONG_RELS[k].second), o_lab(xlab));
    }
    ins2(g, "cmp", long_half(l, 0), long_half(r, 0));
    ins1(g, LONG_RELS[k].third, o_lab(target_lab));
    if (xlab)
        put_label(g, xlab);
}

/* The operands of a 'long' comparison (`op`, `l` and `r` popped) put in
 * the order and the registers the real compiler compares them in - v7's
 * optim() exchanges a relational's operands when degree(left) <
 * degree(right) (an ITOL of an int: 2; a long variable or an LCON: 0;
 * an ITOL of an unsigned variable: -1) - and the comparison returned,
 * deferred (VK_COND, cond_is_long), as an int one is:
 *
 * - a long variable and an int widened in DX:AX (OP_ITOL - "mov ax,i"
 *   / "cwd"): the widened one left - fltprobe/p19_open3.s.golden's "l >
 *   i" -> "cmp dx,*-8.(bp)" ... "cmp ax,*-6.(bp)" (as "i < l"), "l ==
 *   i" the same;
 * - an unsigned widened in DI:SI ("mov si,u" / "sub di,di") and a long
 *   constant: the constant left, the widened value pushed first and
 *   popped into CX (high) and BX (low) after the constant is loaded -
 *   p19_open3's "u > 39999" -> "push si" / "push di" / "mov si,#-25537."
 *   / "mov di,*0." / "pop cx" / "pop bx" / "cmp di,cx" ... "cmp si,bx"
 *   (as "39999 < u"; cctab's "%nl,nl": SS, F);
 * - a long variable and a long constant 0 (02_long/01_addsub's "c >
 *   0L").
 *
 * Anything else - two long variables, a non-zero constant, an int
 * constant widened (v7's longrel() takes its other table for an ITOL of
 * 0), a long in a register compared with a variable - has no golden. */
static Val gen_long_relop(GenState *g, int op, Val l, Val r)
{
    int relop = op;
    if (l.kind == VK_MEM && r.kind == VK_LONG && r.lreg == LREG_DXAX) {
        Val t = l;
        l = r;
        r = t;
        relop = find_relop(op)->mirror;
    }
    if (l.kind == VK_LONG && l.fromu && l.lreg == LREG_DISI && !l.lpair &&
        r.kind == VK_LCON && !r.fromu) {
        relop = find_relop(op)->mirror;
        ins1(g, "push", o_reg("si"));
        ins1(g, "push", o_reg("di"));
        Val c = materialize_long(g, r);
        ins1(g, "pop", o_reg("cx"));
        ins1(g, "pop", o_reg("bx"));
        Val w = val_long();
        w.lreg = LREG_CXBX;
        l = c;
        r = w;
    } else if (l.kind == VK_LONG && l.fromu && l.lreg == LREG_DISI &&
               !l.lpair && r.kind == VK_MEM) {
        /* An unsigned widened and a long variable: the same, the variable
         * loaded - fltprobe/p22_long2.s.golden's "u < l" -> "mov si,
         * *-20.(bp)" / "sub di,di" / "push si" / "push di" / "mov si,
         * *-6.(bp)" / "mov di,*-8.(bp)" / "pop cx" / "pop bx" / "cmp di,cx"
         * ... "cmp si,bx" (as "l > u": an ITOL of an unsigned leaf has
         * degree -1, a NAME 0). */
        relop = find_relop(op)->mirror;
        ins1(g, "push", o_reg("si"));
        ins1(g, "push", o_reg("di"));
        ins2(g, "mov", o_reg("si"), o_mem(r.offset + MCC_SZINT));
        ins2(g, "mov", o_reg("di"), o_mem(r.offset));
        ins1(g, "pop", o_reg("cx"));
        ins1(g, "pop", o_reg("bx"));
        Val w = val_long();
        w.lreg = LREG_CXBX;
        l = val_long();
        r = w;
    } else if (l.kind == VK_LONG && l.lreg == LREG_DXAX && r.kind == VK_MEM) {
        /* "i < l", "l > i" swapped */
    } else if (l.kind == VK_MEM && r.kind == VK_LCON) {
        /* A long variable and a constant, as written - 02_long/01_addsub's
         * "c > 0L", fltprobe/p22_long2.s.golden's "l < 0L", "l >= 0L", "l >
         * 2" and "l > 0" (an int constant widened - lrtab[0] too: "cmp
         * *-8.(bp),*0" / "blt L7" / "bgt L10006" / "cmp *-6.(bp),*2." /
         * "blos L7"). */
    } else if (l.kind == VK_MEM && r.kind == VK_MEM) {
        /* Two long variables: the left one loaded into DI:SI, compared
         * with the right one in memory - fltprobe/p22_long2.s.golden's "l
         * > m" -> "mov si,*-6.(bp)" / "mov di,*-8.(bp)" / "cmp di,*-12.
         * (bp)" / "blt L12" / "bgt L10010" / "cmp si,*-10.(bp)" / "blos
         * L12", "l == m" with "bne" / "bne". */
        ins2(g, "mov", o_reg("si"), o_mem(l.offset + MCC_SZINT));
        ins2(g, "mov", o_reg("di"), o_mem(l.offset));
        l = val_long();
    } else {
        gen_fatal("this 'long' comparison's operand shape is not yet "
                  "supported (confirmed: a long variable with a constant or "
                  "another long variable, an int variable converted, or an "
                  "unsigned one with a long constant or variable) - see "
                  "src/mutos_cc/README.md");
    }
    Val c = {0};
    c.kind = VK_COND;
    c.true_op = relop;
    c.cl = simple_of(l);
    c.cr = simple_of(r);
    c.cond_is_long = 1;
    return c;
}

static void gen_cond_branch(GenState *g, Val v, int lbl, int cond_sense,
                            int base);           /* see "Conditional
                                                  * evaluation" */
static Val gen_logval(GenState *g, int ltrue); /* see "Conditional
                                                 * evaluation" below */

/* Materializes a single deferred comparison into a real 0/1 value in
 * DI - the "standalone relational" pattern (e.g. plain "r = a < b;"):
 * branch-if-true to a fresh label, then gen_logval()'s "set DI=0 and
 * jump past, or set DI=1 at the true label" (labels printed with no
 * trailing newline, matching OP_LABEL's style, so whatever the caller
 * emits next glues onto the same source line - confirmed against
 * "L10001:mov\t*-10.(bp),di" in the golden). */
static void materialize_cond(GenState *g, Val cond, const char *reg)
{
    int ltrue = g->next_lab++;
    emit_cmp_and_branch(g, cond, cond.true_op, ltrue);
    if (strcmp(reg, "di") == 0) {
        (void)gen_logval(g, ltrue);
        return;
    }
    /* Into the next register - see materialize(). */
    int lend = g->next_lab++;
    ins2(g, "mov", o_reg(reg), o_imm(0));
    ins1(g, "jmp", o_lab(lend));
    put_label(g, ltrue);
    ins2(g, "mov", o_reg(reg), o_imm(1));
    put_label(g, lend);
}

/* The register an int comparison's 0/1 goes into when it is the RIGHT
 * operand of a binary operator whose left operand is already computed in
 * DI (slot idx - 1): SI, the next register, as v7's "%n,e" template
 * computes a right operand ("S1") - fltprobe/p32_elem5.s.golden's "(x <
 * 9) * 2 + (x > 2)" -> ... "sal di,*1" / "cmp *-64.(bp),*2." / "bgt
 * L10006" / "mov si,*0." / "jmp L10007" / "L10006:mov si,*1." / "L10007:
 * add di,si" (see plan_dnode()). Anywhere else DI, the working register
 * (where a pending value there makes the register-occupancy guard refuse
 * the expression, as before). */
static const char *cond_reg(const GenState *g, int idx)
{
    if (idx < 1 || idx != g->valsp - 1)
        return "di";
    const Val *l = &g->valstack[idx - 1];
    if (l->kind != VK_REG || l->regvar || strcmp(l->reg, "di") != 0 ||
        (g->reserved & RB_SI))
        return "di";
    for (int i = 0; i < idx - 1; i++)
        if (!g->valstack[i].regvar && (val_regs(&g->valstack[i]) & RB_SI))
            return "di";
    return "si";
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
    if (v.cond_is_float) {
        /* The compare is out ("call fcmp" / "sahf" - gen_fp_compare());
         * the 0/1 is the int shape after it: p1_compare's "r + (d < e)"
         * -> "sahf" / "blt L10007" / "mov di,*0." / "jmp L10008" /
         * "L10007:mov di,*1." / "L10008:". */
        if (v.flags_at != g->ninsn)
            gen_fatal("internal: code between a floating comparison and its "
                      "branch");
        int ltrue = g->next_lab++;
        ins1(g, cond_true_mnem(v.true_op), o_lab(ltrue));
        (void)gen_logval(g, ltrue);
        return val_reg("di");
    }
    if (v.cond_is_ltest) {
        /* A long variable's truth as a value ("x = !l"): v7's cexpr()
         * branches to its true label through cbranch() - both words
         * tested, the high one first (gen_cond_branch(), whose skip label
         * comes after the true one) - then 0 / 1 - fltprobe/p31_long4.s.
         * golden: "cmp di,*0" / "bne L10006" / "cmp si,*0" / "beq L10005"
         * / "L10006:mov di,*0." / "jmp L10007" / "L10005:mov di,*1." /
         * "L10007:". */
        int ltrue = g->next_lab++;
        gen_cond_branch(g, v, ltrue, 1, g->ndeferred);
        (void)gen_logval(g, ltrue);
        return val_reg("di");
    }
    materialize_cond(g, v, "di");
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
    Val v = g->valstack[idx];
    if (!v.cond_is_float && !v.cond_is_ltest) {
        const char *reg = cond_reg(g, idx);
        materialize_cond(g, v, reg);
        g->valstack[idx] = val_reg(reg);
        return;
    }
    Val m = materialize(g, v);
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
    /* A char in memory tested for truth ("if (c)", "while (*p)", an
     * operand of "&&"/"||"/"!"): mutos_c0 writes it unconverted, as v7's
     * build() converts no condition and no operand of those operators,
     * and v7/cc/table.s's cctab tests the byte itself ("%a,z": tstb A1;
     * "%n*,z": F* / tstb #1(R)) - the byte compare with 0 a char compared
     * with '\0' already gets (emit_byte_cmp_and_branch()): "cmpb *1.(si),
     * *0" / "cmpb (di),*0" in tests/mutos_as/kernel_nonopt/amx.s. */
    if (v.bytev) {
        c.cond_is_byte = 1;
        v.bytev = 0;
    }
    c.cond_is_ltest = v.ltest;
    if (v.kind == VK_IND && v.ortest)
        c.cond_ortest = 1;              /* see Val's ortest */
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
static int reg_busy(const GenState *g, unsigned bit);

static int di_busy(const GenState *g)
{
    if (g->valsp >= 1 && g->valstack[g->valsp - 1].kind == VK_MEM_DIRECT)
        return 1;
    return reg_busy(g, RB_DI);
}

/* Whether a pending value on the value stack lives in (or behind) the
 * register(s) `bits` - a 'register' local's own NAME excepted, as in
 * di_busy(). */
static int reg_busy(const GenState *g, unsigned bits)
{
    for (int i = 0; i < g->valsp; i++)
        if (!g->valstack[i].regvar && (val_regs(&g->valstack[i]) & bits))
            return 1;
    return 0;
}

/* The register an address or a pointer is computed into: DI, the
 * working register, or SI while a pending value holds DI - the real
 * compiler's next free register, as its code templates allocate them
 * (R, then R+1): 06_struct/03_starray.s.golden's "pts[i].y = i * 2;"
 * ("lea si,*-16.(bp)" with i * 2 in DI), 02_stptr.s.golden's "mov si,
 * *4.(bp)" / "mov *2.(si),di", 05_nestst.s.golden's "mov si,*4.(bp)" /
 * "sub di,*2.(si)" and 10_integ/02_bubsort.s.golden's "a[j] > a[j + 1]"
 * (the second element's address in SI). Both taken is refused: the
 * next register, DX, cannot address memory, and no golden shows what
 * the real compiler does then. */
static const char *pick_addr_reg(const GenState *g)
{
    if (!reg_busy(g, RB_DI))
        return "di";
    if (!reg_busy(g, RB_SI))
        return "si";
    gen_fatal("an address needed while both DI and SI hold pending values "
              "is not yet supported (no golden reference confirms the "
              "spill a real compiler would emit) - see docs/DEVLOG.md");
}

/* A base register - one the 8086 can address memory through. */
static int is_base_reg(const char *reg)
{
    return strcmp(reg, "di") == 0 || strcmp(reg, "si") == 0 ||
           strcmp(reg, "bx") == 0;
}

/* load_into_di()/load_into_si() for any register. */
static void load_into(GenState *g, const char *reg, Val v)
{
    if (v.kind == VK_REG && strcmp(v.reg, reg) == 0)
        return;
    ins2(g, v.kind == VK_MEM_DIRECT ? "lea" : "mov", o_reg(reg), o_val(v));
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

/* Bit-fields (06_struct/07_bitfield). mutos_c0 writes a field as the
 * STAR of the word holding it, typed unsigned, then FSEL(TY_UNSIGN,
 * bitoffs, flen) - v7/cc/c01.c's build() of a field member. */

/* A bit-field's value: v7/cc/c12.c's unoptim() turns FSEL into "(word
 * >> bitoffs) & ((1 << flen) - 1)" - the word loaded into the working
 * register (DI, or SI while DI holds a pending value - the next free
 * register), shifted by emit_const_shift()'s rule and masked in
 * decimal: 07_bitfield.s.golden's "f.ready + f.mode + f.count" -> "mov
 * di,*-6.(bp)" / "and di,*1." (no shift at bit 0), "mov si,*-6.(bp)" /
 * "sar si,*1" / "sar si,*1" / "and si,*3.", "mov si,*-6.(bp)" / "mov
 * cx,*4." / "sar si,cl" / "and si,*15.". SAR drags the sign bit in, but
 * the mask removes every bit above the field. The AND leaves the flags
 * set from the value, as OP_AND's own result does (see Val's
 * `flagsv`). */
static Val gen_field_load(GenState *g, Val word, int bitoffs, int flen)
{
    const char *reg = pick_addr_reg(g);
    ins2(g, "mov", o_reg(reg), o_val(word));
    if (bitoffs > 0)
        emit_const_shift(g, "sar", reg, bitoffs);
    ins2(g, "and", o_reg(reg), o_imm((1L << flen) - 1));
    Val v = val_reg(reg);
    v.flagsv = 1;
    v.flags_at = g->ninsn;
    return v;
}

/* A constant assigned to a bit-field, v7/cc/c12.c's lvfield() / FSELA
 * rules: 0 clears the field ("&= ~mask", the mask complemented, in
 * decimal - 07_bitfield.s.golden's "f.error = 0;" -> "and *-6.(bp),
 * *-3."); a value equal to the field's mask sets it ("|= value" -
 * "f.ready = 1;" -> "or *-6.(bp),*1.", a 1-bit field at bit 0; v7
 * compares the UNSHIFTED value with the shifted mask, so for a field
 * above bit 0 this rule never applies to a value that fits); any other
 * value clears the field, with the complemented mask in HEX - the FSELA
 * code template's own rendering, "/" and lower-case digits with the
 * operand's size marker - and ORs the shifted value in, in decimal:
 * "f.mode = 2;" -> "and *-6.(bp),<*>/fff3" / "or *-6.(bp),*8.", "f.count
 * = 9;" -> "and *-6.(bp),#/ff0f" / "or *-6.(bp),#144." (<*> is the
 * one-character '*' marker, which cannot stand before a '/' inside this
 * comment). A computed
 * right-hand side (the FSELA template's shift-and-mask of a register)
 * has no golden, and a constant that does not fit the field would be
 * truncated by v7's template in a way no golden shows - both refused. */
static void gen_field_store(GenState *g, Val field, Val rhs)
{
    if (rhs.kind != VK_IMM)
        gen_fatal("assigning a computed value to a bit-field is not yet "
                  "supported (only a constant) - see src/mutos_cc/README.md");
    long fmask = (1L << field.flen) - 1;
    long mask = (fmask << field.bitoffs) & 0xFFFFL;
    long notmask = ~mask & 0xFFFFL;
    long snotmask = notmask >= 0x8000L ? notmask - 0x10000L : notmask;
    Val word = val_from_simple(field.cl);
    long v = rhs.imm;
    if (v == 0) {
        ins2(g, "and", o_val(word), o_imm(snotmask));
        return;
    }
    if (v < 0 || v > fmask)
        gen_fatal("assigning a constant outside a bit-field's range (%ld "
                  "to a %d-bit field) is not yet supported - see "
                  "src/mutos_cc/README.md", v, field.flen);
    if (v == mask) {
        ins2(g, "or", o_val(word), o_imm(v));
        return;
    }
    ins2(g, "and", o_val(word), o_fmt("%c/%lx", disp_marker(snotmask), notmask));
    /* The shifted value is a 16-bit int constant, printed signed like
     * every decimal immediate (a field reaching bit 15). */
    long ov = (v << field.bitoffs) & 0xFFFFL;
    ins2(g, "or", o_val(word), o_imm(ov >= 0x8000L ? ov - 0x10000L : ov));
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
    if (c.cond_is_ltest) {
        /* A long variable tested for truth (see plan_expression()'s
         * `ltest`): both words loaded, each compared with 0, the high one
         * first - v7's cbranch() on a long ("tst R" / "tst R+") -
         * fltprobe/p22_long2.s.golden's "if (l)" -> "mov si,*-6.(bp)" /
         * "mov di,*-8.(bp)" / "cmp di,*0" / "bne L10008" / "cmp si,*0" /
         * "beq L9" / "L10008:", "if (!l)" -> ... "cmp di,*0" / "bne L10" /
         * "cmp si,*0" / "bne L10". */
        if (g->ndeferred > base)
            gen_fatal("a postfix '++'/'--' in a 'long' truth test is not yet "
                      "supported - see src/mutos_cc/README.md");
        Val lv = val_from_simple(c.cl);
        if (lv.kind != VK_MEM || (g->reserved & (RB_DI | RB_SI)))
            gen_fatal("internal: a 'long' truth test lost its operand");
        ins2(g, "mov", o_reg("si"), o_mem(lv.offset + MCC_SZINT));
        ins2(g, "mov", o_reg("di"), o_mem(lv.offset));
        ins2(g, "cmp", o_reg("di"), o_cmpimm(0));
        if (branch_op == OP_NEQUAL) {
            ins1(g, "bne", o_lab(lbl));
            ins2(g, "cmp", o_reg("si"), o_cmpimm(0));
            ins1(g, "bne", o_lab(lbl));
        } else {
            int xlab = g->next_lab++;
            ins1(g, "bne", o_lab(xlab));
            ins2(g, "cmp", o_reg("si"), o_cmpimm(0));
            ins1(g, "beq", o_lab(lbl));
            put_label(g, xlab);
        }
        return;
    }
    if (c.cond_is_float) {
        /* The comparison's "call fcmp" / "sahf" are out already (gen_fp_
         * compare()); the branch reads their flags, so nothing may come
         * between - a postfix fixup here would have to. */
        if (g->ndeferred > base)
            gen_fatal("a postfix '++'/'--' in a 'float'/'double' comparison "
                      "is not yet supported - see src/mutos_cc/README.md");
        if (c.flags_at != g->ninsn)
            gen_fatal("internal: code between a floating comparison and its "
                      "branch");
        ins1(g, cond_true_mnem(branch_op), o_lab(lbl));
        return;
    }
    flush_deferred_from(g, base);
    /* A value an AND has just computed in its register, tested against
     * 0 (for truth, or "== 0"/"!= 0"): the AND's own flags are the
     * test - v7/cc/table.s's cctab compiles an AND as a bit test, the
     * PDP-11's "bit", which MUTOS renders as the AND itself followed by
     * the branch: "movb dx,#_amxscd(bx)" / "and dx,*12." / "beq L144"
     * (tests/mutos_as/kernel_nonopt/amx.s, a char element masked with
     * 014) and "call _inb" / "add sp,*2." / "and ax,*9." / "bne L110"
     * (lp_AC.s, a call's result). Only while nothing has been emitted
     * since the AND (a postfix fixup flushed just above, for one, would
     * have changed the flags) - otherwise the ordinary compare below. */
    if (c.cl.kind == VK_REG && c.cl.flagsv && c.cl.flags_at == g->ninsn &&
        c.cr.kind == VK_IMM && c.cr.imm == 0 &&
        (branch_op == OP_EQUAL || branch_op == OP_NEQUAL)) {
        ins1(g, cond_true_mnem(branch_op), o_lab(lbl));
        return;
    }
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

/* A long "?:" arm's value into DI:SI - see plan_value()'s QUEST. A
 * constant arm is the LCON's own load (materialize_long()); anything but
 * a variable or a constant has no golden. */
static void gen_arm_load_long(GenState *g)
{
    Val v = pop_val(g);
    if (g->reserved & (RB_DI | RB_SI))
        gen_fatal("a 'long' '?:' in a function with a register variable is "
                  "not yet supported");
    if (v.kind == VK_MEM) {
        ins2(g, "mov", o_reg("si"), o_mem(v.offset + MCC_SZINT));
        ins2(g, "mov", o_reg("di"), o_mem(v.offset));
        return;
    }
    if (v.kind == VK_LCON) {
        (void)materialize_long(g, v);
        return;
    }
    gen_fatal("this 'long' '?:' arm is not yet supported (only a 'long' "
              "variable or constant) - see src/mutos_cc/README.md");
}

static int is_float_val(const Val *v);
static void fp_load(GenState *g, Val v);

/* A floating "?:" arm's value onto the floating-point stack - see
 * plan_quest(). */
static void gen_arm_load_float(GenState *g)
{
    Val v = pop_val_ex(g, POP_FLOAT);
    if (!is_float_val(&v))
        gen_fatal("this 'float'/'double' '?:' arm is not yet supported - see "
                  "src/mutos_cc/README.md");
    fp_load(g, v);
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
    if ((l.itolw && r.kind == VK_MEM && !r.itolw) ||
        (r.itolw && l.kind == VK_MEM && !l.itolw)) {
        /* A long variable times an int variable widened ("m * i" or "i *
         * m"): v7's acommute() orders the operands by degree, the ITOL
         * (2) ahead of the NAME, so cr82's "SS" pushes the variable first
         * and "FS" widens the int and pushes it after - fltprobe/
         * p31_long4.s.golden's "m = m * i;" -> "mov di,*-10.(bp)" / "push
         * di" / "mov di,*-12.(bp)" / "push di" / "mov ax,*-14.(bp)" /
         * "cwd" / "push ax" / "push dx" / "call lmul" / "add sp,*8.".
         * Only for '*' (OP_ITOL makes `itolw` for no other operator). */
        Val mem = l.itolw ? r : l;
        Val iv  = l.itolw ? l : r;
        iv.itolw = 0;
        ins2(g, "mov", o_reg("di"), o_mem(mem.offset + MCC_SZINT));
        ins1(g, "push", o_reg("di"));
        ins2(g, "mov", o_reg("di"), o_mem(mem.offset));
        ins1(g, "push", o_reg("di"));
        emit_cwd_from(g, o_val(iv));
        put_seq(g, SEQ_PUSH_AXDX);
        ins1(g, "call", o_sym(helper));
        ins2(g, "add", o_reg("sp"), o_imm(8));
        put_seq(g, SEQ_DXAX_TO_DISI);
        return val_long();
    }
    int rdxax = (r.kind == VK_LONG && !r.lpair && r.lreg == LREG_DXAX);
    if (l.kind != VK_MEM || (r.kind != VK_MEM && !rdxax))
        gen_fatal("'long' %s with a non-memory operand is not yet "
                  "supported", helper);
    /* Push order r_low, r_high, l_low, l_high - see above. An int widened
     * on the right is pushed straight from DX:AX: fltprobe/p22_long2.s.
     * golden's "l / i" -> "mov ax,*-14.(bp)" / "cwd" / "push ax" / "push
     * dx" / "mov di,*-6.(bp)" / "push di" / "mov di,*-8.(bp)" / "push di"
     * / "call ldiv" / "add sp,*8.", "l % i" the same with "lrem". */
    const int words[4] = { r.offset + MCC_SZINT, r.offset,
                           l.offset + MCC_SZINT, l.offset };
    int first = 0;
    if (rdxax) {
        put_seq(g, SEQ_PUSH_AXDX);
        first = 2;
    }
    for (int i = first; i < 4; i++) {
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
/* Defined with the rest of the floating-point code (see "Floating
 * point" below). */
static int is_float_val(const Val *v);
static int push_fp_arg(GenState *g, Val v);
static void check_fp_args(const Val *items, int nargs);

static int push_call_arg(GenState *g, Val v)
{
    if (is_float_val(&v))
        return push_fp_arg(g, v);
    if (v.bytev || v.kind == VK_CHARX) {
        /* A char argument: widened into AX and pushed from there - the
         * real non-optimized compiler's "movb ax,*-8.(bp)" / "cbw" / "push
         * ax" (a char local), "movb ax,(bx)" / "cbw" / "push ax" (through
         * a pointer), "movb ax,4.+_amxtout(bx)" / "cbw" / "push ax" (an
         * element of a file-scope array, its symbol with no marker) and
         * "movb ax,(di)" / "cbw" / "push ax" (tests/mutos_as/kernel_nonopt/
         * lp_AC.s, amx.s). mutos_c0 writes a char argument unconverted,
         * as v7's build() converts nothing under a COMMA or a CALL (a
         * "no-conversion operator"); an ITOC(TY_INT) one would be the same
         * load. */
        if (v.kind != VK_CHARX) {
            Val c = {0};
            c.kind = VK_CHARX;
            v.bytev = 0;
            c.cl = simple_of(v);
            v = c;
        }
        (void)load_charx(g, v);
        ins1(g, "push", o_reg("ax"));
        return 1;
    }
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
        if (g->reserved & RB_DI) {
            /* DI holds a register variable: through SI - fltprobe/
             * p5_call.s.golden's "fi(d + e, 3)" -> "mov si,*3." / "push
             * si". */
            if (v.kind != VK_IMM)
                gen_fatal("pushing this address argument in a function with "
                          "a register variable in DI is not yet supported - "
                          "see src/mutos_cc/README.md");
            ins2(g, "mov", o_reg("si"), o_val(v));
            ins1(g, "push", o_reg("si"));
            return 1;
        }
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
    check_fp_args(items, nargs);
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
        if (is_float_val(&items[i]))
            w = RB_AX | RB_BX | RB_CX | RB_DX;   /* runtime calls */
        else if (items[i].bytev || items[i].kind == VK_CHARX)
            w = RB_AX;                           /* movb ax / cbw / push ax */
        else if (items[i].kind == VK_LCON)
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

/* The high-word instruction of a 'long' '+', '-', '&', '|' or '^': the
 * carry/borrow chained for '+'/'-', the same instruction otherwise. */
static const char *long_mnem_hi(int op)
{
    const AluOp *a = aluop(op);
    return a->mnem_hi ? a->mnem_hi : a->mnem;
}

/* A 'long' '+', '-', '&', '|' or '^' of a variable in memory (`l`, the
 * left operand) and a long constant that is sign-extension-shaped (an
 * in-range LCON, or an int constant widened - OP_ITOL): the constant is
 * sign-extended into DX:AX and pushed, the variable loaded into DI:SI,
 * the constant popped into BX (high) and CX (low) - 02_long/01_addsub.
 * s.golden's "c = c + 1L" -> "mov ax,*1." / "cwd" / "push ax" / "push
 * dx" / "mov si,*-14.(bp)" / "mov di,*-16.(bp)" / "pop bx" / "pop cx" /
 * "add si,cx" / "adc di,bx", and fltprobe/p19_open3.s.golden's "m & 255"
 * and "m | 6" the same with "and"/"or" for both words. The second "pop"
 * is written "pop cx" with a space, not a tab, in both goldens. */
/* The rest of gen_long_constop() once the right operand is in DX:AX:
 * pushed, `l` loaded, popped and combined. */
static Val gen_long_pushop(GenState *g, int op, Val l)
{
    put_seq(g, SEQ_PUSH_AXDX);
    ins2(g, "mov", o_reg("si"), o_mem(l.offset + MCC_SZINT));
    ins2(g, "mov", o_reg("di"), o_mem(l.offset));
    ins1(g, "pop", o_reg("bx"));
    ins0(g, "pop cx"); /* confirmed literal space, not a tab - so emitted
                        * as one operand-less "mnemonic" on purpose. */
    ins2(g, aluop(op)->mnem, o_reg("si"), o_reg("cx"));
    ins2(g, long_mnem_hi(op), o_reg("di"), o_reg("bx"));
    return val_long();
}

static Val gen_long_constop(GenState *g, int op, Val l, Val r)
{
    emit_cwd_from(g, o_imm(r.imm));
    return gen_long_pushop(g, op, l);
}

/* -------------------------------------------------------------- */
/* A 'char' with one int operand.
 *
 * mutos_c0 widens a char operand of an int operator with ITOC(TY_INT)
 * (-> VK_CHARX); the one way the real compiler turns a char into an int
 * is "movb ax,<char>" / "cbw" (load_charx()) - CBW works on AL/AX only -
 * and the operator then works on AX in place, the working register v7's
 * code tables compute into being wherever the left operand already is:
 *
 *   "movb ax,*23.(bx)" / "cbw" / "and ax,*-2." / "or ax,*16." / "pop bx"
 *       / "movb *23.(bx),ax"           (3x - a char member masked, stored)
 *   "movb ax,*52.(di)" / "cbw" / "mov ax,ax" / "mov cx,*20." / "imul cx"
 *                                       (5x - a char times 20)
 *
 * (tests/mutos_as/kernel_nonopt/amx.s, the real non-optimized compiler's
 * output; the "mov ax,ax" is OP_TIMES's own "mov ax,<left>", as in
 * 04_funcs/03_recfact.s.golden). A '-' of a constant is v7/cc/c12.c
 * optim()'s "x - c" -> "x + -c" (its MINUS case), so "c - '0'" is "add
 * ax,*-48." - tests/mutos_as/kernel_opt/genio.s ("movb ax,*-10.(bp)" /
 * "cbw" / "add ax,*-48."), and a char compound subtraction "addb
 * *-8.(bp),*-32." in kernel_nonopt/lp_AC.s; "+ 1"/"- 1" are "inc"/"dec"
 * as for DI (07_ternary's "inc di", 03_recfact's "dec di"), and the
 * shifts are kernel_opt's "cbw" / "sal ax,*1" and "sal ax,cl".
 *
 * One exception: a char AND a constant 0..127 is loaded without the
 * CBW, into DX, the byte working register - the mask clears the high
 * byte a "movb" leaves undefined: "mov dx,*-10.(bp)" / "mov bx,dx" /
 * "movb dx,(bx)" / "and dx,*127." / "pop bx" / "movb 4.+_amxtout(bx),dx"
 * and "movb dx,#_amxscd(bx)" / "and dx,*12." / "beq L144" (amx.s; the
 * element's symbol takes v7's "#1" marker - o_load()). A mask with
 * bit 7 set, 0200..0377, would work the same way but has no example,
 * so it takes the CBW path.
 *
 * Only an int LEAF as the other operand - a constant, a variable in
 * memory, a 'register' local - so no code of its own lies between the
 * char's load and the operator (where the real compiler would evaluate
 * a computed operand relative to the char is not shown anywhere). For a
 * commutative operator the char goes left, as v7's acommute() sorts it
 * (a char leaf's degree(), 1, above an int leaf's 0); "x - c" and a
 * char shift count have no example and are refused. */

/* An int leaf - see above. */
/* A long variable plus or minus an int constant widened (VK_LCON from
 * OP_ITOL, not negative), in place: the low word combined with the
 * constant, the high word with the carry - fltprobe/p25_long3.s.golden's
 * "l += 1;" and "l++;" -> "add *-6.(bp),*1." / "adc *-8.(bp),*0", "l--;"
 * -> "sub *-6.(bp),*1." / "sbb *-8.(bp),*0" (the high word's "*0" written
 * as a template constant, without a decimal point - as in "cmp di,*0").
 * Returns 0, emitting nothing, for any other operand pair. */
static int long_inplace_const(GenState *g, int sub, Val lhs, Val rhs)
{
    if (lhs.kind != VK_MEM || rhs.kind != VK_LCON || !rhs.fromu ||
        rhs.imm < 0 || rhs.offset != 0)
        return 0;
    ins2(g, sub ? "sub" : "add", o_mem(lhs.offset + MCC_SZINT), o_imm(rhs.imm));
    ins2(g, sub ? "sbb" : "adc", o_mem(lhs.offset), o_cmpimm(0));
    return 1;
}

static int is_int_leaf(const Val *v)
{
    switch (v->kind) {
    case VK_IMM:     return !v->charcon;
    case VK_MEM:
    case VK_MEM_CVT:
    case VK_STATIC:  return !v->bytev;
    case VK_REG:     return v->regvar;
    default:         return 0;
    }
}

/* Puts the one VK_CHARX operand of a commutative or char-left operator
 * on the left and checks the other one - see above. */
static void charx_left(int op, Val *l, Val *r)
{
    int commute = (op == OP_PLUS || op == OP_AND || op == OP_OR ||
                   op == OP_EXOR || op == OP_TIMES);
    if (l->kind == VK_CHARX && r->kind == VK_CHARX)
        gen_fatal("%s of two 'char' operands is not yet supported "
                  "(confirmed so far: the sum of two chars) - see "
                  "src/mutos_cc/README.md", aluop(op)->name);
    if (r->kind == VK_CHARX) {
        if (!commute)
            gen_fatal("%s with a 'char' right operand is not yet supported "
                      "- see src/mutos_cc/README.md", aluop(op)->name);
        Val t = *l;
        *l = *r;
        *r = t;
    }
    if (!is_int_leaf(r))
        gen_fatal("%s of a 'char' and a computed int value is not yet "
                  "supported (only a constant, a variable or a 'register' "
                  "local as the int operand) - see src/mutos_cc/README.md",
                  aluop(op)->name);
}

/* PLUS, MINUS, AND, OR, EXOR, LSHIFT or RSHIFT of a VK_CHARX (left, after
 * charx_left()) and an int leaf - see above. Returns the result, in AX
 * (DX for the byte mask). */
static Val gen_charx_binop(GenState *g, int op, Val l, Val r)
{
    if (op == OP_AND && r.kind == VK_IMM && r.imm >= 0 && r.imm <= 127) {
        ins2(g, "movb", o_reg("dx"), o_load(val_from_simple(l.cl)));
        ins2(g, "and", o_reg("dx"), o_imm(r.imm));
        Val d = val_reg("dx");
        d.flagsv = 1;
        d.flags_at = g->ninsn;
        return d;
    }
    (void)load_charx(g, l);
    Val ax = val_reg("ax");
    switch (op) {
    case OP_PLUS:
    case OP_MINUS:
        if (r.kind == VK_IMM) {
            long k = (op == OP_MINUS) ? -r.imm : r.imm;   /* v7's x - c */
            if (k == 1)
                ins1(g, "inc", o_reg("ax"));
            else if (k == -1)
                ins1(g, "dec", o_reg("ax"));
            else if (k != 0)                              /* v7 drops + 0 */
                ins2(g, "add", o_reg("ax"), o_imm(k));
        } else {
            ins2(g, aluop(op)->mnem, o_reg("ax"), o_val(r));
        }
        break;
    case OP_AND:
    case OP_OR:
    case OP_EXOR:
        ins2(g, aluop(op)->mnem, o_reg("ax"), o_val(r));
        if (op == OP_AND) {
            ax.flagsv = 1;
            ax.flags_at = g->ninsn;
        }
        break;
    case OP_LSHIFT:
    case OP_RSHIFT:
        if (r.kind == VK_IMM) {
            emit_const_shift(g, aluop(op)->mnem, "ax", r.imm);
        } else {
            load_into_cx(g, r);
            ins2(g, aluop(op)->mnem, o_reg("ax"), o_reg("cl"));
        }
        break;
    default:
        gen_fatal("internal: gen_charx_binop() for opcode %d", op);
    }
    return ax;
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
 * caller stops there). A CON's value or a NAME's storage class comes
 * back through *aux (0 for anything else), for the evaluation-order
 * decisions that look at a subtree's shape (is_disp_store(),
 * is_deferred_ptr(), enode_degree()); scan_op_args() is the same without
 * it. */
static Arity scan_op_args_v(FILE *t1, int op, int *type, long *aux)
{
    *type = 0;
    *aux = 0;
    switch (op) {
    case OP_NAME: {
        int hclass = c1_read_num(t1, "temp1");
        *type = c1_read_num(t1, "temp1");
        *aux = hclass;
        if (hclass == SC_EXTERN)
            free(c1_read_sym(t1, "temp1"));
        else
            (void)c1_read_num(t1, "temp1");
        return AR_LEAF;
    }
    case OP_CON:
        *type = c1_read_num(t1, "temp1");
        *aux = c1_read_num(t1, "temp1");
        return AR_LEAF;
    case OP_LCON:
        *type = c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        return AR_LEAF;
    case OP_NULLOP:
        return AR_LEAF;
    case OP_FCON:                     /* type, then the constant's text */
        *type = c1_read_num(t1, "temp1");
        free(c1_read_sym(t1, "temp1"));
        return AR_LEAF;
    case OP_AMPER: case OP_STAR: case OP_COMPL: case OP_EXCLA:
    case OP_NEG: case OP_LTOI: case OP_ITOC: case OP_CTOL: case OP_ITOL:
    case OP_RFORCE:
    case OP_ITOF: case OP_FTOI: case OP_FTOL: case OP_LTOF:
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
    case OP_STRASG:                   /* after a struct ASSIGN */
        *type = c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        return AR_UNARY;
    case OP_FSEL:                     /* a bit-field of the STAR below */
        *type = c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        return AR_UNARY;
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

static Arity scan_op_args(FILE *t1, int op, int *type)
{
    long aux;
    return scan_op_args_v(t1, op, type, &aux);
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

/* Whether a dereference consumed by `c` is loaded into its register
 * right away, before its sibling's code: it is the LEFT operand of a
 * comparison or of an int '+', '-' or '*' whose right operand has code
 * of its own - v7's template computes the left operand into its
 * register first ("F"), the right one into the next ("S1"). 10_integ/
 * 02_bubsort.s.golden's "a[j] > a[j + 1]" -> ... "add di,*4.(bp)" / "mov
 * di,(di)" / "mov si,*-8.(bp)" / ... / "cmp di,*2.(si)", 06_struct/
 * 05_nestst.s.golden's "rp->botright.y - rp->topleft.y" -> "mov di,
 * *4.(bp)" / "mov di,*6.(di)" / "mov si,*4.(bp)" / "sub di,*2.(si)", and
 * fltprobe/p10_elem.s.golden's "ps[i].c * qs[i].g" -> ... "add di,si" /
 * "mov di,*4.(di)" / "lea si,*-52.(bp)" / ... / "mov ax,di" / "imul
 * *12.(si)". (With a lone variable or constant on the right, the load -
 * if any - happens in the operator's own handler, the same bytes.) */
static int load_now(const Consumer *c)
{
    if (!c->as_left || c->nops <= 1)
        return 0;
    if (find_relop(c->op))
        return 1;
    return (c->op == OP_PLUS || c->op == OP_MINUS || c->op == OP_TIMES) &&
           (c->type == TY_INT || c->type == TY_UNSIGN);
}

/* The next opcode in temp1, its position left unchanged. */
static int peek_op(FILE *t1)
{
    long pos = ftell(t1);
    if (pos < 0)
        gen_fatal("internal: temp1 is not seekable (ftell failed)");
    int op = c1_read_op(t1, "temp1");
    if (fseek(t1, pos, SEEK_SET) != 0)
        gen_fatal("internal: temp1 is not seekable (fseek failed)");
    return op;
}

/* Whether the next two opcodes add a constant to the pointer value
 * just produced - "CON, PLUS(pointer)", a struct member's offset. */
static int next_is_con_plus(FILE *t1)
{
    long pos = ftell(t1);
    if (pos < 0)
        gen_fatal("internal: temp1 is not seekable (ftell failed)");
    int yes = 0;
    if (c1_read_op(t1, "temp1") == OP_CON) {
        (void)c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        if (c1_read_op(t1, "temp1") == OP_PLUS)
            yes = ty_is_ptr(c1_read_num(t1, "temp1"));
    }
    if (fseek(t1, pos, SEEK_SET) != 0)
        gen_fatal("internal: temp1 is not seekable (fseek failed)");
    return yes;
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
 * Only golden-confirmed decisions are taken: int TIMES of two words read
 * through computed addresses whose right one is at offset 0
 * (order_right_first() - 05_matmul's 2-D elements, fltprobe/p14_axint's
 * "a[i] * b[j]", p17_elem2's "return a[i] * b[j]"); for '+', '-', '&',
 * '|', '^' the same pair pushes the right one's ADDRESS instead
 * (ORD_PUSHADDR - p14_axint's "a[i] & b[j]", p17_elem2's other four), as
 * does '-' with a variable on the left (p19_open3's "x - b[j]"); a
 * comparison of two such elements pushes the LEFT one's address
 * (ORD_PUSHLEFT - p17_elem2's "a[i] > b[j]"). With an offset the right
 * operand is NOT computed first: fltprobe/p10_elem.s.golden's "ps[i].c *
 * qs[i].g" and p17_elem2's "ps[i].c * qs[i].d" load the left one into DI
 * ("mov di,*4.(di)" - load_now()), compute the right one's address into
 * SI with DX as the index, then "mov ax,di" / "imul *12.(si)" - see
 * is_elem_read() for what is and is not understood about the
 * difference. 02_bubsort's
 * swapped relational and 03_linklist's right-hand-side-first store are
 * the same mechanism with other decisions (see
 * docs/DEVLOG.md); neither is taken here. */

/* How a node's operands are ordered, when not in temp1's own postfix
 * order - see eval_order(). */
typedef enum {
    ORD_POSTFIX = 0, /* as written: left operand, right operand, node */
    ORD_SPILL,       /* right operand first, spilled onto the machine
                      * stack - the "%n,n" template (order_right_first()) */
    ORD_DISPSTORE,   /* an ASSIGN through "pointer + constant": the right-
                      * hand side first - see is_disp_store() */
    ORD_PUSHADDR,    /* '+', '-', '&', '|', '^' of two elements, the right
                      * one's address pushed first - see is_pushaddr() */
    ORD_PUSHLEFT,    /* a comparison of two elements, the LEFT one's
                      * address pushed first - see is_pushleft() */
    ORD_DEFPTR,      /* a MINUS whose right operand is "*p": p pushed first
                      * - see is_deferred_ptr() */
    ORD_ACOMMUTE,    /* the top of a chain of int '+' whose terms v7's
                      * acommute() reorders - see acommute_order() */
    ORD_LEFTDI,      /* a '+' whose left operand ends in AX and whose right
                      * one is an element at an offset: the left one moved
                      * into DI first - see is_leftdi() */
    ORD_DISTRIB,     /* the top of a chain of int '+' with two or more
                      * constant multiples of comparisons, which v7's
                      * distrib() factors - see is_distrib() */
    ORD_ASPUSH,      /* an int compound assignment into an element at
                      * offset 0: its address pushed first - is_aspush() */
    ORD_ASDISP,      /* an int compound assignment through "pointer +
                      * constant": the right-hand side first - is_asdisp() */
    ORD_ASPTR,       /* an int compound assignment through a pointer
                      * variable: the pointer pushed first - is_asptr() */
    ORD_CALLACOM     /* the top of a chain of int '+' of calls and
                      * variables: the calls first - is_callacom() */
} EvalOrder;

/* One node of a pre-scanned expression. */
typedef struct {
    int  op, type;
    long aux;         /* a CON's value, a NAME's storage class */
    long off;         /* its own opcode tag */
    long start;       /* the first opcode of its subtree */
    long end;         /* just past its own arguments */
    int  kid[2];      /* operand node indices, -1 for none; kid[0] is
                       * the left (or only) operand */
    int  parent;      /* the node this one is an operand of, -1 for the
                       * root */
    EvalOrder order;  /* see EvalOrder */
    int  planned;     /* this node or a descendant has a non-postfix
                       * order or is a conditional-evaluation node
                       * (is_control()) - its subtree cannot simply be
                       * streamed */
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
    int  line;        /* its source line */
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
        int type = 0, lbl = 0, cond = 0, line = 0;
        long aux = 0;
        int op = c1_read_op(t1, "temp1");
        Arity ar;
        if (op == OP_CBRANCH) {
            lbl = c1_read_num(t1, "temp1");
            cond = c1_read_num(t1, "temp1");
            line = c1_read_num(t1, "temp1");      /* source line */
            ar = AR_STMT;
        } else if (op == OP_EXPR) {
            line = c1_read_num(t1, "temp1");
            ar = AR_STMT;
        } else {
            ar = scan_op_args_v(t1, op, &type, &aux);
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
                term->line = line;
            }
            break;
        }
        ENode nd = { op, type, aux, off, off, ftell(t1), { -1, -1 }, -1,
                     ORD_POSTFIX, 0 };
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
        int idx = etree_add(t, nd);
        for (int k = 0; k < need; k++)
            t->v[nd.kid[k]].parent = idx;
        stk[depth++] = idx;
    }
    free(stk);
    if (fseek(t1, savepos, SEEK_SET) != 0)
        gen_fatal("internal: temp1 is not seekable (fseek failed)");
    return root;
}

static int is_elem_read(const ETree *t, int i, int zero_off);

/* The evaluation-order decision - see this section's header: an int '*'
 * of two words read through computed addresses whose right one is at
 * offset 0 - 10_integ/05_matmul's "a[i][k] * b[k][j]" and fltprobe/
 * p14_axint.s.golden's "a[i] * b[j]" ("lea di,*-16.(bp)" / ... / "mov
 * di,(di)" / "push di" / "lea di,*-10.(bp)" / ... / "mov di,(di)" / "mov
 * ax,di" / "pop cx" / "imul cx"). With an offset ("ps[i].c * qs[i].g",
 * p10_elem) the right one is computed after the left, into SI - see
 * is_pushaddr(). */
/* The same right operand under a computed left one - a sum, a difference,
 * a bitwise operation or a shift, whose code needs the working register:
 * fltprobe/p26_elem4.s.golden's "x = (b[i] + 1) * b[j];" -> [b[j]] / "mov
 * di,(di)" / "push di" / [b[i]] / "mov di,(di)" / "inc di" / "mov ax,di" /
 * "pop cx" / "imul cx". A left operand that ends in AX itself (a product,
 * a quotient, a call) has no golden and is left alone. */
/* Node `i`'s int value is left in AX or DX rather than DI: a product, a
 * quotient, a remainder or a call - or an operation done on one of those
 * where it is (OP_PLUS's "add ax,..." / OP_AND's "and ax,*-2."). */
static int ends_in_axdx(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    switch (n->op) {
    case OP_TIMES: case OP_DIVIDE: case OP_MOD: case OP_CALL:
        return 1;
    case OP_PLUS: case OP_MINUS: case OP_AND: case OP_OR: case OP_EXOR:
    case OP_LSHIFT: case OP_RSHIFT: case OP_NEG: case OP_COMPL:
        return n->kid[0] >= 0 && ends_in_axdx(t, n->kid[0]);
    default:
        return 0;
    }
}

static int is_computed_left(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (!(n->type == TY_INT || n->type == TY_UNSIGN) || ends_in_axdx(t, i))
        return 0;
    switch (n->op) {
    case OP_PLUS: case OP_MINUS: case OP_AND: case OP_OR: case OP_EXOR:
    case OP_LSHIFT: case OP_RSHIFT: case OP_NEG: case OP_COMPL:
        return 1;
    default:
        return 0;
    }
}

/* '/' and '%' the same way - p26_elem4.s.golden's "x = b[j] / b[i];" ->
 * [b[i]] / "mov di,(di)" / "push di" / [b[j]] / "mov di,(di)" / "mov ax,di"
 * / "cwd" / "pop cx" / "idiv cx" ('%': the remainder, "mov x,dx"). */
static int order_right_first(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (!(n->op == OP_TIMES || n->op == OP_DIVIDE || n->op == OP_MOD) ||
        !(n->type == TY_INT || n->type == TY_UNSIGN) ||
        !is_elem_read(t, n->kid[1], 1))
        return 0;
    return is_elem_read(t, n->kid[0], 0) || is_computed_left(t, n->kid[0]);
}

/* Follows pointer-valued node `i` through "+ constant" steps and "&*"
 * pairs - a struct member's address, "&(*(p + 4)) + 2" for "p->a.b" -
 * down to the node that computes the base pointer, which it returns;
 * the constants' sum goes to *off. v7/cc/c12.c's optim() does the same
 * folding (the AMPER/STAR cancel, acommute()'s constant merging and
 * "+0" toss) before any code is chosen, so the real compiler's code
 * templates only ever see "base + total offset". */
static int ptr_fold(const ETree *t, int i, long *off)
{
    *off = 0;
    for (;;) {
        const ENode *n = &t->v[i];
        if (n->op == OP_PLUS && ty_is_ptr(n->type) &&
            t->v[n->kid[1]].op == OP_CON) {
            *off += t->v[n->kid[1]].aux;
            i = n->kid[0];
        } else if (n->op == OP_AMPER && t->v[n->kid[0]].op == OP_STAR) {
            i = t->v[n->kid[0]].kid[0];
        } else {
            return i;
        }
    }
}

/* Node `i` reads a word through an address computed at run time - an
 * element subscripted by a variable or a member of one ("a[i]", "ps[i].c",
 * "m[i][j]", "p[i]"): a STAR whose address, after ptr_fold(), is a pointer
 * PLUS whose right operand is not a constant. With `zero_off`, only one at
 * offset 0 ("a[i]", "m[i][j]") - not "ps[i].c" (ptr_fold()'s constant) nor
 * "a[j + 1]", whose "+ 1" v7's insert() turns into a constant term ("c1*(x
 * + c2) -> c1*x + c1*c2"): 02_bubsort's "*2.(si)".
 *
 * Why the offset decides: the real compiler computes a right operand of
 * the first kind after the left one, into the next register, and uses it
 * there ("imul *12.(si)", "add di,*2.(si)", "cmp di,*2.(si)" - v7's "%n,ew*"
 * template), but one at offset 0 it computes FIRST, onto the machine
 * stack - its value for '*' ("%n,n"), its address for '+', '-', '&', '|',
 * '^' ("%n,nw*") - fltprobe/p14_axint.s.golden and p17_elem2.s.golden. Why
 * the offset matters to v7's dcalc() is not understood (both kinds have
 * the same degree), but it is the offset, not the context: p17_elem2's
 * "return a[i] * b[j]" pushes b[j] (as p14's assignment does) and its "s
 * = ps[i].c * qs[i].d" does not (as p10's return does not). In v7's
 * table order a right operand "%n,ew*" matches first; one at offset 0
 * evidently is not "ew*" here. */
/* Node `i`'s subtree reads a variable (a NAME not under '&') - a
 * subscript known only at run time. */
static int reads_var(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op == OP_NAME)
        return 1;
    if (n->op == OP_AMPER && t->v[n->kid[0]].op == OP_NAME)
        return 0;
    for (int k = 0; k < 2; k++)
        if (n->kid[k] >= 0 && reads_var(t, n->kid[k]))
            return 1;
    return 0;
}

static int is_elem_read(const ETree *t, int i, int zero_off)
{
    const ENode *n = &t->v[i];
    if (n->op != OP_STAR || !(n->type == TY_INT || n->type == TY_UNSIGN))
        return 0;
    long off;
    const ENode *base = &t->v[ptr_fold(t, n->kid[0], &off)];
    if (base->op != OP_PLUS || !ty_is_ptr(base->type))
        return 0;
    const ENode *idx = &t->v[base->kid[1]];
    if (!reads_var(t, base->kid[1]))      /* "c[0][1]": a constant address */
        return 0;
    if (!zero_off)
        return 1;
    if (off != 0)
        return 0;
    /* "a[j + 1]": ITOP(PLUS(j, CON 1), size) - a constant term after all */
    if (idx->op == OP_ITOP) {
        const ENode *x = &t->v[idx->kid[0]];
        if ((x->op == OP_PLUS || x->op == OP_MINUS) &&
            t->v[x->kid[1]].op == OP_CON && t->v[x->kid[1]].aux != 0)
            return 0;
    }
    return 1;
}

/* Node `i` is an int-class NAME in memory - a local, a parameter, a
 * local static or a file-scope variable, not a 'register' local. */
static int is_int_memvar(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    return n->op == OP_NAME && (n->type == TY_INT || n->type == TY_UNSIGN) &&
           (n->aux == SC_AUTO || n->aux == SC_STATIC || n->aux == SC_EXTERN);
}

/* An int '+', '-', '&', '|' or '^' (v7's cr40 templates) of two words read
 * through computed addresses whose right one is at offset 0: its address
 * is computed first and pushed, the left operand computed and loaded, the
 * address popped into BX - fltprobe/p14_axint.s.golden's "a[i] & b[j]" ->
 * "lea di,*-16.(bp)" / "mov si,*-20.(bp)" / "sal si,*1" / "add di,si" /
 * "push di" / "lea di,*-10.(bp)" / ... / "mov di,(di)" / "pop bx" / "and
 * di,(bx)" (v7's "%n,nw*": SS*, F, "I *(sp)+,R"); p17_elem2.s.golden the
 * same for '+', '-', '|' and '^' ("xor di,(bx)" - '^' has a table of its
 * own on the PDP-11, whose XOR needs a register source; not here) and the
 * first link of a '+' chain. The template matches any left operand: a
 * '-' with a variable on the left the same way - p19_open3.s.golden's "x
 * - b[j]" -> "lea di,*-24.(bp)" / ... / "push di" / "mov di,*-30.(bp)" /
 * "pop bx" / "sub di,(bx)" (the commutative operators have the element on
 * the left by then: acommute() orders by degree). See is_elem_read() for
 * the offset. */
static int is_pushaddr(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op != OP_PLUS && n->op != OP_MINUS && n->op != OP_AND &&
        n->op != OP_OR && n->op != OP_EXOR)
        return 0;
    if (!(n->type == TY_INT || n->type == TY_UNSIGN) ||
        !is_elem_read(t, n->kid[1], 1))
        return 0;
    if (is_elem_read(t, n->kid[0], 0))
        return 1;
    /* A variable or a constant minus the element: fltprobe/p23_elem3.s.
     * golden's "5 - b[j]" -> ... "push di" / "mov di,*5." / "pop bx" / "sub
     * di,(bx)", "g - b[j]" (a file-scope g) -> "mov di,_g". */
    return n->op == OP_MINUS && (is_int_memvar(t, n->kid[0]) ||
                                 t->v[n->kid[0]].op == OP_CON);
}

/* A comparison of two words read through computed addresses, the right
 * one at offset 0: the LEFT one's address is computed first and pushed,
 * the right one computed and loaded, the address popped into BX and
 * compared in place - fltprobe/p17_elem2.s.golden's "if (a[i] > b[j])" ->
 * "lea di,*-10.(bp)" / "mov si,*-18.(bp)" / "sal si,*1" / "add di,si" /
 * "push di" / "lea di,*-16.(bp)" / ... / "mov di,(di)" / "pop bx" / "cmp
 * (bx),di" / "ble L10" (the relation as written). v7's cctab "%nw*,nw*"
 * ([move11]: FS*, S*, "cmp" through both) - after "%n,ew*", which
 * 02_bubsort's "a[j] > a[j + 1]" and p23_elem3's "a[i] > ps[i].c" match
 * (a right one WITH an offset: "cmp di,*2.(si)", "cmp di,*4.(si)"). A left
 * one with an offset keeps it as the displacement off BX: p23_elem3.s.
 * golden's "if (ps[i].c > b[j])" -> "lea di,*-20.(bp)" / ... / "add
 * di,si" / "push di" / "lea di,*-36.(bp)" / ... / "mov di,(di)" / "pop bx"
 * / "cmp *4.(bx),di". */
static int is_pushleft(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    switch (n->op) {
    case OP_LESS: case OP_LESSEQ: case OP_GREAT: case OP_GREATEQ:
    case OP_EQUAL: case OP_NEQUAL:
        return is_elem_read(t, n->kid[0], 0) && is_elem_read(t, n->kid[1], 1);
    default:
        return 0;
    }
}

/* Node `i` is a NAME of a pointer variable in memory (a local, a
 * parameter, a local static or a file-scope variable - not a
 * 'register' local, which v7 addresses like an index register). */
static int is_ptr_var(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    return n->op == OP_NAME && ty_is_ptr(n->type) &&
           (n->aux == SC_AUTO || n->aux == SC_STATIC || n->aux == SC_EXTERN);
}

/* A store through "pointer + non-zero constant" - a struct member
 * other than the first, reached through a pointer or an array element
 * ("p->next = head;", "pts[i].y = i * 2;") - with a right-hand side
 * that is not a lone constant: the real compiler computes the right-
 * hand side FIRST, into DI, then the pointer into the next free
 * register, and stores through the displacement - 10_integ/03_linklist.
 * s.golden's "cur->next = head;" -> "mov di,*-6.(bp)" / "mov si,
 * *-8.(bp)" / "mov *2.(si),di", 06_struct/02_stptr.s.golden's "pp->y =
 * pp->y + dy;" -> "mov di,*4.(bp)" / "mov di,*2.(di)" / "add di,*8.
 * (bp)" / "mov si,*4.(bp)" / "mov *2.(si),di", and 03_starray.s.
 * golden's "pts[i].y = i * 2;" -> "mov di,*-18.(bp)" / "sal di,*1" /
 * "lea si,*-16.(bp)" / "mov dx,*-18.(bp)" / "sal dx,*1" / "sal dx,*1"
 * / "add si,dx" / "mov *2.(si),di". With the member at offset 0 the
 * same goldens push the target's address first instead ("cur->val =
 * i;" -> "push *-8.(bp)" ... "pop bx" / "mov (bx),di" - OP_STAR's
 * VK_IND_PENDING): on the PDP-11 that "*p" is itself an addressable
 * operand ("@-8(r5)"), "*(p + 2)" is not. A constant right-hand side
 * ("p->y = 5;") and a link-time-constant address ("p->s = &glob;") keep
 * the target-first order (the constant is stored straight through the
 * loaded pointer); a target whose base is a named object ("s.y = x",
 * no code at all) is left alone - both orders emit the same bytes. */
static int is_disp_store(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op != OP_ASSIGN || !ty_is_word(n->type))
        return 0;
    const ENode *lhs = &t->v[n->kid[0]];
    if (lhs->op != OP_STAR || !ty_is_word(lhs->type))
        return 0;
    long off;
    int base = ptr_fold(t, lhs->kid[0], &off);
    if (off == 0 || t->v[base].op == OP_AMPER)
        return 0;
    const ENode *rhs = &t->v[n->kid[1]];
    if (rhs->op == OP_CON)
        return 0;
    if (rhs->op == OP_AMPER && t->v[rhs->kid[0]].op == OP_NAME &&
        t->v[rhs->kid[0]].aux != SC_AUTO)
        return 0;
    return 1;
}

/* An int "x - *p" whose left operand has code of its own or is a
 * variable, and whose right one reads through a pointer VARIABLE with no
 * offset (the first member of "p->..."): the pointer is pushed before the
 * left operand is computed and popped into BX for the subtraction -
 * 06_struct/05_nestst.s.golden's "rp->botright.x - rp->topleft.x" ->
 * "push *4.(bp)" / "mov di,*4.(bp)" / "mov di,*4.(di)" / "pop bx" / "sub
 * di,(bx)", and fltprobe/p23_elem3.s.golden's "x - *p" -> "push *-38.
 * (bp)" / "mov di,*-44.(bp)" / "pop bx" / "sub di,(bx)" (the variable
 * loaded by SEG_DEFPOP). It is the store's VK_IND_PENDING shape (see
 * is_disp_store()) for an operand: "*p" is an addressable PDP-11 operand
 * ("@4(r5)"), which the x86 code reaches through BX. With a non-zero
 * offset ("rp->botright.y - rp->topleft.y") the same golden computes left
 * to right, "mov si,*4.(bp)" / "sub di,*2.(si)". A constant on the left
 * and '+' have no golden. */
static int is_deferred_ptr(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op != OP_MINUS || n->type != TY_INT)
        return 0;
    const ENode *l = &t->v[n->kid[0]];
    const ENode *r = &t->v[n->kid[1]];
    if ((l->kid[0] < 0 && !is_int_memvar(t, n->kid[0])) ||
        r->op != OP_STAR || r->type != TY_INT)
        return 0;
    if (l->op == OP_AMPER && t->v[l->kid[0]].op == OP_NAME)
        return 0;
    long off;
    int base = ptr_fold(t, r->kid[0], &off);
    return off == 0 && is_ptr_var(t, base);
}

/* v7/cc/c11.c's degree() of node `i`, as the MUTOS compiler's operand
 * ordering shows it: a constant -3, a named object's address -2, a
 * leaf 0 (a char one 1), and anything computed at least 1, one more
 * than two equal-degree operands (a Sethi-Ullman number). The "+0" of a
 * first struct member and an "&*" pair are folded away first, as
 * optim() does. Only the RELATIVE order matters - it decides where
 * acommute_order() puts a term. (v7's PDP-11 optim() keeps an int
 * expression's degree at 0 however deep it is; the MUTOS compiler
 * evidently does not - 06_struct/03_starray.s.golden adds "sum" after
 * both computed terms of "sum + pts[i].x + pts[i].y".) */
/* Node `i` is a compile-time constant int - a CON, or an ITOP or '+'/'-'
 * of two (a subscript "b[0]"'s "CON 0, CON 2, ITOP"). */
static int is_const_node(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op == OP_CON)
        return 1;
    if ((n->op == OP_ITOP || n->op == OP_PLUS || n->op == OP_MINUS) &&
        n->kid[1] >= 0)
        return is_const_node(t, n->kid[0]) && is_const_node(t, n->kid[1]);
    return 0;
}

static int enode_degree(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    switch (n->op) {
    case OP_CON:
        return -3;
    case OP_NAME:
        return n->type == TY_CHAR ? 1 : 0;
    case OP_STAR: {
        /* An element at a constant index of a named array, "b[0]": v7's
         * optim() folds "*(&b + 0)" into the NAME itself - a leaf, degree
         * 0 - fltprobe/p26_elem4.s.golden's "r + b[0] + b[1] + b[2] +
         * ps[i].c" adds the three after the member and before r, as
         * acommute() places leaves. */
        const ENode *a = &t->v[n->kid[0]];
        if (a->op == OP_PLUS && t->v[a->kid[0]].op == OP_AMPER &&
            t->v[t->v[a->kid[0]].kid[0]].op == OP_NAME &&
            is_const_node(t, a->kid[1]))
            return n->type == TY_CHAR ? 1 : 0;
        break;
    }
    case OP_AMPER:
        if (t->v[n->kid[0]].op == OP_STAR)
            return enode_degree(t, t->v[n->kid[0]].kid[0]);
        if (t->v[n->kid[0]].op == OP_NAME)
            return -2;
        break;
    case OP_PLUS:
        if (t->v[n->kid[1]].op == OP_CON && t->v[n->kid[1]].aux == 0)
            return enode_degree(t, n->kid[0]);
        break;
    default:
        break;
    }
    if (n->kid[0] < 0)
        return 0;
    if (n->kid[1] < 0) {
        int d = enode_degree(t, n->kid[0]);
        return d > 1 ? d : 1;
    }
    int d1 = enode_degree(t, n->kid[0]);
    int d2 = enode_degree(t, n->kid[1]);
    if (d1 < 1)
        d1 = 1;
    if (d2 < 0)
        d2 = 0;
    return d1 == d2 ? d1 + 1 : (d1 > d2 ? d1 : d2);
}

/* Node `i`'s subtree has no side effect and no conditional evaluation,
 * so its code may move relative to its siblings'. */
static int is_pure(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    switch (n->op) {
    case OP_NAME: case OP_CON:
        return 1;
    case OP_AMPER: case OP_STAR: case OP_ITOC: case OP_COMPL: case OP_NEG:
    case OP_FSEL:
        return is_pure(t, n->kid[0]);
    case OP_PLUS: case OP_MINUS: case OP_TIMES: case OP_ITOP: case OP_AND:
    case OP_OR: case OP_EXOR: case OP_LSHIFT: case OP_RSHIFT:
        return is_pure(t, n->kid[0]) && is_pure(t, n->kid[1]);
    default:
        return 0;
    }
}

/* Node `i` is a '+' of type `type` - a link of the chain of '+'
 * acommute_order() reorders. */
static int is_chain_plus(const ETree *t, int i, int type)
{
    return t->v[i].op == OP_PLUS && t->v[i].type == type;
}

#define ACOMMUTE_MAX 16

/* The terms of the '+' chain whose top is node `top`, in source order,
 * into terms[] (at most ACOMMUTE_MAX - returns -1 beyond that), and the
 * chain's own '+' nodes, innermost first, into pluses[]. */
static int chain_terms(const ETree *t, int top, int *terms, int *pluses,
                       int *nplus)
{
    int type = t->v[top].type;
    int n = 0;
    *nplus = 0;
    /* The chain is left-leaning as written ("a + b + c" is ((a + b) +
     * c)), but a parenthesized right operand ("a + (b + c)") joins it
     * too, as insert() walks both operands. An explicit stack keeps
     * source order. */
    int stack[2 * ACOMMUTE_MAX], sp = 0;
    stack[sp++] = top;
    while (sp > 0) {
        int i = stack[--sp];
        if (is_chain_plus(t, i, type)) {
            if (sp + 2 > 2 * ACOMMUTE_MAX)
                return -1;
            stack[sp++] = t->v[i].kid[1];
            stack[sp++] = t->v[i].kid[0];
            continue;
        }
        if (n == ACOMMUTE_MAX)
            return -1;
        terms[n++] = i;
    }
    /* The chain's '+' nodes in postfix order (node indices are postfix
     * order): those whose chain of '+' parents reaches `top`. */
    for (int i = 0; i <= top; i++) {
        if (!is_chain_plus(t, i, type))
            continue;
        int j = i;
        while (j != top && t->v[j].parent >= 0 &&
               is_chain_plus(t, t->v[j].parent, type))
            j = t->v[j].parent;
        if (j == top)
            pluses[(*nplus)++] = i;
    }
    return n;
}

/* v7/cc/c12.c's acommute() for an int '+' chain: insert() collects the
 * terms, placing each before the first one of strictly lower degree and
 * carrying the displaced one on the same way (so equal-degree terms can
 * change places), and the chain is rebuilt left-deep in that order -
 * the highest-degree term computed first, a plain variable added last:
 * 06_struct/03_starray.s.golden's "sum + pts[i].x + pts[i].y" -> "lea
 * di,<pts>" ... "mov di,(di)" / "lea si,<pts>" ... "add di,*2.(si)" /
 * "add di,*-20.(bp)". Applied only where it changes what is emitted: at
 * least three terms, two of them with code, none a constant (v7 would
 * fold those - a shape of its own) and none with a side effect (whose
 * order C leaves open, but mutos_c1 keeps as written). Returns the
 * number of terms and their new order in order[], or 0 when the chain
 * streams as written. */
static int acommute_order(const ETree *t, int top, int *order)
{
    int terms[ACOMMUTE_MAX], pluses[ACOMMUTE_MAX], nplus;
    int n = chain_terms(t, top, terms, pluses, &nplus);
    if (n < 3)
        return 0;
    int ncode = 0, nscaled = 0;
    for (int k = 0; k < n; k++) {
        const ENode *e = &t->v[terms[k]];
        if (e->op == OP_CON || !is_pure(t, terms[k]))
            return 0;
        if (e->kid[0] >= 0 &&
            !(e->op == OP_AMPER && t->v[e->kid[0]].op == OP_NAME))
            ncode++;
        if ((e->op == OP_TIMES || e->op == OP_LSHIFT) &&
            t->v[e->kid[1]].op == OP_CON)
            nscaled++;
    }
    /* Two constant multiples are distrib()'s to factor ("i*4 + j*2" ->
     * "(i*2 + j)*2") - a shape of its own, not this one. */
    if (ncode < 2 || nscaled > 1)
        return 0;
    int list[ACOMMUTE_MAX], nl = 0;
    for (int k = 0; k < n; k++) {
        int cur = terms[k];
        int d = enode_degree(t, cur);
        for (int j = 0; j < nl; j++) {
            int dj = enode_degree(t, list[j]);
            if (dj < d) {
                int tmp = list[j];
                list[j] = cur;
                cur = tmp;
                d = dj;
            }
        }
        list[nl++] = cur;
    }
    int same = 1;
    for (int k = 0; k < n; k++) {
        order[k] = list[k];
        if (list[k] != terms[k])
            same = 0;
    }
    return same ? 0 : n;
}

/* A chain of int '+' (three terms or more) of int calls and int
 * variables, a variable written ahead of a call: v7's acommute() puts the
 * calls (degree 10) ahead of the variables (0), in source order among
 * themselves, and rebuilds the chain left-deep - the calls summed first,
 * each right one pushed (is_callsum()), the variables added to AX after
 * them - fltprobe/p32_elem5.s.golden's "x = x + f(1) + g(2);" -> [g(2)] /
 * "push ax" / [f(1)] / "pop bx" / "add ax,bx" / "add ax,*-64.(bp)" / "mov
 * *-64.(bp),ax". Returns the number of calls (the terms' new order in
 * order[]: the calls, then the variables), or 0. */
static int is_callacom(const ETree *t, int top, int *order, int *nterms)
{
    const ENode *n = &t->v[top];
    if (n->op != OP_PLUS || !(n->type == TY_INT || n->type == TY_UNSIGN) ||
        (n->parent >= 0 && is_chain_plus(t, n->parent, n->type)))
        return 0;
    int terms[ACOMMUTE_MAX], pluses[ACOMMUTE_MAX], nplus;
    int nt = chain_terms(t, top, terms, pluses, &nplus);
    if (nt < 3 || nplus != nt - 1)
        return 0;
    int ncall = 0, seenvar = 0, moved = 0;
    for (int k = 0; k < nt; k++) {
        const ENode *e = &t->v[terms[k]];
        if (e->op == OP_CALL && (e->type == TY_INT || e->type == TY_UNSIGN)) {
            if (seenvar)
                moved = 1;
            ncall++;
        } else if (is_int_memvar(t, terms[k])) {
            seenvar = 1;
        } else {
            return 0;
        }
    }
    if (!moved)
        return 0;
    int j = 0;
    for (int k = 0; k < nt; k++)
        if (t->v[terms[k]].op == OP_CALL)
            order[j++] = terms[k];
    for (int k = 0; k < nt; k++)
        if (t->v[terms[k]].op != OP_CALL)
            order[j++] = terms[k];
    *nterms = nt;
    return ncall;
}

static int is_logic(const ETree *t, int i);

/* Node `i`'s value is a comparison's (or "&&"/"||"/"!"'s) 0/1, or one
 * scaled by a constant - computed with branches, so v7's dcalc() rates it
 * far beyond the registers left over: no "%n,e" template takes it as a
 * right operand. */
static int is_relval(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (find_relop(n->op) || is_logic(t, i) || n->op == OP_EXCLA)
        return 1;
    if ((n->op == OP_TIMES || n->op == OP_LSHIFT) && n->kid[1] >= 0 &&
        t->v[n->kid[1]].op == OP_CON)
        return is_relval(t, n->kid[0]);
    return 0;
}

/* An int '+' of two such values: v7's cr40 "%n,n" - the right operand
 * computed first and pushed (SS), the left one into the register (F),
 * the right one popped and added ("I (sp)+,R") - fltprobe/p23_elem3.s.
 * golden's "(a[i] < b[j]) + (a[i] > b[j]) * 2" -> [a[i] > b[j]'s 0/1]
 * / "sal di,*1" / "push di" / [a[i] < b[j]'s 0/1] / "pop bx" / "add
 * di,bx". (A comparison plus a variable is acommute()'s other order - see
 * OP_PLUS.) */
static int is_relsum(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    return n->op == OP_PLUS && (n->type == TY_INT || n->type == TY_UNSIGN) &&
           is_relval(t, n->kid[0]) && is_relval(t, n->kid[1]);
}

/* An int '+' of two call results: v7's cr40 "%n,n" again - a call's
 * value cannot wait in a register while another call is made, so the
 * right one is called first and its result pushed, the left one called,
 * and the pushed one popped into BX and added to AX, where the left
 * call left its value - fltprobe/p27_frame.s.golden's "f82() + f90() +
 * ... + f127()" -> "call _f127" / "push ax" / "call _f126" / "push ax" /
 * ... / "call _f82" / "pop bx" / "add ax,bx" (seven times): acommute()
 * keeps calls of equal degree in source order, and each link of the
 * left-deep chain pushes its right call first. The left operand is a
 * call or such a sum itself (acommute() puts a call ahead of anything
 * cheaper, so a call is a right operand only next to another one).
 *
 * A quotient plus a remainder the same way: neither waits in a register
 * while the other one's "idiv" runs, so the right one is computed first
 * and pushed from DX ("push dx" - its own register, v7's sptab "mov
 * R,-(sp)"), the left quotient computed into AX, popped and added -
 * fltprobe/p32_elem5.s.golden's "x = b[j] / b[i] + b[j] % b[i];" ->
 * [b[j] % b[i]: "idiv cx"] / "push dx" / [b[j] / b[i]: "idiv cx"] / "pop
 * bx" / "add ax,bx" / "mov *-64.(bp),ax". A quotient or a remainder next
 * to a call (either side) is inferred the same way; a product, a
 * remainder on the left or anything else has no golden. */

/* An int call or quotient - its value left in AX (see is_callsum()). */
static int is_hard_ax(const ENode *e)
{
    return (e->op == OP_CALL || e->op == OP_DIVIDE) &&
           (e->type == TY_INT || e->type == TY_UNSIGN);
}

static int is_callsum(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op != OP_PLUS || !(n->type == TY_INT || n->type == TY_UNSIGN))
        return 0;
    const ENode *r = &t->v[n->kid[1]];
    if (!(is_hard_ax(r) ||
          (r->op == OP_MOD && (r->type == TY_INT || r->type == TY_UNSIGN))))
        return 0;
    const ENode *l = &t->v[n->kid[0]];
    if (r->op != OP_CALL && l->op != OP_CALL && !is_callsum(t, n->kid[0]) &&
        !(l->op == OP_DIVIDE && (r->op == OP_DIVIDE || r->op == OP_MOD)))
        return 0;
    return is_hard_ax(l) || is_callsum(t, n->kid[0]);
}

/* An int '+' whose left operand leaves its value in AX - a product, a
 * quotient, a call - and whose right one is an element at a constant
 * offset (v7's "%n,ew*"): the left one goes into the working register
 * before the right one's address is computed into the next one, the
 * index through DX - fltprobe/p26_elem4.s.golden's "x = b[i] * b[j] +
 * b[j - 1];" -> [the product, "imul cx"] / "mov di,ax" / "lea si,*-28.
 * (bp)" / "mov dx,*-34.(bp)" / "sal dx,*1" / "add si,dx" / "add di,*-2.
 * (si)" (v7's F: the left operand into R, movreg() from where it was
 * computed; then S* into R+1). */
static int is_leftdi(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op != OP_PLUS || !(n->type == TY_INT || n->type == TY_UNSIGN))
        return 0;
    if (!is_elem_read(t, n->kid[1], 0) || is_elem_read(t, n->kid[1], 1))
        return 0;
    const ENode *l = &t->v[n->kid[0]];
    return (l->op == OP_TIMES || l->op == OP_DIVIDE || l->op == OP_CALL) &&
           (l->type == TY_INT || l->type == TY_UNSIGN);
}

/* v7/cc/c12.c's distrib(), for an int '+' chain whose terms are
 * comparisons (values computed with branches - is_relval()), two or more
 * of them multiplied by powers of two, and int variables - fltprobe/
 * p26_elem4.s.golden's "r = r + (b[i] < b[j]) + (b[i] > b[j]) * 2 + (b[i]
 * == b[j]) * 4;" ->
 *
 *     [b[i] < b[j]] / "push di" in both arms     C, pushed
 *     [b[i] > b[j]] / "push di" in both arms     B, pushed
 *     [b[i] == b[j]] / "sal di,*1"               A * 2
 *     "pop bx" / "add di,bx" / "sal di,*1"       (A*2 + B) * 2
 *     "pop bx" / "add di,bx"                     ... + C
 *     "add di,*-38.(bp)"                         ... + r
 *
 * acommute() collects the terms by degree (insert() - the comparisons
 * ahead of r, in source order: C, B*2, A*4, r), distrib() factors the
 * multiples - A*4 is divided by B*2's constant, "c1*y + c1*c2*x ->
 * c1*(y + c2*x)": C, (A*2 + B)*2, r - and the chain is rebuilt left-deep.
 * Then rcexpr()'s reorder() runs optim() over the tree again, and that
 * acommute() moves the factored term ahead of C: its inner sum of two
 * equal-degree terms has a degree one higher (the MUTOS degree - see
 * enode_degree()), and a TIMES by a power of two keeps its operand's.
 * Each '+' then takes v7's cr40 templates: a right operand computed with
 * branches goes first, onto the stack ("%n,n" - a comparison's 0/1 pushed
 * in each arm, cexpr()'s czero/cone through sptab), a variable is added
 * from memory ("%n,aw"); "* 2" is a shift (pow2()). Only that shape: an
 * expression with anything else in the chain is left to the other
 * orders (whose refusals stand). */
#define DN_MAX 64
enum { DN_TERM, DN_MUL, DN_ADD };
typedef struct {
    int  kind;
    int  node;        /* DN_TERM: the ETree node */
    int  a, b;        /* DN_MUL: a the operand; DN_ADD: a + b */
    long c;           /* DN_MUL: the constant */
} DNode;
typedef struct {
    DNode v[DN_MAX];
    int   n;
} DTree;

static int dn_new(DTree *d, int kind, int node, int a, int b, long c)
{
    if (d->n >= DN_MAX)
        gen_fatal("expression too large for distrib() (internal limit %d)",
                  DN_MAX);
    d->v[d->n] = (DNode){ kind, node, a, b, c };
    return d->n++;
}

static int dn_degree(const ETree *t, const DTree *d, int k)
{
    const DNode *x = &d->v[k];
    if (x->kind == DN_TERM)
        return enode_degree(t, x->node);
    if (x->kind == DN_MUL)
        return dn_degree(t, d, x->a);  /* v7's optim(): a TIMES by a power
                                         * of two has its operand's degree */
    int d1 = dn_degree(t, d, x->a), d2 = dn_degree(t, d, x->b);
    if (d1 < 1)
        d1 = 1;
    if (d2 < 0)
        d2 = 0;
    return d1 == d2 ? d1 + 1 : (d1 > d2 ? d1 : d2);
}

/* v7's insert(): before the first term of strictly lower degree, the
 * displaced one carried on the same way. */
static void dn_insert(const ETree *t, const DTree *d, int *list, int *nl,
                      int k)
{
    int dk = dn_degree(t, d, k);
    for (int i = 0; i < *nl; i++) {
        int di = dn_degree(t, d, list[i]);
        if (di < dk) {
            int tmp = list[i];
            list[i] = k;
            k = tmp;
            dk = di;
        }
    }
    if (*nl >= DN_MAX)
        gen_fatal("expression too large for distrib() (internal limit %d)",
                  DN_MAX);
    list[(*nl)++] = k;
}

static void dn_flatten(const DTree *d, int k, int *terms, int *nt)
{
    if (d->v[k].kind == DN_ADD) {
        dn_flatten(d, d->v[k].a, terms, nt);
        dn_flatten(d, d->v[k].b, terms, nt);
        return;
    }
    if (*nt >= DN_MAX)
        gen_fatal("expression too large for distrib() (internal limit %d)",
                  DN_MAX);
    terms[(*nt)++] = k;
}

static int dn_optim(const ETree *t, DTree *d, int k);

static void dn_squash(int *list, int *nl, int at)
{
    for (int i = at; i < *nl - 1; i++)
        list[i] = list[i + 1];
    (*nl)--;
}

/* v7's distrib() on an acommute() list - see above. */
static void dn_distrib(const ETree *t, DTree *d, int *list, int *nl)
{
    for (;;) {
        int dividend = -1, divisor = -1, ndmaj = 1000, merged = 0;
        for (int p1 = 0; p1 < *nl && !merged; p1++) {
            if (d->v[list[p1]].kind != DN_MUL)
                continue;
            int ndmin = 0, mindiv = -1, skip = 0;
            for (int p2 = 0; p2 < *nl; p2++) {
                if (p1 == p2 || d->v[list[p2]].kind != DN_MUL)
                    continue;
                long c1 = d->v[list[p1]].c, c2 = d->v[list[p2]].c;
                if (c1 == c2) {
                    /* "c*x + c*y" -> "c*(x + y)" */
                    int add = dn_new(d, DN_ADD, -1, d->v[list[p2]].a,
                                     d->v[list[p1]].a, 0);
                    d->v[list[p1]].a = add;
                    list[p1] = dn_optim(t, d, list[p1]);
                    dn_squash(list, nl, p2);
                    merged = 1;
                    break;
                }
                if (c2 % c1 == 0) {
                    skip = 1;
                    break;
                }
                if (c1 % c2 == 0) {
                    ndmin++;
                    mindiv = p2;
                }
            }
            if (merged || skip)
                continue;
            if (ndmin > 0 && ndmin < ndmaj) {
                ndmaj = ndmin;
                dividend = p1;
                divisor = mindiv;
            }
        }
        if (merged)
            continue;
        if (dividend < 0)
            return;
        int P1 = list[dividend], P2 = list[divisor];
        d->v[P1].c /= d->v[P2].c;
        int add = dn_new(d, DN_ADD, -1, P1, d->v[P2].a, 0);
        d->v[P2].a = add;
        int r = dn_optim(t, d, P2);
        if (dividend < divisor) {
            list[dividend] = r;
            dn_squash(list, nl, divisor);
        } else {
            list[divisor] = r;
            dn_squash(list, nl, dividend);
        }
    }
}

/* v7's optim() of node k as far as this shape goes: a sum is acommute()d
 * (its terms collected by degree, distrib()uted, rebuilt left-deep), a
 * multiple's operand optimized. */
static int dn_optim(const ETree *t, DTree *d, int k)
{
    if (d->v[k].kind == DN_MUL) {
        int a = dn_optim(t, d, d->v[k].a);
        d->v[k].a = a;
        return k;
    }
    if (d->v[k].kind != DN_ADD)
        return k;
    int terms[DN_MAX], nt = 0, list[DN_MAX], nl = 0;
    dn_flatten(d, k, terms, &nt);
    for (int j = 0; j < nt; j++)
        dn_insert(t, d, list, &nl, dn_optim(t, d, terms[j]));
    dn_distrib(t, d, list, &nl);
    int tree = list[0];
    for (int j = 1; j < nl; j++)
        tree = dn_new(d, DN_ADD, -1, tree, list[j], 0);
    return tree;
}

/* A constant multiple distrib() can factor: "x * c", c a power of two
 * above 1, x a comparison's value. */
static int is_scaled_relval(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op != OP_TIMES || !(n->type == TY_INT || n->type == TY_UNSIGN) ||
        t->v[n->kid[1]].op != OP_CON)
        return 0;
    long c = t->v[n->kid[1]].aux;
    return c > 1 && (c & (c - 1)) == 0 && is_relval(t, n->kid[0]);
}

static int is_distrib(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (n->op != OP_PLUS || !(n->type == TY_INT || n->type == TY_UNSIGN) ||
        (n->parent >= 0 && is_chain_plus(t, n->parent, n->type)))
        return 0;
    int terms[ACOMMUTE_MAX], pluses[ACOMMUTE_MAX], nplus;
    int nt = chain_terms(t, i, terms, pluses, &nplus);
    if (nt < 2 || nplus != nt - 1)
        return 0;
    int nscaled = 0;
    for (int k = 0; k < nt; k++) {
        if (is_scaled_relval(t, terms[k]))
            nscaled++;
        else if (!is_relval(t, terms[k]) && !is_int_memvar(t, terms[k]))
            return 0;
    }
    return nscaled >= 2;
}

static void plan_value(Plan *p, const ETree *t, int i);
static void plan_op(Plan *p, SegKind kind, int a, int b, int c);
static void plan_range(Plan *p, long start, long end);

/* Emits the plan for DNode k - see is_distrib(); *np counts the chain's
 * PLUS opcodes used so far (pluses[], innermost first: the last one
 * emitted is the outermost). */
static void plan_dnode(Plan *p, const ETree *t, const DTree *d, int k,
                       const int *pluses, int *np)
{
    const DNode *x = &d->v[k];
    if (x->kind == DN_TERM) {
        plan_value(p, t, x->node);
        return;
    }
    if (x->kind == DN_MUL) {
        int sh = 0;
        for (long c = x->c; c > 1; c >>= 1)
            sh++;
        plan_dnode(p, t, d, x->a, pluses, np);
        plan_op(p, SEG_SHIFT, sh, 0, 0);
        return;
    }
    /* The right operand goes first, onto the machine stack ("%n,n"),
     * unless it is a leaf or a comparison simple enough to be computed
     * into the next register after the left one ("%n,e": F, S1, "add
     * R1,R" - v7's dcalc() takes it as "e" when its degree fits in the
     * registers left): a variable compared with a constant has degree 1
     * - fltprobe/p32_elem5.s.golden's "(x > 2) * 2 + (x < 9) * 4 + (x ==
     * 5) + x" -> [x < 9] into DI / "sal di,*1" / [x > 2] into SI / "add
     * di,si" / "sal di,*1" / [x == 5] into SI / "add di,si" / "add di,x";
     * two elements compared, degree 3, are pushed (p26_elem4's sum). */
    const DNode *r = &d->v[x->b];
    int hard = !(r->kind == DN_TERM &&
                 (t->v[r->node].kid[0] < 0 ||
                  (find_relop(t->v[r->node].op) &&
                   enode_degree(t, r->node) <= 2)));
    if (hard) {
        plan_dnode(p, t, d, x->b, pluses, np);
        plan_op(p, SEG_SPILL, 0, 0, 0);
        plan_dnode(p, t, d, x->a, pluses, np);
        plan_op(p, SEG_SWAP, 0, 0, 0);
    } else {
        plan_dnode(p, t, d, x->a, pluses, np);
        plan_dnode(p, t, d, x->b, pluses, np);
    }
    const ENode *pl = &t->v[pluses[(*np)++]];
    plan_range(p, pl->off, pl->end);
}

static void plan_distrib(Plan *p, const ETree *t, int i)
{
    int terms[ACOMMUTE_MAX], pluses[ACOMMUTE_MAX], nplus;
    int nt = chain_terms(t, i, terms, pluses, &nplus);
    DTree d;
    d.n = 0;
    int tree = -1;
    for (int k = 0; k < nt; k++) {
        const ENode *e = &t->v[terms[k]];
        int x;
        if (is_scaled_relval(t, terms[k]))
            x = dn_new(&d, DN_MUL, -1,
                       dn_new(&d, DN_TERM, e->kid[0], -1, -1, 0), -1,
                       t->v[e->kid[1]].aux);
        else
            x = dn_new(&d, DN_TERM, terms[k], -1, -1, 0);
        tree = (tree < 0) ? x : dn_new(&d, DN_ADD, -1, tree, x, 0);
    }
    tree = dn_optim(t, &d, tree);   /* optim() as the tree is read */
    tree = dn_optim(t, &d, tree);   /* rcexpr()'s reorder(): optim() again */
    int np = 0;
    plan_dnode(p, t, &d, tree, pluses, &np);
    if (np != nplus)
        gen_fatal("internal: distrib() changed the number of additions");
}

/* The int compound assignments that combine in place ('+=', '-=', '&=',
 * '|=', '^=' - one instruction with the target as its memory operand). */
static int is_inplace_asop(int op)
{
    return op == OP_ASPLUS || op == OP_ASMINUS || op == OP_ASSAND ||
           op == OP_ASOR || op == OP_ASXOR;
}

/* Such an assignment into an element read through a computed address at
 * offset 0, its right-hand side not a constant: the target's address is
 * computed first and pushed, the right-hand side computed into DI, the
 * address popped into BX - fltprobe/p26_elem4.s.golden's "b[i] += b[j];"
 * -> "lea di,*-28.(bp)" / "mov si,*-32.(bp)" / "sal si,*1" / "add di,si" /
 * "push di" / [b[j]] / "mov di,(di)" / "pop bx" / "add (bx),di", "b[j] -=
 * x;" -> ... "push di" / "mov di,*-36.(bp)" / "pop bx" / "sub (bx),di"
 * (the PDP-11's "%n*,n" for an assignment operator: SS*, F, "I R,*(sp)+"
 * - the store's VK_IND_PENDING shape, see is_disp_store()). A constant
 * right-hand side goes straight through the address's register, as
 * through a pointer variable ("*ip += 2;" -> "mov di,*-30.(bp)" / "add
 * (di),*2."). */
static int is_aspush(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    return is_inplace_asop(n->op) && (n->type == TY_INT || n->type == TY_UNSIGN) &&
           is_elem_read(t, n->kid[0], 1) && t->v[n->kid[1]].op != OP_CON;
}

/* Such an assignment through "pointer + non-zero constant" - a member of
 * an element - its right-hand side not a constant: the right-hand side
 * first, into DI, then the address into SI (the index through DX), and the
 * operation in place - p26_elem4.s.golden's "ps[i].c += x;" -> "mov
 * di,*-36.(bp)" / "lea si,*-20.(bp)" / "mov dx,*-32.(bp)" / "mov cx,*3." /
 * "sal dx,cl" / "add si,dx" / "add *4.(si),di" - is_disp_store()'s order
 * for a store. */
static int is_asdisp(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (!is_inplace_asop(n->op) || !(n->type == TY_INT || n->type == TY_UNSIGN))
        return 0;
    const ENode *lhs = &t->v[n->kid[0]];
    if (lhs->op != OP_STAR || !ty_is_word(lhs->type) ||
        t->v[n->kid[1]].op == OP_CON)
        return 0;
    long off;
    int base = ptr_fold(t, lhs->kid[0], &off);
    return off != 0 && t->v[base].op != OP_AMPER;
}

/* Such an assignment through a pointer variable ("*ip += x"), its
 * right-hand side not a constant: the pointer pushed straight from memory
 * first, the right-hand side computed into DI, the pointer popped into BX
 * and the operation in place - fltprobe/p32_elem5.s.golden's "*ip += x;"
 * -> "push *-58.(bp)" / "mov di,*-64.(bp)" / "pop bx" / "add (bx),di"
 * (the PDP-11's "%n*,n" for an assignment operator: FS*, S, "I R,*(sp)+"
 * - SS* of a pointer variable is the variable pushed). A constant goes
 * through DI ("*ip += 2;" -> "mov di,ip" / "add (di),*2." - p26). */
static int is_asptr(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (!is_inplace_asop(n->op) || !(n->type == TY_INT || n->type == TY_UNSIGN))
        return 0;
    const ENode *lhs = &t->v[n->kid[0]];
    if (lhs->op != OP_STAR || !ty_is_word(lhs->type) ||
        t->v[n->kid[1]].op == OP_CON)
        return 0;
    long off;
    int base = ptr_fold(t, lhs->kid[0], &off);
    return off == 0 && is_ptr_var(t, base);
}

/* The evaluation order of node `i` - see EvalOrder. */
static EvalOrder eval_order(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (order_right_first(t, i) || is_relsum(t, i) || is_callsum(t, i))
        return ORD_SPILL;
    if (is_disp_store(t, i))
        return ORD_DISPSTORE;
    if (is_deferred_ptr(t, i))
        return ORD_DEFPTR;
    if (is_pushaddr(t, i))
        return ORD_PUSHADDR;
    if (is_pushleft(t, i))
        return ORD_PUSHLEFT;
    if (is_leftdi(t, i))
        return ORD_LEFTDI;
    if (is_distrib(t, i))
        return ORD_DISTRIB;
    if (is_aspush(t, i))
        return ORD_ASPUSH;
    if (is_asdisp(t, i))
        return ORD_ASDISP;
    if (is_asptr(t, i))
        return ORD_ASPTR;
    {
        int order[ACOMMUTE_MAX], nt;
        if (is_callacom(t, i, order, &nt) > 0)
            return ORD_CALLACOM;
    }
    if (n->op == OP_PLUS && (n->type == TY_INT || n->type == TY_UNSIGN) &&
        !(n->parent >= 0 && is_chain_plus(t, n->parent, n->type))) {
        int order[ACOMMUTE_MAX];
        if (acommute_order(t, i, order) > 0)
            return ORD_ACOMMUTE;
    }
    return ORD_POSTFIX;
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
    /* `start`: just past the CALL's own opcode - where its consumer is
     * read (a floating result - see SEG_CALL) */
    plan_add(p, (Seg){ SEG_CALL, n->end, 0, words, n->type, 0 });
}

static void plan_quest(Plan *p, const ETree *t, int i);
static void plan_fvalue(Plan *p, const ETree *t, int i); /* see        */
static void gen_fp_incdec(GenState *g, FILE *t1, int op);
static int is_fnode(const ETree *t, int i);             /* "Floating  */
static Val val_facc(void);                              /* point"     */
static void gen_ltof(GenState *g, int type);

/* A "?:" - v7's cexpr(): cbranch(tr1, c=isn++, 0), the true arm, "jbr
 * r=isn++", "c:", the false arm, "r:". Each arm is loaded where the value
 * goes - the segments' `start`: 0 DI (an int); 1 DI:SI (typed LONG, two
 * long arms - mutos_c0): fltprobe/p22_long2.s.golden's "l = x ? l : m" ->
 * "cmp *-16.(bp),*0" / "beq L10011" / "mov si,*-6.(bp)" / "mov di,*-8.
 * (bp)" / "jmp L10012" / "L10011:mov si,*-10.(bp)" / "mov di,*-12.(bp)" /
 * "L10012:"; 2 the floating-point stack (typed DOUBLE): p21_fltexp.s.
 * golden's "d = x ? d : e" -> "cmp *-44.(bp),*0" / "beq L10009" / "lea
 * ax,*-12.(bp)" / "call fldd" / "jmp L10010" / "L10009:lea ax,*-20.(bp)"
 * / "call fldd" / "L10010:". */
static void plan_quest(Plan *p, const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    const ENode *colon = &t->v[n->kid[1]];
    if (colon->op != OP_COLON)
        gen_fatal("internal: QUEST without a preceding COLON pair");
    int how = 0;
    if (n->type == TY_LONG && colon->type == TY_LONG)
        how = 1;
    else if (n->type == TY_DOUBLE && colon->type == TY_DOUBLE)
        how = 2;
    if (!how) {
        check_int_node(colon);
        check_int_node(n);
    }
    int lfalse = plan_slots(p, 1);
    int lend = plan_slots(p, 1);
    int rec = plan_slots(p, 2);
    plan_op(p, SEG_ALLOC, lfalse, 0, 0);
    plan_cbranch(p, t, n->kid[0], lfalse, 0);
    plan_op(p, SEG_MARK, rec, 0, 0);
    plan_value(p, t, colon->kid[0]);
    plan_add(p, (Seg){ SEG_QTRUE, how, 0, lfalse, lend, rec });
    plan_op(p, SEG_MARK, rec, 0, 0);
    plan_value(p, t, colon->kid[1]);
    plan_add(p, (Seg){ SEG_QFALSE, how, 0, 0, lend, rec });
}

/* Node i's value onto the value stack. */
static void plan_value(Plan *p, const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (is_fnode(t, i)) {
        plan_fvalue(p, t, i);
        return;
    }
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
        plan_quest(p, t, i);
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
    if (n->order == ORD_ACOMMUTE) {
        /* The chain rebuilt left-deep in acommute_order()'s order; each
         * '+' step replays one of the chain's own PLUS opcodes (all the
         * same opcode and type) - the outermost one last, so the step
         * whose handler looks past the chain sees what really follows. */
        int order[ACOMMUTE_MAX], terms[ACOMMUTE_MAX], pluses[ACOMMUTE_MAX];
        int nplus;
        int nt = acommute_order(t, i, order);
        (void)chain_terms(t, i, terms, pluses, &nplus);
        if (nt < 2 || nplus != nt - 1)
            gen_fatal("internal: an acommute() chain changed between "
                      "planning steps");
        int k0 = 1;
        if (is_elem_read(t, order[0], 0) && is_elem_read(t, order[1], 1)) {
            /* The first link's right term an element at offset 0: its
             * address first, pushed - as for a lone '+' (is_pushaddr()). */
            plan_value(p, t, order[1]);
            plan_op(p, SEG_ADDRPUSH, 0, 0, 0);
            plan_value(p, t, order[0]);
            plan_op(p, SEG_DEFPOP, 0, 0, 0);
            plan_range(p, t->v[pluses[0]].off, t->v[pluses[0]].end);
            k0 = 2;
        } else {
            plan_value(p, t, order[0]);
        }
        for (int k = k0; k < nt; k++) {
            /* A dereference about to be added to something with code
             * of its own is loaded first - its register is needed
             * (v7's template computes the left operand into its
             * register before the right one: "mov di,(di)" ahead of
             * "lea si,<pts>" in 03_starray.s.golden). */
            if (t->v[order[k]].kid[0] >= 0)
                plan_op(p, SEG_LOADIND, 0, 0, 0);
            plan_value(p, t, order[k]);
            const ENode *pl = &t->v[pluses[k - 1]];
            plan_range(p, pl->off, pl->end);
        }
        return;
    }
    if (n->order == ORD_DISPSTORE) {
        plan_value(p, t, n->kid[1]);
        plan_op(p, SEG_RHSREG, 0, 0, 0);
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_SWAP2, 0, 0, 0);
        plan_range(p, n->off, n->end);
        return;
    }
    if (n->order == ORD_PUSHADDR) {
        /* The right element's address (SEG_ADDRPUSH), then the left
         * operand; SEG_DEFPOP loads it, pops the address into BX and
         * leaves "(bx)" as the right operand. */
        plan_value(p, t, n->kid[1]);
        plan_op(p, SEG_ADDRPUSH, 0, 0, 0);
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_DEFPOP, 0, 0, 0);
        plan_range(p, n->off, n->end);
        return;
    }
    if (n->order == ORD_PUSHLEFT) {
        /* The left element's address (SEG_ADDRPUSH), then the right
         * operand; SEG_DEFPOPL loads it, pops the address into BX and
         * leaves "(bx)" as the left operand. */
        plan_op(p, SEG_KEEPIND, 0, 0, 0);
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_ADDRPUSH, 0, 0, 0);
        plan_value(p, t, n->kid[1]);
        plan_op(p, SEG_DEFPOPL, 0, 0, 0);
        plan_range(p, n->off, n->end);
        return;
    }
    if (n->order == ORD_DEFPTR) {
        /* The pointer NAME's own range pushes its value; SEG_DEFPUSH
         * puts it on the machine stack. The rest of the right operand
         * (its "+0"s and "&*" pairs, and the STAR) is never streamed:
         * SEG_DEFPOP stands for all of it. */
        long off;
        int base = ptr_fold(t, t->v[n->kid[1]].kid[0], &off);
        plan_range(p, t->v[base].start, t->v[base].end);
        plan_op(p, SEG_DEFPUSH, 0, 0, 0);
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_DEFPOP, 0, 0, 0);
        plan_range(p, n->off, n->end);
        return;
    }
    if (n->order == ORD_DISTRIB) {
        plan_distrib(p, t, i);
        return;
    }
    if (n->order == ORD_ASPUSH) {
        plan_op(p, SEG_KEEPIND, 0, 0, 0);
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_ADDRPUSH, 0, 0, 0);
        plan_value(p, t, n->kid[1]);
        plan_op(p, SEG_ASPOP, 0, 0, 0);
        plan_range(p, n->off, n->end);
        return;
    }
    if (n->order == ORD_CALLACOM) {
        /* See is_callacom(): the calls' sum as is_callsum()'s - the last
         * call computed first and pushed, ..., the first one, each popped
         * and added - then each variable added. The chain's own PLUS
         * opcodes are replayed innermost first, as for ORD_ACOMMUTE. */
        int order[ACOMMUTE_MAX], terms[ACOMMUTE_MAX], pluses[ACOMMUTE_MAX];
        int nt, nplus;
        int nc = is_callacom(t, i, order, &nt);
        (void)chain_terms(t, i, terms, pluses, &nplus);
        if (nc < 1 || nplus != nt - 1)
            gen_fatal("internal: a chain of calls and variables changed "
                      "between planning steps");
        for (int k = nc - 1; k >= 1; k--) {
            plan_value(p, t, order[k]);
            plan_op(p, SEG_SPILL, 0, 0, 0);
        }
        plan_value(p, t, order[0]);
        for (int k = 1; k < nc; k++) {
            plan_op(p, SEG_SWAP, 0, 0, 0);
            const ENode *pl = &t->v[pluses[k - 1]];
            plan_range(p, pl->off, pl->end);
        }
        for (int k = nc; k < nt; k++) {
            plan_value(p, t, order[k]);
            const ENode *pl = &t->v[pluses[k - 1]];
            plan_range(p, pl->off, pl->end);
        }
        return;
    }
    if (n->order == ORD_ASPTR) {
        /* The pointer NAME's range, SEG_DEFPUSH; the STAR (and any "+0"
         * or "&*") is never streamed - SEG_ASPOP's "(bx)" stands for it. */
        long off;
        int base = ptr_fold(t, t->v[n->kid[0]].kid[0], &off);
        plan_range(p, t->v[base].start, t->v[base].end);
        plan_op(p, SEG_DEFPUSH, 0, 0, 0);
        plan_value(p, t, n->kid[1]);
        plan_op(p, SEG_ASPOP, 0, 0, 0);
        plan_range(p, n->off, n->end);
        return;
    }
    if (n->order == ORD_ASDISP) {
        plan_value(p, t, n->kid[1]);
        plan_op(p, SEG_RHSREG, 0, 0, 0);
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_SWAP2, 0, 0, 0);
        plan_range(p, n->off, n->end);
        return;
    }
    if (n->order == ORD_LEFTDI) {
        plan_value(p, t, n->kid[0]);
        plan_op(p, SEG_TODI, 0, 0, 0);
        plan_value(p, t, n->kid[1]);
        plan_range(p, n->off, n->end);
        return;
    }
    if (n->order == ORD_SPILL) {
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
static void fplan_constants(GenState *g, FILE *t1, const ETree *t);

static int plan_expression(GenState *g, FILE *t1)
{
    long pos = ftell(t1);
    if (pos < 0)
        gen_fatal("internal: temp1 is not seekable (ftell failed)");
    int op = c1_read_op(t1, "temp1");
    if (fseek(t1, pos, SEEK_SET) != 0)
        gen_fatal("internal: temp1 is not seekable (fseek failed)");
    g->plan.nfc = 0;
    /* The floating-point stack model where this expression starts (see
     * fp_track() and OP_EXPR's check). */
    g->fstart = g->fdepth;
    g->stmt_argpops = 0;
    g->nargpop = 0;
    if (op != OP_NAME && op != OP_CON && op != OP_LCON && op != OP_FCON)
        return 0;

    ETree t = { NULL, 0, 0 };
    Term term = { 0, 0, 0, 0, 0, 0 };
    int root = prescan_expr(t1, &t, &term);
    if (root >= 0) {
        g->cur_line = term.line;        /* see c1_error() */
        /* Which floating assignments passed as a call argument the real
         * compiler stores with a pop (see fp_store()): those of a
         * floating call whose value goes nowhere - the statement's root,
         * "half(d = 3.0);" - or straight into the statement's own
         * floating store ("f = half(d = 3.0);"). */
        int call = -1;
        const ENode *rn = &t.v[root];
        if (rn->op == OP_CALL)
            call = root;
        else if (rn->op == OP_ASSIGN && rn->kid[1] >= 0 &&
                 (rn->type == TY_FLOAT || rn->type == TY_DOUBLE) &&
                 t.v[rn->kid[1]].op == OP_CALL)
            call = rn->kid[1];
        if (call >= 0 && (t.v[call].type == TY_FLOAT ||
                          t.v[call].type == TY_DOUBLE)) {
            for (int i = 0; i < t.n; i++) {
                const ENode *a = &t.v[i];
                if (a->op != OP_ASSIGN ||
                    !(a->type == TY_FLOAT || a->type == TY_DOUBLE))
                    continue;
                /* an argument: up through the argument list's COMMAs to
                 * the call, as its right operand */
                int k = i, p = a->parent;
                while (p >= 0 && t.v[p].op == OP_COMMA)
                    k = p, p = t.v[p].parent;
                if (p == call && t.v[call].kid[1] == k &&
                    g->nargpop < (int)(sizeof g->argpop_end /
                                       sizeof g->argpop_end[0]))
                    g->argpop_end[g->nargpop++] = a->end;
            }
        }
    }
    /* Which ITOLs widen an unsigned value (see OP_ITOL) - all it
     * indexed, as far as it got. */
    g->nitolu = 0;
    for (int i = 0; i < t.n; i++)
        if (t.v[i].op == OP_ITOL && t.v[i].kid[0] >= 0 &&
            t.v[t.v[i].kid[0]].type == TY_UNSIGN &&
            g->nitolu < (int)(sizeof g->itolu / sizeof g->itolu[0]))
            g->itolu[g->nitolu++] = t.v[i].off;
    /* A 'long' value's type reaches no Val (a long variable is a VK_MEM
     * like an int one), so what the comparisons and truth tests below
     * need to know is taken from the pre-scan: which comparisons have a
     * long operand (OP_LESS... - two long variables compared as words
     * was silently wrong until 2026-10-02: "l > m" compared the high
     * words only), and whether a long is tested for truth anywhere -
     * "if (l)", "!l", "l && i", "l ? a : b" tested its high word only,
     * refused here (v7's longrel() tests both words, "tst" - no golden). */
    g->nlrel = 0;
    g->nzelem = 0;
    g->ntelem = 0;
    g->nltest = 0;
    g->nitollow = 0;
    for (int i = 0; root >= 0 && i < t.n; i++) {
        /* An int value widened only to have a long constant added or
         * subtracted and the long truncated again - "(int) (i + 100000L)",
         * fltprobe/p21_fltexp.1.golden's "r = r + u - 39990" (NAME r, NAME
         * u, PLUS(7), ITOL, LCON, MINUS(6), LTOI(0)): v7's unoptim()
         * distributes the LTOI and cancels LTOI(ITOL(x)), so the real
         * compiler computes it in one word - "mov di,*-46.(bp)" / "add
         * di,*-38.(bp)" / "add di,#25546.". */
        const ENode *n = &t.v[i];
        if (n->op != OP_ITOL || n->parent < 0 || n->kid[0] < 0 ||
            t.v[n->kid[0]].op == OP_CON)
            continue;
        const ENode *pn = &t.v[n->parent];
        if ((pn->op != OP_PLUS && pn->op != OP_MINUS) || pn->type != TY_LONG ||
            pn->kid[0] != i || pn->parent < 0 ||
            t.v[pn->parent].op != OP_LTOI)
            continue;
        const ENode *c = &t.v[pn->kid[1]];
        if (!(c->op == OP_LCON || (c->op == OP_ITOL && c->kid[0] >= 0 &&
                                   t.v[c->kid[0]].op == OP_CON)))
            continue;
        if (g->nitollow < (int)(sizeof g->itollow / sizeof g->itollow[0]))
            g->itollow[g->nitollow++] = n->off;
    }
    for (int i = 0; root >= 0 && i < t.n; i++) {
        const ENode *n = &t.v[i];
        int k0 = n->kid[0], k1 = n->kid[1];
        /* An element read through a computed address tested for truth -
         * the condition itself, an operand of '!', '&&', '||' or a '?:''s
         * condition: loaded and tested like one compared with 0 -
         * fltprobe/p26_elem4.s.golden's "if (b[i])" -> "lea di,*-28.(bp)" /
         * ... / "add di,si" / "mov di,(di)" / "or di,di" / "beq L4", "if
         * (ps[i].c)" -> ... / "mov di,*4.(di)" / "or di,di" (with its offset
         * too). See Val's ortest. */
        if (is_elem_read(&t, i, 0) && g->ntelem < (int)(sizeof g->telem /
                                                       sizeof g->telem[0])) {
            int p = n->parent;
            if ((p < 0 && term.op == OP_CBRANCH) ||
                (p >= 0 && (t.v[p].op == OP_EXCLA || t.v[p].op == OP_LOGAND ||
                            t.v[p].op == OP_LOGOR ||
                            (t.v[p].op == OP_QUEST && t.v[p].kid[0] == i))))
                g->telem[g->ntelem++] = n->off;
        }
        /* An element read through a computed address compared with 0 -
         * see Val's cond_ortest. */
        if (find_relop(n->op) && k0 >= 0 && k1 >= 0 &&
            ((t.v[k1].op == OP_CON && t.v[k1].aux == 0 &&
              is_elem_read(&t, k0, 0)) ||
             (t.v[k0].op == OP_CON && t.v[k0].aux == 0 &&
              is_elem_read(&t, k1, 0))) &&
            g->nzelem < (int)(sizeof g->zelem / sizeof g->zelem[0]))
            g->zelem[g->nzelem++] = n->off;
        if (find_relop(n->op) && k0 >= 0 && k1 >= 0 &&
            (t.v[k0].type == TY_LONG || t.v[k1].type == TY_LONG) &&
            g->nlrel < (int)(sizeof g->lrel / sizeof g->lrel[0]))
            g->lrel[g->nlrel++] = n->off;
        int cand[3], nc = 0;
        if (n->op == OP_LOGAND || n->op == OP_LOGOR) {
            if (k0 >= 0 && t.v[k0].type == TY_LONG)
                cand[nc++] = k0;
            if (k1 >= 0 && t.v[k1].type == TY_LONG)
                cand[nc++] = k1;
        } else if (n->op == OP_EXCLA || n->op == OP_QUEST) {
            if (k0 >= 0 && t.v[k0].type == TY_LONG)
                cand[nc++] = k0;
        }
        if (i == root && term.op == OP_CBRANCH && n->type == TY_LONG)
            cand[nc++] = i;
        int tested = -1;
        for (int c = 0; c < nc; c++) {
            int x = cand[c];
            if (t.v[x].op != OP_NAME || t.v[x].aux != SC_AUTO) {
                tested = x;
                continue;
            }
            /* A long variable tested for truth through v7's cbranch():
             * a CBRANCH's condition itself, or under a chain of '!' - "if
             * (l)", "if (!l)" - an operand of '&&'/'||', or a '?:''s
             * condition: both words tested (gen_cond_branch()) -
             * fltprobe/p25_long3.s.golden's "if (l && i)", "if (l ||
             * j)", "r + (l ? 2 : 50)". */
            int j = x;
            while (t.v[j].parent >= 0 && t.v[t.v[j].parent].op == OP_EXCLA)
                j = t.v[j].parent;
            int pj = t.v[j].parent;
            /* ... or under '!' as a value: v7's cexpr() compiles a
             * logical value through cbranch() too - fltprobe/p31_long4.s.
             * golden's "x = !l;" -> "mov si,*-6.(bp)" / "mov di,*-8.(bp)"
             * / "cmp di,*0" / "bne L10006" / "cmp si,*0" / "beq L10005" /
             * "L10006:mov di,*0." / "jmp L10007" / "L10005:mov di,*1." /
             * "L10007:". */
            int ok = (j == root && term.op == OP_CBRANCH) ||
                     t.v[j].op == OP_EXCLA ||
                     (pj >= 0 && (t.v[pj].op == OP_LOGAND ||
                                  t.v[pj].op == OP_LOGOR ||
                                  (t.v[pj].op == OP_QUEST &&
                                   t.v[pj].kid[0] == j)));
            if (!ok) {
                tested = x;
                continue;
            }
            int dup = 0;
            for (int k = 0; k < g->nltest; k++)
                if (g->ltests[k] == t.v[x].off)
                    dup = 1;
            if (!dup && g->nltest < (int)(sizeof g->ltests /
                                          sizeof g->ltests[0]))
                g->ltests[g->nltest++] = t.v[x].off;
            else if (!dup)
                tested = x;
        }
        if (tested >= 0) {
            free(t.v);
            gen_fatal("a 'long' value tested for truth ('if (l)', '!l', 'l "
                      "&& ...', 'l ? ...') is not yet supported - compare it "
                      "with 0L - see src/mutos_cc/README.md");
        }
    }
    /* An expression with a floating value in it is always planned: its
     * operands' order is v7's, decided on the tree (see "Floating
     * point"), never temp1's postfix order - so an expression that cannot
     * be pre-scanned is refused. Its constants get their labels now, as
     * v7's c1 numbers them while reading the tree. */
    int has_float = (op == OP_FCON);
    for (int i = 0; i < t.n; i++)       /* all it indexed, even if it stopped */
        if (is_fnode(&t, i))
            has_float = 1;
    if (has_float && root < 0) {
        free(t.v);
        gen_fatal("a 'float'/'double' expression with an operator mutos_c1 "
                  "cannot pre-scan is not yet supported - see "
                  "src/mutos_cc/README.md");
    }
    if (has_float)
        fplan_constants(g, t1, &t);
    /* Postfix order: every node's operands come before it, so one
     * forward pass sees a node's children fully marked. */
    for (int i = 0; root >= 0 && i < t.n; i++) {
        ENode *n = &t.v[i];
        n->order = eval_order(&t, i);
        n->planned = n->order != ORD_POSTFIX || is_control(n->op) ||
                     call_needs_plan(&t, i) || is_fnode(&t, i);
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
    if (g->valsp >= 1 && g->valstack[g->valsp - 1].kind == VK_COND &&
        !g->valstack[g->valsp - 1].cond_is_float &&
        !g->valstack[g->valsp - 1].cond_is_long) {
        /* A comparison's value goes onto the stack from each arm: v7's
         * cexpr() for a relational in sptab - cbranch(), then czero and
         * cone each through sptab - fltprobe/p26_elem4.s.golden's "cmp
         * (bx),di" / "blt L10002" / "mov di,*0." / "push di" / "jmp L10003"
         * / "L10002:mov di,*1." / "push di" / "L10003:". */
        Val c = pop_val(g);
        int ltrue = g->next_lab++, lend = g->next_lab++;
        emit_cmp_and_branch(g, c, c.true_op, ltrue);
        ins2(g, "mov", o_reg("di"), o_imm(0));
        ins1(g, "push", o_reg("di"));
        ins1(g, "jmp", o_lab(lend));
        put_label(g, ltrue);
        ins2(g, "mov", o_reg("di"), o_imm(1));
        ins1(g, "push", o_reg("di"));
        put_label(g, lend);
        Val s = {0};
        s.kind = VK_STACKED;
        push_val(g, s);
        return;
    }
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

/* SEG_SWAP2: an assignment target generated after its right-hand side
 * (is_disp_store()) goes back below it, where OP_ASSIGN pops it. */
static void plan_swap2(GenState *g)
{
    if (g->valsp < 2)
        gen_fatal("expression stack underflow - malformed temp1 stream");
    Val tmp = g->valstack[g->valsp - 1];
    g->valstack[g->valsp - 1] = g->valstack[g->valsp - 2];
    g->valstack[g->valsp - 2] = tmp;
}

/* SEG_RHSREG: the right-hand side of a store through "pointer +
 * constant", computed first (is_disp_store()), is put into a register
 * before the target's own code runs - the "S" of v7's template, which
 * computes into R: a dereference in place ("mov di,*2.(di)"), a char
 * through AX (load_charx()), anything in memory or any address into DI
 * (10_integ/03_linklist.s.golden's "mov di,*-6.(bp)" for "cur->next =
 * head;"), a value already in a register left there. */
static void plan_rhsreg(GenState *g)
{
    Val v = materialize(g, pop_val_ex(g, POP_CHARX));
    switch (v.kind) {
    case VK_CHARX:
        v = load_charx(g, v);
        break;
    case VK_IND:
        ins2(g, "mov", o_reg(v.reg), o_val(v));
        v = val_reg(v.reg);
        break;
    case VK_REG:
        break;
    case VK_MEM: case VK_STATIC: case VK_MEM_CVT: case VK_MEM_DIRECT:
    case VK_IMM:
        load_into_di(g, v);
        v = val_reg("di");
        break;
    default:
        gen_fatal("storing this kind of value through a pointer plus a "
                  "constant offset is not yet supported - see "
                  "src/mutos_cc/README.md");
    }
    push_val(g, v);
}

/* SEG_ASPOP: see is_aspush() - the right-hand side into a register (a
 * dereference in place, a variable into DI), the target's address popped
 * into BX, the target "(bx)" below it. */
static void plan_aspop(GenState *g)
{
    if (g->valsp < 2 || g->valstack[g->valsp - 2].kind != VK_STACKED)
        gen_fatal("internal: a compound assignment's pushed target address "
                  "is missing from below the value stack's top");
    Val r = materialize(g, pop_val(g));
    g->valsp--;                          /* the VK_STACKED marker */
    if (r.kind == VK_IND && !r.bytev && !r.sym) {
        ins2(g, "mov", o_reg(r.reg), o_val(r));
        r = val_reg(r.reg);
    } else if (r.kind == VK_MEM || r.kind == VK_STATIC ||
               r.kind == VK_MEM_CVT || r.kind == VK_IMM) {
        if (di_busy(g))
            gen_fatal("internal: DI busy for a compound assignment's "
                      "right-hand side");
        load_into_di(g, r);
        r = val_reg("di");
    }
    if (r.kind != VK_REG || r.regvar)
        gen_fatal("a compound assignment into an element with this right-hand "
                  "side is not yet supported - see src/mutos_cc/README.md");
    require_free(r, RB_BX, "a compound assignment into an element");
    ins1(g, "pop", o_reg("bx"));
    push_val(g, val_ind("bx"));
    push_val(g, r);
}

/* SEG_SHIFT: see is_distrib() - a comparison's 0/1, or a sum computed in
 * DI, shifted left: "sal di,*1". */
static void plan_shift(GenState *g, int count)
{
    Val v = materialize(g, pop_val(g));
    if (v.kind != VK_REG || v.regvar || strcmp(v.reg, "di") != 0)
        gen_fatal("internal: a constant multiple distrib() rebuilt is not in "
                  "DI");
    emit_const_shift(g, "sal", "di", count);
    push_val(g, val_reg("di"));
}

/* SEG_TODI: see is_leftdi(). */
static void plan_todi(GenState *g)
{
    if (g->valsp < 1)
        gen_fatal("expression stack underflow - malformed temp1 stream");
    Val v = g->valstack[g->valsp - 1];
    if (v.kind == VK_REG && !v.regvar && strcmp(v.reg, "di") == 0)
        return;
    if (v.kind != VK_REG || v.regvar || strcmp(v.reg, "ax") != 0)
        gen_fatal("internal: a left operand to be moved into DI is not in AX");
    if (v.clobbered)
        fatal_clobbered();
    if (di_busy(g))
        gen_fatal("a product, quotient or call plus an element while DI holds "
                  "a pending value is not yet supported - see "
                  "src/mutos_cc/README.md");
    g->valsp--;
    ins2(g, "mov", o_reg("di"), o_reg("ax"));
    push_val(g, val_reg("di"));
}

/* SEG_LOADIND: see plan_value()'s ORD_ACOMMUTE case. */
static void plan_loadind(GenState *g)
{
    if (g->valsp < 1)
        gen_fatal("expression stack underflow - malformed temp1 stream");
    Val *top = &g->valstack[g->valsp - 1];
    if (top->kind != VK_IND || top->bytev || top->sym)
        return;
    if (top->clobbered)
        fatal_clobbered();
    Val v = *top;
    g->valsp--;
    ins2(g, "mov", o_reg(v.reg), o_val(v));
    push_val(g, val_reg(v.reg));
}

/* SEG_DEFPUSH: the pointer variable just streamed (is_deferred_ptr())
 * goes onto the machine stack - "push *4.(bp)". */
static void plan_defpush(GenState *g)
{
    Val p = pop_val(g);
    if (p.kind != VK_MEM && p.kind != VK_STATIC)
        gen_fatal("internal: a deferred pointer operand is not a variable "
                  "in memory");
    ins1(g, "push", o_val(p));
    Val s = {0};
    s.kind = VK_STACKED;
    push_val(g, s);
}

/* SEG_ADDRPUSH: the element just computed (is_pushaddr(), is_pushleft())
 * is "(reg)" - its address in a register - and that register is pushed:
 * "push di". A comparison's left element may have a displacement ("*4.
 * (di)" - a member of an element, is_pushleft()); the marker keeps it for
 * SEG_DEFPOPL ("*4.(bx)"). */
static void plan_addrpush(GenState *g)
{
    if (g->valsp < 1)
        gen_fatal("expression stack underflow - malformed temp1 stream");
    Val v = g->valstack[g->valsp - 1];
    if (v.kind != VK_IND || v.bytev || v.sym)
        gen_fatal("internal: an element whose address is pushed is not a "
                  "word dereference of a register");
    if (v.clobbered)
        fatal_clobbered();
    g->keep_ind = 0;
    g->valsp--;
    ins1(g, "push", o_reg(v.reg));
    Val s = {0};
    s.kind = VK_STACKED;
    s.imm = v.imm;
    push_val(g, s);
}

/* SEG_DEFPOP: the left operand is complete; the pushed pointer comes
 * back into BX and the right operand becomes "(bx)" - "pop bx" / "sub
 * di,(bx)". A left operand that is still a dereference ("(di)") is
 * loaded into its register first: the 8086 SUB takes one memory
 * operand, and "(bx)" is it. So is a variable, into DI (v7's "F" before
 * the pop) - fltprobe/p19_open3.s.golden's "x - b[j]" -> ... "push di" /
 * "mov di,*-30.(bp)" / "pop bx" / "sub di,(bx)". */
static void plan_defpop(GenState *g)
{
    if (g->valsp < 2 || g->valstack[g->valsp - 2].kind != VK_STACKED)
        gen_fatal("internal: a deferred pointer operand is missing from "
                  "below the value stack's top");
    Val l = pop_val_ex(g, POP_CHARX);
    g->valsp--;                          /* the VK_STACKED marker */
    l = materialize(g, l);
    if (l.kind == VK_IND && !l.bytev) {
        ins2(g, "mov", o_reg(l.reg), o_val(l));
        l = val_reg(l.reg);
    } else if (((l.kind == VK_MEM || l.kind == VK_STATIC) && !l.bytev &&
                !l.structv) || l.kind == VK_IMM) {
        if (di_busy(g))
            gen_fatal("a variable or constant minus an element while DI "
                      "holds a pending value is not yet supported - see "
                      "src/mutos_cc/README.md");
        load_into_di(g, l);
        l = val_reg("di");
    }
    require_free(l, RB_BX, "a subtraction through a pointer");
    ins1(g, "pop", o_reg("bx"));
    push_val(g, l);
    push_val(g, val_ind("bx"));
}

/* SEG_DEFPOPL: the right operand of a comparison of two elements is
 * complete (is_pushleft()); it is loaded into its register, the pushed
 * address of the left one comes back into BX, and the left operand
 * becomes "(bx)", compared in place - "mov di,(di)" / "pop bx" / "cmp
 * (bx),di". */
static void plan_defpopl(GenState *g)
{
    if (g->valsp < 2 || g->valstack[g->valsp - 2].kind != VK_STACKED)
        gen_fatal("internal: a pushed element address is missing from "
                  "below the value stack's top");
    Val r = materialize(g, pop_val(g));
    long disp = g->valstack[--g->valsp].imm;   /* the VK_STACKED marker */
    if (r.kind == VK_IND && !r.bytev) {
        ins2(g, "mov", o_reg(r.reg), o_val(r));
        r = val_reg(r.reg);
    }
    if (r.kind != VK_REG)
        gen_fatal("internal: the right operand of a comparison of two "
                  "elements is not in a register");
    require_free(r, RB_BX, "a comparison of two elements");
    ins1(g, "pop", o_reg("bx"));
    Val l = val_ind_disp("bx", disp);
    l.memleft = 1;
    push_val(g, l);
    push_val(g, r);
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
        /* A char condition is tested as a byte - see as_cond(). */
        gen_cond_branch(g, pop_val_ex(g, POP_BYTE), p->slot[s->a], s->b, rec[0]);
        region_close(g, rec);
        return;
    }
    case SEG_LOGVAL:
        push_val(g, gen_logval(g, p->slot[s->a]));
        return;
    case SEG_QTRUE:
        if (s->start == 1) {
            gen_arm_load_long(g);
        } else if (s->start == 2) {
            gen_arm_load_float(g);
            /* The other arm's value takes the same place: the real
             * compiler's stack model (fp_track()) counts only one -
             * fltprobe/p21_fltexp's "d = x ? d : e" leaves it balanced,
             * or its line-56 underflow would not have been reported
             * (v7's cexpr() restores nstack after a "?:"'s first arm the
             * same way). */
            g->fdepth--;
        } else {
            gen_arm_load(g);
        }
        region_close(g, &p->slot[s->c]);
        p->slot[s->b] = g->next_lab++;
        ins1(g, "jmp", o_lab(p->slot[s->b]));
        put_label(g, p->slot[s->a]);
        return;
    case SEG_QFALSE:
        if (s->start == 1)
            gen_arm_load_long(g);
        else if (s->start == 2)
            gen_arm_load_float(g);
        else
            gen_arm_load(g);
        region_close(g, &p->slot[s->c]);
        put_label(g, p->slot[s->b]);
        push_val(g, s->start == 1 ? val_long() :
                    s->start == 2 ? val_facc() : val_reg("di"));
        return;
    case SEG_DISCARD:
        discard_val(g);
        region_close(g, &p->slot[s->c]);
        return;
    case SEG_PUSHARG:
        /* A char argument is widened as it is pushed, a floating one
         * pushed as a double - see push_call_arg(). */
        p->slot[s->a] += push_call_arg(g, pop_val_ex(g, POP_BYTE | POP_CHARX |
                                                        POP_FLOAT));
        return;
    case SEG_CALL: {
        /* OP_CALL's own handler, for a call whose arguments a plan
         * already pushed - see plan_call(). */
        int fres = (s->b == TY_DOUBLE || s->b == TY_FLOAT);
        if (!ty_is_word(s->b) && s->b != TY_LONG && !fres)
            gen_fatal("a call returning type %d is not yet supported "
                      "(only a function returning an int, a long, a float, "
                      "a double or a pointer is covered so far)", s->b);
        Val callee = pop_val(g);
        Val res = finish_call(g, callee, p->slot[s->a], s->b == TY_LONG);
        if (fres) {
            /* The callee left its value in dmath.o's "fac" and AX
             * pointing at it (gen_fp_rforce()): loaded from there, "call
             * fldd" - a float result too (p5_call's "e = ff(d)"). Unused,
             * it stays in "fac": p5_call's "tw(d);" is just "call _tw" /
             * "add sp,*8.". The CALL's consumer is read where its opcode
             * stands (`start`). */
            long pos = ftell(t1);
            if (fseek(t1, s->start, SEEK_SET) != 0)
                gen_fatal("internal: temp1 is not seekable (fseek failed)");
            Consumer c = scan_consumer(t1);
            if (fseek(t1, pos, SEEK_SET) != 0)
                gen_fatal("internal: temp1 is not seekable (fseek failed)");
            if (c.op == OP_EXPR) {
                res.kind = VK_FDONE;
            } else {
                ins1(g, "call", o_sym("fldd"));
                res = val_facc();
            }
            g->nfloat = 1;
        }
        push_val(g, res);
        return;
    }
    default:
        gen_fatal("internal: unknown evaluation-order step %d", (int)s->kind);
    }
}

static void fplan_step(GenState *g, FILE *t1, const Seg *s); /* "Floating
                                                   * point" */

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
        if (s->kind == SEG_SWAP2 || s->kind == SEG_RHSREG ||
            s->kind == SEG_LOADIND || s->kind == SEG_DEFPUSH ||
            s->kind == SEG_ADDRPUSH || s->kind == SEG_DEFPOP ||
            s->kind == SEG_DEFPOPL || s->kind == SEG_KEEPIND ||
            s->kind == SEG_TODI || s->kind == SEG_SHIFT ||
            s->kind == SEG_ASPOP) {
            switch (s->kind) {
            case SEG_SWAP2:   plan_swap2(g);   break;
            case SEG_RHSREG:  plan_rhsreg(g);  break;
            case SEG_LOADIND: plan_loadind(g); break;
            case SEG_DEFPUSH: plan_defpush(g); break;
            case SEG_ADDRPUSH: plan_addrpush(g); break;
            case SEG_DEFPOPL: plan_defpopl(g); break;
            case SEG_KEEPIND: g->keep_ind = 1;  break;
            case SEG_TODI:    plan_todi(g);    break;
            case SEG_SHIFT:   plan_shift(g, s->a); break;
            case SEG_ASPOP:   plan_aspop(g);   break;
            default:          plan_defpop(g);  break;
            }
            p->cur++;
            continue;
        }
        if (s->kind == SEG_FDATA || s->kind == SEG_FLOAD ||
            s->kind == SEG_FLOADT || s->kind == SEG_FLOADTP ||
            s->kind == SEG_FSWAP ||
            s->kind == SEG_FITOF || s->kind == SEG_FLEAF ||
            s->kind == SEG_FSTREG || s->kind == SEG_FDROP) {
            fplan_step(g, t1, s);        /* see "Floating point" */
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
/* Byte data: string literals (temp2 - see gen_strings()) and a file-
 * scope char array's string initializer (temp1 - OP_BDATA in
 * c1_generate()). mutos_c0's putstr() writes both the same way. */

/* The most values the real compiler puts on one ".byte" line of a
 * BDATA run - see put_bdata_run(). */
#define MCC_BYTES_PER_LINE 9

/*
 * One BDATA run, its tag already read from `f` ("temp1" or "temp2" in
 * `what`, for diagnostics): (1, value) pairs ended by a word that is
 * not 1 - putstr()'s lone 0 - rendered as
 *
 *     ".byte\t/<v>,/<v>,..."
 *
 * v7/cc/c11.c's BDATA case reads the same pairs, but prints them all on
 * one line in octal; the real MUTOS c1 prints them in hex with
 * mutos_as's '/' prefix (lower case, no leading zeros: "/6f", "/a",
 * "/0") and starts a new ".byte" line after every 9th value. Confirmed
 * byte-for-byte for string literals against 05_arrptr/05_arrofptr.s.
 * golden ("L4:.byte\t/6f,/6e,/65,/0" - one short line per literal) and
 * 07_strlibc.s.golden ("hello, mutos", 13 values: a line of 9, then
 * ".byte\t/74,/6f,/73,/0"), plus 10_integ/04_strrev.s.golden (10
 * values: 9, then ".byte\t/0"); and for a char array's initializer
 * against 10_integ/01_wordcount.s.golden ("_text:.byte\t/74,/68,..."
 * - three runs of 14, 15 and 16 values, each broken 9 + rest).
 * mutos_c0 starts a new BDATA run before every 15th byte (v7's
 * putstr()); each run starts a new line too. The 9-per-line rule and
 * that split together reproduce the line layout of all 208 string
 * literals in the real compiler's output in tests/mutos_as/
 * kernel_nonopt/ and kernel_opt/ (runs of 14, 15, 15, ... values, each
 * broken 9 + rest).
 *
 * A byte value of 0x80 or above is printed as its 8-bit value
 * ("/e4"): mutos_c0 masks every byte to 0..255 like v7's putstr(), and
 * this renders the word as read. No golden string holds such a byte, so
 * that is derived, not confirmed. (The sign-extended "/ff81" values in
 * kernel_nonopt/amx.s are a char array's BRACE-LIST initializer's -
 * "_partab:.byte /1", a space, one value per line - a different code
 * path (v7's INIT, one per element) that mutos_c0 does not produce.)
 */
static void put_bdata_run(GenState *g, FILE *f, const char *what)
{
    char line[OPND_MAX * 4];
    int nvals = 0;
    size_t len = 0;
    while (c1_read_num(f, what) == 1) {
        unsigned v = (unsigned)c1_read_num(f, what) & 0xFFFFu;
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

/*
 * Renders temp2 - written by mutos_c0's putstr(), one run per string
 * literal - after the ".data" that ends the code (v7/cc/c10.c's main()
 * "tack[s] on the string file" the same way, after temp1's EOFC). Only
 * what putstr() writes is accepted:
 *
 *     LABEL <n>     -> "L<n>:" (no newline - the data glues on, as
 *                      every label does; put_label())
 *     BDATA (1 v)... 0
 *                   -> ".byte\t/<v>,/<v>,..." - see put_bdata_run()
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
        put_bdata_run(g, t2, "temp2");
    }
}

/* -------------------------------------------------------------- */
/* Floating point (tests/mutos_cc/08_float, tests/mutos_cc/fltprobe).
 *
 * The 8086 targets MUTOS 1700 ran on have no FPU: the real compiler
 * does all floating arithmetic by calling the software floating-point
 * runtime in libc.a (tests/mutos1700_libc/: stacks.o, singles.o,
 * doubles.o, stkmath.o, convert.o, lconvert.o, dmath.o). That runtime
 * keeps a stack of 8-byte doubles ("fpstk", its top at "fpsp") and takes
 * a memory operand's ADDRESS in AX. Confirmed byte-for-byte against
 * 08_float/01_floatbas.s.golden and 02_dblconv.s.golden:
 *
 *   a = 3.5;        .data / L10000:<TAB>.float 3.50000000000000000e+00 /
 *                   .text / lea ax,L10000 / call flds /
 *                   lea ax,*-8.(bp) / call fstsp
 *   c = a + b;      lea ax,*-8.(bp) / call flds / lea ax,*-12.(bp) /
 *                   call fadds / lea ax,*-16.(bp) / call fstsp
 *   (a * b: fmuls; d / 2.0: fdivs with the constant's label)
 *   d = i;          mov di,*-14.(bp) / mov ax,di / call itof /
 *                   lea ax,*-12.(bp) / call fstdp
 *   (int) c         lea ax,*-16.(bp) / call flds / call ftoi   (-> AX)
 *   l = (long) d;   lea ax,*-12.(bp) / call fldd / call ftol /
 *                   mov di,dx / mov si,ax / (the long store)
 *
 * and a file with any of it ends ".globl<TAB>fltused" ahead of the final
 * ".data" (v7/cc/c10.c's nfloat: the reference that pulls the floating
 * part of printf in). An operand is pushed onto that stack - "flds"/
 * "fldd" for a float/double in memory, "itof" for an int (in AX) - an
 * operator combines the top with an operand in memory ("fadds", "faddd",
 * ...), an assignment pops the top into its target ("fstsp"/"fstdp"),
 * and "ftoi"/"ftol" pop it into AX / DX:AX.
 *
 * Twenty probe programs (tests/mutos_cc/fltprobe/, real-hardware goldens
 * in four rounds, 2026-09-29, 2026-09-30 and twice 2026-10-02) settled
 * the rest,
 * after libc.a's own compiled C (atof.o,
 * ecvt.o, gcvt.o, fltpr.o - optimized, so only a first reading) had
 * shown most of it (docs/DEVLOG.md's "Floating shapes from libc.a's
 * compiled C" and "The fltprobe goldens"). The real compiler is v7's c1
 * with the PDP-11 floating code replaced by calls into that runtime -
 * its trees, optim(), acommute() and degree() unchanged, so the ORDER in
 * which it evaluates operands is v7's, decided on the whole tree before
 * any code. mutos_c1 streams temp1, so every expression with a floating
 * node in it is planned (see "Evaluation order"): plan_fvalue() below
 * rebuilds v7's order, and the ordinary handlers generate each node
 * with its operands arriving in that order. The rules, each confirmed
 * by the goldens:
 *
 * - degree() (v7/cc/c11.c): a leaf is 0, a FLOAT (or char) leaf 1, a
 *   constant -3 - but a FLOATING constant is 1: the MUTOS compiler keeps
 *   one that is exactly a single-precision value as a 4-byte ".float",
 *   typed FLOAT, and degree() gives a FLOAT leaf 1. "a < 1.5" (a float)
 *   loads 1.5 first (03_fltcmp), "e + 10" loads 10.0 first and adds e
 *   from memory (04_fltconst), "1.5 < i" / "i < 1.5" keep their order
 *   (p1_compare: the converted int has degree 1 too). An 8-byte
 *   ".double" constant is DOUBLE, degree 0, like a double variable: "d +
 *   0.1" and "0.1 + d" keep their order, "0.1 * a" loads the float a
 *   first, "d < 0.1" is exchanged (p6_dblcon). A converted int, a
 *   negation, a conversion, a value read through a pointer: max(1,
 *   degree(operand)) - "*p + 1.5" loads *p first (p8_misc); a '+'/'*'
 *   chain acommute()'s; '-' and '/' optim()'s (with '/' two more); a call
 *   10. A local's address is computed ("lea") and counts 1, so an element
 *   of a local array subscripted by a variable has degree 2: "1.5 +
 *   arr[i]" loads the element first (p10_elem). See fdeg().
 * - A relational exchanges its operands (and mirrors itself) when
 *   degree(left) < degree(right), or when they are equal and only the
 *   left is a NAME (optim()). See plan_fvalue().
 * - A '+'/'*' chain is acommute()'s: its terms in order of decreasing
 *   degree, equal ones as written, rebuilt left to right - "(int)(gd +
 *   gi + ga[2] + arr[0] + gs.y)" starts with the float gs.y (p3_global),
 *   "(d + e) * (i * j)" with i * j (p2_arith: an int product has degree
 *   2). See plan_fchain().
 * - Every operator loads its left operand first. A right operand in
 *   memory is combined from there ("lea ax,<it>" / "call fadd<s|d>");
 *   a computed one is computed onto the stack after the left, and the
 *   two are combined by the stack-with-stack entry point, "call fadd"
 *   (stkmath.o): "d - i" -> "fldd d / itof / fsub", "b * (a + e)" ->
 *   "flds b / flds a / faddd e / fmul", "tw(d) + tw(e)" -> ... "fadd"
 *   (p2_arith, p5_call). No "reversed" entry point (singles.o's fsubrs,
 *   ...) is used. A comparison is "call fcmp" (pops both, flags of left
 *   - right in AH) / "sahf" and a signed branch; a zero is compared like
 *   any constant, never tested (p1_compare).
 * - A floating constant is written in .data under a c1 label where v7's
 *   cexpr() prints it: when the operator it is a direct operand of is
 *   matched - before that operator's code, so ahead of a computed left
 *   operand: "(d + e) * 2.0" -> ".data / L10004: .float 2.0 / .text /
 *   fldd d / faddd e / lea ax,L10004 / fmuls" (p2_arith). Its label is
 *   taken as v7's c1 reads the tree: every written constant of the
 *   expression in order, then every int constant converted (unoptim()'s
 *   ITOF(CON) fold, after the reading) - "2 * 1.5" -> L10007 is 1.5,
 *   L10008 is 2.0, printed 2.0 first (p4_const). See fplan_constants().
 *   A negated constant is folded ("-1.5" -> ".float -1.50...", no
 *   "fneg" - 04_fltconst), "-(-e)" is e (p2_arith). The text is "%.17e"
 *   of the value, which the real compiler's printf writes digit for
 *   digit the same for a value exactly representable as a float (see
 *   fcon_render()); a constant that is not one is 8 bytes, ".double"
 *   (p4_const), with the MUTOS ecvt()'s digits (c1_fltdec.c).
 * - A floating zero is an ordinary constant (acommute() tosses only an
 *   int "+0"): "d + 0.0" -> ".float 0.0..." / "flds" / "faddd d", "d -
 *   0.0" -> "fldd d" / "fsubs" (p13_open); a negated one is written as
 *   the zero ("-0.0" -> ".float 0.000...", p16_open2). A constant converted
 *   to an int is not folded: "(int) 2.5" -> "flds" / "ftoi" (p16_open2).
 * - "a += b", "-=", "/=" load the target and combine the right operand
 *   ("fldd d / faddd e / fstdp d"); "/=" also a computed one ("fldd d /
 *   fldd e / faddd e / fdiv / fstdp d"), but "+=" computes it first and
 *   adds the target from memory ("movb ax,c / cbw / itof / faddd d",
 *   p16_open2 - v7's efftab "%a,n"; "-=" with a computed operand, which
 *   would need a reversed subtraction, is refused); "a *= b" loads the
 *   RIGHT operand and multiplies the target in from memory ("fldd e /
 *   fmuld d / fstdp d", "itof / fmuld d") - v7's optim() puts ASTIMES
 *   among the operators whose degree it raises, like TIMES. A used value
 *   is kept on the stack: "fstd" (no pop). See gen_fp_asop().
 * - An int converted is computed into AX: "mov di,i / mov ax,di / call
 *   itof" for a variable (through SI when DI holds a register variable:
 *   "mov si,di / mov ax,si"), "mov di,i / add di,j / mov ax,di" for a
 *   sum, "mov ax,c / add ax,*-48." for "c - '0'" (v7's optim() turns "x
 *   - 48" into "x + -48"), "mov ax,i / imul j" for a product, "movb ax,c
 *   / cbw" for a char - or straight in AX after a computed '*' or '/' or
 *   a call ("mov ax,i / inc ax", p7_itofreg; "mov ax,i / sub ax,j", "mov
 *   ax,i / sal ax,*1", p13_open - see FREG_DI), a remainder from DX ("mov ax,dx"), an
 *   unsigned variable as a long ("mov si,u / sub di,di / push si / push
 *   di / call ltof", p16_open2). See gen_itof().
 * - An element of a floating array subscripted by a variable is
 *   addressed in DI as an int element is ("lea di,arr / mov si,i / mov
 *   cx,*3. / sal si,cl / add di,si") and used as "(di)": "lea ax,(di) /
 *   call fldd"; as an assignment's target, after the right-hand side is
 *   loaded (p8_misc). See fstar_computed().
 * - A file-scope variable's initializer is written by doinit(): the
 *   variable's own directive, the constant converted to its type (an
 *   int constant, a negated one, an inexact one, a float's truncated)
 *   - p3_global, p9_init. See gen_finit().
 * - A call returning a double or a float leaves the value in dmath.o's
 *   "fac" (the callee: "lea ax,fac / call fstdp / lea ax,fac") and the
 *   caller loads it, "call fldd" - or, unused, leaves it there. A double
 *   argument is "sub sp,*8" (no decimal point) / "mov ax,sp" / "call
 *   fstdp" (push_fp_arg()).
 *
 * Refused explicitly: a constant whose value libc.a's atof() does not
 * give exactly, '%', '%=', "-=" with a computed right operand, an int
 * converted after an operand whose register no golden shows
 * (FREG_UNKNOWN), and every node the planner has no degree for
 * (fdeg()). */

/* The text and size of floating literal `text` (negated when `negate`),
 * as the real compiler writes it - libc.a's ecvt() of libc.a's atof(),
 * see c1_fldec.h/c1_fltdec.c - into out[0..n), *dbl set for an 8-byte
 * ".double"; refused when libc.a's atof() would not give its value. */
static void fconst_text(const char *text, int negate, char *out, size_t n,
                        int *dbl, int *is_zero)
{
    int is_float;
    FdecStatus st = fdec_render(text, negate, out, n, &is_float, is_zero);
    if (st == FDEC_SYNTAX)
        gen_fatal("malformed floating constant %s", text);
    if (st != FDEC_OK)
        gen_fatal("floating constant %s: %s - see src/mutos_cc/README.md",
                  text, st == FDEC_RANGE
                        ? "outside the range libc.a's atof() converts "
                          "(the real compiler's own conversion would overflow "
                          "or wrap its exponent)"
                        : "its value is not known exactly (libc.a's atof() "
                          "gives up on it in a way no golden shows)");
    *dbl = !is_float;
}

/* The planned constant with temp1 offset `off` (see fplan_constants()),
 * or NULL. */
static FConst *fc_find(GenState *g, long off)
{
    for (int i = 0; i < g->plan.nfc; i++)
        if (g->plan.fc[i].off == off)
            return &g->plan.fc[i];
    return NULL;
}

/* Writes constant `c`'s .data block - ".data" / "L<n>:<TAB>.float
 * <text>" / ".text" - unless it is out already. */
static void put_float_data(GenState *g, FConst *c)
{
    if (c->printed)
        return;
    ins0(g, ".data");
    put_label(g, c->label);
    put_line(g, "\t.%s %s", c->dbl ? "double" : "float", c->text);
    ins0(g, ".text");
    c->printed = 1;
    g->nfloat = 1;
}

/* A planned constant's value: a VK_FCON naming its label. Its .data
 * block is normally out already (SEG_FDATA, where v7 prints it); a
 * constant with no step of its own is printed where it is read. */
static Val fconst_val(GenState *g, FConst *c)
{
    put_float_data(g, c);
    Val v = {0};
    v.kind = VK_FCON;
    v.offset = c->label;
    v.fdouble = c->dbl;
    return v;
}

/* OP_FCON: the constant planned for this opcode (fplan_constants()). A
 * file-scope variable's initializer never gets here - see gen_finit(). */
static void gen_fcon(GenState *g, FILE *t1)
{
    int type = c1_read_num(t1, "temp1");
    char *text = c1_read_sym(t1, "temp1");
    if (type != TY_DOUBLE)
        gen_fatal("FCON of type %d not yet supported (mutos_c0 writes every "
                  "floating constant as a double)", type);
    FConst *c = fc_find(g, g->op_off);
    free(text);
    if (!c)
        gen_fatal("internal: a floating constant outside a planned "
                  "expression");
    push_val(g, fconst_val(g, c));
}

/* A file-scope floating variable's initializer, after its "DATA NLABEL":
 * "FCON [NEG] INIT(type)" or "CON ITOF INIT(type)" (v7's cinit(): the
 * constant converted to the variable's type, as for '=') - written by v7
 * c1's doinit() as the value of the double, or for a float of the double
 * converted to a float ("sfval = fval"), after optim() folded a NEG and an
 * ITOF(CON). Returns 1 with the initializer read and its line written,
 * through its INIT (the EXPR that follows is read as usual), or 0 with
 * temp1 untouched when the next opcodes are not one.
 *
 * fltprobe/p3_global.s.golden and p9_init.s.golden: ".data" / "_gi:<TAB>
 * .double<TAB>2.50000000000000000e+00" - the directive the variable's
 * type ("double gi = 2;" -> ".double 2.0...", unlike a code constant's
 * ".float"), a tab after it (unlike a code constant's space), the text
 * the MUTOS ecvt()'s ("0.1" -> "1.00000000000000000e-01", "-1.5" ->
 * "-1.50000000000000000e+00"), a float's truncated ("float gy = 0.1;" ->
 * ".float<TAB>9.99999940395355225e-02" - see fdec_render_single()). Each
 * initializer takes a c1 label, as the real c1 numbers every FCON it reads
 * and every ITOF(CON) it folds, though none is written: p9_init's code
 * constant after five initializers is L10005. */
static int gen_finit(GenState *g, FILE *t1)
{
    long pos = ftell(t1);
    if (pos < 0)
        gen_fatal("internal: temp1 is not seekable (ftell failed)");
    char *text = NULL;
    char num[24];
    int negate = 0;
    int op = c1_read_op(t1, "temp1");
    if (op == OP_FCON) {
        (void)c1_read_num(t1, "temp1");
        text = c1_read_sym(t1, "temp1");
        op = c1_read_op(t1, "temp1");
        if (op == OP_NEG) {
            (void)c1_read_num(t1, "temp1");
            negate = 1;
            op = c1_read_op(t1, "temp1");
        }
    } else if (op == OP_CON) {
        (void)c1_read_num(t1, "temp1");
        long v = c1_read_num(t1, "temp1");
        op = c1_read_op(t1, "temp1");
        if (op == OP_ITOF) {
            (void)c1_read_num(t1, "temp1");
            snprintf(num, sizeof num, "%ld", v < 0 ? -v : v);
            negate = (v < 0);
            op = c1_read_op(t1, "temp1");
        } else {
            op = -1;
        }
    } else {
        op = -1;
    }
    if (op != OP_INIT) {
        free(text);
        if (fseek(t1, pos, SEEK_SET) != 0)
            gen_fatal("internal: temp1 is not seekable (fseek failed)");
        return 0;
    }
    int itype = c1_read_num(t1, "temp1");
    if (itype != TY_DOUBLE && itype != TY_FLOAT)
        gen_fatal("INIT of type %d not yet supported", itype);
    const char *lit = text ? text : num;
    char buf[48];
    int is_zero = 0, dbl = 0;
    if (itype == TY_FLOAT) {
        FdecStatus st = fdec_render_single(lit, negate, buf, sizeof buf,
                                           &is_zero);
        if (st != FDEC_OK)                  /* fconst_text()'s diagnostic */
            fconst_text(lit, negate, buf, sizeof buf, &dbl, &is_zero);
    } else {
        fconst_text(lit, negate, buf, sizeof buf, &dbl, &is_zero);
    }
    if (negate && is_zero) {
        /* Written as the zero itself, as in code (fplan_constants() -
         * fltprobe/p16_open2's "-0.0"); for an initializer an inference. */
        if (itype == TY_FLOAT)
            (void)fdec_render_single(lit, 0, buf, sizeof buf, &is_zero);
        else
            fconst_text(lit, 0, buf, sizeof buf, &dbl, &is_zero);
    }
    free(text);
    g->next_lab++;
    put_line(g, "\t.%s\t%s", itype == TY_DOUBLE ? "double" : "float", buf);
    return 1;
}

/* "lea ax,<address of v>" for a floating operand in memory: a constant's
 * label, a local's "*-12.(bp)", a file-scope one's symbol - "_gd", an
 * element or member "16.+_ga", "8.+_gs" - a local static's label "L4",
 * or, through a pointer, the pointer loaded into DI first and "(di)"
 * (fltprobe/p3_global.s.golden's every form). */
static void fp_lea(GenState *g, Val v)
{
    if (v.kind == VK_FCON) {
        ins2(g, "lea", o_reg("ax"), o_lab(v.offset));
        return;
    }
    if (v.kind != VK_FMEM)
        gen_fatal("internal: a floating operand with no address");
    switch (v.fmode) {
    case FM_BP:
        ins2(g, "lea", o_reg("ax"), o_mem(v.offset));
        return;
    case FM_SYM:
        ins2(g, "lea", o_reg("ax"),
             v.imm ? o_fmt("%ld.+%s", v.imm, v.sym) : o_sym(v.sym));
        return;
    case FM_LAB:
        ins2(g, "lea", o_reg("ax"), o_lab(v.offset));
        return;
    default: {
        if (g->reserved & RB_DI)
            gen_fatal("a 'float'/'double' read through a pointer while DI "
                      "holds a register variable is not yet supported - see "
                      "src/mutos_cc/README.md");
        Val ptr = val_from_simple(v.cl);
        if (ptr.kind == VK_REG && strcmp(ptr.reg, "ax") == 0) {
            /* A call's result: AX is no base register - moved to BX,
             * fltprobe/p20_fltlv.s.golden's "*pick(a, 2)" -> "call
             * _pick" / "add sp,*4." / "mov bx,ax" / "lea ax,(bx)". */
            ins2(g, "mov", o_reg("bx"), o_reg("ax"));
            ins2(g, "lea", o_reg("ax"), o_sym("(bx)"));
            return;
        }
        load_into_di(g, ptr);
        ins2(g, "lea", o_reg("ax"),
             v.imm ? o_fmt("*%ld.(di)", v.imm) : o_sym("(di)"));
        return;
    }
    }
}

/* The precision suffix of the runtime entry point that reads `v` from
 * memory: 'd' for a double (a variable, or an 8-byte constant), 's' for
 * a float or a ".float" constant. */
static char fp_suffix(Val v)
{
    return v.fdouble ? 'd' : 's';
}

static int is_float_val(const Val *v)
{
    return v->kind == VK_FMEM || v->kind == VK_FCON || v->kind == VK_FACC;
}

/* Pushes `v` onto the floating-point stack, unless it is already there:
 * "lea ax,<v>" / "call fld<s|d>". */
static void fp_load(GenState *g, Val v)
{
    if (v.kind == VK_FACC || (v.kind == VK_FMEM && v.fonstk))
        return;
    if (v.kind != VK_FMEM && v.kind != VK_FCON)
        gen_fatal("internal: a non-floating value reached fp_load()");
    char fn[8];
    snprintf(fn, sizeof fn, "fld%c", fp_suffix(v));
    fp_lea(g, v);
    ins1(g, "call", o_sym(fn));
    g->nfloat = 1;
}

/* Pops a floating operand (anything but an assignment's spent value). */
static Val pop_float(GenState *g, const char *ctx)
{
    Val v = pop_val_ex(g, POP_FLOAT);
    if (v.kind != VK_FMEM && v.kind != VK_FCON && v.kind != VK_FACC)
        gen_fatal("%s: %s is not yet supported - see src/mutos_cc/README.md",
                  ctx, v.kind == VK_FDONE
                       ? "the value of a 'float'/'double' assignment used again"
                       : "a non-floating operand");
    return v;
}

static Val val_facc(void)
{
    Val r = {0};
    r.kind = VK_FACC;
    return r;
}

/* The runtime's name for a floating '+', '-', '*' or '/' ("fadd", ...),
 * NULL for any other operator. */
static const char *fp_opname(int op)
{
    switch (op) {
    case OP_PLUS:   case OP_ASPLUS:  return "fadd";
    case OP_MINUS:  case OP_ASMINUS: return "fsub";
    case OP_TIMES:  case OP_ASTIMES: return "fmul";
    case OP_DIVIDE: case OP_ASDIV:   return "fdiv";
    default:        return NULL;
    }
}

/* '+', '-', '*', '/' typed DOUBLE, its operands in the planned order
 * (the left one generated first - see this section's header): the left
 * onto the stack, then the right from memory - "lea ax,<right>" / "call
 * f<op><s|d>" - or, computed onto the stack after it, "call f<op>". */
static void gen_fp_binop(GenState *g, int op)
{
    const char *fn = fp_opname(op);
    if (!fn)
        gen_fatal("a 'float'/'double' operator %d is not yet supported (only "
                  "'+', '-', '*', '/') - see src/mutos_cc/README.md", op);
    Val r = pop_float(g, "floating operator");
    Val l = pop_float(g, "floating operator");
    if (r.kind == VK_FACC) {
        if (l.kind != VK_FACC)
            gen_fatal("internal: a floating operator's left operand is not "
                      "on the stack below its computed right one");
        ins1(g, "call", o_sym(fn));
    } else {
        fp_load(g, l);
        char name[8];
        snprintf(name, sizeof name, "%s%c", fn, fp_suffix(r));
        fp_lea(g, r);
        ins1(g, "call", o_sym(name));
    }
    g->nfloat = 1;
    push_val(g, val_facc());
}

/* ---- The planner (see this section's header and "Evaluation order") */

static int ty_isfloat(int type)
{
    return type == TY_FLOAT || type == TY_DOUBLE;
}

/* A floating comparison or equality: a relational whose operands are
 * floating (mutos_c0 types the node INT, as v7's build() does). */
static int is_frel(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    return find_relop(n->op) && n->kid[0] >= 0 &&
           ty_isfloat(t->v[n->kid[0]].type);
}

/* Node i is planned by plan_fvalue(): a floating-typed node or a
 * floating comparison. */
static int is_fnode(const ETree *t, int i)
{
    return ty_isfloat(t->v[i].type) || is_frel(t, i);
}

/* A floating NEG - see fconst_base(). */
static int is_fneg(const ETree *t, int i)
{
    return t->v[i].op == OP_NEG && ty_isfloat(t->v[i].type);
}

/* Node i is a floating constant - a written one, an int constant
 * converted, or either negated (folded) - and its base, the FCON or
 * ITOF node its constant is planned under; -1 when not a constant. */
static int fconst_base(const ETree *t, int i)
{
    while (is_fneg(t, i))
        i = t->v[i].kid[0];
    const ENode *n = &t->v[i];
    if (n->op == OP_FCON)
        return i;
    if (n->op == OP_ITOF && ty_isfloat(n->type) &&
        t->v[n->kid[0]].op == OP_CON)
        return i;
    return -1;
}

static int fconst_node(const ETree *t, int i)
{
    return fconst_base(t, i) >= 0;
}

/* The address `i` computes, when it is a named object plus constants -
 * "&x + c", "&a + i*size" with i and size constants - as v7's optim()
 * folds it (acommute()'s "&x+c" subsumed into the NAME): the NAME node
 * and the total offset. */
static int ffold(const ETree *t, int i, int *name, long *disp)
{
    *disp = 0;
    for (;;) {
        const ENode *n = &t->v[i];
        if (n->op == OP_PLUS && ty_is_ptr(n->type)) {
            const ENode *r = &t->v[n->kid[1]];
            if (r->op == OP_CON) {
                *disp += r->aux;
            } else if (r->op == OP_ITOP && t->v[r->kid[0]].op == OP_CON &&
                       t->v[r->kid[1]].op == OP_CON) {
                *disp += t->v[r->kid[0]].aux * t->v[r->kid[1]].aux;
            } else {
                return 0;
            }
            i = n->kid[0];
            continue;
        }
        if (n->op == OP_AMPER && t->v[n->kid[0]].op == OP_NAME &&
            (t->v[n->kid[0]].aux == SC_AUTO || t->v[n->kid[0]].aux == SC_EXTERN)) {
            *name = n->kid[0];
            return 1;
        }
        return 0;
    }
}

/* Node i is a floating operand in memory reached with no code: a NAME,
 * or an element or member at a constant offset of a named object
 * (ffold()) - v7's degree() and optim() treat either as the NAME leaf
 * it is. */
static int fnamed(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (!ty_isfloat(n->type))
        return 0;
    if (n->op == OP_NAME)
        return 1;
    int name;
    long disp;
    return n->op == OP_STAR && ffold(t, n->kid[0], &name, &disp);
}

static int is_fpreinc(const ETree *t, int i);

/* A leaf: a floating operand that can be the memory operand of an
 * operator, with no code of its own before it is loaded (a hoisted
 * prefix '++'/'--' is its variable by then - is_fpreinc()). */
static int fleaf(const ETree *t, int i)
{
    return fnamed(t, i) || fconst_node(t, i) || is_fpreinc(t, i);
}

/* A prefix '++'/'--' of a floating variable as an operand of a floating
 * '+', '*', '-' or '/': v7's rcexpr() reorder()s the operator's subtree
 * first - sreorder() compiles a prefix operator on a NAME for its effect
 * (efftab) and puts the NAME in its place - so the increment comes ahead
 * of the operator's code, the variable is then an operand like any other
 * (degree and all) - fltprobe/p33_fltinf3.s.golden's "e = ++d * 2.0;" ->
 * ".data" / "L10029: .float 1.0..." / ".text" / "fldd d" / "fadds L10029"
 * / "fstdp d" / ".data" / "L10028: .float 2.0..." / ".text" / "flds
 * L10028" / "lea ax,d" / "fmuld" / "fstdp e" (2.0, degree 1, first; the
 * increment's 1.0 numbered after it, as a converted constant is). The
 * planner hoists it (plan_fhoist()). */
static int is_fpreinc(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (!(n->op == OP_INCBEF || n->op == OP_DECBEF) || !ty_isfloat(n->type) ||
        n->kid[0] < 0 || t->v[n->kid[0]].op != OP_NAME ||
        !ty_isfloat(t->v[n->kid[0]].type) || n->parent < 0)
        return 0;
    const ENode *pn = &t->v[n->parent];
    return ty_isfloat(pn->type) &&
           (pn->op == OP_PLUS || pn->op == OP_TIMES || pn->op == OP_MINUS ||
            pn->op == OP_DIVIDE);
}

/* A floating value read through an address that takes code of its own -
 * not a pointer variable (loaded only when the value's address is
 * needed - see fp_lea()), not an element or member at a constant offset
 * (fnamed()): "arr[i]". */
static int fstar_computed(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    return n->op == OP_STAR && ty_isfloat(n->type) && !fnamed(t, i) &&
           t->v[n->kid[0]].op != OP_NAME;
}

static int v7_islong(int type)
{
    return type == TY_LONG ? 2 : 1;
}

static int imax(int a, int b)
{
    return a > b ? a : b;
}

/* The link nodes of the '+' or '*' chain topped by `top`: nodes of the
 * same operator and type (for an int '+', also an int "x - constant",
 * which v7's optim() makes "x + -constant" first). */
static int is_chain_link(const ETree *t, int i, int op, int type)
{
    const ENode *n = &t->v[i];
    if (n->type != type)
        return 0;
    if (n->op == op)
        return 1;
    return op == OP_PLUS && !ty_isfloat(type) && n->op == OP_MINUS &&
           t->v[n->kid[1]].op == OP_CON;
}

#define FCHAIN_MAX 16

/* The terms of the chain topped by `top`, in source order (v7's
 * insert() walks both operands of every link, left first), into
 * terms[]; its links in postfix order - innermost first - into links[].
 * Returns the number of terms. */
static int fchain_terms(const ETree *t, int top, int *terms, int *links,
                        int *nlinks)
{
    int op = t->v[top].op == OP_MINUS ? OP_PLUS : t->v[top].op;
    int type = t->v[top].type;
    int stack[2 * FCHAIN_MAX], sp = 0, n = 0;
    stack[sp++] = top;
    while (sp > 0) {
        int i = stack[--sp];
        if (is_chain_link(t, i, op, type)) {
            if (sp + 2 > 2 * FCHAIN_MAX)
                gen_fatal("a floating expression with too many terms "
                          "(internal limit %d)", FCHAIN_MAX);
            stack[sp++] = t->v[i].kid[1];
            stack[sp++] = t->v[i].kid[0];
            continue;
        }
        if (n == FCHAIN_MAX)
            gen_fatal("a floating expression with too many terms (internal "
                      "limit %d)", FCHAIN_MAX);
        terms[n++] = i;
    }
    *nlinks = 0;
    for (int i = 0; i <= top; i++) {
        if (!is_chain_link(t, i, op, type))
            continue;
        int j = i;
        while (j != top && t->v[j].parent >= 0 &&
               is_chain_link(t, t->v[j].parent, op, type))
            j = t->v[j].parent;
        if (j == top)
            links[(*nlinks)++] = i;
    }
    return n;
}

static int fdeg(const ETree *t, int i);

/* The plan whose constants fdeg() consults - set by fplan_constants()
 * for the expression being planned. */
static const Plan *fdeg_plan;

static int fc_index(const Plan *p, const ETree *t, int i);

/* v7's insert(): each term goes before the first one of strictly lower
 * degree, the displaced one carried on the same way - decreasing
 * degree, equal ones in source order. */
static void fchain_order(const ETree *t, const int *terms, int n, int *order)
{
    int nl = 0;
    for (int k = 0; k < n; k++) {
        int cur = terms[k];
        int d = fdeg(t, cur);
        for (int j = 0; j < nl; j++) {
            int dj = fdeg(t, order[j]);
            if (dj < d) {
                int tmp = order[j];
                order[j] = cur;
                cur = tmp;
                d = dj;
            }
        }
        order[nl++] = cur;
    }
}

/* acommute()'s degree of the rebuilt chain topped by `top`. */
static int fchain_degree(const ETree *t, int top)
{
    int terms[FCHAIN_MAX], links[FCHAIN_MAX], order[FCHAIN_MAX], nlinks;
    int n = fchain_terms(t, top, terms, links, &nlinks);
    fchain_order(t, terms, n, order);
    int type = t->v[top].type;
    int op = t->v[top].op == OP_MINUS ? OP_PLUS : t->v[top].op;
    int flt = ty_isfloat(type);
    int d = imax(fdeg(t, order[0]), v7_islong(t->v[order[0]].type));
    if (op == OP_TIMES && !flt)
        d++;
    for (int k = 1; k < n; k++) {
        /* An int chain's constants are folded into one (acommute()). */
        if (!flt && t->v[order[k]].op == OP_CON && k > 1 &&
            t->v[order[k - 1]].op == OP_CON)
            continue;
        int d1 = fdeg(t, order[k]);
        d = (d == d1) ? d + v7_islong(type) : imax(d, d1);
    }
    /* "x * 2**n": the degree of x (acommute()'s ispow2() case). */
    if (op == OP_TIMES && !flt && n == 2 && t->v[order[1]].op == OP_CON) {
        long c = t->v[order[1]].aux;
        if (c > 0 && (c & (c - 1)) == 0)
            d = imax(fdeg(t, order[0]), v7_islong(type));
    }
    return d;
}

/* v7/cc/c11.c's degree() of node i after optim() - as far as floating
 * operand order needs it (see this section's header). */
static int fdeg(const ETree *t, int i)
{
    const ENode *n = &t->v[i];
    if (fconst_node(t, i)) {
        /* A ".float" constant is typed FLOAT (degree 1 - see this
         * section's header); an 8-byte ".double" one is DOUBLE, degree 0
         * (p6_dblcon: "d + 0.1" and "0.1 + d" both keep their order,
         * "a * 0.1" and "0.1 * a" both load the float a first, "d <
         * 0.1" is exchanged - equal degrees, only d a NAME). */
        if (!fdeg_plan)
            gen_fatal("internal: a floating constant's degree outside a "
                      "planned expression");
        return fdeg_plan->fc[fc_index(fdeg_plan, t, i)].dbl ? 0 : 1;
    }
    if (fnamed(t, i))
        return n->type == TY_FLOAT ? 1 : 0;
    if (is_fpreinc(t, i))
        return fdeg(t, n->kid[0]);       /* the NAME it becomes */
    switch (n->op) {
    case OP_NAME:
        return (n->type == TY_FLOAT || n->type == TY_CHAR) ? 1 : 0;
    case OP_CON:
        return -3;
    case OP_AMPER:
        /* A static or file-scope object's address is a link-time
         * constant, -2 as in v7. A local's is computed ("lea di,*-28.
         * (bp)") and counts as computed, 1: an element of a local array
         * subscripted by a variable is "&arr + i*size" with two terms of
         * degree 1, so the sum is 2 and the "lea" comes first (equal
         * degrees, as written) - fltprobe/p10_elem.s.golden's "1.5 +
         * arr[i]" loads the element first and adds the .float 1.5 (degree
         * 1) from memory, "fldd (di)" / "lea ax,L10003" / "fadds". */
        if (t->v[n->kid[0]].op == OP_NAME)
            return t->v[n->kid[0]].aux == SC_AUTO ? 1 : -2;
        break;
    case OP_ITOF: case OP_LTOF: case OP_NEG: case OP_FTOI: case OP_FTOL:
    case OP_ITOC: case OP_ITOL: case OP_LTOI: case OP_STAR:
        /* unoptim(): max(islong(type), degree(operand)) - for a STAR too
         * (one optim() does not fold into a NAME - fnamed() above): "*p
         * + 1.5" loads *p first (p8_misc - degree 1, as the constant's,
         * so as written), "arr[i] + 0.5" its element. */
        return imax(v7_islong(n->type), fdeg(t, n->kid[0]));
    case OP_CALL:
        return 10;
    case OP_ITOP: {
        /* optim() makes ITOP a TIMES: acommute()'s degree of "x * c" -
         * the degree of x for a power of two (ispow2()), one more
         * otherwise (fchain_degree()'s int TIMES). */
        const ENode *c = &t->v[n->kid[1]];
        int d = imax(fdeg(t, n->kid[0]), v7_islong(n->type));
        if (c->op != OP_CON)
            break;
        return (c->aux > 0 && (c->aux & (c->aux - 1)) == 0) ? d : d + 1;
    }
    case OP_PLUS: case OP_TIMES:
        return fchain_degree(t, i);
    case OP_MINUS:
        if (!ty_isfloat(n->type) && t->v[n->kid[1]].op == OP_CON)
            return fchain_degree(t, i);
        break;
    default:
        break;
    }
    int binary = n->kid[0] >= 0 && n->kid[1] >= 0;
    int extra;
    switch (n->op) {
    case OP_MINUS: case OP_ASSIGN: case OP_ASPLUS: case OP_ASMINUS:
        extra = 0;
        break;
    case OP_QUEST: case OP_COLON:
        /* optim()'s "def:" as for any binary node: a '?:' with double arms
         * has degree 2 - fltprobe/p24_fltinf2.s.golden's "(x ? d : e) *
         * 2.0" computes the '?:' first and multiplies the .float 2.0
         * (degree 1) in from memory, "fmuls". */
        extra = 0;
        break;
    case OP_LSHIFT: case OP_RSHIFT:
        /* optim()'s "def:" too (an int right shift is a left shift by
         * the negated count, the same degree), "d1++; d2++" for a long */
        extra = n->type == TY_LONG ? 1 : 0;
        break;
    case OP_DIVIDE: case OP_MOD: case OP_ASTIMES: case OP_ASDIV:
    case OP_ASMOD:
        extra = 2;
        break;
    default:
        extra = find_relop(n->op) ? 0 : -1;
        break;
    }
    if (binary && extra >= 0) {
        /* optim()'s "def:" - with its "d1 += 2; d2 += 2" for '/', '%',
         * '*=' and '/=' */
        int l = n->kid[0], r = n->kid[1];
        if (find_relop(n->op) && is_frel(t, i)) {
            int dl = fdeg(t, l), dr = fdeg(t, r);
            if (dl < dr || (dl == dr && fnamed(t, l) && !fnamed(t, r))) {
                int tmp = l;
                l = r;
                r = tmp;
            }
        }
        int d1 = imax(fdeg(t, l), v7_islong(n->type)) + extra;
        int d2 = imax(fdeg(t, r), 0) + extra;
        return d1 == d2 ? d1 + v7_islong(n->type) : imax(d1, d2);
    }
    gen_fatal("this operator (opcode %d) in a 'float'/'double' expression "
              "is not yet supported (mutos_c1 has no v7 degree() for it, "
              "which decides the operand order) - see "
              "src/mutos_cc/README.md", n->op);
}

/* The index in the plan's constant table of constant node i. */
static int fc_index(const Plan *p, const ETree *t, int i)
{
    long off = t->v[fconst_base(t, i)].off;
    for (int k = 0; k < p->nfc; k++)
        if (p->fc[k].off == off)
            return k;
    gen_fatal("internal: an unplanned floating constant");
}

/* v7's cexpr() prints a constant operand's .data block when its
 * operator is matched: SEG_FDATA for node i if it is a constant. */
static void plan_fdata(Plan *p, const ETree *t, int i)
{
    if (i >= 0 && fconst_node(t, i))
        plan_op(p, SEG_FDATA, fc_index(p, t, i), 0, 0);
}

/* A floating operand about to be combined with a computed one generated
 * after it: a leaf is loaded now, ahead of that code - and so is an
 * element whose address was computed into DI ("arr[i]", p10_elem's "arr[i]
 * + arr[j]") or a value read through a pointer variable (fltprobe/
 * p16_open2.s.golden's "*p + i" -> "mov di,*-22.(bp)" / "lea ax,(di)" /
 * "call fldd" before i is converted): v7's templates load the left
 * operand before they compute the right. */
static void plan_fload(Plan *p, const ETree *t, int first, int second)
{
    const ENode *f = &t->v[first];
    int fstar = f->op == OP_STAR && ty_isfloat(f->type);
    if ((fleaf(t, first) || fstar) && !fleaf(t, second))
        plan_op(p, SEG_FLOAD, 0, 0, 0);
}

static void plan_call(Plan *p, const ETree *t, int i);

/* The register an int converted to floating is loaded into - "itof"
 * takes it in AX, and the real compiler gets it there two ways
 * (fltprobe goldens): through DI - "mov di,i / mov ax,di / call itof" -
 * when the conversion is evaluated first, after a variable or constant
 * loaded, or after a computed '+'/'-' ("(t + fl) / c", 05_fltasop) or a
 * negation ("-d + i", p7_itofreg); but straight into AX - "mov ax,c /
 * call itof", "mov ax,c / add ax,*-48.", "mov ax,i / inc ax", "mov ax,i
 * / add ax,j" - after a computed floating '*' ("(fl * 2) - c", "10*fl +
 * (c-'0')", 05_fltasop; "(d * e) + (i + 1)", p7_itofreg), a '/' ("(d /
 * e) - i") or a call ("tw(d) - i"). v7's c1 hands registers out by
 * number; the next operand evidently takes the register the previous
 * one's result was given, and the MUTOS compiler gives a product, a
 * quotient and a call's result the register x86's imul/idiv and a
 * function's return use. The operands of a '*' itself are still in DI
 * ("d * i": "mov di,i / mov ax,di / call itof", p2_arith). An int
 * converted leaves the context as it was ("(double) i + (double) j": DI
 * both times, p13_open), and so does an element whose address was
 * computed in DI ("arr[i] + j", p13_open) or a value read through a
 * pointer variable ("*p + i", p16_open2); an assignment leaves its right-
 * hand side's ("(e = d * 2) + i": AX, p16_open2); in a '+'/'*' chain it is
 * the term evaluated just before ("(d * d) + arr[i] + i": AX after d * d,
 * p15_fltinf). After any other operand - an element after a '*' - the
 * register is not known (FREG_UNKNOWN: a conversion there is refused). */
enum { FREG_DI = 0, FREG_AX = 1, FREG_UNKNOWN = 2,
       FREG_ULONG = 3 /* not a context: SEG_FITOF's mark for an unsigned
                       * operand, converted as a long - see gen_itof() */ };

/* The register context for the operand evaluated after `first`, when
 * `first` was evaluated in context `freg`: a leaf, a computed '+'/'-', a
 * negation or an int converted leaves it, a computed '*' or '/' or a call
 * moves it to AX, and an element whose address was computed in DI leaves
 * DI - fltprobe/p13_open.s.golden's "(double) i + (double) j" and "arr[i]
 * + j" both convert the second int through DI. `first_op` is its operator
 * when `first` is a rebuilt chain node rather than a tree node (-1). */
static int fafter(const ETree *t, int first, int first_op, int freg)
{
    int op = first_op >= 0 ? first_op : t->v[first].op;
    if (first_op < 0 && fleaf(t, first))
        return freg;
    if (op == OP_PLUS || op == OP_MINUS || op == OP_NEG || op == OP_ITOF)
        return freg;
    if (op == OP_TIMES || op == OP_DIVIDE || op == OP_CALL)
        return FREG_AX;
    /* An assignment's value is its right-hand side's: fltprobe/
     * p16_open2.s.golden's "(e = d * 2) + i" converts i in AX ("fmuld" /
     * "fstd" / "mov ax,*-24.(bp)"), as after the '*'. */
    if (first_op < 0 && op == OP_ASSIGN && t->v[first].kid[1] >= 0)
        return fafter(t, t->v[first].kid[1], -1, freg);
    /* An element, or a value read through a pointer variable, after an
     * operand that moved the context to AX has no golden: its address is
     * loaded into DI either way. In DI it stays DI - p13_open's "arr[i] +
     * j", p16_open2's "*p + i" ("mov di,*-22.(bp)" / "lea ax,(di)" / ...
     * / "mov di,*-24.(bp)" / "mov ax,di"). */
    if (first_op < 0 && op == OP_STAR && freg == FREG_DI)
        return FREG_DI;
    return FREG_UNKNOWN;
}

static void plan_fvalue_r(Plan *p, const ETree *t, int i, int freg);

/* An operand is_fpreinc() takes ahead of its operator: the increment's
 * opcodes streamed as a statement's would be (its 1.0's .data block
 * first), its value - the variable - dropped; the operand is the NAME
 * from then on (plan_fvalue_r()). */
static void plan_fhoist(Plan *p, const ETree *t, int i)
{
    if (!is_fpreinc(t, i))
        return;
    const ENode *n = &t->v[i];
    plan_value(p, t, n->kid[0]);
    plan_value(p, t, n->kid[1]);
    plan_range(p, n->off, n->end);
    plan_op(p, SEG_FDROP, 0, 0, 0);
}

/* A '+'/'*' chain of type DOUBLE - see this section's header. The
 * rebuilt nodes are matched outermost first, so their constants' .data
 * blocks come out in that order (the innermost node's left operand
 * before its right one), all ahead of the chain's code; each link then
 * replays one of the chain's own opcodes. */
static void plan_fchain(Plan *p, const ETree *t, int top, int freg)
{
    int terms[FCHAIN_MAX], links[FCHAIN_MAX], order[FCHAIN_MAX], nlinks;
    int n = fchain_terms(t, top, terms, links, &nlinks);
    if (n != nlinks + 1)
        gen_fatal("internal: a floating chain is not a binary tree");
    /* A floating zero stays (acommute() tosses only an int or long
     * "+0"): fltprobe/p13_open.s.golden's "d + 0.0" -> ".float 0.0..." /
     * "flds" / "faddd d" - the constant, degree 1, first. */
    for (int k = 0; k < n; k++)
        plan_fhoist(p, t, terms[k]);
    fchain_order(t, terms, n, order);
    for (int k = n - 1; k >= 2; k--)
        plan_fdata(p, t, order[k]);
    plan_fdata(p, t, order[0]);
    plan_fdata(p, t, order[1]);
    plan_fvalue_r(p, t, order[0], freg);
    /* Each term's register context is the one the term evaluated just
     * before it leaves (fafter()), not the chain link's: fltprobe/
     * p15_fltinf.s.golden's "(d * d) + arr[i] + i" - the element, then d *
     * d, then i converted straight in AX ("mov ax,*-46.(bp)" / "call
     * itof"), as after any computed '*'. */
    int ctx = freg;
    for (int k = 1; k < n; k++) {
        ctx = fafter(t, order[k - 1], -1, ctx);
        if (k == 1)
            plan_fload(p, t, order[0], order[1]);
        plan_fvalue_r(p, t, order[k], ctx);
        const ENode *ln = &t->v[links[k - 1]];
        plan_range(p, ln->off, ln->end);
    }
}

static void plan_fvalue(Plan *p, const ETree *t, int i)
{
    plan_fvalue_r(p, t, i, FREG_DI);
}

/* Node i's floating value (or a floating comparison's condition) onto
 * the value stack, in v7's order - see this section's header; `freg` is
 * the register context an int conversion in it is loaded in (see
 * fafter()). */
static void plan_fvalue_r(Plan *p, const ETree *t, int i, int freg)
{
    const ENode *n = &t->v[i];
    if (is_fpreinc(t, i)) {
        /* Hoisted already (plan_fhoist()): the variable itself. */
        plan_range(p, t->v[n->kid[0]].start, t->v[n->kid[0]].end);
        return;
    }
    if (fconst_node(t, i)) {
        /* Any NEG above it is folded into the planned text - its opcode
         * is never streamed. */
        int b = fconst_base(t, i);
        plan_fdata(p, t, b);
        plan_range(p, t->v[b].start, t->v[b].end);
        return;
    }
    if (fnamed(t, i)) {
        int name;
        long disp;
        if (n->op == OP_NAME)
            plan_range(p, n->start, n->end);
        else if (ffold(t, n->kid[0], &name, &disp))
            plan_add(p, (Seg){ SEG_FLEAF, t->v[name].off, 0, (int)disp,
                               n->type, 0 });
        return;
    }
    int l = n->kid[0], r = n->kid[1];
    switch (n->op) {
    case OP_NEG:
        if (is_fneg(t, l)) {
            plan_fvalue_r(p, t, t->v[l].kid[0], freg);  /* -(-x) is x */
            return;
        }
        plan_fvalue_r(p, t, l, freg);
        plan_range(p, n->off, n->end);
        return;
    case OP_ITOF:
        if (freg == FREG_UNKNOWN)
            gen_fatal("an int converted to 'float'/'double' after a computed "
                      "operand other than '+', '-', '*', '/', a unary '-' or "
                      "a call is not yet supported (the register the real "
                      "compiler loads it in is not known) - see "
                      "src/mutos_cc/README.md");
        if (t->v[l].type == TY_UNSIGN) {
            /* v7's unoptim() makes it LTOF(ITOL): a long in DI:SI, the high
             * word cleared - see gen_itof(). Only in DI: after a computed
             * '*' (AX) the REAL compiler writes no valid code - fltprobe/
             * p19_open3.s.golden's "d = (d * e) + u" -> "mov ax,*-36.(bp)"
             * / "mov <garbage>,ax" / "sub ax,ax" / "push <garbage>" / "push
             * ax" / "call ltof", <garbage> 118 bytes of the C library's
             * character-class table (_ctype_, the entries for '\n' up to
             * DEL) where the name of the register after AX should be: its
             * code table asks for a register pair starting at AX, and the
             * register-name table has nothing after AX. mutos_as refuses
             * the line (what the real "as" does with it is not recorded -
             * fltprobe/README.md), and there is no meaningful code to
             * reproduce: refused, on purpose. */
            if (freg != FREG_DI)
                gen_fatal("an 'unsigned' value converted to 'float'/'double' "
                          "after a computed '*', '/' or call: the real MUTOS "
                          "1700 compiler writes an invalid register name here "
                          "(fltprobe/p19_open3.s.golden), so this cannot be "
                          "compiled - convert it in a separate statement - "
                          "see src/mutos_cc/README.md");
            plan_op(p, SEG_FITOF, FREG_ULONG, 0, 0);
        } else {
            plan_op(p, SEG_FITOF, freg, 0, 0);
        }
        plan_value(p, t, l);            /* the int operand */
        plan_range(p, n->off, n->end);
        return;
    case OP_LTOF:
        plan_value(p, t, l);            /* the long operand */
        plan_range(p, n->off, n->end);
        return;
    case OP_PLUS: case OP_TIMES:
        plan_fchain(p, t, i, freg);
        return;
    case OP_MINUS: case OP_DIVIDE:
        /* "d - 0.0" is an ordinary subtraction: "fldd d" / "lea ax,L" /
         * "fsubs" (p13_open). */
        plan_fhoist(p, t, l);
        plan_fhoist(p, t, r);
        plan_fdata(p, t, l);
        plan_fdata(p, t, r);
        plan_fvalue_r(p, t, l, freg);
        plan_fload(p, t, l, r);
        plan_fvalue_r(p, t, r, fafter(t, l, -1, freg));
        plan_range(p, n->off, n->end);
        return;
    case OP_ASSIGN:
        plan_fdata(p, t, r);
        if (fstar_computed(t, l)) {
            /* A target whose address takes code ("arr[i] = d"): the
             * right-hand side onto the stack first, then the address -
             * p8_misc.s.golden's "fldd d" / "lea di,*-38.(bp)" / ... /
             * "add di,si" / "lea ax,(di)" / "call fstdp" (as a pointer
             * variable is loaded only after it - "*p = 2.5"). */
            plan_fvalue_r(p, t, r, freg);
            plan_op(p, SEG_FLOAD, 0, 0, 0);
            plan_fvalue_r(p, t, l, freg);
            plan_op(p, SEG_SWAP2, 0, 0, 0);
            plan_range(p, n->off, n->end);
            return;
        }
        plan_fvalue_r(p, t, l, freg);
        plan_fvalue_r(p, t, r, freg);
        /* A store through a pointer variable is addressed in the register
         * the right-hand side left the context in - fltprobe/p20_fltlv.s.
         * golden's "*p = *p * 2.0" -> "fmuls" / "mov ax,*4.(bp)" / "mov
         * bx,ax" / "lea ax,(bx)" / "call fstdp" (AX after a '*', as an int
         * is converted there - fafter()), "q->y = q->x + 1.5" -> "fadds" /
         * "mov di,*-22.(bp)" / "lea ax,*8.(di)" (DI after a '+'). */
        plan_op(p, SEG_FSTREG, fafter(t, r, -1, freg) == FREG_AX ? FREG_AX
                                                                : FREG_DI,
                0, 0);
        plan_range(p, n->off, n->end);
        return;
    case OP_ASPLUS: case OP_ASMINUS: case OP_ASDIV:
        plan_fdata(p, t, r);
        plan_fvalue_r(p, t, l, freg);
        if (t->v[l].op == OP_STAR && !fnamed(t, l) &&
            (!fleaf(t, r) || n->op == OP_ASDIV)) {
            /* A target reached through a pointer, and a computed right
             * operand or a '/=': the target's address pushed and the
             * target loaded first, the right operand after it, combined
             * on the stack - fltprobe/p24_fltinf2.s.golden's "q->x += d *
             * e;" -> "mov di,*-24.(bp)" / "lea ax,(di)" / "|" / "push ax" /
             * "call fldd" / "lea ax,*-56.(bp)" / "call fldd" / "lea ax,
             * *-64.(bp)" / "call fmuld" / "call fadd" / "pop ax" / "call
             * fstdp", "q->x /= e;" -> ... "call fldd" / "lea ax,*-64.(bp)" /
             * "call fldd" / "call fdiv" (the divisor loaded too, where "q->y
             * -= e" subtracts it from memory - "fsubd"). */
            plan_op(p, SEG_FLOADTP, 0, 0, 0);
            plan_fvalue_r(p, t, r, freg);
            plan_range(p, n->off, n->end);
            return;
        }
        if (!fleaf(t, r)) {
            /* '/=' and '-=' load the target first (p2_arith's "d /= (e +
             * e)"; fltprobe/p19_open3.s.golden's "d -= c" -> "lea ax,
             * *-44.(bp)" / "call fldd" / "movb ax,*-54.(bp)" / "cbw" /
             * "call itof" / "call fsub" / "lea ax,*-44.(bp)" / "call
             * fstdp", "d -= e * 2" the same around "flds" / "fmuld e");
             * '+=' computes the right operand first and adds the target
             * from memory - gen_fp_asop(). */
            if (n->op == OP_ASDIV || n->op == OP_ASMINUS)
                plan_op(p, SEG_FLOADT, 0, 0, 0);
        }
        plan_fvalue_r(p, t, r, freg);
        plan_range(p, n->off, n->end);
        return;
    case OP_ASTIMES:
        plan_fdata(p, t, r);
        if (fstar_computed(t, l)) {
            /* A target whose address takes code: the right operand
             * loaded first, as for any '*=' (see gen_fp_asop()) - before
             * that code: fltprobe/p20_fltlv.s.golden's "a[i] *= s.y" ->
             * "lea ax,*-12.(bp)" / "call fldd" / "lea di,*-46.(bp)" / ... /
             * "add di,si" / "lea ax,(di)" / "|" / "push ax" / "call fmuld"
             * / "pop ax" / "call fstdp". */
            plan_fvalue_r(p, t, r, freg);
            plan_op(p, SEG_FLOAD, 0, 0, 0);
            plan_fvalue_r(p, t, l, freg);
            plan_op(p, SEG_SWAP2, 0, 0, 0);
            plan_range(p, n->off, n->end);
            return;
        }
        plan_fvalue_r(p, t, l, freg);
        plan_fvalue_r(p, t, r, freg);
        plan_range(p, n->off, n->end);
        return;
    case OP_CALL:
        plan_call(p, t, i);
        return;
    case OP_RFORCE:
        plan_fdata(p, t, l);
        plan_fvalue_r(p, t, l, freg);
        plan_range(p, n->off, n->end);
        return;
    case OP_QUEST:
        plan_quest(p, t, i);
        return;
    case OP_SEQNC: {
        /* A comma whose last operand is floating (SEQNC typed DOUBLE): the
         * left operand for its effect, then the right one - fltprobe/
         * p21_fltexp.s.golden's "g = (i = 2, d - 40000.0)" -> "mov *-40.
         * (bp),*2." / ".data" / "L10011: .float 4.0...e+04" / ".text" /
         * "lea ax,*-12.(bp)" / "call fldd" / ... (the constant's block
         * where its '-' is matched, after the left operand's code). */
        int rec = plan_slots(p, 2);
        plan_op(p, SEG_MARK, rec, 0, 0);
        plan_value(p, t, l);
        plan_op(p, SEG_DISCARD, 0, 0, rec);
        plan_fvalue_r(p, t, r, freg);
        return;
    }
    default:
        break;
    }
    if (is_frel(t, i)) {
        int dl = fdeg(t, l), dr = fdeg(t, r);
        int swap = dl < dr || (dl == dr && fnamed(t, l) && !fnamed(t, r));
        int first = swap ? r : l, second = swap ? l : r;
        plan_fdata(p, t, first);
        plan_fdata(p, t, second);
        plan_fvalue_r(p, t, first, freg);
        plan_fload(p, t, first, second);
        plan_fvalue_r(p, t, second, fafter(t, first, -1, freg));
        if (swap)
            plan_op(p, SEG_FSWAP, 0, 0, 0);
        plan_range(p, n->off, n->end);
        return;
    }
    /* Everything else has no plan of its own: its opcodes stream as
     * written, and its handler refuses what it does not know. */
    for (int k = 0; k < 2; k++)
        if (n->kid[k] >= 0)
            plan_value(p, t, n->kid[k]);
    plan_range(p, n->off, n->end);
}

/* Plans the expression's floating constants - see this section's
 * header: every written one in temp1 order, then every int constant
 * converted, each with the next c1 label and its text, a negation above
 * it folded in. */
static void fplan_constants(GenState *g, FILE *t1, const ETree *t)
{
    Plan *p = &g->plan;
    p->nfc = 0;
    fdeg_plan = p;
    long savepos = ftell(t1);
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < t->n; i++) {
            const ENode *n = &t->v[i];
            int is_fcon = (n->op == OP_FCON);
            int is_icon = (n->op == OP_ITOF && ty_isfloat(n->type) &&
                           n->kid[0] >= 0 && t->v[n->kid[0]].op == OP_CON);
            if (pass == 0 ? !is_fcon : !is_icon)
                continue;
            if (p->nfc == FCONST_MAX)
                gen_fatal("too many floating constants in one expression "
                          "(internal limit %d)", FCONST_MAX);
            FConst *c = &p->fc[p->nfc++];
            memset(c, 0, sizeof *c);
            c->off = n->off;
            int neg = 0;
            for (int j = n->parent; j >= 0 && is_fneg(t, j); j = t->v[j].parent)
                neg = !neg;
            int is_zero = 0;
            if (is_fcon) {
                if (fseek(t1, n->off, SEEK_SET) != 0)
                    gen_fatal("internal: temp1 is not seekable (fseek failed)");
                (void)c1_read_op(t1, "temp1");
                int type = c1_read_num(t1, "temp1");
                char *text = c1_read_sym(t1, "temp1");
                if (type != TY_DOUBLE)
                    gen_fatal("FCON of type %d not yet supported (mutos_c0 "
                              "writes every floating constant as a double)",
                              type);
                fconst_text(text, neg, c->text, sizeof c->text, &c->dbl,
                            &is_zero);
                /* A negated zero is written as the zero itself: fltprobe/
                 * p16_open2.s.golden's "e = -0.0;" -> ".float 0.000...e+00"
                 * - the real c1's printf() writes no sign for it. */
                if (neg && is_zero)
                    fconst_text(text, 0, c->text, sizeof c->text, &c->dbl,
                                &is_zero);
                free(text);
            } else {
                long v = t->v[n->kid[0]].aux;
                char num[24];
                snprintf(num, sizeof num, "%ld", v < 0 ? -v : v);
                fconst_text(num, neg != (v < 0), c->text, sizeof c->text,
                            &c->dbl, &is_zero);
                if (neg != (v < 0) && is_zero)
                    fconst_text(num, 0, c->text, sizeof c->text, &c->dbl,
                                &is_zero);
            }
            c->label = g->next_lab++;
        }
    }
    if (fseek(t1, savepos, SEEK_SET) != 0)
        gen_fatal("internal: temp1 is not seekable (fseek failed)");
}

/* The planner's own steps. */
static void fplan_step(GenState *g, FILE *t1, const Seg *s)
{
    switch (s->kind) {
    case SEG_FLEAF: {
        /* The object's NAME, read where it stands; the element's or
         * member's offset added (plan_fvalue()'s folding - v7's optim()
         * makes the whole "*(&x + c)" one NAME). */
        long pos = ftell(t1);
        if (fseek(t1, s->start, SEEK_SET) != 0)
            gen_fatal("internal: temp1 is not seekable (fseek failed)");
        (void)c1_read_op(t1, "temp1");
        int hclass = c1_read_num(t1, "temp1");
        (void)c1_read_num(t1, "temp1");
        Val fv = {0};
        fv.kind = VK_FMEM;
        fv.fdouble = (s->b == TY_DOUBLE);
        if (hclass == SC_EXTERN) {
            fv.fmode = FM_SYM;
            fv.sym = intern_name(g, c1_read_sym(t1, "temp1"));
            fv.imm = s->a;
        } else if (hclass == SC_AUTO) {
            fv.fmode = FM_BP;
            fv.offset = c1_read_num(t1, "temp1") + s->a;
        } else {
            gen_fatal("an element or member of a 'float'/'double' object of "
                      "this storage class is not yet supported - see "
                      "src/mutos_cc/README.md");
        }
        if (fseek(t1, pos, SEEK_SET) != 0)
            gen_fatal("internal: temp1 is not seekable (fseek failed)");
        push_val(g, fv);
        return;
    }
    case SEG_FDATA:
        put_float_data(g, &g->plan.fc[s->a]);
        return;
    case SEG_FLOADTP: {
        /* See gen_fp_asop(): the target's address into AX, the empty "|"
         * line, "push ax", the target loaded - ahead of the right operand
         * (fltprobe/p24_fltinf2.s.golden's "q->x += d * e;" and "q->x /=
         * e;"). */
        if (g->valsp < 1)
            gen_fatal("expression stack underflow - malformed temp1 stream");
        Val *top = &g->valstack[g->valsp - 1];
        if (top->kind != VK_FMEM || top->fmode != FM_IND)
            gen_fatal("internal: a compound assignment's target is not "
                      "reached through a pointer");
        fp_lea(g, *top);
        put_line(g, "|");
        ins1(g, "push", o_reg("ax"));
        char name[8];
        snprintf(name, sizeof name, "fld%c", fp_suffix(*top));
        ins1(g, "call", o_sym(name));
        g->nfloat = 1;
        top->fonstk = 1;
        top->fpushed = 1;
        return;
    }
    case SEG_FLOAD:
    case SEG_FLOADT: {
        if (g->valsp < 1)
            gen_fatal("expression stack underflow - malformed temp1 stream");
        Val *top = &g->valstack[g->valsp - 1];
        if (!is_float_val(top))
            gen_fatal("internal: a floating load step without a floating "
                      "operand on top");
        fp_load(g, *top);
        if (s->kind == SEG_FLOADT) {
            if (top->kind != VK_FMEM)
                gen_fatal("internal: a compound assignment's target is not "
                          "in memory");
            top->fonstk = 1;
        } else {
            *top = val_facc();
        }
        return;
    }
    case SEG_FSWAP:
        g->fswap = 1;
        return;
    case SEG_FITOF:
        g->itof_reg = s->a;
        return;
    case SEG_FSTREG:
        g->fst_reg = s->a;
        return;
    case SEG_FDROP: {
        Val v = pop_val_ex(g, POP_FLOAT);
        if (v.kind != VK_FMEM)
            gen_fatal("internal: a hoisted floating '++'/'--' left no variable");
        return;
    }
    default:
        gen_fatal("internal: unknown floating plan step %d", (int)s->kind);
    }
}

/* A relational or equality operator over floating operands, generated
 * in the planned order (plan_fvalue()): both loaded - the first below -
 * "call fcmp" pops them and leaves the flags of first - second in AH,
 * and "sahf" moves them into the flags; when the plan exchanged the
 * operands (SEG_FSWAP), the relation is mirrored. The result is a
 * VK_COND a branch reads straight away (gen_cond_branch()) or a value
 * materializes (materialize()). */
static void gen_fp_compare(GenState *g, int op)
{
    Val r = pop_float(g, "floating comparison");
    Val l = pop_float(g, "floating comparison");
    int relop = g->fswap ? find_relop(op)->mirror : op;
    g->fswap = 0;
    if (l.kind != VK_FACC && r.kind == VK_FACC)
        gen_fatal("internal: a floating comparison's first operand is not "
                  "on the stack below its computed second one");
    fp_load(g, l);
    fp_load(g, r);
    ins1(g, "call", o_sym("fcmp"));
    ins0(g, "sahf");
    g->nfloat = 1;
    Val c = {0};
    c.kind = VK_COND;
    c.true_op = relop;
    c.cond_is_float = 1;
    c.flags_at = g->ninsn;
    push_val(g, c);
}

/* OP_NEG typed FLOAT or DOUBLE (v7's build() types a unary operator with
 * its operand's type): the operand onto the stack, "call fneg" - atof.o's
 * "fl = -fl", 04_fltconst's "f = -f". A negated constant and a double
 * negation never get here (plan_fvalue()). */
static void gen_fp_neg(GenState *g)
{
    Val v = pop_float(g, "floating unary '-'");
    fp_load(g, v);
    ins1(g, "call", o_sym("fneg"));
    g->nfloat = 1;
    push_val(g, val_facc());
}

/* Stores the value on top of the stack into floating target `lhs` and
 * pops it ("lea ax,<target>" / "call fst<s|d>p"), or - its value used,
 * the consumer is not the statement's end - keeps it ("fst<s|d>") as
 * the result: ecvt.o's "(fj = arg*10) < 1", p2_arith's "e = (d *= e)". */
static void fp_store(GenState *g, FILE *t1, Val lhs)
{
    Consumer c = scan_consumer(t1);
    int keep = (c.op != OP_EXPR);
    /* An assignment whose value is a call argument: the REAL compiler
     * sometimes stores it WITH a pop, as for a statement, and then pushes
     * the argument from the stack all the same - its code pops the value
     * twice, and its own stack model reports it (see fp_track()):
     * fltprobe/p21_fltexp.s.golden's "f = half(d = 3.0);" -> "lea
     * ax,L10013" / "call flds" / "lea ax,*-12.(bp)" / "call fstdp" / "sub
     * sp,*8" / "mov ax,sp" / "call fstdp" / "call _half", and "56:
     * floating point stack underflow". Reproduced as it is - the same
     * code and the same error, exit status 1. Which store it picks
     * depends on what the CALL's own value is used for: stored with a pop
     * when the call's value goes nowhere ("half(d = 3.0);" - fltprobe/
     * p28_fltstk.s.golden) or straight into the statement's own floating
     * store (p21's "f = ..."); kept ("fstd" - right code) under anything
     * else - an int conversion ("x = half(d = 3.0);", p28 and p30_fltstk2
     * - also right after a floating statement), a floating sum ("e =
     * half(d = 3.0) + 1.0;", although the statement is floating), a
     * comparison as a condition or as a value, a double function's
     * "return half(d = 3.0);", and both of "x = two(d = 1.0, e = 2.0);"
     * (all of p30_fltstk2.s.golden; its "cc -S" printed no message). The
     * nodes are found by plan_expression() (argpop_end). A call of an int
     * function, nested calls and other stores than ASSIGN have no golden:
     * kept. */
    int argpop = 0;
    if (keep && (c.op == OP_CALL || c.op == OP_COMMA)) {
        long here = ftell(t1);
        for (int i = 0; i < g->nargpop; i++)
            if (g->argpop_end[i] == here)
                argpop = 1;
    }
    if (argpop) {
        g->fp_argpop = 1;
        g->stmt_argpops++;
    }
    char fn[8];
    snprintf(fn, sizeof fn, (keep && !argpop) ? "fst%c" : "fst%cp",
             fp_suffix(lhs));
    if (lhs.kind == VK_FMEM && lhs.fmode == FM_IND && g->fst_reg == 1) {
        /* A store after a value computed in AX's context (SEG_FSTREG):
         * the pointer variable through AX into BX - fltprobe/p20_fltlv.s.
         * golden's "*p = *p * 2.0" -> "mov ax,*4.(bp)" / "mov bx,ax" /
         * "lea ax,(bx)" / "call fstdp". */
        Val ptr = val_from_simple(lhs.cl);
        if (!(ptr.kind == VK_MEM || ptr.kind == VK_STATIC) || lhs.imm != 0)
            gen_fatal("a 'float'/'double' store through this address after a "
                      "value computed in AX's context is not yet supported - "
                      "see src/mutos_cc/README.md");
        ins2(g, "mov", o_reg("ax"), o_val(ptr));
        ins2(g, "mov", o_reg("bx"), o_reg("ax"));
        ins2(g, "lea", o_reg("ax"), o_sym("(bx)"));
    } else if (lhs.kind == VK_FMEM && lhs.fmode == FM_IND &&
               lhs.cl.kind == VK_REG && strcmp(lhs.cl.reg, "ax") == 0 &&
               lhs.imm == 0) {
        /* A store through a call's result: into DI, not BX as for a read
         * (fp_lea()) - fltprobe/p24_fltinf2.s.golden's "*pick(a, 1) = 2.5;"
         * -> ... "call _pick" / "add sp,*4." / "mov di,ax" / "lea ax,(di)"
         * / "call fstdp" (v7's store template: the address into R). */
        if (g->reserved & RB_DI)
            gen_fatal("a 'float'/'double' store through a call's result while "
                      "DI holds a register variable is not yet supported - see "
                      "src/mutos_cc/README.md");
        ins2(g, "mov", o_reg("di"), o_reg("ax"));
        ins2(g, "lea", o_reg("ax"), o_sym("(di)"));
    } else {
        fp_lea(g, lhs);
    }
    g->fst_reg = 0;
    ins1(g, "call", o_sym(fn));
    g->nfloat = 1;
    if (keep) {
        push_val(g, val_facc());
        return;
    }
    Val res = {0};
    res.kind = VK_FDONE;
    push_val(g, res);
}

/* A floating compound assignment into a float or double variable - see
 * this section's header: "+=", "-=", "/=" with the target loaded first
 * when the right operand is in memory, "/=" also ahead of a computed right
 * operand (SEG_FLOADT); "*=", and "+=" with a computed right operand, with
 * the right operand loaded and the target combined from memory -
 * fltprobe/p16_open2.s.golden's "d += c" -> "movb ax,*-30.(bp)" / "cbw" /
 * "call itof" / "lea ax,*-12.(bp)" / "call faddd" (v7's efftab "%a,n":
 * the right operand first, then the target into the next register). */
static void gen_fp_asop(GenState *g, FILE *t1, int op)
{
    const char *fn = fp_opname(op);
    if (!fn)
        gen_fatal("'%%=' on a 'float'/'double' variable is not defined - see "
                  "src/mutos_cc/README.md");
    Val rhs = pop_float(g, "floating compound assignment");
    Val lhs = pop_val_ex(g, POP_FLOAT);
    if (lhs.kind != VK_FMEM)
        gen_fatal("a compound assignment to this 'float'/'double' target is "
                  "not yet supported (only a variable) - see "
                  "src/mutos_cc/README.md");
    char name[8];
    if (lhs.fmode == FM_IND) {
        /* A target reached through a pointer or a computed address: its
         * address into AX, an empty "|" comment line, the address kept on
         * the machine stack while the runtime is called, popped back for
         * the store - fltprobe/p20_fltlv.s.golden's "q->x += 0.5" -> "mov
         * di,*-22.(bp)" / "lea ax,(di)" / "|" / "push ax" / "call fldd" /
         * "lea ax,L10004" / "call fadds" / "pop ax" / "call fstdp", "a[i] +=
         * 1.5" the same after the element's address, and "a[i] *= s.y" ->
         * (s.y loaded first) ... "lea ax,(di)" / "|" / "push ax" / "call
         * fmuld" / "pop ax" / "call fstdp" - v7's code table for a
         * compound assignment through a register. A computed right operand
         * other than for '*=' has no golden.
         *
         * Its value used: the right operand loaded first, the target
         * combined from memory and stored without a pop, the value left
         * on the stack - fltprobe/p33_fltinf3.s.golden's "e = (q->x +=
         * d);" -> "lea ax,*-64.(bp)" / "call fldd" / "mov di,*-24.(bp)" /
         * "lea ax,(di)" / "|" / "push ax" / "call faddd" / "pop ax" / "call
         * fstd" / "lea ax,*-72.(bp)" / "call fstdp" ('*=' by inference the
         * same, its right operand loaded first by the plan as for a
         * statement; '-=' and '/=' would need the operands the other way
         * round - no golden). */
        int used = (scan_consumer(t1).op != OP_EXPR);
        if (used && !lhs.fpushed &&
            ((op == OP_ASPLUS && rhs.kind != VK_FACC) || op == OP_ASTIMES)) {
            fp_load(g, rhs);
            fp_lea(g, lhs);
            put_line(g, "|");
            ins1(g, "push", o_reg("ax"));
            snprintf(name, sizeof name, "%s%c", fn, fp_suffix(lhs));
            ins1(g, "call", o_sym(name));
            ins1(g, "pop", o_reg("ax"));
            snprintf(name, sizeof name, "fst%c", fp_suffix(lhs));
            ins1(g, "call", o_sym(name));
            g->nfloat = 1;
            push_val(g, val_facc());
            return;
        }
        if (used)
            gen_fatal("the value of this compound assignment through a pointer "
                      "to a 'float'/'double' is not yet supported (only '+=' "
                      "of a variable or a constant, or '*=') - see "
                      "src/mutos_cc/README.md");
        if (lhs.fpushed) {
            /* Pushed and loaded already (SEG_FLOADTP): a computed right
             * operand combined on the stack ("call fadd"); a divisor in
             * memory loaded first ("call fldd" / "call fdiv"). */
            if (rhs.kind != VK_FACC) {
                if (op != OP_ASDIV)
                    gen_fatal("internal: a pushed compound-assignment target "
                              "with a right operand in memory");
                fp_load(g, rhs);
            }
            ins1(g, "call", o_sym(fn));
        } else if (op == OP_ASTIMES) {
            fp_load(g, rhs);
            fp_lea(g, lhs);
            put_line(g, "|");
            ins1(g, "push", o_reg("ax"));
            snprintf(name, sizeof name, "%s%c", fn, fp_suffix(lhs));
            ins1(g, "call", o_sym(name));
        } else {
            if (rhs.kind == VK_FACC)
                gen_fatal("a compound assignment through a pointer to a "
                          "'float'/'double' with a computed right operand is "
                          "not yet supported - see src/mutos_cc/README.md");
            fp_lea(g, lhs);
            put_line(g, "|");
            ins1(g, "push", o_reg("ax"));
            snprintf(name, sizeof name, "fld%c", fp_suffix(lhs));
            ins1(g, "call", o_sym(name));
            snprintf(name, sizeof name, "%s%c", fn, fp_suffix(rhs));
            fp_lea(g, rhs);
            ins1(g, "call", o_sym(name));
        }
        ins1(g, "pop", o_reg("ax"));
        snprintf(name, sizeof name, "fst%cp", fp_suffix(lhs));
        ins1(g, "call", o_sym(name));
        g->nfloat = 1;
        Val res = {0};
        res.kind = VK_FDONE;
        push_val(g, res);
        return;
    }
    if (op == OP_ASTIMES ||
        (op == OP_ASPLUS && rhs.kind == VK_FACC && !lhs.fonstk)) {
        fp_load(g, rhs);
        snprintf(name, sizeof name, "%s%c", fn, fp_suffix(lhs));
        fp_lea(g, lhs);
        ins1(g, "call", o_sym(name));
    } else if (rhs.kind == VK_FACC) {
        if (!lhs.fonstk)
            gen_fatal("internal: a compound assignment's target is not on "
                      "the stack below its computed right operand");
        ins1(g, "call", o_sym(fn));
    } else {
        fp_load(g, lhs);
        snprintf(name, sizeof name, "%s%c", fn, fp_suffix(rhs));
        fp_lea(g, rhs);
        ins1(g, "call", o_sym(name));
    }
    lhs.fonstk = 0;
    fp_store(g, t1, lhs);
}

/* OP_ITOF: an int onto the floating-point stack - "itof" takes it in AX
 * (see this section's header), loaded there in the register context the
 * plan set (SEG_FITOF - see fafter()): through DI, "mov di,i / mov ax,di"
 * (a register variable in DI through SI: p5_call's "mov si,di / mov
 * ax,si"), or straight into AX, "mov ax,c"; a product (in AX already,
 * p2_arith's "mov ax,i / imul j / call itof"), a char ("movb ax,c /
 * cbw", p5_call) and "x + c" computed for AX (OP_PLUS) need no move. A
 * constant was planned as a floating constant (fplan_constants()). */
static void gen_itof(GenState *g, int type)
{
    if (type != TY_DOUBLE && type != TY_FLOAT)
        gen_fatal("ITOF of type %d not yet supported", type);
    int reg = g->itof_reg;
    g->itof_reg = FREG_DI;
    Val v = pop_val_ex(g, POP_CHARX);
    if (v.kind == VK_IMM) {
        FConst *c = fc_find(g, g->op_off);
        if (!c)
            gen_fatal("internal: a converted int constant outside a planned "
                      "expression");
        push_val(g, fconst_val(g, c));
        return;
    }
    v = materialize(g, v);
    if (reg == FREG_ULONG) {
        /* An unsigned variable: v7's unoptim() makes ITOF of an unsigned
         * LTOF(ITOL), and ITOL of an unsigned clears the high word - the
         * long in DI:SI (high, low), pushed low word first for "ltof" as a
         * long variable is (gen_ltof()): fltprobe/p16_open2.s.golden's "d
         * = u" -> "mov si,*-32.(bp)" / "sub di,di" / "push si" / "push di"
         * / "call ltof" / "add sp,*4". */
        if (!(v.kind == VK_MEM || v.kind == VK_STATIC) || v.bytev || v.structv)
            gen_fatal("converting this 'unsigned' operand to 'float'/'double' "
                      "is not yet supported (only a variable) - see "
                      "src/mutos_cc/README.md");
        if (g->reserved & (RB_DI | RB_SI))
            gen_fatal("converting an 'unsigned' variable to 'float'/'double' "
                      "while DI or SI holds a register variable is not yet "
                      "supported - see src/mutos_cc/README.md");
        ins2(g, "mov", o_reg("si"), o_val(v));
        ins2(g, "sub", o_reg("di"), o_reg("di"));
        ins1(g, "push", o_reg("si"));
        ins1(g, "push", o_reg("di"));
        ins1(g, "call", o_sym("ltof"));
        ins2(g, "add", o_reg("sp"), o_sym("*4"));
        g->nfloat = 1;
        push_val(g, val_facc());
        return;
    }
    if (v.kind == VK_IND && !v.bytev && !v.sym && reg == FREG_DI &&
        strcmp(v.reg, "di") == 0) {
        /* An int read through a pointer in DI - a member: loaded in place,
         * then moved to AX - fltprobe/p24_fltinf2.s.golden's "d = q->k;"
         * -> "mov di,*-24.(bp)" / "mov di,*16.(di)" / "mov ax,di" / "call
         * itof". */
        ins2(g, "mov", o_reg("di"), o_val(v));
        ins2(g, "mov", o_reg("ax"), o_reg("di"));
    } else if (v.kind == VK_CHARX) {
        (void)load_charx(g, v);
    } else if ((v.kind == VK_MEM || v.kind == VK_STATIC) && !v.bytev &&
               !v.structv) {
        if (reg == FREG_AX) {
            ins2(g, "mov", o_reg("ax"), o_val(v));
        } else {
            if (g->reserved & RB_DI)
                gen_fatal("converting an int variable to 'float'/'double' "
                          "while DI holds a register variable is not yet "
                          "supported - see src/mutos_cc/README.md");
            load_into_di(g, v);
            ins2(g, "mov", o_reg("ax"), o_reg("di"));
        }
    } else if (v.kind == VK_REG && v.regvar && strcmp(v.reg, "di") == 0 &&
               reg == FREG_DI) {
        ins2(g, "mov", o_reg("si"), o_reg("di"));
        ins2(g, "mov", o_reg("ax"), o_reg("si"));
    } else if (v.kind == VK_REG && !v.regvar && strcmp(v.reg, "di") == 0 &&
               reg == FREG_DI) {
        /* Computed in DI, the ordinary working register: moved to AX -
         * p7_itofreg.s.golden's "d + (i + j)" -> "mov di,*-30.(bp)" /
         * "add di,*-32.(bp)" / "mov ax,di" / "call itof". */
        ins2(g, "mov", o_reg("ax"), o_reg("di"));
    } else if (v.kind == VK_REG && !v.regvar && strcmp(v.reg, "dx") == 0) {
        /* A remainder, left in DX by "idiv": moved to AX straight away -
         * fltprobe/p16_open2.s.golden's "d * (i % j)" -> "mov ax,*-24.
         * (bp)" / "cwd" / "idiv *-26.(bp)" / "mov ax,dx" / "call itof"
         * (in DI context; after a '*' by inference the same). */
        ins2(g, "mov", o_reg("ax"), o_reg("dx"));
    } else if (!(v.kind == VK_REG && strcmp(v.reg, "ax") == 0 && !v.regvar)) {
        gen_fatal("converting this int operand to 'float'/'double' is not yet "
                  "supported (only a variable, a constant, a char, a product, "
                  "a variable plus or minus a constant or a sum of two "
                  "variables) - see src/mutos_cc/README.md");
    }
    ins1(g, "call", o_sym("itof"));
    g->nfloat = 1;
    push_val(g, val_facc());
}

/* OP_LTOF: a 'long' variable onto the floating-point stack - lconvert.o's
 * "ltof" takes it on the machine stack, the low word pushed first (the
 * high word ends up at the lower address, as a long is kept in memory),
 * each through the working register, and the caller pops it:
 * fltprobe/p5_call.s.golden's "d = l;" -> "mov si,*-22.(bp)" / "push
 * si" / "mov si,*-24.(bp)" / "push si" / "call ltof" / "add sp,*4" (no
 * decimal point, like push_fp_arg()'s "*8"). That function has a
 * register variable in DI, so the working register there is SI; with DI
 * free it is DI - the same register v7's allocation hands out first
 * everywhere else (an inference, not yet a golden). */
static void gen_ltof(GenState *g, int type)
{
    if (type != TY_DOUBLE && type != TY_FLOAT)
        gen_fatal("LTOF of type %d not yet supported", type);
    Val v = pop_val(g);
    if (v.kind == VK_LONG && v.lreg == LREG_DISI && !v.lpair) {
        /* A long just computed in DI:SI: pushed from there, low word
         * first - fltprobe/p21_fltexp.s.golden's "e = l + 1" -> ... "add
         * si,cx" / "adc di,bx" / "push si" / "push di" / "call ltof" /
         * "add sp,*4". */
        ins1(g, "push", o_reg("si"));
        ins1(g, "push", o_reg("di"));
        ins1(g, "call", o_sym("ltof"));
        ins2(g, "add", o_reg("sp"), o_sym("*4"));
        g->nfloat = 1;
        push_val(g, val_facc());
        return;
    }
    if (v.kind != VK_MEM)
        gen_fatal("converting this 'long' operand to 'float'/'double' is "
                  "not yet supported (only a 'long' variable or a value "
                  "computed in DI:SI) - see src/mutos_cc/README.md");
    const char *r = (g->reserved & RB_DI) ? "si" : "di";
    ins2(g, "mov", o_reg(r), o_mem(v.offset + MCC_SZINT));
    ins1(g, "push", o_reg(r));
    ins2(g, "mov", o_reg(r), o_mem(v.offset));
    ins1(g, "push", o_reg(r));
    ins1(g, "call", o_sym("ltof"));
    ins2(g, "add", o_reg("sp"), o_sym("*4"));
    g->nfloat = 1;
    push_val(g, val_facc());
}

/* OP_FTOI/OP_FTOL: the value onto the stack, then "call ftoi" (the int in
 * AX) or "call ftol" (the long in DX:AX, moved on to DI:SI like any long
 * result - 02_dblconv.s.golden's "call ftol" / "mov di,dx" / "mov si,ax").
 * A constant is not folded but converted at run time like any operand:
 * fltprobe/p16_open2.s.golden's "(int) 2.5" and "i = 2.5" -> ".float
 * 2.5..." / "lea ax,L10003" / "call flds" / "call ftoi" (FTOL by
 * inference). */
static void gen_fp_to_int(GenState *g, int op, int type)
{
    if (op == OP_FTOI && type == TY_UNSIGN) {
        /* To an unsigned: v7's unoptim() makes FTOI(UNSIGN) LTOI(FTOL) -
         * the long's low word, AX - fltprobe/p21_fltexp.s.golden's "u =
         * (unsigned) d" -> "lea ax,*-12.(bp)" / "call fldd" / "call ftol"
         * / "mov *-38.(bp),ax". */
        Val v = pop_float(g, "FTOI");
        fp_load(g, v);
        ins1(g, "call", o_sym("ftol"));
        push_val(g, val_reg("ax"));
        return;
    }
    if ((op == OP_FTOI && type != TY_INT) || (op == OP_FTOL && type != TY_LONG))
        gen_fatal("%s of type %d not yet supported", op == OP_FTOI ? "FTOI"
                                                                   : "FTOL", type);
    Val v = pop_float(g, op == OP_FTOI ? "FTOI" : "FTOL");
    fp_load(g, v);
    ins1(g, "call", o_sym(op == OP_FTOI ? "ftoi" : "ftol"));
    if (op == OP_FTOI)
        push_val(g, val_reg("ax"));
    else
        push_val(g, emit_dxax_to_long(g));
}

/* OP_INCBEF/OP_DECBEF/OP_INCAFT/OP_DECAFT typed DOUBLE - its operand a
 * double in memory, its amount the constant 1.0 (mutos_c0's ITOF of CON
 * 1, a ".float" planned as any constant, its .data block written ahead):
 * the variable loaded, the 1.0 added or subtracted from memory and the
 * sum stored back - fltprobe/p21_fltexp.s.golden's "d++;" and "++d;" ->
 * "lea ax,*-12.(bp)" / "call fldd" / "lea ax,L10002" / "call fadds" /
 * "lea ax,*-12.(bp)" / "call fstdp". A postfix one whose value is used
 * keeps the old value on the stack under the new one ("fdup"): "e =
 * d--;" -> "fldd d" / "call fdup" / "lea ax,L10004" / "call fsubs" /
 * "fstdp d" / "fstdp e". A prefix one whose value is used stores the new
 * value without popping it ("fstd" - an inference, as for "e = (d *=
 * e)"). */
static void gen_fp_incdec(GenState *g, FILE *t1, int op)
{
    Val amt = pop_float(g, "floating '++'/'--'");
    Val lv = pop_val_ex(g, POP_FLOAT);
    int used = (scan_consumer(t1).op != OP_EXPR);
    int after = (op == OP_INCAFT || op == OP_DECAFT);
    int incr = (op == OP_INCBEF || op == OP_INCAFT);
    /* A float variable the same way - fltprobe/p24_fltinf2.s.golden's
     * "f++;" -> "lea ax,*-68.(bp)" / "call flds" / "lea ax,L10030" / "call
     * fadds" / "lea ax,*-68.(bp)" / "call fstsp" (its 1.0 a ".float"), and
     * its value used, as a double's: p33_fltinf3.s.golden's "e = f++;" ->
     * "flds f" / "call fdup" / "fadds 1.0" / "fstsp f" / "fstdp e", "d =
     * ++f;" -> "flds f" / "fadds 1.0" / "fstsp f" / "flds f" / "fstdp d". */
    if (lv.kind != VK_FMEM || lv.fmode == FM_IND || amt.kind != VK_FCON)
        gen_fatal("'++'/'--' on this 'float'/'double' operand is not yet "
                  "supported (only a 'float' or 'double' variable) - see "
                  "src/mutos_cc/README.md");
    fp_load(g, lv);
    if (after && used)
        ins1(g, "call", o_sym("fdup"));
    char fn[8];
    snprintf(fn, sizeof fn, "%s%c", incr ? "fadd" : "fsub", fp_suffix(amt));
    fp_lea(g, amt);
    ins1(g, "call", o_sym(fn));
    /* A prefix one's value: stored with a pop, the variable itself is the
     * value, loaded where it is used - fltprobe/p24_fltinf2.s.golden's "e
     * = ++d;" -> "lea ax,*-56.(bp)" / "call fldd" / "lea ax,L10008" / "call
     * fadds" / "lea ax,*-56.(bp)" / "call fstdp" / "lea ax,*-56.(bp)" /
     * "call fldd" / "lea ax,*-64.(bp)" / "call fstdp" (v7: the result of
     * a prefix '++' is its lvalue). */
    snprintf(fn, sizeof fn, "fst%cp", fp_suffix(lv));
    fp_lea(g, lv);
    ins1(g, "call", o_sym(fn));
    g->nfloat = 1;
    if (used && !after) {
        Val v = lv;
        v.fonstk = 0;
        v.fpushed = 0;
        push_val(g, v);
        return;
    }
    if (used) {
        push_val(g, val_facc());
        return;
    }
    Val res = {0};
    res.kind = VK_FDONE;
    push_val(g, res);
}

/* OP_ASSIGN typed DOUBLE: the right-hand side onto the stack, then
 * stored into the target (fp_store()). */
static void gen_fp_assign(GenState *g, FILE *t1)
{
    Val rhs = pop_float(g, "floating assignment");
    Val lhs = pop_val_ex(g, POP_FLOAT);
    if (lhs.kind != VK_FMEM)
        gen_fatal("assigning to this 'float'/'double' target is not yet "
                  "supported (only a local variable or a parameter) - see "
                  "src/mutos_cc/README.md");
    fp_load(g, rhs);
    fp_store(g, t1, lhs);
}

/* OP_RFORCE typed DOUBLE or FLOAT - "return <floating value>;" in a
 * function returning a double or a float (v7/cc/c04.c's doret() types
 * RFORCE with the converted value's own type): the value onto the stack,
 * stored and popped into dmath.o's "fac", and its address left in AX -
 * atof.o's "return(fl)": "lea ax,fl / call fldd / lea ax,fac / call
 * fstdp / lea ax,fac" (p5_call's float function returns the same way). */
static void gen_fp_rforce(GenState *g)
{
    Val v = pop_float(g, "a returned floating value");
    fp_load(g, v);
    ins2(g, "lea", o_reg("ax"), o_sym("fac"));
    g->fp_nocheck = 1;                  /* see fp_track() */
    ins1(g, "call", o_sym("fstdp"));
    ins2(g, "lea", o_reg("ax"), o_sym("fac"));
    g->nfloat = 1;
}

/* Pushes a floating call argument - a double, which is what a float
 * becomes on the runtime's stack - onto the machine stack: the value
 * onto the floating-point stack, 8 bytes of machine stack reserved and
 * the value stored and popped into them - "lea ax,*-12.(bp) / call fldd
 * / sub sp,*8 / mov ax,sp / call fstdp" (06_fltfunc, p4_const, p5_call:
 * "*8", no decimal point, unlike every other constant). Returns the
 * words pushed, 4. */
static int push_fp_arg(GenState *g, Val v)
{
    fp_load(g, v);
    ins2(g, "sub", o_reg("sp"), o_sym("*8"));
    ins2(g, "mov", o_reg("ax"), o_reg("sp"));
    g->fp_nocheck = 1;                  /* see fp_track() */
    ins1(g, "call", o_sym("fstdp"));
    g->nfloat = 1;
    return 4;
}

/* A streamed call (no plan) never has a floating argument: every
 * expression with one is planned, and plan_call() pushes it. */
static void check_fp_args(const Val *items, int nargs)
{
    for (int i = 0; i < nargs; i++)
        if (is_float_val(&items[i]))
            gen_fatal("internal: a floating call argument outside a plan");
}

int c1_generate(FILE *temp1, FILE *temp2, FILE *out)
{
    GenState g = {0};
    g.out = out;
    g.next_lab = 10000; /* see GenState's next_lab field comment */
    g.nltest = 0;

    for (;;) {
        /* Evaluation order - see that section: an active plan decides
         * where the next opcode is read from; otherwise, at the start
         * of each expression, check whether it needs one. */
        plan_step(&g, temp1);
        /* A file-scope floating initializer is not an expression (see
         * gen_finit()); written whole here, its EXPR read next. */
        if (!g.plan.active && g.valsp == 0 && gen_finit(&g, temp1))
            continue;
        if (!g.plan.active && g.valsp == 0 && plan_expression(&g, temp1))
            plan_step(&g, temp1);

        g.op_off = ftell(temp1);
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
            /* The floating-point stack model (fdepth) is NOT reset here -
             * the real c1's runs on across functions: see fp_track(). */
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

        case OP_DATA:
            /* Opens a file-scope variable's initialized data - v7/cc/
             * c02.c's extdef() writes DATA, NLABEL <name> and the
             * initializer (for "char name[] = \"...\"", putstr()'s BDATA
             * runs - see OP_BDATA). Confirmed against 10_integ/
             * 01_wordcount.s.golden's ".data" / "_text:.byte\t/74,...":
             * no arguments of its own on the wire. */
            ins0(&g, ".data");
            break;

        case OP_BDATA:
            /* A run of an initializer's bytes, in temp1 - see
             * put_bdata_run(); temp2's string literals use the same
             * renderer (gen_strings()). */
            put_bdata_run(&g, temp1, "temp1");
            break;

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

        case OP_CSPACE: {
            /* A file-scope variable with no storage class ("int
             * counter;" - v7/cc's DEFXTRN): a common block of `size`
             * bytes - confirmed against 07_scope/01_globstat.s.golden's
             * ".comm\t_counter,2" and 03_externdef.s.golden's
             * ".comm\t_total,2" (between two functions, where the
             * declaration is). The size has NO trailing "." - unlike
             * .blkb's - and is decimal: the kernel sources' own
             * ".comm\t_msgbuf,1024" / ".comm\t_dk_time,128" (tests/
             * mutos_as/kernel_nonopt/'s .s files) rule out v7's
             * octal. */
            char *name = c1_read_sym(temp1, "temp1");
            int size = c1_read_num(temp1, "temp1");
            ins2(&g, ".comm", o_sym(name), o_fmt("%d", size));
            free(name);
            break;
        }

        case OP_NLABEL: {
            /* A named data label - a file-scope 'static' variable's
             * BSS block ("static int hidden;": BSS, NLABEL "_hidden",
             * SSPACE 2 -> ".bss" / "_hidden:.blkb\t2." - 07_scope/
             * 01_globstat.s.golden), or an initialized array's DATA
             * block ("_text:.byte\t/74,..." - 10_integ/01_wordcount.s.
             * golden). Glued onto the .blkb/.byte line like an "L<n>:"
             * label (put_name_label()); a ".globl" comes from the
             * SYMDEF a non-static definition writes first. */
            char *name = c1_read_sym(temp1, "temp1");
            put_name_label(&g, name);
            free(name);
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
            if (hclass == SC_EXTERN && !ty_is_func(type) &&
                (type == TY_FLOAT || type == TY_DOUBLE)) {
                /* A file-scope float or double - see "Floating point". */
                Val fv = {0};
                fv.kind = VK_FMEM;
                fv.fmode = FM_SYM;
                fv.sym = intern_name(&g, c1_read_sym(temp1, "temp1"));
                fv.fdouble = (type == TY_DOUBLE);
                push_val(&g, fv);
                break;
            }
            if (hclass == SC_EXTERN && !ty_is_func(type)) {
                /* A file-scope variable ("int counter;", "static int
                 * hidden;", "extern int total;" - every file-scope name
                 * is EXTERN on the wire, whatever its storage class):
                 * a memory operand named by its symbol, VK_STATIC with
                 * `sym` set - see VK_STATIC's comment. Confirmed for
                 * int against 07_scope/01_globstat.s.golden and
                 * 03_externdef.s.golden; a char one is a byte operand
                 * like a char local ("movb dx,#_amxcmd(bx)" - a char
                 * array element - in kernel_nonopt/amx.s), and a long
                 * one is refused by every long consumer (they take a
                 * bp-relative VK_MEM only, as for a local static). */
                if (!ty_is_word(type) && type != TY_CHAR && type != TY_LONG)
                    gen_fatal("NAME of type %d not yet supported (only "
                              "int/char/long/pointer file-scope variables "
                              "are covered so far)", type);
                Val gv = {0};
                gv.kind = VK_STATIC;
                gv.sym = intern_name(&g, c1_read_sym(temp1, "temp1"));
                gv.bytev = (type == TY_CHAR); /* see Val's `bytev` */
                push_val(&g, gv);
                break;
            }
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
                if (!(ty_is_word(ty_decref(type)) || ty_decref(type) == TY_LONG ||
                      ty_decref(type) == TY_DOUBLE ||
                      ty_decref(type) == TY_FLOAT))
                    gen_fatal("NAME with storage class SC_EXTERN and "
                              "type %d not yet supported (only a called "
                              "function returning an int, a long, a float, a "
                              "double or a pointer is covered so far)", type);
                char *name = c1_read_sym(temp1, "temp1");
                push_val(&g, val_func(name));
                break;
            }
            if (hclass == SC_STATIC && (type == TY_FLOAT || type == TY_DOUBLE)) {
                /* A local static float or double - "L4" (fltprobe/
                 * p3_global.s.golden's "lea ax,L4"). */
                Val fv = {0};
                fv.kind = VK_FMEM;
                fv.fmode = FM_LAB;
                fv.offset = c1_read_num(temp1, "temp1");
                fv.fdouble = (type == TY_DOUBLE);
                push_val(&g, fv);
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
                          "'register' local, a file-scope variable and a "
                          "called function's own SC_EXTERN name are "
                          "covered so far)", hclass);
            if (type == TY_FLOAT || type == TY_DOUBLE) {
                /* A 'float'/'double' local - see "Floating point". */
                Val fv = {0};
                fv.kind = VK_FMEM;
                fv.offset = c1_read_num(temp1, "temp1");
                fv.fdouble = (type == TY_DOUBLE);
                push_val(&g, fv);
                break;
            }
            /* Any pointer is accepted (it is just a word in memory - see
             * ty_is_word()): an int/char/pointer array's own NAME carries
             * the element type (05_arrptr/05_arrofptr.1.golden's "char
             * *names[3]" - NAME type 9 - and 07_strlibc.1.golden's
             * "char src[20]" - type 1), a plain pointer local its
             * pointer type. What a consumer may then DO with it
             * (dereference, subscript, ...) is checked by that
             * consumer. */
            if (!ty_is_word(type) && type != TY_CHAR && type != TY_LONG &&
                type != TY_STRUCT)
                gen_fatal("NAME of type %d not yet supported (only "
                          "int/char/long/pointer/struct locals are covered "
                          "so far)", type);
            int offset = c1_read_num(temp1, "temp1");
            Val mv = val_mem(offset);
            /* A long variable a CBRANCH tests for truth - see Val's
             * `ltest`. */
            for (int k = 0; type == TY_LONG && k < g.nltest; k++)
                if (g.ltests[k] == g.op_off)
                    mv.ltest = 1;
            /* A whole struct (06_struct: "&p", "p2 = p1") - see Val's
             * `structv`. A member reference never reaches here as a
             * struct: mutos_c0 retypes its NAME to the member's type. */
            mv.structv = (type == TY_STRUCT);
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
            if (type == TY_CHAR) {
                /* A constant typed char: mutos_c0 writes one only as the
                 * right operand of a char compared with a constant in
                 * 0..127 (c0_parser.c's char_compare_rhs() - v7/cc/
                 * c12.c optim()'s CHAR retyping; 10_integ/01_wordcount.
                 * 1.golden's "text[i] != '\0'"). Accepted only there -
                 * consumed as a relational's RIGHT operand - so the
                 * relational handler can tell a byte compare apart. */
                Consumer cons = scan_consumer(temp1);
                if (!find_relop(cons.op) || cons.as_left ||
                    value < 0 || value > 127)
                    gen_fatal("a char-typed constant (CON of type 1) outside "
                              "a comparison with a char is not yet supported");
                Val cv = val_imm(value);
                cv.charcon = 1;
                push_val(&g, cv);
                break;
            }
            if (type != TY_INT && type != TY_UNSIGN)
                gen_fatal("CON of type %d not yet supported (only "
                          "TY_INT/TY_UNSIGN constants, and a char one "
                          "compared with a char, are covered so far)", type);
            push_val(&g, val_imm(value));
            break;
        }

        case OP_FCON:
            gen_fcon(&g, temp1);
            break;

        case OP_ITOF:
            gen_itof(&g, c1_read_num(temp1, "temp1"));
            break;

        case OP_FTOI:
        case OP_FTOL:
            gen_fp_to_int(&g, op, c1_read_num(temp1, "temp1"));
            break;

        case OP_LTOF:
            gen_ltof(&g, c1_read_num(temp1, "temp1")); /* "Floating point" */
            break;

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
            if (type != TY_INT && type != TY_UNSIGN)
                gen_fatal("LTOI to type %d not yet supported (only "
                          "TY_INT and TY_UNSIGN are covered so far)", type);
            Val v = pop_val(&g);
            if (v.lowonly) {
                /* Computed as a word already - see Val's `lowonly`. */
                v.lowonly = 0;
                push_val(&g, v);
                break;
            }
            if (v.kind == VK_LCON) {
                /* A long constant to a word: its low word - v7's unoptim()
                 * (LTOI of an LCON is a CON) - fltprobe/p19_open3.s.
                 * golden's "u = 40000" -> "mov *-36.(bp),#-25536.". */
                push_val(&g, val_imm((int16_t)v.imm));
                break;
            }
            if (v.kind == VK_LOWADD) {
                /* Already a word - see VK_LOWADD; a '+' consumes one
                 * (fltprobe/p19_open3's "r + (int) (l - 100000)") and an
                 * assignment as its right-hand side (p22_long2's "x = (int)
                 * (l - 5)"). */
                Consumer cons = scan_consumer(temp1);
                if (!((cons.op == OP_PLUS || (cons.op == OP_ASSIGN &&
                                              !cons.as_left) ||
                       /* subtracted: p25_long3's "r + 10 - (int) (l -
                        * 4995)" - see OP_MINUS */
                       (cons.op == OP_MINUS && !cons.as_left)) &&
                      (cons.type == TY_INT || cons.type == TY_UNSIGN)))
                    gen_fatal("'(int)' of a long sum or difference used other "
                              "than added to a variable is not yet supported - "
                              "see src/mutos_cc/README.md");
                push_val(&g, v);
                break;
            }
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
            if (v.kind == VK_REG && !v.regvar &&
                (strcmp(v.reg, "ax") == 0 || strcmp(v.reg, "dx") == 0)) {
                /* Already in a register with a byte half: stored from
                 * there - "and ax,*-2." / "or ax,*16." / "pop bx" / "movb
                 * *23.(bx),ax" and "and dx,*127." / "pop bx" / "movb
                 * 4.+_amxtout(bx),dx" (tests/mutos_as/kernel_nonopt/
                 * amx.s). DI and SI have no byte half, so a value there
                 * still goes through DX below. */
                v.flagsv = 0;
                push_val(&g, v);
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
            int uns = 0;
            for (int k = 0; k < g.nitolu; k++)
                if (g.itolu[k] == g.op_off)
                    uns = 1;
            for (int k = 0; k < g.nitollow; k++)
                if (g.itollow[k] == g.op_off) {
                    /* Only its low word is ever used - see Val's
                     * `lowonly`: no code, the int itself. */
                    Val v = materialize(&g, pop_val(&g));
                    if (v.kind != VK_REG && v.kind != VK_MEM &&
                        v.kind != VK_STATIC)
                        gen_fatal("this int-class operand of a truncated "
                                  "'long' sum is not yet supported - see "
                                  "src/mutos_cc/README.md");
                    v.lowonly = 1;
                    push_val(&g, v);
                    goto itol_done;
                }
            Val v = materialize(&g, pop_val(&g));
            Consumer cons = scan_consumer(temp1);
            int lbinop = (cons.op == OP_PLUS || cons.op == OP_MINUS ||
                          cons.op == OP_AND || cons.op == OP_OR ||
                          cons.op == OP_EXOR || find_relop(cons.op) ||
                          ((cons.op == OP_LSHIFT || cons.op == OP_RSHIFT) &&
                           !cons.as_left) ||
                          /* the amount of "l += 1", "l++", "--l": see
                           * long_inplace_const() */
                          (!cons.as_left &&
                           (cons.op == OP_ASPLUS || cons.op == OP_ASMINUS ||
                            cons.op == OP_INCBEF || cons.op == OP_INCAFT ||
                            cons.op == OP_DECBEF || cons.op == OP_DECAFT)));
            if (v.kind == VK_IMM && !uns && lbinop) {
                /* An int constant widened: kept as the long constant it
                 * is, VK_LCON, rendered by its consumer as an LCON is -
                 * an assignment "mov ax,#23456." / "cwd" / "mov di,dx" /
                 * "mov si,ax" (02_long/01_addsub.s.golden's "b = 23456;"
                 * - materialize_long()), '&' / '|' / '+' / '-' "mov ax,
                 * #255." / "cwd" / "push ax" / "push dx" / ... / "pop bx"
                 * / "pop cx" (fltprobe/p19_open3.s.golden's "m & 255" -
                 * as 01_addsub's "c + 1L"). `fromu` marks it: v7's
                 * degree() and longrel() treat an ITOL of a constant
                 * apart from an LCON (OP_LESS...). */
                Val c = {0};
                c.kind = VK_LCON;
                c.imm = (int16_t)v.imm;
                c.offset = c.imm < 0 ? -1 : 0;
                c.fromu = 1;
                push_val(&g, c);
                break;
            }
            if ((cons.op == OP_LSHIFT || cons.op == OP_RSHIFT) &&
                !cons.as_left && cons.type == TY_LONG && !uns &&
                (v.kind == VK_MEM || v.kind == VK_STATIC) && !v.bytev &&
                !v.structv) {
                /* The count of a long shift, an int variable: v7's
                 * rcexpr() takes the ITOL off again ("tree->tr2 =
                 * tree->tr2->tr1") - the count is the int itself, loaded
                 * into CX by the shift (OP_LSHIFT's long case). */
                v.lowonly = 1;
                push_val(&g, v);
                break;
            }
            if (cons.op == OP_TIMES && cons.type == TY_LONG && !uns &&
                (v.kind == VK_MEM || v.kind == VK_STATIC) && !v.bytev &&
                !v.structv) {
                /* An int variable times a long: not widened here - see
                 * Val's `itolw` and gen_long_binop_call(). */
                v.itolw = 1;
                push_val(&g, v);
                break;
            }
            if (uns) {
                /* An unsigned value: the high word cleared, the long in
                 * DI:SI - fltprobe/p19_open3.s.golden's "l = u" -> "mov
                 * si,*-36.(bp)" / "sub di,di", the shape p16_open2's "d =
                 * u" converts (gen_itof()). */
                if (!(v.kind == VK_MEM || v.kind == VK_STATIC) || v.bytev ||
                    v.structv)
                    gen_fatal("widening this 'unsigned' operand to 'long' is "
                              "not yet supported (only a variable) - see "
                              "src/mutos_cc/README.md");
                if (g.reserved & (RB_DI | RB_SI))
                    gen_fatal("widening an 'unsigned' variable to 'long' while "
                              "DI or SI holds a register variable is not yet "
                              "supported - see src/mutos_cc/README.md");
                ins2(&g, "mov", o_reg("si"), o_val(v));
                ins2(&g, "sub", o_reg("di"), o_reg("di"));
                Val l = val_long();
                l.fromu = 1;
                push_val(&g, l);
                break;
            }
            if (find_relop(cons.op) ||
                (!cons.as_left && (cons.op == OP_MINUS || cons.op == OP_DIVIDE ||
                                   cons.op == OP_MOD || cons.op == OP_ASTIMES ||
                                   /* "l * 3" - fltprobe/p25_long3.s.golden:
                                    * "mov ax,*3." / "cwd" / "push ax" /
                                    * "push dx" / ... / "call lmul" */
                                   cons.op == OP_TIMES))) {
                /* Compared: left in DX:AX - fltprobe/p19_open3.s.golden's
                 * "l > i" -> "mov ax,*-26.(bp)" / "cwd" / "cmp dx,*-8.
                 * (bp)" ... (gen_long_relop()). The right operand of '-',
                 * '/', '%' or '*=' likewise: v7's templates push it ("SS")
                 * straight from DX:AX - fltprobe/p22_long2.s.golden's "l -
                 * i", "l / i", "l % i" and "l *= i" -> "mov ax,*-14.(bp)" /
                 * "cwd" / "push ax" / "push dx" / ... (OP_MINUS, gen_long_
                 * binop_call(), OP_ASTIMES). */
                emit_cwd_from(&g, o_val(v));
                Val l = val_long();
                l.lreg = LREG_DXAX;
                push_val(&g, l);
                break;
            }
            if (v.kind == VK_REG && !v.regvar && strcmp(v.reg, "ax") == 0)
                /* Already in AX - a product: just "cwd" - fltprobe/
                 * p25_long3.s.golden's "l + i * j" -> "mov ax,*-14.(bp)" /
                 * "imul *-16.(bp)" / "cwd" / "mov di,dx" / "mov si,ax" (no
                 * "mov ax,ax": v7's movreg() moves nothing when the value is
                 * already in the register). */
                ins0(&g, "cwd");
            else
                emit_cwd_from(&g, o_val(v));
            push_val(&g, emit_dxax_to_long(&g));
        itol_done:
            break;
        }

        case OP_PLUS:
        case OP_MINUS: {
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_DOUBLE) {
                gen_fp_binop(&g, op);    /* see "Floating point" */
                break;
            }
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
                if (type == TY_PTR_CHAR && l.kind == VK_STATICADDR && l.sym &&
                    (r.kind == VK_MEM || r.kind == VK_STATIC) && !r.bytev) {
                    /* "text[i]" on a file-scope char array (the ITOP by 1
                     * left the index variable itself - see OP_ITOP): kept
                     * as VK_SYMIDX for the dereference - see its comment.
                     * Confirmed only as an OP_STAR's operand; any other
                     * consumer ("&text[i]", "text + i") and any other
                     * index or element type has no golden. */
                    long pos = ftell(temp1);
                    int next = c1_read_op(temp1, "temp1");
                    fseek(temp1, pos, SEEK_SET);
                    if (next != OP_STAR)
                        gen_fatal("the address of a file-scope array element, "
                                  "or pointer arithmetic on a file-scope "
                                  "array, is not yet supported - see "
                                  "src/mutos_cc/README.md");
                    Val si = {0};
                    si.kind = VK_SYMIDX;
                    si.sym = l.sym;
                    si.cl = simple_of(r);
                    push_val(&g, si);
                    break;
                }
                if (l.kind == VK_STATICADDR || r.kind == VK_STATICADDR ||
                    l.kind == VK_FUNCADDR || r.kind == VK_FUNCADDR)
                    gen_fatal("pointer arithmetic on the address of a string "
                              "literal, file-scope variable or function "
                              "(other than a char array subscripted by a "
                              "variable) is not yet supported - see "
                              "src/mutos_cc/README.md");
                if ((l.kind == VK_REGOFF && r.kind == VK_IMM) ||
                    (r.kind == VK_REGOFF && l.kind == VK_IMM)) {
                    /* A member of a member through a pointer, "rp->
                     * botright.x": the outer member's address "&*(rp + 4)"
                     * is still "rp + 4" pending (OP_STAR's "&*" cancel),
                     * and the inner member's offset joins it - one
                     * displacement, as v7's acommute() merges the two
                     * constants (and tosses a "+0"): 06_struct/05_nestst.
                     * s.golden's "mov di,*4.(bp)" / "mov di,*4.(di)" for
                     * "rp->botright.x", "*6.(di)" for ".y". Dereferenced
                     * next it stays pending; any other consumer gets the
                     * address computed, "add R,*N." last (as for "&a[j +
                     * 1]" above). */
                    Val ro = (l.kind == VK_REGOFF) ? l : r;
                    ro.imm += (l.kind == VK_REGOFF) ? r.imm : l.imm;
                    long pos = ftell(temp1);
                    int next = c1_read_op(temp1, "temp1");
                    fseek(temp1, pos, SEEK_SET);
                    if (next == OP_STAR) {
                        push_val(&g, ro);
                        break;
                    }
                    Val base = val_from_simple(ro.cl);
                    const char *reg = (base.kind == VK_REG) ? base.reg
                                                            : pick_addr_reg(&g);
                    load_into(&g, reg, base);
                    if (ro.imm != 0)
                        ins2(&g, "add", o_reg(reg), o_imm(ro.imm));
                    push_val(&g, val_reg(reg));
                    break;
                }
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
                    if (ro.cl.kind == VK_REG && base.kind == VK_REG &&
                        !base.regvar && is_base_reg(base.reg) &&
                        strcmp(base.reg, "bx") != 0) {
                        /* An array's address already in a base register
                         * (OP_AMPER's "lea"): the scaled index added to
                         * it, N * size pending as the displacement -
                         * fltprobe/p19_open3.s.golden's "b[j - 2]" ->
                         * "lea si,*-24.(bp)" / "mov dx,*-28.(bp)" / "sal
                         * dx,*1" / "add si,dx" / ... "*-4.(si)". (v7's
                         * acommute() would fold N * size into the address
                         * instead; the real compiler does not.) */
                        ins2(&g, "add", o_reg(base.reg), o_reg(ro.cl.reg));
                        ro.cl = simple_of(val_reg(base.reg));
                        if (peek_op(temp1) == OP_STAR || next_is_con_plus(temp1)) {
                            push_val(&g, ro);
                            break;
                        }
                        ins2(&g, "add", o_reg(base.reg), o_imm(ro.imm));
                        push_val(&g, val_reg(base.reg));
                        break;
                    }
                    if (ro.cl.kind != VK_REG || base.kind != VK_MEM ||
                        strcmp(ro.cl.reg, "dx") == 0)
                        gen_fatal("a subscript of the form \"i + N\" on anything "
                                  "but a pointer variable or an array is not yet "
                                  "supported - see src/mutos_cc/README.md");
                    const char *reg = ro.cl.reg;
                    ins2(&g, "add", o_reg(reg), o_val(base));
                    long pos = ftell(temp1);
                    int next = c1_read_op(temp1, "temp1");
                    fseek(temp1, pos, SEEK_SET);
                    /* A member of the element ("p[i + 1].y") adds its
                     * offset to the pending one first - see the REGOFF +
                     * constant fold below. */
                    if (next == OP_STAR || next_is_con_plus(temp1)) {
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
                if (l.lowonly && r.kind == VK_LCON) {
                    /* "(int) (x + c)" with an int x - see Val's `lowonly`:
                     * the word plus the constant's low word (negated for
                     * '-', as v7's optim() makes "x - c" "x + -c"), in DI
                     * - fltprobe/p21_fltexp.s.golden's "r + u - 39990" ->
                     * "mov di,*-46.(bp)" / "add di,*-38.(bp)" / "add di,
                     * #25546.". */
                    uint32_t c = ((uint32_t)(uint16_t)r.offset << 16) |
                                 (uint16_t)r.imm;
                    if (op == OP_MINUS)
                        c = 0u - c;
                    long lo = (int16_t)(uint16_t)(c & 0xFFFFu);
                    l.lowonly = 0;
                    if (!(l.kind == VK_REG && strcmp(l.reg, "di") == 0)) {
                        if (di_busy(&g) || (g.reserved & RB_DI))
                            gen_fatal("'(int)' of a long sum while DI is "
                                      "taken is not yet supported");
                        load_into_di(&g, l);
                    }
                    if (lo == 1)
                        ins1(&g, "inc", o_reg("di"));
                    else if (lo == -1)
                        ins1(&g, "dec", o_reg("di"));
                    else if (lo != 0)
                        ins2(&g, "add", o_reg("di"), o_imm(lo));
                    Val w = val_reg("di");
                    w.lowonly = 1;
                    push_val(&g, w);
                    break;
                }
                if (op == OP_PLUS)
                    constant_to_right(&l, &r);
                const AluOp *a = aluop(op);
                if (l.kind == VK_MEM && r.kind == VK_LCON &&
                    peek_op(temp1) == OP_LTOI) {
                    /* "(int) (l - 100000)": only the low words count -
                     * VK_LOWADD, no code yet (v7's unoptim() distributes
                     * the LTOI that follows). */
                    Val w = {0};
                    w.kind = VK_LOWADD;
                    w.offset = l.offset + MCC_SZINT;
                    w.imm = (int16_t)(op == OP_PLUS ? r.imm : -r.imm);
                    /* A '-' of an int constant widened is a '+' already
                     * (v7's optim() turns "x - c" into "x + -c" when c is
                     * a CON or an ITOL of one, not an LCON): the other term
                     * first, as for "(int) (l + c)" - fltprobe/p25_long3.s.
                     * golden's "r + (int) (l - 9990)" -> "mov di,*-20.(bp)"
                     * / "add di,*-6.(bp)" / "add di,#-9990.". */
                    w.lowplus = (op == OP_PLUS || r.fromu);
                    push_val(&g, w);
                    break;
                }
                if (op == OP_PLUS && l.kind == VK_MEM && r.kind == VK_LONG &&
                    !r.lpair && r.lreg == LREG_DISI) {
                    /* An int (or unsigned) widened, plus a long variable:
                     * the widened one first, in DI:SI, the variable added
                     * from memory - fltprobe/p19_open3.s.golden's "l + i"
                     * and "i + l" alike ("mov ax,*-26.(bp)" / "cwd" /
                     * "mov di,dx" / "mov si,ax" / "add si,*-6.(bp)" / "adc
                     * di,*-8.(bp)"): v7's acommute() puts the ITOL (degree
                     * 2) ahead of the NAME. */
                    Val t = l;
                    l = r;
                    r = t;
                }
                if (l.kind == VK_LONG && !l.lpair && l.lreg == LREG_DISI &&
                    r.kind == VK_MEM) {
                    /* The widened int left, in DI:SI, the variable combined
                     * from memory - "l + i" / "i + l" above, and "i - l":
                     * fltprobe/p22_long2.s.golden's "l = i - l" -> "mov ax,
                     * *-14.(bp)" / "cwd" / "mov di,dx" / "mov si,ax" / "sub
                     * si,*-6.(bp)" / "sbb di,*-8.(bp)". */
                    ins2(&g, a->mnem, o_reg("si"), o_mem(r.offset + MCC_SZINT));
                    ins2(&g, a->mnem_hi, o_reg("di"), o_mem(r.offset));
                    push_val(&g, val_long());
                    break;
                }
                if (l.kind != VK_MEM)
                    gen_fatal("'long' %s with a non-memory left operand "
                              "is not yet supported",
                              op == OP_PLUS ? "addition" : "subtraction");
                if (op == OP_MINUS && r.kind == VK_LONG && !r.lpair &&
                    r.lreg == LREG_DXAX) {
                    /* A long variable minus an int widened: v7's "%nl,nl"
                     * - the right operand computed and pushed ("SS"), the
                     * left loaded, the right popped - fltprobe/p22_long2.s.
                     * golden's "l = l - i" -> "mov ax,*-14.(bp)" / "cwd" /
                     * "push ax" / "push dx" / "mov si,*-6.(bp)" / "mov di,
                     * *-8.(bp)" / "pop bx" / "pop cx" / "sub si,cx" / "sbb
                     * di,bx" - c + 1L's shape (gen_long_pushop()). */
                    (void)gen_long_pushop(&g, op, l);
                    push_val(&g, val_long());
                    break;
                }
                if (r.kind == VK_MEM) {
                    ins2(&g, "mov", o_reg("si"), o_mem(l.offset + MCC_SZINT));
                    ins2(&g, "mov", o_reg("di"), o_mem(l.offset));
                    ins2(&g, a->mnem, o_reg("si"), o_mem(r.offset + MCC_SZINT));
                    ins2(&g, a->mnem_hi, o_reg("di"), o_mem(r.offset));
                } else if (r.kind == VK_LCON) {
                    /* A constant: v7's optim() makes "x - c" "x + -c", and
                     * its c1 keeps a constant that fits in an int as
                     * ITOL(CON) when it is not negative (getree()'s LCON
                     * case) but folds a negative one back into an LCON
                     * (unoptim()) - two templates: the ITOL pushed as for
                     * "c + 1L" (gen_long_constop() - 02_long/01_addsub.s.
                     * golden, fltprobe/p22_long2.s.golden's "l + 1"), the
                     * LCON added as two immediates - p22_long2.s.golden's
                     * "l - 2" -> "mov si,*-6.(bp)" / "mov di,*-8.(bp)" /
                     * "add si,*-2." / "adc di,*-1.". */
                    uint32_t c = ((uint32_t)(uint16_t)r.offset << 16) |
                                 (uint16_t)r.imm;
                    if (op == OP_MINUS)
                        c = 0u - c;
                    Val e = r;
                    e.offset = (int16_t)(uint16_t)(c >> 16);
                    e.imm = (int16_t)(uint16_t)(c & 0xFFFFu);
                    const AluOp *pa = aluop(OP_PLUS);
                    if (e.offset == 0 && e.imm >= 0) {
                        (void)gen_long_constop(&g, OP_PLUS, l, e);
                    } else {
                        ins2(&g, "mov", o_reg("si"), o_mem(l.offset + MCC_SZINT));
                        ins2(&g, "mov", o_reg("di"), o_mem(l.offset));
                        ins2(&g, pa->mnem, o_reg("si"), o_imm(e.imm));
                        ins2(&g, pa->mnem_hi, o_reg("di"), o_imm(e.offset));
                    }
                } else {
                    gen_fatal("'long' %s with this right-operand shape "
                              "is not yet supported",
                              op == OP_PLUS ? "addition" : "subtraction");
                }
                push_val(&g, val_long());
                break;
            }
            /* An unsigned sum or difference (a bit-field's value - 06_struct/
             * 07_bitfield.1.golden's "f.ready + f.mode + f.count", PLUS
             * type 7) is the same 16-bit instruction as an int one. */
            if (type != TY_INT && type != TY_UNSIGN)
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            if (op == OP_PLUS && g.valsp >= 2 &&
                g.valstack[g.valsp - 1].kind == VK_STACKED) {
                /* The right operand was computed first and pushed (see
                 * is_relsum()): the left one into DI, the right one popped
                 * into BX and added - fltprobe/p23_elem3.s.golden's "pop
                 * bx" / "add di,bx" (v7's "%n,n": SS, F, "add (sp)+,R"). */
                (void)pop_any(&g);              /* the VK_STACKED marker */
                Val l = materialize(&g, pop_val(&g));
                /* A call's result stays in AX, where the call left it (see
                 * is_callsum()): "pop bx" / "add ax,bx". */
                if (l.kind != VK_REG || l.regvar ||
                    (strcmp(l.reg, "di") != 0 && strcmp(l.reg, "ax") != 0))
                    gen_fatal("internal: the left operand of a '+' with a "
                              "pushed right operand is not in DI or AX");
                ins1(&g, "pop", o_reg("bx"));
                ins2(&g, "add", o_reg(l.reg), o_reg("bx"));
                push_val(&g, val_reg(l.reg));
                break;
            }
            Val l, r;
            Consumer cons_plus;
            int rcond = g.valsp >= 1 &&
                        g.valstack[g.valsp - 1].kind == VK_COND;
            pop_operands_ex(&g, &l, &r, POP_CHARX);
            if (l.kind == VK_MEM_DIRECT || r.kind == VK_MEM_DIRECT)
                gen_fatal("an address used in int %s is not yet supported - "
                          "see src/mutos_cc/README.md", aluop(op)->name);
            if (l.kind == VK_LOWADD || r.kind == VK_LOWADD) {
                /* "r + (int) (l - 100000)" - see VK_LOWADD: the low word
                 * first, the other term, the constant last - fltprobe/
                 * p19_open3.s.golden: "mov di,*-6.(bp)" / "add di,*-34.
                 * (bp)" / "add di,#31072." (also for "(l - 39990)" and
                 * "(l - 65530)" - "add di,#25546.", "add di,*6."). v7's
                 * acommute() would put the variable first; this is the
                 * real compiler's order. Only a '+' and a plain word
                 * operand on the other side have a golden. */
                if (op == OP_MINUS && r.kind == VK_LOWADD &&
                    ((l.kind == VK_REG && !l.regvar &&
                      strcmp(l.reg, "di") == 0) ||
                     (l.kind == VK_IMM && !di_busy(&g)))) {
                    /* Subtracted from a value already computed in DI: the
                     * low word and the constant into SI, then subtracted -
                     * fltprobe/p25_long3.s.golden's "r + 10 - (int) (l -
                     * 4995)" -> "mov di,*-20.(bp)" / "add di,*10." / "mov
                     * si,*-6.(bp)" / "add si,#-4995." / "sub di,si" (v7's
                     * "%n,e": F, S1, "sub R1,R"). A constant on the left
                     * is loaded into DI first ("F") - p31_long4.s.golden's
                     * "x = 10 - (int) (l - 35);" -> "mov di,*10." / "mov
                     * si,*-6.(bp)" / "add si,*-35." / "sub di,si". */
                    if (l.kind == VK_IMM) {
                        load_into_di(&g, l);
                        l = val_reg("di");
                    }
                    require_free(l, RB_SI, "a subtraction");
                    if (g.reserved & RB_SI)
                        gen_fatal("'(int)' of a long sum subtracted while SI "
                                  "holds a register variable is not yet "
                                  "supported - see src/mutos_cc/README.md");
                    ins2(&g, "mov", o_reg("si"), o_mem(r.offset));
                    if (r.imm == 1)
                        ins1(&g, "inc", o_reg("si"));
                    else if (r.imm == -1)
                        ins1(&g, "dec", o_reg("si"));
                    else if (r.imm != 0)
                        ins2(&g, "add", o_reg("si"), o_imm(r.imm));
                    ins2(&g, "sub", o_reg("di"), o_reg("si"));
                    push_val(&g, val_reg("di"));
                    break;
                }
                Val w = (l.kind == VK_LOWADD) ? l : r;
                Val o = (l.kind == VK_LOWADD) ? r : l;
                if (op != OP_PLUS || o.kind == VK_LOWADD || !is_int_leaf(&o) ||
                    o.kind == VK_IMM || o.regvar)
                    gen_fatal("'(int)' of a long sum or difference used other "
                              "than added to a variable is not yet supported - "
                              "see src/mutos_cc/README.md");
                if (di_busy(&g))
                    gen_fatal("'(int)' of a long sum or difference while DI "
                              "holds a pending value is not yet supported - see "
                              "src/mutos_cc/README.md");
                /* "(int) (l + c)" takes the other term first: fltprobe/
                 * p22_long2.s.golden's "r + (int) (l + 99990)" -> "mov
                 * di,*-18.(bp)" / "add di,*-6.(bp)" / "add di,#-31082.",
                 * against "(int) (l - 99990)"'s "mov di,*-6.(bp)" / "add
                 * di,*-18.(bp)" / "add di,#31082." in the same golden. */
                if (w.lowplus) {
                    ins2(&g, "mov", o_reg("di"), o_val(o));
                    ins2(&g, "add", o_reg("di"), o_mem(w.offset));
                } else {
                    ins2(&g, "mov", o_reg("di"), o_mem(w.offset));
                    ins2(&g, "add", o_reg("di"), o_val(o));
                }
                if (w.imm == 1)
                    ins1(&g, "inc", o_reg("di"));
                else if (w.imm == -1)
                    ins1(&g, "dec", o_reg("di"));
                else if (w.imm != 0)
                    ins2(&g, "add", o_reg("di"), o_imm(w.imm));
                push_val(&g, val_reg("di"));
                break;
            }
            /* v7's optim() makes "x - c" (a constant right operand) "x +
             * -c" before any code is chosen: fltprobe/05_fltasop.s.golden's
             * "c - '0'" -> "add ax,*-48." (as gen_charx_binop() already
             * did for a char). "n - 1" stays "dec di" (04_funcs/
             * 03_recfact) - "+ -1" below. */
            if (op == OP_MINUS && r.kind == VK_IMM && !r.charcon &&
                l.kind != VK_CHARX) {
                op = OP_PLUS;
                r.imm = -r.imm;
            }
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
                 * With ONE char operand the computation is in AX - see
                 * gen_charx_binop(); MINUS of two chars has no example. */
                if (l.kind != VK_CHARX || r.kind != VK_CHARX) {
                    charx_left(op, &l, &r);
                    push_val(&g, gen_charx_binop(&g, op, l, r));
                    break;
                }
                if (op != OP_PLUS)
                    gen_fatal("%s of two 'char' operands is not yet supported "
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
            if (op == OP_PLUS && r.kind == VK_IMM && r.imm == 0 &&
                (l.kind == VK_MEM || l.kind == VK_STATIC || l.kind == VK_REG ||
                 l.kind == VK_IND)) {
                /* "x + 0" is x: v7's acommute() tosses an int "+0" before
                 * any code is chosen (as "x - 0", which optim() makes "x +
                 * -0" first) - fltprobe/p13_open.s.golden's "(d * e) + (j +
                 * 0)" converts j alone, "mov ax,*-40.(bp)" / "call itof". */
                push_val(&g, l);
                break;
            }
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
                if (cons.op == OP_ITOF && !l.bytev && !l.structv &&
                    g.itof_reg == 1 /* FREG_AX - see gen_itof() */) {
                    /* Converted to floating next: computed in AX, where
                     * "itof" takes it - fltprobe/05_fltasop.s.golden's
                     * "10*fl + (c-'0')" -> "mov ax,*-42.(bp)" / "add
                     * ax,*-48." / "call itof" (see gen_itof()); "+ 1" and
                     * "- 1" are "inc ax" / "dec ax", as in DI
                     * (p7_itofreg.s.golden's "(d * e) + (i + 1)" and
                     * "(d * e) - (j - 1)"). A "+ 0" never gets here (see
                     * above). */
                    ins2(&g, "mov", o_reg("ax"), o_val(l));
                    if (r.imm == 1)
                        ins1(&g, "inc", o_reg("ax"));
                    else if (r.imm == -1)
                        ins1(&g, "dec", o_reg("ax"));
                    else
                        ins2(&g, "add", o_reg("ax"), o_imm(r.imm));
                    push_val(&g, val_reg("ax"));
                    break;
                }
            }
            if (g.itof_reg == 1 /* FREG_AX */ &&
                (l.kind == VK_MEM || l.kind == VK_STATIC) &&
                (r.kind == VK_MEM || r.kind == VK_STATIC) &&
                !l.bytev && !l.structv && !r.bytev && !r.structv &&
                scan_consumer(temp1).op == OP_ITOF) {
                /* The sum or difference of two int variables converted to
                 * floating in AX: "mov ax,i" / "add ax,j" / "call itof" -
                 * p7_itofreg.s.golden's "(d * e) + (i + j)" - and "mov
                 * ax,i" / "sub ax,j" - p13_open.s.golden's "(d * e) - (i -
                 * j)". (In DI it is the ordinary "mov di,i" / "add di,j",
                 * then "mov ax,di" - gen_itof().) */
                ins2(&g, "mov", o_reg("ax"), o_val(l));
                ins2(&g, aluop(op)->mnem, o_reg("ax"), o_val(r));
                push_val(&g, val_reg("ax"));
                break;
            }
            if (op == OP_PLUS && rcond && r.kind == VK_REG &&
                strcmp(r.reg, "di") == 0 && !r.regvar &&
                (l.kind == VK_MEM || l.kind == VK_STATIC) && !l.bytev &&
                !l.structv) {
                /* A comparison's 0/1 (just materialized into DI) plus a
                 * variable: v7's acommute() puts the comparison first (its
                 * degree is higher) and adds the variable to it -
                 * fltprobe/p1_compare.s.golden's "r = r + (d < e)" ->
                 * ... "L10008:add di,*-28.(bp)". */
                ins2(&g, "add", o_reg("di"), o_val(l));
                push_val(&g, val_reg("di"));
                break;
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
                if (other.kind == VK_REG && !other.regvar && !ind.bytev &&
                    (strcmp(other.reg, "di") == 0 || strcmp(other.reg, "si") == 0) &&
                    strcmp(other.reg, ind.reg) != 0) {
                    /* The other operand is already a value in a register
                     * of its own (a left operand loaded first - see
                     * load_now(), SEG_LOADIND): the dereference is added
                     * to it straight from memory - 06_struct/03_starray.
                     * s.golden's "sum + pts[i].x + pts[i].y" -> "mov di,
                     * (di)" / "lea si,*-16.(bp)" / ... / "add di,*2.(si)"
                     * (as "cmp di,*2.(si)" in 02_bubsort, "sub di,*2.
                     * (si)" in 05_nestst). */
                    ins2(&g, "add", o_reg(other.reg), o_val(ind));
                    push_val(&g, val_reg(other.reg));
                    break;
                }
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
                 * commutative and no golden shows its AX shape. A constant
                 * is added there too: fltprobe/p13_open.s.golden's "r +
                 * (int) d - 30" -> "call ftoi" / "add ax,*-42.(bp)" / "add
                 * ax,*-30." (v7's optim() makes "- 30" "+ -30"), as the
                 * kernel's amx.s adds "*22." to a product in AX; "+ 1" and
                 * "- 1" are "inc ax" / "dec ax", as in DI and as for an
                 * int converted in AX (p7_itofreg). A register other
                 * operand is left to the paths below, unconfirmed. */
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
                if (l_ax && !l.regvar && r.kind == VK_IMM && !r.charcon) {
                    if (r.imm == 1)
                        ins1(&g, "inc", o_reg("ax"));
                    else if (r.imm == -1)
                        ins1(&g, "dec", o_reg("ax"));
                    else
                        ins2(&g, "add", o_reg("ax"), o_val(r));
                    push_val(&g, val_reg("ax"));
                    break;
                }
            }
            if (op == OP_PLUS && r.kind == VK_REG && !r.regvar && !r.postfix &&
                strcmp(r.reg, "di") == 0 &&
                (l.kind == VK_MEM || l.kind == VK_STATIC) && !l.bytev &&
                !l.structv && (cons_plus = scan_consumer(temp1),
                               cons_plus.op == OP_ASSIGN && !cons_plus.as_left)) {
                /* A value computed into DI - a '?:''s arms - plus a
                 * variable, the sum stored: acommute() puts the computed one
                 * first, and the variable is added to it there - fltprobe/
                 * p25_long3.s.golden's "r = r + (l ? 2 : 50);" -> ...
                 * "L10004:add di,*-20.(bp)" / "mov *-20.(bp),di". (Inside a
                 * larger expression the sum stays where it was put before -
                 * "mov si,<x>" / "add si,di", see below - since a sibling
                 * computed after it may need DI; no golden shows that.) */
                Val t = l;
                l = r;
                r = t;
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
                load_into_si(&g, l);   /* nothing to do if it is there */
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
            } else if (r.kind == VK_IMM && r.imm == -1 && op == OP_PLUS) {
                /* The symmetric "- 1" (made "+ -1" above) -> DEC case,
                 * confirmed against 04_funcs/03_recfact.s.golden's "n -
                 * 1" -> "dec\tdi" (never a "sub") and 04_mutrec.s.golden's
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
            if (type == TY_LONG) {
                /* A long variable and a long constant (an int one widened
                 * - OP_ITOL): gen_long_constop() - fltprobe/p19_open3.s.
                 * golden's "m & 255", "m | 6"; '^' by the same table entry
                 * (no golden). Any other operand shape has none. */
                Val r = pop_val(&g);
                Val l = pop_val(&g);
                constant_to_right(&l, &r);
                if (l.kind == VK_MEM && r.kind == VK_MEM) {
                    /* Two long variables: the left one loaded, the right
                     * one combined from memory - fltprobe/p22_long2.s.
                     * golden's "l = l & m" -> "mov si,*-6.(bp)" / "mov di,
                     * *-8.(bp)" / "and si,*-10.(bp)" / "and di,*-12.(bp)"
                     * ('|' and '^' by the same table entry, no golden). */
                    ins2(&g, "mov", o_reg("si"), o_mem(l.offset + MCC_SZINT));
                    ins2(&g, "mov", o_reg("di"), o_mem(l.offset));
                    ins2(&g, aluop(op)->mnem, o_reg("si"),
                         o_mem(r.offset + MCC_SZINT));
                    ins2(&g, aluop(op)->mnem, o_reg("di"), o_mem(r.offset));
                    push_val(&g, val_long());
                    break;
                }
                if (l.kind != VK_MEM || r.kind != VK_LCON ||
                    r.offset != (r.imm < 0 ? -1 : 0))
                    gen_fatal("'long' %s of anything but a long variable and "
                              "a constant or another long variable is not yet "
                              "supported - see src/mutos_cc/README.md",
                              aluop(op)->name);
                push_val(&g, gen_long_constop(&g, op, l, r));
                break;
            }
            /* Unsigned (mutos_c0 types the operator so when an operand is
             * a bit-field) is the same instruction. */
            if (type != TY_INT && type != TY_UNSIGN)
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val l, r;
            pop_operands_ex(&g, &l, &r, POP_CHARX);
            if (l.kind == VK_CHARX || r.kind == VK_CHARX) {
                /* A char with an int operand - see gen_charx_binop(). */
                charx_left(op, &l, &r);
                push_val(&g, gen_charx_binop(&g, op, l, r));
                break;
            }
            constant_to_right(&l, &r);
            if (r.kind == VK_REG && !r.regvar &&
                (strcmp(r.reg, "ax") == 0 || strcmp(r.reg, "dx") == 0) &&
                is_int_leaf(&l)) {
                Val t = l;               /* acommute(): the computed */
                l = r;                   /* value goes left          */
                r = t;
            }

            if (l.kind == VK_REG && !l.regvar &&
                (strcmp(l.reg, "ax") == 0 || strcmp(l.reg, "dx") == 0) &&
                is_int_leaf(&r)) {
                /* The left operand is already a value in AX or DX (a
                 * call's result, a quotient or remainder, a char just
                 * widened or masked): the operator works on it there, as
                 * v7's templates compute into the register the left
                 * operand is in - "and ax,*-2." / "or ax,*16." after a
                 * char's "cbw" (tests/mutos_as/kernel_nonopt/amx.s, 3x) and
                 * "call _inb" / "add sp,*2." / "and ax,*9." (lp_AC.s),
                 * never moved to DI first. An AND's flags then serve a
                 * truth test directly (see Val's `flagsv`). */
                ins2(&g, aluop(op)->mnem, o_reg(l.reg), o_val(r));
                Val res = val_reg(l.reg);
                if (op == OP_AND) {
                    res.flagsv = 1;
                    res.flags_at = g.ninsn;
                }
                push_val(&g, res);
                break;
            }
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
            if (type == TY_LONG) {
                /* A long variable shifted by a constant (mutos_c0 widens the
                 * count - OP_ITOL keeps it a VK_LCON): loaded into DI:SI and
                 * shifted a bit at a time through the carry - fltprobe/
                 * p22_long2.s.golden's "l << 2" -> "mov si,*-6.(bp)" / "mov
                 * di,*-8.(bp)" / "sal si,*1" / "rcl di,*1" / "sal si,*1" /
                 * "rcl di,*1", "l >> 1" -> ... "sar di,*1" / "rcr si,*1".
                 * From 3 up (as for an int - MCC_SHIFT_REPEAT_MAX), the count
                 * into CX and the one-bit pair looped - fltprobe/p25_long3.s.
                 * golden's "l << 3" -> ... "mov cx,*3." / "sal si,*1" / "rcl
                 * di,*1" / "loop .-4", "l >> 4" -> "mov cx,*4." / "sar di,*1"
                 * / "rcr si,*1" / "loop .-4" (".-4": back over the pair, two
                 * bytes each). A variable count - see below. */
                Val r = pop_val(&g);
                Val l = pop_val(&g);
                if (l.kind == VK_MEM && r.lowonly &&
                    (r.kind == VK_MEM || r.kind == VK_STATIC) &&
                    !(g.reserved & (RB_DI | RB_SI))) {
                    /* By an int variable (see OP_ITOL): the long into
                     * DI:SI, the count into CX, the loop skipped for a
                     * count of 0 - fltprobe/p31_long4.s.golden's "l = l <<
                     * i;" -> "mov si,*-6.(bp)" / "mov di,*-8.(bp)" / "mov
                     * cx,*-14.(bp)" / "or cx,cx" / "jz .+8" / "sal si,*1" /
                     * "rcl di,*1" / "loop .-4" (".+8": past the pair and
                     * the loop). '>>' the same with "sar di" / "rcr si" (no
                     * golden). */
                    r.lowonly = 0;
                    ins2(&g, "mov", o_reg("si"), o_mem(l.offset + MCC_SZINT));
                    ins2(&g, "mov", o_reg("di"), o_mem(l.offset));
                    ins2(&g, "mov", o_reg("cx"), o_val(r));
                    ins2(&g, "or", o_reg("cx"), o_reg("cx"));
                    ins1(&g, "jz", o_sym(".+8"));
                    if (op == OP_LSHIFT) {
                        ins2(&g, "sal", o_reg("si"), o_shift1());
                        ins2(&g, "rcl", o_reg("di"), o_shift1());
                    } else {
                        ins2(&g, "sar", o_reg("di"), o_shift1());
                        ins2(&g, "rcr", o_reg("si"), o_shift1());
                    }
                    ins1(&g, "loop", o_sym(".-4"));
                    push_val(&g, val_long());
                    break;
                }
                if (l.kind != VK_MEM || r.kind != VK_LCON || r.offset != 0 ||
                    r.imm < 0 || (g.reserved & (RB_DI | RB_SI)))
                    gen_fatal("this 'long' shift is not yet supported (only a "
                              "'long' variable shifted by a constant count or "
                              "an int variable) - see src/mutos_cc/README.md");
                ins2(&g, "mov", o_reg("si"), o_mem(l.offset + MCC_SZINT));
                ins2(&g, "mov", o_reg("di"), o_mem(l.offset));
                if (r.imm > MCC_SHIFT_REPEAT_MAX) {
                    ins2(&g, "mov", o_reg("cx"), o_imm(r.imm));
                    if (op == OP_LSHIFT) {
                        ins2(&g, "sal", o_reg("si"), o_shift1());
                        ins2(&g, "rcl", o_reg("di"), o_shift1());
                    } else {
                        ins2(&g, "sar", o_reg("di"), o_shift1());
                        ins2(&g, "rcr", o_reg("si"), o_shift1());
                    }
                    ins1(&g, "loop", o_sym(".-4"));
                    push_val(&g, val_long());
                    break;
                }
                for (long k = 0; k < r.imm; k++) {
                    if (op == OP_LSHIFT) {
                        ins2(&g, "sal", o_reg("si"), o_shift1());
                        ins2(&g, "rcl", o_reg("di"), o_shift1());
                    } else {
                        ins2(&g, "sar", o_reg("di"), o_shift1());
                        ins2(&g, "rcr", o_reg("si"), o_shift1());
                    }
                }
                push_val(&g, val_long());
                break;
            }
            /* An unsigned left shift is the int one; an unsigned right
             * shift would need SHR, not this handler's SAR (mutos_c0
             * refuses it too). */
            if (type != TY_INT && !(type == TY_UNSIGN && op == OP_LSHIFT))
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val l, r;
            pop_operands_ex(&g, &l, &r, POP_CHARX);
            if (l.kind == VK_CHARX || r.kind == VK_CHARX) {
                /* A char shifted by an int - see gen_charx_binop(). */
                charx_left(op, &l, &r);
                push_val(&g, gen_charx_binop(&g, op, l, r));
                break;
            }
            if (g.itof_reg == 1 /* FREG_AX - see gen_itof() */ &&
                r.kind == VK_IMM && (l.kind == VK_MEM || l.kind == VK_STATIC) &&
                !l.bytev && !l.structv && scan_consumer(temp1).op == OP_ITOF) {
                /* An int variable shifted by a constant, converted to
                 * floating in AX: computed there - fltprobe/p13_open.s.
                 * golden's "(d / e) + (i << 1)" -> "mov ax,*-38.(bp)" /
                 * "sal ax,*1" / "call itof" (in DI: "mov di,i" / "sal
                 * di,*1" / "mov ax,di", p11_itof2). The count's encoding
                 * is DI's (emit_const_shift()); only "<< 1" is a golden. */
                ins2(&g, "mov", o_reg("ax"), o_val(l));
                emit_const_shift(&g, aluop(op)->mnem, "ax", r.imm);
                push_val(&g, val_reg("ax"));
                break;
            }
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

        case OP_NEG: {
            /* Unary '-': mutos_c0 writes it only for a floating operand
             * (an int one is refused there) - see gen_fp_neg(). */
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_LONG && g.valsp >= 1 &&
                g.valstack[g.valsp - 1].kind == VK_LCON) {
                /* A long constant negated: folded, as v7's unoptim() does
                 * (mutos_c0 writes "-100000" as LCON, NEG(LONG)). */
                Val *c = &g.valstack[g.valsp - 1];
                uint32_t v = ((uint32_t)(uint16_t)c->offset << 16) |
                             (uint16_t)c->imm;
                v = 0u - v;
                c->offset = (int16_t)(uint16_t)(v >> 16);
                c->imm = (int16_t)(uint16_t)(v & 0xFFFFu);
                c->fromu = 0;
                break;
            }
            if (type == TY_INT) {
                /* An int negated: computed in DI, "neg" - fltprobe/
                 * p21_fltexp.s.golden's "d = -i" -> "mov di,*-40.(bp)" /
                 * "neg di" / "mov ax,di" / "call itof". */
                Val v = materialize(&g, pop_val(&g));
                if (v.kind != VK_MEM && v.kind != VK_STATIC &&
                    !(v.kind == VK_REG && strcmp(v.reg, "di") == 0 &&
                      !v.regvar))
                    gen_fatal("unary '-' of this int operand is not yet "
                              "supported (only a variable) - see "
                              "src/mutos_cc/README.md");
                if (v.kind != VK_REG && (g.reserved & RB_DI))
                    gen_fatal("unary '-' in a function with a register "
                              "variable in DI is not yet supported");
                load_into_di(&g, v);
                ins1(&g, "neg", o_reg("di"));
                push_val(&g, val_reg("di"));
                break;
            }
            if (type == TY_LONG) {
                /* A long variable negated: loaded into DI:SI, both words
                 * negated and the borrow taken from the high one -
                 * fltprobe/p25_long3.s.golden's "l = -l" -> "mov si,*-6.
                 * (bp)" / "mov di,*-8.(bp)" / "neg di" / "neg si" / "sbb
                 * di,*0" (the high word's "*0" a template constant, as in
                 * long_inplace_const()). */
                Val v = pop_val(&g);
                if (v.kind != VK_MEM || (g.reserved & (RB_DI | RB_SI)))
                    gen_fatal("unary '-' of anything but a 'long' variable is "
                              "not yet supported - see src/mutos_cc/README.md");
                ins2(&g, "mov", o_reg("si"), o_mem(v.offset + MCC_SZINT));
                ins2(&g, "mov", o_reg("di"), o_mem(v.offset));
                ins1(&g, "neg", o_reg("di"));
                ins1(&g, "neg", o_reg("si"));
                ins2(&g, "sbb", o_reg("di"), o_cmpimm(0));
                push_val(&g, val_long());
                break;
            }
            if (type != TY_DOUBLE && type != TY_FLOAT)
                gen_fatal("NEG of type %d not yet supported (only an int, a "
                          "'long' or a 'float'/'double' operand)", type);
            gen_fp_neg(&g);
            break;
        }

        case OP_COMPL: {
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_LONG && g.valsp >= 1 &&
                g.valstack[g.valsp - 1].kind == VK_LCON) {
                /* "~100000L": folded - see OP_NEG. */
                Val *c = &g.valstack[g.valsp - 1];
                c->offset = (int16_t)~(uint16_t)c->offset;
                c->imm = (int16_t)~(uint16_t)c->imm;
                c->fromu = 0;
                break;
            }
            if (type == TY_LONG) {
                /* A long variable complemented: loaded into DI:SI, the high
                 * word first - fltprobe/p22_long2.s.golden's "l = ~l" ->
                 * "mov si,*-6.(bp)" / "mov di,*-8.(bp)" / "not di" / "not
                 * si". */
                Val v = pop_val(&g);
                if (v.kind != VK_MEM || (g.reserved & (RB_DI | RB_SI)))
                    gen_fatal("'~' of anything but a 'long' variable is not "
                              "yet supported - see src/mutos_cc/README.md");
                ins2(&g, "mov", o_reg("si"), o_mem(v.offset + MCC_SZINT));
                ins2(&g, "mov", o_reg("di"), o_mem(v.offset));
                ins1(&g, "not", o_reg("di"));
                ins1(&g, "not", o_reg("si"));
                push_val(&g, val_long());
                break;
            }
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
            Val v = pop_val_ex(&g, POP_BYTE | POP_STRUCT | POP_FLOAT);
            v.bytev = 0;
            v.structv = 0;     /* "&p" of a struct: an ordinary address */
            if (v.kind == VK_FMEM) {
                /* A float or double object's address: a local's as any
                 * local's (an array decaying - p3_global's "p = arr" ->
                 * "lea di,*-20.(bp)"), a file-scope or static one's a
                 * link-time constant ("gp = &gd" -> "mov _gp,#_gd"). */
                if (v.fmode == FM_BP) {
                    v.kind = VK_MEM;
                } else if ((v.fmode == FM_SYM || v.fmode == FM_LAB) &&
                           v.imm == 0) {
                    Val r = {0};
                    r.kind = VK_STATICADDR;
                    r.offset = (v.fmode == FM_LAB) ? v.offset : 0;
                    r.sym = (v.fmode == FM_SYM) ? v.sym : NULL;
                    push_val(&g, r);
                    break;
                } else {
                    gen_fatal("'&' of this 'float'/'double' operand is not yet "
                              "supported - see src/mutos_cc/README.md");
                }
            } else if (is_float_val(&v)) {
                gen_fatal("'&' of a 'float'/'double' value is not supported");
            }
            if (v.kind == VK_STATIC) {
                /* The address of a static object - a string literal's
                 * (c0's putstr() NAME(SC_STATIC, TY_CHAR, <label>) +
                 * AMPER(9)): a link-time constant, no code - see
                 * VK_STATICADDR. */
                Val r = {0};
                r.kind = VK_STATICADDR;
                r.offset = v.offset;
                r.sym = v.sym; /* a file-scope variable's "&x" */
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
            /* Into SI while DI holds a pending value - 06_struct/
             * 03_starray.s.golden's "pts[i].y = i * 2;" -> "mov di,
             * *-18.(bp)" / "sal di,*1" / "lea si,*-16.(bp)" (the right-
             * hand side first - see is_disp_store()). */
            const char *areg = pick_addr_reg(&g);
            ins2(&g, "lea", o_reg(areg), o_val(v));
            push_val(&g, val_reg(areg));
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
                /* Any power of two, by emit_const_shift()'s rule - a
                 * double's 8 "mov cx,*3." / "sal si,cl": fltprobe/
                 * p24_fltinf2.s.golden's "a[i + 1] = 2.0;" -> "lea di,
                 * *-48.(bp)" / "mov si,*-70.(bp)" / "mov cx,*3." / "sal
                 * si,cl" / "add di,si" / "lea ax,*8.(di)". */
                int szlg = exact_log2(sz);
                if (szlg < 0)
                    gen_fatal("ITOP scaling by %ld is not yet supported "
                              "(only an element whose size is a power of "
                              "two)", sz);
                /* A base register either way: the displacement is added
                 * through it ("*2.(si)"). Into DX when DI and SI are both
                 * taken and the array's address is one of them (OP_AMPER's
                 * "lea si,<b>"): the index is added to it there -
                 * fltprobe/p19_open3.s.golden's "a[i] * b[j - 2]" -> "lea
                 * si,*-24.(bp)" / "mov dx,*-28.(bp)" / "sal dx,*1" / "add
                 * si,dx" / "mov ax,di" / "imul *-4.(si)" (as "ps[i].c"'s
                 * index, p10_elem - the constant term the displacement,
                 * not folded into the "lea"). */
                const char *reg = di_busy(&g) ? "si" : "di";
                if (strcmp(reg, "si") == 0 && reg_busy(&g, RB_SI)) {
                    const Val *below = g.valsp >= 1 ? &g.valstack[g.valsp - 1]
                                                    : NULL;
                    if (!below || below->kind != VK_REG || below->regvar ||
                        strcmp(below->reg, "si") != 0 || reg_busy(&g, RB_DX))
                        gen_fatal("a subscript of the form \"i + N\" while both "
                                  "DI and SI hold pending values is not yet "
                                  "supported - see src/mutos_cc/README.md");
                    reg = "dx";
                }
                ins2(&g, "mov", o_reg(reg), o_val(val_from_simple(amt.cl)));
                emit_const_shift(&g, "sal", reg, szlg);
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
                    gen_fatal("a 2-D array subscript while DI holds another "
                              "pending value is not yet supported - see "
                              "src/mutos_cc/README.md");
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
            /* DI, or SI while DI is busy, or - both taken, e.g. by a
             * right-hand side computed first and the array base "lea"'d
             * after it - DX: the index is only ever added to the base
             * register, never addressed through, so DX (the next
             * register) serves: 06_struct/03_starray.s.golden's "pts[i].
             * y = i * 2;" -> "lea si,*-16.(bp)" / "mov dx,*-18.(bp)" /
             * "sal dx,*1" / "sal dx,*1" / "add si,dx" / "mov *2.(si),di",
             * and "sum + pts[i].x + pts[i].y" alike. */
            const char *reg = "di";
            if (di_busy(&g)) {
                reg = "si";
                if (reg_busy(&g, RB_SI)) {
                    if (reg_busy(&g, RB_DX))
                        gen_fatal("a subscript while DI, SI and DX all hold "
                                  "pending values is not yet supported - "
                                  "see src/mutos_cc/README.md");
                    reg = "dx";
                }
            }
            load_into(&g, reg, amt);
            long sz = size.imm;
            /* Any power of two, by emit_const_shift()'s rule: a double's
             * 8 is "mov cx,*3." / "sal si,cl" (fltprobe/p8_misc.s.golden's
             * "arr[i]" - "lea di,*-38.(bp)" / "mov si,*-44.(bp)" / "mov
             * cx,*3." / "sal si,cl" / "add di,si"). */
            int lg = exact_log2(sz);
            if (lg < 0)
                gen_fatal("ITOP scaling by %ld is not yet supported "
                          "(only an element whose size is a power of two)",
                          sz);
            emit_const_shift(&g, "sal", reg, lg);
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
                    /* A struct member's type is its own, mutos_c0 retyping
                     * the member access's STAR/AMPER spine to it (v7's
                     * setype()) - int, unsigned (a bit-field's word) or
                     * any pointer, one word each, like an int. */
                    int is_char = (type == TY_CHAR && atype == TY_PTR_CHAR);
                    int is_int = (ty_is_word(type) && ty_is_ptr(atype) &&
                                  ty_decref(atype) == type);
                    /* A struct member that is itself a struct, its address
                     * taken ("&r.b", "&rp->b"): the member's address, as
                     * for any other member. */
                    int is_struct = (type == TY_STRUCT && ty_is_ptr(atype) &&
                                     ty_decref(atype) == TY_STRUCT);
                    if (!is_char && !is_int && !is_struct)
                        gen_fatal("'&*' with types %d/%d is not yet "
                                  "supported", type, atype);
                    if (top->kind == VK_REGOFF) {
                        /* "&a[j + 1]" (a pointer variable, see OP_PLUS):
                         * the pending constant is added now - 10_integ/
                         * 02_bubsort.s.golden's "add di,*4.(bp)" / "add
                         * di,*2." / "push di". A pointer VARIABLE plus a
                         * constant ("&p->b", nothing in a register yet)
                         * stays pending when another constant follows -
                         * a member of that member, "p->b.c", whose two
                         * offsets become one displacement (see OP_PLUS);
                         * otherwise the pointer is loaded and the offset
                         * added - "&p[2]" the same as "&a[j + 1]". An
                         * element's address with its register already
                         * computed ("p[i + 1].y") keeps the pending offset
                         * the same way. */
                        if (next_is_con_plus(temp1))
                            break;
                        Val ro = pop_any(&g);
                        Val base = val_from_simple(ro.cl);
                        const char *reg = (base.kind == VK_REG)
                                          ? base.reg : pick_addr_reg(&g);
                        load_into(&g, reg, base);
                        if (ro.imm != 0)
                            ins2(&g, "add", o_reg(reg), o_imm(ro.imm));
                        push_val(&g, val_reg(reg));
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
                    if ((is_char || is_struct) && top->kind == VK_MEM_DIRECT)
                        break;   /* "&buf[3]", "&r.b": the address, lea'd later */
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
            if (type == TY_FLOAT || type == TY_DOUBLE) {
                /* Through a pointer variable - loaded into DI only when
                 * the value's address is needed (fp_lea()): p3_global's
                 * "*p = 2.0" -> "lea ax,L10003" / "call flds" / "mov di,
                 * *-22.(bp)" / "lea ax,(di)" / "call fstdp". An element
                 * or member at a constant offset never gets here (the
                 * planner folds it - see "Floating point"). An address
                 * computed into DI - an element "arr[i]" - is used where
                 * it is: p8_misc's "lea di,*-38.(bp)" / ... / "add di,si" /
                 * "lea ax,(di)" / "call fldd" (as an assignment's target
                 * it is computed after the right-hand side - see
                 * plan_fvalue()). */
                Val ptr = pop_any(&g);
                long disp = 0;
                if (ptr.kind == VK_REGOFF) {
                    /* "p->y", "p[2]": the pointer plus a constant, the
                     * constant the displacement - fltprobe/p20_fltlv.s.
                     * golden's "q->y" -> "mov di,*-22.(bp)" / "lea ax,*8.
                     * (di)", "p[2]" -> "lea ax,*16.(di)". */
                    disp = ptr.imm;
                    ptr = val_from_simple(ptr.cl);
                }
                if (ptr.kind != VK_MEM && ptr.kind != VK_STATIC &&
                    !(ptr.kind == VK_REG && !ptr.regvar &&
                      (strcmp(ptr.reg, "di") == 0 ||
                       (strcmp(ptr.reg, "ax") == 0 && disp == 0))))
                    gen_fatal("reading a 'float'/'double' through anything but "
                              "a pointer variable, an address computed into "
                              "DI or a call's result is not yet supported - "
                              "see src/mutos_cc/README.md");
                Val fv = {0};
                fv.kind = VK_FMEM;
                fv.fmode = FM_IND;
                fv.cl = simple_of(ptr);
                fv.imm = disp;
                fv.fdouble = (type == TY_DOUBLE);
                push_val(&g, fv);
                break;
            }
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
            /* A compound assignment through a pointer or subscript: '+=',
             * '-=', '&=', '|=', '^=' into a word combine in place, the target
             * the dereference itself (see is_aspush()/is_asdisp() for the
             * order); '*=' into a word at a constant address too (it ends
             * as a plain memory operand) - fltprobe/p26_elem4.s.golden. */
            if (cons.as_left && cons.op >= OP_ASPLUS && cons.op <= OP_ASXOR &&
                !((is_inplace_asop(cons.op) && ty_is_word(type)) ||
                  (cons.op == OP_ASTIMES && type == TY_INT &&
                   ptr.kind == VK_MEM_DIRECT)))
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
                 * - an element of a file-scope array indexed by a
                 *   variable ("text[i]" - VK_SYMIDX): the index loaded
                 *   into DX and moved to BX, the element "_text(bx)" -
                 *   10_integ/01_wordcount.s.golden's "mov dx,*-6.(bp)" /
                 *   "mov bx,dx" / "cmpb _text(bx),*10."; as the target of
                 *   an assignment it has no golden (the kernel pushes the
                 *   index there - "push *-34.(bp)" ... "pop bx" / "movb
                 *   _amxscd(bx),dx" in kernel_nonopt/amx.s) and is
                 *   refused;
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
                if (ptr.kind == VK_SYMIDX) {
                    if (is_target) {
                        /* An element of a file-scope char array as the
                         * target of an assignment: the INDEX is pushed
                         * (not an address - the symbol is the store's
                         * displacement), the right-hand side computed,
                         * and the index popped into BX for the store:
                         * "amxscd[unit] = scd;" -> "push *-34.(bp)" /
                         * "mov dx,*-30.(bp)" / "pop bx" / "movb
                         * _amxscd(bx),dx" (tests/mutos_as/kernel_nonopt/
                         * amx.s - the int right-hand side through DX, its
                         * ITOC(TY_CHAR)). A constant right-hand side has
                         * no example (as for a char pointer below). */
                        if (!cons.saw_runtime)
                            gen_fatal("storing a constant into an element of "
                                      "a file-scope array is not yet "
                                      "supported - see src/mutos_cc/README.md");
                        ins1(&g, "push", o_val(val_from_simple(ptr.cl)));
                        Val pend = val_ind_pending();
                        pend.sym = ptr.sym;
                        push_val(&g, pend);
                        break;
                    }
                    if (cons.op == OP_ASSIGN && cons.as_left)
                        gen_fatal("storing into an element of a file-scope "
                                  "array inside a larger expression is not "
                                  "yet supported - see src/mutos_cc/README.md");
                    Val idx = val_from_simple(ptr.cl);
                    ins2(&g, "mov", o_reg("dx"), o_val(idx));
                    ins2(&g, "mov", o_reg("bx"), o_reg("dx"));
                    Val el = val_ind("bx");
                    el.sym = ptr.sym;
                    el.bytev = 1;
                    push_val(&g, el);
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
            if (ptr.kind == VK_SYMIDX)
                gen_fatal("internal: a file-scope char array element "
                          "dereferenced as type %d", type);
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
                    /* A non-zero offset takes is_disp_store()'s right-
                     * hand-side-first plan, which leaves the right-hand
                     * side on the value stack (so is_target is not set);
                     * reaching here with one means that plan was not
                     * made (the expression's shape is outside it). */
                    if (type != TY_INT || ptr.imm != 0)
                        gen_fatal("storing a computed value through a pointer "
                                  "plus a non-zero constant offset is not "
                                  "yet supported in this expression shape - "
                                  "see src/mutos_cc/README.md");
                    Val base = val_from_simple(ptr.cl);
                    ins1(&g, "push", o_val(base));
                    push_val(&g, val_ind_pending());
                    break;
                }
                /* The pointer into DI, or SI while DI holds a pending
                 * value (see pick_addr_reg()); one already in a base
                 * register is used where it is. */
                Val base = val_from_simple(ptr.cl);
                const char *reg;
                if (base.kind == VK_REG && is_base_reg(base.reg)) {
                    reg = base.reg;
                } else {
                    reg = pick_addr_reg(&g);
                    load_into(&g, reg, base);
                }
                Val ind = val_ind_disp(reg, ptr.imm);
                /* A comparison's left element at an offset whose address
                 * is about to be pushed (SEG_KEEPIND - is_pushleft()) stays
                 * "*N.(di)": fltprobe/p23_elem3.s.golden's "ps[i].c >
                 * b[j]" -> ... "add di,si" / "push di" / ... / "pop bx" /
                 * "cmp *4.(bx),di". */
                if (load_now(&cons) && !g.keep_ind) {
                    ins2(&g, "mov", o_reg(reg), o_val(ind));
                    push_val(&g, val_reg(reg));
                    break;
                }
                g.keep_ind = 0;
                for (int k = 0; k < g.ntelem; k++)
                    if (g.telem[k] == g.op_off)
                        ind.ortest = 1;         /* see Val's ortest */
                push_val(&g, ind);
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
            /* The pointer into DI - or, while DI holds a pending value,
             * SI (pick_addr_reg()); an address already in a base
             * register (a subscript's sum) is dereferenced where it is. */
            const char *preg;
            if (ptr.kind == VK_REG && is_base_reg(ptr.reg) && !ptr.regvar) {
                preg = ptr.reg;
            } else {
                preg = pick_addr_reg(&g);
                load_into(&g, preg, ptr);
            }
            if (load_now(&cons) && !g.keep_ind) {
                ins2(&g, "mov", o_reg(preg), o_val(val_ind(preg)));
                push_val(&g, val_reg(preg));
                break;
            }
            g.keep_ind = 0;
            Val pind = val_ind(preg);
            for (int k = 0; k < g.ntelem; k++)
                if (g.telem[k] == g.op_off)
                    pind.ortest = 1;            /* see Val's ortest */
            push_val(&g, pind);
            break;
        }

        case OP_INCBEF:
        case OP_DECBEF:
        case OP_INCAFT:
        case OP_DECAFT: {
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_DOUBLE) {
                gen_fp_incdec(&g, temp1, op);  /* see "Floating point" */
                break;
            }
            if (type == TY_LONG) {
                /* A long variable incremented as a statement of its own:
                 * in place, as "l += 1" - see long_inplace_const(). */
                Val amt = pop_val_ex(&g, POP_LPAIR);
                Val lv  = pop_val(&g);
                Consumer lc = scan_consumer(temp1);
                if (lc.op == OP_LTOI && lv.kind == VK_MEM &&
                    (amt.kind == VK_LCON || amt.kind == VK_IMM)) {
                    /* Its value truncated to an int ("x = l++"): the real
                     * compiler distributes the LTOI into the '++' as into
                     * a '+' (v7's unoptim() - LTOI(l) the low word,
                     * LTOI(ITOL(1)) the 1) and increments the LOW WORD as
                     * an int, the carry into the high word lost -
                     * fltprobe/p31_long4.s.golden's "x = l++;" -> "mov
                     * di,*-6.(bp)" / "inc *-6.(bp)" / "mov *-16.(bp),di"
                     * (its own wrong code for a low word of 0xffff), the
                     * '++' right after the load, not delayed to the end of
                     * the statement as an int "x = i++" is (v7's delay()
                     * looks for the postfix operator right under the
                     * assignment, not under an LTOI): the regtab "%a,1",
                     * "mov A1',R" / "inc A1''". The prefix forms, "x =
                     * ++l", are inferred the same way (gen_incdec()). The
                     * result stands for its own LTOI (`lowonly`). */
                    Val low = lv;
                    low.offset += MCC_SZINT;
                    Val res;
                    if (op == OP_INCAFT || op == OP_DECAFT) {
                        int is_incr = (op == OP_INCAFT);
                        load_into_di(&g, low);
                        if ((int16_t)amt.imm == 1)
                            ins1(&g, is_incr ? "inc" : "dec", o_val(low));
                        else
                            ins2(&g, is_incr ? "add" : "sub", o_val(low),
                                 o_imm((int16_t)amt.imm));
                        res = val_reg("di");
                        res.postfix = 1;
                    } else {
                        res = gen_incdec(&g, op, low,
                                         val_imm((int16_t)amt.imm));
                    }
                    res.lowonly = 1;
                    push_val(&g, res);
                    break;
                }
                if (lc.op != OP_EXPR ||
                    !long_inplace_const(&g, op == OP_DECBEF || op == OP_DECAFT,
                                        lv, amt))
                    gen_fatal("a 'long' '++'/'--' whose value is used, or of "
                              "anything but a 'long' variable, is not yet "
                              "supported - see src/mutos_cc/README.md");
                push_val(&g, lv);
                break;
            }
            if (type != TY_INT && !ty_is_ptr(type))
                gen_fatal("'++'/'--' of type %d not yet supported "
                          "(only an int or a pointer is covered so far)",
                          type);
            Val amt = pop_val(&g);
            Val lv  = pop_val(&g);
            if (scan_consumer(temp1).op == OP_EXPR && lv.kind == VK_MEM &&
                amt.kind == VK_IMM) {
                /* A statement of its own, its value unused: the increment
                 * alone, in place - v7's efftab - tests/mutos_cc/11_kernel/
                 * 01_delay.s.golden's "i++;" -> "inc *-6.(bp)",
                 * fltprobe/p20_fltlv.s.golden's "p++;" -> "add *-48.
                 * (bp),*8.". */
                int is_incr = (op == OP_INCBEF || op == OP_INCAFT);
                if (amt.imm == 1)
                    ins1(&g, is_incr ? "inc" : "dec", o_val(lv));
                else
                    ins2(&g, is_incr ? "add" : "sub", o_val(lv), o_imm(amt.imm));
                push_val(&g, lv);
                break;
            }
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
            if (g.valsp >= 2 && (is_float_val(&g.valstack[g.valsp - 1]) ||
                                 is_float_val(&g.valstack[g.valsp - 2]))) {
                gen_fp_compare(&g, op);   /* see "Floating point" */
                break;
            }
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
            /* A char compared with a small constant - mutos_c0 writes
             * the constant typed char and the char operand unconverted
             * (10_integ/01_wordcount.1.golden: STAR(1) CON(1, 10)
             * EQUAL(0)): a byte compare, whose two shapes live in
             * emit_byte_cmp_and_branch(). Nothing is swapped - the
             * constant is already on the right, as v7's optim() puts it
             * before its own CHAR retyping. */
            if (g.valsp >= 2 && g.valstack[g.valsp - 1].charcon) {
                pop_operands_ex(&g, &l, &r, POP_BYTE);
                if (!l.bytev || r.bytev || r.kind != VK_IMM)
                    gen_fatal("internal: a char-typed constant compared with "
                              "something other than a char in memory");
                l.bytev = 0;
                Val c = {0};
                c.kind = VK_COND;
                c.true_op = op;
                c.cl = simple_of(l);
                c.cr = simple_of(r);
                c.cond_is_byte = 1;
                push_val(&g, c);
                break;
            }
            pop_operands_ex(&g, &l, &r, POP_CHARX);
            int lrel = 0;
            for (int k = 0; k < g.nlrel; k++)
                if (g.lrel[k] == g.op_off)
                    lrel = 1;
            if (lrel || l.kind == VK_LCON || l.kind == VK_LONG ||
                r.kind == VK_LCON || r.kind == VK_LONG) {
                push_val(&g, gen_long_relop(&g, op, l, r));
                break;
            }
            int relop = op;
            if (l.kind == VK_CHARX || r.kind == VK_CHARX) {
                /* A char compared with an int that is not a char-typed
                 * constant (a variable, a register local, a constant
                 * outside 0..127 - mutos_c0 widened the char): the char
                 * goes left - v7's optim() exchanges a relational's
                 * operands when degree(left) < degree(right), and a char
                 * leaf's degree (1) is above an int leaf's (0) or a
                 * constant's (-3) - is widened into AX, and AX is
                 * compared with the other operand: tests/mutos_as/
                 * kernel_opt/ifss.s's "movb ax,*22.(di)" / "cbw" / "cmp
                 * ax,*-6.(bp)" / "jne", cons_NOBIOS.s's "movb ax,
                 * _kennung(bx)" / "cbw" / "cmp ax,*-8.(bp)", and 22 "cbw"
                 * / "cmp ax,di" there. Two chars have no example. */
                if (l.kind == VK_CHARX && r.kind == VK_CHARX)
                    gen_fatal("comparing two 'char' values is not yet "
                              "supported - see src/mutos_cc/README.md");
                if (r.kind == VK_CHARX) {
                    Val t = l;
                    l = r;
                    r = t;
                    relop = find_relop(op)->mirror;
                }
                if (!is_int_leaf(&r))
                    gen_fatal("comparing a 'char' with a computed int value is "
                              "not yet supported - see src/mutos_cc/README.md");
                l = load_charx(&g, l);
            } else if (is_const_val(&l) && !is_const_val(&r)) {
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
            c.cond_memleft = l.memleft;      /* see SEG_DEFPOPL */
            if (l.kind == VK_IND && !l.bytev && !l.sym &&
                r.kind == VK_IMM && r.imm == 0)
                for (int k = 0; k < g.nzelem; k++)
                    if (g.zelem[k] == g.op_off)
                        c.cond_ortest = 1;
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
            /* A char condition is tested as a byte - see as_cond(). */
            gen_cond_branch(&g, pop_val_ex(&g, POP_BYTE), lbl, cond_sense,
                            g.defer_floor);
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
            /* "!c" on a char: tested as a byte - see as_cond(). */
            Val v = pop_val_ex(&g, POP_BYTE);
            Val c = as_cond(v);
            Val neg = {0};
            neg.kind = VK_COND;
            neg.true_op = cond_invert(c.true_op);
            neg.cl = c.cl;
            neg.cr = c.cr;
            /* The operand width goes with the comparison. Dropping
             * cond_is_long made "if (!(l > 0L))" a 16-bit compare with
             * an operand placeholder as its text ("cmp *-8.(bp),
             * <unmaterialized-long-const>"), exit status 0 - found
             * 2026-09-26; now gen_long_cmp() refuses the inverted
             * operator, as it refuses any other unconfirmed shape. */
            neg.cond_is_long = c.cond_is_long;
            neg.cond_is_ltest = c.cond_is_ltest;
            neg.cond_is_byte = c.cond_is_byte;
            neg.cond_memleft = c.cond_memleft;
            /* An element read through a computed address, tested (see Val's
             * ortest): "!b[i]" is v7's cbranch() of b[i] inverted - loaded
             * and tested as for "if (b[i])". */
            neg.cond_ortest = c.cond_ortest;
            /* A floating comparison has already set the flags; "!" only
             * inverts which branch reads them (v7's optim() turns "!(a <
             * b)" into "a >= b" - the same code). */
            neg.cond_is_float = c.cond_is_float;
            neg.flags_at = c.flags_at;
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
            /* A char argument stays a byte operand in the list until
             * push_call_arg() widens it; a floating one until it is
             * pushed (push_fp_arg()). */
            Val rhs = pop_val_ex(&g, POP_BYTE | POP_CHARX | POP_FLOAT);
            Val lhs = pop_val_ex(&g, POP_BYTE | POP_CHARX | POP_FLOAT);
            if (rhs.kind == VK_FDONE || lhs.kind == VK_FDONE)
                gen_fatal("internal: a spent floating assignment as a call "
                          "argument");
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
            if (!ty_is_word(type) && type != TY_LONG && type != TY_DOUBLE)
                gen_fatal("a call returning type %d is not yet supported "
                          "(only a function returning an int, a long, a "
                          "double or a pointer is covered so far)", type);
            /* A lone char argument is widened as it is pushed - see
             * push_call_arg(); a floating one is pushed as a double. */
            Val args = pop_val_ex(&g, POP_BYTE | POP_CHARX | POP_FLOAT);
            if (args.kind == VK_FDONE)
                gen_fatal("internal: a spent floating assignment as a call "
                          "argument");
            Val callee = pop_val(&g);
            Val res = gen_call(&g, callee, args, type == TY_LONG);
            if (type == TY_DOUBLE) {
                /* The callee left its value in dmath.o's "fac" and AX
                 * pointing at it (see gen_fp_rforce()): loaded from
                 * there, "call fldd" - atof.o's "call _ldexp" / "add
                 * sp,*10." / "call fldd". Unused, it would be left on
                 * the floating-point stack - discard_val() refuses that. */
                ins1(&g, "call", o_sym("fldd"));
                g.nfloat = 1;
                res = val_facc();
            }
            push_val(&g, res);
            break;
        }

        case OP_TIMES: {
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_DOUBLE) {
                gen_fp_binop(&g, op);    /* see "Floating point" */
                break;
            }
            if (type == TY_LONG) {
                Val r = pop_val(&g);
                Val l = pop_val(&g);
                constant_to_right(&l, &r);
                push_val(&g, gen_long_binop_call(&g, l, r, "lmul"));
                break;
            }
            /* The low word of a product is the same signed or unsigned. */
            if (type != TY_INT && type != TY_UNSIGN)
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
            pop_operands_ex(&g, &l, &r, POP_CHARX);
            constant_to_right(&l, &r);
            if ((l.kind == VK_MEM || l.kind == VK_STATIC) && !l.bytev &&
                !l.structv && r.kind == VK_IND && !r.bytev) {
                /* A variable times a dereference: v7's acommute() orders a
                 * commutative operator's operands by decreasing degree, so
                 * the dereference (computed) goes first, into AX, and the
                 * variable is the IMUL operand - fltprobe/p23_elem3.s.
                 * golden's "x * b[j]" -> "lea di,*-36.(bp)" / ... / "add
                 * di,si" / "mov ax,(di)" / "imul *-44.(bp)". */
                Val t = l;
                l = r;
                r = t;
            }
            if (l.kind == VK_CHARX || r.kind == VK_CHARX) {
                /* A char times an int: the char widened into AX, then the
                 * ordinary shape below with AX as the "mov ax,<left>"
                 * side - "movb ax,*52.(di)" / "cbw" / "mov ax,ax" / "mov
                 * cx,*20." / "imul cx" (tests/mutos_as/kernel_nonopt/
                 * amx.s, 5x). See gen_charx_binop(). The constant checks
                 * below would refuse a power of two only after the load,
                 * so they are made first. */
                charx_left(op, &l, &r);
                if (r.kind == VK_IMM && exact_log2(r.imm) >= 1) {
                    /* v7's pow2(): a shift - see below. */
                    push_val(&g, gen_charx_binop(&g, OP_LSHIFT, l,
                                                 val_imm(exact_log2(r.imm))));
                    break;
                }
                if (r.kind == VK_IMM && r.imm <= 1)
                    gen_fatal("multiplying by the constant %ld is not yet "
                              "supported - no golden reference confirms the "
                              "shape a real compiler would emit", r.imm);
                l = load_charx(&g, l);
            }
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
            if (imul_side.kind == VK_IMM && exact_log2(imul_side.imm) >= 1) {
                /* A power-of-two multiplier is a left shift - v7/cc/c10.c's
                 * pow2() turns TIMES by 2^k into LSHIFT by k before any code
                 * is chosen - and so gets the shift's own code: 06_struct/
                 * 03_starray.s.golden's "i * 2" -> "mov di,*-18.(bp)" / "sal
                 * di,*1" (see OP_LSHIFT/emit_const_shift()). */
                if (!(ax_side.kind == VK_REG && strcmp(ax_side.reg, "di") == 0))
                    require_free(imul_side, RB_DI, "TIMES");
                load_into_di(&g, ax_side);
                emit_const_shift(&g, "sal", "di", exact_log2(imul_side.imm));
                push_val(&g, val_reg("di"));
                break;
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
                if (imul_side.imm == 0 || imul_side.imm == 1)
                    gen_fatal("multiplying by the constant %ld is not yet "
                              "supported - no golden reference confirms the "
                              "shape a real compiler would emit (v7 folds or "
                              "drops it)", imul_side.imm);
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
            if (type == TY_DOUBLE) {
                gen_fp_binop(&g, op);    /* see "Floating point" - '%' is
                                          * refused there */
                break;
            }
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
            if (g.valsp >= 1 && g.valstack[g.valsp - 1].kind == VK_STACKED) {
                /* The right operand was evaluated first and spilled (see
                 * order_right_first()): the left one from its working
                 * register into AX, sign-extended, the spilled one popped
                 * into CX and the divisor - fltprobe/p26_elem4.s.golden's
                 * "mov di,(di)" / "mov ax,di" / "cwd" / "pop cx" / "idiv
                 * cx" (v7's "%n,n" again). */
                (void)pop_any(&g);
                Val l = materialize(&g, pop_val(&g));
                if (l.kind == VK_IND) {
                    ins2(&g, "mov", o_reg(l.reg), o_val(l));
                    l = val_reg(l.reg);
                }
                if (l.kind != VK_REG || strcmp(l.reg, "ax") == 0 ||
                    strcmp(l.reg, "dx") == 0)
                    gen_fatal("internal: the left operand of a division with "
                              "a spilled right operand is expected in a "
                              "working register other than ax and dx");
                emit_cwd_from(&g, o_reg(l.reg));
                ins1(&g, "pop", o_reg("cx"));
                ins1(&g, "idiv", o_reg("cx"));
                push_val(&g, val_reg(op == OP_DIVIDE ? "ax" : "dx"));
                break;
            }
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
            if (type == TY_DOUBLE && (op == OP_ASPLUS || op == OP_ASMINUS)) {
                gen_fp_asop(&g, temp1, op);     /* see "Floating point" */
                break;
            }
            if (type == TY_LONG && (op == OP_ASPLUS || op == OP_ASMINUS)) {
                /* A long variable += an int widened (OP_ITOL, in DI:SI):
                 * added into memory, low word then high - fltprobe/
                 * p19_open3.s.golden's "l += i" -> "mov ax,*-26.(bp)" /
                 * "cwd" / "mov di,dx" / "mov si,ax" / "add *-6.(bp),si" /
                 * "adc *-8.(bp),di"; '-=' the same with "sub"/"sbb" (no
                 * golden). Any other right-hand side has none. */
                Val rhs = pop_val_ex(&g, POP_LPAIR);
                Val lhs = pop_val(&g);
                if (long_inplace_const(&g, op == OP_ASMINUS, lhs, rhs)) {
                    push_val(&g, lhs);
                    break;
                }
                if (lhs.kind == VK_MEM && rhs.kind == VK_LCON &&
                    !(g.reserved & (RB_DI | RB_SI))) {
                    /* Any other long constant: into DI:SI as for an
                     * assignment (materialize_long()), then added from
                     * there - fltprobe/p31_long4.s.golden's "l += 70000;"
                     * -> "mov si,#4464." / "mov di,*1." / "add *-6.(bp),si"
                     * / "adc *-8.(bp),di" (v7's "%a,nl": S, then the two
                     * words; an int constant widened that is negative is
                     * an LCON to v7's unoptim() too - inferred). */
                    rhs = materialize_long(&g, rhs);
                }
                if (lhs.kind != VK_MEM || rhs.kind != VK_LONG ||
                    rhs.lreg != LREG_DISI || rhs.lpair)
                    gen_fatal("'long' %s with anything but an int or unsigned "
                              "value added to a long variable is not yet "
                              "supported - see src/mutos_cc/README.md",
                              aluop(op)->name);
                const AluOp *a = aluop(op == OP_ASPLUS ? OP_PLUS : OP_MINUS);
                ins2(&g, a->mnem, o_mem(lhs.offset + MCC_SZINT), o_reg("si"));
                ins2(&g, a->mnem_hi, o_mem(lhs.offset), o_reg("di"));
                push_val(&g, lhs);
                break;
            }
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val rhs = materialize(&g, pop_val(&g));
            Val lhs = pop_val(&g);
            if (lhs.kind == VK_IND && !lhs.bytev && !lhs.sym) {
                /* Through a pointer or into an element (see is_aspush(),
                 * is_asdisp()): in place, the right-hand side a constant
                 * or already in a register - fltprobe/p26_elem4.s.golden's
                 * "add (di),*2.", "add (bx),di", "sub (bx),di", "add *4.
                 * (si),di". */
                if (!(rhs.kind == VK_IMM ||
                      (rhs.kind == VK_REG && !rhs.regvar &&
                       strcmp(rhs.reg, lhs.reg) != 0)))
                    gen_fatal("a compound assignment through a pointer with "
                              "this right-hand side is not yet supported - "
                              "see src/mutos_cc/README.md");
                ins2(&g, aluop(op)->mnem, o_val(lhs), o_val(rhs));
                break;
            }
            if (lhs.kind != VK_MEM)
                gen_fatal("compound assignment to a non-memory lvalue is "
                          "not yet supported");
            if ((op == OP_ASPLUS || op == OP_ASMINUS) && rhs.kind == VK_REG &&
                strcmp(rhs.reg, "ax") == 0 && !rhs.regvar) {
                /* A right-hand side computed into AX - a floating value
                 * converted ("i += d", mutos_c0's FTOI, as for "i *= e"):
                 * fltprobe/p8_misc.s.golden's "call ftoi" / "add
                 * *-44.(bp),ax" and "call ftoi" / "sub *-44.(bp),ax" - in
                 * place, like a constant. A call's result ("i += f()")
                 * is the same value in AX and takes the same shape - an
                 * inference (fltprobe/p11_itof2 asks). */
                ins2(&g, aluop(op)->mnem, o_val(lhs), o_reg("ax"));
                break;
            }
            if (rhs.kind == VK_IND && !rhs.bytev && !rhs.sym &&
                !rhs.ortest) {
                /* An element read through a computed address combined into
                 * a variable or a constant-index element: loaded into its
                 * register, then the operation in place - v7's efftab "%aw,n"
                 * (S, "I R,A1") - fltprobe/p32_elem5.s.golden's "b[3] ^=
                 * b[i];" -> "lea di,*-56.(bp)" / "mov si,*-60.(bp)" / "sal
                 * si,*1" / "add di,si" / "mov di,(di)" / "xor *-50.(bp),di". */
                ins2(&g, "mov", o_reg(rhs.reg), o_val(rhs));
                rhs = val_reg(rhs.reg);
            }
            if (rhs.kind == VK_REG && !rhs.regvar && is_inplace_asop(op) &&
                (strcmp(rhs.reg, "di") == 0 || strcmp(rhs.reg, "si") == 0)) {
                /* A right-hand side computed into a working register - the
                 * same "%aw,n" (an element above; any other computed value
                 * inferred the same way). */
                ins2(&g, aluop(op)->mnem, o_val(lhs), o_val(rhs));
                break;
            }
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
            if (type == TY_DOUBLE) {
                gen_fp_asop(&g, temp1, op);     /* see "Floating point" */
                break;
            }
            if (type == TY_LONG) {
                /* A long variable *= an int widened: the runtime's
                 * in-place multiply, "almul", given the right operand's two
                 * words (pushed from DX:AX - OP_ITOL) and the target's
                 * address - fltprobe/p22_long2.s.golden's "l *= i" -> "mov
                 * ax,*-14.(bp)" / "cwd" / "push ax" / "push dx" / "lea
                 * di,*-8.(bp)" / "push di" / "call almul" / "add sp,*6."
                 * (no store: almul writes the product itself). */
                Val rhs = pop_val(&g);
                Val lhs = pop_val(&g);
                if (lhs.kind != VK_MEM || rhs.kind != VK_LONG ||
                    rhs.lreg != LREG_DXAX || rhs.lpair)
                    gen_fatal("'long' *= with anything but an int value "
                              "multiplied into a long variable is not yet "
                              "supported - see src/mutos_cc/README.md");
                if (g.reserved & RB_DI)
                    gen_fatal("'long' *= in a function with a register "
                              "variable in DI is not yet supported");
                put_seq(&g, SEQ_PUSH_AXDX);
                ins2(&g, "lea", o_reg("di"), o_mem(lhs.offset));
                ins1(&g, "push", o_reg("di"));
                ins1(&g, "call", o_sym("almul"));
                ins2(&g, "add", o_reg("sp"), o_imm(6));
                push_val(&g, lhs);
                break;
            }
            if (type != TY_INT)
                gen_fatal("ASTIMES of type %d not yet supported", type);
            Val rhs = materialize(&g, pop_val(&g));
            Val lhs = pop_val(&g);
            if (lhs.kind != VK_MEM)
                gen_fatal("compound assignment to a non-memory lvalue is "
                          "not yet supported");
            if (rhs.kind == VK_IND && !rhs.bytev && !rhs.sym) {
                /* An element: loaded in place first - fltprobe/p26_elem4.s.
                 * golden's "b[0] *= b[i];" -> ... "mov di,(di)" / "mov
                 * ax,di" / "imul *-28.(bp)" / "mov *-28.(bp),ax". */
                ins2(&g, "mov", o_reg(rhs.reg), o_val(rhs));
                rhs = val_reg(rhs.reg);
            }
            if (rhs.kind == VK_REG && !rhs.regvar &&
                strcmp(rhs.reg, "dx") != 0) {
                /* A right-hand side computed into a register - the
                 * template's "mov ax,R" / "imul A1" / "mov A1,ax": a
                 * floating value converted ("i *= e", mutos_c0's FTOI), R =
                 * AX itself - fltprobe/p2_arith.s.golden's "call ftoi" /
                 * "mov ax,ax" / "imul *-30.(bp)" / "mov *-30.(bp),ax" - or
                 * DI (p26_elem4's element, above). */
                ins2(&g, "mov", o_reg("ax"), o_reg(rhs.reg));
                ins1(&g, "imul", o_val(lhs));
                ins2(&g, "mov", o_val(lhs), o_reg("ax"));
                break;
            }
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
            if (type == TY_DOUBLE) {
                gen_fp_asop(&g, temp1, op);     /* see "Floating point" - '%=' is
                                          * refused there */
                break;
            }
            if (type != TY_INT)
                gen_fatal("%s of type %d not yet supported", aluop(op)->name,
                          type);
            Val rhs = materialize(&g, pop_val(&g));
            Val lhs = pop_val(&g);
            if (lhs.kind != VK_MEM)
                gen_fatal("compound assignment to a non-memory lvalue is "
                          "not yet supported");
            if (rhs.kind == VK_REG && strcmp(rhs.reg, "ax") == 0 &&
                !rhs.regvar) {
                /* A right-hand side computed into AX - a floating value
                 * converted ("i /= e", mutos_c0's FTOI): moved out of the
                 * way into CX, the divisor, before the target is loaded -
                 * fltprobe/p13_open.s.golden's "call ftoi" / "mov cx,ax" /
                 * "mov ax,*-40.(bp)" / "cwd" / "idiv cx" / "mov
                 * *-40.(bp),ax". A call's result ("i /= f()", "i %= f()")
                 * is the same value in AX and is given the same shape - an
                 * inference, as for "i += f()" (which p11_itof2 then
                 * confirmed). */
                ins2(&g, "mov", o_reg("cx"), o_reg("ax"));
                emit_cwd_from(&g, o_val(lhs));
                ins1(&g, "idiv", o_reg("cx"));
                ins2(&g, "mov", o_val(lhs), o_reg(op == OP_ASDIV ? "ax" : "dx"));
                break;
            }
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
            if (type == TY_DOUBLE) {
                gen_fp_assign(&g, temp1); /* see "Floating point" */
                break;
            }
            if (g.valsp >= 2 && g.valstack[g.valsp - 2].kind == VK_FIELD) {
                /* "f.mode = 2;" - see OP_FSEL and gen_field_store(). The
                 * assigned constant is the expression's value. */
                Val rhs = pop_val(&g);
                Val field = pop_any(&g);
                gen_field_store(&g, field, rhs);
                push_val(&g, rhs);
                break;
            }
            if (type == TY_STRUCT) {
                /* A whole-struct assignment, "p2 = p1;": ASSIGN(4), then
                 * STRASG(4, size) (see OP_STRASG). v7/cc/c10.c's strasg()
                 * retypes a struct of at most 4 bytes as a long (2 bytes:
                 * an int) and assigns that - here the long ASSIGN's own
                 * shape, the source loaded low word into SI, high word
                 * into DI, and stored low then high: 06_struct/
                 * 04_stassign.s.golden's "mov si,*-6.(bp)" / "mov di,
                 * *-8.(bp)" / "mov *-10.(bp),si" / "mov *-12.(bp),di". A
                 * larger struct (a block copy) and any operand but two
                 * struct variables have no golden. */
                long pos = ftell(temp1);
                int next = c1_read_op(temp1, "temp1");
                int size = -1;
                if (next == OP_STRASG) {
                    (void)c1_read_num(temp1, "temp1");
                    size = c1_read_num(temp1, "temp1");
                }
                if (fseek(temp1, pos, SEEK_SET) != 0)
                    gen_fatal("internal: temp1 is not seekable (fseek failed)");
                Val rhs = pop_val_ex(&g, POP_STRUCT);
                Val lhs = pop_val_ex(&g, POP_STRUCT);
                if (!rhs.structv || !lhs.structv || rhs.kind != VK_MEM ||
                    lhs.kind != VK_MEM)
                    gen_fatal("a struct assignment other than between two "
                              "struct variables is not yet supported - see "
                              "src/mutos_cc/README.md");
                if (size == 4) {
                    ins2(&g, "mov", o_reg("si"), o_mem(rhs.offset + MCC_SZINT));
                    ins2(&g, "mov", o_reg("di"), o_mem(rhs.offset));
                    ins2(&g, "mov", o_mem(lhs.offset + MCC_SZINT), o_reg("si"));
                    ins2(&g, "mov", o_mem(lhs.offset), o_reg("di"));
                    push_val(&g, val_long());
                } else if (size == 2) {
                    /* v7: "setype(tree, INT)" - an int "x = y;" (see the
                     * memory-to-memory case below). */
                    if (g.reserved & RB_DI)
                        gen_fatal("a struct assignment in a function with a "
                                  "register variable in DI is not yet "
                                  "supported");
                    ins2(&g, "mov", o_reg("di"), o_mem(rhs.offset));
                    ins2(&g, "mov", o_mem(lhs.offset), o_reg("di"));
                    push_val(&g, val_reg("di"));
                } else {
                    gen_fatal("assigning a struct of %d bytes is not yet "
                              "supported (only 2 or 4 bytes) - see "
                              "src/mutos_cc/README.md", size);
                }
                break;
            }
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
                Val rhs = pop_val_ex(&g, POP_LPAIR);
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
                /* low word, then high word - from SI and DI, or DX and SI
                 * (see Val's `lpair`) */
                ins2(&g, "mov", o_mem(lhs.offset + MCC_SZINT),
                     o_reg(rhs.lpair ? "dx" : "si"));
                ins2(&g, "mov", o_mem(lhs.offset),
                     o_reg(rhs.lpair ? "si" : "di"));
                push_val(&g, rhs);
                break;
            }
            /* int, char, or any pointer (TY_PTR_FUNC_INT, "int **",
             * 05_arrptr/05_arrofptr.1.golden's "char *" element ASSIGN(9)
             * - one word, whatever it points to). */
            if (!ty_is_word(type) && type != TY_CHAR)
                gen_fatal("ASSIGN of type %d not yet supported", type);
            if ((type == TY_INT || type == TY_UNSIGN) && g.valsp >= 1 &&
                g.valstack[g.valsp - 1].kind == VK_LOWADD) {
                /* "x = (int) (l - c)" - the low words only (see VK_LOWADD),
                 * computed in DI: fltprobe/p22_long2.s.golden's "x = (int)
                 * (l - 5)" -> "mov di,*-6.(bp)" / "add di,*-5." / "mov
                 * *-16.(bp),di". */
                Val w = pop_any(&g);
                if (di_busy(&g) || (g.reserved & RB_DI))
                    gen_fatal("'(int)' of a long sum or difference while DI "
                              "is taken is not yet supported - see "
                              "src/mutos_cc/README.md");
                ins2(&g, "mov", o_reg("di"), o_mem(w.offset));
                if (w.imm == 1)
                    ins1(&g, "inc", o_reg("di"));
                else if (w.imm == -1)
                    ins1(&g, "dec", o_reg("di"));
                else if (w.imm != 0)
                    ins2(&g, "add", o_reg("di"), o_imm(w.imm));
                push_val(&g, val_reg("di"));
            }
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
                 * "movb dx,*-6.(bp)" / "pop bx" / "movb (bx),dx". A file-
                 * scope array element is loaded through v7's "#1" operand,
                 * "movb dx,#_amxi_bu(bx)" / "movb *-10.(bp),dx" in tests/
                 * mutos_as/kernel_nonopt/amx.s - see o_load(). */
                rhs.bytev = 0;
                ins2(&g, "movb", o_reg("dx"), o_load(rhs));
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
                /* A file-scope array's element keeps its symbol as the
                 * displacement - "movb _amxscd(bx),dx" (see OP_STAR). */
                const char *esym = lhs.sym;
                lhs = val_ind("bx");
                lhs.sym = esym;
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
            } else if (rhs.kind == VK_MEM ||
                       (rhs.kind == VK_STATIC && lhs.kind != VK_REG)) {
                /* "x = y;" between two memory operands - 8086 MOV takes
                 * one: the value goes through DI, the working register, as
                 * v7's "S / mov R,A1" template computes the right-hand side
                 * into it - 10_integ/03_linklist.s.golden's "head = cur;"
                 * and "cur = head;": "mov di,*-8.(bp)" / "mov *-6.(bp),di".
                 * A static or file-scope operand is the same memory operand
                 * to MOV (before this, two local statics were written as the
                 * impossible "mov L4,L5"). With a 'register' local in DI the
                 * real compiler uses another register - DX in tests/
                 * mutos_as/kernel_nonopt/amx.s, whose function has two -
                 * which no golden settles for one: refused. Into a
                 * 'register' local it is a plain "mov di,_y" (below). */
                if (g.reserved & RB_DI)
                    gen_fatal("a memory-to-memory assignment (\"x = y;\") in a "
                              "function with a register variable in DI is not "
                              "yet supported - no golden confirms the register "
                              "a real compiler routes it through there");
                require_free(lhs, RB_DI, "ASSIGN");
                load_into_di(&g, rhs);
                rhs = val_reg("di");
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

        case OP_STRASG:
            /* The struct assignment's size, already read by OP_ASSIGN (see
             * there) - "BNN", STRASG, STRUCT, size - its value passed on
             * for OP_EXPR to discard. */
            (void)c1_read_num(temp1, "temp1");
            (void)c1_read_num(temp1, "temp1");
            break;

        case OP_FSEL: {
            /* A bit-field of the word the STAR below it reads - "BNNN":
             * FSEL, TY_UNSIGN, bitoffs, flen (v7/cc/c04.c writes the
             * field's width and offset the same way). As the target of an
             * assignment it becomes a VK_FIELD (gen_field_store()), any
             * other use reads its value (gen_field_load()). */
            int type = c1_read_num(temp1, "temp1");
            int bitoffs = c1_read_num(temp1, "temp1");
            int flen = c1_read_num(temp1, "temp1");
            if (type != TY_UNSIGN)
                gen_fatal("FSEL of type %d not yet supported", type);
            if (flen < 1 || flen > 15 || bitoffs < 0 || bitoffs + flen > 16)
                gen_fatal("a bit-field of %d bits at bit %d is not yet "
                          "supported - see src/mutos_cc/README.md",
                          flen, bitoffs);
            Consumer cons = scan_consumer(temp1);
            Val word = pop_val(&g);
            if (word.kind != VK_MEM && word.kind != VK_STATIC &&
                word.kind != VK_IND)
                gen_fatal("internal: a bit-field's word is not a memory "
                          "operand");
            if (cons.as_left && cons.op == OP_ASSIGN) {
                Val f = {0};
                f.kind = VK_FIELD;
                f.cl = simple_of(word);
                f.bitoffs = bitoffs;
                f.flen = flen;
                push_val(&g, f);
                break;
            }
            if (cons.as_left &&
                ((cons.op >= OP_ASPLUS && cons.op <= OP_ASXOR) ||
                 cons.op == OP_INCBEF || cons.op == OP_DECBEF ||
                 cons.op == OP_INCAFT || cons.op == OP_DECAFT))
                gen_fatal("changing a bit-field other than by a plain "
                          "assignment is not yet supported - see "
                          "src/mutos_cc/README.md");
            push_val(&g, gen_field_load(&g, word, bitoffs, flen));
            break;
        }

        case OP_RFORCE: {
            int type = c1_read_num(temp1, "temp1");
            if (type == TY_DOUBLE || type == TY_FLOAT) {
                gen_fp_rforce(&g);       /* see "Floating point" */
                break;
            }
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
            /* RFORCE carries the type of the value converted to the
             * function's (v7/cc/c04.c's doret()): unsigned for a bit-field
             * returned from an int function - 06_struct/07_bitfield.1.
             * golden's RFORCE(7) - the same word in AX; a pointer from a
             * function returning one - fltprobe/p20_fltlv.s.golden's
             * "return a + i;" in "double *pick()" -> "mov di,*6.(bp)" /
             * "mov cx,*3." / "sal di,cl" / "add di,*4.(bp)" / "mov ax,di". */
            if (type != TY_INT && type != TY_UNSIGN && !ty_is_ptr(type))
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
            /* Every statement leaves the floating-point stack as it found
             * it - see fp_track(); below 0 only after the real compiler's
             * own underflow has been reported. */
            /* Each of the real compiler's double pops (fp_store()) leaves
             * the model one lower - silently when nothing goes below the
             * bottom: fltprobe/p28_fltstk's "half(d = 3.0);" at line 48,
             * the model at -1 and no message. */
            if (g.fdepth != g.fstart - g.stmt_argpops ||
                (g.fdepth < 0 && !g.fp_argpop))
                gen_fatal("internal: the floating-point stack model is off "
                          "by %d at the end of a statement",
                          g.fdepth - (g.fstart - g.stmt_argpops));
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
            } else if (extra < MCC_CHKSTK_MIN) {
                /* A plain "sub sp,N" - confirmed against 01_intarith.
                 * s.golden's "L1:sub\tsp,*6.\njmp\tL2" and, as the
                 * largest size, fltprobe/p29_frame2.s.golden's
                 * "L10:sub\tsp,*98." (see MCC_CHKSTK_MIN). N is an
                 * ordinary immediate (render_operand()'s rule). */
                ins2(&g, "sub", o_reg("sp"), o_imm(extra));
            } else {
                /* "mov ax,N / call chkstk" - confirmed against every
                 * 09_abiprobe/03..07 golden (N = 128/176/224/256/300),
                 * e.g. 07_frame300.s.golden's "L1:mov\tax,#300.", and
                 * fltprobe/p27_frame.s.golden's N = 100..126 ("L7:mov\t
                 * ax,*100."): N is an ordinary immediate - '*' up to
                 * 127, '#' from 128 (p27's f127, its 127-byte array
                 * rounded up: "mov\tax,#128."). (Before the 09_abiprobe
                 * goldens existed this path hard-coded '*' and was
                 * reachable only above 256 bytes.) */
                ins2(&g, "mov", o_reg("ax"), o_imm(extra));
                ins1(&g, "call", o_sym("chkstk"));
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
    if (g.nfloat)
        ins1(&g, ".globl", o_sym("fltused")); /* see "Floating point" */
    ins0(&g, ".data");
    gen_strings(&g, temp2);
    free_names(&g);
    return g.nerrors ? 1 : 0;          /* see c1_error() */
}
