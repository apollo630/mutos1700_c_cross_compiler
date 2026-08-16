/*
 * encode.h - per-mnemonic opcode/addressing-mode encoder (Step 3).
 *
 * Turns a mnemonic + its classified operands (see pass2.h) into actual
 * 8086 machine code bytes, using only the encodings confirmed against
 * real hardware-linked object code (see the consolidated Milestone 2
 * memory notes): standard ModRM, group1 0x83/0x81 immediates, mov's
 * A0-A3/B8-BF/C6/C7 forms, E9/EB/E8 jumps and calls, short Jcc, and the
 * fixed "negated-Jcc + long jmp" expansion mutos_as/c1 always emits for
 * pseudo-branches (beq/bne/blt/bge/ble/bgt/bhi/blos) - confirmed to be
 * unconditional, not just for out-of-range targets.
 *
 * Scope: this covers exactly the mnemonics needed to fully assemble
 * malloc.s (the smallest fully-verified corpus file) and validate the
 * result byte-for-byte against the real malloc.o. It is intentionally
 * not a complete 8086 instruction set yet - see encode.c's mnemonic
 * table for what is currently supported; extending it is mechanical.
 */

#ifndef MUTOS_AS_ENCODE_H
#define MUTOS_AS_ENCODE_H

#include "pass2.h"
#include "symtab.h"

typedef struct {
    unsigned char *data;
    size_t         len;
    size_t         cap;
} CodeBuf;

void codebuf_init(CodeBuf *cb);
void codebuf_free(CodeBuf *cb);
void codebuf_put(CodeBuf *cb, unsigned char b);
void codebuf_put16le(CodeBuf *cb, unsigned int w);

typedef struct {
    long          text_offset; /* byte offset of the 2-byte field within
                                 * the segment this relocation applies to */
    int           symidx;      /* index into the output symbol table -
                                 * meaningful only for R_EXT (see below) */
    unsigned char low4;        /* low 4 bits of the real reloc word: bit3
                                 * (0x08)=R_EXT, bit0 (0x01)=PC-relative -
                                 * see the consolidated Milestone 2
                                 * relocation notes */
} RelocEntry;

typedef struct {
    RelocEntry *entries;
    size_t      n, cap;
} RelocList;

void reloclist_init(RelocList *rl);
void reloclist_free(RelocList *rl);
void reloclist_add(RelocList *rl, long text_offset, int symidx, unsigned char low4);

/* Encodes one instruction's machine code into `out` (appended).
 *
 * `resolve` selects the pass:
 *   - false (Pass 1): sizing only. Symbol values are NOT required to
 *     be final yet; any expression that needs a symbol/label value
 *     (jump targets, direct addresses, absolute constants that aren't
 *     already a literal number) is encoded as a zero placeholder of
 *     the correct width. This still produces the exact correct BYTE
 *     COUNT, because every form used here has a length that depends
 *     only on the operand's syntactic shape (register/indirect/
 *     immediate + its size marker), never on the resolved value -
 *     matching the confirmed "no short/long jump auto-selection" rule.
 *   - true (Pass 2): full resolution. Every symbol must already be in
 *     `st` with a final value (SYM_TEXT/SYM_DATA values are absolute
 *     byte offsets into their segment - see the consolidated a.out
 *     notes: MUTOS values are pure segment-local offsets, not V7's
 *     "baked-in preceding segment size" convention). `cur_addr` is
 *     this instruction's own address (needed for PC-relative jumps).
 *
 * Returns true on success (bytes appended to `out`), false if the
 * mnemonic/operand combination is not (yet) supported - the caller
 * should treat that as a hard error, not silently skip.
 *
 * `relocs` (nullable) collects a RelocEntry whenever, during Pass 2
 * (resolve=true), an instruction encodes a reference to a symbol that
 * is still undefined (external) at this point - e.g. "jmp cret" - so
 * the caller can later emit a real trel/drel-style relocation table
 * instead of just leaving the zero placeholder in the byte stream.
 */
bool encode_instruction(CodeBuf *out, const Token *mnemonic,
                         const ParsedOperand *ops, int nops,
                         long cur_addr, SymTab *st, bool resolve,
                         RelocList *relocs);

/* Exposes the internal expression resolver for callers that need to
 * evaluate a plain expression outside of instruction encoding (e.g.
 * the assembler driver's handling of ".=.+4" location-counter
 * assignments and STMT_DATA_VALUE words). Always fully resolves
 * (equivalent to encode_instruction's resolve=true path). */
long resolve_expr_pub(const ExprNode *e, long cur_addr, SymTab *st);

/* Same as resolve_expr_pub, but with resolve=false: does NOT trust any
 * symbol's value (always returns a placeholder for symbol-dependent
 * subexpressions) but still REGISTERS every referenced symbol in `st`
 * via find_or_create. Needed for Pass 1 processing of directive
 * operands (.word/.byte/.comm-size) that reference external symbols -
 * without this, a symbol referenced ONLY from such an operand (e.g.
 * an interrupt-vector-table ".word _handler" entry) would never be
 * created until Pass 2, by which point symtab_finalize() has already
 * run and it gets no valid out_index - confirmed real bug via mch.s's
 * ivectab (11 such external handler symbols, all initially crashing
 * the object-file writer with an out-of-bounds symbol-table write). */
long register_expr_symbols(const ExprNode *e, SymTab *st);

/* Exposes the internal non-PC-relative word-relocation classifier (see
 * encode.c's classify_word_reloc) for callers outside instruction
 * encoding - specifically assemble.c's .word/.byte directive and
 * STMT_DATA_VALUE handling, which can equally hold a symbol's absolute
 * address (e.g. mch.s's ivectab: ".word _ndpint") and need the exact
 * same R_TEXT/R_DATA/R_EXT classification and relocation-entry
 * bookkeeping as any in-instruction word field. Returns true and fills
 * *low4_out / *symidx_out if a relocation entry is needed; false if `e`
 * is a pure numeric literal or a local ABS symbol needing none. */
bool classify_word_reloc_pub(const ExprNode *e, SymTab *st, bool in_text,
                              unsigned char *low4_out, int *symidx_out);

#endif /* MUTOS_AS_ENCODE_H */
