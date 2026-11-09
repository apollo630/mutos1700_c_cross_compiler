/*
 * LD-06 - a warning clears an earlier error
 * (docs/MUTOS_ORIGINAL_BUGS.md, LD-06; docs/RULES.md).
 *
 * Hook: the end of error() (ld.c), "errlev = n". A warning - "out of date
 * (warning)", n 0 - after an error of level 1 ("No relocation bits", an
 * overflow) sets errlev back to 0: ld ends with exit status 0 and makes
 * the output executable.
 *
 * The correction (the corrected mode): errlev is raised, never lowered -
 * a warning leaves an earlier error's level, so the link ends with exit
 * status 1 and the output is not made executable, as after an error with
 * no warning behind it. What ld writes into the output is the same. One
 * message changes with it: error() puts "ld:" before a message only while
 * errlev is 0, so a message after such a warning no longer gets one - as
 * a message after an error never does.
 *
 * The original's path is a level that goes down: "met" in the original
 * mode, "applied" in the corrected one (from=<level> to=<the warning's>).
 *
 * State: active.
 */
#include "ldrules.h"

int16_t
rule_ld_06(int16_t level, int16_t n)
{
	if (n >= level)
		return n;
	if (ld_mode == MUTOS_CORRECTED) {
		mutos_rulelog("ld", "LD-06", "applied", "from=%d to=%d", level, n);
		return level;
	}
	mutos_rulelog("ld", "LD-06", "met", "from=%d to=%d", level, n);
	return n;
}
