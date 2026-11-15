/*
 * c2rules.c - what mutos_c2's rules share (c2rules.h): the rule log's
 * entries, which wait for output() to write their nodes.
 */
#include "../c2port/c2.h"

#include <stdarg.h>

/* entries wait for their node to be written; a chunk is one function */
#define	NPENDING	1024

static struct pending {
	const char	*rule;
	int		applied;
	uint16_t	node;
	int		written;
	char		keys[96];
} pending[NPENDING];
static	int	npending;
static	int	logon = -1;	/* -1: not looked at yet */
static	uint16_t cur;		/* the node being written, or 0 */
static	long	start;		/* where its output starts */
static	long	chunk;		/* where the chunk's output starts */

void
c2_rule(const char *rule, int applied, uint16_t node, const char *fmt, ...)
{
	struct pending *e;
	int i;

	if (logon < 0)
		logon = mutos_rulelog_on();
	if (!logon)
		return;
	/* one entry for a node and a rule: the last word counts */
	for (i = 0; i < npending; i++)
		if (pending[i].node == node && pending[i].rule == rule)
			break;
	if (i == NPENDING)
		return;
	e = &pending[i];
	if (i == npending)
		npending++;
	e->rule = rule;
	e->applied = applied;
	e->node = node;
	e->written = 0;
	e->keys[0] = '\0';
	if (fmt != NULL) {
		va_list ap;

		va_start(ap, fmt);
		vsnprintf(e->keys, sizeof e->keys, fmt, ap);
		va_end(ap);
	}
}

void
c2_rule_node(uint16_t node)
{
	const char *src;
	int i;

	if (logon < 0)
		logon = mutos_rulelog_on();
	if (!logon)
		return;
	src = getenv(MUTOS_RULELOG_SRC_ENV);
	if (cur == 0)
		chunk = c2_outpos;
	else
		for (i = 0; i < npending; i++) {
			struct pending *e = &pending[i];

			if (e->node != cur)
				continue;
			e->written = 1;
			mutos_rulelog("c2", e->rule, e->applied ? "applied" : "met",
			    "%s%s%sout=%ld-%ld%s%s", src ? "src=" : "",
			    src ? src : "", src ? " " : "", start, c2_outpos,
			    e->keys[0] ? " " : "", e->keys);
		}
	cur = node;
	start = c2_outpos;
	if (node != 0)
		return;
	/* the chunk is written: an entry for it of each rule applied in it */
	for (i = 0; i < npending; i++) {
		struct pending *e = &pending[i];
		int k;

		if (!e->applied || !e->written)
			continue;
		for (k = 0; k < i; k++)
			if (pending[k].applied && pending[k].written
			    && pending[k].rule == e->rule)
				break;
		if (k == i)
			mutos_rulelog("c2", e->rule, "applied",
			    "%s%s%sout=%ld-%ld chunk=1", src ? "src=" : "",
			    src ? src : "", src ? " " : "", chunk, c2_outpos);
	}
	npending = 0;		/* its nodes are gone */
}

/*
 * c2_displen - the bytes of a displacement in the operand at s, which ends
 * at a comma or at the end: none for a register and for "(r)", one for
 * "*n.(r)" and for "(bp)", two for anything else (an address, "#n(r)").
 */
int
c2_displen(uint16_t s)
{
	uint16_t t;
	int paren = 0;

	if (isreg(s) >= 0)
		return 0;
	for (t = s; rb(t) != 0 && rb(t) != ','; t++)
		if (rb(t) == '(')
			paren = 1;
	if (!paren)
		return 2;			/* an address */
	if (rb(s) == '(')			/* "(bp)" has a byte of 0 */
		return rb((uint16_t)(s + 1)) == 'b' && rb((uint16_t)(s + 2)) == 'p'
		    && rb((uint16_t)(s + 3)) == ')' ? 1 : 0;
	return rb(s) == '*' ? 1 : 2;
}
