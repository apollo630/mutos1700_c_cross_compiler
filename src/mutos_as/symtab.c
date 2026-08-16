/*
 * symtab.c - see symtab.h.
 */

#include <stdlib.h>
#include <string.h>

#include "symtab.h"

static unsigned long hash_name(const char *name, size_t len)
{
    unsigned long h = 5381;
    for (size_t i = 0; i < len; i++)
        h = ((h << 5) + h) + (unsigned char)name[i];
    return h;
}

void symtab_init(SymTab *st)
{
    memset(st, 0, sizeof(*st));
}

void symtab_free(SymTab *st)
{
    for (int i = 0; i < SYMTAB_BUCKETS; i++) {
        Symbol *s = st->buckets[i];
        while (s) {
            Symbol *next = s->next;
            free(s->name);
            free(s);
            s = next;
        }
        st->buckets[i] = NULL;
    }
}

Symbol *symtab_find(SymTab *st, const char *name, size_t len)
{
    unsigned long h = hash_name(name, len) % SYMTAB_BUCKETS;
    for (Symbol *s = st->buckets[h]; s; s = s->next) {
        if (strlen(s->name) == len && strncmp(s->name, name, len) == 0)
            return s;
    }
    return NULL;
}

Symbol *symtab_find_or_create(SymTab *st, const char *name, size_t len)
{
    Symbol *s = symtab_find(st, name, len);
    if (s) return s;

    s = calloc(1, sizeof(*s));
    s->name = malloc(len + 1);
    memcpy(s->name, name, len);
    s->name[len] = '\0';
    s->type = SYM_UNDEF;
    s->defined = false;
    s->out_index = -1;
    s->next = NULL;
    s->create_next = NULL;

    unsigned long h = hash_name(name, len) % SYMTAB_BUCKETS;
    s->next = st->buckets[h];
    st->buckets[h] = s;

    /* Append to the creation-order list (see symtab.h) - this is the
     * order actually used for output/relocation indexing, NOT the
     * hash-bucket chain above (which exists purely for O(1) lookup). */
    if (st->create_tail)
        st->create_tail->create_next = s;
    else
        st->create_head = s;
    st->create_tail = s;

    return s;
}

int symtab_finalize(SymTab *st)
{
    int n = 0;
    for (Symbol *s = st->create_head; s; s = s->create_next)
        s->out_index = n++;
    return n;
}

void symtab_foreach(SymTab *st, void (*fn)(Symbol *, void *), void *ctx)
{
    for (Symbol *s = st->create_head; s; s = s->create_next)
        fn(s, ctx);
}
