/*
 * C1-01 - branches on stale flags after not and mov
 * (docs/MUTOS_ORIGINAL_BUGS.md, C1-01; docs/RULES.md).
 *
 * Hook: cexpr() (c1port/c10.c), once a row is matched: the row's template
 * goes through the rule. In cctab, the list 40c2 of ops !, unary -, ~, +,
 * -, >>, <<, | and ^ is the single row "%n,n / HA": compute the tree
 * through cregtab - regtab without the "or r,r" that rcexpr() appends
 * when it falls back from cctab - and branch on the flags the
 * computation's last instruction set. On the 8086 three of the ops end in
 * an instruction that sets none:
 *   ~   "not";
 *   !   its value, "mov r,*0." or "mov r,*1." after a branch - the flags
 *       are those of the comparison before;
 *   >>, <<  by a count that is not a constant: "sal r,cl" with a count
 *       of 0 leaves the flags as they were.
 * The other six end in an instruction that sets the zero and the sign
 * flag from the result: neg, add, sub, or, xor, a shift by a constant
 * count (folded when the count is 0).
 *
 * The correction (the corrected mode): for those three, an int or char
 * tree, the row ends in "or r,r" ("orb" for a char) on the register that
 * holds the value - "HAorJ\311A,IA\n", what rcexpr() appends when cctab
 * has no row at all. The computation is the original's.
 *
 * The rule log: each use of the row for one of the three - "met" in the
 * original mode (op=~, op=! or op=<< / op=>>), "applied" in the corrected
 * mode; a long or floating tree, which the correction leaves alone, is
 * "met" in either.
 *
 * State: active.
 */
#include "../c1port/c1.h"

#include <string.h>

/* row 40c2 of cctab with the test of the value: "or" (J: "b" for a
 * char), a tab (\311: the 0200 bit), the register (IA), twice */
static char testvalue[] = "HAorJ\311A,IA\n";

char *
rule_c1_01(struct tnode *tree, struct table *table, char *tabstring)
{
	const char *op;

	if (table != cctab || strcmp(tabstring, "HA") != 0)
		return tabstring;
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
			return tabstring;
		op = tree->op == LSHIFT ? "<<" : ">>";
		break;
	default:
		return tabstring;
	}
	if (c1_mode != MUTOS_CORRECTED || tree->type == LONG
	 || tree->type == FLOAT || tree->type == DOUBLE) {
		c1_rule("C1-01", 0, NULL, NULL, "op=%s", op);
		return tabstring;
	}
	c1_rule("C1-01", 1, NULL, NULL, "op=%s", op);
	return testvalue;
}
