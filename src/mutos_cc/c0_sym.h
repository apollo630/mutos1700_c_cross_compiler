/*
 * c0_sym.h - local (AUTO storage class) symbol table for mutos_c0.
 *
 * Scope for this increment: a single, flat per-function symbol
 * table of AUTO (stack-local) variables - matching exactly what
 * tests/mutos_cc/01_expr/01_intarith.c needs. No nested-block
 * shadowing, no parameters, no EXTERN/STATIC/REG storage classes
 * yet - see src/mutos_cc/README.md for the expansion plan.
 *
 * Offset assignment matches v7/cc/c03.c's declarator-processing loop
 * exactly (confirmed byte-for-byte against 01_intarith's goldens -
 * see docs/DEVLOG.md's Milestone 4 section): starting from
 * MCC_STAUTO, each newly-declared AUTO variable subtracts its own
 * size from the running total *before* taking that as its offset,
 * so the first local lands at MCC_STAUTO - size, not at MCC_STAUTO
 * itself.
 */

#ifndef MUTOS_C0_SYM_H
#define MUTOS_C0_SYM_H

#include "mutos_cc.h"

typedef struct SymEntry {
    char name[MCC_NCPS + 1];  /* truncated to MCC_NCPS significant
                                * chars, NUL-terminated - see
                                * CLAUDE.md's "Identifier length
                                * limits" rule. */
    int  hclass;               /* always SC_AUTO in this scope */
    int  type;                 /* always TY_INT in this scope */
    int  offset;                /* bp-relative offset (negative) */
    struct SymEntry *next;
} SymEntry;

typedef struct {
    SymEntry *head;    /* most-recently-declared first */
    int autolen;        /* running total, v7/cc/c03.c's `autolen` */
    int maxauto;         /* most-negative autolen reached so far -
                           * v7/cc/c03.c's `maxauto`; SETSTK's
                           * argument is -maxauto (see c0_parser.c). */
} SymTab;

void symtab_init(SymTab *st);
void symtab_clear(SymTab *st);  /* frees all entries; safe to call
                                  * on an already-empty table */

/*
 * Declares a new AUTO variable of the given type/size (in bytes).
 * Returns NULL (without modifying the table) if `name` collides
 * with an already-declared name within its first MCC_NCPS
 * characters - the caller is expected to report a redeclaration
 * error in that case.
 */
SymEntry *symtab_declare_auto(SymTab *st, const char *name, int type, int size);

/* Returns NULL if `name` (truncated to MCC_NCPS chars) is not
 * currently declared. */
SymEntry *symtab_lookup(SymTab *st, const char *name);

#endif /* MUTOS_C0_SYM_H */
