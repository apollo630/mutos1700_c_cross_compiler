/*
 * L-03 - dtoi compares with memory instead of a constant
 * (docs/MUTOS_ORIGINAL_BUGS.md, L-03; docs/RULES.md).
 *
 * Hook: c1_dtoi() (c1port/c1lib.c), libc.a's dtoi re-enacted, for a
 * double from 2**15 to 2**16 in magnitude (an exponent of 16) - an int
 * initialized with a floating constant (doinit()). There the original
 * compares the value's top sixteen bits with the word at address 8000 of
 * c1's own data segment, where an immediate 8000 was meant: what it gives
 * depends on state the port does not model, and the port refuses.
 *
 * The correction (the corrected mode): what the corrected copy of
 * libc.a's dtoi gives (src/libc_fixed/fixed/convert.s: the immediate
 * 0x8000, the test of the first fraction bit left out) - -32768 for
 * -32768 to -32768.99..., SIGFPE for every other value, which the port
 * refuses as it refuses dtoi's overflow from 2**16 -, so that c1 folds
 * the value the program's ftoi gives at run time.
 *
 * The rule log: "applied" in the corrected mode where a value is folded
 * (-32768). A refused constant logs nothing: c1 stops there.
 *
 * State: active.
 */
#include "../c1port/c1.h"

int
rule_l_03(const unsigned char *d)
{
	if (c1_mode != MUTOS_CORRECTED)
		c1trap("dtoi of a magnitude of 2**15 or more (the original "
		    "compares with the word at address 8000)");
	/* the top sixteen bits, sign included, as the copy compares them */
	if (d[6] != 0200 || d[5] != 0)
		c1trap("dtoi overflows (SIGFPE)");
	c1_rule("L-03", 1, NULL, NULL, "int=-32768");
	return -32768;
}
