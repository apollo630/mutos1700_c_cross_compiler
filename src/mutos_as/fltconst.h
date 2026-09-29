/*
 * fltconst.h - decimal text -> MUTOS 1700 floating constant: 4 bytes
 * (the ".float" directive) or 8 bytes (".double").
 *
 * FORMAT. MUTOS 1700's floating format, as the real toolchain's own data
 * bytes show it (tests/mutos1700_libc: atof.o's 2**56 is 00 00 00 b9,
 * ecvt.o's 10.0 is 00 00 20 84, 1.0 is 00 00 00 81, atof.o's 5.0 is
 * 00 00 20 83) and as its software floating-point runtime handles it
 * (stkmath.o's fneg flips bit 7 of a double's byte 6, which is a float's
 * byte 2 - stacks.o's flds loads a float into a double's high four
 * bytes): the value is 0.1mmm...(binary) * 2**(e - 128), stored
 * little-endian as a whole - the excess-128 exponent e in the HIGHEST
 * byte, the sign in the top bit of the byte below it, then the mantissa
 * bits after the leading 1, which is not stored: 23 of them in a float,
 * 55 in a double (24 and 56 significant bits). e == 0 is zero. NOT
 * PDP-11 word order (see CLAUDE.md rule 5). Double layout confirmed by
 * tests/mutos_as/float_coverage/fltdbl.o.golden (1 + 2**-32 ->
 * 00 00 80 00 00 00 00 81).
 *
 * ACCEPTED TEXT (MUTOS1700_Assembler_as.pdf sect. 3.2: a float constant
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
 * real assembler accepts for .double; the prefixed forms are the
 * manual's constant spellings. Nothing may follow the number.
 *
 * CONVERSION MODEL. The real assembler converts with v7 libc atof()'s
 * algorithm on the 56-bit double, and a .float is the HIGH FOUR BYTES of
 * that double - truncated, not rounded. Every confirmed constant fits
 * this (19 in tests/mutos_as/float_coverage/'s goldens), and libc.a's
 * own atof.o - v7's algorithm, compiled - run on libc.a's own
 * floating-point runtime under an 8086 emulator gives the real bytes of
 * every nonzero one of them; only zeros come out differently (below):
 *
 *   - digits: fl = 10*fl + digit while fl < 2**56 (atof's "big" -
 *     also a constant in libc's own atof.o), a fraction digit lowering
 *     the decimal exponent; after that an integer digit raises the
 *     exponent and a fraction digit is dropped;
 *   - flexp = 5**k, k = |decimal exponent|, by repeated squaring;
 *   - fl /= flexp (negative exponent) or fl *= flexp; ldexp(fl, exponent);
 *     negated for a leading '-';
 *   - with nd digits, nd - k < -39 (LOGHUGE) makes atof() give up;
 *   - .float 0.10000000000000000e+00 -> cc cc 4c 7d (fltopen IN): the
 *     high half of the double for 0.1 - correct rounding would give cd.
 *
 * ROUNDING (fltmode.o.golden). Addition and division round to nearest,
 * ties to even - exactly what libc.a's dmath.o implements (a guard byte
 * with sticky bits); the division's tie rule never matters, since
 * atof()'s one division cannot tie. The multiplication rounds to
 * nearest, ties to even as well, but of which product is open:
 * libc.a's dmath.o forms a*b plus (a0*b1 - a0*b2) * 2**32 (a0..b2 the
 * mantissas' 16-bit words - one partial product loads the wrong word),
 * and no real constant has yet needed a product where that differs from
 * the exact one. It can in atof()'s fl * flexp for k >= 4 (a positive
 * decimal exponent, counting digits dropped past 2**56: "%.17e" text
 * from e+20 up) and inside flexp for k = 50..54 (from e-33 down).
 * flt_encode() runs the conversion under every combination still
 * possible and accepts a constant only if all give the same bytes;
 * otherwise FLT_ROUNDING. E.g. .float
 * 1.26765060022822940e+30 (2**100) is 00 00 00 e5 with the exact
 * product and ff ff 7f e4 with libc.a's.
 *
 * ZERO. The real arithmetic, given a zero operand, does not clear its
 * accumulator fac as libc.a's does: it zeroes only fac's exponent byte
 * and leaves the rest as the previous operation left it - libc.a's
 * runtime changed in just that way reproduces all seven real zeros with
 * k >= 1. For k >= 1 the previous operation built flexp, so the result
 * is flexp's mantissa with exponent byte 0 on either path (ldexp()
 * leaves an exponent byte of 0 alone), the sign applied afterwards:
 *
 *     ".float 0.0" -> 00 00 20 00 (5**1, fltopen Z1),
 *     ".float 0.00000000000000000e+00" -> bc a2 31 00 (5**17, fltzero -
 *     and every zero compiled into atof.o/ecvt.o),
 *     ".double 0.00000000000000000e+00" -> 00 00 c5 2e bc a2 31 00
 *     (fltopen Z3), "-0.00000000000000000e+00" -> bc a2 b1 00 (fltopen
 *     Z2), ".float 0.0e+05" -> 00 40 1c 00 (5**4 on the multiplication
 *     path, fltmode Z4), ".double 0.0000000000000000000000000" ->
 *     85 14 40 61 51 59 04 00 (the rounded 5**25, fltmode Z25).
 *
 * For k = 0 nothing builds flexp, and fac holds what the digit loop
 * left there: ".float 0.00000000000000000e+17" -> ff ff ff 00 (fltmode
 * Z0), which libc.a's runtime does not reproduce. So a zero with k = 0
 * is accepted only in that shape - a .float, 18 digits, no minus sign -
 * and the LOGHUGE path is refused; every other zero is accepted, both
 * directives and signs (FLT_ZERO otherwise). Values outside the format's
 * exponent range at any step of the conversion are refused too
 * (FLT_RANGE): the real arithmetic's overflow and underflow behaviour is
 * unknown.
 */

#ifndef MUTOS_AS_FLTCONST_H
#define MUTOS_AS_FLTCONST_H

#include <stddef.h>

/* Which directive's format: its size and its prefix. */
typedef enum {
    FP_FLOAT = 0,   /* .float:  4 bytes, the double's high half, 0f prefix */
    FP_DOUBLE       /* .double: 8 bytes, 0d prefix */
} FpKind;

typedef enum {
    FLT_OK = 0,
    FLT_SYNTAX,     /* not a number in the accepted syntax */
    FLT_ZERO,       /* a zero whose real bytes are not known (see above) */
    FLT_ROUNDING,   /* the bytes depend on which product the real dmul forms */
    FLT_RANGE       /* exponent outside the format at some step, or text too long */
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
