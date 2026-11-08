/*
 * LD-08 - a text of 65,530 bytes or more wraps
 * (docs/MUTOS_ORIGINAL_BUGS.md, LD-08; docs/RULES.md).
 *
 * Hook: middle() (ld.c), where the six bytes of "jmp $" that finishout()
 * writes are added to the text's size - without add()'s check, so that a
 * text of 65,530 bytes or more wraps (the header says 0 for 65,530) and ld
 * says nothing.
 *
 * The original's path is a sum past 65,535. State: diagnostic (step F0) -
 * it reports that ("met", tsize=<bytes>) and returns the 16-bit sum, as
 * the original computes it.
 */
#include "ldrules.h"

uint16_t
rule_ld_08(uint16_t size)
{
	if ((uint32_t)size + 6 > 0xffff)
		mutos_rulelog("ld", "LD-08", "met", "tsize=%u", (unsigned)size);
	return (uint16_t)(size + 6);
}
