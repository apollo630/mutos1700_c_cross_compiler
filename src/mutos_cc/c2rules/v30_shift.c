/*
 * V30-SHIFT in c2 - the length of "sal x,*n." when a branch is measured
 * (docs/TOOLCHAIN_PLAN.md, section 7.4, step V2; docs/RULES.md).
 *
 * The rule itself is c1's (c1rules/v30_shift.c): under -1, "mov cx,*n." /
 * "sal x,cl" is written as "sal x,*n." (C1 /r ib; sar, shr; C0 for a
 * byte). c2 changes nothing in such an instruction, but shortbr() must
 * know its length.
 *
 * Hook: toofar() (c2port/c21.c), the case of the shifts. The original
 * takes the length from the count's text - "cl", "*1" or "*n.", no
 * register and no parenthesis: 4 bytes, whatever is shifted. That is
 * the 8086's longest shift (D3 /r with a word of address) and enough for
 * the shift of a register by a constant (3) and of an operand with a
 * byte of displacement (4); with a word of address - "sal _x,*n.", "sal
 * #300.(bp),*n." - the form has 5. Under -1 the hook adds what is
 * missing from the original's count.
 *
 * Without -1 the hook adds nothing - c2 is the original then, whatever
 * its input holds.
 *
 * The rule log: "applied" for a shift by a constant other than 1 measured
 * under -1, "met" without -1; len= is the length counted.
 *
 * State: active.
 */
#include "../c2port/c2.h"

/*
 * q: a SAL, SAR or SHR node, regs[RT2] its destination and regs[RT1] its
 * count (toofar()'s dualop()); base is what the original has counted for
 * it. Returns the bytes to add.
 */
int
rule_v30_shift(uint16_t q, int base)
{
	uint16_t n = REG(RT1);
	int len;

	if (rb(n) != '*')
		return 0;			/* by CL */
	if (rb((uint16_t)(n + 1)) == '1' && rb((uint16_t)(n + 2)) == 0)
		return 0;			/* by 1: the 8086's */
	/* the opcode, the mod r/m byte, a displacement and the count */
	len = 2 + c2_displen(REG(RT2)) + 1;
	c2_rule("V30-SHIFT", c2_v30, q, "len=%d", c2_v30 ? len : base);
	if (!c2_v30 || len <= base)
		return 0;
	return len - base;
}
