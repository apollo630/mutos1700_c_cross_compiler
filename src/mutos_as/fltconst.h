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
 * this, most tellingly the four in float_coverage/fltopen.o.golden,
 * two of which (Z1, Z3) were predicted byte for byte before the real
 * assembler ran:
 *
 *   - digits: fl = 10*fl + digit while fl < 2**56 (atof's "big" -
 *     also a constant in libc's own atof.o), a fraction digit lowering
 *     the decimal exponent; after that an integer digit raises the
 *     exponent and a fraction digit is dropped;
 *   - flexp = 5**k, k = |decimal exponent|, by repeated squaring;
 *   - fl /= flexp (negative exponent) or fl *= flexp; ldexp(fl, exponent);
 *     negated for a leading '-';
 *   - .float 0.10000000000000000e+00 -> cc cc 4c 7d (fltopen IN): the
 *     high half of the double for 0.1 - correct rounding would give cd;
 *   - a zero dividend gives the DIVISOR's mantissa with exponent byte 0:
 *     ".float 0.0" -> 00 00 20 00 (5**1, fltopen Z1),
 *     ".float 0.00000000000000000e+00" -> bc a2 31 00 (5**17, fltzero -
 *     and every zero compiled into atof.o/ecvt.o),
 *     ".double 0.00000000000000000e+00" -> 00 00 c5 2e bc a2 31 00
 *     (fltopen Z3; its high half is fltzero's bytes), and the sign is
 *     applied afterwards: "-0.00000000000000000e+00" -> bc a2 b1 00
 *     (fltopen Z2).
 *
 * How each double operation (dmul, dadd, ddiv) rounds is not known,
 * except that ddiv does not truncate (fltdbl.o.golden needs its one
 * inexact quotient rounded up; truncation gives one unit less). So
 * flt_encode() runs the model under every remaining combination - dmul
 * and dadd each truncating, to nearest (ties even or away) or away from
 * zero, ddiv any of those but truncating - and accepts a constant only
 * if all of them give the same bytes. That covers every exactly
 * representable value whose conversion needs no rounding (all constants
 * seen in real objects), and inexact values too where the unknown
 * rounding cannot reach the stored bytes (a .float's hidden low 32 bits
 * absorb it, e.g. 0.1). It refuses (FLT_ROUNDING) a value whose bytes
 * the unknown rounding decides - including some EXACT floats written in
 * the compiler's 18-digit "%.17e" form, e.g. 2.93572534179687500e+03,
 * whose 18th digit makes 10*fl inexact: under a truncating dmul the
 * real assembler would store one unit less in the last place.
 *
 * A zero is accepted only where it is observed: a negative decimal
 * exponent of at most 24 (more fraction digits than exponent), so that
 * 5**k is exact; any sign, either directive. Other zeros ("0", "0e5"
 * - atof's multiplication path - or more than 24) are refused
 * (FLT_ZERO). Values outside the format's exponent range at any step of
 * the conversion are refused too (FLT_RANGE): the real arithmetic's
 * overflow and underflow behaviour is unknown.
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
    FLT_ROUNDING,   /* the bytes depend on the real arithmetic's unknown rounding */
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
