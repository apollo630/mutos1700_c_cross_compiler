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
    int  type;                 /* TY_INT for a plain int or an array
                                 * (arrays are referenced via their
                                 * base element type - see
                                 * c0_parser.c's array-decay handling),
                                 * or TY_INT|010 (one pointer degree -
                                 * see mutos_cc.h's XTYPE comment) for
                                 * a pointer - confirmed against
                                 * 05_incdec.1.golden's NAME(p) using
                                 * type 8. */
    int  offset;                /* bp-relative offset (negative) */
    int  is_ptr;                 /* 1 iff declared "int *name" - only
                                   * a single pointer-to-int degree is
                                   * supported so far (see
                                   * src/mutos_cc/README.md). */
    int  is_array;                /* 1 iff declared "int name[N]" -
                                    * only a single-dimension array of
                                    * int is supported so far; an
                                    * array is not a modifiable lvalue
                                    * (no postfix/prefix ++/--, no
                                    * direct assignment target) in
                                    * this grammar scope. */
    struct SymEntry *next;
} SymEntry;

typedef struct {
    SymEntry *head;    /* most-recently-declared first */
    int autolen;        /* running total, v7/cc/c03.c's `autolen` */
    int maxauto;         /* most-negative autolen reached so far -
                           * v7/cc/c03.c's `maxauto`; SETSTK's
                           * argument is -maxauto (see c0_parser.c). */
    int paramlen;        /* running total for parameter offsets -
                           * starts at MCC_STARG (4) and grows
                           * UPWARD by each parameter's size, unlike
                           * autolen: a parameter's own offset is
                           * taken BEFORE adding its size (the first
                           * parameter lands at MCC_STARG itself),
                           * the mirror image of autolen's "subtract
                           * first, then take the offset" order -
                           * confirmed against every 04_funcs
                           * golden's ANAME sequence (offsets 4, 6,
                           * 8, ... in declared order). See
                           * src/mutos_cc/README.md's Milestone 4
                           * "Function parameters and calls" section. */
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

/*
 * Declares a new parameter (also hclass SC_AUTO - see mutos_cc.h's
 * MCC_STARG comment - but with a positive, upward-growing offset,
 * assigned in the order this function is called) of the given
 * type/size (in bytes). Same redeclaration behavior as
 * symtab_declare_auto() above.
 */
SymEntry *symtab_declare_param(SymTab *st, const char *name, int type, int size);

/*
 * Declares a new STATIC local variable (hclass SC_STATIC - a "static
 * int n;" inside a function body). Unlike an AUTO local, its
 * `offset` field is NOT a bp-relative stack offset at all - it is
 * the internal intermediate-code LABEL NUMBER of its own dedicated
 * BSS block (v7/cc/c03.c's declist(): "dsym->hoffset = isn;" for the
 * STATIC case), confirmed against 04_funcs/05_staticvar.1.golden's
 * NAME(SC_STATIC, TY_INT, 4) referencing the same label number (4)
 * its own SNAME/BSS block used. The caller is responsible for
 * allocating that label number (from the same p->isn counter every
 * other intermediate-code label comes from) and emitting the
 * BSS/LABEL/SSPACE/PROG/SNAME sequence - this function only records
 * the resulting (name -> label) binding, exactly mirroring
 * symtab_declare_auto()/symtab_declare_param() above. Same
 * redeclaration behavior as those two.
 */
SymEntry *symtab_declare_static(SymTab *st, const char *name, int type, int label);

/*
 * Declares a new 'register'-class local variable (hclass SC_REG - a
 * "register int i;" inside a function body whose own register
 * allocation succeeded - see c0_parser.c's try_claim_register()).
 * Like symtab_declare_static() above, `offset` is NOT a bp-relative
 * stack offset - it is the register-allocator's own slot number
 * (v7/cc's `regvar`, right after this variable claimed it), which
 * c1_gen.c maps to a physical register name (di/si) for every later
 * reference too. Same redeclaration behavior as the other
 * symtab_declare_*() functions above.
 */
SymEntry *symtab_declare_reg(SymTab *st, const char *name, int type, int regnum);

/* Returns NULL if `name` (truncated to MCC_NCPS chars) is not
 * currently declared. */
SymEntry *symtab_lookup(SymTab *st, const char *name);

#endif /* MUTOS_C0_SYM_H */
