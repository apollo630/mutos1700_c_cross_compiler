/*
 * assemble.c - Step 3 two-pass assembler driver.
 *
 * Pass 1: walks all statements, tracking a location counter per
 * segment (.text / .data), defining every label's symbol at its
 * current address, and computing each instruction's byte size (via
 * encode_instruction() in sizing mode - see encode.h for why that is
 * safe as a single pass: encoded length never depends on a resolved
 * symbol value here, only on the operand's syntactic shape).
 *
 * Pass 2: walks all statements again with a now-complete symbol table,
 * actually encoding instructions into the text/data output buffers.
 *
 * This driver intentionally covers only what malloc.s needs (see
 * encode.c's mnemonic table) - it exists to prove the Step 3 encoding
 * tables are correct by producing a byte-for-byte match against the
 * real hardware-linked malloc.o, not to be a complete assembler yet.
 */

/* Needed for fileno()/dup()/dup2()/close()/chmod() under strict
 * -std=c11 (these are POSIX, not pure C11 - glibc hides their
 * prototypes otherwise). Must come before any system header include. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "mutos_as.h"
#include "pass2.h"
#include "encode.h"
#include "symtab.h"
#include "objwrite.h"

typedef enum { SEG_TEXT, SEG_DATA, SEG_BSS } Segment;

typedef struct {
    SymTab   st;
    long     text_loc, data_loc, bss_loc;
    Segment  seg;
    int      errors;
} AsmState;

/* Selects the right location-counter pointer for the current segment.
 * BSS gets its own counter (bss_loc) - see AsmState. */
static long *seg_lc(AsmState *as)
{
    switch (as->seg) {
        case SEG_TEXT: return &as->text_loc;
        case SEG_DATA: return &as->data_loc;
        case SEG_BSS:  return &as->bss_loc;
    }
    return &as->text_loc;
}

/* Selects the output CodeBuf for the current segment, or NULL for BSS
 * - a ".bss" segment reserves space (its size becomes the object
 * file's a_bss header field) but is implicitly all-zero and has NO
 * byte stream of its own to write into, unlike .text/.data. */
static CodeBuf *seg_target(AsmState *as, CodeBuf *text_out, CodeBuf *data_out)
{
    switch (as->seg) {
        case SEG_TEXT: return text_out;
        case SEG_DATA: return data_out;
        case SEG_BSS:  return NULL;
    }
    return NULL;
}

/* Selects the relocation list for the current segment, or NULL for
 * BSS - a zero-filled reservation can never itself need relocating. */
static RelocList *seg_relocs(AsmState *as, RelocList *text_relocs, RelocList *data_relocs)
{
    switch (as->seg) {
        case SEG_TEXT: return text_relocs;
        case SEG_DATA: return data_relocs;
        case SEG_BSS:  return NULL;
    }
    return NULL;
}

static void define_label(AsmState *as, const Token *label)
{
    Symbol *s = symtab_find_or_create(&as->st, label->text, label->len);
    if (s->defined) {
        fprintf(stderr, "error: redefinition of symbol '%.*s'\n", (int)label->len, label->text);
        as->errors++;
        return;
    }
    switch (as->seg) {
        case SEG_TEXT: s->type = SYM_TEXT; s->value = as->text_loc; break;
        case SEG_DATA: s->type = SYM_DATA; s->value = as->data_loc; break;
        case SEG_BSS:  s->type = SYM_LOCALBSS; s->value = as->bss_loc; break;
    }
    s->defined = true;
}

/* Parses a raw (unclassified) operand as a plain expression - used for
 * directive arguments (.comm's size, .word/.byte data values, etc.),
 * which are not addressing modes and so don't need classify_operand()'s
 * register/indirect detection. A leading '*'/'#' size marker (seen in
 * real mch.s .byte lists) is skipped first, same as classify_operand()
 * does for instruction operands - for a .byte/.word directive the
 * marker is redundant (the directive itself already states the size)
 * and carries no separate meaning. */
static ExprNode *parse_raw_operand_expr(const Operand *op)
{
    int idx = 0;
    if (op->ntokens > 0 && (op->tokens[0].type == TOK_STAR || op->tokens[0].type == TOK_HASH))
        idx = 1;
    return expr_parse(op->tokens, op->ntokens, &idx);
}

static void handle_directive(AsmState *as, const Statement *s)
{
    const Token *d = &s->mnemonic_or_name;

    if (d->len == 6 && strncmp(d->text, ".globl", 6) == 0) {
        for (int i = 0; i < s->noperands; i++) {
            if (s->operands[i].ntokens != 1 || s->operands[i].tokens[0].type != TOK_IDENT)
                continue;
            const Token *nm = &s->operands[i].tokens[0];
            Symbol *sym = symtab_find_or_create(&as->st, nm->text, nm->len);
            sym->is_extern = true;
        }
        return;
    }

    if (d->len == 5 && strncmp(d->text, ".comm", 5) == 0) {
        if (s->noperands != 2 || s->operands[0].ntokens != 1 ||
            s->operands[0].tokens[0].type != TOK_IDENT) {
            fprintf(stderr, "error: malformed .comm at line %d\n", s->line);
            as->errors++;
            return;
        }
        const Token *nm = &s->operands[0].tokens[0];
        ExprNode *sizeexpr = parse_raw_operand_expr(&s->operands[1]);
        long size = 0;
        if (!sizeexpr || !expr_try_eval_const(sizeexpr, &size)) {
            fprintf(stderr, "error: non-constant .comm size at line %d\n", s->line);
            as->errors++;
        }
        expr_free(sizeexpr);

        Symbol *sym = symtab_find_or_create(&as->st, nm->text, nm->len);
        if (!sym->defined) {
            sym->type = SYM_BSS;
            sym->value = size; /* BSS "value" is the requested SIZE, not
                                 * an address - see symtab.h */
            sym->is_extern = true;
            sym->defined = true; /* defined-as-a-common, not a hard error
                                   * if referenced; real linker still
                                   * merges multiple .comm decls for the
                                   * same name by taking the max size -
                                   * not implemented here, single-module
                                   * scope only */
        } else if (sym->value < size) {
            sym->value = size;
        }
        return;
    }

    if (d->len == 5 && strncmp(d->text, ".text", 5) == 0) { as->seg = SEG_TEXT; return; }
    if (d->len == 5 && strncmp(d->text, ".data", 5) == 0) { as->seg = SEG_DATA; return; }
    if (d->len == 4 && strncmp(d->text, ".bss", 4) == 0) { as->seg = SEG_BSS; return; }

    /* .even is handled by the caller (run_pass) now, not here - see
     * the note below. Left as a no-op fallthrough for safety. */

    /* .word/.byte handled by the caller (run_pass), since emitting
     * their actual bytes needs access to the output CodeBuf, which
     * this function doesn't have - see run_pass()'s STMT_DIRECTIVE
     * case. Nothing to do here for them. */
    if ((d->len == 5 && strncmp(d->text, ".word", 5) == 0) ||
        (d->len == 5 && strncmp(d->text, ".byte", 5) == 0))
        return;

    /* .asciz / .end etc.: not needed by the current corpus subset this
     * driver targets - silently ignored so it degrades gracefully
     * rather than erroring on files outside its scope. */
}

static void handle_assignment(AsmState *as, const Statement *s, CodeBuf *text_out, CodeBuf *data_out, bool pass2)
{
    long *lc = seg_lc(as);

    if (s->mnemonic_or_name.type == TOK_DOT) {
        /* Location-counter assignment, e.g. ".=.+4" (mch.s crt0). The
         * RHS may reference '.' itself, meaning "current LC" at the
         * time of evaluation. This RESERVES storage (typically used
         * like "name:.=.+4" to carve out a fixed-size field without a
         * .word/.byte directive) - in Pass 2 the reserved bytes must
         * actually be written (as zero fill) into the output buffer,
         * not just have the location counter jump forward, or the
         * buffer silently falls behind by the reserved amount. Found
         * via a size mismatch: mch.s's "monssvec:.=.+4" / "mon3vec:.=
         * .+4" (2 x 4 bytes) accounted for the exact remaining gap
         * between Pass 1's correct data_loc and Pass 2's actual
         * data_out length. Not applicable in .bss (seg_target()
         * returns NULL there - nothing to zero-fill, the LC bump
         * alone is the entire effect, exactly like .blkb). */
        ExprNode *e = parse_raw_operand_expr(&s->operands[0]);
        long v = 0;
        if (e) v = resolve_expr_pub(e, *lc, &as->st);
        expr_free(e);
        long old = *lc;
        if (pass2 && v > old) {
            CodeBuf *target = seg_target(as, text_out, data_out);
            if (target)
                for (long i = old; i < v; i++)
                    codebuf_put(target, 0x00);
        }
        *lc = v;
        return;
    }

    /* Plain "name = expr" - not exercised by malloc.s; define as an
     * absolute symbol if the RHS is already constant-foldable. */
    ExprNode *e = parse_raw_operand_expr(&s->operands[0]);
    long v = 0;
    bool ok = e && expr_try_eval_const(e, &v);
    expr_free(e);
    if (ok) {
        Symbol *sym = symtab_find_or_create(&as->st, s->mnemonic_or_name.text, s->mnemonic_or_name.len);
        sym->type = SYM_ABS;
        sym->value = v;
        sym->defined = true;
    }
}

/* resolve_expr_pub() comes from encode.h. */

static bool classify_all(const Statement *s, ParsedOperand out[MAX_OPERANDS])
{
    for (int i = 0; i < s->noperands; i++) {
        out[i] = classify_operand(&s->operands[i]);
        if (!out[i].ok) return false;
    }
    return true;
}

static void free_all(ParsedOperand out[MAX_OPERANDS], int n)
{
    for (int i = 0; i < n; i++)
        parsed_operand_free(&out[i]);
}

/* One shared walk over every statement in the file, used for both
 * passes; `pass2` selects sizing-only (false) vs full encoding (true). */
static void run_pass(AsmState *as, const char *src, size_t len, const char *filename,
                      CodeBuf *text_out, CodeBuf *data_out, bool pass2,
                      RelocList *text_relocs, RelocList *data_relocs)
{
    Parser p;
    parser_init(&p, src, len, filename);
    as->text_loc = 0;
    as->data_loc = 0;
    as->bss_loc = 0;
    as->seg = SEG_TEXT;

    Statement s;
    while (parser_next_statement(&p, &s)) {
        long *lc = seg_lc(as);

        if (s.kind == STMT_EMPTY)
            continue;

        if (!pass2) {
            /* Pass 1: define every label attached to this statement at
             * the CURRENT address (before this statement's own bytes,
             * matching "label: instruction" semantics). */
            for (int i = 0; i < s.nlabels; i++)
                define_label(as, &s.labels[i]);
        }

        switch (s.kind) {
            case STMT_LABEL_ONLY:
                break;

            case STMT_DIRECTIVE: {
                /* The a.out header comment (mutos_ld.c / ld.c) is
                 * explicit: "text size ... in bytes but even". Real
                 * malloc.o confirms this with an actual trailing 0x00
                 * pad byte where an odd-length .text is immediately
                 * followed by ".data" - pad here, in BOTH passes (Pass
                 * 1 to get the size right, Pass 2 to actually emit the
                 * byte), before the segment switch itself. */
                const Token *d = &s.mnemonic_or_name;
                if (d->len == 5 && strncmp(d->text, ".data", 5) == 0 &&
                    as->seg == SEG_TEXT && (as->text_loc & 1)) {
                    if (pass2 && text_out) codebuf_put(text_out, 0x00);
                    as->text_loc++;
                }
                /* Symmetric case: switching FROM .data back TO .text
                 * with an odd data_loc - confirmed real via mch.s:
                 * "_szicode: . - _icode" (a STMT_DATA_VALUE word,
                 * ending the .data content at an odd offset, 581)
                 * followed by ".globl _icodech" / ".text" - the real
                 * mch.o's data segment is 582 bytes (even), one 0x00
                 * pad byte past _szicode's word. */
                if (d->len == 5 && strncmp(d->text, ".text", 5) == 0 &&
                    as->seg == SEG_DATA && (as->data_loc & 1)) {
                    if (pass2 && data_out) codebuf_put(data_out, 0x00);
                    as->data_loc++;
                }

                /* .word <expr>[,<expr>...] / .byte <expr>[,<expr>...] -
                 * emit raw data into whichever segment is current. */
                if (d->len == 5 && (strncmp(d->text, ".word", 5) == 0 ||
                                     strncmp(d->text, ".byte", 5) == 0)) {
                    bool is_word = (d->text[1] == 'w');
                    CodeBuf *target = pass2 ? seg_target(as, text_out, data_out) : NULL;
                    RelocList *rl = pass2 ? seg_relocs(as, text_relocs, data_relocs) : NULL;
                    for (int i = 0; i < s.noperands; i++) {
                        ExprNode *e = parse_raw_operand_expr(&s.operands[i]);
                        long v;
                        if (pass2) {
                            v = e ? resolve_expr_pub(e, *lc, &as->st) : 0;
                        } else {
                            v = 0;
                            if (e) register_expr_symbols(e, &as->st);
                        }
                        if (pass2 && target) {
                            /* A ".word" value can hold a symbol's
                             * absolute address (CONFIRMED real:
                             * mch.s's ivectab, e.g. ".word _ndpint",
                             * needs an R_EXT relocation entry per
                             * entry) - same non-PC-relative
                             * classification as any in-instruction
                             * word field. Not applicable to ".byte"
                             * (no word-sized field to relocate). */
                            if (is_word && rl && e) {
                                unsigned char low4; int symidx;
                                if (classify_word_reloc_pub(e, &as->st, as->seg == SEG_TEXT, &low4, &symidx))
                                    reloclist_add(rl, (long)target->len, symidx, low4);
                            }
                            if (is_word) codebuf_put16le(target, (unsigned int)(v & 0xFFFF));
                            else codebuf_put(target, (unsigned char)(v & 0xFF));
                        }
                        expr_free(e);
                        *lc += is_word ? 2 : 1;
                    }
                    break;
                }

                /* .blkb <expr> - reserves <expr> zero-filled bytes in
                 * the CURRENT segment (only meaningful in .bss in the
                 * real corpus seen so far - CONFIRMED via v30opt.s
                 * ".bss" / "_identDe:.blkb 512." etc.). Unlike .word/
                 * .byte, this writes NO actual bytes anywhere (seg_
                 * target() is NULL for .bss) - the reservation's whole
                 * effect is the location-counter advance, which is
                 * exactly what makes it become the object file's
                 * a_bss header field once accumulated across the
                 * whole .bss section (see main()'s objwrite_write()
                 * call). If ever used inside .text/.data instead, it
                 * degrades to the same zero-fill-via-explicit-bytes
                 * behavior as ".=.+N" for consistency, though no real
                 * sample of that combination exists in the corpus. */
                if (d->len == 5 && strncmp(d->text, ".blkb", 5) == 0) {
                    ExprNode *e = s.noperands >= 1 ? parse_raw_operand_expr(&s.operands[0]) : NULL;
                    long n = 0;
                    if (pass2) {
                        n = e ? resolve_expr_pub(e, *lc, &as->st) : 0;
                    } else if (e) {
                        register_expr_symbols(e, &as->st);
                        expr_try_eval_const(e, &n); /* best-effort Pass 1 size (real
                                                       * corpus uses only plain constants) */
                    }
                    CodeBuf *target = pass2 ? seg_target(as, text_out, data_out) : NULL;
                    if (pass2 && target)
                        for (long i = 0; i < n; i++)
                            codebuf_put(target, 0x00);
                    expr_free(e);
                    *lc += n;
                    break;
                }

                /* .even - round the location counter up to the next
                 * even address, actually writing a 0x00 pad byte when
                 * needed. MUST be handled here (not in
                 * handle_directive) because that function has no
                 * access to text_out/data_out - a bug that silently
                 * desynced Pass 1 sizing (LC advanced) from Pass 2's
                 * actual buffer content (no compensating byte written)
                 * for any ".even" that fires mid-segment rather than
                 * exactly at a .text/.data transition boundary. */
                if (d->len == 5 && strncmp(d->text, ".even", 5) == 0) {
                    if (as->seg == SEG_TEXT && (as->text_loc & 1)) {
                        if (pass2 && text_out) codebuf_put(text_out, 0x00);
                        as->text_loc++;
                    } else if (as->seg == SEG_DATA && (as->data_loc & 1)) {
                        if (pass2 && data_out) codebuf_put(data_out, 0x00);
                        as->data_loc++;
                    } else if (as->seg == SEG_BSS && (as->bss_loc & 1)) {
                        as->bss_loc++; /* no byte to write - .bss has no
                                         * output stream, see seg_target() */
                    }
                    break;
                }

                /* .asciz *<string>* - a NUL-terminated ASCII string,
                 * confirmed real usage in mch.s: an embedded pathname
                 * quoted between two literal '*' characters. Unlike
                 * every other use of '*' in this syntax (byte-size
                 * marker), here it is a STRING DELIMITER - the normal
                 * tokenizer/operand parser has no notion of string
                 * literals, so this is handled as a special case by
                 * re-scanning the RAW source text directly (via the
                 * mnemonic token's pointer into the original buffer)
                 * rather than through the parsed Operand tokens, which
                 * would be garbage for this directive. */
                if (d->len == 6 && strncmp(d->text, ".asciz", 6) == 0) {
                    const char *p = d->text + d->len;
                    while (*p == ' ' || *p == '\t') p++;
                    if (*p == '*') {
                        p++;
                        const char *start = p;
                        while (*p && *p != '*' && *p != '\n') p++;
                        size_t slen = (size_t)(p - start);
                        CodeBuf *target = pass2 ? seg_target(as, text_out, data_out) : NULL;
                        if (pass2 && target) {
                            for (size_t i = 0; i < slen; i++)
                                codebuf_put(target, (unsigned char)start[i]);
                            codebuf_put(target, 0x00); /* NUL terminator */
                        }
                        *lc += (long)slen + 1;
                    }
                    break;
                }

                handle_directive(as, &s);
                break;
            }

            case STMT_ASSIGNMENT:
                handle_assignment(as, &s, text_out, data_out, pass2);
                break;

            case STMT_DATA_VALUE: {
                /* Implicit ".word <expr>" - always 2 bytes (see
                 * mutos_as.h). Register any referenced symbol in BOTH
                 * passes (see register_expr_symbols()'s doc comment) -
                 * only actually resolve/emit the value in Pass 2. */
                ExprNode *e = parse_raw_operand_expr(&s.operands[0]);
                if (pass2 && data_out) {
                    long v = e ? resolve_expr_pub(e, *lc, &as->st) : 0;
                    if (data_relocs && e) {
                        unsigned char low4; int symidx;
                        if (classify_word_reloc_pub(e, &as->st, as->seg == SEG_TEXT, &low4, &symidx))
                            reloclist_add(data_relocs, (long)data_out->len, symidx, low4);
                    }
                    codebuf_put16le(data_out, (unsigned int)(v & 0xFFFF));
                } else if (e) {
                    register_expr_symbols(e, &as->st);
                }
                expr_free(e);
                *lc += 2;
                break;
            }

            case STMT_INSTRUCTION: {
                ParsedOperand ops[MAX_OPERANDS];
                if (!classify_all(&s, ops)) {
                    fprintf(stderr, "error: could not classify operands at line %d\n", s.line);
                    as->errors++;
                    break;
                }
                CodeBuf scratch;
                codebuf_init(&scratch);
                CodeBuf *target = (pass2 && as->seg != SEG_BSS) ? seg_target(as, text_out, data_out) : &scratch;
                RelocList *rl = pass2 ? seg_relocs(as, text_relocs, data_relocs) : NULL;
                size_t before = target->len;
                bool okenc = encode_instruction(target, &s.mnemonic_or_name, ops, s.noperands,
                                                 *lc, &as->st, pass2, rl);
                if (!okenc && s.noperands == 0 && !encode_is_known_mnemonic(&s.mnemonic_or_name)) {
                    /* A BARE IDENTIFIER statement (no label, no colon,
                     * no operands at all) is a genuine syntactic
                     * ambiguity this hand-written parser can't resolve
                     * on its own, since the real mnemonic table lives
                     * in encode.c (a separate module): it could be
                     * either a zero-operand mnemonic (nop/cld/...) or
                     * a symbol reference used as an implicit
                     * ".word <ident>" data value (a pointer/jump-table
                     * entry). CONFIRMED real via tty.s: "_ttbl:_t0" /
                     * bare "_t1" / "_t2" on their own lines building a
                     * table of .data label addresses; "_esc30"
                     * appearing bare 3x after escape-sequence byte
                     * pairs; "L10003".."L10008"/"L255" etc. as bare
                     * jump-table entries. Real mnemonics are tried
                     * FIRST (encode_instruction() above) and always
                     * win when recognized.
                     *
                     * The extra encode_is_known_mnemonic() gate (added
                     * after a real bug: "insw"/"outsw" had no dispatch
                     * entry at all and were silently mis-assembled as
                     * fake external-symbol references instead of
                     * erroring - see kernel_opt/mch_insw_outsw.s and
                     * encode_is_known_mnemonic()'s doc comment) makes
                     * this ONLY fire for identifiers that are not even
                     * recognizable as an instruction name in the first
                     * place - i.e. truly a symbol reference. A token
                     * that IS a known mnemonic name but that
                     * encode_instruction() rejected (wrong operand
                     * count, an unsupported operand shape, or a
                     * genuine future gap of this same kind) now falls
                     * through to the hard error below instead of ever
                     * reaching here, matching encode_instruction()'s
                     * own documented contract in encode.h. Reuses the
                     * same resolve/register/reloc-classify pattern as
                     * the existing STMT_DATA_VALUE and ".word"
                     * handling. */
                    ExprNode sym_expr;
                    memset(&sym_expr, 0, sizeof(sym_expr));
                    sym_expr.op = EX_SYM;
                    sym_expr.sym = s.mnemonic_or_name.text;
                    sym_expr.sym_len = s.mnemonic_or_name.len;
                    if (pass2) {
                        long v = resolve_expr_pub(&sym_expr, *lc, &as->st);
                        if (rl) {
                            unsigned char low4; int symidx;
                            if (classify_word_reloc_pub(&sym_expr, &as->st, as->seg == SEG_TEXT, &low4, &symidx))
                                reloclist_add(rl, (long)target->len, symidx, low4);
                        }
                        codebuf_put16le(target, (unsigned int)(v & 0xFFFF));
                    } else {
                        register_expr_symbols(&sym_expr, &as->st);
                        codebuf_put16le(target, 0); /* placeholder - only the SIZE matters in Pass 1 */
                    }
                    okenc = true;
                }
                if (!okenc) {
                    /* Hard error for anything encode_instruction() would
                     * not encode and the bare-identifier fallback above
                     * did not claim - this now includes a recognized
                     * mnemonic name used with an operand count/shape
                     * that isn't supported (previously such statements
                     * with zero operands were silently swallowed by the
                     * fallback above instead of reaching this branch -
                     * see the comment on the gate above). */
                    if (s.noperands == 0 && encode_is_known_mnemonic(&s.mnemonic_or_name))
                        fprintf(stderr,
                                "error: mnemonic '%.*s' is recognized but not supported with 0 operands at line %d\n",
                                (int)s.mnemonic_or_name.len, s.mnemonic_or_name.text, s.line);
                    else
                        fprintf(stderr,
                                "error: unsupported instruction '%.*s' (%d operand%s) at line %d\n",
                                (int)s.mnemonic_or_name.len, s.mnemonic_or_name.text,
                                s.noperands, s.noperands == 1 ? "" : "s", s.line);
                    as->errors++;
                } else {
                    /* Advance the location counter by however many bytes
                     * this instruction actually produced - needed in
                     * BOTH passes: Pass 1 uses it for sizing/label
                     * addresses, Pass 2 needs it too (cur_addr feeds
                     * PC-relative jump/branch calculations for every
                     * following instruction). */
                    *lc += (long)(target->len - before);
                }
                free_all(ops, s.noperands);
                codebuf_free(&scratch);
                break;
            }

            case STMT_EMPTY:
                break;
        }
    }

    /* End-of-file padding: the a.out header comment ("text size ... in
     * bytes but even") applies not just at an explicit ".data"
     * transition (handled above) but also at the very end of the
     * source if the file never has a trailing .data section at all -
     * confirmed real: mch.s ends "... ret\n.end strt" with no closing
     * .data, and the real mch.o's text segment still ends with a
     * single trailing 0x00 pad byte to reach an even total length. */
    if (as->seg == SEG_TEXT && (as->text_loc & 1)) {
        if (pass2 && text_out) codebuf_put(text_out, 0x00);
        as->text_loc++;
    }
    if (as->seg == SEG_DATA && (as->data_loc & 1)) {
        if (pass2 && data_out) codebuf_put(data_out, 0x00);
        as->data_loc++;
    }

    /* Fold syntax-level errors (e.g. "unexpected token at start of
     * statement") into as->errors too - previously tracked ONLY in
     * the Parser's own local error_count and never propagated to the
     * caller, meaning a genuine syntax error printed a message but
     * silently did NOT block Pass 2 or affect the exit code/
     * executable-bit decision. Needed for as.1's documented "no
     * errors detected -> mark executable" behavior to be correct in
     * the presence of a syntax error, not just an encoding error. */
    as->errors += p.error_count;
}

/* Reads all of `f` into a freshly malloc'd, NUL-terminated buffer,
 * growing as needed (used for both named files and stdin, per as.1's
 * "Standardeingabe oder verkettete files" input model). `*out_len` is
 * set to the number of real bytes read (excluding the trailing NUL).
 * `*buf`/`*cap` are the caller's existing buffer/capacity, appended
 * to starting at `*len` - lets multiple files be concatenated into
 * one growing buffer across repeated calls. */
static void read_append(FILE *f, char **buf, size_t *len, size_t *cap)
{
    for (;;) {
        if (*len + 4096 + 1 > *cap) {
            *cap = (*cap == 0) ? 65536 : (*cap * 2);
            *buf = realloc(*buf, *cap);
        }
        size_t n = fread(*buf + *len, 1, *cap - *len - 1, f);
        *len += n;
        if (n == 0) break;
    }
}

int main(int argc, char **argv)
{
    /* as.1: "as [-LW] [-o output] file ..." / "asf [-LW] [-o output]
     * file ..." - two separate SYNOPSIS lines, not a switch on "as"
     * itself: asf is a distinct program name (traditionally a symlink
     * to the same binary) for translating programs containing K 1810
     * WM 87 (8087) arithmetic-coprocessor instructions. Recognized
     * here via argv[0]'s basename purely so invoking this binary
     * under that name doesn't error out or behave differently in any
     * way that would be user-visible as a bug - but it has NO actual
     * functional effect beyond that: neither Assembler_as.pdf nor any
     * other project material documents a single concrete 8087/K1810
     * WM87 instruction mnemonic or encoding (Anlage A/B/C are 8086/
     * 80186-only) to gate behind it. Implementing a real "asf mode"
     * would mean fabricating coprocessor opcodes with no source to
     * confirm them against - the same category of risk this project
     * has consistently avoided elsewhere (e.g. deliberately not
     * implementing CALLI/JMPI's direct "d:s" form). is_asf is tracked
     * for when/if real FPU documentation or a golden sample becomes
     * available. */
    const char *prog = argv[0];
    for (const char *p = argv[0]; *p; p++)
        if (*p == '/') prog = p + 1;
    size_t proglen = strlen(prog);
    bool is_asf = (proglen >= 3 && strcmp(prog + proglen - 3, "asf") == 0);
    (void)is_asf;

    /* as.1 documents "-L" (include compiler-internal 'L'-prefixed
     * labels in the written symbol table; without it, they'd be
     * tracked only internally and excluded from output) - REMOVED per
     * explicit request: every golden reference .o this project
     * validates against was produced with those labels present
     * unconditionally, with no flag needed, so this project always
     * includes them - matching the golden files is what actually
     * matters here, not literal switch-for-switch parity with as.1.
     * "-o"/"-W" and everything else from as.1 are unaffected. */
    bool verbose = false;
    bool opt_W = false;
    const char *opt_o = NULL;
    const char *filenames[128];
    int nfilenames = 0;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-v") == 0) { verbose = true; continue; }
        if (strcmp(a, "-W") == 0) { opt_W = true; continue; }
        if (strcmp(a, "-o") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "usage: %s [-W] [-o output] [file ...]\n", argv[0]);
                return 1;
            }
            opt_o = argv[++i];
            continue;
        }
        if (a[0] == '-' && a[1] == 'o' && a[2] != '\0') {
            opt_o = a + 2; /* attached form, e.g. "-ofoo.o" */
            continue;
        }
        if (a[0] == '-' && a[1] != '\0') {
            fprintf(stderr, "usage: %s [-W] [-o output] [file ...]\n", argv[0]);
            return 1;
        }
        if (nfilenames < (int)(sizeof(filenames) / sizeof(filenames[0])))
            filenames[nfilenames++] = a;
    }

    /* as.1: "Die Eingabe des zu assemblierenden Quellprogramms erfolgt
     * entweder über Standardeingabe, oder es werden die miteinander
     * verketteten files als zu assemblierendes Quellprogramm
     * eingegeben." - no filenames at all means read from stdin;
     * one or more filenames means read and CONCATENATE them (as one
     * continuous source, exactly as if `cat file1 file2 ... | as` had
     * been used) rather than assembling each separately. */
    char *src = NULL;
    size_t srclen = 0, srccap = 0;
    const char *display_name;
    if (nfilenames == 0) {
        read_append(stdin, &src, &srclen, &srccap);
        display_name = "<stdin>";
    } else {
        for (int i = 0; i < nfilenames; i++) {
            FILE *f = fopen(filenames[i], "rb");
            if (!f) { perror(filenames[i]); free(src); return 1; }
            read_append(f, &src, &srclen, &srccap);
            fclose(f);
            /* Defensive separator between concatenated files, so a
             * statement can never accidentally straddle a file
             * boundary just because one file didn't end in a newline
             * - harmless (a blank statement) when one was already
             * present. */
            if (srclen == 0 || src[srclen - 1] != '\n') {
                if (srclen + 1 > srccap) { srccap = srccap ? srccap * 2 : 4096; src = realloc(src, srccap); }
                src[srclen++] = '\n';
            }
        }
        display_name = filenames[0];
    }
    src[srclen] = '\0';
    size_t rd = srclen;
    const char *path = display_name;

    AsmState as;
    symtab_init(&as.st);
    as.errors = 0;

    /* as.1: "Mit -W werden alle Fehlermeldungen des Assemblers
     * ausgeschaltet." - redirect stderr to /dev/null for the duration
     * of both passes and the object-file write, restoring it right
     * before the final success summary (which is a status line, not
     * an error message, and stays visible either way). Errors are
     * still tracked internally (as.errors, Parser.error_count) and
     * still correctly block Pass 2 / the exit code / the executable-
     * bit decision under -W - only the printed text is suppressed. */
    int saved_stderr_fd = -1;
    if (opt_W) {
        fflush(stderr);
        saved_stderr_fd = dup(fileno(stderr));
        FILE *reopened = freopen("/dev/null", "w", stderr);
        (void)reopened;
    }

    /* Pass 1: sizing + symbol addresses. */
    run_pass(&as, src, rd, path, NULL, NULL, false, NULL, NULL);

    if (as.errors) {
        fprintf(stderr, "%d error(s) in Pass 1, aborting before Pass 2\n", as.errors);
        return 1;
    }

    /* Symbol table is complete now - freeze the output ordering/index
     * that relocation entries will reference (see symtab.h). Every
     * symbol, including compiler-internal 'L'-prefixed labels, is
     * always included in the written symbol table - matching every
     * golden reference .o this project validates against, which all
     * have them present unconditionally. */
    int nsyms = symtab_finalize(&as.st);

    /* Pass 2: real encoding. */
    CodeBuf text_out, data_out;
    codebuf_init(&text_out);
    codebuf_init(&data_out);
    RelocList text_relocs, data_relocs;
    reloclist_init(&text_relocs);
    reloclist_init(&data_relocs);
    run_pass(&as, src, rd, path, &text_out, &data_out, true, &text_relocs, &data_relocs);

    if (as.errors) {
        fprintf(stderr, "%d error(s) in Pass 2\n", as.errors);
        return 1;
    }

    /* as.1: "-o output" selects the output path; without it, output
     * goes to "a.out" in the current directory (NOT derived from the
     * input filename - that per-input-name ".o" naming was only ever
     * this project's own test-harness convenience, never the real
     * tool's documented default). */
    const char *objpath = opt_o ? opt_o : "a.out";

    bool wrote = objwrite_write(objpath, &as.st, nsyms, &text_out, &data_out, (size_t)as.bss_loc,
                                 &text_relocs, &data_relocs);
    if (!wrote) {
        fprintf(stderr, "error: could not write '%s'\n", objpath);
        return 1;
    }

    /* as.1: "Wenn es keine unaufgelösten Referenzen nach der
     * Assemblierung gibt und keine Fehler erkannt wurden, wird das
     * ausgegebene File mit dem Attribut ausführbar (executable)
     * markiert, anderenfalls als nicht ausführbar." - walk every
     * symbol for any that is still referenced but never defined
     * (SYM_UNDEF, defined==false); mark the output file executable
     * only if none remain (as.errors==0 is already
     * guaranteed at this point). */
    {
        bool unresolved = false;
        for (Symbol *s = as.st.create_head; s; s = s->create_next)
            if (!s->defined) { unresolved = true; break; }
        chmod(objpath, unresolved ? 0644 : 0755);
    }

    if (opt_W && saved_stderr_fd >= 0) {
        fflush(stderr);
        dup2(saved_stderr_fd, fileno(stderr));
        close(saved_stderr_fd);
    }

    fprintf(stderr, "%s: text=%zu bytes, data=%zu bytes, bss=%ld bytes, %d symbols, "
            "%zu text reloc(s), %zu data reloc(s) -> %s\n",
            path, text_out.len, data_out.len, as.bss_loc, nsyms, text_relocs.n, data_relocs.n,
            objpath);

    if (verbose) {
        for (size_t i = 0; i < text_relocs.n; i++) {
            RelocEntry *r = &text_relocs.entries[i];
            long shift = r->text_offset & 1;
            long table_off = r->text_offset - shift;
            unsigned int word = (unsigned int)(((shift & 1) << 15) | ((r->symidx & 0x7FF) << 4) | r->low4);
            fprintf(stderr, "  trel[off=%3ld]: word=0x%04x (shift=%ld symidx=%d low4=0x%x)\n",
                    table_off, word, shift, r->symidx, r->low4);
        }
        for (size_t i = 0; i < data_relocs.n; i++) {
            RelocEntry *r = &data_relocs.entries[i];
            long shift = r->text_offset & 1;
            long table_off = r->text_offset - shift;
            unsigned int word = (unsigned int)(((shift & 1) << 15) | ((r->symidx & 0x7FF) << 4) | r->low4);
            fprintf(stderr, "  drel[off=%3ld]: word=0x%04x (shift=%ld symidx=%d low4=0x%x)\n",
                    table_off, word, shift, r->symidx, r->low4);
        }
    }

    codebuf_free(&text_out);
    codebuf_free(&data_out);
    reloclist_free(&text_relocs);
    reloclist_free(&data_relocs);
    symtab_free(&as.st);
    free(src);
    return 0;
}
