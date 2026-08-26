/*
 * encode.c - see encode.h.
 */

#include <stdlib.h>
#include <string.h>

#include "encode.h"
#include "../h/mutos_aout.h"

/* ------------------------------------------------------------------ */
/* CodeBuf - trivial growable byte buffer                               */
/* ------------------------------------------------------------------ */

void codebuf_init(CodeBuf *cb)
{
    cb->data = NULL;
    cb->len = 0;
    cb->cap = 0;
}

void codebuf_free(CodeBuf *cb)
{
    free(cb->data);
    cb->data = NULL;
    cb->len = cb->cap = 0;
}

static void codebuf_reserve(CodeBuf *cb, size_t more)
{
    if (cb->len + more <= cb->cap) return;
    size_t newcap = cb->cap ? cb->cap * 2 : 16;
    while (newcap < cb->len + more) newcap *= 2;
    cb->data = realloc(cb->data, newcap);
    cb->cap = newcap;
}

void codebuf_put(CodeBuf *cb, unsigned char b)
{
    codebuf_reserve(cb, 1);
    cb->data[cb->len++] = b;
}

void codebuf_put16le(CodeBuf *cb, unsigned int w)
{
    codebuf_put(cb, (unsigned char)(w & 0xFF));
    codebuf_put(cb, (unsigned char)((w >> 8) & 0xFF));
}

/* ------------------------------------------------------------------ */
/* RelocList - trivial growable relocation-entry list                   */
/* ------------------------------------------------------------------ */

void reloclist_init(RelocList *rl) { rl->entries = NULL; rl->n = 0; rl->cap = 0; }
void reloclist_free(RelocList *rl) { free(rl->entries); rl->entries = NULL; rl->n = rl->cap = 0; }

void reloclist_add(RelocList *rl, long text_offset, int symidx, unsigned char low4)
{
    if (rl->n >= rl->cap) {
        rl->cap = rl->cap ? rl->cap * 2 : 8;
        rl->entries = realloc(rl->entries, rl->cap * sizeof(*rl->entries));
    }
    rl->entries[rl->n].text_offset = text_offset;
    rl->entries[rl->n].symidx = symidx;
    rl->entries[rl->n].low4 = low4;
    rl->n++;
}

/* ------------------------------------------------------------------ */
/* Expression resolution                                                */
/* ------------------------------------------------------------------ */

/* Resolves an expression to a value. When `resolve` is false (Pass 1,
 * sizing only), the numeric result is always 0 (a placeholder) - every
 * encoding form used here has a byte-length that depends only on the
 * operand's syntactic shape, never the resolved value, so a
 * placeholder is safe and keeps Pass 1 a single, cheap pass.
 *
 * IMPORTANT: registration of every referenced symbol (via
 * find_or_create, so it gets a valid out_index once symtab_finalize()
 * runs between the passes) must still happen unconditionally, for
 * EVERY symbol anywhere in the tree - not just when the expression IS
 * a bare symbol. The recursive calls below are never short-circuited
 * before recursing into children, even when resolve=false, specifically
 * so that a symbol nested inside an arithmetic expression (e.g.
 * "_clknumb+0", where the top node is EX_ADD and the symbol is a
 * child) still gets registered during Pass 1. A symbol reachable ONLY
 * through such a nested reference (mch.s: "movb al,_clknumb+0") was
 * found completely unregistered until Pass 2 - by which point
 * symtab_finalize() had already run, leaving it with no valid
 * out_index and corrupting the object-file writer's symbol table
 * (heap-buffer-overflow, confirmed via AddressSanitizer). */
static long resolve_expr(const ExprNode *e, long cur_addr, SymTab *st, bool resolve)
{
    if (!e) return 0;

    switch (e->op) {
        case EX_NUM: return resolve ? e->num : 0;
        case EX_LOCCTR: return resolve ? cur_addr : 0;
        case EX_LOCALREF:
            /* Numeric local-label resolution (nearest '<n>f'/'<n>b')
             * needs a scan of nearby label definitions, not just a
             * flat symbol-table lookup - not used anywhere in
             * malloc.s, left unimplemented for now (documented gap,
             * see encode.h). */
            return 0;
        case EX_SYM: {
            /* Always register, regardless of resolve - see the
             * function comment above. */
            symtab_find_or_create(st, e->sym, e->sym_len);
            if (!resolve) return 0;
            Symbol *s = symtab_find(st, e->sym, e->sym_len);
            /* Only TEXT/DATA/ABS symbols carry a genuine "value = this
             * module's address/constant" meaning. A BSS (.comm) symbol's
             * "value" is the requested SIZE for the linker's common-
             * block merge (see symtab.h) - NOT a usable address, even
             * though it is marked `defined` to avoid a spurious
             * redefinition error. Using it as an address by mistake
             * silently produced plausible-looking-but-wrong offsets
             * here during Step 4 validation (e.g. a 256-byte .comm's
             * SIZE, 256, got used as if it were _bss_puf's ADDRESS) -
             * treat BSS the same as a true external: zero placeholder,
             * pending the linker's relocation. */
            if (s && s->defined && s->type != SYM_BSS)
                return s->value;
            return 0;
        }
        case EX_NEG:
            return -resolve_expr(e->lhs, cur_addr, st, resolve);
        case EX_ADD:
            return resolve_expr(e->lhs, cur_addr, st, resolve) + resolve_expr(e->rhs, cur_addr, st, resolve);
        case EX_SUB:
            return resolve_expr(e->lhs, cur_addr, st, resolve) - resolve_expr(e->rhs, cur_addr, st, resolve);
        case EX_MUL:
            return resolve_expr(e->lhs, cur_addr, st, resolve) * resolve_expr(e->rhs, cur_addr, st, resolve);
        case EX_DIV: {
            long a = resolve_expr(e->lhs, cur_addr, st, resolve);
            long b = resolve_expr(e->rhs, cur_addr, st, resolve);
            return (resolve && b) ? a / b : 0;
        }
    }
    return 0;
}

long resolve_expr_pub(const ExprNode *e, long cur_addr, SymTab *st)
{
    return resolve_expr(e, cur_addr, st, true);
}

long register_expr_symbols(const ExprNode *e, SymTab *st)
{
    return resolve_expr(e, 0, st, false);
}

/* ------------------------------------------------------------------ */
/* Non-PC-relative word-relocation classification                       */
/* ------------------------------------------------------------------ */

/* Walks `e`, summing a signed coefficient PER RELOCATION BASE - the
 * TEXT segment base, the DATA segment base, and (separately) each
 * distinct external/.comm symbol's own address, which is its own
 * independent unknown base, unlike TEXT/DATA labels which all share
 * one base per segment. This distinction matters: CONFIRMED real via
 * mch.o "mov ivsstep+1,#t_trap-ivsstep-3" - t_trap and ivsstep are two
 * DIFFERENT local TEXT labels, so this expression's value is a
 * location-INDEPENDENT constant (their difference doesn't change no
 * matter where the linker places .text) and needs NO relocation at
 * all - a naive "does any symbol appear with nonzero coefficient"
 * check (tracking only one symbol slot) wrongly flags this as needing
 * R_TEXT, since it silently drops the second symbol's contribution
 * instead of letting it cancel the first's. Only when the SAME base's
 * net coefficient is nonzero does that base's relocation apply. */
typedef struct { Symbol *sym; long coeff; } ExtSymCoeff;
typedef struct {
    long text_coeff;
    long data_coeff;
    long bss_coeff;  /* SYM_LOCALBSS (real ".bss" segment labels, e.g.
                       * v30opt.s's "_identDe") - a distinct base from
                       * TEXT/DATA, same reasoning as those two. */
    ExtSymCoeff ext[4]; /* small fixed set - real code references at
                          * most one external symbol per expression;
                          * sized generously for safety. */
    int  next_ext;
} RelocAccum;

static void reloc_accum_walk(const ExprNode *e, long sign, RelocAccum *acc, SymTab *st, bool in_text)
{
    if (!e) return;
    switch (e->op) {
        case EX_SYM: {
            Symbol *s = symtab_find(st, e->sym, e->sym_len);
            if (!s) return;
            switch (s->type) {
                case SYM_TEXT: acc->text_coeff += sign; return;
                case SYM_DATA: acc->data_coeff += sign; return;
                case SYM_LOCALBSS: acc->bss_coeff += sign; return;
                case SYM_ABS: return; /* true constant, no base at all */
                case SYM_BSS:
                case SYM_UNDEF:
                    for (int i = 0; i < acc->next_ext; i++)
                        if (acc->ext[i].sym == s) { acc->ext[i].coeff += sign; return; }
                    if (acc->next_ext < (int)(sizeof(acc->ext)/sizeof(acc->ext[0]))) {
                        acc->ext[acc->next_ext].sym = s;
                        acc->ext[acc->next_ext].coeff = sign;
                        acc->next_ext++;
                    }
                    return;
            }
            return;
        }
        case EX_LOCCTR:
            /* '.' is the current segment's OWN address - contributes to
             * that segment's base exactly like a label defined at this
             * point would. CONFIRMED real (mch.o "_szicode: . - _icode",
             * a STMT_DATA_VALUE in .data): without this, '.' silently
             * contributed nothing, leaving only _icode's DATA
             * coefficient nonzero and wrongly flagging a same-segment,
             * location-independent difference as needing R_DATA. */
            if (in_text) acc->text_coeff += sign; else acc->data_coeff += sign;
            return;
        case EX_NEG:
            reloc_accum_walk(e->lhs, -sign, acc, st, in_text);
            return;
        case EX_ADD:
            reloc_accum_walk(e->lhs, sign, acc, st, in_text);
            reloc_accum_walk(e->rhs, sign, acc, st, in_text);
            return;
        case EX_SUB:
            reloc_accum_walk(e->lhs, sign, acc, st, in_text);
            reloc_accum_walk(e->rhs, -sign, acc, st, in_text);
            return;
        default:
            return; /* EX_NUM/EX_LOCALREF/EX_MUL/EX_DIV: no symbol
                      * contribution expected here. */
    }
}

/* Determines whether the word-sized field about to be emitted for `e`
 * needs a non-PC-relative relocation entry (i.e. this is an ABSOLUTE
 * embedded address, not a CALL/JMP displacement - those are handled
 * separately with the R_EXT|PCREL=0x09 low4 by encode_jmp/encode_call/
 * encode_pseudo_branch). CONFIRMED real (sys1nonopt.o): any embedded
 * absolute reference to a symbol - whether a LOCAL text/data label
 * ("mov dx,#_execnt" -> R_DATA) or an external/.comm symbol
 * ("111.+_u" -> R_EXT with symidx) - needs its own relocation entry,
 * since the linker only fixes up the final segment/symbol address at
 * link time; only a pure numeric literal, an already-resolved local
 * ABS symbol, or a same-segment difference that cancels to a location-
 * independent constant (see reloc_accum_walk above), needs none.
 * `in_text` tells '.' (EX_LOCCTR) which segment it belongs to - every
 * call site inside this file is always mid-instruction-encoding (i.e.
 * always .text), so they all pass true; assemble.c's .word/.byte/
 * STMT_DATA_VALUE handling (via classify_word_reloc_pub) passes the
 * statement's actual current segment, since those directives can
 * appear in either .text or .data. On success, sets *low4_out
 * (relocation type, with the EXT bit and symidx only meaningful/set
 * for a genuinely external or BSS/.comm symbol) and *symidx_out (0 for
 * non-EXT types, where the linker ignores this field). Checked in
 * TEXT, then DATA, then external-symbol order - real code is never
 * expected to mix bases with all nonzero net coefficients in one
 * expression, so first-match is fine. */
static bool classify_word_reloc(const ExprNode *e, SymTab *st, bool in_text,
                                 unsigned char *low4_out, int *symidx_out)
{
    if (!e) return false;
    RelocAccum acc;
    memset(&acc, 0, sizeof(acc));
    reloc_accum_walk(e, 1, &acc, st, in_text);

    if (acc.text_coeff != 0) { *low4_out = R_TEXT; *symidx_out = 0; return true; }
    if (acc.data_coeff != 0) { *low4_out = R_DATA; *symidx_out = 0; return true; }
    if (acc.bss_coeff != 0)  { *low4_out = R_BSS;  *symidx_out = 0; return true; }
    for (int i = 0; i < acc.next_ext; i++) {
        if (acc.ext[i].coeff != 0) {
            *low4_out = R_EXT; *symidx_out = acc.ext[i].sym->out_index;
            return true;
        }
    }
    return false;
}

bool classify_word_reloc_pub(const ExprNode *e, SymTab *st, bool in_text,
                              unsigned char *low4_out, int *symidx_out)
{
    return classify_word_reloc(e, st, in_text, low4_out, symidx_out);
}

/* ------------------------------------------------------------------ */
/* Register numbering (identical field values for word/byte forms -    */
/* the opcode's own w-bit selects the interpretation, see encode.h)     */
/* ------------------------------------------------------------------ */

static int reg_number(const Token *t)
{
    static const struct { const char *name; int num; } T[] = {
        {"ax",0},{"cx",1},{"dx",2},{"bx",3},{"sp",4},{"bp",5},{"si",6},{"di",7},
        {"al",0},{"cl",1},{"dl",2},{"bl",3},{"ah",4},{"ch",5},{"dh",6},{"bh",7},
    };
    for (size_t i = 0; i < sizeof(T)/sizeof(T[0]); i++)
        if (strlen(T[i].name) == t->len && strncmp(T[i].name, t->text, t->len) == 0)
            return T[i].num;
    return -1;
}

/* Indirect-addressing rm field for the single-register forms actually
 * observed in the corpus: (si) (di) (bp) (bx). */
static int indirect_rm(const Token *reg)
{
    if (reg->len == 2 && strncmp(reg->text, "si", 2) == 0) return 4;
    if (reg->len == 2 && strncmp(reg->text, "di", 2) == 0) return 5;
    if (reg->len == 2 && strncmp(reg->text, "bp", 2) == 0) return 6;
    if (reg->len == 2 && strncmp(reg->text, "bx", 2) == 0) return 7;
    return -1;
}

/* Base+index compound indirect-addressing rm field, e.g. "(bx)(di)" -
 * confirmed real via kernel_opt/subr.s ("mov ax,*20.+2(bx)(di)"). Only
 * these four combinations exist in the 8086 ModRM table (order-
 * independent here, matching either "(base)(index)" or
 * "(index)(base)" source order, though every real sample writes base
 * first). Returns -1 for any other pairing (e.g. "(si)(di)"), which
 * the real ISA cannot encode. */
static int base_index_rm(const Token *r1, const Token *r2)
{
    bool r1_bx = (r1->len == 2 && strncmp(r1->text, "bx", 2) == 0);
    bool r1_bp = (r1->len == 2 && strncmp(r1->text, "bp", 2) == 0);
    bool r2_bx = (r2->len == 2 && strncmp(r2->text, "bx", 2) == 0);
    bool r2_bp = (r2->len == 2 && strncmp(r2->text, "bp", 2) == 0);
    bool r1_si = (r1->len == 2 && strncmp(r1->text, "si", 2) == 0);
    bool r1_di = (r1->len == 2 && strncmp(r1->text, "di", 2) == 0);
    bool r2_si = (r2->len == 2 && strncmp(r2->text, "si", 2) == 0);
    bool r2_di = (r2->len == 2 && strncmp(r2->text, "di", 2) == 0);
    bool has_bx = r1_bx || r2_bx;
    bool has_bp = r1_bp || r2_bp;
    bool has_si = r1_si || r2_si;
    bool has_di = r1_di || r2_di;
    if (has_bx && has_si) return 0; /* [bx+si] */
    if (has_bx && has_di) return 1; /* [bx+di] */
    if (has_bp && has_si) return 2; /* [bp+si] */
    if (has_bp && has_di) return 3; /* [bp+di] */
    return -1;
}

/* True if `e` contains an EX_SYM or EX_LOCALREF leaf anywhere in the
 * tree - i.e. the expression's value depends on a symbol/label address
 * rather than being a pure numeric literal. EX_LOCCTR ('.') does NOT
 * count: the current location counter is always exactly known at the
 * point of use, unlike a symbol/label whose address may not be. */
static bool expr_has_symbol(const ExprNode *e)
{
    if (!e) return false;
    switch (e->op) {
        case EX_SYM:
        case EX_LOCALREF:
            return true;
        case EX_NEG:
            return expr_has_symbol(e->lhs);
        case EX_ADD: case EX_SUB: case EX_MUL: case EX_DIV:
            return expr_has_symbol(e->lhs) || expr_has_symbol(e->rhs);
        default:
            return false;
    }
}

/* Emits a ModRM byte (+ any displacement bytes) for an INDIRECT
 * operand. The 8086 special case applies: rm=110 (bp, no index) at
 * mod=00 actually means "disp16, no base register", so a bp-based
 * indirect with NO explicit displacement still has to be forced to
 * mod=01 with an explicit disp8=0 to mean "[bp+0]" instead.
 *
 * IMPORTANT: mod01-vs-mod10 (byte vs word displacement) is chosen by
 * the ACTUAL RESOLVED VALUE fitting in a signed byte or not - NOT by
 * the operand's '*'/'#' size marker. This is the opposite of how the
 * marker works for IMMEDIATE values (there it's honored literally,
 * e.g. "sub di,#2" real-confirmed uses the full 4-byte 0x81 word-
 * immediate form despite 2 fitting easily in a byte). Confirmed for
 * displacements via real mch.o: "mov ax,#2(bx)" - '#' (word) marker,
 * yet disassembles to 8B 47 02 (mod=01, 1-byte disp), not the 4-byte
 * mod=10 form the marker alone would suggest.
 *
 * EXCEPTION, confirmed via sys1nonopt.o "mov 174.+_u(bx),*0." (no
 * marker at all, external symbol _u) -> mod=10 (6-byte C7 87 form),
 * NOT mod=01 even though 174 numerically fits a signed byte as a raw
 * bit pattern: a displacement containing ANY symbol reference
 * (expr_has_symbol(), not just "not currently constant-foldable")
 * unconditionally forces mod=10, because the assembler cannot assume
 * a symbol's eventual linked value will keep fitting in a signed byte.
 * This check must come before the constant-folding/value-fitting logic
 * below, not just fall out of "expr_try_eval_const() failed" - a
 * symbol that happens to be constant-foldable right now (e.g. it's a
 * previously-defined SYM_ABS in this same module) still must not use
 * this path's value-based sizing. */
static void emit_modrm_indirect(CodeBuf *out, int reg_field, const ParsedOperand *mem,
                                 long cur_addr, SymTab *st, bool resolve, RelocList *relocs)
{
    int rm = mem->has_index ? base_index_rm(&mem->reg, &mem->reg2) : indirect_rm(&mem->reg);
    int mod;
    long disp = 0;
    if (mem->expr && expr_has_symbol(mem->expr)) {
        mod = 2;
        /* Always call resolve_expr(), even in Pass 1 (resolve=false) -
         * this matches the established pattern used everywhere else in
         * this codebase (see resolve_expr's EX_SYM case: "Always
         * register, regardless of resolve"). A symbol referenced ONLY
         * from within an indirect-addressing displacement (never as a
         * direct/immediate operand elsewhere in the file) would
         * otherwise never get registered during Pass 1 at all, then
         * get created fresh during Pass 2 - AFTER symtab_finalize()
         * already assigned every symbol's out_index - leaving it at
         * the default out_index=-1 and corrupting the object-file
         * symbol-table write (heap-buffer-overflow, confirmed real via
         * tty.s: "call @8.+_cdevsw(bx)" is the ONLY place "_cdevsw"
         * appears anywhere in the file). The returned placeholder
         * value is harmless to discard in Pass 1 - mod is already
         * fixed at 2 by expr_has_symbol() above regardless of the
         * actual disp value, so Pass 1 sizing is unaffected either
         * way. */
        disp = resolve_expr(mem->expr, cur_addr, st, resolve);
    } else {
        bool have_const = false;
        if (mem->expr)
            have_const = expr_try_eval_const(mem->expr, &disp);
        if (have_const) {
            /* Pure numeric literal (no symbol anywhere in the tree) -
             * size by actual value, in both Pass 1 and Pass 2. */
            mod = (disp >= -128 && disp <= 127) ? 1 : 2;
        } else if (!mem->expr) {
            mod = 0; /* no displacement written at all, e.g. "(bp)"/"(si)" */
        } else {
            /* Should not normally be reached (expr_has_symbol() above
             * already catches every non-foldable case), but fall back
             * defensively to the marker rather than crash/misencode. */
            mod = (mem->size == SZ_WORD) ? 2 : 1;
            if (resolve)
                disp = resolve_expr(mem->expr, cur_addr, st, resolve);
        }
    }
    if (rm == 6 && mod == 0 && !mem->has_index)
        mod = 1; /* bp with no disp still needs mod=01 disp8=0 - only
                   * applies to the SINGLE-register (bp) form (rm==6);
                   * a base+index [bp+si]/[bp+di] form uses rm==2/3
                   * (never 6), so this never fires for those anyway -
                   * has_index checked explicitly for clarity. */

    codebuf_put(out, (unsigned char)((mod << 6) | ((reg_field & 7) << 3) | (rm & 7)));
    if (mod == 1) {
        codebuf_put(out, (unsigned char)(disp & 0xFF));
    } else if (mod == 2) {
        /* Non-PC-relative relocation for a symbolic 16-bit displacement
         * (see classify_word_reloc) - CONFIRMED real via sys1nonopt.o
         * "mov 174.+_u(bx),*0." needing an R_EXT entry for _u. Recorded
         * at out->len, which at this exact point equals the absolute
         * text offset the disp16 bytes are about to be written at
         * (out is the whole file's TEXT buffer, growing byte-for-byte
         * in lockstep with the LC - simpler and less error-prone than
         * manually counting prefix bytes per caller). */
        if (resolve && relocs && mem->expr) {
            unsigned char low4; int symidx;
            if (classify_word_reloc(mem->expr, st, true, &low4, &symidx))
                reloclist_add(relocs, (long)out->len, symidx, low4);
        }
        codebuf_put16le(out, (unsigned int)(disp & 0xFFFF));
    }
}

/* Emits ModRM (mod=00, rm=110) + disp16 for a DIRECT (bare absolute
 * address, no base register) memory operand - e.g. "_global_" or
 * "monssvec+2". This is the general fallback form; AL/AX get shorter
 * dedicated opcodes instead (A0-A3) where the *mnemonic's* register
 * operand is specifically the accumulator - see encode_mov(). */
static void emit_modrm_direct(CodeBuf *out, int reg_field, const ParsedOperand *mem,
                               long cur_addr, SymTab *st, bool resolve, RelocList *relocs)
{
    long addr = mem->expr ? resolve_expr(mem->expr, cur_addr, st, resolve) : 0;
    codebuf_put(out, (unsigned char)(((reg_field & 7) << 3) | 0x06)); /* mod=00 rm=110 */
    /* Same non-PC-relative relocation as the mod=2 case above - a
     * DIRECT address is ALWAYS a full disp16, whether the referenced
     * symbol is external (R_EXT+symidx) or a local text/data label
     * (R_TEXT/R_DATA, symidx unused) - CONFIRMED real via sys1nonopt.o
     * "cmp _execnt,*2." needing an R_DATA entry for the local _execnt
     * data label, and "111.+_u" needing R_EXT for external _u. */
    if (resolve && relocs && mem->expr) {
        unsigned char low4; int symidx;
        if (classify_word_reloc(mem->expr, st, true, &low4, &symidx))
            reloclist_add(relocs, (long)out->len, symidx, low4);
    }
    codebuf_put16le(out, (unsigned int)(addr & 0xFFFF));
}

/* ------------------------------------------------------------------ */
/* Shift/rotate group (rol/ror/rcl/rcr/shl,sal/shr/sar)                 */
/* ------------------------------------------------------------------ */

typedef struct { const char *name; int idx; } Group2Entry;
static const Group2Entry GROUP2[] = {
    {"rol",0}, {"ror",1}, {"rcl",2}, {"rcr",3}, {"shl",4}, {"sal",4}, {"shr",5}, {"sar",7},
};
/* Explicit "-b" (byte) spellings - Assembler_as.pdf Anlage A documents
 * these as separate mnemonics (rolb/rorb/rclb/rcrb/shlb/salb/shrb/
 * sarb), matching the same "no suffix=word, b-suffix=byte" convention
 * as mov/movb, xchg/xchgb etc. NOT observed directly in the real
 * corpus (every group2 sample so far is either a word register or
 * already correctly byte-inferred via a byte-class REGISTER operand
 * like "bl" - see encode_group2's bm computation) - but genuinely
 * NEEDED for a byte-sized MEMORY destination (e.g. "shrb (bx),*1"),
 * where there is no register to infer byte-ness from at all, unlike
 * the register-destination case. */
static const Group2Entry GROUP2B[] = {
    {"rolb",0}, {"rorb",1}, {"rclb",2}, {"rcrb",3}, {"shlb",4}, {"salb",4}, {"shrb",5}, {"sarb",7},
};

static int group2_lookup(const Token *mnem, bool *byte_mode_out)
{
    for (size_t i = 0; i < sizeof(GROUP2)/sizeof(GROUP2[0]); i++)
        if (strlen(GROUP2[i].name) == mnem->len && strncmp(GROUP2[i].name, mnem->text, mnem->len) == 0) {
            *byte_mode_out = false;
            return GROUP2[i].idx;
        }
    for (size_t i = 0; i < sizeof(GROUP2B)/sizeof(GROUP2B[0]); i++)
        if (strlen(GROUP2B[i].name) == mnem->len && strncmp(GROUP2B[i].name, mnem->text, mnem->len) == 0) {
            *byte_mode_out = true;
            return GROUP2B[i].idx;
        }
    return -1;
}

/* dst is REGISTER or INDIRECT; src is either the literal count
 * (REGISTER "cl", or an IMMEDIATE/DIRECT constant expression) - never
 * itself marked in the corpus with a distinguishing size, since a
 * shift count is inherently a byte value regardless of the operand's
 * own '*'/'#' marker.
 *
 * CONFIRMED against real mch.o disassembly: a count that resolves to
 * the constant 1 ALWAYS uses the short D0/D1 form (2 bytes total),
 * never the 80186 C0/C1-with-immediate-1 form (3 bytes) - every single
 * "shr cx,#1" / "shl bx,#1" / etc. sample in the corpus (10+ instances)
 * disassembles to D0/D1. No real sample with a count other than 1 or
 * "cl" exists in the corpus, so the C0/C1-for-other-immediates path
 * below is the documented AP-186 encoding but UNCONFIRMED by a real
 * sample - only the count==1 and count==cl cases are proven. */
static bool encode_group2(CodeBuf *out, int opidx, const ParsedOperand *dst,
                           const ParsedOperand *src, long cur_addr, SymTab *st, bool resolve,
                           RelocList *relocs, bool byte_mode)
{
    bool is_cl = (src->mode == ADDR_REGISTER && reg_number(&src->reg) == 1);
    /* NOTE: compares by REGISTER NUMBER, not the literal text "cl" -
     * confirmed necessary by real code: mch.s line 173 writes "shr
     * bx, cx" (the WORD register name) where every other shift-by-CL
     * sample in the corpus correctly writes "cl" - yet the real mch.o
     * disassembles this exact instruction to the 2-byte D3 form (D3 EB),
     * proving the count register is recognized by its NUMBER (cx and cl
     * are both register #1) regardless of which name spelling is used,
     * the same "word name still means the byte register" pattern
     * confirmed earlier for movb. */
    bool is_one = false;
    long count = 0;
    if (!is_cl && src->expr) {
        count = resolve_expr(src->expr, cur_addr, st, resolve);
        is_one = !resolve || count == 1; /* during sizing (resolve=false) we
                                           * cannot know the real value yet;
                                           * assume the common count==1 case
                                           * so Pass 1's size matches Pass 2's
                                           * - every real corpus sample is
                                           * count==1 or "cl" anyway. */
    }

    void (*emit_modrm)(CodeBuf*, int, const ParsedOperand*, long, SymTab*, bool, RelocList*) =
        (dst->mode == ADDR_INDIRECT) ? emit_modrm_indirect : NULL;

    if (dst->mode != ADDR_REGISTER && dst->mode != ADDR_INDIRECT) return false;
    int rn = (dst->mode == ADDR_REGISTER) ? reg_number(&dst->reg) : 0;
    if (dst->mode == ADDR_REGISTER && rn < 0) return false;

    /* Byte-sized destination uses the D0/D2/C0 byte-opcode row instead
     * of D1/D3/C1 - either because the mnemonic itself was explicitly
     * byte-suffixed (byte_mode, e.g. "shrb (bx),*1" - the ONLY signal
     * available for a memory destination, which has no register class
     * of its own to infer from), or because the destination is a
     * byte-class REGISTER (e.g. "shr bl,*1.") - same "reg_class
     * determines byte-ness" convention used throughout this encoder
     * (mov/movb, mul, neg, inc/dec). The register-inferred case is
     * confirmed defensively (no real corpus sample exercises a byte
     * register here); the explicit byte_mode case is a plain,
     * unconditional hardware fact (Assembler_as.pdf Anlage A
     * documents rolb/rorb/rclb/rcrb/shlb/salb/shrb/sarb as distinct
     * mnemonics) needed for memory destinations specifically. */
    bool bm = byte_mode || (dst->mode == ADDR_REGISTER && dst->reg_class == REG_BYTE);

    if (is_cl) {
        codebuf_put(out, bm ? 0xD2 : 0xD3);
    } else if (is_one) {
        codebuf_put(out, bm ? 0xD0 : 0xD1);
    } else {
        codebuf_put(out, bm ? 0xC0 : 0xC1);
    }
    if (dst->mode == ADDR_REGISTER)
        codebuf_put(out, (unsigned char)(0xC0 | (opidx << 3) | rn));
    else
        emit_modrm(out, opidx, dst, cur_addr, st, resolve, relocs);

    if (!is_cl && !is_one)
        codebuf_put(out, (unsigned char)(count & 0xFF));
    return true;
}

/* ------------------------------------------------------------------ */
/* Group1 arithmetic (add/or/adc/sbb/and/sub/xor/cmp)                   */
/* ------------------------------------------------------------------ */

typedef struct { const char *name; int idx; } Group1Entry;
static const Group1Entry GROUP1[] = {
    {"add",0}, {"or",1}, {"adc",2}, {"sbb",3}, {"and",4}, {"sub",5}, {"xor",6}, {"cmp",7},
};
/* Byte-mode ('b'-suffixed) group1 mnemonics - confirmed real usage:
 * "subb ah,ah", "orb dh,#0(bp)", "cmpb state,#/FE". */
static const Group1Entry GROUP1B[] = {
    {"addb",0}, {"orb",1}, {"adcb",2}, {"sbbb",3}, {"andb",4}, {"subb",5}, {"xorb",6}, {"cmpb",7},
};

static int group1_lookup(const Token *mnem, bool *byte_mode_out)
{
    for (size_t i = 0; i < sizeof(GROUP1)/sizeof(GROUP1[0]); i++)
        if (strlen(GROUP1[i].name) == mnem->len && strncmp(GROUP1[i].name, mnem->text, mnem->len) == 0) {
            *byte_mode_out = false;
            return GROUP1[i].idx;
        }
    for (size_t i = 0; i < sizeof(GROUP1B)/sizeof(GROUP1B[0]); i++)
        if (strlen(GROUP1B[i].name) == mnem->len && strncmp(GROUP1B[i].name, mnem->text, mnem->len) == 0) {
            *byte_mode_out = true;
            return GROUP1B[i].idx;
        }
    return -1;
}

static bool encode_group1(CodeBuf *out, int opidx, const ParsedOperand *dst,
                           const ParsedOperand *src, long cur_addr, SymTab *st, bool resolve,
                           bool byte_mode, RelocList *relocs)
{
    /* dst=AL/AX (accumulator), src=IMMEDIATE -> the classic short
     * accumulator-immediate forms (opidx*8+4 for AL, opidx*8+5 for AX),
     * e.g. 0x0C ib for "or al,imm8", 0x04 for add, 0x3C for cmp, etc. -
     * CONFIRMED real: mch.s "or al" with a hex-marked byte immediate
     * disassembles to the 2-byte short form, not the general 3-byte
     * 0x83 ModRM form. Takes priority over the general
     * REGISTER,IMMEDIATE case below, which still applies to every
     * other register. */
    if (dst->mode == ADDR_REGISTER && src->mode == ADDR_IMMEDIATE && reg_number(&dst->reg) == 0) {
        long imm = resolve_expr(src->expr, cur_addr, st, resolve);
        bool word = !byte_mode && dst->reg_class == REG_WORD;
        codebuf_put(out, (unsigned char)((opidx << 3) | (word ? 0x05 : 0x04)));
        if (word) {
            /* Immediate word could hold a symbol's address (e.g. "cmp
             * ax,#L12") - CONFIRMED real via mch.o's "sub ax,#..."/
             * "cmp ax,#..." accumulator short forms needing R_TEXT
             * entries here, same as every other word-immediate site. */
            if (resolve && relocs && src->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(src->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(imm & 0xFFFF));
        }
        else codebuf_put(out, (unsigned char)(imm & 0xFF));
        return true;
    }

    /* AND/OR/XOR (opidx 1/4/6) have NO sign-extend 's' bit in their real
     * hardware encoding: it is the FIXED "1000 000:w", unlike
     * ADD/ADC/SUB/SBB/CMP's variable "1000 00:s:w" (which is what makes
     * the compact 0x83 sign-extended-byte form possible for those five
     * in the first place). CONFIRMED via Assembler_as.pdf Anlage C
     * directly, and via real hardware output (sys1nonopt.o: "and
     * di,*-2." -> 81 e7 fe ff, the full word form, NOT 83 e7 fe) - a
     * wrong intermediate hypothesis (unary-minus-expression-shape) was
     * tested and disproven by malloc.o's "add dx,*-4." still using
     * 0x83 despite being the same EX_SUB expression shape, before the
     * real per-opcode hardware-table explanation was found. These
     * three therefore NEVER use 0x83, always the full 0x80(byte)/0x81
     * (word) form, regardless of the immediate's marker, value, or
     * expression shape. Do not reintroduce a marker/size-only gate for
     * opidx 1/4/6 - it was already found and removed once before. */
    bool no_sbit = (opidx == 1 || opidx == 4 || opidx == 6); /* or/and/xor */

    /* dst=REGISTER, src=IMMEDIATE -> 0x83 /opidx ib (sign-extended byte;
     * matches every real WORD sample seen so far, EXCEPT or/and/xor -
     * see no_sbit above). Byte-mode ('b'-suffixed mnemonics) forces an
     * 8-bit immediate with the 0x80 opcode instead (genuine byte op,
     * no sign-extend ambiguity applies there for any group1 op). A
     * '#' word-marked immediate on a plain (non-b) mnemonic falls back
     * to the standard 0x81 /opidx iw form, as does any or/and/xor on a
     * word destination regardless of marker. */
    if (dst->mode == ADDR_REGISTER && src->mode == ADDR_IMMEDIATE) {
        int rn = reg_number(&dst->reg);
        if (rn < 0) return false;
        long imm = resolve_expr(src->expr, cur_addr, st, resolve);
        bool word_dst = !byte_mode && dst->reg_class == REG_WORD;
        if (word_dst && (no_sbit || src->size == SZ_WORD)) {
            codebuf_put(out, 0x81);
            codebuf_put(out, (unsigned char)(0xC0 | (opidx << 3) | rn));
            /* Immediate word could itself hold a symbol's address (e.g.
             * "and dx,#_symbol") - same non-PC-relative relocation as
             * the memory-displacement cases (classify_word_reloc). */
            if (resolve && relocs && src->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(src->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(imm & 0xFFFF));
        } else {
            /* byte_mode ('b'-suffixed mnemonic) OR dst already being a
             * byte-class register (plain mnemonic, e.g. "or bl,-128")
             * needs opcode 0x80 (w=0, genuine byte operation), NOT
             * 0x83 (w=1,s=1, sign-extended-byte-into-WORD) - confirmed
             * real via "cmpb state,#/FE" and "or bl,#-128" both using
             * 0x80. Using 0x83 here would reinterpret the same
             * register number as its WORD sibling instead of the
             * intended byte register. */
            bool bm = byte_mode || dst->reg_class == REG_BYTE;
            codebuf_put(out, bm ? 0x80 : 0x83);
            codebuf_put(out, (unsigned char)(0xC0 | (opidx << 3) | rn));
            codebuf_put(out, (unsigned char)(imm & 0xFF));
        }
        return true;
    }

    if ((dst->mode == ADDR_INDIRECT || dst->mode == ADDR_DIRECT) && src->mode == ADDR_IMMEDIATE) {
        long imm = resolve_expr(src->expr, cur_addr, st, resolve);
        void (*emit)(CodeBuf*, int, const ParsedOperand*, long, SymTab*, bool, RelocList*) =
            (dst->mode == ADDR_INDIRECT) ? emit_modrm_indirect : emit_modrm_direct;
        if (!byte_mode && (no_sbit || src->size == SZ_WORD)) {
            codebuf_put(out, 0x81);
            emit(out, opidx, dst, cur_addr, st, resolve, relocs);
            if (resolve && relocs && src->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(src->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(imm & 0xFFFF));
        } else {
            /* Same 0x80-vs-0x83 distinction as above, confirmed real
             * via "cmpb state,#/FE" -> opcode 0x80. */
            codebuf_put(out, byte_mode ? 0x80 : 0x83);
            emit(out, opidx, dst, cur_addr, st, resolve, relocs);
            codebuf_put(out, (unsigned char)(imm & 0xFF));
        }
        return true;
    }

    /* dst=INDIRECT/DIRECT, src=REGISTER -> "+1" store form (r/m,reg),
     * or "+0" if src is a byte-class register written directly (e.g.
     * plain "or" with a byte register, not "orb") - same "infer byte
     * from the register itself" pattern confirmed repeatedly elsewhere
     * (mov/test/inc-dec). Confirmed real for the load ("+3") case via
     * "or dh,cs:[bp+0]" -> opcode 0x0A (not 0x0B); the store ("+1")
     * case is untested by a concrete sample but follows the identical
     * rule for consistency. */
    if ((dst->mode == ADDR_INDIRECT || dst->mode == ADDR_DIRECT) && src->mode == ADDR_REGISTER) {
        int rn = reg_number(&src->reg);
        if (rn < 0) return false;
        bool bm = byte_mode || src->reg_class == REG_BYTE;
        codebuf_put(out, (unsigned char)((opidx << 3) | (bm ? 0x00 : 0x01)));
        if (dst->mode == ADDR_INDIRECT) emit_modrm_indirect(out, rn, dst, cur_addr, st, resolve, relocs);
        else emit_modrm_direct(out, rn, dst, cur_addr, st, resolve, relocs);
        return true;
    }

    /* dst=REGISTER, src=INDIRECT/DIRECT/REGISTER -> "+3" load form
     * (reg,r/m), or "+2" if dst is a byte-class register - CONFIRMED
     * real: "or dh,cs:[bp+0]" disassembles to opcode 0x0A (+2), not
     * 0x0B (+3); "sub ah,ah" -> 0x2A not 0x2B; "xor al,al" -> 0x32 not
     * 0x33 - all plain (non-'b'-suffixed) mnemonics with a genuinely
     * byte-class register operand. */
    if (dst->mode == ADDR_REGISTER &&
        (src->mode == ADDR_INDIRECT || src->mode == ADDR_REGISTER || src->mode == ADDR_DIRECT)) {
        int rn = reg_number(&dst->reg);
        if (rn < 0) return false;
        bool bm = byte_mode || dst->reg_class == REG_BYTE;
        codebuf_put(out, (unsigned char)((opidx << 3) | (bm ? 0x02 : 0x03)));
        if (src->mode == ADDR_REGISTER) {
            int srn = reg_number(&src->reg);
            if (srn < 0) return false;
            codebuf_put(out, (unsigned char)(0xC0 | (rn << 3) | srn));
        } else if (src->mode == ADDR_INDIRECT) {
            emit_modrm_indirect(out, rn, src, cur_addr, st, resolve, relocs);
        } else {
            emit_modrm_direct(out, rn, src, cur_addr, st, resolve, relocs);
        }
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* TEST                                                                  */
/* ------------------------------------------------------------------ */

/* "test"/"testb" - confirmed real forms all use the group3 F6/F7 /0
 * immediate encoding (dst,#imm or dst,*imm): "test _dbreak,#/FFFF"
 * (DIRECT), "test ax,#3" (REGISTER), "testb #0(bp),#..." (INDIRECT).
 * A plain reg<->reg/mem TEST (84/85, no immediate) has not been seen
 * in the corpus but is included for completeness. */
static bool encode_test(CodeBuf *out, const ParsedOperand *dst, const ParsedOperand *src,
                         long cur_addr, SymTab *st, bool resolve, bool byte_mode, RelocList *relocs)
{
    /* dst=AL/AX, src=IMMEDIATE -> short accumulator forms A8/A9 (TEST
     * has its own dedicated accumulator shortcut, separate from
     * group1's - confirmed real: "test ax,#3" disassembles to A9 03 00
     * (3 bytes), not the general 4-byte F7 ModRM form. */
    if (dst->mode == ADDR_REGISTER && src->mode == ADDR_IMMEDIATE && reg_number(&dst->reg) == 0) {
        long imm = resolve_expr(src->expr, cur_addr, st, resolve);
        bool word = !byte_mode && dst->reg_class == REG_WORD;
        codebuf_put(out, word ? 0xA9 : 0xA8);
        if (word) {
            if (resolve && relocs && src->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(src->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(imm & 0xFFFF));
        }
        else codebuf_put(out, (unsigned char)(imm & 0xFF));
        return true;
    }

    if (src->mode == ADDR_IMMEDIATE || (src->mode == ADDR_DIRECT && dst->mode != ADDR_DIRECT)) {
        long imm = resolve_expr(src->expr, cur_addr, st, resolve);
        /* Byte form applies for 'testb', OR when dst is already a
         * byte-class register written directly (e.g. "test bh,*1" -
         * same "infer from the register itself" pattern confirmed
         * for mov/group1 earlier). */
        bool bm = byte_mode || (dst->mode == ADDR_REGISTER && dst->reg_class == REG_BYTE);
        codebuf_put(out, bm ? 0xF6 : 0xF7);
        if (dst->mode == ADDR_REGISTER) {
            int rn = reg_number(&dst->reg);
            if (rn < 0) return false;
            codebuf_put(out, (unsigned char)(0xC0 | rn));
        } else if (dst->mode == ADDR_INDIRECT) {
            emit_modrm_indirect(out, 0, dst, cur_addr, st, resolve, relocs);
        } else if (dst->mode == ADDR_DIRECT) {
            emit_modrm_direct(out, 0, dst, cur_addr, st, resolve, relocs);
        } else return false;
        if (bm) codebuf_put(out, (unsigned char)(imm & 0xFF));
        else {
            if (resolve && relocs && src->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(src->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(imm & 0xFFFF));
        }
        return true;
    }
    if (dst->mode == ADDR_REGISTER && (src->mode == ADDR_REGISTER || src->mode == ADDR_INDIRECT)) {
        int rn = reg_number(&dst->reg);
        if (rn < 0) return false;
        bool bm = byte_mode || dst->reg_class == REG_BYTE;
        codebuf_put(out, bm ? 0x84 : 0x85);
        if (src->mode == ADDR_REGISTER) {
            int srn = reg_number(&src->reg);
            if (srn < 0) return false;
            codebuf_put(out, (unsigned char)(0xC0 | (rn << 3) | srn));
        } else {
            emit_modrm_indirect(out, rn, src, cur_addr, st, resolve, relocs);
        }
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* INC / DEC                                                            */
/* ------------------------------------------------------------------ */

/* Register form uses the short 40-47/48-4F opcodes (confirmed: "inc
 * sp"/"inc ax"). A DIRECT (bare-address) or INDIRECT target - e.g.
 * "dec clkflg" - falls back to the general FE/FF /0 or /1 ModRM form
 * (word width assumed, since every real sample is a plain "inc"/"dec"
 * with no 'b' suffix - an "incb"/"decb" byte form is inferred by
 * symmetry with movb/xchgb but not directly observed). */
static bool encode_incdec(CodeBuf *out, bool is_inc, const ParsedOperand *op,
                           long cur_addr, SymTab *st, bool resolve, RelocList *relocs)
{
    if (op->mode == ADDR_REGISTER && op->reg_class == REG_WORD) {
        int rn = reg_number(&op->reg);
        if (rn < 0) return false;
        codebuf_put(out, (unsigned char)((is_inc ? 0x40 : 0x48) + rn));
        return true;
    }
    /* Byte-class register (e.g. "dec bl") - the short 40-4F/48-4F forms
     * only exist for WORD registers on 8086/80186; a byte register
     * needs the general FE /0 or /1 form with mod=11 register-direct
     * ModRM. Confirmed necessary by size (using the word short-form
     * here would silently produce the wrong register AND the wrong,
     * too-short byte count - "dec bl" must be 2 bytes, FE CB). */
    if (op->mode == ADDR_REGISTER && op->reg_class == REG_BYTE) {
        int rn = reg_number(&op->reg);
        if (rn < 0) return false;
        codebuf_put(out, 0xFE);
        codebuf_put(out, (unsigned char)(0xC0 | ((is_inc ? 0 : 1) << 3) | rn));
        return true;
    }
    if (op->mode == ADDR_INDIRECT || op->mode == ADDR_DIRECT) {
        codebuf_put(out, 0xFF);
        int digit = is_inc ? 0 : 1;
        if (op->mode == ADDR_INDIRECT)
            emit_modrm_indirect(out, digit, op, cur_addr, st, resolve, relocs);
        else
            emit_modrm_direct(out, digit, op, cur_addr, st, resolve, relocs);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* XCHG / XCHGB                                                         */
/* ------------------------------------------------------------------ */

/* Word XCHG with AX as either operand ALWAYS uses the 0x90+reg
 * shortcut - confirmed against real mch.o: "xchg ax,bp"/"xchg ax,dx"/
 * "xchg ax,di" all disassemble to the 1-byte-opcode shortcut (0x95,
 * 0x92, 0x97), never the general 87 ModRM form. Byte XCHG (xchgb) has
 * no such shortcut and always uses the general 86 ModRM form -
 * confirmed: "xchgb ah,al" -> 86 e0 (mod=11). */
static bool encode_xchg(CodeBuf *out, const ParsedOperand *a, const ParsedOperand *b, bool byte_mode)
{
    if (!byte_mode) {
        bool a_is_ax = a->mode == ADDR_REGISTER && a->reg.len == 2 && strncmp(a->reg.text, "ax", 2) == 0;
        bool b_is_ax = b->mode == ADDR_REGISTER && b->reg.len == 2 && strncmp(b->reg.text, "ax", 2) == 0;
        if (a_is_ax && b->mode == ADDR_REGISTER) {
            int rn = reg_number(&b->reg);
            if (rn < 0) return false;
            codebuf_put(out, (unsigned char)(0x90 + rn));
            return true;
        }
        if (b_is_ax && a->mode == ADDR_REGISTER) {
            int rn = reg_number(&a->reg);
            if (rn < 0) return false;
            codebuf_put(out, (unsigned char)(0x90 + rn));
            return true;
        }
    }
    if (a->mode == ADDR_REGISTER && b->mode == ADDR_REGISTER) {
        int an = reg_number(&a->reg), bn = reg_number(&b->reg);
        if (an < 0 || bn < 0) return false;
        codebuf_put(out, byte_mode ? 0x86 : 0x87);
        codebuf_put(out, (unsigned char)(0xC0 | (an << 3) | bn));
        return true;
    }
    return false; /* reg,mem xchg: not needed by the current corpus subset */
}

/* ------------------------------------------------------------------ */
/* MOV                                                                   */
/* ------------------------------------------------------------------ */

static int seg_reg_number(const Token *t)
{
    static const struct { const char *name; int num; } S[] = {
        {"es",0}, {"cs",1}, {"ss",2}, {"ds",3},
    };
    for (size_t i = 0; i < sizeof(S)/sizeof(S[0]); i++)
        if (strlen(S[i].name) == t->len && strncmp(S[i].name, t->text, t->len) == 0)
            return S[i].num;
    return -1;
}

static bool encode_mov(CodeBuf *out, const ParsedOperand *dst, const ParsedOperand *src,
                        long cur_addr, SymTab *st, bool resolve, bool byte_mode, RelocList *relocs)
{
    /* Segment-register MOV - separate opcodes (8E load / 8C store),
     * confirmed real usage: "mov ds,ax" / "mov ax,cs" / "mov bx,cs". */
    if (dst->mode == ADDR_REGISTER && dst->reg_class == REG_SEGMENT &&
        (src->mode == ADDR_REGISTER || src->mode == ADDR_INDIRECT)) {
        int sn = seg_reg_number(&dst->reg);
        if (sn < 0) return false;
        codebuf_put(out, 0x8E);
        if (src->mode == ADDR_REGISTER) {
            int srn = reg_number(&src->reg);
            if (srn < 0) return false;
            codebuf_put(out, (unsigned char)(0xC0 | (sn << 3) | srn));
        } else {
            emit_modrm_indirect(out, sn, src, cur_addr, st, resolve, relocs);
        }
        return true;
    }
    if (src->mode == ADDR_REGISTER && src->reg_class == REG_SEGMENT &&
        (dst->mode == ADDR_REGISTER || dst->mode == ADDR_INDIRECT)) {
        int sn = seg_reg_number(&src->reg);
        if (sn < 0) return false;
        codebuf_put(out, 0x8C);
        if (dst->mode == ADDR_REGISTER) {
            int drn = reg_number(&dst->reg);
            if (drn < 0) return false;
            codebuf_put(out, (unsigned char)(0xC0 | (sn << 3) | drn));
        } else {
            emit_modrm_indirect(out, sn, dst, cur_addr, st, resolve, relocs);
        }
        return true;
    }

    /* reg,imm -> always the short B8+reg iw / B0+reg ib form,
     * REGARDLESS of the immediate's own size marker - confirmed
     * against real code (malloc.o: "mov dx,*0." encodes as B8-family
     * "ba 00 00", not any *-sized form; opcodetest.o: "mov di,*42."
     * likewise). movb forces the byte form (B0+reg ib) instead. */
    if (dst->mode == ADDR_REGISTER && src->mode == ADDR_IMMEDIATE) {
        int rn = reg_number(&dst->reg);
        if (rn < 0) return false;
        long imm = resolve_expr(src->expr, cur_addr, st, resolve);
        /* Byte form applies both for 'movb' (word-name-means-byte
         * convention) AND for a genuinely byte-class register written
         * directly with plain 'mov' - confirmed real: "mov al,*0"
         * (not movb) still encodes as the 2-byte B0+reg ib form. */
        if (byte_mode || dst->reg_class == REG_BYTE) {
            codebuf_put(out, (unsigned char)(0xB0 + rn));
            codebuf_put(out, (unsigned char)(imm & 0xFF));
        } else {
            codebuf_put(out, (unsigned char)(0xB8 + rn));
            /* Immediate word could hold a symbol's address (e.g. "mov
             * dx,#_execnt") - CONFIRMED real via sys1nonopt.o needing
             * an R_DATA entry here (this short B8+reg form had zero
             * relocation coverage before). */
            if (resolve && relocs && src->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(src->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(imm & 0xFFFF));
        }
        return true;
    }

    /* mem,imm -> C7 /0 iw (word) or C6 /0 ib (byte). CONFIRMED real
     * (sys1nonopt.o "mov *4.(di),*0." -> C7, a plain 'mov' with a
     * '*'-marked immediate still word-sized): this selection depends
     * ONLY on byte_mode (i.e. whether the mnemonic was 'movb'), NEVER
     * on the immediate operand's own '*'/'#' marker. Do not
     * reintroduce a src->size check here - it was already found and
     * removed once before. */
    if (dst->mode == ADDR_INDIRECT && src->mode == ADDR_IMMEDIATE) {
        long imm = resolve_expr(src->expr, cur_addr, st, resolve);
        if (byte_mode) {
            codebuf_put(out, 0xC6);
            emit_modrm_indirect(out, 0, dst, cur_addr, st, resolve, relocs);
            codebuf_put(out, (unsigned char)(imm & 0xFF));
        } else {
            codebuf_put(out, 0xC7);
            emit_modrm_indirect(out, 0, dst, cur_addr, st, resolve, relocs);
            /* The stored immediate word (not the address field, which
             * emit_modrm_indirect above already covers) can itself
             * hold a symbol's address too. */
            if (resolve && relocs && src->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(src->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(imm & 0xFFFF));
        }
        return true;
    }
    if (dst->mode == ADDR_DIRECT && src->mode == ADDR_IMMEDIATE) {
        long imm = resolve_expr(src->expr, cur_addr, st, resolve);
        if (byte_mode) {
            codebuf_put(out, 0xC6);
            emit_modrm_direct(out, 0, dst, cur_addr, st, resolve, relocs);
            codebuf_put(out, (unsigned char)(imm & 0xFF));
        } else {
            codebuf_put(out, 0xC7);
            emit_modrm_direct(out, 0, dst, cur_addr, st, resolve, relocs);
            if (resolve && relocs && src->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(src->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(imm & 0xFFFF));
        }
        return true;
    }

    /* mem,reg -> 0x89/0x88 /r store form. DIRECT (bare address) with
     * the accumulator (AL/AX) instead gets the short A2/A3 forms -
     * confirmed real: opcodetest.o "movb _global_,ax" (AL) -> A2,
     * "mov _global_,ax" (AX) -> A3. Any other register, or any
     * INDIRECT destination, always uses the general 88/89 ModRM form. */
    if ((dst->mode == ADDR_INDIRECT || dst->mode == ADDR_DIRECT) && src->mode == ADDR_REGISTER) {
        int rn = reg_number(&src->reg);
        if (rn < 0) return false;
        if (dst->mode == ADDR_DIRECT && rn == 0) {
            codebuf_put(out, byte_mode ? 0xA2 : 0xA3);
            long addr = dst->expr ? resolve_expr(dst->expr, cur_addr, st, resolve) : 0;
            if (resolve && relocs && dst->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(dst->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(addr & 0xFFFF));
            return true;
        }
        codebuf_put(out, byte_mode ? 0x88 : 0x89);
        if (dst->mode == ADDR_INDIRECT)
            emit_modrm_indirect(out, rn, dst, cur_addr, st, resolve, relocs);
        else
            emit_modrm_direct(out, rn, dst, cur_addr, st, resolve, relocs);
        return true;
    }

    /* reg,mem or reg,reg -> 0x8B/0x8A /r load form, with the same
     * AL/AX-direct-address short-form exception (A0/A1) as above -
     * confirmed real: opcodetest.o "movb ax,_global_" (AL) -> A0. */
    if (dst->mode == ADDR_REGISTER &&
        (src->mode == ADDR_INDIRECT || src->mode == ADDR_REGISTER || src->mode == ADDR_DIRECT)) {
        int rn = reg_number(&dst->reg);
        if (rn < 0) return false;

        if (src->mode == ADDR_DIRECT && rn == 0) {
            codebuf_put(out, byte_mode ? 0xA0 : 0xA1);
            long addr = src->expr ? resolve_expr(src->expr, cur_addr, st, resolve) : 0;
            if (resolve && relocs && src->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(src->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(addr & 0xFFFF));
            return true;
        }

        codebuf_put(out, byte_mode ? 0x8A : 0x8B);
        if (src->mode == ADDR_REGISTER) {
            int srn = reg_number(&src->reg);
            if (srn < 0) return false;
            codebuf_put(out, (unsigned char)(0xC0 | (rn << 3) | srn));
        } else if (src->mode == ADDR_INDIRECT) {
            emit_modrm_indirect(out, rn, src, cur_addr, st, resolve, relocs);
        } else { /* ADDR_DIRECT, non-accumulator register */
            emit_modrm_direct(out, rn, src, cur_addr, st, resolve, relocs);
        }
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* PUSH / POP                                                            */
/* ------------------------------------------------------------------ */

static bool encode_pushpop(CodeBuf *out, bool is_push, const ParsedOperand *op,
                            long cur_addr, SymTab *st, bool resolve, RelocList *relocs)
{
    if (op->mode == ADDR_REGISTER && op->reg_class == REG_WORD) {
        int rn = reg_number(&op->reg);
        if (rn < 0) return false;
        codebuf_put(out, (unsigned char)((is_push ? 0x50 : 0x58) + rn));
        return true;
    }
    /* Segment-register push/pop - separate, non-contiguous opcodes
     * (there is no POP CS on 8086/80186). Confirmed real usage:
     * "push ds". */
    if (op->mode == ADDR_REGISTER && op->reg_class == REG_SEGMENT) {
        static const struct { const char *name; unsigned char push_op, pop_op; } SEG[] = {
            {"es", 0x06, 0x07}, {"cs", 0x0E, 0x00 /* no POP CS */},
            {"ss", 0x16, 0x17}, {"ds", 0x1E, 0x1F},
        };
        for (size_t i = 0; i < sizeof(SEG)/sizeof(SEG[0]); i++)
            if (strlen(SEG[i].name) == op->reg.len && strncmp(SEG[i].name, op->reg.text, op->reg.len) == 0) {
                unsigned char b = is_push ? SEG[i].push_op : SEG[i].pop_op;
                if (b == 0 && !is_push) return false; /* POP CS doesn't exist */
                codebuf_put(out, b);
                return true;
            }
        return false;
    }
    /* PUSH of a constant/address expression - confirmed real usage:
     * "push monssvec+2". IMPORTANT correction found via byte-level
     * validation: a BARE (unmarked) DIRECT expression means "push the
     * WORD STORED AT that address" (FF /6, general memory operand),
     * exactly like an unmarked DIRECT operand always means "dereference"
     * for mov/group1/etc - NOT "push this address as an immediate
     * constant". Real mch.o confirms: "push monssvec+2" disassembles
     * to FF 36 xx xx (PUSH r/m16, mod=00 rm=110 disp16), 4 bytes, not
     * the 3-byte 0x68 imm16 form. A genuinely marked immediate push
     * ('#'/'*'-prefixed, ADDR_IMMEDIATE) still uses 0x68/0x6A - by
     * analogy with the marker convention elsewhere, though no real
     * sample of THAT specific form exists in the corpus to confirm. */
    if (is_push && op->mode == ADDR_DIRECT && op->expr) {
        codebuf_put(out, 0xFF);
        emit_modrm_direct(out, 6, op, cur_addr, st, resolve, relocs);
        return true;
    }
    if (is_push && op->mode == ADDR_IMMEDIATE && op->expr) {
        long v = resolve_expr(op->expr, cur_addr, st, resolve);
        if (op->size == SZ_BYTE) {
            codebuf_put(out, 0x6A);
            codebuf_put(out, (unsigned char)(v & 0xFF));
        } else {
            codebuf_put(out, 0x68);
            /* Immediate word could hold a symbol's address (e.g. "push
             * #_symbol") - same non-PC-relative relocation coverage as
             * every other word-immediate site. */
            if (resolve && relocs && op->expr) {
                unsigned char low4; int symidx;
                if (classify_word_reloc(op->expr, st, true, &low4, &symidx))
                    reloclist_add(relocs, (long)out->len, symidx, low4);
            }
            codebuf_put16le(out, (unsigned int)(v & 0xFFFF));
        }
        return true;
    }
    /* PUSH of an INDIRECT memory operand - confirmed real usage:
     * "push *4.(bp)" -> FF /6, mod01 disp8. POP never takes an
     * immediate; a POP-of-memory form (8F /0) has not been observed
     * and is not implemented. */
    if (is_push && op->mode == ADDR_INDIRECT) {
        codebuf_put(out, 0xFF);
        emit_modrm_indirect(out, 6, op, cur_addr, st, resolve, relocs);
        return true;
    }
    return false;
}

/* LEA reg,mem - confirmed real usage: "lea sp,#6(bp)", "lea sp,#-4(bp)". */
static bool encode_lea(CodeBuf *out, const ParsedOperand *dst, const ParsedOperand *src,
                        long cur_addr, SymTab *st, bool resolve, RelocList *relocs)
{
    if (dst->mode != ADDR_REGISTER || src->mode != ADDR_INDIRECT) return false;
    int rn = reg_number(&dst->reg);
    if (rn < 0) return false;
    codebuf_put(out, 0x8D);
    emit_modrm_indirect(out, rn, src, cur_addr, st, resolve, relocs);
    return true;
}

/* LDS/LES reg,mem - C5/C4 /r (load a far pointer: reg gets the
 * offset, DS/ES gets the segment, from a 4-byte memory location) -
 * Assembler_as.pdf Anlage A: "load pointer using DS lds 2 R+W A+W" /
 * "load pointer using ES les 2 R+W A+W". Same ModRM shape as LEA
 * above, just a different opcode and a genuine memory dereference
 * (LEA computes an address, LDS/LES load THROUGH one). NOT yet
 * observed in the real corpus. */
static bool encode_ldx(CodeBuf *out, unsigned char opcode, const ParsedOperand *dst,
                        const ParsedOperand *src, long cur_addr, SymTab *st, bool resolve,
                        RelocList *relocs)
{
    if (dst->mode != ADDR_REGISTER || src->mode != ADDR_INDIRECT) return false;
    int rn = reg_number(&dst->reg);
    if (rn < 0) return false;
    codebuf_put(out, opcode);
    emit_modrm_indirect(out, rn, src, cur_addr, st, resolve, relocs);
    return true;
}

/* Group3 single-operand arithmetic/logic: NOT(/2) NEG(/3) MUL(/4)
 * IMUL(/5) DIV(/6) IDIV(/7) - opcode F6 (byte) / F7 (word), same
 * ModRM-digit-selects-operation shape for all six, register OR memory
 * (INDIRECT/DIRECT) operand. Confirmed real via kernel_opt/amx.s AND
 * kernel_nonopt/amx.s: "imul *-12.(bp)" is a genuine memory operand,
 * proving the earlier "register-only" note was based on an incomplete
 * corpus sample - the real 8086 ISA allows r/m8/r/m16 for every
 * Group3 op, and k5170.s additionally exercises this for "div" with a
 * memory operand. Byte-vs-word for a REGISTER operand is inferred
 * from the register class (REG_BYTE); for an INDIRECT/DIRECT operand
 * there's no register to infer from, AND the operand's own '*'/'#'
 * marker does NOT indicate operation width either - confirmed real:
 * "imul *-12.(bp)" (byte-MARKED displacement) still disassembles to
 * the WORD form F7, not F6. This matches the established asymmetric-
 * marker principle used throughout this file: on a DISPLACEMENT, the
 * marker is only ever a mod01-vs-mod10 addressing-mode fallback (see
 * emit_modrm_indirect's doc comment), never an operation-width
 * indicator - so a memory operand here can ONLY get the byte form via
 * an explicit 'b'-suffixed mnemonic (mulb/negb/imulb/divb/idivb/notb,
 * mirroring incb/decb elsewhere in this file). */
static bool encode_group3(CodeBuf *out, int digit, const ParsedOperand *op,
                           long cur_addr, SymTab *st, bool resolve, RelocList *relocs,
                           bool byte_mode_suffix)
{
    if (op->mode == ADDR_REGISTER) {
        int rn = reg_number(&op->reg);
        if (rn < 0) return false;
        bool byte_mode = byte_mode_suffix || (op->reg_class == REG_BYTE);
        codebuf_put(out, byte_mode ? 0xF6 : 0xF7);
        codebuf_put(out, (unsigned char)(0xC0 | (digit << 3) | rn));
        return true;
    }
    if (op->mode == ADDR_INDIRECT || op->mode == ADDR_DIRECT) {
        codebuf_put(out, byte_mode_suffix ? 0xF6 : 0xF7);
        if (op->mode == ADDR_INDIRECT)
            emit_modrm_indirect(out, digit, op, cur_addr, st, resolve, relocs);
        else
            emit_modrm_direct(out, digit, op, cur_addr, st, resolve, relocs);
        return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* JMP / pseudo-branches                                                */
/* ------------------------------------------------------------------ */

static long expr_target_addr(const ExprNode *e, long cur_addr, SymTab *st, bool resolve)
{
    return resolve_expr(e, cur_addr, st, resolve);
}

/* True if `e` is a bare symbol reference that is still unresolved at
 * this point (truly undefined, or a .comm/BSS symbol - see the
 * resolve_expr() comment on why BSS doesn't count as a real address).
 * Used to decide whether an R_EXT relocation entry is needed. */
static bool expr_is_unresolved_sym(const ExprNode *e, SymTab *st, int *symidx_out)
{
    if (!e || e->op != EX_SYM) return false;
    Symbol *s = symtab_find(st, e->sym, e->sym_len);
    if (s && s->defined && s->type != SYM_BSS) return false;
    *symidx_out = s ? s->out_index : -1; /* -1 should not happen in practice -
                                           * resolve_expr() always registers
                                           * EX_SYM symbols first (see above) -
                                           * but never leave this uninitialized. */
    return true;
}

static bool encode_jmp(CodeBuf *out, const ParsedOperand *target, long cur_addr, SymTab *st,
                        bool resolve, RelocList *relocs)
{
    if (target->mode == ADDR_INDIRECT) {
        if (target->reg_class != REG_WORD) return false;
        codebuf_put(out, 0xFF);
        if (target->expr == NULL) {
            /* "jmp/br @reg" - register-indirect jump, FF /4, mod=11. */
            int rn = reg_number(&target->reg);
            if (rn < 0) return false;
            codebuf_put(out, (unsigned char)(0xE0 | rn)); /* /4, mod=11 */
        } else {
            /* "jmp/br @disp(reg)" - indirect THROUGH a computed memory
             * address (the WORD stored there is the real target) -
             * CONFIRMED real via tty.s "br @L10001(bx)" (a jump-table
             * dispatch). Same displacement/ModRM machinery, and same
             * relocation coverage, as any other indirect memory
             * operand - see emit_modrm_indirect. */
            emit_modrm_indirect(out, 4, target, cur_addr, st, resolve, relocs);
        }
        return true;
    }
    if (target->mode != ADDR_DIRECT || !target->expr) return false;
    long field_off = cur_addr + 1; /* the E9's disp16 starts 1 byte in */
    codebuf_put(out, 0xE9);
    long tgt = expr_target_addr(target->expr, cur_addr, st, resolve);
    long rel = resolve ? (tgt - (cur_addr + 3)) : 0;
    codebuf_put16le(out, (unsigned int)(rel & 0xFFFF));

    if (resolve && relocs) {
        int symidx;
        if (expr_is_unresolved_sym(target->expr, st, &symidx))
            reloclist_add(relocs, field_off, symidx, 0x09); /* R_EXT + PCREL */
    }
    return true;
}

/* CALL rel16 - same shape as JMP, different opcode (E8 vs E9); confirmed
 * real (opcodetest.o: "call _putchar" etc. -> E8 rel16, IP-relative,
 * same as JMP for internal targets - no relocation needed). */
static bool encode_call(CodeBuf *out, const ParsedOperand *target, long cur_addr, SymTab *st,
                         bool resolve, RelocList *relocs)
{
    if (target->mode == ADDR_INDIRECT) {
        if (target->reg_class != REG_WORD) return false;
        codebuf_put(out, 0xFF);
        if (target->expr == NULL) {
            /* "call @reg" - register-indirect call, FF /2, mod=11. */
            int rn = reg_number(&target->reg);
            if (rn < 0) return false;
            codebuf_put(out, (unsigned char)(0xD0 | rn)); /* /2, mod=11 */
        } else {
            /* "call @disp(reg)" - indirect THROUGH a computed memory
             * address (e.g. a device-switch-table dispatch) - CONFIRMED
             * real via tty.s "call @8.+_cdevsw(bx)". Same machinery as
             * encode_jmp's memory-indirect case above. */
            emit_modrm_indirect(out, 2, target, cur_addr, st, resolve, relocs);
        }
        return true;
    }
    if (target->mode != ADDR_DIRECT || !target->expr) return false;
    long field_off = cur_addr + 1;
    codebuf_put(out, 0xE8);
    long tgt = expr_target_addr(target->expr, cur_addr, st, resolve);
    long rel = resolve ? (tgt - (cur_addr + 3)) : 0;
    codebuf_put16le(out, (unsigned int)(rel & 0xFFFF));

    if (resolve && relocs) {
        int symidx;
        if (expr_is_unresolved_sym(target->expr, st, &symidx))
            reloclist_add(relocs, field_off, symidx, 0x09); /* R_EXT + PCREL */
    }
    return true;
}

/* CALLI/JMPI - "inter segment call/jump" (Anlage A: "calli 1 @ A+W" /
 * "calli 1 d:s", "jmpi 1 @ M+W" / "jmpi 1 d:s"). Only the INDIRECT
 * form ("@reg"/"@disp(reg)", FF /3 for calli and FF /5 for jmpi - the
 * intersegment siblings of call's FF /2 and jmp's FF /4) is
 * implemented here, reusing the exact same indirect-operand machinery
 * as encode_call/encode_jmp. The DIRECT "d:s" (segment:offset literal)
 * form is a DELIBERATE, DOCUMENTED GAP: it needs a genuinely new
 * operand syntax (a colon-separated segment:offset pair) that neither
 * the lexer nor classify_operand currently parse at all, and no real
 * corpus sample uses it - implementing it speculatively without any
 * real syntax/encoding sample to confirm against would risk guessing
 * wrong. NOT yet observed in the real corpus in any form. */
static bool encode_calli(CodeBuf *out, const ParsedOperand *target, long cur_addr, SymTab *st,
                          bool resolve, RelocList *relocs)
{
    if (target->mode != ADDR_INDIRECT || target->reg_class != REG_WORD) return false;
    codebuf_put(out, 0xFF);
    if (target->expr == NULL) {
        int rn = reg_number(&target->reg);
        if (rn < 0) return false;
        codebuf_put(out, (unsigned char)(0xD8 | rn)); /* /3, mod=11 */
    } else {
        emit_modrm_indirect(out, 3, target, cur_addr, st, resolve, relocs);
    }
    return true;
}

static bool encode_jmpi(CodeBuf *out, const ParsedOperand *target, long cur_addr, SymTab *st,
                         bool resolve, RelocList *relocs)
{
    if (target->mode != ADDR_INDIRECT || target->reg_class != REG_WORD) return false;
    codebuf_put(out, 0xFF);
    if (target->expr == NULL) {
        int rn = reg_number(&target->reg);
        if (rn < 0) return false;
        codebuf_put(out, (unsigned char)(0xE8 | rn)); /* /5, mod=11 */
    } else {
        emit_modrm_indirect(out, 5, target, cur_addr, st, resolve, relocs);
    }
    return true;
}

/* Real (non-pseudo) short conditional jumps, e.g. "je L0007" - unlike
 * beq/bne/etc., these are NOT expanded; they are exactly a 0x7x rel8
 * jump, confirmed via real mch.o usage of je/jne/jnz directly. jz/jnz
 * are the same opcodes as je/jne (both test ZF). */
typedef struct { const char *name; unsigned char opcode; } RealJcc;
static const RealJcc REAL_JCC[] = {
    {"je",0x74}, {"jz",0x74}, {"jne",0x75}, {"jnz",0x75},
    {"jl",0x7C}, {"jge",0x7D}, {"jle",0x7E}, {"jg",0x7F},
    {"jb",0x72}, {"jc",0x72}, {"jae",0x73}, {"jnc",0x73},
    {"jbe",0x76}, {"jna",0x76}, {"ja",0x77}, {"jnbe",0x77},
    {"js",0x78}, {"jns",0x79}, {"jp",0x7A}, {"jpe",0x7A}, {"jnp",0x7B}, {"jpo",0x7B},
    /* Remaining Anlage A alias spellings for opcodes already above -
     * pure "not" negations of an existing condition, same opcode:
     * jnae="not above or equal"=jb, jnb="not below"=jae,
     * jng="not greater"=jle, jnge="not greater or equal"=jl,
     * jnl="not less"=jge, jnle="not less or equal"=jg. UNCONFIRMED by
     * any real sample (every real corpus Jcc use so far picks the
     * "positive" spelling already in the table above), but these are
     * definitionally the exact same opcode by 8086 flag semantics, so
     * there is no real ambiguity to confirm. */
    {"jnae",0x72}, {"jnb",0x73}, {"jng",0x7E}, {"jnge",0x7C}, {"jnl",0x7D}, {"jnle",0x7F},
    /* jo/jno are genuinely NEW opcodes (test OF), not aliases of
     * anything else already in this table. */
    {"jo",0x70}, {"jno",0x71},
};
static const RealJcc *real_jcc_lookup(const Token *mnem)
{
    for (size_t i = 0; i < sizeof(REAL_JCC)/sizeof(REAL_JCC[0]); i++)
        if (strlen(REAL_JCC[i].name) == mnem->len && strncmp(REAL_JCC[i].name, mnem->text, mnem->len) == 0)
            return &REAL_JCC[i];
    return NULL;
}
static bool encode_real_jcc(CodeBuf *out, unsigned char opcode, const ParsedOperand *target,
                             long cur_addr, SymTab *st, bool resolve)
{
    if (target->mode != ADDR_DIRECT || !target->expr) return false;
    codebuf_put(out, opcode);
    long tgt = expr_target_addr(target->expr, cur_addr, st, resolve);
    long rel = resolve ? (tgt - (cur_addr + 2)) : 0;
    codebuf_put(out, (unsigned char)(rel & 0xFF));
    return true;
}

/* LOOP family + JCXZ - confirmed real target syntax includes the bare
 * '.' location-counter symbol for a busy-wait idiom ("loop ." = loop
 * back onto itself, disp=-2). */
typedef struct { const char *name; unsigned char opcode; } LoopOp;
static const LoopOp LOOP_OPS[] = {
    {"loop",0xE2}, {"loope",0xE1}, {"loopz",0xE1}, {"loopne",0xE0}, {"loopnz",0xE0},
    {"jcxz",0xE3},
};
static const LoopOp *loop_op_lookup(const Token *mnem)
{
    for (size_t i = 0; i < sizeof(LOOP_OPS)/sizeof(LOOP_OPS[0]); i++)
        if (strlen(LOOP_OPS[i].name) == mnem->len && strncmp(LOOP_OPS[i].name, mnem->text, mnem->len) == 0)
            return &LOOP_OPS[i];
    return NULL;
}
static bool encode_loop(CodeBuf *out, unsigned char opcode, const ParsedOperand *target,
                         long cur_addr, SymTab *st, bool resolve)
{
    if (target->mode != ADDR_DIRECT || !target->expr) return false;
    codebuf_put(out, opcode);
    long tgt = expr_target_addr(target->expr, cur_addr, st, resolve);
    long rel = resolve ? (tgt - (cur_addr + 2)) : 0;
    codebuf_put(out, (unsigned char)(rel & 0xFF));
    return true;
}

typedef struct { const char *name; unsigned char negated_jcc; } PseudoBranch;
static const PseudoBranch PSEUDO_BRANCHES[] = {
    /* Confirmed: c1/mutos_as unconditionally expands these into
     * "negated-Jcc(skip 3 bytes) + long jmp rel16", even well within
     * short-branch range - see the consolidated Milestone 2 notes. */
    {"beq", 0x75}, /* negate of je  is jne */
    {"bne", 0x74}, /* negate of jne is je  */
    {"blt", 0x7D}, /* negate of jl  is jge */
    {"bge", 0x7C}, /* negate of jge is jl  */
    {"ble", 0x7F}, /* negate of jle is jg  */
    {"bgt", 0x7E}, /* negate of jg  is jle */
    {"bhi", 0x76}, /* negate of ja  is jbe (unsigned) */
    {"blos",0x77}, /* negate of jbe is ja  (unsigned) */
    {"bloss",0x77}, /* Anlage B's documented spelling for the same
                      * mnemonic real code writes as "blos" (4 letters,
                      * confirmed via malloc.s/sys1nonopt.s) - accepted
                      * as a harmless synonym for the same opcode,
                      * matching the inb/outb precedent. UNCONFIRMED
                      * whether the real assembler itself accepts this
                      * 5-letter spelling too. */
    {"bhis",0x72}, /* negate of jae (bhis, unsigned >=) is jb (unsigned) */
    {"blo", 0x73}, /* negate of jb  (blo,  unsigned <)  is jae (unsigned) -
                     * consistent with the same jb(0x72)/jae(0x73) pair used
                     * by bhis above, per Assembler_as.pdf Anlage B. */
};

/* "br" = unconditional long branch (Assembler_as.pdf Anlage B). Unlike
 * the conditional pseudo-branches above, there is no condition to
 * negate - Anlage B lists its "Code des Sprungs" as 0x00 (n/a) for
 * exactly this reason. The manual's expansion rule ("passender
 * bedingter Sprung + unbedingter Sprung") degenerates to just the
 * unconditional jump itself, i.e. identical output to "jmp" (E9 rel16,
 * 3 bytes) - so it is dispatched straight to encode_jmp(). */

static const PseudoBranch *pseudo_branch_lookup(const Token *mnem)
{
    for (size_t i = 0; i < sizeof(PSEUDO_BRANCHES)/sizeof(PSEUDO_BRANCHES[0]); i++)
        if (strlen(PSEUDO_BRANCHES[i].name) == mnem->len &&
            strncmp(PSEUDO_BRANCHES[i].name, mnem->text, mnem->len) == 0)
            return &PSEUDO_BRANCHES[i];
    return NULL;
}

static bool encode_pseudo_branch(CodeBuf *out, const PseudoBranch *pb, const ParsedOperand *target,
                                  long cur_addr, SymTab *st, bool resolve, RelocList *relocs)
{
    if (target->mode != ADDR_DIRECT || !target->expr) return false;
    codebuf_put(out, pb->negated_jcc);
    codebuf_put(out, 0x03); /* skip over the 3-byte jmp below */
    codebuf_put(out, 0xE9);
    long jmp_addr = cur_addr + 2; /* the E9 starts 2 bytes into this instruction */
    long field_off = jmp_addr + 1;
    long tgt = expr_target_addr(target->expr, cur_addr, st, resolve);
    long rel = resolve ? (tgt - (jmp_addr + 3)) : 0;
    codebuf_put16le(out, (unsigned int)(rel & 0xFFFF));

    if (resolve && relocs) {
        int symidx;
        if (expr_is_unresolved_sym(target->expr, st, &symidx))
            reloclist_add(relocs, field_off, symidx, 0x09); /* R_EXT + PCREL */
    }
    return true;
}

/* Single-byte, no-operand instruction table - see its use-site inside
 * encode_instruction() below for provenance/corpus-confirmation notes
 * on individual entries. Kept at FILE SCOPE (rather than local to
 * encode_instruction(), as it originally was) so encode_is_known_mnemonic()
 * can walk this exact same array instead of a second, separately
 * hand-typed copy of the same name list that could silently drift out
 * of sync with it. */
static const struct { const char *name; unsigned char op; } NOARG_TABLE[] = {
    {"cbw", 0x98}, {"cwd", 0x99}, {"ret", 0xC3}, {"nop", 0x90},
    {"cli", 0xFA}, {"sti", 0xFB}, {"cld", 0xFC}, {"std", 0xFD},
    {"pusha", 0x60}, {"popa", 0x61}, {"stob", 0xAA},
    {"pushf", 0x9C}, {"popf", 0x9D}, {"stow", 0xAB}, {"lodb", 0xAC}, {"lodw", 0xAD},
    {"wait", 0x9B}, {"iret", 0xCF}, {"reti", 0xCB},
    /* NOTE: "reti" is NOT an alias of "iret" - real mch.o disassembly
     * confirms "iret" = CF (true IRET, restores flags) while
     * "reti" = CB (RETF, far return only, no flags) - two genuinely
     * different mnemonics/opcodes that happen to look similar; do not
     * conflate them. */

    /* Remaining Assembler_as.pdf Anlage A zero-operand instructions
     * with no real corpus sample yet, but plain, unambiguous
     * single-byte 8086 opcodes (no MUTOS-specific encoding quirk
     * possible - there's no operand to have a quirky marker/
     * addressing convention on). */
    {"clc", 0xF8}, {"cmc", 0xF5}, {"stc", 0xF9}, {"hlt", 0xF4},
    {"lahf", 0x9F}, {"sahf", 0x9E}, {"into", 0xCE}, {"xlat", 0xD7},
    {"lock", 0xF0}, {"daa", 0x27}, {"das", 0x2F}, {"aaa", 0x37}, {"aas", 0x3F},
    /* CMPS/SCAS (compare-string/scan-string) - documented plainly as
     * "cmps"/"cmpsb"/"scas"/"scasb" in Anlage A with no separate
     * word-vs-implicit-default naming twist (unlike MOVS, which real
     * code confirms uses these exact bare names too - see
     * "movs"/"movsb" in encode_instruction) - included here under
     * their documented names; UNCONFIRMED by any real sample
     * (STOS/LODS are the only string-op family known to diverge from
     * Anlage A's naming, into stow/stob/lodw/lodb - see the
     * STMT_INSTRUCTION dispatch's "in"/"inw" comment for the same
     * divergence pattern in the I/O family). */
    {"cmps", 0xA7}, {"cmpsb", 0xA6}, {"scas", 0xAF}, {"scasb", 0xAE},
};

/* ------------------------------------------------------------------ */
/* Dispatch                                                              */
/* ------------------------------------------------------------------ */

bool encode_instruction(CodeBuf *out, const Token *mnemonic,
                         const ParsedOperand *ops, int nops,
                         long cur_addr, SymTab *st, bool resolve,
                         RelocList *relocs)
{
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "push", 4) == 0 && nops == 1)
        return encode_pushpop(out, true, &ops[0], cur_addr, st, resolve, relocs);
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "pop", 3) == 0 && nops == 1)
        return encode_pushpop(out, false, &ops[0], cur_addr, st, resolve, relocs);

    if (mnemonic->len == 3 && strncmp(mnemonic->text, "jmp", 3) == 0 && nops == 1)
        return encode_jmp(out, &ops[0], cur_addr, st, resolve, relocs);
    if (mnemonic->len == 2 && strncmp(mnemonic->text, "br", 2) == 0 && nops == 1)
        return encode_jmp(out, &ops[0], cur_addr, st, resolve, relocs);

    /* Bare "j <label>" - CORRECTION: this is NOT an alias of "jmp"
     * (an earlier assumption taken from the manual's wording). Real
     * mch.o disassembly proves "j exitmon" / "j ." / "j spl" all
     * encode as the SHORT EB rel8 form (2 bytes), never the long E9
     * rel16 form "jmp" always uses - confirmed via multiple real
     * samples, including the classic "j ." self-loop (disp=-2). */
    if (mnemonic->len == 1 && mnemonic->text[0] == 'j' && nops == 1) {
        if (ops[0].mode != ADDR_DIRECT || !ops[0].expr) return false;
        codebuf_put(out, 0xEB);
        long tgt = expr_target_addr(ops[0].expr, cur_addr, st, resolve);
        long rel = resolve ? (tgt - (cur_addr + 2)) : 0;
        codebuf_put(out, (unsigned char)(rel & 0xFF));
        return true;
    }

    if (mnemonic->len == 4 && strncmp(mnemonic->text, "call", 4) == 0 && nops == 1)
        return encode_call(out, &ops[0], cur_addr, st, resolve, relocs);
    if (mnemonic->len == 5 && strncmp(mnemonic->text, "calli", 5) == 0 && nops == 1)
        return encode_calli(out, &ops[0], cur_addr, st, resolve, relocs);
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "jmpi", 4) == 0 && nops == 1)
        return encode_jmpi(out, &ops[0], cur_addr, st, resolve, relocs);

    {
        const RealJcc *rj = real_jcc_lookup(mnemonic);
        if (rj && nops == 1)
            return encode_real_jcc(out, rj->opcode, &ops[0], cur_addr, st, resolve);
    }
    {
        const LoopOp *lp = loop_op_lookup(mnemonic);
        if (lp && nops == 1)
            return encode_loop(out, lp->opcode, &ops[0], cur_addr, st, resolve);
    }

    /* "rep" as its own 1-byte prefix pseudo-instruction, analogous to
     * "seg xx" - confirmed real usage: mch.s always writes it on its
     * own line immediately before a string op ("rep" / "movs", "rep" /
     * "stob"). "repe"/"repz" are accepted as synonyms (same 0xF3 byte -
     * on real 8086 hardware REP/REPE/REPZ are the exact same opcode,
     * the mnemonic distinction is purely about which string op follows
     * it: unconditional for movs/stos, "while equal/zero" for cmps/
     * scas). "repne"/"repnz" (0xF2, "while not equal/not zero") is the
     * other real opcode in this family - neither pair has been
     * observed in the corpus (which only ever pairs "rep" with movs/
     * stos), but both are documented in Assembler_as.pdf Anlage A. */
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "rep", 3) == 0 && nops == 0) {
        codebuf_put(out, 0xF3);
        return true;
    }
    if ((mnemonic->len == 4 && strncmp(mnemonic->text, "repe", 4) == 0 && nops == 0) ||
        (mnemonic->len == 4 && strncmp(mnemonic->text, "repz", 4) == 0 && nops == 0)) {
        codebuf_put(out, 0xF3);
        return true;
    }
    if ((mnemonic->len == 5 && strncmp(mnemonic->text, "repne", 5) == 0 && nops == 0) ||
        (mnemonic->len == 5 && strncmp(mnemonic->text, "repnz", 5) == 0 && nops == 0)) {
        codebuf_put(out, 0xF2);
        return true;
    }

    if (mnemonic->len == 4 && strncmp(mnemonic->text, "test", 4) == 0 && nops == 2)
        return encode_test(out, &ops[0], &ops[1], cur_addr, st, resolve, false, relocs);
    if (mnemonic->len == 5 && strncmp(mnemonic->text, "testb", 5) == 0 && nops == 2)
        return encode_test(out, &ops[0], &ops[1], cur_addr, st, resolve, true, relocs);

    if (mnemonic->len == 3 && strncmp(mnemonic->text, "inc", 3) == 0 && nops == 1)
        return encode_incdec(out, true, &ops[0], cur_addr, st, resolve, relocs);
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "dec", 3) == 0 && nops == 1)
        return encode_incdec(out, false, &ops[0], cur_addr, st, resolve, relocs);

    if (mnemonic->len == 4 && strncmp(mnemonic->text, "xchg", 4) == 0 && nops == 2)
        return encode_xchg(out, &ops[0], &ops[1], false);
    if (mnemonic->len == 5 && strncmp(mnemonic->text, "xchgb", 5) == 0 && nops == 2)
        return encode_xchg(out, &ops[0], &ops[1], true);

    {
        bool byte_mode = false;
        int opidx = group2_lookup(mnemonic, &byte_mode);
        if (opidx >= 0 && nops == 2)
            return encode_group2(out, opidx, &ops[0], &ops[1], cur_addr, st, resolve, relocs, byte_mode);
    }

    /* "movs" - bare, no operand, always immediately preceded by "rep"
     * in the corpus. Real width is UNCONFIRMED (no standalone .o with
     * this pattern to disassemble): defaults to the WORD form (0xA5),
     * following this toolchain's consistent "no suffix = word,
     * b-suffix = byte" convention seen everywhere else (mov/movb,
     * xchg/xchgb) - documented inference, not a proven fact. */
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "movs", 4) == 0 && nops == 0) {
        codebuf_put(out, 0xA5);
        return true;
    }

    /* "seg <segreg>" - emits the segment-override prefix byte for the
     * NEXT instruction. Confirmed real usage: mch.s "seg es" before a
     * ".byte /6f" (NEC V30 outm). Since x86 prefixes are just extra
     * bytes preceding an opcode, treating "seg xx" as its own 1-byte
     * pseudo-instruction needs no special coupling with what follows -
     * the byte stream ends up identical either way. */
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "seg", 3) == 0 && nops == 1 &&
        ops[0].mode == ADDR_REGISTER && ops[0].reg_class == REG_SEGMENT) {
        static const struct { const char *name; unsigned char pfx; } SEGPFX[] = {
            {"es", 0x26}, {"cs", 0x2E}, {"ss", 0x36}, {"ds", 0x3E},
        };
        for (size_t i = 0; i < sizeof(SEGPFX)/sizeof(SEGPFX[0]); i++)
            if (strlen(SEGPFX[i].name) == ops[0].reg.len &&
                strncmp(SEGPFX[i].name, ops[0].reg.text, ops[0].reg.len) == 0) {
                codebuf_put(out, SEGPFX[i].pfx);
                return true;
            }
        return false;
    }

    /* IN/OUT - confirmed real forms (mch.s): bare "in"/"out" (implicit
     * DX-addressed AL access, EC/EE) and single-operand "in /C2" /
     * "out /D6" (immediate 8-bit port, E4/E6 - always byte-sized;
     * AL is implicit, no word-port forms seen in the corpus). */
    if (mnemonic->len == 2 && strncmp(mnemonic->text, "in", 2) == 0) {
        if (nops == 0) { codebuf_put(out, 0xEC); return true; }
        if (nops == 1 && (ops[0].mode == ADDR_DIRECT || ops[0].mode == ADDR_IMMEDIATE) && ops[0].expr) {
            long port = resolve_expr(ops[0].expr, cur_addr, st, resolve);
            codebuf_put(out, 0xE4);
            codebuf_put(out, (unsigned char)(port & 0xFF));
            return true;
        }
        return false;
    }
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "out", 3) == 0) {
        if (nops == 0) { codebuf_put(out, 0xEE); return true; }
        if (nops == 1 && (ops[0].mode == ADDR_DIRECT || ops[0].mode == ADDR_IMMEDIATE) && ops[0].expr) {
            long port = resolve_expr(ops[0].expr, cur_addr, st, resolve);
            codebuf_put(out, 0xE6);
            codebuf_put(out, (unsigned char)(port & 0xFF));
            return true;
        }
        return false;
    }
    /* "inb"/"outb" - Anlage A's documented explicit-byte spelling
     * ("inb 2 AL * P+B" / "inb 2 AL DX"). Real confirmed usage is bare
     * "in"/"out" for byte (implicit AL) and "inw"/"outw" for word
     * (implicit AX) - the OPPOSITE of what Anlage A's table literally
     * says (there, unsuffixed "in"/"out" is the WORD/AX form and "inb"
     * is the byte form) - the same kind of real-toolchain-vs-manual
     * naming divergence already confirmed for STOS/LODS (real: stow/
     * stob/lodw/lodb) and the blos/bloss branch mnemonic. Accepted
     * here as harmless synonyms for the already-confirmed byte form
     * (identical E4/EC opcodes to bare "in"/"out") in case the real
     * assembler also tolerates the documented spelling - UNCONFIRMED
     * by any real sample either way. */
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "inb", 3) == 0) {
        if (nops == 0) { codebuf_put(out, 0xEC); return true; }
        if (nops == 1 && (ops[0].mode == ADDR_DIRECT || ops[0].mode == ADDR_IMMEDIATE) && ops[0].expr) {
            long port = resolve_expr(ops[0].expr, cur_addr, st, resolve);
            codebuf_put(out, 0xE4);
            codebuf_put(out, (unsigned char)(port & 0xFF));
            return true;
        }
        return false;
    }
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "outb", 4) == 0) {
        if (nops == 0) { codebuf_put(out, 0xEE); return true; }
        if (nops == 1 && (ops[0].mode == ADDR_DIRECT || ops[0].mode == ADDR_IMMEDIATE) && ops[0].expr) {
            long port = resolve_expr(ops[0].expr, cur_addr, st, resolve);
            codebuf_put(out, 0xE6);
            codebuf_put(out, (unsigned char)(port & 0xFF));
            return true;
        }
        return false;
    }
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "int", 3) == 0 && nops == 1 && ops[0].expr) {
        long v = resolve_expr(ops[0].expr, cur_addr, st, resolve);
        codebuf_put(out, 0xCD);
        codebuf_put(out, (unsigned char)(v & 0xFF));
        return true;
    }

    {
        const PseudoBranch *pb = pseudo_branch_lookup(mnemonic);
        if (pb && nops == 1)
            return encode_pseudo_branch(out, pb, &ops[0], cur_addr, st, resolve, relocs);
    }

    if (mnemonic->len == 3 && strncmp(mnemonic->text, "mov", 3) == 0 && nops == 2)
        return encode_mov(out, &ops[0], &ops[1], cur_addr, st, resolve, false, relocs);
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "movb", 4) == 0 && nops == 2)
        return encode_mov(out, &ops[0], &ops[1], cur_addr, st, resolve, true, relocs);

    {
        bool byte_mode = false;
        int opidx = group1_lookup(mnemonic, &byte_mode);
        if (opidx >= 0 && nops == 2)
            return encode_group1(out, opidx, &ops[0], &ops[1], cur_addr, st, resolve, byte_mode, relocs);
    }

    if (mnemonic->len == 3 && strncmp(mnemonic->text, "lea", 3) == 0 && nops == 2)
        return encode_lea(out, &ops[0], &ops[1], cur_addr, st, resolve, relocs);
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "lds", 3) == 0 && nops == 2)
        return encode_ldx(out, 0xC5, &ops[0], &ops[1], cur_addr, st, resolve, relocs);
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "les", 3) == 0 && nops == 2)
        return encode_ldx(out, 0xC4, &ops[0], &ops[1], cur_addr, st, resolve, relocs);

    if (mnemonic->len == 3 && strncmp(mnemonic->text, "mul", 3) == 0 && nops == 1)
        return encode_group3(out, 4, &ops[0], cur_addr, st, resolve, relocs, false);
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "mulb", 4) == 0 && nops == 1)
        return encode_group3(out, 4, &ops[0], cur_addr, st, resolve, relocs, true);
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "neg", 3) == 0 && nops == 1)
        return encode_group3(out, 3, &ops[0], cur_addr, st, resolve, relocs, false);
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "negb", 4) == 0 && nops == 1)
        return encode_group3(out, 3, &ops[0], cur_addr, st, resolve, relocs, true);
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "imul", 4) == 0 && nops == 1)
        return encode_group3(out, 5, &ops[0], cur_addr, st, resolve, relocs, false);
    if (mnemonic->len == 5 && strncmp(mnemonic->text, "imulb", 5) == 0 && nops == 1)
        return encode_group3(out, 5, &ops[0], cur_addr, st, resolve, relocs, true);
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "div", 3) == 0 && nops == 1)
        return encode_group3(out, 6, &ops[0], cur_addr, st, resolve, relocs, false);
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "divb", 4) == 0 && nops == 1)
        return encode_group3(out, 6, &ops[0], cur_addr, st, resolve, relocs, true);
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "idiv", 4) == 0 && nops == 1)
        return encode_group3(out, 7, &ops[0], cur_addr, st, resolve, relocs, false);
    if (mnemonic->len == 5 && strncmp(mnemonic->text, "idivb", 5) == 0 && nops == 1)
        return encode_group3(out, 7, &ops[0], cur_addr, st, resolve, relocs, true);
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "not", 3) == 0 && nops == 1)
        return encode_group3(out, 2, &ops[0], cur_addr, st, resolve, relocs, false);
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "notb", 4) == 0 && nops == 1)
        return encode_group3(out, 2, &ops[0], cur_addr, st, resolve, relocs, true);

    if (mnemonic->len == 4 && strncmp(mnemonic->text, "incb", 4) == 0 && nops == 1) {
        codebuf_put(out, 0xFE);
        if (ops[0].mode == ADDR_INDIRECT) { emit_modrm_indirect(out, 0, &ops[0], cur_addr, st, resolve, relocs); return true; }
        if (ops[0].mode == ADDR_DIRECT) { emit_modrm_direct(out, 0, &ops[0], cur_addr, st, resolve, relocs); return true; }
        return false;
    }
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "decb", 4) == 0 && nops == 1) {
        codebuf_put(out, 0xFE);
        if (ops[0].mode == ADDR_INDIRECT) { emit_modrm_indirect(out, 1, &ops[0], cur_addr, st, resolve, relocs); return true; }
        if (ops[0].mode == ADDR_DIRECT) { emit_modrm_direct(out, 1, &ops[0], cur_addr, st, resolve, relocs); return true; }
        return false;
    }

    /* Explicit-width string-move mnemonics - "movsb" confirmed real
     * (0xA4); "movsw" would be the word twin of the default bare
     * "movs" (0xA5) handled below, not directly observed but included
     * for symmetry. */
    if (mnemonic->len == 5 && strncmp(mnemonic->text, "movsb", 5) == 0 && nops == 0) {
        codebuf_put(out, 0xA4);
        return true;
    }
    if (mnemonic->len == 5 && strncmp(mnemonic->text, "movsw", 5) == 0 && nops == 0) {
        codebuf_put(out, 0xA5);
        return true;
    }

    /* Word IN/OUT - confirmed real bare "inw"/"outw" (implicit AX,DX,
     * mirroring bare "in"/"out"'s implicit AL,DX). */
    if (mnemonic->len == 3 && strncmp(mnemonic->text, "inw", 3) == 0 && nops == 0) {
        codebuf_put(out, 0xED);
        return true;
    }
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "outw", 4) == 0 && nops == 0) {
        codebuf_put(out, 0xEF);
        return true;
    }

    /* INSB/INSW/OUTSB/OUTSW - 80186 block-I/O string instructions
     * (port DX <-> ES:[DI] for INS*, DS:[SI] <-> port DX for OUTS*,
     * DI/SI auto-advanced per the Direction Flag, combinable with
     * "rep" exactly like MOVS/STOS/CMPS/SCAS). Unlike the plain IN/
     * OUT family (where this toolchain's real bare "in"/"out" is byte
     * and "inw"/"outw" is word - the opposite of Anlage A's literal
     * wording), these follow the standard, unambiguous Intel B/W
     * suffix convention with no divergence: mch.s's real hardware
     * source only ever reaches these opcodes via raw ".byte /6d" /
     * ".byte /6f" (0x6D/0x6F, i.e. the WORD forms, matching the
     * preceding "seg es" prefix and surrounding word-sized PIC-mask
     * idiom) since this dispatch table previously had no mnemonic
     * entry for them at all - confirmed via kernel_opt/
     * mch_insw_outsw.s (mch.s with those two ".byte" lines swapped
     * for plain "insw"/"outsw") failing to reproduce mch.o.golden
     * byte-for-byte before this fix: with no mnemonic match here,
     * encode_instruction() returned false, and since the statement
     * has zero operands, assemble.c's bare-identifier fallback
     * mis-fired, treating "insw"/"outsw" as if they were pointer-
     * table entries (an implicit ".word <ident>") - emitting a wrong
     * 2-byte external-symbol placeholder + a spurious relocation
     * entry instead of the correct single opcode byte, which also
     * desynced the location counter for every following instruction.
     * INSB/OUTSB (0x6C/0x6E) are added alongside for completeness/
     * symmetry, though only INSW/OUTSW are exercised by the real
     * corpus so far. */
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "insb", 4) == 0 && nops == 0) {
        codebuf_put(out, 0x6C);
        return true;
    }
    if (mnemonic->len == 4 && strncmp(mnemonic->text, "insw", 4) == 0 && nops == 0) {
        codebuf_put(out, 0x6D);
        return true;
    }
    if (mnemonic->len == 5 && strncmp(mnemonic->text, "outsb", 5) == 0 && nops == 0) {
        codebuf_put(out, 0x6E);
        return true;
    }
    if (mnemonic->len == 5 && strncmp(mnemonic->text, "outsw", 5) == 0 && nops == 0) {
        codebuf_put(out, 0x6F);
        return true;
    }

    /* Single-byte, no-operand instructions - just a lookup table.
     * "stob" (confirmed real usage in mch.s, after "rep": stores AL to
     * ES:DI and advances DI - STOSB abbreviated) is included here;
     * its symmetric siblings (stow/lodb/lodw/etc.) are NOT included,
     * since none have been directly observed in the corpus yet - see
     * the Step 5 memory note.
     *
     * NOTE: this table moved to FILE SCOPE (as NOARG_TABLE, just above
     * encode_instruction) so encode_is_known_mnemonic() below can walk
     * the exact same array instead of a separately hand-typed name
     * list that could silently drift out of sync with it. */
    if (nops == 0) {
        for (size_t i = 0; i < sizeof(NOARG_TABLE)/sizeof(NOARG_TABLE[0]); i++)
            if (strlen(NOARG_TABLE[i].name) == mnemonic->len &&
                strncmp(NOARG_TABLE[i].name, mnemonic->text, mnemonic->len) == 0) {
                codebuf_put(out, NOARG_TABLE[i].op);
                return true;
            }

        /* AAM/AAD - "ascii adjust for multiply/division" - the only
         * two Anlage A zero-OPERAND-list instructions that still emit
         * a second byte: a fixed 0x0A (base-10) immediate divisor/
         * multiplier, per the standard 8086 encoding (D4 0A / D5 0A).
         * UNCONFIRMED by any real sample. */
        if (mnemonic->len == 3 && strncmp(mnemonic->text, "aam", 3) == 0) {
            codebuf_put(out, 0xD4); codebuf_put(out, 0x0A); return true;
        }
        if (mnemonic->len == 3 && strncmp(mnemonic->text, "aad", 3) == 0) {
            codebuf_put(out, 0xD5); codebuf_put(out, 0x0A); return true;
        }
    }

    return false; /* mnemonic/operand combination not (yet) supported */
}

/* ------------------------------------------------------------------ */
/* Known-mnemonic membership check                                      */
/* ------------------------------------------------------------------ */

/* Returns true if `mnemonic` names ANY instruction this assembler
 * recognizes AT ALL, regardless of whether the specific operand count
 * or addressing-mode shape used on a given statement is one
 * encode_instruction() actually accepts.
 *
 * WHY THIS EXISTS: before this function, assemble.c's STMT_INSTRUCTION
 * handling treated ANY statement where encode_instruction() failed
 * AND the statement had zero operands as a bare-symbol data value
 * (the real, CONFIRMED tty.s idiom for jump/pointer-table entries,
 * e.g. a lone "_t0" or "L10003" on its own line - see that function's
 * comment). That fallback cannot tell "a genuine label/symbol name"
 * apart from "a real mnemonic the user meant to use, but which either
 * isn't implemented yet or was given an unsupported operand shape" -
 * both look identical at that point (a bare identifier, no operands).
 * CONFIRMED REAL BUG this closes: "insw"/"outsw" (see
 * kernel_opt/mch_insw_outsw.s) previously had NO dispatch entry at
 * all, so they silently fell into the bare-symbol path and were
 * mis-assembled as a 2-byte external-symbol placeholder + a spurious
 * relocation entry instead of the correct 1-byte opcode - wrong
 * bytes AND a desynced location counter for everything downstream,
 * with zero diagnostic. Fixed for that specific pair by adding a real
 * dispatch entry (see above), but the SAME failure mode remains open
 * for any OTHER mnemonic-shaped token the fallback shouldn't have
 * claimed - e.g. a real mnemonic used with the wrong number of
 * operands (a plain typo), or any future gap like the one insw/outsw
 * just were. assemble.c now calls this function to gate the fallback:
 * a bare, zero-operand, unencoded statement is only ever treated as
 * an implicit data value when the leading token does NOT match a
 * known mnemonic name; otherwise it is a hard assembly error, per
 * encode_instruction()'s own documented contract in encode.h ("the
 * caller should treat [false] as a hard error, not silently skip").
 *
 * Deliberately built by walking the SAME per-family tables
 * (GROUP1/GROUP1B/GROUP2/GROUP2B/REAL_JCC/LOOP_OPS/PSEUDO_BRANCHES/
 * NOARG_TABLE) encode_instruction() itself dispatches through, so a
 * new entry added to any of those tables is automatically covered
 * here with no second edit needed. Only the "irregular" mnemonics -
 * ones encode_instruction() matches via a direct strncmp rather than
 * a shared table (mov, push, the I/O family, string ops, etc.) - are
 * listed explicitly in IRREGULAR_MNEMONICS[] below; adding a new
 * irregular dispatch entry to encode_instruction() must also add its
 * name there, or it silently re-opens this exact bug for itself. This
 * whole function, and its one-name-list-per-new-irregular-mnemonic
 * upkeep cost, is intentionally right next to encode_instruction() so
 * it stays visible whenever that function is edited. */
static const char *const IRREGULAR_MNEMONICS[] = {
    "j", "push", "pop", "jmp", "br", "call", "calli", "jmpi",
    "mov", "movb", "lea", "lds", "les",
    "test", "testb", "inc", "dec", "incb", "decb", "xchg", "xchgb",
    "movs", "movsb", "movsw",
    "in", "out", "inb", "outb", "inw", "outw", "insb", "insw", "outsb", "outsw",
    "int", "seg",
    "rep", "repe", "repz", "repne", "repnz",
    "aam", "aad",
};

bool encode_is_known_mnemonic(const Token *mnemonic)
{
    for (size_t i = 0; i < sizeof(IRREGULAR_MNEMONICS)/sizeof(IRREGULAR_MNEMONICS[0]); i++)
        if (strlen(IRREGULAR_MNEMONICS[i]) == mnemonic->len &&
            strncmp(IRREGULAR_MNEMONICS[i], mnemonic->text, mnemonic->len) == 0)
            return true;

    for (size_t i = 0; i < sizeof(GROUP1)/sizeof(GROUP1[0]); i++)
        if (strlen(GROUP1[i].name) == mnemonic->len && strncmp(GROUP1[i].name, mnemonic->text, mnemonic->len) == 0)
            return true;
    for (size_t i = 0; i < sizeof(GROUP1B)/sizeof(GROUP1B[0]); i++)
        if (strlen(GROUP1B[i].name) == mnemonic->len && strncmp(GROUP1B[i].name, mnemonic->text, mnemonic->len) == 0)
            return true;
    for (size_t i = 0; i < sizeof(GROUP2)/sizeof(GROUP2[0]); i++)
        if (strlen(GROUP2[i].name) == mnemonic->len && strncmp(GROUP2[i].name, mnemonic->text, mnemonic->len) == 0)
            return true;
    for (size_t i = 0; i < sizeof(GROUP2B)/sizeof(GROUP2B[0]); i++)
        if (strlen(GROUP2B[i].name) == mnemonic->len && strncmp(GROUP2B[i].name, mnemonic->text, mnemonic->len) == 0)
            return true;
    for (size_t i = 0; i < sizeof(REAL_JCC)/sizeof(REAL_JCC[0]); i++)
        if (strlen(REAL_JCC[i].name) == mnemonic->len && strncmp(REAL_JCC[i].name, mnemonic->text, mnemonic->len) == 0)
            return true;
    for (size_t i = 0; i < sizeof(LOOP_OPS)/sizeof(LOOP_OPS[0]); i++)
        if (strlen(LOOP_OPS[i].name) == mnemonic->len && strncmp(LOOP_OPS[i].name, mnemonic->text, mnemonic->len) == 0)
            return true;
    for (size_t i = 0; i < sizeof(PSEUDO_BRANCHES)/sizeof(PSEUDO_BRANCHES[0]); i++)
        if (strlen(PSEUDO_BRANCHES[i].name) == mnemonic->len && strncmp(PSEUDO_BRANCHES[i].name, mnemonic->text, mnemonic->len) == 0)
            return true;
    for (size_t i = 0; i < sizeof(NOARG_TABLE)/sizeof(NOARG_TABLE[0]); i++)
        if (strlen(NOARG_TABLE[i].name) == mnemonic->len && strncmp(NOARG_TABLE[i].name, mnemonic->text, mnemonic->len) == 0)
            return true;

    return false;
}
