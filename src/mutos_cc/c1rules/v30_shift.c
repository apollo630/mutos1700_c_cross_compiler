/*
 * V30-SHIFT - "mov cx,*n." / "sal r,cl" becomes "sal r,*n."
 * (docs/TOOLCHAIN_PLAN.md, section 7.4, step V2; docs/RULES.md).
 *
 * Hook: cexpr() (c1port/c10.c), once a row is matched: the row's template
 * goes through the rule. The 8086 shifts by 1 or by CL, so the rows of a
 * shift (<<, >>, =<<, =>>; sal, sar, shr) whose count is an addressable
 * operand load it into CX and shift by CL:
 *	movB2	cx,A2
 *	IB1	R,cl		(or A1, or #1(R*), for the destination)
 * (a count of 0 is folded, a count of 1 has a row of its own, and optim()
 * makes two shifts by 1 of a count of 2; from 3 on these rows are used).
 * The 80186 and the V30 shift by a constant byte (C1 /r ib, C0 for a
 * byte): the same result and the same flags as the shift by CL with that
 * count - on the V30 neither form masks the count, on the 80186 both take
 * it modulo 32 - and CX not loaded. c1 keeps no account of what a
 * register holds, so nothing it writes later reads the n from CX.
 *
 * Under -1, for a count that is a constant from 3 to 15, the row is used
 * without its "mov" and with the count, A2, for "cl": a shift of a
 * register (the row's R, or its A1 when that is a register variable)
 * and, since 2026-11-15, a shift in memory (=<<, =>>: the row's A1, or
 * #1(R*) behind a pointer).
 *
 * With a word of address the form is 5 bytes ("sal _x,*n."), one more
 * than the 8086's longest shift, which is what c2 takes any shift for
 * when it measures a branch; c2rules/v30_shift.c counts that byte.
 *
 * The rule log: "applied" for each row used so under -1, "met" without
 * -1 where -1 would; n= is the count.
 *
 * State: active.
 */
#include "../c1port/c1.h"

#include <stdlib.h>
#include <string.h>

/* the load of the count: "mov", B2, a tab and "cx,", A2 */
static const char load[] = "movD\343x,B\n";
/* the shift's line after it: I, B1 and a tab, then R ("IA") or A1 ("A") */
static const char shreg[] = "MC\311A,cl\n";
static const char shadr[] = "MC\301,cl\n";
/* or #1(R*): the destination behind a pointer */
static const char shind[] = "MC\243(IB),cl\n";

/*
 * The rows' templates without the load and with "B" (A2) for "cl", made
 * once each and kept (a row's template must stay valid while it is
 * printed: cexpr() is recursive). The code tables have seven such rows:
 * one of a value, six of an assignment.
 */
#define	NROWS	12

static struct {
	const char	*orig;
	char		*v30;
} rows[NROWS];

static char *
bycount(char *tabstring, const char *at, const char *cl)
{
	char *v30;
	size_t n;
	int i;

	for (i = 0; i < NROWS && rows[i].orig != NULL; i++)
		if (rows[i].orig == tabstring)
			return rows[i].v30;
	if ((v30 = malloc(strlen(tabstring) + 1)) == NULL) {
		error("Out of space-- c1");
		exit(1);
	}
	n = (size_t)(at - tabstring);
	memcpy(v30, tabstring, n);			/* F, F* ... */
	at += sizeof(load) - 1;
	memcpy(v30 + n, at, (size_t)(cl - at));		/* the shift up to "cl" */
	n += (size_t)(cl - at);
	v30[n++] = 'B';
	strcpy(v30 + n, cl + 2);
	if (i < NROWS) {
		rows[i].orig = tabstring;
		rows[i].v30 = v30;
	}
	return v30;
}

char *
rule_v30_shift(struct tnode *tree, char *tabstring)
{
	const char *at, *sh;
	struct tnode *p2;

	if ((at = strstr(tabstring, load)) == NULL)
		return tabstring;
	sh = at + sizeof(load) - 1;
	p2 = tree->tr2;
	if (p2 == 0 || p2->op != CON || p2->value < 3 || p2->value > 15)
		return tabstring;
	if (strncmp(sh, shreg, sizeof(shreg) - 1) == 0)
		sh += sizeof(shreg) - 4;
	else if (strncmp(sh, shadr, sizeof(shadr) - 1) == 0)
		sh += sizeof(shadr) - 4;
	else if (strncmp(sh, shind, sizeof(shind) - 1) == 0)
		sh += sizeof(shind) - 4;
	else
		return tabstring;
	c1_rule("V30-SHIFT", c1_v30, NULL, NULL, "n=%d", p2->value);
	return c1_v30 ? bycount(tabstring, at, sh) : tabstring;
}
