/*
 * V30-IMUL - "mov cx,*n." / "imul cx" becomes "imul ax,*n."
 * (docs/TOOLCHAIN_PLAN.md, section 7.4, step V3; docs/RULES.md).
 *
 * Hook: cexpr() (c1port/c10.c), once a row is matched: the row's template
 * goes through the rule. The 8086 multiplies AX by a register or by an
 * operand in memory, so the rows of * and =* for a right operand that is
 * a constant (or a register variable) load it into CX:
 *	mov	ax,...		(the left operand)
 *	movB2	cx,A2
 *	I	cx		(DX:AX = AX * CX)
 * (a product by a power of two is a shift, and one whose constant does
 * not fit in an int a call of lmul). The 80186 and the V30 multiply a
 * register by a constant (6B /r ib, the byte sign-extended; 69 /r iw):
 * the low word of the same product in AX, CX not loaded, DX left. c1
 * keeps no account of what a register holds, so nothing it writes later
 * reads the n from CX; and it reads the product from AX only - the int
 * result of an int multiplication (a long one is lmul's).
 *
 * The flags: both forms set the carry and the overflow flag when the
 * product does not fit in a word and leave the others undefined. c1
 * never branches on a product's flags; it tests the result ("or ax,ax").
 *
 * Under -1, for a right operand that is a constant, the row is used
 * without its load and with "ax" and the constant, A2, for "cx". c1
 * writes a constant from -128 to 127 as "*n.", the form with a byte,
 * and any other as "#n", the one with a word (mutos_as refuses a "*n."
 * outside that range). A product of chars with a byte instruction
 * ("imulb") is left as it is.
 *
 * c2 takes the form for an "imul" with the one operand "ax"; what that
 * means for it, and the length it must count, is c2rules/v30_imul.c.
 *
 * The rule log: "applied" for each row used so under -1, "met" without
 * -1 where -1 would; n= is the constant.
 *
 * State: active.
 */
#include "../c1port/c1.h"

#include <stdlib.h>
#include <string.h>

/* the load of the constant: "mov", an optional B2, a tab and "cx,", A2 */
static const char loadb[] = "movD\343x,B\n";
static const char load[] = "mov\343x,B\n";
/* the multiplication after it: I, an optional BE, a tab and "cx" */
static const char mulb[] = "ML\343x\n";
static const char mul[] = "M\343x\n";

/*
 * The rows' templates without the load and with "ax,B" for "cx", made
 * once each and kept (a row's template must stay valid while it is
 * printed: cexpr() is recursive). The code tables have five such rows'
 * templates: three of a value, two of an assignment.
 */
#define	NROWS	8

static struct {
	const char	*orig;
	char		*v30;
} rows[NROWS];

/* at: the load; in: the multiplication's "cx"; both in tabstring */
static char *
byconst(char *tabstring, const char *at, const char *in)
{
	char *v30;
	size_t n;
	int i;

	for (i = 0; i < NROWS && rows[i].orig != NULL; i++)
		if (rows[i].orig == tabstring)
			return rows[i].v30;
	/* shorter than the row: "ax,B" for "cx", and the load gone */
	if ((v30 = malloc(strlen(tabstring) + 1)) == NULL) {
		error("Out of space-- c1");
		exit(1);
	}
	n = (size_t)(at - tabstring);
	memcpy(v30, tabstring, n);			/* the left operand */
	at = strchr(at, '\n') + 1;
	memcpy(v30 + n, at, (size_t)(in - at));		/* I and its suffix */
	n += (size_t)(in - at);
	memcpy(v30 + n, "\341x,B", 4);			/* a tab, "ax,", A2 */
	n += 4;
	strcpy(v30 + n, in + 2);
	if (i < NROWS) {
		rows[i].orig = tabstring;
		rows[i].v30 = v30;
	}
	return v30;
}

char *
rule_v30_imul(struct tnode *tree, char *tabstring)
{
	const char *at, *m;
	struct tnode *p1, *p2;

	if (tree->op != TIMES && tree->op != ASTIMES)
		return tabstring;
	if ((at = strstr(tabstring, loadb)) != NULL)
		m = at + sizeof(loadb) - 1;
	else if ((at = strstr(tabstring, load)) != NULL)
		m = at + sizeof(load) - 1;
	else
		return tabstring;
	if (strncmp(m, mulb, sizeof(mulb) - 1) == 0)
		m += 2;
	else if (strncmp(m, mul, sizeof(mul) - 1) == 0)
		m += 1;
	else
		return tabstring;
	p1 = tree->tr1;
	p2 = tree->tr2;
	if (p2 == 0 || p2->op != CON)
		return tabstring;
	/* a byte instruction (the suffix BE): left as it is */
	if (tree->type == CHAR || p1->type == CHAR || p2->type == CHAR)
		return tabstring;
	c1_rule("V30-IMUL", c1_v30, NULL, NULL, "n=%d", p2->value);
	return c1_v30 ? byconst(tabstring, at, m) : tabstring;
}
