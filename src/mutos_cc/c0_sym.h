/*
 * c0_sym.h - symbol tables for mutos_c0.
 *
 * One SymTab per function (parameters, AUTO/STATIC/REG locals) and one
 * for the whole translation unit (file-scope variables, hclass
 * SC_EXTERN - see symtab_declare_global()). The function's table is
 * block-structured: symtab_block_enter()/symtab_block_exit() bracket a
 * nested compound statement, whose declarations may shadow an outer
 * one's and are removed again at its closing '}' - v7/cc's pushed-down
 * name list (struct phshtab / hblklev in v7/cc/c0.h; blkend() in
 * c02.c), confirmed against tests/mutos_cc/07_scope/02_shadow.
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
    int  hclass;               /* SC_AUTO (a local or a parameter),
                                 * SC_STATIC (a local 'static'),
                                 * SC_REG (a claimed 'register' local)
                                 * or SC_EXTERN (a file-scope variable -
                                 * `offset` unused, referenced by NAME -
                                 * see symtab_declare_global()) */
    int  type;                 /* TY_INT for a plain int or an array
                                 * (arrays are referenced via their
                                 * base element type - see
                                 * c0_parser.c's array-decay handling),
                                 * or TY_INT|010 (one pointer degree -
                                 * see mutos_cc.h's XTYPE comment) for
                                 * a pointer - confirmed against
                                 * 05_incdec.1.golden's NAME(p) using
                                 * type 8. */
    int  offset;                /* bp-relative offset (negative);
                                  * see the symtab_declare_*()
                                  * functions for the other classes */
    int  is_static;              /* SC_EXTERN only: declared at file
                                  * scope with 'static' (internal
                                  * linkage - no .globl/.comm, a BSS
                                  * block of its own; see c0_parser.c's
                                  * parse_global_var()) */
    int  is_ptr;                 /* 1 iff declared as a pointer VARIABLE
                                   * ("int *p", "char **pp", a "char
                                   * *s[]" parameter) - `type` is then
                                   * the full pointer type. 0 for an
                                   * array of pointers ("char
                                   * *names[3]" is_array, type 9). */
    int  is_array;                /* 1 iff declared "T name[N]" or
                                    * "int name[N][M]" (see dim2) -
                                    * `type` is then the ELEMENT type
                                    * (int, char, long or a pointer); an
                                    * array is not a modifiable lvalue
                                    * (no postfix/prefix ++/--, no
                                    * direct assignment target) in
                                    * this grammar scope. */
    int  dim2;                    /* 0 for a scalar/pointer/1-D array;
                                    * for a 2-D array "int name[N][M]"
                                    * (is_array also set) the INNER
                                    * dimension M - one row is then
                                    * M * MCC_SZINT bytes, the scale
                                    * factor of the outer subscript
                                    * (05_arrptr/02_array2d.c - see
                                    * c0_parser.c's emit_subscript()).
                                    * Arrays of three or more
                                    * dimensions are rejected by
                                    * parse_decl(). */
    struct SymEntry *next;
} SymEntry;

typedef struct {
    SymEntry *head;    /* most-recently-declared first */
    SymEntry *scope;   /* the first entry NOT declared in the current
                         * (innermost) block - the value `head` had when
                         * that block was entered (NULL at a function's
                         * own level, which parameters and the body's
                         * outermost block share, as in v7/cc where both
                         * are declared at blklev 1). A redeclaration is
                         * an error only against the entries in front of
                         * it; anything behind it belongs to an
                         * enclosing block and is shadowed instead. */
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

/* A block's entry state, saved by symtab_block_enter() and restored
 * by symtab_block_exit(). */
typedef struct {
    SymEntry *head;
    SymEntry *scope;
    int       autolen;
} SymBlock;

void symtab_init(SymTab *st);
void symtab_clear(SymTab *st);  /* frees all entries; safe to call
                                  * on an already-empty table */

/*
 * Declares a new AUTO variable of the given type/size (in bytes).
 * Returns NULL (without modifying the table) if `name` collides
 * with a name already declared IN THE CURRENT BLOCK (see `scope`)
 * within its first MCC_NCPS characters - the caller is expected to
 * report a redeclaration error in that case.
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

/*
 * Declares a file-scope variable (hclass SC_EXTERN) in the translation
 * unit's own table - not a function's. v7/cc's extdef() declares every
 * file-scope name through decl1(EXTERN, ...), whatever its storage
 * class, and treeout() then references it by NAME ("BNNS": NAME,
 * EXTERN, type, name) - confirmed against 07_scope/01_globstat.1.golden,
 * where "int counter;" and "static int hidden;" are both referenced
 * that way. `offset` is unused (0). Same redeclaration behavior as the
 * other symtab_declare_*() functions; a compatible redeclaration
 * ("extern int total;" ... "int total;" - 07_scope/03_externdef.c) is
 * the caller's to accept, by looking the name up first.
 */
SymEntry *symtab_declare_global(SymTab *st, const char *name, int type,
                                int is_static);

/*
 * Nested-block scoping. symtab_block_enter() opens a new innermost
 * block: later declarations may reuse a name declared outside it
 * (shadowing), and their AUTO slots continue below the enclosing
 * block's. symtab_block_exit() closes it: every entry declared since is
 * freed (an outer entry of the same name is visible again) and the AUTO
 * allocation point (autolen) goes back to where the block started, so a
 * following sibling block reuses the same frame slots - v7/cc/c02.c's
 * statement() LBRACE case ("sauto = autolen; ... autolen = sauto;").
 * maxauto keeps the deepest point reached, which is what SETSTK
 * reserves: 07_scope/02_shadow.1.golden's inner "int x;" at -8 below
 * the outer one at -6, SETSTK 8.
 */
void symtab_block_enter(SymTab *st, SymBlock *saved);
void symtab_block_exit(SymTab *st, const SymBlock *saved);

/* Returns NULL if `name` (truncated to MCC_NCPS chars) is not
 * currently declared (in any enclosing block - the innermost
 * declaration wins). */
SymEntry *symtab_lookup(SymTab *st, const char *name);

#endif /* MUTOS_C0_SYM_H */
