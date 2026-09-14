/*
 * c1_stream.c - byte-level reader for the temp1/temp2 stream.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "c1_stream.h"

static void fatal_stream(const char *stream_name, const char *why)
{
    fprintf(stderr, "mutos_c1: malformed %s: %s\n", stream_name, why);
    exit(1);
}

int c1_read_op(FILE *src, const char *stream_name)
{
    int lo = getc(src);
    int hi = getc(src);
    if (lo == EOF || hi == EOF)
        fatal_stream(stream_name, "unexpected end of file reading an opcode tag");
    if (hi != 0xFE)
        fatal_stream(stream_name, "expected opcode tag byte (0xFE) not found - "
                                   "stream is out of sync with what mutos_c1 expects");
    return lo & 0xFF;
}

int c1_read_num(FILE *src, const char *stream_name)
{
    int lo = getc(src);
    int hi = getc(src);
    if (lo == EOF || hi == EOF)
        fatal_stream(stream_name, "unexpected end of file reading a data word");
    uint16_t raw = (uint16_t)((hi << 8) | (lo & 0xFF));
    return (int16_t)raw;
}

char *c1_read_sym(FILE *src, const char *stream_name)
{
    size_t cap = 16, n = 0;
    char *s = malloc(cap);
    for (;;) {
        int c = getc(src);
        if (c == EOF)
            fatal_stream(stream_name, "unexpected end of file reading a symbol name");
        if (n + 1 >= cap) { cap *= 2; s = realloc(s, cap); }
        s[n++] = (char)c;
        if (c == '\0')
            break;
    }
    return s;
}
