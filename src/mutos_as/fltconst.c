/*
 * fltconst.c - decimal text -> MUTOS 1700 floating constant, 4 bytes
 * (.float) or 8 bytes (.double). See fltconst.h for the format, the
 * accepted syntax, the conversion model and why a constant is refused.
 *
 * Method: the real assembler's conversion is re-enacted step by step -
 * v7 libc atof()'s algorithm on the MUTOS 56-bit double (digits
 * accumulated while below 2**56, flexp = 5**k by repeated squaring, one
 * division or multiplication, ldexp), then .float = the high four bytes
 * of the double. Each double operation's rounding is unknown except
 * that the division does not truncate, so the conversion is run once
 * for every rounding-mode combination still consistent with the real
 * bytes (FLT_COMBOS below), and a constant is accepted only if every
 * run gives the same bytes. All arithmetic is exact integer arithmetic
 * on 64-bit words; no host floating point is involved anywhere.
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

/* v7 atof()'s LOGHUGE: with nd digits and a decimal exponent of -k,
 * nd - k < -LOGHUGE makes atof() give up and return a zero whose bytes
 * are unknown - such a value is below both formats' range anyway. */
#define LOGHUGE 39

/* Largest k for which 5**k, and every power of five the repeated
 * squaring for it builds, is exact in 56 bits (5**24 < 2**56 < 5**25). */
#define MAX_EXACT_POW5 24

/* ------------------------------------------------------------------ */
/* The emulated MUTOS double.                                          */
/* ------------------------------------------------------------------ */

typedef enum {
    RM_TRUNC,       /* toward zero (the magnitude is truncated) */
    RM_NEAR_EVEN,   /* to nearest, ties to even */
    RM_NEAR_AWAY,   /* to nearest, ties away from zero */
    RM_AWAY,        /* away from zero on any nonzero remainder */
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
        case RM_NEAR_EVEN: up = tail > half || (tail == half && (sticky || (keep & 1))); break;
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

static bool dbl_mul(Dbl a, Dbl b, RoundMode mode, Dbl *out)
{
    uint64_t hi, lo;
    mul64(a.m, b.m, &hi, &lo);          /* 111 or 112 bits */
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

typedef enum { CV_OK, CV_ZERO, CV_RANGE } ConvResult;

/* v7 atof(): one run with dmul rounding mode_m, dadd rounding mode_a and
 * ddiv rounding mode_d. On CV_OK, out[] is the 8-byte double image. */
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
     * dropped. A double below 2**56 is exactly one with e <= 0. */
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

    /* Zero: only the division path is observed - the divisor's mantissa
     * with exponent byte 0 (fltconst.h) - and only for an exact 5**k. */
    if (fl_zero) {
        if (exp10 >= 0 || k > MAX_EXACT_POW5)
            return CV_ZERO;
        uint64_t p5 = 1;
        for (long i = 0; i < k; i++)
            p5 *= 5;
        Dbl z;
        (void)dbl_from_u64(p5, RM_TRUNC, &z);          /* exact, in range */
        uint64_t mant = z.m & ((UINT64_C(1) << (SIG - 1)) - 1);
        for (int i = 0; i < 7; i++)
            out[i] = (unsigned char)((mant >> (8 * i)) & 0xFF);
        out[6] = (unsigned char)(out[6] | (t->neg ? 0x80 : 0));
        out[7] = 0;
        return CV_OK;
    }

    if (exp10 < 0 && nd - k < -LOGHUGE)
        return CV_RANGE;

    /* flexp = 5**k: flexp *= exp5 for every set bit of k, exp5 squared
     * in between - v7's loop, including its last squaring rule. */
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

    uint64_t mant = fl.m & ((UINT64_C(1) << (SIG - 1)) - 1);
    for (int i = 0; i < 7; i++)
        out[i] = (unsigned char)((mant >> (8 * i)) & 0xFF);
    out[6] = (unsigned char)(out[6] | (t->neg ? 0x80 : 0));   /* fl = -fl */
    out[7] = (unsigned char)(x + 128);
    return CV_OK;
}

/* The rounding modes the real double arithmetic may use, per operation.
 * dmul and dadd: any. ddiv: anything but truncation -
 * float_coverage/fltdbl.o.golden (".double 1.00000000023283064365386962890625"
 * -> 00 00 80 00 00 00 00 81) needs its one inexact division, 0.13
 * ulp below the result, rounded up; truncation gives ff ff 7f 00 ...
 * 81. Every other confirmed constant allows every mode. Keep in sync
 * with tests/mutos_as/float_coverage/fltmodel.py, which re-derives
 * this set from the goldens. */
static const RoundMode MODES_MUL_ADD[] = { RM_TRUNC, RM_NEAR_EVEN, RM_NEAR_AWAY, RM_AWAY };
static const RoundMode MODES_DIV[]     = { RM_NEAR_EVEN, RM_NEAR_AWAY, RM_AWAY };

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

    /* Every plausible rounding-mode combination; accepted only if all
     * agree on the bytes this directive stores (a .float stores the high
     * four bytes of the double - float_coverage/fltopen.o.golden). */
    size_t size = flt_size(kind);
    size_t from = 8 - size;
    unsigned char first[8], cur[8];
    bool have = false;
    for (int a = 0; a < NELEM(MODES_MUL_ADD); a++) {
        for (int b = 0; b < NELEM(MODES_MUL_ADD); b++) {
            for (int c = 0; c < NELEM(MODES_DIV); c++) {
                ConvResult r = convert(&t, MODES_MUL_ADD[a], MODES_MUL_ADD[b], MODES_DIV[c], cur);
                if (r == CV_ZERO)
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
            return "this zero is not supported (the real assembler's bytes for a zero are known only "
                   "when it has more fraction digits than its exponent, by at most 24 - e.g. "
                   "0.00000000000000000e+00)";
        case FLT_ROUNDING:
            return dbl ? "the real assembler's result depends on how its double arithmetic rounds, "
                         "which is unconfirmed for this value"
                       : "the real assembler's result depends on how its double arithmetic rounds, "
                         "which is unconfirmed for this value (it may be one unit lower in the last place)";
        case FLT_RANGE:
            return dbl ? "out of range for a double (the value, or a step of the real assembler's "
                         "conversion such as 5**k for a large decimal exponent)"
                       : "out of range for a float (the value, or a step of the real assembler's "
                         "conversion such as 5**k for a large decimal exponent)";
    }
    return "invalid";
}
