/*
 * V30-PUSH - "mov r,constant" / "push r" becomes "push constant"
 * (docs/TOOLCHAIN_PLAN.md, section 7.4, step V1; docs/RULES.md).
 *
 * Hook: pushopt() (c2port/c21.c). The original makes "mov r,x" / "push r"
 * into "push x" when r is not read before it is written again (a call
 * writes every register) and r is no register variable - but not for a
 * constant x ("*n." or "#..." without a register in parentheses): the
 * 8086 has no push of a constant. The 80186 and the V30 have (6A ib,
 * the byte sign-extended; 68 iw), and neither form changes a flag, as
 * "mov" and "push" of a register change none. The hook stands where
 * pushopt() has found the pair with a constant and has seen that r is not
 * read again - the original's own test, on its own masks (regmask()) -:
 * under -1 the rule lets it make the push; without -1 pushopt() leaves
 * the pair, as the original does.
 *
 * Not every constant: "*n." is a byte for mutos_as, which "mov r,*n."
 * takes as the word n whatever n is, and "push *n." as the byte n
 * sign-extended. c1 writes "*n." only for n from -128 to 127; for any
 * other text after the "*" (a number outside that range, a symbol) the
 * rule leaves the pair, and says nothing.
 *
 * What it changes besides the two nodes (c2rules.h, c2_rule(): the
 * chunk's entry in the rule log):
 *   - the lengths shortbr() counts. toofar() takes "mov r,*n." for 4
 *     bytes and "push r" for 1, "push *n." for 4 (it is 2, or 3 with
 *     "#"): never less than the instruction has, so a branch c2 makes
 *     short still reaches - and a branch over the push may become short
 *     that was long;
 *   - what the passes make of the function that run again after
 *     pushopt() (main()'s loop): with one node where there were two,
 *     and r no longer holding the constant, they may join other common
 *     code, move another block or turn another branch around (measured:
 *     in 5 of 600 generated programs of calls, tests/rules/check_v30.py).
 * For c2 the push of a constant is a push of an operand without a
 * register, as "push _x" is, which the original makes itself.
 *
 * The rule log: "applied" for the push made under -1, "met" for the pair
 * left without -1 where -1 would make the push; x= is the constant.
 *
 * State: active.
 */
#include "../c2port/c2.h"

/* whether the constant at s (in the image) can be pushed as it is */
static int
pushable(uint16_t s)
{
	int n = 0, neg = 0, digits = 0;

	if (rb(s) != '*')
		return 1;		/* "#...": a word */
	s++;
	if (rb(s) == '-') {
		neg = 1;
		s++;
	}
	for (; rb(s) >= '0' && rb(s) <= '9'; s++) {
		if ((n = n * 10 + (rb(s) - '0')) > 1000)
			return 0;
		digits++;
	}
	if (rb(s) == '.')
		s++;
	if (digits == 0 || rb(s) != 0)
		return 0;
	if (neg)
		n = -n;
	return n >= -128 && n <= 127;
}

/*
 * p: the "push r" node; regs[RT1] holds the constant of the "mov" before
 * it (pushopt()'s dualop()).
 */
int
rule_v30_push(uint16_t p)
{
	char x[40];
	int i;

	if (!pushable(REG(RT1)))
		return 0;
	for (i = 0; i < (int)sizeof x - 1 && rb((uint16_t)(REG(RT1) + i)) != 0; i++)
		x[i] = (char)rb((uint16_t)(REG(RT1) + i));
	x[i] = '\0';
	c2_rule("V30-PUSH", c2_v30, p, "x=%s", x);
	return c2_v30;
}
