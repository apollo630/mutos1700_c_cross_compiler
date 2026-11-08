/*
 * LD-06 - a warning clears an earlier error
 * (docs/MUTOS_ORIGINAL_BUGS.md, LD-06; docs/RULES.md).
 *
 * Hook: the end of error() (ld.c), "errlev = n". A warning - "out of date
 * (warning)", n 0 - after an error of level 1 ("No relocation bits", an
 * overflow) sets errlev back to 0: ld ends with exit status 0 and makes
 * the output executable.
 *
 * The original's path is a level that goes down. State: diagnostic (step
 * F0) - it reports that ("met", from=<level> to=<level>) and returns n,
 * as the original sets it.
 */
#include "ldrules.h"

int16_t
rule_ld_06(int16_t level, int16_t n)
{
	if (n < level)
		mutos_rulelog("ld", "LD-06", "met", "from=%d to=%d", level, n);
	return n;
}
