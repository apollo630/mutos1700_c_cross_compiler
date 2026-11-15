/*
 * V30-IMUL in c2 - the length of "imul r,*n." when a branch is measured
 * (docs/TOOLCHAIN_PLAN.md, section 7.4, step V3; docs/RULES.md).
 *
 * The rule itself is c1's (c1rules/v30_imul.c): under -1, "mov cx,*n." /
 * "imul cx" is written as "imul ax,*n." (6B /r ib) or "imul ax,#n" (69 /r
 * iw). c2 changes nothing in such an instruction, but shortbr() must
 * know its length.
 *
 * Hook: toofar() (c2port/c21.c), the case of a single operand. The
 * original takes "imul" for an instruction with one operand, as the
 * 8086's is: singop() gives it everything after the tab, and onelen()
 * finds a register in the first two characters, "ax" - 2 bytes. The
 * form has 3 or 4 (and with an operand in memory in the middle, which
 * c1 does not write, up to 6). A branch over enough of them that c2
 * makes short would not reach; mutos_as refuses such a branch (since
 * 2026-11-15), so that the compilation would fail. Under -1 the hook
 * adds the bytes the original leaves out: the constant's (1 after "*",
 * else 2) and, for the form with three operands, the displacement of
 * the one in the middle.
 *
 * What else c2 does with the form is the original's own, and holds:
 *   - rmove() takes an "imul" as changing AX, DX and the flags; the form
 *     changes AX and the flags, and leaves DX - known less than is true;
 *   - regmask() takes it as reading AX and its operand and writing AX
 *     and DX: the form reads AX and writes AX. No code of c1's reads DX
 *     after a multiplication without loading it, as "imul cx" leaves the
 *     product's high word there;
 *   - CX is not loaded any more, so c2 knows no constant in CX after it
 *     (where it would write "add ax,cx" for "add ax,*30.").
 *
 * Without -1 the hook adds nothing - c2 is the original then, whatever
 * its input holds.
 *
 * The rule log: "applied" for an "imul" by a constant measured under -1,
 * "met" without -1; len= is the length counted. With the entry of the
 * function (c2rules.h) that accounts for a branch c2 leaves long, or
 * makes short, under -1 only.
 *
 * State: active.
 */
#include "../c2port/c2.h"

/*
 * q: an IMUL node, regs[RT1] its operands (toofar()'s singop()); base is
 * what the original has counted for it. Returns the bytes to add.
 */
int
rule_v30_imul(uint16_t q, int base)
{
	uint16_t s, a, b;
	int len;

	s = REG(RT1);
	for (a = s; rb(a) != 0 && rb(a) != ','; a++)
		;
	if (rb(a) == 0)
		return 0;			/* the 8086's: one operand */
	a++;
	for (b = a; rb(b) != 0 && rb(b) != ','; b++)
		;
	len = 2;				/* the opcode and the mod r/m byte */
	if (rb(b) == ',') {			/* r,x,constant */
		len += c2_displen(a);
		a = (uint16_t)(b + 1);
	} else					/* r,constant */
		len += c2_displen(s);
	len += rb(a) == '*' ? 1 : 2;
	c2_rule("V30-IMUL", c2_v30, q, "len=%d", c2_v30 ? len : base);
	if (!c2_v30 || len <= base)
		return 0;
	return len - base;
}
