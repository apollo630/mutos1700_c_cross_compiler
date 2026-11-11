/*
 * L-01 - itod writes the low mantissa byte twice more
 * (docs/MUTOS_ORIGINAL_BUGS.md, L-01; docs/RULES.md).
 *
 * Hook: c1_itod() (c1port/c1lib.c), libc.a's itod re-enacted, once it has
 * made its double: an int constant converted to a floating one, where
 * unoptim() folds an ITOF of a constant (then fconchk() makes it a float
 * or a double constant) and where doinit() initializes a float or a
 * double with an int constant. The original's store loop writes AX, whose
 * high byte still holds BL, into bytes 0 to 3: an int with more than
 * eight significant bits becomes a slightly wrong double - and, in c1, an
 * 8-byte constant where a float would do.
 *
 * The correction (the corrected mode): the double the corrected copy of
 * libc.a's itod makes (src/libc_fixed/fixed/convert.s: AX cleared before
 * the store loop) - the int exactly, bytes 0 to 4 zero -, so that c1
 * folds the value the program's itof gives at run time.
 *
 * The rule log: an int whose double the two give differently (BL not 0)
 * - "met" in the original mode, "applied" in the corrected mode (int=).
 *
 * State: active.
 */
#include "../c1port/c1.h"

#include <string.h>

void
rule_l_01(int v, unsigned char *d)
{
	int16_t x = (int16_t)v;
	uint16_t m;
	int e = 0220, i;
	unsigned char fixed[8];

	if (x == 0)
		return;
	m = (uint16_t)(x < 0 ? -x : x);
	while ((m & 0x8000) == 0) {
		m <<= 1;
		e--;
	}
	for (i = 0; i < 5; i++)
		fixed[i] = 0;
	fixed[5] = (unsigned char)m;
	fixed[6] = (unsigned char)((x < 0 ? 0200 : 0) | ((m >> 8) & 0177));
	fixed[7] = (unsigned char)e;
	if (memcmp(fixed, d, 8) == 0)
		return;
	if (c1_mode != MUTOS_CORRECTED) {
		c1_rule("L-01", 0, NULL, NULL, "int=%d", x);
		return;
	}
	memcpy(d, fixed, 8);
	c1_rule("L-01", 1, NULL, NULL, "int=%d", x);
}
