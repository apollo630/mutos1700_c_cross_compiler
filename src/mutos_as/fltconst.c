/*
 * fltconst.c - decimal text -> MUTOS 1700 4-byte floating constant.
 * See fltconst.h for the format, the accepted syntax and why only
 * exactly representable values are accepted.
 *
 * Method: the text is read as D * 10**E (D an integer of the significant
 * digits, trailing zeros moved into E). D is held as a small bignum;
 * 10**E = 5**E * 2**E, so for E >= 0 the value is (D * 5**E) * 2**E and
 * for E < 0 it is (D / 5**-E) * 2**E - dyadic, and so possibly
 * representable, only if 5**-E divides D exactly. The remaining integer
 * N (made odd by moving its factors of 2 into the binary exponent) must
 * then fit the float's 24 significant bits. No host floating point is
 * involved anywhere, so the result is exact on any host.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "fltconst.h"

/* Longest significant-digit string accepted. Any exactly representable
 * float needs fewer: its exact decimal expansion is at most about 113
 * significant digits (2**-151 * (2**24 - 1) is the worst case). */
#define MAX_SIG_DIGITS 120

/* Decimal-exponent bounds (after trailing zeros are folded in) outside
 * which no float can be exact: a float is below 2**127 < 10**39, and an
 * exact one has at most 151 binary fraction digits, so at most 151
 * decimal ones. */
#define MAX_DEC_EXP  40
#define MIN_DEC_EXP  (-160)

/* Guards the exponent/fraction-digit counters against overflow on
 * absurd input; anything this long is out of range anyway. */
#define COUNT_LIMIT  100000L

/* ------------------------------------------------------------------ */
/* A minimal unsigned bignum: 32-bit limbs, least significant first.   */
/* 32 limbs (1024 bits) exceed the largest intermediate value: 120     */
/* digits (399 bits) times 10**40 (133 bits).                          */
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

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

FltStatus flt_encode(const char *s, size_t len, unsigned char out[4])
{
    size_t i = 0;
    bool neg = false;

    /* Optional "0f"/"0F" float-constant prefix (the manual's spelling). */
    if (len >= 2 && s[0] == '0' && (s[1] == 'f' || s[1] == 'F'))
        i = 2;
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
        if (nd + pending + 1 > MAX_SIG_DIGITS)
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
    if (i < len && (s[i] == 'e' || s[i] == 'E')) {
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

    if (nd == 0)
        return FLT_ZERO;

    /* value = D * 10**E */
    long e = exp10 - frac + pending;
    if (e > MAX_DEC_EXP || e < MIN_DEC_EXP)
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
    if (bits > 24)
        return FLT_INEXACT;

    /* value = 0.1mmm...(binary) * 2**x, the mantissa N/2**bits. */
    long x = b2 + bits;
    if (x + 128 < 1 || x + 128 > 255)
        return FLT_RANGE;

    uint32_t f = n.w[0] << (24 - bits);   /* 24 bits, top bit set */
    out[0] = (unsigned char)(f & 0xFF);
    out[1] = (unsigned char)((f >> 8) & 0xFF);
    out[2] = (unsigned char)(((f >> 16) & 0x7F) | (neg ? 0x80 : 0));
    out[3] = (unsigned char)(x + 128);
    return FLT_OK;
}

const char *flt_status_text(FltStatus st)
{
    switch (st) {
        case FLT_OK:      return "ok";
        case FLT_SYNTAX:  return "not a floating-point number";
        case FLT_ZERO:    return "zero is not supported (the real assembler's bytes for it are unconfirmed)";
        case FLT_INEXACT: return "not exactly representable as a float (the real assembler's rounding is unconfirmed)";
        case FLT_RANGE:   return "out of range for a float";
    }
    return "invalid";
}
