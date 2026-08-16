/*
 * objwrite.c - see objwrite.h.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "objwrite.h"
#include "../h/mutos_aout.h"

static uint8_t symtype_to_ntype(SymType t)
{
    switch (t) {
        case SYM_UNDEF: return N_UNDF;
        case SYM_ABS:   return N_ABS;
        case SYM_TEXT:  return N_TEXT;
        case SYM_DATA:  return N_DATA;
        /* SYM_BSS (.comm) is written as N_UNDF|N_EXT in the real
         * format, NOT a distinct N_BSS entry - confirmed against real
         * malloc.o: a .comm'd symbol like "_canonb" has type byte 0x20
         * (UNDF|EXT), with its requested SIZE in the value field. The
         * SYM_BSS distinction is purely an internal bookkeeping detail
         * of this assembler's own symbol table (see symtab.h) so that
         * resolve_expr() can tell "external, still needs linking" apart
         * from "already has a real in-module address" - it collapses
         * back to N_UNDF here since that's what real object files use. */
        case SYM_BSS:   return N_UNDF;
        /* SYM_LOCALBSS (a real ".bss" segment label) IS written as a
         * genuine N_BSS(0x04) entry, unlike SYM_BSS above - confirmed
         * real via v30opt.o's "_identDe" etc., which have type byte
         * 0x04 (plain N_BSS, not EXT since they're not .globl'd). */
        case SYM_LOCALBSS: return N_BSS;
    }
    return N_UNDF;
}

/* K&R 8-character external-symbol-name truncation - confirmed real via
 * mch.s/opcodetest.s: `global_char`/`global_int` both truncate to the
 * same 8 bytes ("_global_"), a genuine collision the real toolchain
 * reproduces rather than avoiding. No NUL terminator is written if the
 * name is >= 8 characters (matches the fixed 8-byte name field). */
static void write_name8(uint8_t *dst, const char *name)
{
    size_t len = strlen(name);
    size_t n = len < 8 ? len : 8;
    memcpy(dst, name, n);
    for (size_t i = n; i < 8; i++) dst[i] = 0;
}

typedef struct {
    uint8_t *buf;
    size_t   n; /* symbols written so far */
} SymWriteCtx;

static void write_one_symbol(Symbol *s, void *ctx_)
{
    SymWriteCtx *ctx = (SymWriteCtx *)ctx_;
    uint8_t *dst = ctx->buf + (size_t)s->out_index * MUTOS_SYM_SIZE;
    mutos_sym_t sym;
    memset(&sym, 0, sizeof(sym));
    write_name8((uint8_t *)sym.name, s->name);
    uint8_t type = symtype_to_ntype(s->type);
    /* A symbol that is still SYM_UNDEF at this point was only ever
     * REFERENCED, never defined in this module (e.g. "cret", an
     * external runtime helper malloc.s calls but doesn't itself
     * define) - it MUST carry N_EXT regardless of whether it happened
     * to be .globl'd, since the linker can only ever resolve it by
     * looking in other object files/libraries. Confirmed real: every
     * genuinely undefined symbol in malloc.o/mch.o has type byte 0x20
     * (UNDF|EXT), never bare 0x00. */
    if (s->type == SYM_UNDEF || s->is_extern) type |= N_EXT;
    sym.type = type;
    sym.spare = 0;
    sym.value = (uint16_t)(s->value & 0xFFFF);
    mutos_sym_write(dst, &sym);
    ctx->n++;
}

static void build_reloc_table(uint8_t *table, size_t table_bytes, const RelocList *relocs)
{
    memset(table, 0, table_bytes);
    for (size_t i = 0; i < relocs->n; i++) {
        const RelocEntry *r = &relocs->entries[i];
        long shift = r->text_offset & 1;
        long slot = r->text_offset - shift; /* nominal even-byte grid index */
        if (slot < 0 || (size_t)slot + 1 >= table_bytes) continue; /* out of range, skip defensively */
        uint16_t word = (uint16_t)(((shift & 1) << 15) | ((r->symidx & 0x7FF) << 4) | (r->low4 & 0xF));
        mutos_put_u16le(table + slot, word);
    }
}

bool objwrite_write(const char *path, SymTab *st, int nsyms,
                     const CodeBuf *text, const CodeBuf *data, size_t bss_size,
                     const RelocList *text_relocs, const RelocList *data_relocs)
{
    size_t a_text = text->len;
    size_t a_data = data->len;
    size_t a_syms = (size_t)nsyms * MUTOS_SYM_SIZE;

    mutos_hdr_t hdr;
    hdr.magic = A_MAGIC1;   /* OMAGIC - matches every real .o seen (malloc.o, mch.o) */
    hdr.text = (uint16_t)a_text;
    hdr.data = (uint16_t)a_data;
    hdr.bss = (uint16_t)bss_size;   /* total ".bss" segment size (.blkb
                                      * reservations) - CONFIRMED real
                                      * via v30opt.o (a_bss=618); .comm
                                      * goes through the symbol table
                                      * instead (N_UNDF|N_EXT with size
                                      * in value) and never contributes
                                      * here, matching every .comm-only
                                      * .o seen so far (a_bss=0). */
    hdr.syms = (uint16_t)a_syms;
    hdr.entry = 0;           /* relocatable object, no entry point */
    hdr.unused = 0;
    hdr.flag = 0;            /* 0 = relocation info present (NOT stripped) -
                               * this is a real linkable object file, unlike
                               * the linked+stripped opcodetest/reloctest
                               * executables used earlier for structural
                               * validation only */

    uint8_t *trel = malloc(a_text ? a_text : 1);
    uint8_t *drel = malloc(a_data ? a_data : 1);
    build_reloc_table(trel, a_text, text_relocs);
    build_reloc_table(drel, a_data, data_relocs);

    uint8_t *symtab_buf = malloc(a_syms ? a_syms : 1);
    SymWriteCtx ctx = { symtab_buf, 0 };
    symtab_foreach(st, write_one_symbol, &ctx);

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(trel); free(drel); free(symtab_buf);
        return false;
    }

    uint8_t hdrbuf[MUTOS_HDR_SIZE];
    mutos_hdr_write(hdrbuf, &hdr);

    bool ok = true;
    ok = ok && fwrite(hdrbuf, 1, MUTOS_HDR_SIZE, f) == MUTOS_HDR_SIZE;
    ok = ok && (a_text == 0 || fwrite(text->data, 1, a_text, f) == a_text);
    ok = ok && (a_data == 0 || fwrite(data->data, 1, a_data, f) == a_data);
    ok = ok && (a_text == 0 || fwrite(trel, 1, a_text, f) == a_text);
    ok = ok && (a_data == 0 || fwrite(drel, 1, a_data, f) == a_data);
    ok = ok && (a_syms == 0 || fwrite(symtab_buf, 1, a_syms, f) == a_syms);

    fclose(f);
    free(trel);
    free(drel);
    free(symtab_buf);
    return ok;
}
