/*
 * fltconst.h - decimal text -> MUTOS 1700 4-byte floating constant
 * (the ".float" directive).
 *
 * MUTOS 1700's floating format, as the real toolchain's own data bytes
 * show it (tests/mutos1700_libc: atof.o's 2**56 is 00 00 00 b9, ecvt.o's
 * 10.0 is 00 00 20 84, 1.0 is 00 00 00 81, atof.o's 5.0 is 00 00 20 83)
 * and as its software floating-point runtime handles it (stkmath.o's
 * fneg flips bit 7 of a double's byte 6, which is a float's byte 2 -
 * stacks.o's flds loads a float into a double's high four bytes): the
 * value is 0.1mmm...(binary) * 2**(e - 128), stored
 * little-endian as a whole - the excess-128 exponent e in the HIGHEST
 * byte, the sign in the top bit of the byte below it, then the 23
 * mantissa bits after the leading 1, which is not stored. e == 0 is
 * zero. NOT PDP-11 word order (see CLAUDE.md rule 5).
 *
 * Accepted text (MUTOS1700_Assembler_as.pdf sect. 3.2: "denen Zeichen
 * folgen, die atof als Floating-Point-Zahl akzeptiert"):
 *
 *     [0f|0F] [+|-] digits [. [digits]] [(e|E) [+|-] digits]
 *     [0f|0F] [+|-] . digits [(e|E) [+|-] digits]
 *
 * The bare form is what the real compiler writes (tests/mutos_cc/
 * 08_float's .s goldens: ".float 3.50000000000000000e+00"); the 0f
 * prefix is the manual's float-constant spelling. Nothing may follow
 * the number.
 *
 * Only a value EXACTLY representable in this format is accepted: it is
 * encoded exactly, which is the only case real bytes confirm (every
 * constant the real compiler keeps in 4 bytes is exact - see
 * docs/DEVLOG.md's 08_float section). Whether the real assembler rounds
 * or truncates an inexact value is not known, and a zero's real bytes
 * are not a plain zero (see flt_encode()'s FLT_ZERO), so both are
 * refused rather than guessed. The check is exact: the decimal text is
 * converted with integer arithmetic, never through a host float.
 */

#ifndef MUTOS_AS_FLTCONST_H
#define MUTOS_AS_FLTCONST_H

#include <stddef.h>

typedef enum {
    FLT_OK = 0,
    FLT_SYNTAX,     /* not a number in the accepted syntax */
    FLT_ZERO,       /* the value is zero */
    FLT_INEXACT,    /* not exactly representable in 24 significant bits */
    FLT_RANGE       /* exponent outside the format, or text too long */
} FltStatus;

/* Converts the decimal text text[0..len) (no surrounding blanks) into
 * the 4 bytes of a MUTOS float, lowest address first, in out[0..3].
 * Returns FLT_OK on success; on any other status out[] is untouched. */
FltStatus flt_encode(const char *text, size_t len, unsigned char out[4]);

/* A short English description of a non-OK status, for diagnostics. */
const char *flt_status_text(FltStatus st);

#endif /* MUTOS_AS_FLTCONST_H */
