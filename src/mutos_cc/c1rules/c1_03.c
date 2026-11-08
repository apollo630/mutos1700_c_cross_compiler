/*
 * C1-03 - x = l++ loses the carry
 * (docs/MUTOS_ORIGINAL_BUGS.md, C1-03; docs/RULES.md).
 *
 * Hook: delay() (c1port/c10.c), when sdelay() has taken a postfix ++ or --
 * of a name out of the tree, before the tree is compiled. sdelay() puts
 * the name back into the tree as ncopy() gives it, which copies only a
 * register variable: any other name node is then in the tree and in the
 * increment at once. When the tree converts the long to an int (x = l++:
 * LTOI of the name), unoptim() turns that very node into the long's low
 * word - its offset raised by 2, its type int - and the increment that
 * follows is an int increment of the low word ("inc 2.+_l"): the carry
 * into the high word is lost.
 *
 * The correction (the corrected mode): the increment of a long gets a
 * copy of its name node, so that what the tree does to the node it keeps
 * is not done to the increment's - "add 2.+_l,*1" / "adc _l,*0". A tree
 * that leaves the node alone compiles as before, and so does the
 * increment: the copy is the node, field for field.
 *
 * The rule log: once the statement is compiled, an entry for each such
 * increment whose node in the tree is no longer a long - "met" in the
 * original mode (the increment was of the low word), "applied" in the
 * corrected mode (the copy kept it a long's).
 *
 * State: active.
 */
#include "../c1port/c1.h"

#include <string.h>

/* whether the tree's node (ctx) has been made into something else */
static int
changed(const void *ctx, const char *text, size_t n, char *keys, size_t nkeys)
{
	const struct tnode *name = ctx;

	(void)text;
	(void)n;
	(void)keys;
	(void)nkeys;
	return name->type != LONG;
}

void
rule_c1_03(struct tnode *incdec)
{
	struct tnode *name = incdec->tr1, *copy;
	int applied = 0;

	if (incdec->type != LONG || name == 0 || name->op != NAME
	 || name->type != LONG || name->class == REG)
		return;
	if (c1_mode == MUTOS_CORRECTED) {
		copy = getblk(sizeof(*copy));
		memcpy(copy, name, sizeof(*copy));
		incdec->tr1 = copy;
		applied = 1;
	}
	c1_rule("C1-03", applied, changed, name, "op=%s",
	    incdec->op == INCAFT ? "++" : "--");
}
