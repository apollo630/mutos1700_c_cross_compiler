/*
 * C1-01 - branches on stale flags after not and mov
 * (docs/MUTOS_ORIGINAL_BUGS.md, C1-01; docs/RULES.md).
 *
 * Hook: cexpr()'s template letter H (c1port/c10.c). In cctab, the list
 * 40c2 of ops !, unary -, ~, +, -, >>, <<, | and ^ is the single row
 * "%n,n / H": compute the tree through cregtab - regtab without the
 * "or r,r" that rcexpr() appends when it falls back from cctab - and
 * branch on the flags the computation's last instruction set. On the
 * 8086 three of the ops end in an instruction that sets none:
 *   ~   "not";
 *   !   its value, "mov r,*0." or "mov r,*1." after a branch - the flags
 *       are those of the comparison before;
 *   >>, <<  by a count that is not a constant: "sal r,cl" with a count
 *       of 0 leaves the flags as they were.
 *
 * State: diagnostic (step F0) - it reports each use of the row for one of
 * them ("met", op=~, op=! or op=<< / op=>>); the code is the original's.
 */
#include "../c1port/c1.h"

void
rule_c1_01(struct tnode *tree, struct table *table)
{
	const char *op;

	if (table != cctab)
		return;
	switch (tree->op) {
	case COMPL:
		op = "~";
		break;
	case EXCLA:
		op = "!";
		break;
	case LSHIFT:
	case RSHIFT:
		if (tree->tr2 == 0 || tree->tr2->op == CON)
			return;
		op = tree->op == LSHIFT ? "<<" : ">>";
		break;
	default:
		return;
	}
	c1_rule("C1-01", 0, NULL, "op=%s", op);
}
