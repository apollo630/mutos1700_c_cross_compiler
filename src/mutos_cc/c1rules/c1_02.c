/*
 * C1-02 - e * 0 and e & 0 drop e's side effects
 * (docs/MUTOS_ORIGINAL_BUGS.md, C1-02; docs/RULES.md).
 *
 * Hook: acommute() (c1port/c12.c), where a product or an AND whose
 * constants fold to 0 returns that 0 in place of the whole list - the
 * other operands are not looked at (v7's c12.c). The original's path is
 * wrong when one of them has a side effect: an assignment, ++ or --, a
 * call.
 *
 * State: diagnostic (step F0) - it reports those ("met", op=* or op=&)
 * and returns the 0, as the original does.
 */
#include "../c1port/c1.h"

/* whether evaluating the tree changes anything but a register */
static int
sideeffect(struct tnode *t)
{
	if (t == 0)
		return 0;
	switch (t->op) {
	case AUTOI:
	case AUTOD:
	case INCBEF:
	case DECBEF:
	case INCAFT:
	case DECAFT:
	case ASULSH:
	case ASPLUS:
	case ASMINUS:
	case ASTIMES:
	case ASDIV:
	case ASMOD:
	case ASRSH:
	case ASLSH:
	case ASAND:
	case ASOR:
	case ASXOR:
	case ASSIGN:
	case ASANDN:
	case LASTIMES:
	case LASDIV:
	case LASMOD:
	case ASLSHL:
	case CALL1:
	case CALL2:
	case CALL:
	case MCALL:
	case STRASG:
	case STRSET:
	case ASUDIV:
	case ASUMOD:
		return 1;
	}
	if (t->op < 0 || (opdope[t->op] & LEAF) != 0)
		return 0;
	if ((opdope[t->op] & BINARY) != 0 && sideeffect(t->tr2))
		return 1;
	return sideeffect(t->tr1);
}

struct tnode *
rule_c1_02(struct tnode *zero, int op, struct tnode **ops, int n)
{
	int i;

	for (i = 0; i < n; i++)
		if (sideeffect(ops[i])) {
			c1_rule("C1-02", 0, NULL, "op=%s", op == AND ? "&" : "*");
			break;
		}
	return zero;
}
