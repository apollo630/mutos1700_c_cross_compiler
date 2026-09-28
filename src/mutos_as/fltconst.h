/*
 * fltconst.h - decimal text -> MUTOS 1700 floating constant: 4 bytes
 * (the ".float" directive) or 8 bytes (".double").
 *
 * MUTOS 1700's floating format, as the real toolchain's own data bytes
 * show it (tests/mutos1700_libc: atof.o's 2**56 is 00 00 00 b9, ecvt.o's
 * 10.0 is 00 00 20 84, 1.0 is 00 00 00 81, atof.o's 5.0 is 00 00 20 83)
 * and as its software floating-point runtime handles it (stkmath.o's
 * fneg flips bit 7 of a double's byte 6, which is a float's byte 2 -
 * stacks.o's flds loads a float into a double's high four bytes): the
 * value is 0.1mmm...(binary) * 2**(e - 128), stored
 * little-endian as a whole - the excess-128 exponent e in the HIGHEST
 * byte, the sign in the top bit of the byte below it, then the mantissa
 * bits after the leading 1, which is not stored: 23 of them in a float,
 * 55 in a double (24 and 56 significant bits). e == 0 is zero. NOT
 * PDP-11 word order (see CLAUDE.md rule 5). The double layout is
 * confirmed by real assembler output from a known source:
 * tests/mutos_as/float_coverage/fltdbl.o.golden, ".double
 * 1.00000000023283064365386962890625" (1 + 2**-32, 33 significant bits)
 * -> 00 00 80 00 00 00 00 81.
 *
 * Accepted text (MUTOS1700_Assembler_as.pdf sect. 3.2: a float constant
 * is "0f" followed by "Zeichen ..., die atof als Floating-Point-Zahl
 * akzeptiert"; a double constant starts with "0d" instead and marks its
 * exponent with d or D):
 *
 *     .float:  [0f|0F] [+|-] digits [. [digits]] [(e|E) [+|-] digits]
 *              [0f|0F] [+|-] . digits [(e|E) [+|-] digits]
 *     .double: the same without a prefix, or with 0d|0D in place of 0f,
 *              and then (d|D) in place of (e|E)
 *
 * The bare form is what the real compiler writes (tests/mutos_cc/
 * 08_float's .s goldens: ".float 3.50000000000000000e+00") and what the
 * real assembler accepts for .double (fltdbl.s above); the prefixed
 * forms are the manual's constant spellings. Nothing may follow the
 * number.
 *
 * Only a value EXACTLY representable in the format is accepted: it is
 * encoded exactly, which is the only case real bytes confirm (every
 * nonzero 4-byte constant in libc.a is exact, and so is fltdbl.s's
 * double - see docs/DEVLOG.md's 08_float section). Whether the real
 * assembler rounds or truncates an inexact value is not known (ecvt.o's
 * one 8-byte constant, 0.03, is correctly rounded - a single sample, and
 * its assembler-input spelling is lost), so an inexact value is refused
 * rather than guessed. The check is exact: the decimal text is converted
 * with integer arithmetic, never through a host float.
 *
 * Zero is not a plain zero. The real assembler writes a zero .float as
 * bc a2 31 00 - exponent byte 0, the mantissa bits of 5**17 - confirmed
 * from a known source (float_coverage/fltzero.o.golden, ".float
 * 0.00000000000000000e+00", the "%.17e" text the real compiler writes)
 * and matching every zero constant compiled into atof.o/ecvt.o. The
 * mantissa looks like a leftover of scaling the text's 17 fraction
 * digits by 5**17 (v7 atof()'s flexp), so another spelling of zero -
 * "0.0" would be scaled by 5 - may well have other bytes. So a zero is
 * accepted only in the confirmed shape: .float, no minus sign, exactly
 * 17 digits after the '.', exponent 0 (FLT_ZERO otherwise). No zero
 * .double has real bytes anywhere, so every zero .double is refused.
 */

#ifndef MUTOS_AS_FLTCONST_H
#define MUTOS_AS_FLTCONST_H

#include <stddef.h>

/* Which directive's format: its size, its precision, its prefix. */
typedef enum {
    FP_FLOAT = 0,   /* .float:  4 bytes, 24 significant bits, 0f prefix */
    FP_DOUBLE       /* .double: 8 bytes, 56 significant bits, 0d prefix */
} FpKind;

typedef enum {
    FLT_OK = 0,
    FLT_SYNTAX,     /* not a number in the accepted syntax */
    FLT_ZERO,       /* a zero whose real bytes are not confirmed (see above) */
    FLT_INEXACT,    /* not exactly representable in the format's significant bits */
    FLT_RANGE       /* exponent outside the format, or text too long */
} FltStatus;

/* Size in bytes of one constant of this kind: 4 or 8. */
size_t flt_size(FpKind kind);

/* Converts the decimal text text[0..len) (no surrounding blanks) into
 * the flt_size(kind) bytes of a MUTOS floating constant, lowest address
 * first, in out[]. Returns FLT_OK on success; on any other status out[]
 * is untouched. */
FltStatus flt_encode(FpKind kind, const char *text, size_t len, unsigned char *out);

/* A short English description of a non-OK status, for diagnostics. */
const char *flt_status_text(FltStatus st, FpKind kind);

#endif /* MUTOS_AS_FLTCONST_H */
