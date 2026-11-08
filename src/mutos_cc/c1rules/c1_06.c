/*
 * C1-06 - l <<= i on a local long jumps into an instruction for i == 0
 * (docs/MUTOS_ORIGINAL_BUGS.md, C1-06; docs/RULES.md).
 *
 * Hook: cexpr() (c1port/c10.c), once a row is matched. The rows of a long
 * shift by a count that is not a constant skip the shift loop and the two
 * stores with "jz .+16": the jz (2 bytes), the shift and the rotate by 1
 * (2 each), "loop .-4" (2) and two stores of 4 bytes - right for a store to
 * an address ("mov _l,di", "mov 2.+_l,si", "mov _x(bx),di"), wrong for one
 * with a byte displacement ("mov *-8.(bp),di", 3 bytes) or none ("mov
 * (bx),di", 2): the jump lands inside the instruction after them.
 *
 * The original's path is such a row whose two stores are not 8 bytes. The
 * stores are known only once the row is printed, so the rule asks to see
 * the text (c1_rule()'s check): after the jz it reads the three lines of
 * the loop and the two stores, and counts the stores' bytes as mutos_as
 * encodes a "mov" from a word register to memory. State: diagnostic
 * (step F0) - it reports that ("met", stores=<bytes>); the code is the
 * original's.
 */
#include "../c1port/c1.h"

#include <string.h>

/*
 * The bytes of "mov <dst>,<word register>" for the destination's text:
 * opcode and ModRM, then no displacement for "(reg)" (but (bp), which
 * takes a byte), a byte for "*n.(reg)", a word for any other address.
 */
static int
storelen(const char *dst, size_t n)
{
	const char *lp = memchr(dst, '(', n);

	if (lp == NULL)
		return 4;			/* an address: disp16 */
	if (lp == dst)
		return (n >= 4 && strncmp(lp, "(bp)", 4) == 0) ? 3 : 2;
	if (dst[0] == '*')
		return 3;			/* disp8 */
	return 4;				/* disp16 */
}

/* the line after p (in text of n bytes), or NULL */
static const char *
nextline(const char *p, const char *end)
{
	const char *nl = memchr(p, '\n', (size_t)(end - p));

	return nl == NULL ? NULL : nl + 1;
}

static int
check(const char *text, size_t n, char *keys, size_t nkeys)
{
	const char *end = text + n, *p = text, *q;
	int i, sum = 0;

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

void
rule_c1_06(const char *tabstring)
{
	if (strstr(tabstring, "jz\256+16") != NULL)
		c1_rule("C1-06", 0, check, NULL);
}
