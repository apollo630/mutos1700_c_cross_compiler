/*
 * symtab.c - see symtab.h.
 */

#include <stdlib.h>
#include <string.h>

#include "symtab.h"

int symtab_cur_seq = 0;

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
    s->def_seq = 0x7fffffff;
    s->main_seq = (len > 0 && name[0] == 'L') ? 0 : ++st->seq;
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

void symtab_mark_extern(SymTab *st, Symbol *s)
{
    s->is_extern = true;
    if (s->main_seq == 0) s->main_seq = ++st->seq;
}

static int by_main_seq(const void *a, const void *b)
{
    const Symbol *x = *(Symbol *const *)a, *y = *(Symbol *const *)b;
    return (x->main_seq > y->main_seq) - (x->main_seq < y->main_seq);
}

int symtab_finalize(SymTab *st, bool drop_local_L)
{
    int n = 0;
    if (!drop_local_L) {
        for (Symbol *s = st->create_head; s; s = s->create_next)
            s->out_index = n++;
        return n;
    }
    /* The original's -C (found 2026-10-25 by running it on the
     * goldens, tests/realbin/ascmp.py): a symbol that is defined, not
     * global and starts with a capital L - the compiler's L<number>
     * labels, a "La = 5" assignment, a label in .data or .bss - is
     * left out of the symbol table. Undefined symbols, .comm's and
     * .globl's stay, whatever they are called. Indices after a
     * dropped symbol move up, which the relocations (only ever of
     * undefined symbols) follow, as they take out_index.
     *
     * The order (found by asfuzz.py): the names that are not "L..." in
     * the order of their first mention, an "L..." name made external
     * at its .globl or .comm among them, and the "L..." names that
     * stay undefined after all of them, in the order of their first
     * mention. */
    int total = 0;
    for (Symbol *s = st->create_head; s; s = s->create_next) total++;
    Symbol **list = malloc((size_t)(total ? total : 1) * sizeof(*list));
    int m = 0;
    for (Symbol *s = st->create_head; s; s = s->create_next) {
        s->out_index = -1;
        if (s->main_seq != 0) list[m++] = s;
    }
    qsort(list, (size_t)m, sizeof(*list), by_main_seq);
    for (int i = 0; i < m; i++) list[i]->out_index = n++;
    free(list);
    for (Symbol *s = st->create_head; s; s = s->create_next)
        if (s->main_seq == 0 && !s->defined)
            s->out_index = n++;
    return n;
}

void symtab_foreach(SymTab *st, void (*fn)(Symbol *, void *), void *ctx)
{
    for (Symbol *s = st->create_head; s; s = s->create_next)
        fn(s, ctx);
}
