/*
 * C1-03 - x = l++ loses the carry
 * (docs/MUTOS_ORIGINAL_BUGS.md, C1-03; docs/RULES.md).
 *
 * Hook: delay() (c1port/c10.c), after it has compiled the tree from which
 * sdelay() took a postfix ++ or -- of a name, and before the increment is
 * compiled. sdelay() puts the name back into the tree as ncopy() gives it,
 * which copies only a register variable: any other name node is then in
 * the tree and in the increment at once. When the tree converts the long
 * to an int (x = l++: LTOI of the name), unoptim() turns that very node
 * into the long's low word - its offset raised by 2, its type int - and
 * the increment that follows is an int increment of the low word ("inc
 * 2.+_l"): the carry into the high word is lost.
 *
 * The original's path is the increment of a long whose operand is no
 * longer a long. State: diagnostic (step F0) - it reports that ("met");
 * the code is the original's.
 */
#include "../c1port/c1.h"

void
rule_c1_03(struct tnode *incdec)
{
	if (incdec->type == LONG && incdec->tr1 != 0 && incdec->tr1->type != LONG)
		c1_rule("C1-03", 0, NULL, "op=%s",
		    incdec->op == INCAFT ? "++" : "--");
}
