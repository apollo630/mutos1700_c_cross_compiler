/*
 * fltconst.c - decimal text -> MUTOS 1700 floating constant, 4 bytes
 * (.float) or 8 bytes (.double). See fltconst.h for the format, the
 * accepted syntax, the conversion model and why a constant is refused.
 *
 * Method: the real assembler's conversion is re-enacted step by step -
 * v7 libc atof()'s algorithm on the MUTOS 56-bit double (digits
 * accumulated while below 2**56, flexp = 5**k by repeated squaring, one
 * division or multiplication, ldexp), then .float = the high four bytes
 * of the double. Addition and division round to nearest, ties to even
 * (float_coverage/fltmode.o.golden; the division's tie rule is left
 * open but never matters). The multiplication rounds to nearest-even
 * too, but of which product is open: the exact one, or the one
 * libc.a's own dmath.o forms (one partial product taken from the wrong
 * word - see dbl_mul()); every real constant so far fits both. So the
 * conversion is run once for each combination still consistent with
 * the real bytes (MODES_MUL, MODES_ADD, MODES_DIV below), and a
 * constant is accepted only if every run gives the same bytes. All
 * arithmetic is exact integer arithmetic on 64-bit words; no host
 * floating point is involved anywhere.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fltconst.h"

/* Significant bits of a MUTOS double (the leading 1 included). */
#define SIG 56

/* Guards the digit and exponent counters against overflow on absurd
 * input; anything this long is out of range anyway. */
#define COUNT_LIMIT  100000L

/* v7 atof()'s LOGHUGE (libc.a's atof.o compares with -39): with nd
 * digits and a decimal exponent of -k, nd - k < -LOGHUGE makes atof()
 * give up - fl = 0 and the exponent 0 - and return bytes no real
 * constant shows. A nonzero value there is below both formats' range
 * anyway. */
#define LOGHUGE 39

/* The one zero with decimal exponent 0 the real assembler has shown:
 * ".float 0.00000000000000000e+17" -> ff ff ff 00 (fltmode.o.golden,
 * Z0), 18 digits. See the zero rule in convert(). */
#define ZERO_K0_DIGITS 18
static const unsigned char ZERO_K0_FLOAT[4] = { 0xFF, 0xFF, 0xFF, 0x00 };

/* ------------------------------------------------------------------ */
/* The emulated MUTOS double.                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    RM_TRUNC,       /* toward zero (the magnitude is truncated) */
    RM_NEAR_EVEN,   /* to nearest, ties to even */
    RM_NEAR_AWAY,   /* to nearest, ties away from zero */
    RM_AWAY,        /* away from zero on any nonzero remainder */
    RM_LIBC_MUL,    /* dmul only: libc.a dmath.o's product (dbl_mul()),
                     * rounded to nearest, ties to even */
    RM_COUNT
} RoundMode;

/* A nonzero double: value = m * 2**e, 2**55 <= m < 2**56. */
typedef struct {
    uint64_t m;
    long     e;
} Dbl;

/* The MUTOS exponent of d: d = 0.1mmm...(binary) * 2**x. */
static long dbl_x(Dbl d)
{
    return d.e + SIG;
}

/* The format's exponent byte is excess-128, 1..255 for a nonzero value. */
static bool x_in_range(long x)
{
    return x >= -127 && x <= 127;
}

static int bitlen64(uint64_t v)
{
    int n = 0;
    while (v) {
        v >>= 1;
        n++;
    }
    return n;
}

/* Full 64 x 64 -> 128-bit product, as (hi, lo). */
static void mul64(uint64_t a, uint64_t b, uint64_t *hi, uint64_t *lo)
{
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32;
    uint64_t b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    *lo = (p00 & 0xFFFFFFFFu) | (mid << 32);
    *hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

/* Rounds the exact value (hi:lo + f) * 2**e - f in (0,1) when sticky,
 * else 0; hi:lo nonzero - to SIG bits with the given mode. Callers pass
 * at least SIG + 1 bits whenever sticky is set, and at most 2 * SIG
 * bits, so the discarded tail always lies within lo. Returns false if
 * the result is outside the format's exponent range (the real
 * arithmetic's overflow behaviour is unknown). */
static bool round_dbl(uint64_t hi, uint64_t lo, bool sticky, long e, RoundMode mode, Dbl *out)
{
    int len = hi ? 64 + bitlen64(hi) : bitlen64(lo);
    uint64_t keep;
    int s;

    if (len <= SIG) {
        s = SIG - len;                  /* exact: hi == 0, sticky clear */
        keep = lo << s;
        out->m = keep;
        out->e = e - s;
        return x_in_range(dbl_x(*out));
    }
    s = len - SIG;                      /* 1..56: products have <= 112 bits */
    keep = (hi << (64 - s)) | (lo >> s);
    uint64_t tail = lo & ((UINT64_C(1) << s) - 1);
    uint64_t half = UINT64_C(1) << (s - 1);
    bool up;
    switch (mode) {
        case RM_TRUNC:     up = false; break;
        case RM_NEAR_EVEN:
        case RM_LIBC_MUL:  up = tail > half || (tail == half && (sticky || (keep & 1))); break;
        case RM_NEAR_AWAY: up = tail >= half; break;
        default:           up = tail != 0 || sticky; break;
    }
    if (up) {
        keep++;
        if (keep == (UINT64_C(1) << SIG)) {
            keep >>= 1;
            s++;
        }
    }
    out->m = keep;
    out->e = e + s;
    return x_in_range(dbl_x(*out));
}

static bool dbl_from_u64(uint64_t v, RoundMode mode, Dbl *out)
{
    return round_dbl(0, v, false, 0, mode, out);
}

/* The integer value of a double known to be a (nonnegative) integer
 * below 2**64 - the digit accumulator, always below 2**60. */
static uint64_t dbl_int(Dbl d)
{
    return d.e >= 0 ? d.m << d.e : d.m >> -d.e;
}

/* a * b, where a is the operand the runtime's fmuld addresses (dmul's
 * [si]) and b the one on its floating-point stack ([di]) - the order
 * atof() multiplies in: fl * 10, flexp * exp5, exp5 * exp5, fl * flexp.
 *
 * RM_LIBC_MUL forms the product the way libc.a's dmath.o does (pmuld,
 * text 0x2e4..0x425, read from the real object and checked against it
 * under an 8086 emulator): sixteen 16-bit partial products of the
 * mantissas' words a0..a3, b0..b3 (a3/b3 the top byte with the leading
 * 1), except that the one for a0*b2 loads b1's word ("mov dx,2[di]" at
 * 0x352 where "4[di]" was meant). The product is therefore
 * a*b + (a0*b1 - a0*b2) * 2**32 - between 2**110 and 2**112 like the
 * exact one - then rounded to nearest, ties to even, from all of its
 * bits (guard byte plus sticky). It equals the exact product whenever
 * a0 is 0 or b1 == b2, which covers every multiplication any real
 * constant so far has needed. */
static bool dbl_mul(Dbl a, Dbl b, RoundMode mode, Dbl *out)
{
    uint64_t hi, lo;
    mul64(a.m, b.m, &hi, &lo);          /* 111 or 112 bits */
    if (mode == RM_LIBC_MUL) {
        uint64_t a0 = a.m & 0xFFFFu;
        uint64_t add = (a0 * ((b.m >> 16) & 0xFFFFu)) << 32;   /* < 2**64 */
        uint64_t sub = (a0 * ((b.m >> 32) & 0xFFFFu)) << 32;
        lo += add;
        if (lo < add)
            hi++;
        if (lo < sub)
            hi--;
        lo -= sub;
    }
    return round_dbl(hi, lo, false, a.e + b.e, mode, out);
}

static bool dbl_div(Dbl a, Dbl b, RoundMode mode, Dbl *out)
{
    /* q = floor(a.m * 2**62 / b.m) by restoring division: a.m / b.m is
     * in (1/2, 2), so q has 62 or 63 bits - at least SIG + 6. */
    uint64_t r = a.m, q = 0;
    if (r >= b.m) {
        r -= b.m;
        q = 1;
    }
    for (int i = 0; i < 62; i++) {
        r <<= 1;                        /* r < b.m < 2**56: no overflow */
        q <<= 1;
        if (r >= b.m) {
            r -= b.m;
            q |= 1;
        }
    }
    return round_dbl(0, q, r != 0, a.e - b.e - 62, mode, out);
}

/* ------------------------------------------------------------------ */
/* The conversion (v7 atof()) for one rounding-mode combination.       */
/* ------------------------------------------------------------------ */

/* The parsed text: a digit span with at most one '.', a sign and the
 * explicit decimal exponent. */
typedef struct {
    const char *digits;     /* digits and '.', [digits, digits_end) */
    const char *digits_end;
    bool        neg;
    long        eexp;
} FltText;

/* CV_OK: out[0..8) is the double image. CV_OK_HIGH: only out[4..8), the
 * image's high half (all a .float stores), is known. CV_ZERO: a zero
 * whose real bytes are not known. CV_RANGE: a step leaves the format's
 * exponent range. */
typedef enum { CV_OK, CV_OK_HIGH, CV_ZERO, CV_RANGE } ConvResult;

/* Writes a double image: the low 55 bits of m (the leading 1 is not
 * stored), the sign in bit 7 of byte 6, the exponent byte xbyte. */
static void put_image(uint64_t m, bool neg, unsigned xbyte, unsigned char out[8])
{
    uint64_t mant = m & ((UINT64_C(1) << (SIG - 1)) - 1);
    for (int i = 0; i < 7; i++)
        out[i] = (unsigned char)((mant >> (8 * i)) & 0xFF);
    out[6] = (unsigned char)(out[6] | (neg ? 0x80 : 0));   /* fl = -fl */
    out[7] = (unsigned char)xbyte;
}

/* v7 atof(): one run with dmul rounding mode_m, dadd rounding mode_a and
 * ddiv rounding mode_d. */
static ConvResult convert(const FltText *t, RoundMode mode_m, RoundMode mode_a,
                          RoundMode mode_d, unsigned char out[8])
{
    Dbl fl = { 0, 0 };
    bool fl_zero = true;
    bool frac = false;
    long exp10 = 0, nd = 0;

    /* Digits: while fl < 2**56 ("big"), fl = 10*fl + digit - a dmul and
     * a dadd - and a fraction digit lowers the exponent; after that an
     * integer digit raises the exponent and a fraction digit is
     * dropped. A double below 2**56 is exactly one with e <= 0. The
     * product 10*fl is formed exactly under every dmul mode (10.0's
     * mantissa words b1, b2 are 0, see dbl_mul()) and rounded here. */
    for (const char *p = t->digits; p < t->digits_end; p++) {
        if (*p == '.') {
            frac = true;
            continue;
        }
        if (fl_zero || fl.e <= 0) {
            uint64_t v = 0;
            if (!fl_zero) {
                Dbl ten_fl;
                if (!dbl_from_u64(dbl_int(fl) * 10, mode_m, &ten_fl))
                    return CV_RANGE;
                v = dbl_int(ten_fl);
            }
            v += (uint64_t)(*p - '0');
            if (v) {
                if (!dbl_from_u64(v, mode_a, &fl))
                    return CV_RANGE;
                fl_zero = false;
            }
            if (frac)
                exp10--;
        } else if (!frac) {
            exp10++;
        }
        nd++;
    }
    exp10 += t->eexp;
    long k = exp10 < 0 ? -exp10 : exp10;

    if (exp10 < 0 && nd - k < -LOGHUGE)
        return fl_zero ? CV_ZERO : CV_RANGE;

    /* flexp = 5**k: flexp *= exp5 for every set bit of k, exp5 squared
     * in between - v7's loop, including its last squaring rule. For
     * k >= 1 the loop ends with a flexp *= exp5, so the runtime's
     * accumulator fac holds flexp afterwards (the zero rule below). */
    Dbl flexp, exp5;
    (void)dbl_from_u64(1, RM_TRUNC, &flexp);
    (void)dbl_from_u64(5, RM_TRUNC, &exp5);
    for (long kk = k;;) {
        if ((kk & 1) && !dbl_mul(flexp, exp5, mode_m, &flexp))
            return CV_RANGE;
        kk >>= 1;
        if (kk == 0)
            break;
        if (!dbl_mul(exp5, exp5, mode_m, &exp5))
            return CV_RANGE;
    }

    /* Zero. The real arithmetic, given a zero operand, clears only the
     * exponent byte of its accumulator fac and leaves the rest of fac as
     * the previous operation left it (fltconst.h). For k >= 1 that is
     * flexp - the divisor or the multiplier - so the result is flexp's
     * mantissa with exponent byte 0, the sign applied afterwards
     * (fltopen.o.golden, fltmode.o.golden: k = 1, 4, 17, 25). For k = 0
     * no multiplication builds flexp, and fac holds whatever the digit
     * loop left there: observed only for an unsigned .float with 18
     * digits. ldexp() leaves an exponent byte of 0 alone. */
    if (fl_zero) {
        if (k == 0) {
            if (t->neg || nd != ZERO_K0_DIGITS)
                return CV_ZERO;
            memset(out, 0, 4);
            memcpy(out + 4, ZERO_K0_FLOAT, 4);
            return CV_OK_HIGH;
        }
        put_image(flexp.m, t->neg, 0, out);
        return CV_OK;
    }

    if (exp10 < 0) {
        if (!dbl_div(fl, flexp, mode_d, &fl))
            return CV_RANGE;
    } else if (!dbl_mul(fl, flexp, mode_m, &fl)) {
        return CV_RANGE;
    }
    fl.e += exp10;                                      /* ldexp: exact */
    long x = dbl_x(fl);
    if (!x_in_range(x))
        return CV_RANGE;

    put_image(fl.m, t->neg, (unsigned)(x + 128), out);
    return CV_OK;
}

/* The modes the real double arithmetic may use, per operation. Of the
 * combinations of truncation, nearest-even, nearest-away and
 * away-from-zero for dmul, dadd and ddiv - plus libc.a's own product
 * for dmul - only these reproduce every real constant
 * (tests/mutos_as/float_coverage/fltmodel.py survivors):
 *
 *   dadd: nearest-even (fltmode.o.golden's M1..M4 and CF);
 *   ddiv: nearest, ties even or away - fltdbl.o.golden rules out
 *         truncation, M1..M4 the rest. The tie rule never matters:
 *         atof()'s one division has the divisor 5**k, whose mantissa
 *         has an odd factor of at least 5 for every k in range, so an
 *         exact quotient has at most 54 significant bits;
 *   dmul: nearest-even (M1..M4, CF, Z25) - of the exact product or of
 *         libc.a's (RM_LIBC_MUL, see dbl_mul()). libc.a's atof run on
 *         libc.a's runtime gives every nonzero real constant's bytes;
 *         no real constant has needed a product where the two differ.
 *         In atof() they can differ only in fl * flexp for k >= 4 (a
 *         positive decimal exponent, counting digits dropped past
 *         2**56: "%.17e" text from e+20 up - 5**4 is the first power
 *         whose word b2 is not 0) and in flexp itself for k = 50..54
 *         ("%.17e" text from e-33 down). A constant whose stored bytes
 *         they decide is FLT_ROUNDING until a real one decides.
 *
 * Keep in sync with fltmodel.py's EXPECTED. */
static const RoundMode MODES_MUL[] = { RM_NEAR_EVEN, RM_LIBC_MUL };
static const RoundMode MODES_ADD[] = { RM_NEAR_EVEN };
static const RoundMode MODES_DIV[] = { RM_NEAR_EVEN, RM_NEAR_AWAY };

#define NELEM(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* ------------------------------------------------------------------ */

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* ASCII letter to lower case; anything else unchanged. */
static char to_lower(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

size_t flt_size(FpKind kind)
{
    return kind == FP_DOUBLE ? 8 : 4;
}

FltStatus flt_encode(FpKind kind, const char *s, size_t len, unsigned char *out)
{
    FltText t;
    size_t i = 0;
    char exp_letter = 'e';   /* atof's exponent letter: the bare form's */
    char prefix = (kind == FP_DOUBLE) ? 'd' : 'f';

    /* Optional "0f"/"0F" (.float) or "0d"/"0D" (.double) constant prefix
     * - the manual's spelling; after "0d" the exponent letter is d/D. */
    if (len >= 2 && s[0] == '0' && to_lower(s[1]) == prefix) {
        i = 2;
        if (kind == FP_DOUBLE)
            exp_letter = 'd';
    }
    t.neg = false;
    if (i < len && (s[i] == '+' || s[i] == '-')) {
        t.neg = (s[i] == '-');
        i++;
    }

    /* Digits with at most one '.', at least one digit. */
    t.digits = s + i;
    long ndigits = 0;
    bool dot = false;
    for (; i < len; i++) {
        if (s[i] == '.') {
            if (dot)
                break;
            dot = true;
            continue;
        }
        if (!is_digit(s[i]))
            break;
        if (++ndigits > COUNT_LIMIT)
            return FLT_RANGE;
    }
    t.digits_end = s + i;
    if (ndigits == 0)
        return FLT_SYNTAX;

    /* Optional exponent. */
    t.eexp = 0;
    if (i < len && to_lower(s[i]) == exp_letter) {
        bool eneg = false;
        i++;
        if (i < len && (s[i] == '+' || s[i] == '-')) {
            eneg = (s[i] == '-');
            i++;
        }
        if (i >= len || !is_digit(s[i]))
            return FLT_SYNTAX;
        for (; i < len && is_digit(s[i]); i++) {
            if (t.eexp < COUNT_LIMIT)
                t.eexp = t.eexp * 10 + (s[i] - '0');
        }
        if (eneg)
            t.eexp = -t.eexp;
    }
    if (i != len)
        return FLT_SYNTAX;

    /* Every rounding-mode combination still possible; accepted only if
     * all agree on the bytes this directive stores (a .float stores the
     * high four bytes of the double - float_coverage/fltopen.o.golden). */
    size_t size = flt_size(kind);
    size_t from = 8 - size;
    unsigned char first[8], cur[8];
    bool have = false;
    for (int a = 0; a < NELEM(MODES_MUL); a++) {
        for (int b = 0; b < NELEM(MODES_ADD); b++) {
            for (int c = 0; c < NELEM(MODES_DIV); c++) {
                ConvResult r = convert(&t, MODES_MUL[a], MODES_ADD[b], MODES_DIV[c], cur);
                if (r == CV_ZERO || (r == CV_OK_HIGH && kind == FP_DOUBLE))
                    return FLT_ZERO;        /* independent of the modes */
                if (r == CV_RANGE)
                    return FLT_RANGE;
                if (!have) {
                    memcpy(first, cur, 8);
                    have = true;
                } else if (memcmp(first + from, cur + from, size) != 0) {
                    return FLT_ROUNDING;
                }
            }
        }
    }
    memcpy(out, first + from, size);
    return FLT_OK;
}

const char *flt_status_text(FltStatus st, FpKind kind)
{
    bool dbl = (kind == FP_DOUBLE);
    switch (st) {
        case FLT_OK:       return "ok";
        case FLT_SYNTAX:   return "not a floating-point number";
        case FLT_ZERO:
            return "this zero is not supported (the real assembler's bytes for a zero are known when its "
                   "fraction digits and exponent do not cancel - e.g. 0.00000000000000000e+00, 0.0, 0e5 - "
                   "unless its decimal exponent is below -39 minus its digit count (e.g. 0e-41); when "
                   "they cancel, only for an unsigned .float of 18 digits, e.g. 0.00000000000000000e+17)";
        case FLT_ROUNDING:
            return dbl ? "the real assembler's result depends on how its double multiplication forms "
                         "its product, which is unconfirmed for this value"
                       : "the real assembler's result depends on how its double multiplication forms "
                         "its product, which is unconfirmed for this value (the last place may differ)";
        case FLT_RANGE:
            return dbl ? "out of range for a double (the value, or a step of the real assembler's "
                         "conversion such as 5**k for a large decimal exponent)"
                       : "out of range for a float (the value, or a step of the real assembler's "
                         "conversion such as 5**k for a large decimal exponent)";
    }
    return "invalid";
}
