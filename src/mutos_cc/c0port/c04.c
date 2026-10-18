/*
 * c04.c - types, the intermediate code and small helpers: v7/cc/c04.c
 * as the original MUTOS 1700 c0 has it (text 41ec..4a1e,
 * tests/realbin/c0map.txt). Every function does what v7's does.
 */
#include "c0.h"

#include <stdarg.h>

/*
 * 41ec decref - same. Reduce the degree-of-reference by one. e.g. turn
 * "ptr-to-int" into "int".
 */
int16_t
decref(int16_t at)
{
	int16_t t;

	t = at;
	if ((t & ~TYPE) == 0) {
		error("Illegal indirection");
		return t;
	}
	return (int16_t)(((t >> TYLEN) & ~TYPE) | (t & TYPE));
}

/*
 * 4220 incref - same. Increase the degree of reference by one; e.g.
 * turn "int" to "ptr-to-int".
 */
int16_t
incref(int16_t t)
{
	return (int16_t)((((uint16_t)t & ~TYPE) << TYLEN) | (t & TYPE) | PTR);
}

/*
 * 423e cbranch - same. Make a tree that causes a branch to lbl if the
 * tree's value is non-zero together with the cond.
 */
void
cbranch(uint16_t t, int16_t lbl, int16_t cond)
{
	treeout(t, 0);
	outcode("BNNN", CBRANCH, lbl, cond, line);
}

/*
 * 426a rcexpr - same. Write out a tree.
 */
void
rcexpr(uint16_t atp)
{
	uint16_t tp;

	/*
	 * Special optimization
	 */
	if (OP(tp = atp) == INIT && OP(TR1(tp)) == CON) {
		if (TYP(tp) == CHAR) {
			outcode("B1N0", BDATA, VALUE(TR1(tp)));
			return;
		} else if (TYP(tp) == INT || TYP(tp) == UNSIGN) {
			outcode("BN", SINIT, VALUE(TR1(tp)));
			return;
		}
	}
	treeout(tp, 0);
	outcode("BN", EXPR, line);
}

/*
 * 42d6 treeout - same. Write out a tree, operands first; a structure
 * value not taken by address gets a STRASG.
 */
void
treeout(uint16_t atp, int16_t isstruct)
{
	uint16_t tp;
	uint16_t hp;
	int16_t nextisstruct;

	if ((tp = atp) == 0) {
		outcode("B", NULLOP);
		return;
	}
	nextisstruct = TYP(tp) == STRUCT;
	switch (OP(tp)) {

	case NAME:
		hp = TR1(tp);
		if (HCLASS(hp) == TYPEDEF)
			error("Illegal use of type name");
		outcode("BNN", NAME, HCLASS(hp) == 0 ? STATIC : HCLASS(hp),
		    TYP(tp));
		if (HCLASS(hp) == EXTERN)
			outcode("S", HNAME(hp));
		else
			outcode("N", HOFFSET(hp));
		break;

	case LCON:
		outcode("BNNN", OP(tp), TYP(tp), LVHI(tp), LVLO(tp));
		break;

	case CON:
		outcode("BNN", OP(tp), TYP(tp), VALUE(tp));
		break;

	case FCON:
		outcode("BNF", OP(tp), TYP(tp), CSTR(tp));
		break;

	case STRING:
		outcode("BNNN", NAME, STATIC, TYP(tp), TR1(tp));
		break;

	case FSEL:
		treeout(TR1(tp), nextisstruct);
		outcode("BNNN", OP(tp), TYP(tp), BITOFFS(TR2(tp)), FLEN(TR2(tp)));
		break;

	case ETYPE:
		error("Illegal use of type");
		break;

	case AMPER:
		treeout(TR1(tp), 1);
		outcode("BN", OP(tp), TYP(tp));
		break;


	case CALL:
		treeout(TR1(tp), 1);
		treeout(TR2(tp), 0);
		outcode("BN", CALL, TYP(tp));
		break;

	default:
		treeout(TR1(tp), nextisstruct);
		if (opdope(OP(tp)) & BINARY)
			treeout(TR2(tp), nextisstruct);
		outcode("BN", OP(tp), TYP(tp));
		break;
	}
	if (nextisstruct && isstruct == 0)
		outcode("BNN", STRASG, STRUCT, STRSIZE(STRP(tp)));
}

/*
 * 44c0 branch - same. Generate a branch.
 */
void
branch(int16_t lab)
{
	outcode("BN", BRANCH, lab);
}

/*
 * 44da label - same. Generate a label.
 */
void
label(int16_t l)
{
	outcode("BN", LABEL, l);
}

/*
 * 44f4 plength - same. ap is a tree node whose type is some kind of
 * pointer; return the size of the object to which the pointer points.
 */
int16_t
plength(uint16_t ap)
{
	int16_t t, l;
	uint16_t p;

	p = ap;
	if (p == 0 || ((t = TYP(p)) & ~TYPE) == 0)	/* not a reference */
		return 1;
	TYP(p) = decref(t);
	l = length(p);
	TYP(p) = t;
	return l;
}

/*
 * 453e length - same (a jump table on t & TYPE; the product long,
 * through libc.a's almul). Return the number of bytes in the object
 * whose tree node is acs.
 */
int16_t
length(uint16_t acs)
{
	int16_t t, elsz;
	int32_t n;
	uint16_t cs;
	int16_t nd;

	cs = acs;
	t = TYP(cs);
	n = 1;
	nd = 0;
	while ((t & XTYPE) == ARRAY) {
		t = decref(t);
		n = (int32_t)((uint32_t)n * (uint32_t)(int32_t)WI(SUBSP(cs) + 2 * nd));
		nd++;
	}
	if ((t & ~TYPE) == FUNC)
		return 0;
	if (t >= PTR)
		elsz = SZPTR;
	else switch (t & TYPE) {

	case INT:
	case UNSIGN:
		elsz = SZINT;
		break;

	case CHAR:
		elsz = 1;
		break;

	case FLOAT:
		elsz = SZFLOAT;
		break;

	case LONG:
		elsz = SZLONG;
		break;

	case DOUBLE:
		elsz = SZDOUB;
		break;

	case STRUCT:
		if ((elsz = STRSIZE(STRP(cs))) == 0)
			error("Undefined structure");
		break;
	default:
		error("Compiler error (length)");
		return 0;
	}
	n = (int32_t)((uint32_t)n * (uint32_t)(int32_t)elsz);
	if (n >= 50000) {
		error("Warning: very large data structure");
		nerror--;
	}
	return (int16_t)n;
}

/*
 * 4646 rlength - same. The number of bytes in an object, rounded up to
 * a word.
 */
int16_t
rlength(uint16_t cs)
{
	return (int16_t)((length(cs) + ALIGN) & ~ALIGN);
}

/*
 * 465c simplegoto - same. After an "if (...) goto", look to see if the
 * transfer is to a simple label.
 */
int16_t
simplegoto(void)
{
	uint16_t csp;

	if ((peeksym = symbol()) == NAME && nextchar() == ';') {
		csp = csym;
		if (HBLKLEV(csp) == 0)
			pushdecl(csp);
		if (HCLASS(csp) == 0 && HTYP(csp) == 0) {
			HTYP(csp) = ARRAY;
			HFLAG(csp) = (signed char)(HFLAG(csp) | FLABL);
			if (HOFFSET(csp) == 0)
				HOFFSET(csp) = isn++;
		}
		if ((HCLASS(csp) == 0 || HCLASS(csp) == STATIC)
		 && HTYP(csp) == ARRAY) {
			peeksym = -1;
			return HOFFSET(csp);
		}
	}
	return 0;
}

/*
 * 46cc nextchar - same. Return the next non-white-space character.
 */
int16_t
nextchar(void)
{
	while (spnextchar() == ' ')
		peekc = 0;
	return peekc;
}

/*
 * 46e8 spnextchar - same. Return the next character, translating all
 * white space to blank and handling line-ends.
 */
int16_t
spnextchar(void)
{
	int16_t c;

	if ((c = peekc) == 0)
		c = c0_getchar();
	if (c == '\t' || c == '\014')	/* FF */
		c = ' ';
	else if (c == '\n') {
		c = ' ';
		if (inhdr == 0)
			line++;
		inhdr = 0;
	} else if (c == '\001') {	/* SOH, insert marker */
		inhdr++;
		c = ' ';
	}
	peekc = c;
	return c;
}

/*
 * 4754 chconbrk - same. Is a break or continue legal?
 */
void
chconbrk(int16_t l)
{
	if (l == 0)
		error("Break/continue error");
}

/*
 * 476c dogoto - same. The goto statement (one simplegoto() did not
 * take: "goto *p;", a name that is not a label).
 *
 * v7's "*cp++ = tree();" fetches cp before tree() runs: the tree is
 * stored where cp pointed then - in the frame some earlier call left on
 * the stack -, and the cp tree() returns with, which points at the same
 * tree in tree()'s own operand stack, is moved past it. Where that
 * earlier frame is dead the store is invisible and build(STAR) gets the
 * tree, as here. The port refuses where it is not: a syntax error (tree()
 * returns 0 and build() takes whatever word the dead frame left), a cp
 * last set by funchead() (it points at the saved SI of the statement()
 * frame now above it - cfunc()'s sloc, whose labels then come out wrong),
 * or a goto nested five or more statements deeper than the last
 * expression (a statement frame is 18 bytes, tree()'s 180: from about ten
 * levels on, with switch and for frames between them sooner, the old
 * operand stack lies under live frames).
 */
void
dogoto(void)
{
	uint16_t np;
	uint16_t t;
	int from_funchead = c0_cp_funchead;
	int depth = c0_tree_stmtdepth;

	t = tree();
	if (t == 0)
		c0trap("a goto whose expression has a syntax error (the original "
		    "builds from a word a dead stack frame left)");
	if (from_funchead)
		c0trap("a goto expression right after a register parameter's copy "
		    "(the original stores it over a saved register)");
	if (c0_stmtdepth - depth >= 5)
		c0trap("a goto expression nested deep below the last expression (the "
		    "original stores it where live frames may be)");
	if (WU(cp) != t)
		c0trap("tree() returned with its result not at cp");
	cp += 2;
	build(STAR);
	cp -= 2;
	chkw(np = WU(cp), -1);
	rcexpr(block(JUMP, 0, 0, 0, np, 0));
}

/*
 * 47ba doret - same. The return statement, which has to convert the
 * returned object to the function's type.
 */
void
doret(void)
{
	uint16_t t;

	if (nextchar() != ';') {
		t = tree();
		WU(cp) = FUNCBLK;
		cp += 2;
		WU(cp) = t;
		cp += 2;
		build(ASSIGN);
		WU(cp - 2) = TR2(WU(cp - 2));
		if (TYP(FUNCBLK) == CHAR)
			WU(cp - 2) = block(ITOC, INT, 0, 0, WU(cp - 2), 0);
		build(RFORCE);
		cp -= 2;
		rcexpr(WU(cp));
	}
	branch(retlab);
}

/*
 * 484e outcode - same. Coded output:
 *   B: beginning of line; an operator
 *   N: a number
 *   S: a symbol (external)
 *   F: a floating constant's text
 *   1: number 1
 *   0: number 0
 * The arguments are the original's words; S and F are addresses in the
 * image.
 */
void
outcode(const char *s, ...)
{
	va_list ap;
	FILE *bufp;
	int16_t n;
	uint16_t np;
	int a;

	c0_checkcrt0();
	bufp = c0_temp1;
	if (strflg)
		bufp = c0_temp2;
	va_start(ap, s);
	for (;;) switch (*s++) {
	case 'B':
		a = va_arg(ap, int);
		putc(a & 0377, bufp);
		putc(0376, bufp);
		continue;

	case 'N':
		a = va_arg(ap, int);
		putc(a & 0377, bufp);
		putc((uint16_t)a >> 8 & 0377, bufp);
		continue;

	case 'F':
		n = 1000;
		np = (uint16_t)va_arg(ap, int);
		goto str;

	case 'S':
		n = NCPS;
		np = (uint16_t)va_arg(ap, int);
		if (B(np))
			putc('_', bufp);
	str:
		while (n-- && B(np)) {
			putc(B(np++) & 0177, bufp);
		}
		putc(0, bufp);
		continue;

	case '1':
		putc(1, bufp);
		putc(0, bufp);
		continue;

	case '0':
		putc(0, bufp);
		putc(0, bufp);
		continue;

	case '\0':
		va_end(ap);
		if (ferror(bufp)) {
			error("Write error on temp");
			c0_exit(1);
		}
		return;

	default:
		error("Botch in outcode");
	}
}
