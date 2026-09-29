/*
 * c1_fltdec.c - the text the real MUTOS 1700 compiler writes for a
 * floating constant (see c1_fltdec.h).
 *
 * THE VALUE is libc.a's atof() of the literal - which is what the real
 * assembler computes too, for every nonzero value: mutos_as's fltconst.c
 * models it exactly (tests/mutos_as/float_coverage/, including libc.a's
 * inexact product in dmul), and its double image is what this module
 * starts from (fltconst.c is compiled into mutos_c1 as it is). A zero -
 * whose bytes the assembler's runtime and libc.a's differ on - is simply
 * zero here.
 *
 * THE TEXT is fltpr.o's _pscien(): ecvt(value, 18, &decpt, &sign), then
 * the first digit, '.', the other 17, 'e', the exponent's sign and two
 * digits of decpt - 1 (decpt itself for a zero, whose first digit is
 * '0'). ecvt() is libc.a's ecvt.o: v7's cvt(arg, ndigits, decpt, sign,
 * eflag=1), compiled - its disassembly is the reference here:
 *
 *     arg = modf(arg, &fi);
 *     if (fi != 0) {                       integer part, last digit first
 *         while (fi != 0) {
 *             fj = modf(fi/10, &fi);       fldd fi / fdivs <10.0>
 *             *--p1 = (int)((fj+.03)*10) + '0';
 *                                          fldd fj / faddd <.03> /
 *                                          fmuls <10.0> / ftoi
 *             r2++;
 *         }
 *         (the digits moved to the front)
 *     } else if (arg > 0)
 *         while ((fj = arg*10) < 1) {      flds <10.0> / fmuld arg
 *             arg = fj; r2--;
 *         }
 *     p1 = &buf[ndigits]; *decpt = r2;
 *     while (p <= p1 && p < &buf[NDIG]) {  one digit more than asked
 *         arg *= 10;                        flds <10.0> / fmuld arg
 *         arg = modf(arg, &fj);
 *         *p++ = (int)fj + '0';
 *     }
 *     *p1 += 5; (carry, "1" and ++*decpt when it runs off the front)
 *     *p1 = '\0';
 *
 * THE ARITHMETIC is dmath.o's on 56-bit mantissas, as tests/mutos_as/
 * float_coverage/fltmodel.py models it and libcatof.py's "ops" checks it
 * against the real code: every result rounded to nearest, ties to even.
 * dmul forms libc.a's inexact product - a*b plus (a0*b1 - a0*b2) * 2**32,
 * a0 the low word of its memory operand's mantissa, b1/b2 the next two of
 * the stack operand's - but every multiplication here has 10.0 on one
 * side, whose words below the top one are zero: with 10.0 in memory
 * (fmuls) a0 is 0, with 10.0 on the stack (fmuld arg) b1 and b2 are - so
 * each product is exact before rounding. The .03 is ecvt.o's own 8-byte
 * constant, c3 f5 28 5c 8f c2 75 7b; modf.o splits a value exactly (it
 * shifts the integer bits out), ftoi truncates. All of it reproduces the
 * golden texts; tests/mutos_as/float_coverage/libcatof.py's "ecvt"
 * subcommand compares this module with libc.a's own ecvt() on atof()'s
 * results under an 8086 emulator.
 */
#include "c1_fltdec.h"
#include "../mutos_as/fltconst.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NDIG 80                  /* ecvt.c's buffer */

/* A nonnegative MUTOS double: m * 2**e, m zero or in [2**55, 2**56). */
typedef struct {
    uint64_t m;
    int      e;
} MF;

static const uint64_t TOP = (uint64_t)1 << 55;

static int mf_zero(MF a)
{
    return a.m == 0;
}

static int bitlen(uint64_t v)
{
    int n = 0;
    while (v) {
        n++;
        v >>= 1;
    }
    return n;
}

/* v * 2**e plus a fraction of 2**e (when sticky), v > 0 and at least 56
 * bits long whenever sticky, rounded to 56 bits, nearest-even. */
static MF mf_round(uint64_t v, int sticky, int e)
{
    int L = bitlen(v);
    MF r;
    if (L <= 56) {
        r.m = v << (56 - L);
        r.e = e - (56 - L);
        return r;
    }
    int s = L - 56;
    uint64_t keep = v >> s;
    uint64_t tail = v & (((uint64_t)1 << s) - 1);
    uint64_t half = (uint64_t)1 << (s - 1);
    if (tail > half || (tail == half && (sticky || (keep & 1)))) {
        keep++;
        if (keep == (uint64_t)1 << 56) {
            keep >>= 1;
            s++;
        }
    }
    r.m = keep;
    r.e = e + s;
    return r;
}

/* The double image dbl[8] (lowest address first): the low 55 mantissa
 * bits in bytes 0..6, the sign in bit 7 of byte 6, the excess-128
 * exponent in byte 7 - 0 for a zero (see src/mutos_as/fltconst.h). */
static MF mf_decode(const unsigned char dbl[8], int *neg)
{
    MF r = { 0, 0 };
    *neg = (dbl[6] & 0x80) != 0;
    if (dbl[7] == 0)
        return r;
    uint64_t low = 0;
    for (int i = 6; i >= 0; i--)
        low = (low << 8) | dbl[i];
    r.m = (low & (TOP - 1)) | TOP;
    r.e = (int)dbl[7] - 128 - 56;
    return r;
}

static MF mf_mul10(MF a)
{
    if (mf_zero(a))
        return a;
    return mf_round(a.m * 10, 0, a.e);       /* below 2**60: exact */
}

static MF mf_div10(MF a)
{
    if (mf_zero(a))
        return a;
    uint64_t v = a.m << 8;                   /* below 2**64 */
    return mf_round(v / 10, v % 10 != 0, a.e - 8);
}

/* a + b, both nonnegative: the exact sum, rounded. */
static MF mf_add(MF a, MF b)
{
    if (mf_zero(a))
        return b;
    if (mf_zero(b))
        return a;
    if (a.e < b.e) {
        MF t = a;
        a = b;
        b = t;
    }
    int d = a.e - b.e;
    uint64_t A = a.m << 7, B, sticky = 0;    /* seven guard bits */
    uint64_t bm = b.m << 7;
    if (d >= 64) {
        B = 0;
        sticky = 1;
    } else {
        B = bm >> d;
        if (d > 0 && (bm & (((uint64_t)1 << d) - 1)))
            sticky = 1;
    }
    return mf_round(A + B, (int)sticky, a.e - 7);
}

/* modf.o: the fraction, the integer part into *ip - exact. */
static MF mf_modf(MF a, MF *ip)
{
    MF zero = { 0, 0 };
    if (mf_zero(a) || a.e >= 0) {
        *ip = a;
        return zero;
    }
    if (a.e <= -56) {
        *ip = zero;
        return a;
    }
    int k = -a.e;                            /* fraction bits, 1..55 */
    uint64_t mask = ((uint64_t)1 << k) - 1;
    ip->m = a.m & ~mask;
    ip->e = a.e;
    uint64_t f = a.m & mask;
    if (f == 0)
        return zero;
    return mf_round(f, 0, a.e);              /* normalized, exact */
}

static int mf_cmp(MF a, MF b)
{
    if (mf_zero(a) || mf_zero(b))
        return mf_zero(a) ? (mf_zero(b) ? 0 : -1) : 1;
    if (a.e != b.e)
        return a.e < b.e ? -1 : 1;
    return a.m < b.m ? -1 : a.m > b.m;
}

/* ftoi: truncated toward zero (only values below 10 reach it). */
static int mf_toint(MF a)
{
    if (mf_zero(a) || a.e <= -64)
        return 0;
    return (int)(a.e >= 0 ? a.m << a.e : a.m >> -a.e);
}

void fdec_ecvt(const unsigned char dbl[8], int ndigits, char *buf,
               int *decpt, int *sign)
{
    static const unsigned char K03[8] = { 0xc3, 0xf5, 0x28, 0x5c,
                                          0x8f, 0xc2, 0x75, 0x7b };
    int neg, kneg;
    MF arg = mf_decode(dbl, &neg), fi, fj;
    MF k03 = mf_decode(K03, &kneg);
    MF one = { TOP, -55 };
    if (ndigits < 0)
        ndigits = 0;
    if (ndigits >= NDIG - 1)
        ndigits = NDIG - 2;
    int r2 = 0, p = 0, p1;
    *sign = neg;
    arg = mf_modf(arg, &fi);
    if (!mf_zero(fi)) {
        p1 = NDIG;
        while (!mf_zero(fi)) {
            fj = mf_modf(mf_div10(fi), &fi);
            buf[--p1] = (char)(mf_toint(mf_mul10(mf_add(fj, k03))) + '0');
            r2++;
        }
        while (p1 < NDIG)
            buf[p++] = buf[p1++];
    } else if (!mf_zero(arg)) {
        while (mf_cmp(fj = mf_mul10(arg), one) < 0) {
            arg = fj;
            r2--;
        }
    }
    p1 = ndigits;                            /* eflag: no "p1 += r2" */
    *decpt = r2;
    if (p1 < 0) {
        buf[0] = '\0';
        return;
    }
    while (p <= p1 && p < NDIG) {
        arg = mf_modf(mf_mul10(arg), &fj);
        buf[p++] = (char)(mf_toint(fj) + '0');
    }
    if (p1 >= NDIG) {
        buf[NDIG - 1] = '\0';
        return;
    }
    p = p1;
    buf[p1] += 5;
    while (buf[p1] > '9') {
        buf[p1] = '0';
        if (p1 > 0) {
            ++buf[--p1];
        } else {
            buf[p1] = '1';
            (*decpt)++;
        }
    }
    buf[p] = '\0';
}

FdecStatus fdec_render(const char *text, int negate, char *out, size_t n,
                       int *is_float, int *is_zero)
{
    unsigned char dbl[8];
    *is_float = 0;
    *is_zero = 0;
    switch (flt_encode(FP_DOUBLE, text, strlen(text), dbl)) {
    case FLT_OK:
        break;
    case FLT_SYNTAX:
        return FDEC_SYNTAX;
    case FLT_RANGE:
        return FDEC_RANGE;
    default:
        return FDEC_UNKNOWN;
    }
    if (dbl[7] == 0) {
        /* A zero: printf's "0.00000000000000000e+00" (ecvt() gives "000...",
         * _pscien() counts the decimal point up to 1). */
        snprintf(out, n, "%s0.00000000000000000e+00", negate ? "-" : "");
        *is_float = 1;
        *is_zero = 1;
        return FDEC_OK;
    }
    /* A result libc.a's ldexp() stored with a wrapped exponent byte (see
     * fltconst.h's RANGE) is not the literal's value: refused, as a
     * value near the ends of the range is - the host's reading decides. */
    double v = strtod(text, NULL);
    if (!(v < 1e38 && v > 1e-38))
        return FDEC_RANGE;
    *is_float = (dbl[0] | dbl[1] | dbl[2] | dbl[3]) == 0;
    char buf[NDIG + 1];
    int decpt, sign;
    fdec_ecvt(dbl, 18, buf, &decpt, &sign);
    int x = decpt - 1;
    snprintf(out, n, "%s%c.%.17se%c%02d", negate ? "-" : "", buf[0], buf + 1,
             x < 0 ? '-' : '+', x < 0 ? -x : x);
    return FDEC_OK;
}
