/*
 * C1-06 - l <<= i on a local long jumps into an instruction for i == 0
 * (docs/MUTOS_ORIGINAL_BUGS.md, C1-06; docs/RULES.md).
 *
 * Hook: cexpr() (c1port/c10.c), once a row is matched: the row's template
 * goes through the rule. The rows of a long shift by a count that is not
 * a constant skip the shift loop and the two stores with "jz .+16": the
 * jz (2 bytes), the shift and the rotate by 1 (2 each), "loop .-4" (2) and
 * two stores of 4 bytes - right for a store to an address ("mov _l,di",
 * "mov 2.+_l,si", "mov _x(bx),di"), wrong for one with a byte displacement
 * ("mov *-8.(bp),di", 3 bytes) or none ("mov (bx),di", 2): the jump lands
 * inside the instruction after them.
 *
 * The correction (the corrected mode): the jump skips the loop only -
 * "jz .+8", as the original's own rows for the value of l << i have it
 * (s1944 ...). The distance is the three two-byte instructions whatever
 * the stores are; with a count of 0 the stores then write back the two
 * registers the row loaded from the very place they store to, which
 * leaves the long as it was. Every such row is changed, a file-scope
 * long's too, where the original's jump was right: one form, the
 * stores' bytes not to be foreseen before the row prints them.
 *
 * The rule log: in the original mode, a row whose two stores, as
 * printed, are not 8 bytes ("met", stores=<bytes>) - the stores are known
 * only once the row is printed, so the rule asks to see the text
 * (c1_rule()'s check): after the jz it reads the three lines of the loop
 * and the two stores, and counts the stores' bytes as mutos_as encodes a
 * "mov" from a word register to memory. In the corrected mode, every row
 * it changes ("applied").
 *
 * State: active.
 */
#include "../c1port/c1.h"

#include <stdlib.h>
#include <string.h>

/*
 * The bytes of "mov <dst>,<word register>" for the destination's text:
 * opcode and ModRM, then no displacement for "(reg)" (but (bp), which
 * takes a byte), a byte for "*n.(reg)" and for a plain number from -128
 * to 127 ("+2(bx)", the second word through a pointer), a word for any
 * other displacement and for an address.
 */
static int
storelen(const char *dst, size_t n)
{
	const char *lp = memchr(dst, '(', n);
	const char *p;
	long v;
	int neg;

	if (lp == NULL)
		return 4;			/* an address: disp16 */
	if (lp == dst)
		return (n >= 4 && strncmp(lp, "(bp)", 4) == 0) ? 3 : 2;
	if (dst[0] == '*')
		return 3;			/* disp8 */
	p = dst;
	if (*p == '+')
		p++;
	neg = *p == '-';
	if (neg)
		p++;
	if (p == lp || *p < '0' || *p > '9')
		return 4;			/* a symbol, "#n.": disp16 */
	for (v = 0; p < lp && *p >= '0' && *p <= '9'; p++)
		if ((v = v * 10 + (*p - '0')) > 1000)
			return 4;
	if (p < lp && *p == '.')
		p++;
	if (p != lp)
		return 4;
	if (neg)
		v = -v;
	return (v >= -128 && v <= 127) ? 3 : 4;
}

/* the line after p (in text of n bytes), or NULL */
static const char *
nextline(const char *p, const char *end)
{
	const char *nl = memchr(p, '\n', (size_t)(end - p));

	return nl == NULL ? NULL : nl + 1;
}

static int
check(const void *ctx, const char *text, size_t n, char *keys, size_t nkeys)
{
	const char *end, *p, *q;
	int i, sum = 0;

	(void)ctx;
	if (text == NULL)
		return 0;
	end = text + n;
	p = text;
	/* the jz of the row */
	for (;;) {
		if ((size_t)(end - p) >= 8 && strncmp(p, "jz\t.+16", 7) == 0)
			break;
		if ((p = nextline(p, end)) == NULL)
			return 0;
	}
	/* the shift, the rotate and the loop */
	for (i = 0; i < 4; i++)
		if ((p = nextline(p, end)) == NULL)
			return 0;
	/* the two stores: "mov\t<dst>,<reg>" */
	for (i = 0; i < 2; i++) {
		const char *nl, *comma;

		if ((size_t)(end - p) < 5 || strncmp(p, "mov\t", 4) != 0)
			return 0;
		p += 4;
		if ((nl = memchr(p, '\n', (size_t)(end - p))) == NULL)
			return 0;
		comma = NULL;
		for (q = p; q < nl; q++)
			if (*q == ',')
				comma = q;
		if (comma == NULL)
			return 0;
		sum += storelen(p, (size_t)(comma - p));
		p = nl + 1;
	}
	if (sum == 8)
		return 0;
	snprintf(keys, nkeys, "stores=%d", sum);
	return 1;
}

/*
 * The rows' templates with "jz\256+16" or "jz\256+16." made "jz\256+8",
 * made once each and kept (a row's template must stay valid while it is
 * printed: cexpr() is recursive, and an inner row of the same kind may be
 * matched meanwhile). The code tables have twelve such rows.
 */
#define	NROWS	16

static struct {
	const char	*orig;
	char		*fixed;
} rows[NROWS];

static char *
skiploop(char *tabstring)
{
	static const char jz16[] = "jz\256+16";
	const char *at, *rest;
	char *fixed;
	size_t n;
	int i;

	for (i = 0; i < NROWS && rows[i].orig != NULL; i++)
		if (rows[i].orig == tabstring)
			return rows[i].fixed;
	at = strstr(tabstring, jz16);
	rest = at + sizeof(jz16) - 1;
	if (*rest == '.')
		rest++;
	if ((fixed = malloc(strlen(tabstring) + 1)) == NULL) {
		error("Out of space-- c1");
		exit(1);
	}
	n = (size_t)(at - tabstring);
	memcpy(fixed, tabstring, n);
	strcpy(fixed + n, "jz\256+8");
	strcat(fixed + n, rest);
	if (i < NROWS) {
		rows[i].orig = tabstring;
		rows[i].fixed = fixed;
	}
	return fixed;
}

char *
rule_c1_06(char *tabstring)
{
	if (strstr(tabstring, "jz\256+16") == NULL)
		return tabstring;
	if (c1_mode == MUTOS_CORRECTED) {
		c1_rule("C1-06", 1, NULL, NULL, NULL);
		return skiploop(tabstring);
	}
	c1_rule("C1-06", 0, check, NULL, NULL);
	return tabstring;
}
