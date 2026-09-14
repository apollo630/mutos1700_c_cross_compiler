/*
 * c1_stream.h - byte-level reader for the temp1/temp2 tagged
 * intermediate-code stream. The exact inverse of c0_outcode.c's
 * writer - see c0_outcode.h for the wire format.
 *
 * Unlike a self-describing format, the tag byte alone does not say
 * how many/which arguments follow: the reader must know, opcode by
 * opcode, which of these primitives to call next - exactly mirroring
 * how the writer (mutos_c0) called outcode() with an explicit format
 * string at each call site. c1_gen.c's dispatch loop is what encodes
 * that per-opcode knowledge on the reading side.
 */

#ifndef MUTOS_C1_STREAM_H
#define MUTOS_C1_STREAM_H

#include <stdio.h>

/* Reads one opcode tag: two bytes, the second of which MUST be 0xFE.
 * Fatal error (does not return) on malformed input or premature EOF. */
int c1_read_op(FILE *src, const char *stream_name);

/* Reads one 16-bit little-endian word, sign-extended to `int` (CON's
 * value field, label numbers, etc. are all written this way). */
int c1_read_num(FILE *src, const char *stream_name);

/* Reads a NUL-terminated symbol name (already including any leading
 * '_' the writer prepended - see c0_outcode.h's 'S' format). Caller
 * owns the returned buffer (free() it). */
char *c1_read_sym(FILE *src, const char *stream_name);

#endif /* MUTOS_C1_STREAM_H */
