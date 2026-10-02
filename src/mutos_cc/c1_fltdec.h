/*
 * c1_fltdec.h - the text the real MUTOS 1700 compiler writes for a
 * floating constant, and whether it stores it in 4 bytes or 8.
 *
 * The real c1 is v7's, running on MUTOS 1700 and linked with libc.a: it
 * converts a constant's source text with libc.a's atof() and writes it
 * with printf("%.17e"), which is fltpr.o's _pscien() calling libc.a's
 * ecvt() (v7's ecvt.c, compiled) for 18 digits - all of it on libc.a's
 * 56-bit software double. fltprobe/p4_const.s.golden shows the result
 * for inexact values ("0.1" -> ".double 1.00000000000000000e-01",
 * "3.14159265358979" -> "3.14159265358979001e+00", "1e30" ->
 * "1.00000000000000005e+30") and for large exact ones ("72057594037927936."
 * -> ".float 7.20575940379279360e+16"); a value whose double has zero low
 * four bytes - exactly a float - is a 4-byte ".float", any other an
 * 8-byte ".double". See c1_fltdec.c for the model.
 */
#ifndef MUTOS_C1_FLTDEC_H
#define MUTOS_C1_FLTDEC_H

#include <stddef.h>

typedef enum {
    FDEC_OK = 0,
    FDEC_SYNTAX,    /* not a floating literal */
    FDEC_RANGE,     /* libc.a's atof() overflows in its arithmetic (SIGFPE
                     * - the real compiler would die), or the value is out
                     * of the format's range */
    FDEC_UNKNOWN    /* a text whose conversion is not known exactly */
} FdecStatus;

/* Renders the floating literal text[] (digits, '.', an exponent - no
 * sign), negated when `negate`, as the real compiler writes it: the
 * "%.17e" text into out[0..n), *is_float = 1 for a 4-byte ".float",
 * 0 for an 8-byte ".double", *is_zero for a zero. */
FdecStatus fdec_render(const char *text, int negate, char *out, size_t n,
                       int *is_float, int *is_zero);

/* The text of a 'float' variable's initializer: the real c1's doinit()
 * converts the double to a float ("sfval = fval" - the high half of its
 * image kept, the low four bytes dropped: truncated) and prints that with
 * "%.17e" - fltprobe/p9_init.s.golden's "float gy = 0.1;" -> ".float
 * 9.99999940395355225e-02". Otherwise as fdec_render(). */
FdecStatus fdec_render_single(const char *text, int negate, char *out,
                              size_t n, int *is_zero);

/* The digits libc.a's ecvt() gives for the MUTOS double image dbl[8]
 * (lowest address first) and `ndigits` digits - into buf (at least 80
 * bytes) - with *decpt and *sign as ecvt() sets them. For tests. */
void fdec_ecvt(const unsigned char dbl[8], int ndigits, char *buf,
               int *decpt, int *sign);

#endif /* MUTOS_C1_FLTDEC_H */
