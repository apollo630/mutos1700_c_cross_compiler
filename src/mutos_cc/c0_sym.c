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

    st->autolen -= size;
    e->offset = st->autolen;
    if (st->autolen < st->maxauto)
        st->maxauto = st->autolen;

    e->next = st->head;
    st->head = e;
    return e;
}
