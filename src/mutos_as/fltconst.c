/*
 * fltconst.c - decimal text -> MUTOS 1700 floating constant, 4 bytes
 * (.float) or 8 bytes (.double). See fltconst.h for the format, the
 * accepted syntax, why only exactly representable values are accepted
 * and which zero is.
 *
 * Method: the text is read as D * 10**E (D an integer of the significant
 * digits, trailing zeros moved into E). D is held as a small bignum;
 * 10**E = 5**E * 2**E, so for E >= 0 the value is (D * 5**E) * 2**E and
 * for E < 0 it is (D / 5**-E) * 2**E - dyadic, and so possibly
 * representable, only if 5**-E divides D exactly. The remaining integer
 * N (made odd by moving its factors of 2 into the binary exponent) must
 * then fit the format's significant bits (24 or 56). No host floating
 * point is involved anywhere, so the result is exact on any host.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fltconst.h"

/* Longest significant-digit string accepted, for either format (the
 * digit buffer's size); each format has its own, lower or equal, limit
 * in FpFormat below. */
#define MAX_SIG_DIGITS 160

/* Decimal-exponent upper bound (after trailing zeros are folded in)
 * above which no constant can be exact: both formats are below
 * 2**127 < 10**39. The lower bound depends on the format (FpFormat). */
#define MAX_DEC_EXP  40

/* Guards the exponent/fraction-digit counters against overflow on
 * absurd input; anything this long is out of range anyway. */
#define COUNT_LIMIT  100000L

/* ------------------------------------------------------------------ */
/* A minimal unsigned bignum: 32-bit limbs, least significant first.   */
/* 32 limbs (1024 bits) exceed the largest intermediate value: 160     */
/* digits (532 bits) times 10**40 (133 bits).                          */
/* ------------------------------------------------------------------ */

#define BIG_LIMBS 32

typedef struct {
    uint32_t w[BIG_LIMBS];
    int      n;              /* limbs in use; w[n-1] != 0 unless n == 0 */
} Big;

static void big_trim(Big *b)
{
    while (b->n > 0 && b->w[b->n - 1] == 0)
        b->n--;
}

/* b = b * m + a. Returns false on overflow of the fixed limb array. */
static bool big_mul_add(Big *b, uint32_t m, uint32_t a)
{
    uint64_t carry = a;
    for (int i = 0; i < b->n; i++) {
        uint64_t t = (uint64_t)b->w[i] * m + carry;
        b->w[i] = (uint32_t)t;
        carry = t >> 32;
    }
    if (carry) {
        if (b->n == BIG_LIMBS)
            return false;
        b->w[b->n++] = (uint32_t)carry;
    }
    return true;
}

/* b = b / d, returning the remainder. */
static uint32_t big_div_small(Big *b, uint32_t d)
{
    uint64_t rem = 0;
    for (int i = b->n - 1; i >= 0; i--) {
        uint64_t cur = (rem << 32) | b->w[i];
        b->w[i] = (uint32_t)(cur / d);
        rem = cur % d;
    }
    big_trim(b);
    return (uint32_t)rem;
}

/* Number of trailing zero bits of a nonzero b. */
static int big_ctz(const Big *b)
{
    int bits = 0;
    int i = 0;
    while (b->w[i] == 0) {
        bits += 32;
        i++;
    }
    uint32_t w = b->w[i];
    while ((w & 1u) == 0) {
        w >>= 1;
        bits++;
    }
    return bits;
}

/* b >>= s, for any s >= 0. */
static void big_shr(Big *b, int s)
{
    int limbs = s / 32, bits = s % 32;
    if (limbs >= b->n) {
        b->n = 0;
        return;
    }
    for (int i = 0; i + limbs < b->n; i++) {
        uint32_t lo = b->w[i + limbs] >> bits;
        uint32_t hi = (bits && i + limbs + 1 < b->n) ? b->w[i + limbs + 1] << (32 - bits) : 0;
        b->w[i] = lo | hi;
    }
    b->n -= limbs;
    big_trim(b);
}

/* Bit length of b (0 for zero). */
static int big_bitlen(const Big *b)
{
    if (b->n == 0)
        return 0;
    uint32_t top = b->w[b->n - 1];
    int bits = 0;
    while (top) {
        top >>= 1;
        bits++;
    }
    return (b->n - 1) * 32 + bits;
}

/* ------------------------------------------------------------------ */
/* The two formats.                                                    */
/* ------------------------------------------------------------------ */

typedef struct {
    int  nbytes;          /* size of one constant: 4 or 8 */
    int  sig_bits;        /* significant bits, the unstored leading 1 included */
    char prefix;          /* the manual's constant prefix after '0', lower case */
    char prefix_exp;      /* exponent letter after that prefix, lower case */
    int  max_sig_digits;  /* longer digit strings cannot be exact */
    long min_dec_exp;     /* lower decimal-exponent bound for an exact value */
    bool zero_confirmed;  /* a zero has confirmed real bytes (FLOAT_ZERO) */
} FpFormat;

/* An exact float has at most 151 binary fraction digits (2**-127 times a
 * 24-bit mantissa), so at most 151 decimal ones, and at most about 113
 * significant digits (2**-151 * (2**24 - 1) is the worst case). An exact
 * double has at most 183 binary fraction digits (2**-127 times a 56-bit
 * mantissa) and at most 145 significant digits (2**-183 * (2**56 - 1)).
 * The float values are the ones this file had before .double existed,
 * kept so every .float text gets the same answer as before. */
static const FpFormat FORMATS[2] = {
    /* FP_FLOAT  */ { 4, 24, 'f', 'e', 120, -160L, true  },
    /* FP_DOUBLE */ { 8, 56, 'd', 'd', 160, -190L, false },
};

/* The real assembler's bytes for a zero .float - see fltconst.h:
 * float_coverage/fltzero.o.golden (".float 0.00000000000000000e+00"),
 * and every zero constant in libc.a's atof.o (data+4, +16) and ecvt.o
 * (+0, +4, +8, +28). Exponent byte 0; the rest is the top of 5**17 =
 * 0xB1A2BC2EC5 without its leading 1. */
static const unsigned char FLOAT_ZERO[4] = { 0xbc, 0xa2, 0x31, 0x00 };

/* The one zero spelling those bytes are confirmed for: "%.17e" of zero,
 * i.e. 17 digits after the '.' and a zero exponent. */
#define ZERO_FRAC_DIGITS 17

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
    return (size_t)FORMATS[kind == FP_DOUBLE].nbytes;
}

FltStatus flt_encode(FpKind kind, const char *s, size_t len, unsigned char *out)
{
    const FpFormat *fmt = &FORMATS[kind == FP_DOUBLE];
    size_t i = 0;
    bool neg = false;
    char exp_letter = 'e';   /* atof's exponent letter: the bare form's */

    /* Optional "0f"/"0F" (.float) or "0d"/"0D" (.double) constant prefix
     * - the manual's spelling; after "0d" the exponent letter is d/D. */
    if (len >= 2 && s[0] == '0' && to_lower(s[1]) == fmt->prefix) {
        i = 2;
        exp_letter = fmt->prefix_exp;
    }
    if (i < len && (s[i] == '+' || s[i] == '-')) {
        neg = (s[i] == '-');
        i++;
    }

    /* Mantissa digits, integer and fraction part as one digit string:
     * leading zeros dropped, zeros after a nonzero digit held back in
     * `pending` until a later nonzero digit shows they are interior
     * (so a long run of trailing zeros never counts against the
     * significant-digit limit). */
    char digits[MAX_SIG_DIGITS];
    int nd = 0;
    long pending = 0;
    long frac = 0;          /* digits after the '.' */
    int ndigits_seen = 0;   /* any digit at all, for the syntax check */
    bool dot = false;

    for (; i < len; i++) {
        char c = s[i];
        if (c == '.') {
            if (dot)
                break;
            dot = true;
            continue;
        }
        if (!is_digit(c))
            break;
        ndigits_seen = 1;
        if (dot) {
            if (frac >= COUNT_LIMIT)
                return FLT_RANGE;
            frac++;
        }
        if (c == '0') {
            if (nd > 0) {
                if (pending >= COUNT_LIMIT)
                    return FLT_RANGE;
                pending++;
            }
            continue;
        }
        if (nd + pending + 1 > fmt->max_sig_digits)
            return FLT_RANGE;
        while (pending > 0) {
            digits[nd++] = 0;
            pending--;
        }
        digits[nd++] = (char)(c - '0');
    }
    if (!ndigits_seen)
        return FLT_SYNTAX;

    /* Optional exponent. */
    long exp10 = 0;
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
            if (exp10 < COUNT_LIMIT)
                exp10 = exp10 * 10 + (s[i] - '0');
        }
        if (eneg)
            exp10 = -exp10;
    }
    if (i != len)
        return FLT_SYNTAX;

    /* Zero: only the confirmed .float spelling (see fltconst.h). */
    if (nd == 0) {
        if (fmt->zero_confirmed && !neg && frac == ZERO_FRAC_DIGITS && exp10 == 0) {
            memcpy(out, FLOAT_ZERO, sizeof FLOAT_ZERO);
            return FLT_OK;
        }
        return FLT_ZERO;
    }

    /* value = D * 10**E */
    long e = exp10 - frac + pending;
    if (e > MAX_DEC_EXP || e < fmt->min_dec_exp)
        return FLT_RANGE;

    Big n;
    memset(&n, 0, sizeof(n));
    for (int k = 0; k < nd; k++)
        if (!big_mul_add(&n, 10, (uint32_t)digits[k]))
            return FLT_RANGE;

    /* value = N * 2**b2 */
    long b2 = e;
    if (e >= 0) {
        for (long k = 0; k < e; k++)
            if (!big_mul_add(&n, 5, 0))
                return FLT_RANGE;
    } else {
        for (long k = 0; k < -e; k++)
            if (big_div_small(&n, 5) != 0)
                return FLT_INEXACT;
    }

    int tz = big_ctz(&n);
    big_shr(&n, tz);
    b2 += tz;

    int bits = big_bitlen(&n);
    if (bits > fmt->sig_bits)
        return FLT_INEXACT;

    /* value = 0.1mmm...(binary) * 2**x, the mantissa N/2**bits. */
    long x = b2 + bits;
    if (x + 128 < 1 || x + 128 > 255)
        return FLT_RANGE;

    /* The mantissa left-aligned in sig_bits (at most 56) bits, its top
     * bit - the leading 1 - set; N has at most two limbs here. */
    uint64_t m = (uint64_t)n.w[0] | ((n.n > 1 ? (uint64_t)n.w[1] : 0) << 32);
    m <<= fmt->sig_bits - bits;

    /* Low bytes of the mantissa, lowest first; in the byte below the
     * exponent, the leading 1's place holds the sign instead. */
    int top = fmt->nbytes - 2;
    for (int k = 0; k <= top; k++)
        out[k] = (unsigned char)((m >> (8 * k)) & 0xFF);
    out[top] = (unsigned char)((out[top] & 0x7F) | (neg ? 0x80 : 0));
    out[top + 1] = (unsigned char)(x + 128);
    return FLT_OK;
}

const char *flt_status_text(FltStatus st, FpKind kind)
{
    bool dbl = (kind == FP_DOUBLE);
    switch (st) {
        case FLT_OK:      return "ok";
        case FLT_SYNTAX:  return "not a floating-point number";
        case FLT_ZERO:
            return dbl ? "zero is not supported in .double (the real assembler's bytes for it are unconfirmed)"
                       : "this zero is not supported (the real assembler's bytes are confirmed only for "
                         "0.00000000000000000e+00: 17 fraction digits, exponent 0, no minus sign)";
        case FLT_INEXACT:
            return dbl ? "not exactly representable as a double (the real assembler's rounding is unconfirmed)"
                       : "not exactly representable as a float (the real assembler's rounding is unconfirmed)";
        case FLT_RANGE:   return dbl ? "out of range for a double" : "out of range for a float";
    }
    return "invalid";
}
