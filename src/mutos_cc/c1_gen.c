/*
 * c1_gen.c - mutos_c1 code generator.
 *
 * Current opcode coverage (matches mutos_c0's current grammar
 * coverage - see c0_parser.c and src/mutos_cc/README.md): SYMDEF,
 * PROG, EVEN, RLABEL, SAVE, SETREG, BRANCH, LABEL, ANAME, NAME, CON,
 * PLUS, MINUS, TIMES, DIVIDE, MOD, AND, OR, EXOR, COMPL, ASSIGN,
 * RFORCE, EXPR, RETRN, SETSTK, EOFC.
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

typedef enum { VK_IMM, VK_MEM, VK_REG } ValKind;

typedef struct {
    ValKind kind;
    long    imm;    /* VK_IMM */
    int     offset;  /* VK_MEM: bp-relative offset */
    const char *reg;  /* VK_REG: a static string ("ax", "di", "dx") */
} Val;

typedef struct {
    int  regvar;              /* consumed from SETREG but not yet
                                * acted on - no register-variable
                                * allocation is implemented yet. */
    Val  valstack[VALSTACK_MAX];
    int  valsp;
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

static Val val_imm(long v) { Val r; r.kind = VK_IMM; r.imm = v; r.offset = 0; r.reg = NULL; return r; }
static Val val_mem(int off) { Val r; r.kind = VK_MEM; r.imm = 0; r.offset = off; r.reg = NULL; return r; }
static Val val_reg(const char *reg) { Val r; r.kind = VK_REG; r.imm = 0; r.offset = 0; r.reg = reg; return r; }

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
    }
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

int c1_generate(FILE *temp1, FILE *temp2, FILE *out)
{
    (void)temp2; /* string-literal (SNAME/temp2) support is not
                  * needed by mutos_c0's current grammar coverage -
                  * see src/mutos_cc/README.md. */

    GenState g = {0};

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
            if (type != TY_INT)
                gen_fatal("NAME of type %d not yet supported (only "
                          "TY_INT is covered so far)", type);
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
            Val r = pop_val(&g);
            Val l = pop_val(&g);
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
            Val r = pop_val(&g);
            Val l = pop_val(&g);
            load_into_di(out, l);
            char rbuf[32];
            render_operand(rbuf, sizeof rbuf, r);
            const char *mnem = op == OP_AND ? "and" : op == OP_OR ? "or" : "xor";
            fprintf(out, "%s\tdi,%s\n", mnem, rbuf);
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_COMPL: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("COMPL of type %d not yet supported", type);
            Val v = pop_val(&g);
            load_into_di(out, v);
            fprintf(out, "not\tdi\n");
            push_val(&g, val_reg("di"));
            break;
        }

        case OP_TIMES: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("TIMES of type %d not yet supported", type);
            Val r = pop_val(&g);
            Val l = pop_val(&g);
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
            Val r = pop_val(&g);
            Val l = pop_val(&g);
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

        case OP_ASSIGN: {
            int type = c1_read_num(temp1, "temp1");
            if (type != TY_INT)
                gen_fatal("ASSIGN of type %d not yet supported", type);
            Val rhs = pop_val(&g);
            Val lhs = pop_val(&g);
            if (lhs.kind != VK_MEM)
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
            Val v = pop_val(&g);
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
