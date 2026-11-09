/*
 * LD-09 - -i and -n round a text of 65,468 bytes or more to 0
 * (docs/MUTOS_ORIGINAL_BUGS.md, LD-09; docs/RULES.md).
 *
 * Hook: middle() (ld.c), where the text - the six bytes of "jmp $"
 * already added (LD-08) - is rounded up to 64 bytes for -i and -n:
 * (tsize + 077) & ~077, without add()'s check, so that from 65,473 bytes
 * the sum wraps to 0 and ld says nothing.
 *
 * The correction (the corrected mode): the 077 is added with the
 * original's own add() - the hook hands it over -, whose check reports
 * "text overflow" at level 1, as LD-08's correction does for the six
 * bytes: the link ends with exit status 1 and the output is not made
 * executable. The rounded size is the original's, so the output's bytes
 * are the original's.
 *
 * The original's path is a rounding past 65,535: "met" in the original
 * mode, "applied" in the corrected one (tsize=<bytes before the rounding>).
 *
 * State: active.
 */
#include "ldrules.h"
#include "../ld.h"

uint16_t
rule_ld_09(uint16_t size, int16_t (*add)(int16_t, int16_t, uint16_t))
{
	if ((uint32_t)size + 077 <= 0xffff)
		return (uint16_t)((size + 077) & ~077);
	if (ld_mode == MUTOS_CORRECTED) {
		mutos_rulelog("ld", "LD-09", "applied", "tsize=%u", (unsigned)size);
		return (uint16_t)(add((int16_t)size, 077, S_TEXTOVF) & ~077);
	}
	mutos_rulelog("ld", "LD-09", "met", "tsize=%u", (unsigned)size);
	return (uint16_t)((size + 077) & ~077);
}
