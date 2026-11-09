/*
 * LD-08 - a text of 65,530 bytes or more wraps
 * (docs/MUTOS_ORIGINAL_BUGS.md, LD-08; docs/RULES.md).
 *
 * Hook: middle() (ld.c), where the six bytes of "jmp $" that finishout()
 * writes are added to the text's size - without add()'s check, so that a
 * text of 65,530 bytes or more wraps (the header says 0 for 65,530) and ld
 * says nothing.
 *
 * The correction (the corrected mode): the six bytes are added with the
 * original's own add() - the hook hands it over -, whose check reports
 * "text overflow" at level 1, as an overflow of pass 1 does: the link ends
 * with exit status 1 and the output is not made executable. The sum is
 * add()'s, the 16-bit one, so the output's bytes are the original's.
 *
 * The original's path is a sum past 65,535: "met" in the original mode,
 * "applied" in the corrected one (tsize=<bytes before the six>).
 *
 * State: active.
 */
#include "ldrules.h"
#include "../ld.h"

uint16_t
rule_ld_08(uint16_t size, int16_t (*add)(int16_t, int16_t, uint16_t))
{
	if ((uint32_t)size + 6 <= 0xffff)
		return (uint16_t)(size + 6);
	if (ld_mode == MUTOS_CORRECTED) {
		mutos_rulelog("ld", "LD-08", "applied", "tsize=%u", (unsigned)size);
		return (uint16_t)add((int16_t)size, 6, S_TEXTOVF);
	}
	mutos_rulelog("ld", "LD-08", "met", "tsize=%u", (unsigned)size);
	return (uint16_t)(size + 6);
}
