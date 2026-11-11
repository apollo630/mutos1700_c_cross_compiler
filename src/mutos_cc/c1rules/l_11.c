/*
 * L-11 - ftol overflows from 2**30
 * (docs/MUTOS_ORIGINAL_BUGS.md, L-11; docs/RULES.md).
 *
 * Hook: c1_ftol() (c1port/c1lib.c), libc.a's ftol re-enacted, for a
 * double of 2**30 or more in magnitude (an exponent of 31 or more) - a
 * long initialized with a floating constant (doinit()). The original's
 * ftol takes the shift count 0 of 2**30 to 2**31 for an overflow and
 * raises SIGFPE, as it does from 2**31; the port refuses.
 *
 * The correction (the corrected mode): what the corrected copy of
 * libc.a's ftol gives (src/libc_fixed/fixed/lconvert.s) - 2**30 to 2**31
 * in magnitude converted, truncated toward 0, and -2**31 for -2**31 to
 * -2**31-0.99...; SIGFPE for every other value, which the port refuses as
 * before -, so that c1 folds the value the program's ftol gives at run
 * time.
 *
 * The rule log: "applied" in the corrected mode where a value is folded
 * (long=). A refused constant logs nothing: c1 stops there.
 *
 * State: active.
 */
#include "../c1port/c1.h"

int32_t
rule_l_11(const unsigned char *d)
{
	int e = d[7] - 0200;
	uint32_t m;
	int32_t v;

	if (c1_mode != MUTOS_CORRECTED)
		c1trap("ftol overflows (SIGFPE)");
	/* the top 32 mantissa bits, the leading 1 included */
	m = (uint32_t)(0200 | (d[6] & 0177)) << 24 | (uint32_t)d[5] << 16
	    | (uint32_t)d[4] << 8 | d[3];
	if (e != 31 && !(e == 32 && (d[6] & 0200) && m == 0x80000000u))
		c1trap("ftol overflows (SIGFPE)");
	if (e == 32)
		v = INT32_MIN;
	else {
		v = (int32_t)(m >> 1);	/* no shift: the leading 1 is bit 30 */
		if (d[6] & 0200)
			v = -v;
	}
	c1_rule("L-11", 1, NULL, NULL, "long=%ld", (long)v);
	return v;
}
