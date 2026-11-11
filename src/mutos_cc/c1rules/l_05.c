/*
 * L-05 - dmath.o's multiplication uses a wrong partial product, in the
 * conversion of a floating constant (docs/MUTOS_ORIGINAL_BUGS.md, L-05;
 * docs/RULES.md).
 *
 * Hook: c1_atof() (c1port/c1lib.c), where getree() reads a floating
 * constant: libc.a's atof() of its text, on libc.a's floating-point
 * runtime, whose dmul forms its product from one wrong partial product
 * - src/mutos_as/fltconst.c re-enacts it, compiled into c1.
 *
 * The correction (the corrected mode): the conversion on the runtime of
 * the corrected copy of libc.a (src/libc_fixed/fixed/dmath.s, whose dmul
 * rounds the exact product): flt_encode_with(..., FLT_LIBC_FIXED), as
 * mutos_as converts in the corrected mode (its own rule L-05) - so that
 * c1 folds, prints and sizes (fconchk(): a float or a double) the value
 * the copy's atof() gives for the text. What the copy does not correct
 * stays: ldexp()'s wrapped exponent (L-06), the overflow of 5**k (L-07),
 * the bytes of a zero.
 *
 * The rule log: a constant whose double (or whose overflow) the two
 * runtimes give differently - "met" in the original mode, "applied" in
 * the corrected mode (const= its text). The constant is read before the
 * statement that writes it is compiled; its entry waits for the end of
 * that statement (c1_rule_mark()).
 *
 * State: active.
 */
#include "../c1port/c1.h"
#include "../../mutos_as/fltconst.h"

#include <string.h>

int
rule_l_05(const char *text, unsigned char *d)
{
	unsigned char orig[8], fixed[8];
	size_t n = strlen(text);
	FltStatus so, sf;
	int corrected = c1_mode == MUTOS_CORRECTED;

	so = flt_encode_with(FP_DOUBLE, text, n, orig, FLT_LIBC_ORIGINAL);
	sf = flt_encode_with(FP_DOUBLE, text, n, fixed, FLT_LIBC_FIXED);
	if (so != sf || (so == FLT_OK && memcmp(orig, fixed, 8) != 0))
		c1_rule("L-05", corrected, NULL, NULL, "const=%.64s", text);
	if (corrected) {
		if (sf == FLT_OK)
			memcpy(d, fixed, 8);
		return sf;
	}
	if (so == FLT_OK)
		memcpy(d, orig, 8);
	return so;
}
