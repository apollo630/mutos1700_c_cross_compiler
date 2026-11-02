/*
 * tsort.c - mutos_tsort, the MUTOS 1700 topological sort: a port of the
 * original tsort.
 *
 * v7's tsort.c (v7/tsort/tsort.c) as the original binary has it - main() at
 * text 00b6, present() at 01be, anypred() at 01e6, index() at 0210, cmp() at
 * 02b2, error() at 02d8, note() at 02f6 and findloop() at 0312
 * (tests/realbin/tsortmap.txt), with libc.a's fscanf underneath (_doscan at
 * 0452, _innum at 0624, _instr at 096c). Read instruction by instruction
 * beside v7's source, the functions do what v7's do; what differs is the
 * representation (the words 16 bits wide, the structures of 8 and 4 bytes)
 * and the libc.a underneath.
 *
 * What tsort does:
 *
 *	tsort [file]
 *
 * reads pairs of items (strings with no blank in them) from the file, or
 * the standard input, and writes the items in an order that obeys the pairs
 * ("a b": a before b); a pair of the same item twice only says that the
 * item exists. When the pairs have a cycle, it says "tsort: cycle in
 * reverse order" and the items of one cycle on the standard error, one
 * each line, and breaks the cycle at the last of them it names. An odd
 * number of items is "tsort: odd data" (status 1), a file that cannot be
 * opened "tsort: cannot open x" (status 1).
 *
 * What the port keeps of the original, where it is odd:
 *  - the two items are read by fscanf(input, "%s%s", precedes, follows)
 *    into char precedes[50], follows[50] in main()'s frame; the
 *    separators are the three characters of libc.a's __sctab with the
 *    bit 1 - blank, tab and newline, not CR, VT or FF - and an item of
 *    50 characters or more runs on: follows' tail into precedes, precedes'
 *    tail into x and j, and further into input, the FILE pointer. The
 *    port keeps the frame as bytes, in the original's order (follows,
 *    precedes, x, j, input), the same stack bytes that stay from one pair
 *    to the next, and x set to the value fscanf returned before the
 *    names are looked up (so that an item of 50 to 53 characters ends
 *    with the low byte of 2);
 *  - the exit status is what main() leaves in AX - it has no exit(): -1
 *    (255) when the standard input is empty, else what the printf() of the
 *    last line returned, 0 or -1 once the standard output has failed to take
 *    a block (the error flag of libc.a's stdio, see ../h/mutos_stdio.h);
 *  - the items are looked up in the order they were first seen, by a linear
 *    search of a list (a quadratic algorithm), and the nodes whose
 *    predecessors are all printed come out in that order.
 *
 * What the port refuses (mu_trap(): exit status 125, the last line of the
 * messages ending in "refused"), where the original depends on state that
 * the port does not model:
 *  - a file name that is a directory (the original reads its entries);
 *  - an item of 54 characters or more in the first place, or 104 or more
 *    in the second: the original's input pointer is overwritten;
 *  - a string with no 0 behind it in precedes or follows (never the case
 *    for what fscanf writes);
 *  - a byte of 0200 or more in the input: __sctab has 128 entries, and the
 *    original looks the byte up in whatever follows the table;
 *  - more graph than the port knows the original's heap to hold
 *    (TS_HEAP, 54K): the original's malloc() returns 0 when the 64K of its
 *    segment are used up - measured, a chain of 2010 pairs of items of eight
 *    characters (56302 bytes of blocks) is sorted and one of 2011 ends with
 *    "tsort: too many items" - and a 0 from the malloc() of a predecessor
 *    list is not an error but a write to address 0 (the first byte of the
 *    text);
 *  - error 1 and error 2 of findloop(), which cannot happen when the lists
 *    are what index() makes of them (and whose second argument v7 does not
 *    pass).
 */
#define _POSIX_C_SOURCE 200809L	/* fileno() */

#define MU_TOOL "tsort"
#include "../h/mutos_stdio.h"

#include <stdint.h>
#include <sys/stat.h>

enum { DEAD, LIVE, ONCE, TWICE };

struct predlist {		/* 4 bytes in the original */
	struct predlist *nextpred;
	struct nodelist *pred;
};

struct nodelist {		/* 8 bytes in the original */
	struct nodelist *nextnode;
	struct predlist *inedges;
	char *name;
	int live;
};

/*
 * the heap: what the port allows the original's malloc() to have given out (its
 * break starts at 0x2280, the 64K segment ends 56K further on). Measured on the
 * original: a chain of 2010 pairs of items of eight characters - 2011 items
 * and 2010 predecessors, 22 * 2011 + 6 * 2010 = 56302 bytes of blocks (12 for
 * the name, 10 for the node, 6 for the predecessor) - is sorted, one of 2011
 * pairs ends with "tsort: too many items". The port stops 1K short of the
 * first, for whatever else libc.a takes from the heap (stdio's buffers).
 */
#ifndef TS_HEAP
#define TS_HEAP		0xD800u		/* the port refuses past this much malloc()ed memory */
#endif
static unsigned heap_used;

/* main()'s frame: follows[50], precedes[50], x, j, input (bp-6e ... bp-6) */
#define FOLLOWS		0
#define PRECEDES	50
#define FR_X		100
#define FR_LIMIT	104		/* the first byte of input */
static uint8_t fr[FR_LIMIT + 2];

static struct nodelist firstnode;	/* {NULL, NULL, NULL, DEAD} */

/*
 * malloc() of libc.a: nw = (n + 3) >> 1 words, header included; n is the
 * size the original asks for (the structures of the 8086 are 8 and 4 bytes),
 * host the size the host's structure needs
 */
static void *
ts_malloc(size_t n, size_t host)
{
	void *p;

	heap_used += ((unsigned)(n + 3) >> 1) * 2;
	if (heap_used > TS_HEAP)
		mu_trap("more graph than the port knows the original's heap to hold");
	p = calloc(1, host > n ? host : n);
	if (p == NULL)
		mu_trap("the host has no memory");
	return p;
}

static MU_NORETURN void
error(const char *s, const char *t)
{
	mu_eprintf("tsort: %s%s\n", s, t);
	mu_exit(1);
}

static void
note(const char *s, const char *t)
{
	mu_eprintf("tsort: %s%s\n", s, t);
}

/* ---- fscanf(input, "%s%s", ...) (libc.a: _doscan 0452, _instr 096c) ------------- */

/* __sctab: the blanks the %s conversion skips and stops at */
static int
sep(int c)
{
	return c == ' ' || c == '\t' || c == '\n';
}

static int
ts_getc(FILE *f)
{
	int c = getc(f);

	if (c >= 0x80)
		mu_trap("a byte of 0200 or more - the original looks it up behind its __sctab");
	return c;
}

/* _instr for %s: 1 if a string was stored at fr[dst]; *ended is set at the end of the file */
static int
instr_s(FILE *f, unsigned dst, int *ended)
{
	int c;
	unsigned n = 0;
	int width = 30000;

	*ended = 0;
	do {
		c = ts_getc(f);
	} while (c != EOF && sep(c));
	while (c != EOF) {
		if (sep(c))
			break;
		if (dst + n >= FR_LIMIT)
			mu_trap("an item that runs on into the FILE pointer of the original's frame");
		fr[dst + n] = (uint8_t)c;
		n++;
		if (--width <= 0)
			break;
		c = ts_getc(f);
	}
	if (c != EOF) {
		if (width > 0)
			ungetc(c, f);
	} else
		*ended = 1;
	if (n == 0)
		return 0;
	if (dst + n >= FR_LIMIT)
		mu_trap("an item that runs on into the FILE pointer of the original's frame");
	fr[dst + n] = 0;
	return 1;
}

/* fscanf(input, "%s%s", precedes, follows): the number of items, or EOF */
static int
scan2(FILE *f)
{
	int ended, nmatch = 0;

	if (instr_s(f, PRECEDES, &ended))
		nmatch++;
	if (ended)
		return nmatch == 0 ? EOF : nmatch;
	if (instr_s(f, FOLLOWS, &ended))
		nmatch++;
	if (ended)
		return nmatch == 0 ? EOF : nmatch;
	return nmatch;
}

/* the C string at fr[off] (no 0 before x: refused) */
static const char *
frstr(unsigned off)
{
	unsigned k;

	for (k = off; k < FR_X; k++)
		if (fr[k] == 0)
			return (const char *)fr + off;
	/* the bytes of x (2, 0 after a pair) would end it, but this is not what fscanf wrote */
	if (fr[FR_X] == 0)
		return (const char *)fr + off;
	if (fr[FR_X + 1] == 0)
		return (const char *)fr + off;
	mu_trap("a string with no 0 behind it in the frame");
}

/* ---- the graph --------------------------------------------------------------------- */

/* present (text 01be): is i on j's predecessor list? */
static int
present(struct nodelist *i, struct nodelist *j)
{
	struct predlist *t;

	for (t = j->inedges; t != NULL; t = t->nextpred)
		if (t->pred == i)
			return 1;
	return 0;
}

/* anypred (text 01e6): is there any live predecessor for i? */
static int
anypred(struct nodelist *i)
{
	struct predlist *t;

	for (t = i->inedges; t != NULL; t = t->nextpred)
		if (t->pred->live == LIVE)
			return 1;
	return 0;
}

/* cmp (text 02b2) */
static int
cmp(const char *s, const char *t)
{
	while (*s == *t) {
		if (*s == 0)
			return 1;
		s++;
		t++;
	}
	return 0;
}

/* index (text 0210): turn a string into a node pointer */
static struct nodelist *
index_(const char *s)
{
	struct nodelist *i;
	char *t;
	const char *e;

	for (i = &firstnode; i->nextnode != NULL; i = i->nextnode)
		if (cmp(s, i->name))
			return i;
	for (e = s; *e; e++)
		;
	t = ts_malloc((size_t)(e + 1 - s), 0);
	i->nextnode = ts_malloc(8, sizeof(struct nodelist));
	i->name = t;
	i->live = LIVE;
	i->nextnode->nextnode = NULL;
	i->nextnode->inedges = NULL;
	i->nextnode->live = DEAD;
	while ((*t++ = *s++) != 0)
		;
	return i;
}

/* findloop (text 0312): given that there is a cycle, find some node in it */
static struct nodelist *
findloop(void)
{
	struct nodelist *i, *j;
	struct predlist *p;

	for (i = &firstnode; i->nextnode != NULL; i = i->nextnode)
		if (i->live == LIVE)
			break;
	note("cycle in reverse order", "");
	while (i->live == LIVE) {
		i->live = ONCE;
		for (p = i->inedges; ; p = p->nextpred) {
			if (p == NULL)
				mu_trap("error 1 of findloop(), whose second argument v7 leaves out");
			i = p->pred;
			if (i->live != DEAD)
				break;
		}
	}
	while (i->live == ONCE) {
		i->live = TWICE;
		note(i->name, "");
		for (p = i->inedges; ; p = p->nextpred) {
			if (p == NULL)
				mu_trap("error 2 of findloop(), whose second argument v7 leaves out");
			i = p->pred;
			if (i->live != DEAD)
				break;
		}
	}
	for (j = &firstnode; j->nextnode != NULL; j = j->nextnode)
		if (j->live != DEAD)
			j->live = LIVE;
	return i;
}

/* ---- main (text 00b6) --------------------------------------------------------------- */

int
main(int argc, char **argv)
{
	struct predlist *t;
	FILE *input = stdin;
	struct nodelist *i, *j;
	int x;
	int ax = 0;			/* what the original leaves in AX */

	if (argc > 1) {
		struct stat st;

		input = fopen(argv[1], "r");
		if (input == NULL)
			error("cannot open ", argv[1]);
		if (fstat(fileno(input), &st) == 0 && S_ISDIR(st.st_mode))
			mu_trap("a directory named as the input - the original reads its entries");
	}
	for (;;) {
		x = scan2(input);
		ax = x;
		fr[FR_X] = (uint8_t)x;
		fr[FR_X + 1] = (uint8_t)((unsigned)x >> 8);
		if (x == EOF)
			break;
		if (x != 2)
			error("odd data", "");
		i = index_(frstr(PRECEDES));
		j = index_(frstr(FOLLOWS));
		if (i == j || present(i, j))
			continue;
		t = ts_malloc(4, sizeof(struct predlist));
		t->nextpred = j->inedges;
		t->pred = i;
		j->inedges = t;
	}
	for (;;) {
		x = 0;		/* anything LIVE on this sweep? */
		for (i = &firstnode; i->nextnode != NULL; i = i->nextnode) {
			if (i->live == LIVE) {
				x = 1;
				ax = anypred(i);
				if (!ax)
					break;
			}
		}
		if (x == 0)
			break;
		if (i->nextnode == NULL)
			i = findloop();
		ax = mu_printf("%s\n", i->name);
		i->live = DEAD;
	}
	mu_exit(ax);
}
