/*
 * c12.c - the MUTOS 1700 code generator, part 3: the expression
 * optimizer and the tree allocator (v7/cc/c12.c, ported from the
 * original binary - see c1.h and docs/C1_PORT_PLAN.md).
 *
 * Function order and the text offsets in the comments follow the
 * original (tests/realbin/c1map.txt).
 */
#include "c1.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/*
 * 3e3c optim - delta. v7's structure; what the original does differently:
 *   - an auto name becomes OFFS on BP (regno 6; v7: r5);
 *   - an ITOL is dropped from the count of a shift (<<, >>, the unsigned
 *     >> and their assignments) before anything else;
 *   - a floating constant's leaf goes through fconchk() (the MUTOS
 *     format's "is it a float" test), every other leaf is returned;
 *   - no "char becomes int" for a binary tree; no PDP-11 =& to =&~ and
 *     & to &~ rewriting (ASAND and AND stay); an int->ptr conversion op
 *     29 (a char index) becomes * with the char operand converted (ITOC);
 *   - the unsigned / % /= %= >> >>= become UDIV UMOD ASUDIV ASUMOD ULSH
 *     ASULSH; "x - c" becomes "x + -c" by negating the value word only
 *     (no PDP-11 floating sign bit);
 *   - an assignment-like op (=, ++, --, =+, =-, =&, =|, =^) to a char of
 *     a converted (ITOC) value drops the conversion unless the value is
 *     a register variable or an assignment to one, and warns
 *     "Unreasonable Conversion in =" for an address;
 *   - a commutative tree is returned as acommute() leaves it;
 *   - after the operands: ITOL of a nonnegative constant is dropped from
 *     the right of + - ++ -- =+ =- and the relations; a shift by 2
 *     becomes two shifts by 1 (a copy of the node, with cone); a call
 *     through a register (OFFS name) or an indirection becomes CALL1;
 *     unsigned operands of the divisions by a power of 2 go to pow2().
 */
struct tnode *
optim(struct tnode *atree)
{
	int op, dope;
	int d1, d2, op1;
	struct tnode *t, *tree;

	if ((tree = atree) == 0)
		return 0;
	if ((op = tree->op) == 0)
		return tree;
	if (op == NAME && tree->class == AUTO) {
		tree->class = OFFS;
		tree->regno = BP;
		tree->offset = tree->nloc;
	}
	if ((op == LSHIFT || op == RSHIFT || op == ULSH || op == ASULSH
	  || op == ASRSH || op == ASLSH) && tree->tr2->op == ITOL)
		tree->tr2 = tree->tr2->tr1;
	dope = opdope[op];
	if ((dope & LEAF) != 0) {
		if (op == FCON || op == SFCON)
			return fconchk(tree);
		return tree;
	}
	if ((dope & BINARY) == 0)
		return unoptim(tree);
	/* is known to be binary */
	switch (op) {

	case LTOP:
		tree->op = ITOP;
		tree->tr1 = unoptim(tnode(LTOI, INT, tree->tr1, 0));
		/* fall through */
	case ITOP:
		tree->op = TIMES;
		break;

	case 29:	/* int->ptr of a char (opntab "op 29") */
		tree->op = TIMES;
		tree->tr1 = unoptim(tnode(ITOC, INT, tree->tr1, 0));
		break;

	case MINUS:
		if ((t = isconstant(tree->tr2)) != 0
		 && (t->type != UNSIGN || tree->type != LONG)) {
			tree->op = PLUS;
			t->value = (int16_t)-t->value;
		}
		break;

	case DIVIDE:
		if (tree->type == UNSIGN)
			tree->op = UDIV;
		break;

	case MOD:
		if (tree->type == UNSIGN)
			tree->op = UMOD;
		break;

	case RSHIFT:
		if (tree->type == UNSIGN)
			tree->op = ULSH;
		break;

	case ASDIV:
		if (tree->type == UNSIGN)
			tree->op = ASUDIV;
		break;

	case ASMOD:
		if (tree->type == UNSIGN)
			tree->op = ASUMOD;
		break;

	case ASRSH:
		if (tree->type == UNSIGN)
			tree->op = ASULSH;
		break;

	case INCBEF:
	case DECBEF:
	case INCAFT:
	case DECAFT:
	case ASPLUS:
	case ASMINUS:
	case ASAND:
	case ASOR:
	case ASXOR:
	case ASSIGN:
		if (tree->tr1->type != CHAR || tree->tr2->op != ITOC)
			break;
		t = tree->tr2->tr1;
		op1 = t->op;
		if (op1 == NAME && t->class == REG)
			break;
		if ((op1 == ASSIGN || op1 == INCBEF || op1 == INCAFT
		  || op1 == DECBEF || op1 == DECAFT)
		 && t->tr1->op == NAME && t->tr1->class == REG)
			break;
		if (t->op == AMPER)
			werror("Unreasonable Conversion in =");
		else
			tree->tr2 = tree->tr2->tr1;
		break;
	}
	op = tree->op;
	dope = opdope[op];
	if (dope & LVALUE && tree->tr1->op == FSEL)
		return lvfield(tree);
	if ((dope & COMMUTE) != 0) {
		d1 = tree->type;
		tree = acommute(tree);
		if (tree->op == op)
			tree->type = (int16_t)d1;
		return tree;
	}
	tree->tr1 = optim(tree->tr1);
	tree->tr2 = optim(tree->tr2);
	if (tree->type == LONG) {
		t = lconst(tree->op, tree->tr1, tree->tr2);
		if (t)
			return t;
	}
	if ((dope & RELAT) != 0) {
		d2 = degree(tree->tr2);
		d1 = degree(tree->tr1);
		if (d1 < d2
		 || (d1 == d2 && tree->tr1->op == NAME && tree->tr2->op != NAME)) {
			t = tree->tr1;
			tree->tr1 = tree->tr2;
			tree->tr2 = t;
			tree->op = maprel[op - EQUAL];
		}
		if (tree->tr1->type == CHAR && tree->tr2->op == CON
		 && (dcalc(tree->tr1, 0) <= 12 || tree->tr1->op == STAR)
		 && tree->tr2->value <= 127 && tree->tr2->value >= 0)
			tree->tr2->type = CHAR;
	}
	d1 = max(degree(tree->tr1), islong(tree->type));
	d2 = max(degree(tree->tr2), 0);
	switch (op) {

	case COLON:
	case QUEST:
		tree->degree = (int16_t)max(d1, d2);
		return tree;

	/*
	 * In assignment to fields, treat all-zero and all-1 specially.
	 */
	case FSELA:
		if (tree->tr2->op == CON && tree->tr2->value == 0) {
			tree->op = ASAND;
			tree->tr2->value = (int16_t)~tree->mask;
			return optim(tree);
		}
		if (tree->tr2->op == CON && tree->mask == tree->tr2->value) {
			tree->op = ASOR;
			return optim(tree);
		}
		tree->degree = 10;
		return tree;

	case LTIMES:
	case LDIV:
	case LMOD:
	case LASTIMES:
	case LASDIV:
	case LASMOD:
		tree->degree = 10;
		return tree;

	case CALL:
		if (tree->tr1->op == NAME && tree->tr1->class == OFFS) {
			tree->op = CALL1;
			tree->tr1->class = REG;
		} else if (tree->tr1->op == STAR) {
			tree->op = CALL1;
			tree->tr1 = tree->tr1->tr1;
		}
		tree->degree = 10;
		return tree;

	case PTOI:
	case DIVIDE:
	case ASTIMES:
	case ASDIV:
	case UDIV:
	case ASUDIV:
		if (tree->tr2->op == CON && tree->tr2->value == 1)
			return tree->tr1;
		/* fall through */
	case MOD:
	case ASMOD:
	case UMOD:
	case ASUMOD:
		if (tree->tr1->type == UNSIGN && ispow2(tree))
			return pow2(tree);
		if ((op == MOD || op == ASMOD) && tree->type == DOUBLE) {
			error("Floating %% not defined");
			tree->type = INT;
		}
		/* fall through */
	case ULSH:
	case ASULSH:
		d1 += 2;
		d2 += 2;
		if (tree->type == LONG)
			return hardlongs(tree);
		goto constant;

	case INCBEF:
	case DECBEF:
	case INCAFT:
	case DECAFT:
	case PLUS:
	case MINUS:
	case EQUAL:
	case NEQUAL:
	case LESSEQ:
	case LESS:
	case GREATEQ:
	case GREAT:
	case LESSEQP:
	case LESSP:
	case GREATQP:
	case GREATP:
	case ASPLUS:
	case ASMINUS:
		if (tree->tr2->op == ITOL && (t = tree->tr2->tr1)->op == CON
		 && t->value >= 0)
			tree->tr2 = tree->tr2->tr1;
		goto constant;

	case RSHIFT:
	case LSHIFT:
		if (tree->tr2->op == CON && tree->tr2->value == 2) {
			t = getblk(sizeof(*t));
			tree->tr2 = &cone;
			*t = *tree;
			tree->tr1 = t;
		}
		/* fall through */
	case ASRSH:
	case ASLSH:
		if (tree->tr2->op == CON && tree->tr2->value == 0)
			return tree->tr1;
		if (tree->type == LONG) {
			d1++;
			d2++;
		}
		goto constant;

	constant:
		if (tree->tr1->op == CON && tree->tr2->op == CON) {
			const_(op, &tree->tr1->value, tree->tr2->value);
			return tree->tr1;
		}
		break;
	}
	if (dope & RELAT) {
		if (tree->tr1->type == LONG)	/* long relations are a mess */
			d1 = 10;
		if (opdope[tree->tr1->op] & RELAT && tree->tr2->op == CON
		 && tree->tr2->value == 0) {
			tree = tree->tr1;
			switch (op) {
			case GREATEQ:
				return &cone;
			case LESS:
				return &czero;
			case LESSEQ:
			case EQUAL:
				tree->op = notrel[tree->op - EQUAL];
			}
			return tree;
		}
	}
	tree->degree = (int16_t)(d1 == d2 ? d1 + islong(tree->type) : max(d1, d2));
	return tree;
}

/*
 * 4746 unoptim - delta. v7's structure; what the original does
 * differently:
 *   - &x of an auto (OFFS) name is left as it is (v7: register + offset);
 *   - *p: through a register variable usable as a base (isregptr()) it
 *     becomes that register as OFFS; "*(r + c)" and "*(r + &x)" as in v7
 *     but only for such a register; any other "*(x + c)", and
 *     "*(x + &name)" for an external or static name or with x a register
 *     variable, becomes op 89 (STARC) - the PLUS node itself, typed;
 *   - ITOC: of a constant sign-extends its low byte, of an ITOC is that
 *     ITOC (v7: a name becomes char);
 *   - ITOF of a constant and LTOF of a long constant are converted with
 *     libc.a's itod()/ltof() (c1_itod(), c1_ltof()) and go through
 *     fconchk(); the negation of a floating constant through fneg;
 *   - a long LCON negation and the conversion of a negative int to long
 *     with 16-bit arithmetic first, as the original computes them.
 */
struct tnode *
unoptim(struct tnode *atree)
{
	struct tnode *subtre, *tree, *p;

	if ((tree = atree) == 0)
		return 0;
again:
	if (tree->op == AMPER && tree->tr1->op == STAR) {
		subtre = tree->tr1->tr1;
		subtre->type = tree->type;
		return optim(subtre);
	}
	subtre = tree->tr1 = optim(tree->tr1);
	switch (tree->op) {

	case ITOL:
		if (subtre->op == CON && subtre->type == INT && subtre->value < 0) {
			subtre = getblk(sizeof(*subtre));
			subtre->op = LCON;
			subtre->type = LONG;
			setlvalue(subtre, tree->tr1->value);
			return subtre;
		}
		break;

	case FTOI:
		if (tree->type == UNSIGN) {
			tree->op = FTOL;
			tree->type = LONG;
			tree = tnode(LTOI, UNSIGN, tree, 0);
		}
		break;

	case LTOF:
		if (subtre->op == LCON) {
			tree = getblk(sizeof(*tree));
			tree->op = FCON;
			tree->type = DOUBLE;
			tree->value = isn++;
			c1_ltof(lvalue(subtre), tree->fvalue);
			return optim(tree);
		}
		break;

	case ITOF:
		if (tree->tr1->type == UNSIGN) {
			tree->tr1 = tnode(ITOL, LONG, tree->tr1, 0);
			tree->op = LTOF;
			tree = optim(tree);
		}
		if (subtre->op != CON)
			break;
		tree = getblk(sizeof(*tree));
		c1_itod(subtre->value, tree->fvalue);
		tree->value = isn++;
		return fconchk(tree);

	case ITOC:
		p = tree->tr1;
		if (p->op == CON) {
			p->value = (int16_t)(int8_t)(uint8_t)p->value;
			return p;
		}
		if (p->op == ITOC)
			return p;
		break;

	case LTOI:
		p = tree->tr1;
		switch (p->op) {

		case LCON:
			p->op = CON;
			p->type = tree->type;
			p->value = p->lvlo;
			return p;

		case NAME:
			p->offset += 2;
			p->type = tree->type;
			return p;

		case STAR:
			p->type = tree->type;
			p->tr1->type = (int16_t)(tree->type + PTR);
			p->tr1 = tnode(PLUS, tree->type, p->tr1, tconst(2, INT));
			return optim(p);

		case ITOL:
			return p->tr1;

		case PLUS:
		case MINUS:
		case AND:
		case ANDN:
		case OR:
		case EXOR:
			p->tr2 = tnode(LTOI, tree->type, p->tr2, 0);
			/* fall through */
		case NEG:
		case COMPL:
			p->tr1 = tnode(LTOI, tree->type, p->tr1, 0);
			p->type = tree->type;
			return optim(p);
		}
		break;

	case FSEL:
		tree->op = AND;
		tree->tr1 = tree->tr2->tr1;
		tree->tr2->tr1 = subtre;
		tree->tr2->op = RSHIFT;
		tree->tr1->value = (int16_t)(shl16(1, tree->tr1->value) - 1);
		return optim(tree);

	case FSELR:
		tree->op = LSHIFT;
		tree->type = UNSIGN;
		tree->tr1 = tree->tr2;
		tree->tr1->op = AND;
		tree->tr2 = tree->tr2->tr2;
		tree->tr1->tr2 = subtre;
		tree->tr1->tr1->value = (int16_t)(shl16(1, tree->tr1->tr1->value) - 1);
		return optim(tree);

	case AMPER:
		if (subtre->op == STAR)
			return subtre->tr1;
		if (subtre->op == NAME && subtre->class == OFFS)
			return tree;
		break;

	case STAR:
		if (subtre->op == AMPER) {
			subtre->tr1->type = tree->type;
			return subtre->tr1;
		}
		if (tree->type == STRUCT)
			break;
		if (isregptr(subtre)) {
			subtre->type = tree->type;
			subtre->class = OFFS;
			subtre->regno = (signed char)subtre->nloc;
			return subtre;
		}
		p = subtre->tr1;
		if (subtre->op == PLUS && isregptr(p)) {
			if (subtre->tr2->op == CON) {
				p->offset += subtre->tr2->value;
				p->class = OFFS;
				p->type = tree->type;
				p->regno = (signed char)p->nloc;
				return p;
			}
			if (subtre->tr2->op == AMPER) {
				p = subtre->tr2->tr1;
				if (p->class == EXTERN || p->class == STATIC) {
					p->class += XOFFS - EXTERN;
					p->regno = (signed char)subtre->tr1->nloc;
					p->type = tree->type;
					return p;
				}
			}
		}
		p = subtre->tr2;
		if (subtre->op == PLUS
		 && (p->op == CON
		  || (p->op == AMPER && p->tr1->op == NAME
		   && (p->tr1->class == EXTERN || p->tr1->class == STATIC
		    || (subtre->tr1->op == NAME && subtre->tr1->class == REG))))) {
			subtre->op = STARC;
			subtre->type = tree->type;
			return subtre;
		}
		break;

	case EXCLA:
		if ((opdope[subtre->op] & RELAT) == 0)
			break;
		tree = subtre;
		tree->op = notrel[tree->op - EQUAL];
		break;

	case COMPL:
		if (tree->type == CHAR)
			tree->type = INT;
		if (tree->op == subtre->op)
			return subtre->tr1;
		if (subtre->op == CON) {
			subtre->value = (int16_t)~subtre->value;
			return subtre;
		}
		if (subtre->op == LCON) {
			setlvalue(subtre, ~lvalue(subtre));
			return subtre;
		}
		if (subtre->op == ITOL) {
			if (subtre->tr1->op == CON) {
				tree = getblk(sizeof(*tree));
				tree->op = LCON;
				tree->type = LONG;
				if (subtre->tr1->type == UNSIGN)
					setlvalue(tree, ~(int32_t)(uint16_t)subtre->tr1->value);
				else
					setlvalue(tree, (int16_t)~subtre->tr1->value);
				return tree;
			}
			if (subtre->tr1->type == UNSIGN)
				break;
			subtre->op = tree->op;
			subtre->type = subtre->tr1->type;
			tree->op = ITOL;
			tree->type = LONG;
			goto again;
		}
		/* fall through */
	case NEG:
		if (tree->type == CHAR)
			tree->type = INT;
		if (tree->op == subtre->op)
			return subtre->tr1;
		if (subtre->op == CON) {
			subtre->value = (int16_t)-subtre->value;
			return subtre;
		}
		if (subtre->op == LCON) {
			setlvalue(subtre, (int32_t)(0 - (uint32_t)lvalue(subtre)));
			return subtre;
		}
		if (subtre->op == ITOL && subtre->tr1->op == CON) {
			tree = getblk(sizeof(*tree));
			tree->op = LCON;
			tree->type = LONG;
			if (subtre->tr1->type == UNSIGN)
				setlvalue(tree, (int32_t)(0 - (uint32_t)(uint16_t)subtre->tr1->value));
			else
				setlvalue(tree, (int16_t)-subtre->tr1->value);
			return tree;
		}
		if (subtre->op == FCON || subtre->op == SFCON) {
			c1_fneg(subtre->fvalue);
			return subtre;
		}
		break;
	}
	if ((opdope[tree->op] & LEAF) == 0)
		tree->degree = (int16_t)max(islong(tree->type), degree(subtre));
	return tree;
}

/*
 * 4dd6 lvfield - delta: =& (ASAND) where v7 has =&~ (ASANDN).
 */
struct tnode *
lvfield(struct tnode *t)
{
	struct tnode *t1, *t2;

	switch (t->op) {

	case ASSIGN:
		t2 = getblk(sizeof(*t2));
		t2->op = FSELA;
		t2->type = UNSIGN;
		t1 = t->tr1->tr2;
		t2->mask = (int16_t)shl16((uint16_t)(shl16(1, t1->tr1->value) - 1),
		    t1->tr2->value);
		t2->tr1 = t->tr1;
		t2->tr2 = t->tr2;
		t = t2;
		/* fall through */
	case ASAND:
	case ASPLUS:
	case ASMINUS:
	case ASOR:
	case ASXOR:
	case INCBEF:
	case INCAFT:
	case DECBEF:
	case DECAFT:
		t1 = t->tr1;
		t1->op = FSELR;
		t->tr1 = t1->tr1;
		t1->tr1 = t->tr2;
		t->tr2 = t1;
		t1 = t1->tr2;
		t2 = tconst(t1->tr2->value, INT);
		t1 = tnode(COMMA, INT, tconst(t1->tr1->value, INT), t2);
		return optim(tnode(FSELT, UNSIGN, t, t1));

	}
	error("Unimplemented field operator");
	return t;
}

#define	LSTSIZ	20
struct acl {
	int	nextl;
	int	nextn;
	struct	tnode *nlist[LSTSIZ];
	struct	tnode *llist[LSTSIZ+1];
};

static void distrib(struct acl *);
static void squash(struct tnode **, struct tnode **);
static void insert(int, struct tnode *, struct acl *);

/*
 * 4f0a acommute - delta: no PDP-11 rule for ^; "+0" (or |0) is dropped
 * only from a list of two or more, and a single remaining operand is
 * returned as it is (v7: a char gets a LOAD, the type is the tree's);
 * & with -1 is dropped like * by 1; & with a constant of at most 8 bits
 * turns its ITOC operands into op 121 (CTOI).
 */
struct tnode *
acommute(struct tnode *atree)
{
	struct acl acl;
	int d, i, op, flt, d1;
	struct tnode *t1, **t2, *tree;
	struct tnode *t;

	acl.nextl = 0;
	acl.nextn = 0;
	tree = atree;
	op = tree->op;
	flt = isfloat(tree);
	insert(op, tree, &acl);
	acl.nextl--;
	t2 = &acl.llist[acl.nextl];
	if (!flt) {
		/* put constants together */
		for (i = acl.nextl; i > 0; i--) {
			if (t2[0]->op == CON && t2[-1]->op == CON) {
				acl.nextl--;
				t2--;
				const_(op, &t2[0]->value, t2[1]->value);
			} else if ((t = lconst(op, t2[-1], t2[0])) != 0) {
				acl.nextl--;
				t2--;
				t2[0] = t;
			}
		}
	}
	if (op == PLUS || op == OR) {
		/* toss out "+0" */
		if (acl.nextl > 0
		 && (((t1 = isconstant(*t2)) != 0 && t1->value == 0)
		  || ((*t2)->op == LCON && lvalue(*t2) == 0))) {
			acl.nextl--;
			t2--;
		}
		if (acl.nextl <= 0)
			return *t2;
		/* subsume constant in "&x+c" */
		if (op == PLUS && t2[0]->op == CON && t2[-1]->op == AMPER) {
			t2--;
			t2[0]->tr1->offset += t2[1]->value;
			acl.nextl--;
		}
	} else if (op == TIMES || op == AND) {
		t1 = acl.llist[acl.nextl];
		if (t1->op == CON) {
			if (t1->value == 0)
				return t1;
			if (op == AND && (t1->value & 0177400) == 0) {
				for (i = 0; i < acl.nextl; i++) {
					t2 = &acl.llist[i];
					if ((*t2)->op == ITOC)
						(*t2)->op = CTOI;
				}
			} else if ((op == TIMES && t1->value == 1)
				|| (op == AND && t1->value == -1)) {
				if (acl.nextl > 0 && --acl.nextl <= 0) {
					t1 = acl.llist[0];
					if (tree->type == UNSIGN)
						t1->type = tree->type;
					return t1;
				}
			}
		}
	}
	if (op == PLUS && !flt)
		distrib(&acl);
	tree = *(t2 = &acl.llist[0]);
	d = max(degree(tree), islong(tree->type));
	if (op == TIMES && !flt)
		d++;
	for (i = 0; i < acl.nextl; i++) {
		t1 = acl.nlist[i];
		t1->tr2 = t = *++t2;
		d1 = degree(t);
		t1->degree = (int16_t)(d = d == d1 ? d + islong(t1->type) : max(d, d1));
		t1->tr1 = tree;
		tree = t1;
		if (tree->type == LONG) {
			if (tree->op == TIMES)
				tree = hardlongs(tree);
			else if (tree->op == PLUS && (t = isconstant(tree->tr1)) != 0
			       && t->value < 0 && t->type != UNSIGN) {
				tree->op = MINUS;
				t->value = (int16_t)-t->value;
				t = tree->tr1;
				tree->tr1 = tree->tr2;
				tree->tr2 = t;
			}
		}
	}
	if (tree->op == TIMES && ispow2(tree))
		tree->degree = (int16_t)max(degree(tree->tr1), islong(tree->type));
	return tree;
}

/*
 * 5254 distrib - v7's. A divisor of 0, or -32768 / -1, traps the
 * original's idiv (SIGFPE, landing in fpeintr()); the port refuses there.
 */
static void
distrib(struct acl *list)
{
	struct tnode **p1, **p2;
	struct tnode *t;
	int ndmaj, ndmin;
	struct tnode **dividend, **divisor = 0;
	struct tnode **maxnod, **mindiv = 0;

loop:
	maxnod = &list->llist[list->nextl];
	ndmaj = 1000;
	dividend = 0;
	for (p1 = list->llist; p1 <= maxnod; p1++) {
		if ((*p1)->op != TIMES || (*p1)->tr2->op != CON)
			continue;
		ndmin = 0;
		for (p2 = list->llist; p2 <= maxnod; p2++) {
			if (p1 == p2 || (*p2)->op != TIMES || (*p2)->tr2->op != CON)
				continue;
			if ((*p1)->tr2->value == (*p2)->tr2->value) {
				(*p2)->tr2 = (*p1)->tr1;
				(*p2)->op = PLUS;
				(*p1)->tr1 = (*p2);
				*p1 = optim(*p1);
				squash(p2, maxnod);
				list->nextl--;
				goto loop;
			}
			if (rem16((*p2)->tr2->value, (*p1)->tr2->value) == 0)
				goto contmaj;
			if (rem16((*p1)->tr2->value, (*p2)->tr2->value) == 0) {
				ndmin++;
				mindiv = p2;
			}
		}
		if (ndmin > 0 && ndmin < ndmaj) {
			ndmaj = ndmin;
			dividend = p1;
			divisor = mindiv;
		}
	contmaj:;
	}
	if (dividend == 0)
		return;
	t = list->nlist[--list->nextn];
	p1 = dividend;
	p2 = divisor;
	t->op = PLUS;
	t->type = (*p1)->type;
	t->tr1 = (*p1);
	t->tr2 = (*p2)->tr1;
	(*p1)->tr2->value = div16((*p1)->tr2->value, (*p2)->tr2->value);
	(*p2)->tr1 = t;
	t = optim(*p2);
	if (p1 < p2) {
		*p1 = t;
		squash(p2, maxnod);
	} else {
		*p2 = t;
		squash(p1, maxnod);
	}
	list->nextl--;
	goto loop;
}

/*
 * 53fc squash - v7's.
 */
static void
squash(struct tnode **p, struct tnode **maxp)
{
	struct tnode **np;

	for (np = p; np < maxp; np++)
		*np = *(np + 1);
}

/*
 * 5416 const (const_ here: "const" is a keyword) - delta: the unsigned
 * UDIV and UMOD; no ANDN... wait, ANDN is kept; the shifts are the
 * 8086's (a count is not masked: from 16 on, << gives 0 and >> the sign).
 * PTOI's unsigned division by 0 and -32768 / -1 trap the original's
 * division (SIGFPE); the port refuses there.
 */
void
const_(int op, int16_t *vp, int av)
{
	int16_t v;

	v = (int16_t)av;
	switch (op) {

	case PTOI:
		*vp = (int16_t)udiv16(*vp, v);
		return;

	case PLUS:
		*vp = (int16_t)(*vp + v);
		return;

	case TIMES:
		*vp = (int16_t)(*vp * v);
		return;

	case AND:
		*vp &= v;
		return;

	case OR:
		*vp |= v;
		return;

	case EXOR:
		*vp ^= v;
		return;

	case DIVIDE:
	case MOD:
		if (v == 0)
			error("Divide check");
		else if (op == DIVIDE)
			*vp = div16(*vp, v);
		else
			*vp = rem16(*vp, v);
		return;

	case UDIV:
	case UMOD:
		if (v == 0)
			error("Divide check");
		else if (op == UDIV)
			*vp = (int16_t)udiv16(*vp, v);
		else
			*vp = (int16_t)urem16(*vp, v);
		return;

	case RSHIFT:
		*vp = sar16(*vp, v);
		return;

	case LSHIFT:
		*vp = (int16_t)shl16((uint16_t)*vp, v);
		return;

	case ANDN:
		*vp &= (int16_t)~v;
		return;
	}
	error("C error: const");
}

/*
 * 5538 lconst - delta: a plain CON operand counts as well (its value
 * sign-extended for an int, else zero-extended), on the left after an
 * ITOL, on the right before one. ">>" shifts by the HIGH word of the
 * right operand - the original's code loads the count from it (text
 * 5699) -, "<<" by the low word, both one bit at a time. And when that
 * count is 0 both jump into the middle of an instruction (the "je" at
 * text 56a4 lands on the displacement byte of the "jmp" at 56b3, the one
 * at 56f4 on that of the "jmp" at 5702) and the original runs whatever
 * those bytes decode to - under the emulator it ends up restarting
 * main(). So ">>" works only for a count from 65536 up and "<<" for one
 * whose low word is not 0; the port refuses the others.
 */
struct tnode *
lconst(int op, struct tnode *lp, struct tnode *rp)
{
	int32_t l, r;
	uint16_t n;

	if (lp->op == LCON)
		l = lvalue(lp);
	else if (lp->op == ITOL && lp->tr1->op == CON) {
		if (lp->tr1->type == INT)
			l = lp->tr1->value;
		else
			l = (uint16_t)lp->tr1->value;
	} else if (lp->op == CON) {
		if (lp->type == INT)
			l = lp->value;
		else
			l = (uint16_t)lp->value;
	} else
		return 0;
	if (rp->op == LCON)
		r = lvalue(rp);
	else if (rp->op == CON) {
		if (rp->type == INT)
			r = rp->value;
		else
			r = (uint16_t)rp->value;
	} else if (rp->op == ITOL && rp->tr1->op == CON) {
		if (rp->tr1->type == INT)
			r = rp->tr1->value;
		else
			r = (uint16_t)rp->tr1->value;
	} else
		return 0;
	switch (op) {

	case PLUS:
		l = (int32_t)((uint32_t)l + (uint32_t)r);
		break;

	case MINUS:
		l = (int32_t)((uint32_t)l - (uint32_t)r);
		break;

	case TIMES:
	case LTIMES:
		l = (int32_t)((uint32_t)l * (uint32_t)r);
		break;

	case DIVIDE:
	case LDIV:
		if (r == 0)
			error("Divide check");
		else
			l = c1_aldiv(l, r);
		break;

	case MOD:
	case LMOD:
		if (r == 0)
			error("Divide check");
		else
			l = c1_alrem(l, r);
		break;

	case AND:
		l &= r;
		break;

	case ANDN:
		l &= ~r;
		break;

	case OR:
		l |= r;
		break;

	case EXOR:
		l ^= r;
		break;

	case LSHIFT:
		if ((n = (uint16_t)r) == 0)
			c1trap("a long constant shifted left by a count whose low "
			    "word is 0 (lconst() jumps into an instruction)");
		for (; n != 0; n--)
			l = (int32_t)((uint32_t)l << 1);
		break;

	case RSHIFT:
		if ((n = (uint16_t)((uint32_t)r >> 16)) == 0)
			c1trap("a long constant shifted right by a count below "
			    "65536 (lconst() jumps into an instruction)");
		for (; n != 0; n--)
			l = (l >> 1);	/* sar dx / rcr ax */
		break;

	default:
		return 0;
	}
	if (lp->op == LCON) {
		setlvalue(lp, l);
		return lp;
	}
	lp = getblk(sizeof(*lp));
	lp->op = LCON;
	lp->type = LONG;
	setlvalue(lp, l);
	return lp;
}

/*
 * 579a insert - v7's.
 */
static void
insert(int op, struct tnode *atree, struct acl *list)
{
	int d, d1, i;
	struct tnode *t, *tree;

	tree = atree;
ins:
	if (tree->op != op)
		tree = optim(tree);
	if (tree->op == op && list->nextn < LSTSIZ - 2) {
		list->nlist[list->nextn++] = tree;
		insert(op, tree->tr1, list);
		insert(op, tree->tr2, list);
		return;
	}
	if (!isfloat(tree)) {
		/* c1*(x+c2) -> c1*x+c1*c2 */
		if ((tree->op == TIMES || tree->op == LSHIFT)
		  && tree->tr2->op == CON && tree->tr2->value > 0
		  && tree->tr1->op == PLUS && tree->tr1->tr2->op == CON) {
			d = tree->tr2->value;
			if (tree->op == TIMES)
				tree->tr2->value = (int16_t)(tree->tr2->value
				    * tree->tr1->tr2->value);
			else
				tree->tr2->value = (int16_t)shl16(
				    (uint16_t)tree->tr1->tr2->value, d);
			tree->tr1->tr2->value = (int16_t)d;
			tree->tr1->op = tree->op;
			tree->op = PLUS;
			tree = optim(tree);
			if (op == PLUS)
				goto ins;
		}
	}
	d = degree(tree);
	for (i = 0; i < list->nextl; i++) {
		if ((d1 = degree(list->llist[i])) < d) {
			t = list->llist[i];
			list->llist[i] = tree;
			tree = t;
			d = d1;
		}
	}
	list->llist[list->nextl++] = tree;
}

/*
 * 593a tnode - v7's.
 */
struct tnode *
tnode(int op, int type, struct tnode *tr1, struct tnode *tr2)
{
	struct tnode *p;

	p = getblk(sizeof(*p));
	p->op = (int16_t)op;
	p->type = (int16_t)type;
	p->degree = 0;
	p->tr1 = tr1;
	if (opdope[op] & BINARY)
		p->tr2 = tr2;
	else
		p->tr2 = 0;
	return p;
}

/*
 * 5980 tconst - v7's.
 */
struct tnode *
tconst(int val, int type)
{
	struct tnode *p;

	p = getblk(sizeof(*p));
	p->op = CON;
	p->type = (int16_t)type;
	p->value = (int16_t)val;
	return p;
}

/*
 * 59a6 getblk - v7's, on a host arena. The original hands out its data
 * segment's break from funcbase on, and getree() sets curbase back to
 * funcbase after each expression, so blocks are reused without being
 * cleared; it reports "Out of space-- c1" when its 64K data segment is
 * full. Here each block is cleared when handed out (see
 * docs/C1_PORT_PLAN.md 5.3), and the arena is large enough for any input
 * the original can compile. Consecutive blocks of one size are
 * contiguous, as getree()'s SWIT case needs (pswitch() takes them as an
 * array): a block is aligned only as far as its size requires.
 */
#define	CORESIZE	(4L << 20)

char	*funcbase, *curbase;		/* 431e, 4320 */
static	char	*coremax;		/* 4322 */

void
blkinit(void)
{
	funcbase = curbase = malloc(CORESIZE);
	if (funcbase == 0) {
		error("Out of space-- c1");
		exit(1);
	}
	coremax = funcbase + CORESIZE;
}

void *
getblk(int size)
{
	char *p;
	uintptr_t a;

	if (size & 01)
		abort();
	a = size & 7 ? (size & 3 ? 2 : 4) : 8;
	p = (char *)(((uintptr_t)curbase + a - 1) & ~(a - 1));
	if ((curbase = p + size) >= coremax) {
		error("Out of space-- c1");
		exit(1);
	}
	memset(p, 0, (size_t)size);
	return p;
}

/*
 * 59f8 islong - v7's.
 */
int
islong(int t)
{
	if (t == LONG)
		return 2;
	return 1;
}

/*
 * 5a10 isconstant - delta: a CON, or an ITOL of a CON (v7: an SFCON as
 * well).
 */
struct tnode *
isconstant(struct tnode *t)
{
	if (t->op == CON)
		return t;
	if (t->op == ITOL && t->tr1->op == CON)
		return t->tr1;
	return 0;
}

/*
 * 5a3a fconchk - new: a floating constant is an SFCON of type float if
 * the low four bytes of its double are 0 (it fits the MUTOS float
 * format, the double's high half), otherwise an FCON of type double.
 * The value word (its label) is kept.
 */
struct tnode *
fconchk(struct tnode *p)
{
	if ((p->fvalue[0] | p->fvalue[1] | p->fvalue[2] | p->fvalue[3]) == 0) {
		p->op = SFCON;
		p->type = FLOAT;
	} else {
		p->op = FCON;
		p->type = DOUBLE;
	}
	return p;
}

/*
 * 5a68 hardlongs - v7's.
 */
struct tnode *
hardlongs(struct tnode *t)
{
	switch (t->op) {

	case TIMES:
	case DIVIDE:
	case MOD:
		t->op += LTIMES - TIMES;
		break;

	case ASTIMES:
	case ASDIV:
	case ASMOD:
		t->op += LASTIMES - ASTIMES;
		t->tr1 = tnode(AMPER, LONG + PTR, t->tr1, 0);
		break;

	default:
		return t;
	}
	return optim(t);
}

/*
 * 5aba isregptr - new: a register variable in SI, DI or BP - one the
 * 8086 can use as a base.
 */
int
isregptr(struct tnode *p)
{
	return p->op == NAME && p->class == REG
	    && (p->nloc == SI || p->nloc == DI || p->nloc == BP);
}

/*
 * 5aec werror - new: a warning, which does not count as an error.
 */
void
werror(const char *s, ...)
{
	va_list ap;

	fprintf(stderr, "%d: Warning: ", line);
	va_start(ap, s);
	vfprintf(stderr, s, ap);
	va_end(ap);
	putc('\n', stderr);
}
