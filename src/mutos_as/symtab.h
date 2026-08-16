/*
 * symtab.h - symbol table for mutos_as Pass 1 (address assignment) and
 * Pass 2 (encoding).
 *
 * Mirrors the a.out symbol classes from mutos_aout.h (N_UNDF/N_ABS/
 * N_TEXT/N_DATA/N_BSS, N_EXT), since that is exactly what Pass 2 must
 * eventually emit. A .comm declaration creates an UNDEF|EXT symbol
 * whose "value" field holds the requested SIZE (confirmed against a
 * real linked malloc.o: e.g. "_canonb" is UNDF|EXT with value=256,
 * the exact byte count from ".comm _canonb,256") - the linker resolves
 * the actual address and picks the largest requested size across all
 * input modules, mirroring classic V7 common-block linking.
 */

#ifndef MUTOS_AS_SYMTAB_H
#define MUTOS_AS_SYMTAB_H

#include <stddef.h>
#include <stdbool.h>

typedef enum {
    SYM_UNDEF = 0,  /* referenced but not (yet) defined in this module */
    SYM_ABS,        /* absolute constant, e.g. "name = 123" */
    SYM_TEXT,       /* label in .text */
    SYM_DATA,       /* label in .data */
    SYM_BSS,        /* .comm - value holds the requested size, not an address */
    SYM_LOCALBSS    /* label in a real ".bss" segment (e.g. via .blkb) -
                      * DISTINCT from SYM_BSS (.comm) above: this is a
                      * true local/module-owned reservation with a real
                      * bss-segment-relative ADDRESS in `value` (exactly
                      * like SYM_TEXT/SYM_DATA), written to the object
                      * file as a genuine N_BSS(0x04) symbol - confirmed
                      * real via v30opt.o's ".bss" section (_identDe/
                      * _devinfo/_hd/_part via .blkb), whose non-zero
                      * a_bss header field (618) and N_BSS-typed symbols
                      * are otherwise unseen in the .comm-only corpus. */
} SymType;

typedef struct Symbol {
    char    *name;       /* owned copy */
    SymType  type;
    long     value;
    bool     is_extern;  /* .globl'd (or a .comm, which is implicitly external) */
    bool     defined;    /* false for a symbol only ever referenced, never defined
                           * (stays SYM_UNDEF; must be resolved by the linker) */
    int      out_index;  /* assigned by symtab_finalize(); index this
                           * symbol will have in the emitted a.out symbol
                           * table - used as the relocation table's
                           * symidx field for R_EXT entries. CONFIRMED
                           * real ordering rule (re-derived against
                           * malloc.o.golden after this file regressed to
                           * an earlier hash-bucket-order version): the
                           * real vendor assembler assigns indices in
                           * CREATION order, i.e. the order each distinct
                           * symbol name is FIRST MENTIONED (as either a
                           * jump/branch-target reference or a label
                           * definition) during the single left-to-right
                           * Pass 1 scan - not hash-bucket/chain order,
                           * not definition order, not alphabetical.
                           * E.g. "jmp L1" mentions L1 before the L1:
                           * definition is reached; a chained label line
                           * like "L21:L19:jmp L22" mentions L21, then
                           * L19, then L22, strictly left to right - all
                           * confirmed by walking malloc.o.golden's own
                           * symbol table against malloc.s token order.
                           * Every symbol (including 'L'-prefixed
                           * compiler-internal labels) always gets a real
                           * out_index and is always written - matching
                           * every golden reference .o unconditionally. */
    struct Symbol *next;       /* hash-chain link (bucket lookup only) */
    struct Symbol *create_next;/* creation-order list link (output order) */
} Symbol;

#define SYMTAB_BUCKETS 256

typedef struct {
    Symbol *buckets[SYMTAB_BUCKETS];
    Symbol *create_head;  /* first-created symbol (output order head) */
    Symbol *create_tail;  /* last-created symbol, for O(1) append */
} SymTab;

void    symtab_init(SymTab *st);
void    symtab_free(SymTab *st);

/* Finds or creates a symbol (created as SYM_UNDEF/undefined if new). */
Symbol *symtab_find_or_create(SymTab *st, const char *name, size_t len);

/* Finds an existing symbol, or NULL. */
Symbol *symtab_find(SymTab *st, const char *name, size_t len);

/* Assigns `out_index` to every symbol, in creation order (order of
 * first mention - see the out_index field comment above), and returns
 * the total count. Every symbol - including 'L'-prefixed compiler-
 * internal labels - is always written to the object file, matching
 * every golden reference .o this project validates against. */
int symtab_finalize(SymTab *st);

/* Calls `fn(sym, ctx)` once per symbol, in the same creation order
 * symtab_finalize() assigned indices in - used to actually write the
 * a.out symbol table. */
void symtab_foreach(SymTab *st, void (*fn)(Symbol *, void *), void *ctx);

#endif /* MUTOS_AS_SYMTAB_H */
