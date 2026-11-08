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
 * The correction (the corrected mode): the operands with a side effect
 * are kept, in their order, as a comma expression before the 0 - "(u =
 * 7, x) * 0" is "((u = 7, x), 0)", SEQNC nodes as c0 writes for a comma -,
 * and the operands without one are dropped, as the original drops them
 * all. A list without a side effect still folds to the 0.
 *
 * The rule log: such a list - "met" in the original mode (the 0 alone),
 * "applied" in the corrected mode (op=* or op=&).
 *
 * State: active.
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

/*
 * (e, rest): a SEQNC node with the degree optim() gives a binary node
 * (c12.c's last lines) - e's operands are optimized already.
 */
static struct tnode *
seqnc(struct tnode *e, struct tnode *rest)
{
	struct tnode *t = tnode(SEQNC, rest->type, e, rest);
	int d1 = max(degree(e), islong(t->type));
	int d2 = max(degree(rest), 0);

	t->degree = (int16_t)(d1 == d2 ? d1 + islong(t->type) : max(d1, d2));
	return t;
}

struct tnode *
rule_c1_02(struct tnode *zero, int op, struct tnode **ops, int n)
{
	struct tnode *t;
	int i;

	for (i = 0; i < n; i++)
		if (sideeffect(ops[i]))
			break;
	if (i == n)
		return zero;
	if (c1_mode != MUTOS_CORRECTED) {
		c1_rule("C1-02", 0, NULL, NULL, "op=%s", op == AND ? "&" : "*");
		return zero;
	}
	t = zero;
	for (i = n - 1; i >= 0; i--)
		if (sideeffect(ops[i]))
			t = seqnc(ops[i], t);
	c1_rule("C1-02", 1, NULL, NULL, "op=%s", op == AND ? "&" : "*");
	return t;
}
