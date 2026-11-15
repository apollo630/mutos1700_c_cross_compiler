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
 * Under -1, for a count that is a constant from 3 to 15 and a shift of a
 * register (the row's R, or its A1 when that is a register variable), the
 * row is used without its "mov" and with the count, A2, for "cl".
 *
 * A shift in memory is left as it is: c2 measures a branch from the
 * instructions' text and takes any shift for 4 bytes (toofar(), c2port/
 * c21.c: the length of the count's text, "cl", "*1" or "*n."). The
 * 8086's shift is 4 bytes at most; "sal r,*n." is 3, but "sal _x,*n.",
 * with a word of address, is 5 - one more than c2 counts, so that a
 * branch c2 makes short might not reach. (No shift in memory by a
 * constant from 3 on is in the kernel's code.)
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

/*
 * The rows' templates without the load and with "B" (A2) for "cl", made
 * once each and kept (a row's template must stay valid while it is
 * printed: cexpr() is recursive). The code tables have four such rows:
 * one of a value, three of an assignment.
 */
#define	NROWS	8

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
	struct tnode *p1, *p2;

	if ((at = strstr(tabstring, load)) == NULL)
		return tabstring;
	sh = at + sizeof(load) - 1;
	p1 = tree->tr1;
	p2 = tree->tr2;
	if (p2 == 0 || p2->op != CON || p2->value < 3 || p2->value > 15)
		return tabstring;
	if (strncmp(sh, shreg, sizeof(shreg) - 1) == 0)
		sh += sizeof(shreg) - 4;
	else if (strncmp(sh, shadr, sizeof(shadr) - 1) == 0
	      && p1->op == NAME && p1->class == REG)
		sh += sizeof(shadr) - 4;
	else
		return tabstring;
	c1_rule("V30-SHIFT", c1_v30, NULL, NULL, "n=%d", p2->value);
	return c1_v30 ? bycount(tabstring, at, sh) : tabstring;
}
