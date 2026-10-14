/*
 * c11.c - the MUTOS 1700 code generator, part 2: operand printing, the
 * degree and register calculations, switches, conditional branches,
 * reading the intermediate file (v7/cc/c11.c, ported from the original
 * binary - see c1.h and docs/C1_PORT_PLAN.md).
 *
 * Function order and the text offsets in the comments follow the
 * original (tests/realbin/c1map.txt).
 */
#include "c1.h"

#include <setjmp.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/*
 * 21b4 max - v7's.
 */
int
max(int a, int b)
{
	if (a > b)
		return a;
	return b;
}

/*
 * 21ce degree - delta: the address of a name is -2 (like a constant)
 * only for an external or static name, 1 otherwise (v7: -2 for any
 * AMPER) - an auto's address takes an instruction. The class test reads
 * the operand's class byte whatever it is, as the original does (for an
 * operand that is not a name, the low byte of its degree).
 */
int
degree(struct tnode *t)
{
	struct tnode *t1;

	if (t == 0 || t->op == 0)
		return 0;
	if (t->op == CON)
		return -3;
	if (t->op == AMPER) {
		if ((t->tr1->op == NAME && t->tr1->class == EXTERN)
		 || t->tr1->class == STATIC)
			return -2;
		return 1;
	}
	if (t->op == ITOL) {
		if ((t1 = isconstant(t)) != 0
		 && (t1->value >= 0 || t1->type == UNSIGN))
			return -2;
		if ((t1 = t->tr1)->type == UNSIGN && opdope[t1->op] & LEAF)
			return -1;
	}
	if ((opdope[t->op] & LEAF) != 0) {
		if (t->type == CHAR || t->type == FLOAT)
			return 1;
		return 0;
	}
	return t->degree;
}

/*
 * 2278 pname - x86: the MUTOS 1700 operand syntax. A constant is
 * "*<n>." or "#<n>." (pnum()), a long constant's word chosen by flag (a
 * flag above 10 - the "+" of A1+ - asks for the second, low word), a
 * floating constant its label "L<n>"; a name: an offset first ("<n>.+"
 * before a symbol, pnum() before a register), then the symbol (pbase())
 * and/or "(<register>)", or a register variable's register name; an
 * address "#" and the name.
 */
void
pname(struct tnode *p, int flag)
{
	int16_t i;

loop:
	switch (p->op) {

	case LCON:
		pnum(flag > 10 ? p->lvlo : p->lvhi);
		return;

	case CON:
		pnum(p->value);
		return;

	case FCON:
	case SFCON:
		printf("L%d", p->value > 0 ? p->value : (int16_t)-p->value);
		return;

	case NAME:
		i = p->offset;
		if (flag > 10)
			i += 2;
		if (i) {
			if (p->class == OFFS)
				pnum(i);
			else
				printf("%d.+", i);
			if (p->class == REG)
				regerr();
		}
		switch (p->class) {

		case SOFFS:
		case XOFFS:
			pbase(p);
			/* fall through */
		case OFFS:
			printf("(%s)", rname(p->regno));
			return;

		case EXTERN:
		case STATIC:
			pbase(p);
			return;

		case REG:
			printf("%s", rname(p->nloc));
			return;

		}
		error("Compiler error: pname");
		return;

	case AMPER:
		putchar('#');
		p = p->tr1;
		if (p->op == NAME && p->class == REG)
			regerr();
		goto loop;

	}
	error("pname called illegally");
}

/*
 * 23d6 regerr - v7's.
 */
void
regerr(void)
{
	error("Illegal use of register");
}

/*
 * 23e8 pbase - v7's: "L<n>" for a static, otherwise the name, at most
 * eight characters (copied into a buffer and printed "%.8s").
 */
void
pbase(struct tnode *p)
{
	if (p->class == SOFFS || p->class == STATIC)
		printf("L%d", p->nloc);
	else
		printf("%.8s", p->name);
}

/*
 * 244e pnum - new: a number with its size marker, "*<n>." for -128..127
 * (an 8-bit immediate or displacement), "#<n>." otherwise.
 */
void
pnum(int n)
{
	printf("%c%d.", ssize(n), (int16_t)n);
}

/*
 * 246c ssize - new.
 */
int
ssize(int n)
{
	if ((int16_t)n < -128 || (int16_t)n > 127)
		return '#';
	return '*';
}

/*
 * 248c xdcalc - delta: dcalc() alone (v7's char adjustment is gone).
 */
int
xdcalc(struct tnode *p, int nrleft)
{
	return dcalc(p, nrleft);
}

/*
 * 24a6 dcalc - delta, the 8086's addressability classes:
 *   - a constant: 0 -> 4, 1 -> 5, -128..127 -> 7 (an 8-bit immediate),
 *     others 8; an SFCON the same by its value word;
 *   - a long constant and the address of an external or static name: 8
 *     (an immediate); an FCON 12 (addressable, its label);
 *   - a name: 9 in a register, 12 otherwise;
 *   - *p: 12 only through a register variable usable as a base (SI, DI,
 *     BP - isregptr()) at offset 0 and not a long, 24 otherwise;
 *   - AUTOI/AUTOD and anything else by its degree, with a strict "<"
 *     against the free registers (v7: "<=") for 20.
 */
int
dcalc(struct tnode *p, int nrleft)
{
	struct tnode *p1;

	if (p == 0)
		return 0;
	switch (p->op) {

	case NAME:
		if (p->class == REG)
			return 9;
		return 12;

	case CON:
	case SFCON:
		if (p->value == 0)
			return 4;
		if (p->value == 1)
			return 5;
		if (p->value < -128 || p->value > 127)
			return 8;
		return 7;

	case FCON:
		return 12;

	case LCON:
		return 8;

	case AMPER:
		if (p->tr1->class == EXTERN || p->tr1->class == STATIC)
			return 8;
		break;

	case STAR:
		p1 = p->tr1;
		if (isregptr(p1) && p1->offset == 0 && p->type != LONG)
			return 12;
		return 24;
	}
	if (p->type == LONG)
		nrleft--;
	return p->degree < nrleft ? 20 : 24;
}

/*
 * 2588 notcompat - v7's.
 */
int
notcompat(struct tnode *p, int st, int op)
{
	int at;

	at = p->type;
	if (st == 0)		/* word, byte */
		return at != CHAR && at != INT && at != UNSIGN && at < PTR;
	if (st == 1)		/* word */
		return at != INT && at != UNSIGN && at < PTR;
	if (st == 9 && (at & XTYPE))
		return 0;
	st -= 2;
	if ((at & ~(TYPE + XTYPE)) != 0)
		at = 020;
	if ((at & ~TYPE) != 0)
		at = (at & TYPE) | 020;
	if (st == FLOAT && at == DOUBLE)
		at = FLOAT;
	if (p->op == NAME && p->class == REG && op == ASSIGN && st == CHAR)
		return 0;
	return st != at;
}

/*
 * 262e prins - v7's. Prints an instruction from instab or branchtab:
 * the first form, or the second when c is nonzero.
 */
void
prins(int op, int c, struct instab *itable)
{
	struct instab *insp;
	char *ip;

	for (insp = itable; insp->iop != 0; insp++) {
		if (insp->iop == op) {
			ip = c ? insp->str2 : insp->str1;
			if (ip == 0)
				break;
			printf("%s", ip);
			return;
		}
	}
	error("No match' for op %d", op);
}

/*
 * 267a collcon - delta: only "x + c" and "x + &name" for an external or
 * static name (v7 looks through an indirection first). Called with a
 * second argument (the register) the original ignores.
 */
int
collcon(struct tnode *p)
{
	int op;

	if (p->op == PLUS) {
		op = p->tr2->op;
		if (op == CON)
			return 1;
		if (op == AMPER && p->tr2->tr1->op == NAME
		 && (p->tr2->tr1->class == EXTERN || p->tr2->tr1->class == STATIC))
			return 1;
	}
	return 0;
}

/*
 * 26c4 isfloat - delta: 1 for a float, 2 for a double (v7 returns 'f'
 * for both - the PDP-11 suffix); 0 for a null tree.
 */
int
isfloat(struct tnode *t)
{
	if (t == 0)
		return 0;
	if ((opdope[t->op] & RELAT) != 0)
		t = t->tr1;
	if (t->type == FLOAT) {
		nfloat = 1;
		return 1;
	}
	if (t->type == DOUBLE) {
		nfloat = 1;
		return 2;
	}
	return 0;
}

/*
 * 270a arlength - v7's.
 */
int
arlength(int t)
{
	if (t >= PTR)
		return 2;
	switch (t) {

	case INT:
	case CHAR:
	case UNSIGN:
		return 2;

	case LONG:
		return 4;

	case FLOAT:
	case DOUBLE:
		return 8;
	}
	return 1024;
}

/*
 * 274c pswitch - x86. v7's three kinds of switch, in 8086 code:
 *   - direct: "sub ax,#/<min>" if needed, a bounds check and a jump
 *     table in the text segment ("seg cs / jmp @L<n>(bx)") - no
 *     ".data"/".text" around the table;
 *   - simple (fewer than 10 cases): breq() per case, "br" to the
 *     default;
 *   - hashed: "div bx" by the table size, the remainder indexing a
 *     jump table of per-bucket compare chains.
 */
static char dirsw[] =		/* data 04a8 */
	"cmp\tax,%c%d.\nbhi\tL%d\nshl\tax,#1\nxchg\tbx,ax\nseg\tcs\n"
	"jmp\t@L%d(bx)\nL%d:";
static char hashsw[] =		/* data 04ec */
	"sub\tdx,dx\nmov\tbx,#%d.\ndiv\tbx\nshl\tdx,*1\nmov\tbx,dx\nseg\tcs\n"
	"jmp\t@L%d(bx)\nL%d:";

void
pswitch(struct swtab *afp, struct swtab *alp, int deflab)
{
	int16_t ncase, i, j, tabs = 0, worst, best, range;
	struct swtab *swp, *fp, *lp;
	int16_t *poctab;

	fp = afp;
	lp = alp;
	if (fp == lp) {
		printf("jmp\tL%d\n", deflab);
		return;
	}
	isn++;
	if (sort(fp, lp))
		return;
	ncase = (int16_t)(lp - fp);
	lp--;
	range = (int16_t)(lp->swval - fp->swval);
	/* direct switch */
	if (range > 0 && range <= 3 * ncase) {
		if (fp->swval)
			printf("sub\tax,#/%x\n", (uint16_t)fp->swval);
		printf(dirsw, ssize(range), range, deflab, isn, isn);
		isn++;
		for (i = fp->swval; ; i++) {
			if (i == fp->swval) {
				printf("L%d\n", fp->swlab);
				if (fp == lp)
					break;
				fp++;
			} else
				printf("L%d\n", deflab);
		}
		return;
	}
	/* simple switch */
	if (ncase < 10) {
		for (fp = afp; fp <= lp; fp++)
			breq(fp->swval, fp->swlab);
		printf("br\tL%d\n", deflab);
		return;
	}
	/* hash switch */
	best = 077777;
	poctab = getblk((ncase + 2) / 2 * (int)sizeof(*poctab));
	for (i = ncase / 4; i <= ncase / 2; i++) {
		for (j = 0; j < i; j++)
			poctab[j] = 0;
		for (swp = fp; swp <= lp; swp++)
			poctab[urem16(swp->swval, i)]++;
		worst = 0;
		for (j = 0; j < i; j++)
			if (poctab[j] > worst)
				worst = poctab[j];
		if (i * worst < best) {
			tabs = i;
			best = (int16_t)(i * worst);
		}
	}
	i = isn++;
	printf(hashsw, tabs, i, i);
	isn++;
	for (i = 0; i < tabs; i++)
		printf("L%d\n", isn + i);
	for (i = 0; i < tabs; i++) {
		printf("L%d:", isn++);
		for (swp = fp; swp <= lp; swp++) {
			if (urem16(swp->swval, tabs) == i)
				breq(udiv16(swp->swval, tabs), swp->swlab);
		}
		printf("br\tL%d\n", deflab);
	}
}

/*
 * 29cc breq - x86.
 */
void
breq(int v, int l)
{
	if ((int16_t)v == 0)
		printf("test\tax,ax\n");
	else
		printf("cmp\tax,%c%d.\n", ssize(v), (int16_t)v);
	printf("beq\tL%d\n", l);
}

/*
 * 2a16 sort - v7's.
 */
int
sort(struct swtab *afp, struct swtab *alp)
{
	struct swtab *cp, *fp, *lp;
	int intch;
	int16_t t;

	fp = afp;
	lp = alp;
	while (fp < --lp) {
		intch = 0;
		for (cp = fp; cp < lp; cp++) {
			if (cp->swval == cp[1].swval) {
				error("Duplicate case (%d)", cp->swval);
				return 1;
			}
			if (cp->swval > cp[1].swval) {
				intch++;
				t = cp->swval;
				cp->swval = cp[1].swval;
				cp[1].swval = t;
				t = cp->swlab;
				cp->swlab = cp[1].swlab;
				cp[1].swlab = t;
			}
		}
		if (intch == 0)
			break;
	}
	return 0;
}

/*
 * 2a98 ispow2 - v7's.
 */
int
ispow2(struct tnode *tree)
{
	int16_t d;

	if (!isfloat(tree) && tree->tr2->op == CON) {
		d = tree->tr2->value;
		if (d > 1 && (d & (d - 1)) == 0)
			return d;
	}
	return 0;
}

/*
 * 2ad0 pow2 - delta: the unsigned / and % (UDIV, UMOD and their
 * assignments) are reduced as well; a division becomes an unsigned right
 * shift (ULSH, ASULSH) by a positive count (v7: a negative one).
 */
struct tnode *
pow2(struct tnode *tree)
{
	int d, i;

	if ((d = ispow2(tree)) != 0) {
		for (i = 0; (d >>= 1) != 0; i++)
			;
		tree->tr2->value = (int16_t)i;
		switch (tree->op) {

		case TIMES:
			tree->op = LSHIFT;
			break;

		case ASTIMES:
			tree->op = ASLSH;
			break;

		case DIVIDE:
		case UDIV:
			tree->op = ULSH;
			tree->tr2->value = (int16_t)i;
			break;

		case ASDIV:
		case ASUDIV:
			tree->op = ASULSH;
			tree->tr2->value = (int16_t)i;
			break;

		case MOD:
		case UMOD:
			tree->op = AND;
			tree->tr2->value = (int16_t)((1 << i) - 1);
			break;

		case ASMOD:
		case ASUMOD:
			tree->op = ASAND;
			tree->tr2->value = (int16_t)((1 << i) - 1);
			break;

		default:
			error("pow2 botch");
		}
		tree = optim(tree);
	}
	return tree;
}

/*
 * 2bba cbranch - x86. v7's structure. A relation's value is tested with
 * cctab as the code tables leave it - the operands in Intel order, so
 * without v7's maprel[] (only "op + 200" for a test against 0); a
 * floating value outside a relation through "call ftest / sahf", which
 * pops the floating-point stack model.
 */
void
cbranch(struct tnode *atree, int albl, int cond, int areg)
{
	int l1, op;
	int lbl, reg;
	struct tnode *tree;

	lbl = albl;
	reg = areg;
again:
	if ((tree = atree) == 0)
		return;
	switch (tree->op) {

	case LOGAND:
		if (cond) {
			cbranch(tree->tr1, l1 = isn++, 0, reg);
			cbranch(tree->tr2, lbl, 1, reg);
			label(l1);
		} else {
			cbranch(tree->tr1, lbl, 0, reg);
			cbranch(tree->tr2, lbl, 0, reg);
		}
		return;

	case LOGOR:
		if (cond) {
			cbranch(tree->tr1, lbl, 1, reg);
			cbranch(tree->tr2, lbl, 1, reg);
		} else {
			cbranch(tree->tr1, l1 = isn++, 1, reg);
			cbranch(tree->tr2, lbl, 0, reg);
			label(l1);
		}
		return;

	case EXCLA:
		cbranch(tree->tr1, lbl, !cond, reg);
		return;

	case SEQNC:
		rcexpr(tree->tr1, efftab, reg);
		atree = tree->tr2;
		goto again;

	case ITOL:
		tree = tree->tr1;
		break;
	}
	op = tree->op;
	if (opdope[op] & RELAT
	 && tree->tr1->op == ITOL && tree->tr2->op == ITOL) {
		tree->tr1 = tree->tr1->tr1;
		tree->tr2 = tree->tr2->tr1;
		if (op >= LESSEQ && op <= GREAT
		 && (tree->tr1->type == UNSIGN || tree->tr2->type == UNSIGN))
			tree->op = (int16_t)(op = op + LESSEQP - LESSEQ);
	}
	if (tree->type == LONG
	  || (opdope[op] & RELAT && tree->tr1->type == LONG)) {
		longrel(tree, lbl, cond, reg);
		return;
	}
	rcexpr(tree, cctab, reg);
	op = tree->op;
	if ((opdope[op] & RELAT) == 0) {
		if (isfloat(tree)) {
			printf("call\tftest\nsahf\n");
			fpdepth--;
		}
		op = NEQUAL;
	} else {
		l1 = tree->tr2->op;
		if ((l1 == CON || l1 == SFCON) && tree->tr2->value == 0)
			op += 200;		/* special for ptr tests */
	}
	branch(lbl, op, !cond);
}

/*
 * 2e32 branch - v7's, with "jmp" for v7's "jbr".
 */
void
branch(int lbl, int aop, int c)
{
	int op;

	if ((op = aop) != 0)
		prins(op, c, branchtab);
	else
		printf("jmp");
	printf("\tL%d\n", lbl);
}

/*
 * 2e6a longrel - x86. v7's structure: cexpr() with cctab and xlongrel()'s
 * branches; when that fails, the value in a register pair tested word by
 * word ("test hi,hi / jne / test lo,lo") and branched on. In that case
 * the original stores the label it makes in the variable that holds the
 * saved xzero (text 2f83), so xzero comes back as that label number.
 */
void
longrel(struct tnode *atree, int lbl, int cond, int reg)
{
	int16_t xl1, xl2, xo, xz;
	int op, isrel;
	struct tnode *tree;

	reorder(&atree, cctab, reg);
	tree = atree;
	isrel = 0;
	if (opdope[tree->op] & RELAT) {
		isrel++;
		op = tree->op;
	} else
		op = NEQUAL;
	if (!cond)
		op = notrel[op - EQUAL];
	xl1 = xlab1;
	xl2 = xlab2;
	xo = xop;
	xlab1 = (int16_t)lbl;
	xlab2 = 0;
	xop = (int16_t)op;
	xz = xzero;
	xzero = !isrel || (tree->tr2->op == ITOL && tree->tr2->tr1->op == CON
	    && tree->tr2->tr1->value == 0);
	if (cexpr(tree, cctab, reg) < 0) {
		reg = rcexpr(tree, regtab, reg);
		printf("test\t%s,%s\njne\tL%d\ntest\t%s,%s\n", rname(reg),
		    rname(reg), cond ? xlab1 : isn, rname(reg - 1), rname(reg - 1));
		xz = isn++;
		branch(xlab1, op, 0);
		printf("L%d:\t", xz);
	}
	xlab1 = xl1;
	xlab2 = xl2;
	xop = xo;
	xzero = xz;
}

/*
 * 2fc4 xlongrel - v7's. lrtab is the binary's (data 0536, c1tab.c).
 * xzero can hold a label number here (see longrel()) and xop can be 0
 * (an 'X' template reached outside longrel()): the original then reads
 * its data segment far outside lrtab - refused.
 */
int
xlongrel(int f)
{
	int op, bno;

	op = xop;
	if ((xzero != 0 && xzero != 1) || op < EQUAL || op > GREATP)
		c1trap("xlongrel() reads outside lrtab[] (a stale xzero or xop)");
	if (f == 0) {
		if ((bno = lrtab[xzero][0][op - EQUAL]) != 0)
			branch(xlab1, bno, 0);
		if ((bno = lrtab[xzero][1][op - EQUAL]) != 0) {
			xlab2 = isn++;
			branch(xlab2, bno, 0);
		}
		if (lrtab[xzero][2][op - EQUAL] == 0)
			return 1;
	} else {
		branch(xlab1, lrtab[xzero][2][op - EQUAL], 0);
		if (xlab2)
			label(xlab2);
	}
	return 0;
}

/*
 * 307a label - v7's.
 */
void
label(int l)
{
	printf("L%d:", l);
}

/*
 * 3090 popstk - x86: "add sp,<n>".
 */
void
popstk(int a)
{
	if (a == 0)
		return;
	printf("add\tsp,%c%d.\n", (int16_t)a < 0200 ? '*' : '#', (int16_t)a);
}

/*
 * 30bc error - v7's.
 */
void
error(const char *s, ...)
{
	va_list ap;

	nerror++;
	fprintf(stderr, "%d: ", line);
	va_start(ap, s);
	vfprintf(stderr, s, ap);
	va_end(ap);
	putc('\n', stderr);
}

/*
 * 311e getree - x86: v7's structure, the MUTOS 1700 output. Differences
 * from v7, by case:
 *   - BDATA: ".byte" lines of at most nine "/%x" values;
 *   - SYMDEF ".globl\t%.8s" (v7 allows an empty name), CSPACE ".comm"
 *     with "%u", SSPACE ".blkb\t%u.", SAVE the 8086 prologue, RETRN
 *     "|RTYP %d" before "jmp cret", PROFIL through mcount with AX;
 *   - SETSTK: the frame is the argument minus 4 (bp, di, si are pushed
 *     by SAVE); from stkthresh on (option s<n>, default 100) through
 *     chkstk, below by "sub sp";
 *   - SNAME, ANAME, RNAME as "| name=..." comment lines with the whole
 *     name (v7: "~name=..." without the underscore), RNAME by register
 *     name;
 *   - SETREG prints "|NREG %d";
 *   - NLABEL "%.8s:" without a newline;
 *   - FCON: atof() under setjmp() - a SIGFPE (fpeintr) gives the
 *     constant 1.0 (the float at data 055a);
 *   - EXPR, CBRANCH and C3BRANCH: the trees are printed before (option
 *     b) and after (option a) optim(); code is generated from register
 *     nreg (v7: 0, registers are handed out downwards here); C3BRANCH
 *     with "test dx,dx" for a long and "call ftest / sahf" for a
 *     floating value outside a relation (counted off the floating-point
 *     stack model);
 *   - lbl, lbl2, lbl3 and s are statics (data 47b2, 47b4, 47b6, 47b8).
 */
#define	STKS	100

static int16_t	lbl, lbl2, lbl3;	/* 47b2, 47b4, 47b6 */
static char	s[NCPS + 1 + 64];	/* 47b8: s[9] - a longer name runs past it */

void
getree(void)
{
	struct tnode *expstack[STKS];
	struct tnode **sp;
	int16_t t, op, v;
	struct swtab *swp;
	char numbuf[64];
	char xname[NCPS + 2 + 64];
	struct tnode *np, *fp, *lp, *sap;
	volatile int16_t cond = 0;	/* volatile: setjmp() in the FCON case */
	char *n;

	curbase = funcbase;
	sp = expstack;
	for (;;) {
		if (sp >= &expstack[STKS])
			error("Stack overflow botch");
		op = geti();
		if ((op & 0177400) != 0177000) {
			error("Intermediate file error");
			exit(1);
		}
		lbl = 0;
		switch (op &= 0377) {

		case SINIT:
			printf("/%x\n", (uint16_t)geti());
			break;

		case EOFC:
			return;

		case BDATA:
			if (geti() == 1) {
				t = 0;
				printf(".byte\t");
				for (;;) {
					printf("/%x", (uint16_t)geti());
					t++;
					if (geti() != 1)
						break;
					if (t == 9) {
						t = 0;
						printf("\n.byte\t");
					} else
						putchar(',');
				}
				if (t)
					putchar('\n');
			}
			break;

		case PROG:
			printf(".text\n");
			break;

		case DATA:
			printf(".data\n");
			break;

		case BSS:
			printf(".bss\n");
			break;

		case SYMDEF:
			outname(s);
			printf(".globl\t%.8s\n", s);
			sfuncr.nloc = 0;
			break;

		case RETRN:
			printf("|RTYP %d\n", geti());
			printf("jmp\tcret\n");
			break;

		case CSPACE:
			n = outname(s);
			v = geti();
			printf(".comm\t%.8s,%u\n", n, (uint16_t)v);
			break;

		case SSPACE:
			t = geti();
			printf(".blkb\t%u.\n", (uint16_t)t);
			totspace += (uint16_t)t;
			break;

		case EVEN:
			printf(".even\n");
			break;

		case SAVE:
			printf("push\tbp\nmov\tbp,sp\npush\tdi\npush\tsi\n");
			break;

		case SETSTK:
			t = (int16_t)(geti() - 4);
			if (t == 0)
				break;
			if (t >= stkthresh) {
				printf("mov\tax,%c%d.\n", t < 0200 ? '*' : '#', t);
				printf("call\tchkstk\n");
			} else
				printf("sub\tsp,%c%d.\n", t < 0200 ? '*' : '#', t);
			break;

		case PROFIL:
			t = geti();
			printf("mov\tax,#L%d\ncall\tmcount\n", t);
			printf(".bss\nL%d:.=.+2\n.text\n", t);
			break;

		case SNAME:
			n = outname(s);
			v = geti();
			printf("| %s=L%d\n", n, v);
			break;

		case ANAME:
			n = outname(s);
			v = geti();
			printf("| %s=%d.\n", n, v);
			break;

		case RNAME:
			n = outname(s);
			v = geti();
			printf("| %s=%s\n", n, regname[v]);
			break;

		case SWIT:
			t = geti();
			line = geti();
			curbase = funcbase;
			while (swp = getblk(sizeof(*swp)), (swp->swlab = geti()) != 0)
				swp->swval = geti();
			pswitch((struct swtab *)funcbase, swp, t);
			break;

		case C3BRANCH:		/* for fortran [sic] */
			lbl = geti();
			lbl2 = geti();
			lbl3 = geti();
			goto xpr;

		case CBRANCH:
			lbl = geti();
			cond = geti();
			/* fall through */

		case EXPR:
		xpr:
			line = geti();
			if (sp != &expstack[1]) {
				error("Expression input botch");
				exit(1);
			}
			nstack = 0;
			--sp;
			treedump("b", *sp);
			if (dflag_b)
				tprint(*sp, 0);
			*sp = optim(*sp);
			treedump("a", *sp);
			if (dflag_a)
				tprint(*sp, 0);
			if (op == CBRANCH)
				cbranch(*sp, lbl, cond, nreg);
			else if (op == EXPR)
				rcexpr(*sp, efftab, nreg);
			else {
				if ((*sp)->type == LONG) {
					rcexpr(tnode(RFORCE, (*sp)->type, *sp, 0),
					    efftab, nreg);
					printf("test\tdx,dx\nbgt\tL%d\n", lbl3);
					printf("blt\tL%d\ntest\tax,ax\n", lbl);
					printf("bgt\tL%d\nbr\tL%d\n", lbl3, lbl2);
				} else {
					rcexpr(*sp, cctab, nreg);
					if ((opdope[(*sp)->op] & RELAT) == 0
					 && isfloat(*sp)) {
						printf("call\tftest\nsahf\n");
						fpdepth--;
					}
					printf("bgt\tL%d\n", lbl3);
					printf("blt\tL%d\nbr\tL%d\n", lbl, lbl2);
				}
			}
			curbase = funcbase;
			break;

		case NAME:
			t = geti();
			if (t == EXTERN) {
				np = getblk(sizeof(*np));
				np->type = geti();
				/* outname() pads to nine bytes: the original
				 * writes the ninth into the next block */
				outname(xname);
				memcpy(np->name, xname, NCPS);
			} else {
				np = getblk(sizeof(*np));
				np->type = geti();
				np->nloc = geti();
			}
			np->op = NAME;
			np->class = (signed char)t;
			np->regno = 0;
			np->offset = 0;
			*sp++ = np;
			break;

		case CON:
			t = geti();
			v = geti();
			*sp++ = tconst(v, t);
			break;

		case LCON:
			geti();	/* ignore type, assume long */
			t = geti();
			op = geti();
			if ((t == 0 && op >= 0) || (t == -1 && op < 0)) {
				*sp++ = tnode(ITOL, LONG, tconst(op, INT), 0);
				break;
			}
			lp = getblk(sizeof(*lp));
			lp->op = LCON;
			lp->type = LONG;
			setlvalue(lp, (int32_t)((uint32_t)(uint16_t)t << 16)
			    + (uint16_t)op);
			*sp++ = lp;
			break;

		case FCON:
			t = geti();
			outname(numbuf);
			fp = getblk(sizeof(*fp));
			fp->op = FCON;
			fp->type = t;
			fp->value = isn++;
			if (setjmp(fpejmp)) {
				/* flds of the float 1.0 at data 055a */
				memset(fp->fvalue, 0, 8);
				fp->fvalue[7] = 0201;
			} else
				c1_atof(fp, numbuf);
			*sp++ = fp;
			break;

		case FSEL:
			--sp;
			v = geti();
			*sp = tnode(FSEL, v, *sp, 0);
			t = geti();
			np = tconst(t, INT);
			v = geti();
			(*sp++)->tr2 = tnode(COMMA, INT, tconst(v, INT), np);
			break;

		case STRASG:
			sap = getblk(sizeof(*sap));
			sap->op = STRASG;
			sap->type = geti();
			sap->mask = geti();
			sap->tr1 = *--sp;
			sap->tr2 = 0;
			*sp++ = sap;
			break;

		case NULLOP:
			*sp++ = tnode(0, 0, 0, 0);
			break;

		case LABEL:
			label(geti());
			break;

		case NLABEL:
			n = outname(s);
			printf("%.8s:", n);
			break;

		case RLABEL:
			n = outname(s);
			printf("%.8s:\n", n);
			break;

		case BRANCH:
			branch(geti(), 0, 0);
			break;

		case SETREG:
			nreg = (int16_t)(geti() - 1);
			printf("|NREG %d\n", nreg);
			break;

		default:
			if (op >= NOPS + 2) {
				/* opdope[op] past its 124 entries: the
				 * original reads whatever follows it */
				error("Intermediate file error");
				exit(1);
			}
			if (opdope[op] & BINARY) {
				if (sp < &expstack[1]) {
					error("Binary expression botch");
					exit(1);
				}
				np = *--sp;
				--sp;
				v = geti();
				*sp = tnode(op, v, *sp, np);
				sp++;
			} else {
				v = geti();
				sp[-1] = tnode(op, v, sp[-1], 0);
			}
			break;
		}
	}
}

/*
 * 3924 geti - v7's: a 16-bit word, low byte first.
 */
int16_t
geti(void)
{
	int i;

	i = getchar();
	i += getchar() * 256;
	return (int16_t)i;
}

/*
 * 3978 outname - v7's: a NUL-ended name, padded with NULs to nine bytes.
 */
char *
outname(char *str)
{
	char *p, c;
	int n;

	p = str;
	n = 0;
	while ((c = (char)getchar()) != 0) {
		*p++ = c;
		n++;
	}
	do {
		*p++ = 0;
	} while (n++ < 8);
	return str;
}

/*
 * 39ca strasg - x86: a structure of one word is assigned as an int, of
 * two as a long; a larger one by "repz / movs" between the two
 * addresses STRSET pushes, SI and DI saved around it.
 */
void
strasg(struct tnode *atp)
{
	struct tnode *tp;
	int16_t nwords;

	nwords = (int16_t)((uint16_t)atp->mask / 2);
	tp = atp->tr1;
	if (tp->op != ASSIGN) {
		if (tp->op == RFORCE) {	/* function return */
			if (sfuncr.nloc == 0) {
				sfuncr.nloc = isn++;
				printf(".bss\nL%d:.=.+%d\n.text\n", sfuncr.nloc,
				    (int16_t)(nwords * 2));
			}
			atp->tr1 = tnode(ASSIGN, STRUCT, &sfuncr, tp->tr1);
			strasg(atp);
			printf("mov\tax,#L%d\n", sfuncr.nloc);
			return;
		}
		if (tp->op == CALL) {
			rcexpr(tp, efftab, nreg);
			return;
		}
		error("Illegal structure operation");
		return;
	}
	tp->tr2 = strfunc(tp->tr2);
	if (nwords == 1)
		setype(tp, INT);
	else if (nwords == 2)
		setype(tp, LONG);
	else {
		if ((tp->tr1->op != NAME && tp->tr1->op != STAR)
		 || (tp->tr2->op != NAME && tp->tr2->op != STAR)) {
			error("unimplemented structure assignment");
			return;
		}
		tp->tr1 = tnode(AMPER, STRUCT + PTR, tp->tr1, 0);
		tp->tr2 = tnode(AMPER, STRUCT + PTR, tp->tr2, 0);
		tp->op = STRSET;
		tp->type = STRUCT + PTR;
		tp = optim(tp);
		rcexpr(tp, efftab, nreg);
		printf("pop\tbx\npop\tcx\npush\tdi\npush\tsi\nmov\tsi,bx\nmov\tdi,cx\n");
		printf("mov\tcx,*%d.\n", nwords);
		printf("repz\nmovs\n");
		printf("pop\tsi\npop\tdi\n");
		return;
	}
	rcexpr(tp, efftab, nreg);
}

/*
 * 3b42 setype - v7's.
 */
void
setype(struct tnode *p, int t)
{
	for (;; p = p->tr1) {
		p->type = (int16_t)t;
		if (p->op == AMPER)
			t = decref(t);
		else if (p->op == STAR)
			t = incref(t);
		else if (p->op == ASSIGN)
			setype(p->tr2, t);
		else if (p->op != PLUS)
			break;
	}
}

/*
 * 3b8a decref - v7's.
 */
int
decref(int at)
{
	int t;

	t = at;
	if ((t & ~TYPE) == 0) {
		error("Illegal indirection");
		return t;
	}
	return (((int16_t)t >> TYLEN) & ~TYPE) | (t & TYPE);
}

/*
 * 3bbe incref - v7's.
 */
int
incref(int t)
{
	return (int16_t)((((t & ~TYPE) << TYLEN) | (t & TYPE) | PTR));
}

/*
 * 3bdc tprint - new: the debug traces' tree printer (options a, b, c,
 * r). Each node on a line: the operator's name, the type ("int ptr"),
 * then for a name its class (octal), regno, offset and nloc - an
 * external name's nloc is the first two bytes of the name -, for a CON
 * its value, unsigned, for anything else the word at offset 4 (degree;
 * a long constant's high word, a floating constant's label). Operands
 * follow indented, "<" the left, ">" the right.
 */
static char *tynames[8] = {	/* data 095a */
	"int", "char", "float", "double", "struct", "rstruct", "long", "unsign"
};
static char *xtnames[4] = {	/* data 096a */
	"", "ptr", "func", "array"
};

/*
 * The printer itself writes to a stream: stdout for the traces, the file
 * named by the environment variable C1PORT_TREES for treedump() - a
 * test hook of the port, not of the original (see tests/realbin/
 * c1trees.py, which writes the original's trees in the same form).
 */
static void
tprint1(FILE *f, struct tnode *t, int d)
{
	int i;

	if (t == 0)
		return;
	/* the original prints opntab[op] before checking op's range */
	fprintf(f, "%s ", t->op >= 0 && t->op < NOPS ? opntab[t->op] : "?");
	switch (t->op) {
	case NAME:
		fprintf(f, "%s %s %o %d %d %d", tynames[t->type & 07],
		    xtnames[(t->type & 030) >> 3], (uint16_t)t->class,
		    t->regno, t->offset, t->nloc);
		break;
	case CON:
		fprintf(f, "%s %s %u", tynames[t->type & 07],
		    xtnames[(t->type & 030) >> 3], (uint16_t)t->value);
		break;
	default:
		fprintf(f, "%s %s %d", tynames[t->type & 07],
		    xtnames[(t->type & 030) >> 3], t->degree);
		break;
	}
	fprintf(f, "\n");
	if (t->op < 0 || t->op > 121) {
		fprintf(f, "unknown op %o\n", (uint16_t)t->op);
		return;
	}
	if (opdope[t->op] & LEAF)
		return;
	for (i = 0; i < d; i++)
		putc(' ', f);
	putc('<', f);
	putc(' ', f);
	tprint1(f, t->tr1, d + 1);
	if ((opdope[t->op] & BINARY) == 0)
		return;
	for (i = 0; i < d; i++)
		putc(' ', f);
	putc('>', f);
	putc(' ', f);
	tprint1(f, t->tr2, d + 1);
}

void
tprint(struct tnode *t, int d)
{
	tprint1(stdout, t, d);
}

/*
 * Test hook: getree() calls it with each expression tree before ("b")
 * and after ("a") optim(), at the points where the original's options
 * b and a print them. With C1PORT_TREES set, the tree goes to that file
 * after a line "@@ <tag> <line>".
 */
void
treedump(const char *tag, struct tnode *t)
{
	static FILE *f;
	static int checked;
	const char *path;

	if (!checked) {
		checked = 1;
		if ((path = getenv("C1PORT_TREES")) != 0 && *path)
			f = fopen(path, "w");
	}
	if (f == 0)
		return;
	fprintf(f, "@@ %s %d\n", tag, line);
	tprint1(f, t, 0);
	fflush(f);
}
