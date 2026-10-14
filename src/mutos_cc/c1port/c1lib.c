/*
 * c1lib.c - the original c1's library, where its output passes through
 * it (docs/C1_PORT_PLAN.md, rule 4): libc.a's atof() for a floating
 * constant's text, through src/mutos_as/fltconst.c, which re-enacts it
 * on libc.a's 56-bit software double; itod, ltof, dtoi, ftol and fneg for
 * the constant conversions; aldiv and alrem for lconst()'s long '/' and
 * '%'; printf's "%.17e" (through src/mutos_cc/c1_fltdec.c's ecvt()) and
 * its "%c" of 0. Where a routine's result is not what C would compute,
 * the comment says so; where it depends on state the port does not
 * model, the port refuses (c1trap()).
 */
#define _POSIX_C_SOURCE 200809L	/* fileno(), ftruncate() */

#include "c1.h"
#include "../../mutos_as/fltconst.h"
#include "../c1_fltdec.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * atof(text) into a floating constant's fvalue - what getree()'s FCON
 * case does. Where libc.a's atof() raises SIGFPE (an operation of its
 * arithmetic overflows - fltconst.h's FLT_RANGE), this calls the
 * original's handler, fpeintr(), which reports the overflow and
 * longjmp()s back into getree(). The handler is installed once and not
 * re-armed, and the kernel resets a caught signal to its default (v7's
 * semantics): a second overflow kills the original c1. Its .s file then
 * holds what stdio had written of it - whole 512-byte buffers - and
 * nothing is printed; the port leaves the same and exits with 128 +
 * SIGFPE, the status a shell reports for such a process (both settled
 * under tests/realbin/mutosemu.py, which delivers the signal).
 *
 * A text whose bytes fltconst.c cannot pin down (FLT_ROUNDING,
 * FLT_UNKNOWN) or does not accept is refused: the port does not guess.
 */
void
c1_atof(struct tnode *p, const char *text)
{
	static int fpecount;

	switch (flt_encode(FP_DOUBLE, text, strlen(text), p->fvalue)) {
	case FLT_OK:
		return;
	case FLT_RANGE:
		if (fpecount++ == 0)
			fpeintr(SIGFPE);
		fflush(stdout);
		if (ftruncate(fileno(stdout), ftell(stdout) & ~511L) != 0)
			perror("c1: .s file");
		fflush(stderr);
		_exit(128 + SIGFPE);
	default:
		fprintf(stderr, "%d: c1: floating constant \"%s\": its value "
		    "in libc.a's atof() is not known exactly - refused\n",
		    line, text);
		exit(2);
	}
}

/*
 * A MUTOS double from a nonzero integer, exactly: the value
 * 0.1mmm...(binary) * 2**(e - 128), the exponent byte e at byte 7, the
 * sign in bit 7 of byte 6, the mantissa after its leading 1 in bytes 6
 * (low seven bits) down to 0 - what libc.a's itod and ltof store.
 */
static void
dblint(uint32_t mag, int neg, unsigned char *d)
{
	int e = 32;
	uint64_t m;
	int i;

	while ((mag & 0x80000000u) == 0) {
		mag <<= 1;
		e--;
	}
	m = (uint64_t)mag << 24;	/* the leading 1 at bit 55 */
	for (i = 0; i < 7; i++)
		d[i] = (unsigned char)(m >> (8 * i));
	d[6] = (unsigned char)((d[6] & 0177) | (neg ? 0200 : 0));
	d[7] = (unsigned char)(128 + e);
}

/*
 * libc.a's itod (text 7972): int to double. The magnitude is normalized
 * in a 16-bit register (BX, its top bit the leading 1, the exponent
 * counted down from 0220); the double's top word gets the sign, BH's low
 * seven bits and the exponent, byte 5 BL - and then the store loop
 * ("zextend") writes AX, which still holds BL in its high byte, into the
 * two words below: bytes 0 to 3 are 0, BL, 0, BL, not 0 (an itod bug
 * the port keeps: an int with more than eight significant bits becomes
 * a double, not a float, constant). 0 is eight 0 bytes.
 */
void
c1_itod(int v, unsigned char *d)
{
	int16_t x = (int16_t)v;
	uint16_t bx;
	int e = 0220;

	if (x == 0) {
		memset(d, 0, 8);
		return;
	}
	bx = (uint16_t)(x < 0 ? -x : x);
	while ((bx & 0x8000) == 0) {
		bx <<= 1;
		e--;
	}
	d[7] = (unsigned char)e;
	d[6] = (unsigned char)((x < 0 ? 0200 : 0) | ((bx >> 8) & 0177));
	d[5] = (unsigned char)bx;
	d[4] = 0;
	d[3] = (unsigned char)bx;
	d[2] = 0;
	d[1] = (unsigned char)bx;
	d[0] = 0;
}

/*
 * libc.a's ltof (text 7a6b): long to double, exact. For 0 it clears only
 * the top word (bytes 6 and 7) of its floating-point stack slot, leaving
 * the other six bytes as the slot's last value left them - not modelled:
 * refused.
 */
void
c1_ltof(int32_t v, unsigned char *d)
{
	if (v == 0)
		c1trap("ltof of 0 (the result's low six bytes are left over in "
		    "libc.a's floating-point stack)");
	dblint(v < 0 ? 0u - (uint32_t)v : (uint32_t)v, v < 0, d);
}

/*
 * A long in two 16-bit words negated as libc.a does it: "neg hi" / "neg
 * lo" / "sbb hi,0".
 */
static void
neg32(uint16_t *hi, uint16_t *lo)
{
	uint32_t v = 0u - ((uint32_t)*hi << 16 | *lo);

	*hi = (uint16_t)(v >> 16);
	*lo = (uint16_t)v;
}

/*
 * libc.a's aldiv (text 7cb4), "l /= r" for a long - what lconst() folds
 * a long constant '/' with - re-enacted on its 16-bit words, because it
 * is not C's division:
 *   - its short path (two "div"s by one word) is taken when the
 *     divisor's high word is what it takes for the sign extension of the
 *     low word - but with the 0xffff of a negative one negated to 1: a
 *     divisor 0 to 32767 is divided by exactly, one from 0x18000 to
 *     0x1ffff by its low word alone, and a negative one of one word
 *     (-1 to -32768) goes the long way;
 *   - the long way shifts and subtracts sixteen times, comparing the
 *     high words signed, and clears the quotient's high word: right for
 *     a divisor from 65536 up in magnitude, the low sixteen bits of the
 *     quotient below that (0x7fffffff / -1 is -65535; fuzzing found it,
 *     tests/realbin/c1fuzz.py).
 */
int32_t
c1_aldiv(int32_t l, int32_t r)
{
	uint16_t hi = (uint16_t)((uint32_t)l >> 16), lo = (uint16_t)l;
	uint16_t rhi = (uint16_t)((uint32_t)r >> 16), rlo = (uint16_t)r;
	uint16_t sign, ax, dx, borrow, q, rem;
	int i;

	dx = (rlo & 0x8000) ? 1 : 0;		/* cwd, neg dx */
	if (dx == rhi) {
		sign = dx;
		if (hi & 0x8000) {
			neg32(&hi, &lo);
			sign = (uint16_t)~sign;
		}
		q = hi / rlo;
		rem = hi % rlo;
		lo = (uint16_t)((((uint32_t)rem << 16) | lo) / rlo);
		hi = q;
	} else {
		sign = 0;
		if (hi & 0x8000) {
			sign = (uint16_t)~sign;
			neg32(&hi, &lo);
		}
		ax = 0;
		dx = rhi;
		if (dx & 0x8000) {
			neg32(&dx, &rlo);
			sign = (uint16_t)~sign;
		}
		for (i = 0; i < 16; i++) {
			ax = (uint16_t)(ax << 1 | hi >> 15);	/* shl si, rcl di, */
			hi = (uint16_t)(hi << 1 | lo >> 15);	/* rcl ax */
			lo = (uint16_t)(lo << 1);
			if ((int16_t)dx > (int16_t)ax ||
			    ((int16_t)dx == (int16_t)ax && rlo > hi))
				continue;
			borrow = hi < rlo;
			hi = (uint16_t)(hi - rlo);
			ax = (uint16_t)(ax - dx - borrow);
			lo++;
		}
		hi = 0;
	}
	if (sign & 0x8000)
		neg32(&hi, &lo);
	return (int32_t)((uint32_t)hi << 16 | lo);
}

/*
 * libc.a's alrem (text 7d9a), "l %= r" for a long: exact for a divisor
 * of one word (-32768 to 32767, its short path); otherwise the same
 * sixteen shift-and-subtract steps as aldiv's long way, which leave the
 * right remainder only where the quotient has sixteen bits or fewer. The
 * sign is the dividend's.
 */
int32_t
c1_alrem(int32_t l, int32_t r)
{
	uint16_t hi = (uint16_t)((uint32_t)l >> 16), lo = (uint16_t)l;
	uint16_t rhi = (uint16_t)((uint32_t)r >> 16), rlo = (uint16_t)r;
	uint16_t ax, dx, cx, borrow, rem;
	int i;

	dx = (rlo & 0x8000) ? 0xffff : 0;	/* cwd */
	ax = dx ? (uint16_t)-rlo : rlo;
	if (dx == rhi) {
		cx = ax;
		if (hi & 0x8000)
			neg32(&hi, &lo);
		rem = hi % cx;
		rem = (uint16_t)((((uint32_t)rem << 16) | lo) % cx);
		if ((uint32_t)l & 0x80000000u)
			rem = (uint16_t)-rem;
		return (int16_t)rem;		/* cwd */
	}
	if (hi & 0x8000)
		neg32(&hi, &lo);
	ax = 0;
	dx = rhi;
	if (dx & 0x8000)
		neg32(&dx, &rlo);
	for (i = 0; i < 16; i++) {
		ax = (uint16_t)(ax << 1 | hi >> 15);
		hi = (uint16_t)(hi << 1 | lo >> 15);
		lo = (uint16_t)(lo << 1);
		if ((int16_t)dx > (int16_t)ax ||
		    ((int16_t)dx == (int16_t)ax && rlo > hi))
			continue;
		borrow = hi < rlo;
		hi = (uint16_t)(hi - rlo);
		ax = (uint16_t)(ax - borrow - dx);
	}
	if ((uint32_t)l & 0x80000000u)
		neg32(&ax, &hi);
	return (int32_t)((uint32_t)ax << 16 | hi);
}

/*
 * fldd / fneg / fstdp (text 73e1): the sign bit, bit 7 of byte 6, flipped.
 */
void
c1_fneg(unsigned char *d)
{
	d[6] ^= 0200;
}

/*
 * Where the original traps - an 8086 division fault, or a library
 * routine whose result depends on state the port does not model - the
 * port stops with a message rather than guess. (The original's divide
 * fault is a SIGFPE, which its fpeintr() catches once and answers with a
 * longjmp() into whatever getree() last set up.)
 */
void
c1trap(const char *what)
{
	fflush(stdout);
	fprintf(stderr, "%d: c1: %s - the original traps or depends on state "
	    "the port does not model here; refused\n", line, what);
	exit(2);
}

/*
 * printf("%.17e") of a MUTOS double, as libc.a's printf (fltpr.o's
 * _pscien over ecvt() for 18 digits - src/mutos_cc/c1_fltdec.c's
 * fdec_ecvt()) writes it. A zero (exponent byte 0) is
 * "0.00000000000000000e+00" whatever its sign bit (fltprobe/p16_open2,
 * p18_fltop3: ecvt()'s "arg < 0" is false for it).
 */
void
c1_pfloat(const unsigned char *d)
{
	char buf[96];
	int decpt, sign, x;

	if (d[7] == 0) {
		printf("0.00000000000000000e+00");
		return;
	}
	fdec_ecvt(d, 18, buf, &decpt, &sign);
	x = decpt - 1;
	printf("%s%c.%.17se%c%02d", sign ? "-" : "", buf[0], buf + 1,
	    x < 0 ? '-' : '+', x < 0 ? -x : x);
}

/*
 * libc.a's dtoi (text 79c9): truncated toward 0. From 2**16 in magnitude
 * it raises SIGFPE; at 2**15 it compares with the word at address 8000
 * (a bug: an immediate was meant) - both refused.
 */
int
c1_dtoi(const unsigned char *d)
{
	int e = d[7];
	int v;

	if (e == 0)
		return 0;
	e -= 0200;
	if (e <= 0)
		return 0;
	if (e > 16)
		c1trap("dtoi overflows (SIGFPE)");
	if (e == 16)
		c1trap("dtoi of a magnitude of 2**15 or more (the original "
		    "compares with the word at address 8000)");
	v = ((0200 | (d[6] & 0177)) << 8 | d[5]) >> (16 - e);
	if (d[6] & 0200)
		v = -v;
	return (int16_t)v;
}

/*
 * libc.a's ftol (text 7a18): truncated toward 0; from 2**31 in magnitude
 * it raises SIGFPE - refused.
 */
int32_t
c1_ftol(const unsigned char *d)
{
	int e = d[7];
	uint32_t m;

	if (e == 0)
		return 0;
	e -= 0200;
	if (e <= 0)
		return 0;
	if (e >= 31)
		c1trap("ftol overflows (SIGFPE)");
	m = (uint32_t)(0200 | (d[6] & 0177)) << 24 | (uint32_t)d[5] << 16
	    | (uint32_t)d[4] << 8 | d[3];
	m >>= 32 - e;
	if (d[6] & 0200)
		return (int32_t)(0 - m);
	return (int32_t)m;
}

/*
 * regname[r]. A register number outside 0..7 makes the original read the
 * word beside the table as a name's pointer and print whatever it points
 * at - regname[-1], the register "after" AX, is _ctype_ (p19_open3,
 * tests/mutos_cc/invalid_goldens.txt), regname[8] some table's bytes:
 * refused, the number in the message (tests/realbin/c1fuzz.py checks the
 * original wrote those bytes).
 */
char *
rname(int r)
{
	char what[128];

	if (r < 0 || r > 7) {
		snprintf(what, sizeof what, "a register number outside "
		    "regname[], %d (the original prints the bytes the word "
		    "beside the table points at)", r);
		c1trap(what);
	}
	return regname[r];
}

/*
 * cexpr()'s offset of an indirection, "%c%d." with '*' for -128..127 and
 * the character 0 otherwise (text 1574). libc.a's printf writes nothing
 * for a %c of 0 (docs/C1_PORT_PLAN.md 5.1: settled by running a program
 * built with the real toolchain under tests/realbin/mutosemu.py -
 * printf("[%c%d.]", 0, 160) gives "[160.]").
 */
void
pnum0(int v)
{
	if ((int16_t)v >= -128 && (int16_t)v <= 127)
		printf("*%d.", (int16_t)v);
	else
		printf("%d.", (int16_t)v);
}
