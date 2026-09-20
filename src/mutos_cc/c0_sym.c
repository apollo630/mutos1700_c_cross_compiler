/*
 * c0_sym.c - local symbol table implementation for mutos_c0.
 */

#include <stdlib.h>
#include <string.h>

#include "c0_sym.h"

void symtab_init(SymTab *st)
{
    st->head = NULL;
    st->autolen = MCC_STAUTO;
    st->maxauto = MCC_STAUTO;
    st->paramlen = MCC_STARG;
}

void symtab_clear(SymTab *st)
{
    SymEntry *e = st->head;
    while (e) {
        SymEntry *next = e->next;
        free(e);
        e = next;
    }
    st->head = NULL;
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

SymEntry *symtab_declare_auto(SymTab *st, const char *name, int type, int size)
{
    if (symtab_lookup(st, name))
        return NULL;

    SymEntry *e = malloc(sizeof *e);
    truncated_name(e->name, name);
    e->hclass = SC_AUTO;
    e->type = type;
    e->is_ptr = 0;
    e->is_array = 0;

    st->autolen -= size;
    e->offset = st->autolen;
    if (st->autolen < st->maxauto)
        st->maxauto = st->autolen;

    e->next = st->head;
    st->head = e;
    return e;
}

SymEntry *symtab_declare_param(SymTab *st, const char *name, int type, int size)
{
    if (symtab_lookup(st, name))
        return NULL;

    SymEntry *e = malloc(sizeof *e);
    truncated_name(e->name, name);
    e->hclass = SC_AUTO;
    e->type = type;
    e->is_ptr = 0;
    e->is_array = 0;

    /* Mirror image of symtab_declare_auto()'s offset order: a
     * parameter's own offset is its size's running total BEFORE
     * adding this parameter's size, not after - see paramlen's own
     * comment in c0_sym.h. */
    e->offset = st->paramlen;
    st->paramlen += size;

    e->next = st->head;
    st->head = e;
    return e;
}

SymEntry *symtab_declare_static(SymTab *st, const char *name, int type, int label)
{
    if (symtab_lookup(st, name))
        return NULL;

    SymEntry *e = malloc(sizeof *e);
    truncated_name(e->name, name);
    e->hclass = SC_STATIC;
    e->type = type;
    e->is_ptr = 0;
    e->is_array = 0;
    e->offset = label; /* NOT a stack offset - see c0_sym.h's comment */

    e->next = st->head;
    st->head = e;
    return e;
}

SymEntry *symtab_declare_reg(SymTab *st, const char *name, int type, int regnum)
{
    if (symtab_lookup(st, name))
        return NULL;

    SymEntry *e = malloc(sizeof *e);
    truncated_name(e->name, name);
    e->hclass = SC_REG;
    e->type = type;
    e->is_ptr = 0;
    e->is_array = 0;
    e->offset = regnum; /* NOT a stack offset - see c0_sym.h's comment */

    e->next = st->head;
    st->head = e;
    return e;
}
