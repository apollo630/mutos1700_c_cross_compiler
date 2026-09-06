/*
 * util.c - small shared helpers for mutos_cpp.
 */

#include <stdlib.h>
#include <string.h>
#include "mutos_cpp.h"

char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = malloc(n);
    if (!p) { fprintf(stderr, "mutos_cpp: out of memory\n"); exit(8); }
    memcpy(p, s, n);
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *p = malloc(n + 1);
    if (!p) { fprintf(stderr, "mutos_cpp: out of memory\n"); exit(8); }
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

bool is_ident_start(int c) {
    return (c == '_') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool is_ident_cont(int c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

char *read_identifier_from_source(CppState *cs) {
    size_t cap = 32, len = 0;
    char *buf = malloc(cap);
    int c = source_getc(cs);
    buf[len++] = (char)c;
    while (is_ident_cont(source_peekc(cs))) {
        if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); }
        buf[len++] = (char)source_getc(cs);
    }
    buf[len] = '\0';
    return buf;
}

void skip_hspace_only(CppState *cs) {
    int c;
    while ((c = source_peekc(cs)) == ' ' || c == '\t')
        source_getc(cs);
}

const char *cur_dir(CppState *cs) {
    Source *s = cs->src;
    while (s && s->kind != SRC_FILE) s = s->parent;
    return s ? s->dir : "";
}

char *dir_of(const char *path) {
    const char *slash = strrchr(path, '/');
    if (!slash) return xstrdup(".");
    size_t n = (size_t)(slash - path);
    if (n == 0) return xstrdup("/");
    return xstrndup(path, n);
}
