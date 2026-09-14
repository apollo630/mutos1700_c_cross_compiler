/*
 * c0_outcode.h - the temp1/temp2 tagged intermediate-code stream
 * writer.
 *
 * Format (transcribed from v7/cc/c04.c's outcode(), reimplemented
 * type-safely with stdarg.h instead of the original's raw K&R
 * varargs-by-pointer-walk trick - the *wire format* below is
 * unchanged, only the C mechanism producing it is modernized):
 *
 *   'B'  one "operator" tag: writes (value & 0xFF, 0xFE). The fixed
 *        sentinel byte 0xFE (octal 0376) marks "this is an
 *        opcode/tag byte", distinguishing it from an ordinary 16-bit
 *        data word - a legitimate data word's high byte is never
 *        0xFE by construction (see 'N' below: MCC's opcode values
 *        are all < 256, so 0xFE would require an implausibly large
 *        word value that never occurs in this compiler's own data).
 *   'N'  one 16-bit word, little-endian (low byte, then high byte).
 *   'S'  one symbol name: a leading '_' IF the name is non-empty
 *        (matching every C-linkage name in this ABI - see
 *        docs/MUTOS_C_ABI.md), then up to MCC_NCPS significant
 *        characters of the name, masked to 7 bits, then a
 *        terminating NUL. An empty name string writes just the NUL
 *        (no leading '_').
 *   '1'  the literal word 1 (shorthand for a common constant).
 *   '0'  the literal word 0 (shorthand for a common constant).
 *
 * Each format character consumes exactly one variadic argument:
 * 'B'/'N'/'1'/'0' consume an `int`; 'S' consumes a `const char *`.
 */

#ifndef MUTOS_C0_OUTCODE_H
#define MUTOS_C0_OUTCODE_H

#include <stdio.h>

void outcode(FILE *dst, const char *fmt, ...);

#endif /* MUTOS_C0_OUTCODE_H */
