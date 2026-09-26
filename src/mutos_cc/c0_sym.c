/*
 * c0_sym.c - symbol table implementation for mutos_c0 (see c0_sym.h).
 */

#include <stdlib.h>
#include <string.h>

#include "c0_sym.h"

void symtab_init(SymTab *st)
{
    st->head = NULL;
    st->scope = NULL;
    st->autolen = MCC_STAUTO;
    st->maxauto = MCC_STAUTO;
    st->paramlen = MCC_STARG;
}

/* Frees the entries from `st->head` up to (not including) `stop`. */
static void free_until(SymTab *st, SymEntry *stop)
{
    SymEntry *e = st->head;
    while (e && e != stop) {
        SymEntry *next = e->next;
        free(e);
        e = next;
    }
    st->head = e;
}

void symtab_clear(SymTab *st)
{
    free_until(st, NULL);
    st->scope = NULL;
}

static void truncated_name(char *dst, const char *src)
{
    strncpy(dst, src, MCC_NCPS);
    dst[MCC_NCPS] = '\0';
}

SymEntry *symtab_lookup(SymTab *st, const char *name)
{
    char tn[MCC_NCPS + 1];
    truncated_name(tn, name);
    for (SymEntry *e = st->head; e; e = e->next)
        if (strcmp(e->name, tn) == 0)
            return e;
    return NULL;
}

/* Like symtab_lookup(), but only among the entries declared in the
 * current (innermost) block - the redeclaration check. */
static SymEntry *lookup_in_block(SymTab *st, const char *name)
{
    char tn[MCC_NCPS + 1];
    truncated_name(tn, name);
    for (SymEntry *e = st->head; e && e != st->scope; e = e->next)
        if (strcmp(e->name, tn) == 0)
            return e;
    return NULL;
}

/* Allocates and links a new entry of class `hclass` in front of the
 * table, or returns NULL if `name` is already declared in the current
 * block. The caller fills in `offset` (and any flags it needs). */
static SymEntry *new_entry(SymTab *st, const char *name, int hclass, int type)
{
    if (lookup_in_block(st, name))
        return NULL;

    SymEntry *e = malloc(sizeof *e);
    if (!e)
        abort(); /* out of memory in a compiler this small: nothing sane
                  * left to report through the diagnostic machinery */
    truncated_name(e->name, name);
    e->hclass = hclass;
    e->type = type;
    e->offset = 0;
    e->is_static = 0;
    e->is_ptr = 0;
    e->is_array = 0;
    e->dim2 = 0;

    e->next = st->head;
    st->head = e;
    return e;
}

SymEntry *symtab_declare_auto(SymTab *st, const char *name, int type, int size)
{
    SymEntry *e = new_entry(st, name, SC_AUTO, type);
    if (!e)
        return NULL;
    st->autolen -= size;
    e->offset = st->autolen;
    if (st->autolen < st->maxauto)
        st->maxauto = st->autolen;
    return e;
}

SymEntry *symtab_declare_param(SymTab *st, const char *name, int type, int size)
{
    SymEntry *e = new_entry(st, name, SC_AUTO, type);
    if (!e)
        return NULL;
    /* Mirror image of symtab_declare_auto()'s offset order: a
     * parameter's own offset is its size's running total BEFORE
     * adding this parameter's size, not after - see paramlen's own
     * comment in c0_sym.h. */
    e->offset = st->paramlen;
    st->paramlen += size;
    return e;
}

SymEntry *symtab_declare_static(SymTab *st, const char *name, int type, int label)
{
    SymEntry *e = new_entry(st, name, SC_STATIC, type);
    if (e)
        e->offset = label; /* NOT a stack offset - see c0_sym.h's comment */
    return e;
}

SymEntry *symtab_declare_reg(SymTab *st, const char *name, int type, int regnum)
{
    SymEntry *e = new_entry(st, name, SC_REG, type);
    if (e)
        e->offset = regnum; /* NOT a stack offset - see c0_sym.h's comment */
    return e;
}

SymEntry *symtab_declare_global(SymTab *st, const char *name, int type,
                                int is_static)
{
    SymEntry *e = new_entry(st, name, SC_EXTERN, type);
    if (e)
        e->is_static = is_static;
    return e;
}

void symtab_block_enter(SymTab *st, SymBlock *saved)
{
    saved->head = st->head;
    saved->scope = st->scope;
    saved->autolen = st->autolen;
    st->scope = st->head;
}

void symtab_block_exit(SymTab *st, const SymBlock *saved)
{
    free_until(st, saved->head);
    st->scope = saved->scope;
    st->autolen = saved->autolen;
}
