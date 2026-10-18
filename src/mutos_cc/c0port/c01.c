/*
 * c01.c - building expression trees: v7/cc/c01.c as the original MUTOS
 * 1700 c0 has it (text 140e..2202, tests/realbin/c0map.txt). build()
 * and convert() carry the MUTOS front end's char conversions; error()
 * has a never-called sibling, werror().
 */
#include "c0.h"

#include <stdarg.h>

/*
 * 140e build - delta. Called from tree, this routine takes the top 1,
 * 2, or 3 operands on the expression stack, makes a new node with the
 * operator op, and puts it on the stack. Essentially all the work is in
 * inserting appropriate conversions.
 *
 * The binary differs from v7:
 *   - a relational operator with a constant in -128..127 on one side and
 *     a char on the other makes the constant a char (text 14c2) - both
 *     operands CHAR, so no conversion, and t (the result's type until
 *     RELAT resets it) CHAR;
 *   - QUEST and SEQNC (the comma operator) give their node p2's subsp
 *     and strp (v7: NULL) - a structure-valued ?: keeps its description;
 *   - + - * / % convert a char operand to int by ITOC (109, type INT)
 *     before the conversion table (text 184b);
 *   - the conversion table is 5x5 words (lintyp()'s five classes) and a
 *     code is five bits: leftc = cvn >> 5 (v7: four bits; XX 16);
 *   - "dope & LVALUE || op == CAST" where v7 tests ASSGOP (text 1991):
 *     ++ and -- (LVALUE, not ASSGOP) take the assignments' rule;
 *   - no "if (t==CHAR) t = INT;" - a result can be CHAR.
 */
void
build(int16_t op)
{
	int16_t t1;
	int16_t t2, t;
	uint16_t p1, p2;
	uint16_t p3;
	int16_t dope, leftc, cvn, pcvn;
	uint16_t c;

	t2 = 0;
	p2 = 0;
	/*
	 * a[i] => *(a+i)
	 */
	if (op == LBRACK) {
		build(PLUS);
		op = STAR;
	}
	dope = opdope(op);
	if ((dope & BINARY) != 0) {
		cp -= 2;
		p2 = chkfun(disarray(WU(cp)));
		t2 = TYP(p2);
	}
	cp -= 2;
	p1 = WU(cp);
	/*
	 * sizeof gets turned into a number here.
	 */
	if (op == SIZEOF) {
		c = cblock(length(p1));
		TYP(c) = UNSIGN;
		WU(cp) = c;
		cp += 2;
		return;
	}
	if (op != AMPER) {
		p1 = disarray(p1);
		if (op != CALL)
			p1 = chkfun(p1);
	}
	t1 = TYP(p1);
	pcvn = 0;
	t = INT;
	/*
	 * MUTOS: a char compared with a constant that fits in a char
	 */
	if (dope & RELAT) {
		if (OP(p1) == CON && t2 == CHAR) {
			if (VALUE(p1) >= -128 && VALUE(p1) <= 127) {
				TYP(p1) = CHAR;
				t = t1 = CHAR;
			}
		} else if (OP(p2) == CON && t1 == CHAR) {
			if (VALUE(p2) >= -128 && VALUE(p2) <= 127) {
				TYP(p2) = CHAR;
				t = t2 = CHAR;
			}
		}
	}
	switch (op) {

	case CAST:
		if ((t1 & XTYPE) == FUNC || (t1 & XTYPE) == ARRAY)
			error("Disallowed conversion");
		if (t1 == UNSIGN && t2 == CHAR) {
			t2 = INT;
			p2 = block(AND, INT, 0, 0, p2, cblock(0377));
		}
		break;

	/* end of expression */
	case 0:
		WU(cp) = p1;
		cp += 2;
		return;

	/* no-conversion operators */
	case QUEST:
		if (OP(p2) != COLON)
			error("Illegal conditional");
		else
			if (fold(QUEST, p1, p2))
				return;
		/* FALLTHROUGH */

	case SEQNC:
		WU(cp) = block(op, t2, SUBSP(p2), STRP(p2), p1, p2);
		cp += 2;
		return;

	case COMMA:
	case LOGAND:
	case LOGOR:
		WU(cp) = block(op, t, 0, 0, p1, p2);
		cp += 2;
		return;

	case EXCLA:
		t1 = INT;
		break;

	case CALL:
		if ((t1 & XTYPE) != FUNC)
			error("Call of non-function");
		WU(cp) = block(CALL, decref(t1), SUBSP(p1), STRP(p1), p1, p2);
		cp += 2;
		return;

	case STAR:
		if ((t1 & XTYPE) == FUNC)
			error("Illegal indirection");
		WU(cp) = block(STAR, decref(t1), SUBSP(p1), STRP(p1), p1, 0);
		cp += 2;
		return;

	case AMPER:
		if (OP(p1) == NAME || OP(p1) == STAR) {
			WU(cp) = block(op, incref(t1), SUBSP(p1), STRP(p1), p1, 0);
			cp += 2;
			return;
		}
		error("Illegal lvalue");
		break;

	/*
	 * a.b goes to (&a)->b
	 */
	case DOT:
		if (OP(p1) == CALL && t1 == STRUCT) {
			t1 = incref(t1);
			setype(p1, t1, p1);
		} else {
			WU(cp) = p1;
			cp += 2;
			build(AMPER);
			cp -= 2;
			p1 = WU(cp);
		}
		/* FALLTHROUGH */

	/*
	 * In a->b, a is given the type ptr-to-structure element;
	 * then the offset is added in without conversion;
	 * then * is tacked on to access the member.
	 */
	case ARROW:
		if (OP(p2) != NAME || HCLASS(TR1(p2)) != MOS) {
			error("Illegal structure ref");
			WU(cp) = p1;
			cp += 2;
			return;
		}
		if (t2 == INT && HFLAG(TR1(p2)) & FFIELD)
			t2 = UNSIGN;
		t = incref(t2);
		chkw(p1, -1);
		setype(p1, t, p2);
		c = cblock(HOFFSET(TR1(p2)));
		WU(cp) = block(PLUS, t, SUBSP(p2), STRP(p2), p1, c);
		cp += 2;
		build(STAR);
		if (HFLAG(TR1(p2)) & FFIELD) {
			cp -= 2;
			c = WU(cp);
			WU(cp) = block(FSEL, UNSIGN, 0, 0, c, HSTRP(TR1(p2)));
			cp += 2;
		}
		return;

	/*
	 * MUTOS: a char operand of + - * / % is made an int first
	 */
	case PLUS:
	case MINUS:
	case TIMES:
	case DIVIDE:
	case MOD:
		if (t1 == CHAR) {
			p1 = block(ITOC, INT, 0, 0, p1, 0);
			t1 = INT;
		}
		if (t2 == CHAR) {
			p2 = block(ITOC, INT, 0, 0, p2, 0);
			t2 = INT;
		}
		break;
	}
	if ((dope & LVALUE) != 0)
		chklval(p1);
	if ((dope & LWORD) != 0)
		chkw(p1, LONG);
	if ((dope & RWORD) != 0)
		chkw(p2, LONG);
	if ((dope & BINARY) == 0) {
		if (op == ITOF)
			t1 = DOUBLE;
		else if (op == FTOI)
			t1 = INT;
		if (!fold(op, p1, 0)) {
			WU(cp) = block(op, t1, SUBSP(p1), STRP(p1), p1, 0);
			cp += 2;
		}
		return;
	}
	cvn = 0;
	if (t1 == STRUCT || t2 == STRUCT) {
		if (t1 != t2 || STRP(p1) != STRP(p2))
			error("Incompatible structures");
		cvn = 0;
	} else
		cvn = cvtab(lintyp(t1), lintyp(t2));
	leftc = (int16_t)((cvn >> 5) & 037);
	cvn &= 037;
	t = leftc ? t2 : t1;
	if ((t == INT || t == CHAR) && (t1 == UNSIGN || t2 == UNSIGN))
		t = UNSIGN;
	if (dope & LVALUE || op == CAST) {
		t = t1;
		if (op == ASSIGN || op == CAST) {
			if (cvn == ITP || cvn == PTI)
				cvn = leftc = 0;
			else if (cvn == LTP) {
				if (leftc == 0)
					cvn = LTI;
				else {
					cvn = ITL;
					leftc = 0;
				}
			}
		}
		if (leftc)
			cvn = leftc;
		leftc = 0;
	} else if (op == COLON || op == MAX || op == MIN) {
		if (t1 >= PTR && t1 == t2)
			cvn = 0;
		if (op != COLON && (t1 >= PTR || t2 >= PTR))
			op += MAXP - MAX;
	} else if (dope & RELAT) {
		if (op >= LESSEQ && (t1 >= PTR || t2 >= PTR || ((t1 == UNSIGN
		    || t2 == UNSIGN) && (t == INT || t == CHAR || t == UNSIGN))))
			op += LESSEQP - LESSEQ;
		if (cvn == ITP || cvn == PTI)
			cvn = 0;
	}
	if (cvn == PTI) {
		cvn = 0;
		if (op == MINUS) {
			t = INT;
			pcvn++;
		} else {
			if (t1 != t2 || t1 != (PTR + CHAR))
				cvn = XX;
		}
	}
	if (cvn) {
		t1 = plength(p1);
		t2 = plength(p2);
		if (cvn == XX || (cvn == PTI && t1 != t2))
			error("Illegal conversion");
		else if (leftc)
			p1 = convert(p1, t, cvn, t2);
		else
			p2 = convert(p2, t, cvn, t1);
	}
	if (dope & RELAT)
		t = INT;
	if (t == FLOAT)
		t = DOUBLE;
	if (op == CAST) {
		if (t != DOUBLE && (t != INT || TYP(p2) != CHAR)) {
			TYP(p2) = t;
			SUBSP(p2) = SUBSP(p1);
			STRP(p2) = STRP(p1);
		}
		if (t == INT && TYP(p1) == CHAR)
			p2 = block(ITOC, INT, 0, 0, p2, 0);
		WU(cp) = p2;
		cp += 2;
		return;
	}
	if (fold(op, p1, p2) == 0) {
		p3 = leftc ? p2 : p1;
		WU(cp) = block(op, t, SUBSP(p3), STRP(p3), p1, p2);
		cp += 2;
	}
	if (pcvn && t1 != (PTR + CHAR)) {
		cp -= 2;
		p1 = WU(cp);
		WU(cp) = convert(p1, 0, PTI, plength(TR1(p1)));
		cp += 2;
	}
}

/*
 * 1bfe convert - delta. Generate the appropriate conversion operator.
 * cvntab[] has two MUTOS pseudo operators that are two conversions:
 * 123, char to floating (ITOC of type INT, then ITOF), and 122,
 * floating to char (FTOI of type INT, then ITOC).
 */
uint16_t
convert(uint16_t p, int16_t t, int16_t cvn, int16_t len)
{
	int16_t op;

	op = cvntab(cvn);
	if (op == CTOF_P) {
		p = block(ITOC, INT, 0, 0, p, 0);
		op = ITOF;
	}
	if (op == FTOC_P) {
		p = block(FTOI, INT, 0, 0, p, 0);
		op = ITOC;
	}
	if (opdope(op) & BINARY) {
		if (len == 0)
			error("Illegal conversion");
		return block(op, t, 0, 0, p, cblock(len));
	}
	return block(op, t, 0, 0, p, 0);
}

/*
 * 1c94 setype - same. Traverse an expression tree, adjust things so
 * the types of things in it are consistent with the view that its top
 * node has type at. Used with structure references.
 */
void
setype(uint16_t ap, int16_t at, uint16_t anewp)
{
	uint16_t p, newp;
	int16_t t;

	p = ap;
	t = at;
	newp = anewp;
	for (;; p = TR1(p)) {
		SUBSP(p) = SUBSP(newp);
		STRP(p) = STRP(newp);
		TYP(p) = t;
		if (OP(p) == AMPER)
			t = decref(t);
		else if (OP(p) == STAR)
			t = incref(t);
		else if (OP(p) != PLUS)
			break;
	}
}

/*
 * 1cea chkfun - delta: a null tree (an empty argument list) is
 * returned as is. A mention of a function name is turned into a pointer
 * to that function.
 */
uint16_t
chkfun(uint16_t ap)
{
	uint16_t p;
	int16_t t;

	p = ap;
	if (p != 0 && ((t = TYP(p)) & XTYPE) == FUNC && OP(p) != ETYPE)
		return block(AMPER, incref(t), SUBSP(p), STRP(p), p, 0);
	return p;
}

/*
 * 1d2a disarray - delta: a null tree is returned as is. A mention of an
 * array is turned into a pointer to the base of the array.
 */
uint16_t
disarray(uint16_t ap)
{
	int16_t t;
	uint16_t p;

	p = ap;
	/* check array & not MOS and not typer */
	if (p == 0 || ((t = TYP(p)) & XTYPE) != ARRAY
	 || (OP(p) == NAME && HCLASS(TR1(p)) == MOS)
	 || OP(p) == ETYPE)
		return p;
	SUBSP(p) += 2;
	WU(cp) = p;
	cp += 2;
	setype(p, decref(t), p);
	build(AMPER);
	cp -= 2;
	return WU(cp);
}

/*
 * 1d94 chkw - same. Make sure that p is a ptr to a node with type int
 * or char or 'okt'.
 */
void
chkw(uint16_t p, int16_t okt)
{
	int16_t t;

	if ((t = TYP(p)) != INT && t < PTR && t != CHAR && t != UNSIGN
	 && t != okt)
		error("Illegal type of operand");
}

/*
 * 1dc6 lintyp - delta: five classes for the 5x5 cvtab[] - char 0, int
 * and unsigned 1, float and double 2, long 3, anything else 4.
 */
int16_t
lintyp(int16_t t)
{
	switch (t) {

	case CHAR:
		return 0;

	case INT:
	case UNSIGN:
		return 1;

	case FLOAT:
	case DOUBLE:
		return 2;

	case LONG:
		return 3;

	default:
		return 4;
	}
}

/*
 * The six words the original pushes after the format: the arguments a
 * call gives (the rest of the six are never read by its formats).
 */
static void
vmessage(const char *prefix, const char *s, va_list ap)
{
	uint16_t args[6];
	uint16_t fl[1];
	uint16_t ln[1];
	int i;

	for (i = 0; i < 6; i++)
		args[i] = 0;
	{
		/* as many words as the format has conversions (at most six) */
		const char *q;
		int n = 0;

		for (q = s; *q; q++)
			if (*q == '%' && n < 6)
				args[n++] = (uint16_t)va_arg(ap, int);
	}
	if (B(FILENAME)) {
		fl[0] = FILENAME;
		c0_vfprintf(stderr, "%s:", fl);
	}
	ln[0] = (uint16_t)line;
	c0_vfprintf(stderr, prefix, ln);
	c0_vfprintf(stderr, s, args);
	c0_vfprintf(stderr, "\n", args);
}

/*
 * 1e08 error - same. Report an error.
 */
void
error(const char *s, ...)
{
	va_list ap;

	nerror++;
	va_start(ap, s);
	vmessage("%d: ", s, ap);
	va_end(ap);
}

/*
 * 1e6c werror - new, and never called: error()'s text with "WARNING, "
 * after the line number, without counting an error.
 */
void
werror(const char *s, ...)
{
	va_list ap;

	va_start(ap, s);
	vmessage("%d: WARNING, ", s, ap);
	va_end(ap);
}

/*
 * 1ecc block - same. Generate a node in an expression tree, setting
 * the operator, type, dimen/struct table ptrs, and the operands.
 */
uint16_t
block(int16_t op, int16_t t, uint16_t subs, uint16_t str, uint16_t p1,
    uint16_t p2)
{
	uint16_t p;

	p = gblock(SZ_TNODE);
	OP(p) = op;
	TYP(p) = t;
	SUBSP(p) = subs;
	STRP(p) = str;
	TR1(p) = p1;
	if (opdope(op) & BINARY)
		TR2(p) = p2;
	else
		TR2(p) = 0;
	return p;
}

/*
 * 1f1a nblock - same.
 */
uint16_t
nblock(uint16_t ads)
{
	uint16_t ds;

	ds = ads;
	return block(NAME, HTYP(ds), HSUBSP(ds), HSTRP(ds), ds, 0);
}

/*
 * 1f3a cblock - same. Generate a block for a constant.
 */
uint16_t
cblock(int16_t v)
{
	uint16_t p;

	p = gblock(SZ_CNODE);
	OP(p) = CON;
	TYP(p) = INT;
	SUBSP(p) = 0;
	STRP(p) = 0;
	VALUE(p) = v;
	return p;
}

/*
 * 1f6a fblock - same. A block for a float constant.
 */
uint16_t
fblock(int16_t t, uint16_t string)
{
	uint16_t p;

	p = gblock(SZ_FNODE);
	OP(p) = FCON;
	TYP(p) = t;
	SUBSP(p) = 0;
	STRP(p) = 0;
	CSTR(p) = string;
	return p;
}

/*
 * 1f9a gblock - same. Assign a block for use in the expression tree.
 */
uint16_t
gblock(int16_t n)
{
	uint16_t p;

	c0_checkcrt0();
	p = curbase;
	if ((curbase += n) >= coremax) {
		if (c0_sbrk(1024) == -1) {
			error("Out of space");
			c0_exit(1);
		}
		coremax += 1024;
	}
	return p;
}

/*
 * 1fe2 chklval - same. Check that a tree can be used as an lvalue.
 */
void
chklval(uint16_t ap)
{
	uint16_t p;

	p = ap;
	if (OP(p) == FSEL)
		p = TR1(p);
	if (OP(p) != NAME && OP(p) != STAR)
		error("Lvalue required");
}

/*
 * The 8086's shifts (fold(): "mov cx,v2 / shl si,cl", text 2121..213b):
 * by the count's low byte, CL, all of it - the 8086 and the V30 do not
 * mask the count (the 80186 does, to five bits), so from 16 on a shl or
 * shr gives 0 and a sar the sign (as c1port/c1.h's shl16() and sar16()).
 */
static int16_t
shl16(int16_t v1, int16_t v2)
{
	int16_t n = (int16_t)(v2 & 0377);

	return n >= 16 ? 0 : (int16_t)(uint16_t)((uint32_t)(uint16_t)v1 << n);
}

static int16_t
shr16(int16_t v1, int16_t v2)
{
	int16_t n = (int16_t)(v2 & 0377);

	return n >= 16 ? 0 : (int16_t)((uint16_t)v1 >> n);
}

static int16_t
sar16(int16_t v1, int16_t v2)
{
	int16_t n = (int16_t)(v2 & 0377);

	return n >= 16 ? (int16_t)(v1 < 0 ? -1 : 0) : (int16_t)(v1 >> n);
}

/*
 * 200a fold - same (a jump table on op - NEG). Reduce some forms of
 * `constant op constant' to a constant. More of this is done in the next
 * pass but this is used to allow constant expressions to be used in
 * switches and array bounds.
 */
int16_t
fold(int16_t op, uint16_t ap1, uint16_t ap2)
{
	uint16_t p1;
	int16_t v1, v2;
	int16_t unsignf;

	v2 = 0;
	p1 = ap1;
	if (OP(p1) != CON)
		return 0;
	unsignf = TYP(p1) == UNSIGN;
	if (op == QUEST) {
		if (OP(TR1(ap2)) == CON && OP(TR2(ap2)) == CON) {
			VALUE(p1) = VALUE(p1) ? VALUE(TR1(ap2)) : VALUE(TR2(ap2));
			WU(cp) = p1;
			cp += 2;
			return 1;
		}
		return 0;
	}
	if (ap2) {
		if (OP(ap2) != CON)
			return 0;
		v2 = VALUE(ap2);
		unsignf |= TYP(ap2) == UNSIGN;
	}
	v1 = VALUE(p1);
	switch (op) {

	case PLUS:
		v1 = (int16_t)(v1 + v2);
		break;

	case MINUS:
		v1 = (int16_t)(v1 - v2);
		break;

	case TIMES:
		v1 = (int16_t)(v1 * v2);
		break;

	case DIVIDE:
		if (v2 == 0)
			goto divchk;
		if (unsignf) {
			v1 = (int16_t)((uint16_t)v1 / (uint16_t)v2);
			break;
		}
		if (v1 == -32768 && (v2 == -1 || v2 == 1))
			c0trap(v2 == 1 ? "-32768 / 1 folded: the 8086's idiv faults "
			    "(a quotient of -32768)" : "-32768 / -1 folded: the "
			    "8086's idiv faults");
		v1 = (int16_t)(v1 / v2);
		break;

	case MOD:
		if (v2 == 0)
			goto divchk;
		if (unsignf) {
			v1 = (int16_t)((uint16_t)v1 % (uint16_t)v2);
			break;
		}
		if (v1 == -32768 && (v2 == -1 || v2 == 1))
			c0trap(v2 == 1 ? "-32768 % 1 folded: the 8086's idiv faults "
			    "(a quotient of -32768)" : "-32768 % -1 folded: the "
			    "8086's idiv faults");
		v1 = (int16_t)(v1 % v2);
		break;

	case AND:
		v1 &= v2;
		break;

	case OR:
		v1 |= v2;
		break;

	case EXOR:
		v1 ^= v2;
		break;

	case NEG:
		v1 = (int16_t)-v1;
		break;

	case COMPL:
		v1 = (int16_t)~v1;
		break;

	case LSHIFT:
		v1 = shl16(v1, v2);
		break;

	case RSHIFT:
		if (unsignf) {
			v1 = shr16(v1, v2);
			break;
		}
		v1 = sar16(v1, v2);
		break;

	case EQUAL:
		v1 = v1 == v2;
		break;

	case NEQUAL:
		v1 = v1 != v2;
		break;

	case LESS:
		v1 = v1 < v2;
		break;

	case GREAT:
		v1 = v1 > v2;
		break;

	case LESSEQ:
		v1 = v1 <= v2;
		break;

	case GREATEQ:
		v1 = v1 >= v2;
		break;

	divchk:
		error("Divide check");
		/* FALLTHROUGH */
	default:
		return 0;
	}
	VALUE(p1) = v1;
	WU(cp) = p1;
	cp += 2;
	return 1;
}

/*
 * 21ce conexp - same. Compile an expression expected to have constant
 * value, for example an array bound or a case value. After a syntax
 * error tree() returns a null tree and t->value is the word at address
 * 8 - crt0's copy of the code segment.
 */
int16_t
conexp(void)
{
	uint16_t t;

	initflg++;
	if ((t = tree()) != 0)
		if (OP(t) != CON)
			error("Constant required");
	initflg--;
	curbase = funcbase;
	return VALUE(t);
}
