/*
 * c21.c - the original MUTOS 1700 c2's functions from rmove() to the end
 * of the program, in the binary's order (tests/realbin/c2map.txt): v7's
 * c21.c rewritten for the 8086, then the passes the binary adds after
 * natural(). Each function's comment gives its text offset and what the
 * binary does differently from v7 ("x86": rewritten for the 8086, "new":
 * no v7 counterpart). The data are in the memory image (c2.h); the
 * locals are host variables of the original's widths.
 *
 * The registers: regs[0..5] hold what ax, dx, si, di, bx and cx are
 * known to hold (an operand's text up to its comma), regs[6..11] the
 * constant each holds ("*n." without a register in it), regs[12] (RT1)
 * an instruction's source operand, regs[13] (RT2) its destination - in
 * the MUTOS assembler's order "op dst,src" dualop() puts the first
 * operand into RT2. A node's two new bytes (N_USE, N_SET) are the
 * registers it reads and writes, as regmask() works them out: bit r for
 * register r of isreg() (ax 0, dx 1, si 2, di 3, bx 4, cx 5, bp 6).
 */
#include <stdlib.h>

#include "c2.h"

static	int16_t	toofar(uint16_t p);
static	int16_t	iabs(int16_t x);
static	void	savereg(int16_t ai, uint16_t as);
static	void	dest(uint16_t as, int16_t flt);
static	int16_t	findrand(uint16_t as, int16_t flt);
static	void	convreg(uint16_t s, int16_t r);
static	int16_t	bytereg(int16_t r);
static	void	repladdr(uint16_t p, int16_t f, int16_t flt);
static	void	redunbr(uint16_t p);
static	uint16_t findcon(int16_t i);
static	int16_t	compare(int16_t oper, uint16_t cp1, uint16_t cp2);
static	void	setcon(uint16_t ar1, uint16_t ar2);
static	int16_t	natural(uint16_t ap);
static	int16_t	addindex(uint16_t s, int16_t from, int16_t to);
static	int16_t	renback(uint16_t p);
static	int16_t	pinned(uint16_t s, int16_t from, int16_t to);
static	void	renreg(uint16_t s, int16_t from, int16_t to);
static	int16_t	renfwd(uint16_t p);
void	check(void);

/*
 * The register-variable test of the new passes: "(m & (017 << (nreg+1)))
 * & 017" - with 3 (no "|NREG" below 3) always 0; with nreg n < 3 the
 * registers di, si, dx above it are not touched. m is a mask byte; the
 * shift is the 8086's (by cl, the low byte of nreg+1).
 */
static int16_t
regvar(int16_t m)
{
	return (int16_t)((uint16_t)(uint8_t)m
	    & shl16(017, (unsigned)(uint16_t)(RI(NREG) + 1)) & 017);
}

/* the bit of register r (the original's "1 << r", shl by cl) */
static int16_t
rbit(int16_t r)
{
	return (int16_t)shl16(1, (unsigned)(uint16_t)r);
}

/* a node out of the list */
static void
unlink_node(uint16_t p)
{
	S_BACK(FORW(p), BACK(p));
	S_FORW(BACK(p), FORW(p));
}

/*
 * rmove() (text 0fb8): x86. The redundant-move test reads the registers'
 * contents (regs[]), the source held in the destination register, and
 * "mov r,r"; a "movb" clears what is known; a move between registers
 * loses its byte suffix; no source()/setcc(): ccloc is only ever cleared,
 * so that a "test" without a comma is taken for redundant and removed
 * (C2-04). New cases: "lea r,(di)" .. "(bx)" becomes "mov r,di" (its
 * register then forgotten - regs[-1] when the destination is no register,
 * C2-05), "sub r,r" becomes "mov r,*0." and is looked at again, "or r,r"
 * "cmp r,*0." ("ors -> tests"), "test r,r" (not ax) "cmp r,*0.";
 * imul, idiv and div forget ax and dx, cwd dx, cbw ax, loop cx; push
 * only replaces its operand's address; a long branch after "cmp" with
 * two constants is removed or made a JBR (compare()); "|" and "seg"
 * leave the registers as they are.
 */
void
rmove(void)
{
	uint16_t p, q;
	int16_t r, r1, flt;

	for (p = FORW(FIRST); p != 0; p = FORW(p)) {
		flt = 0;
		switch (OP(p)) {

		case MOV:
			if (SUBOP(p) == BYTE) {
				clearreg();
				break;
			}
			dualop(p);
			if ((r = isreg(REG(RT2))) >= 0 && equstr(REG(r), REG(RT1)))
				goto redundant;
			if ((r = findrand(REG(RT1), flt)) >= 0
			    && isreg(REG(RT2)) == r)
				goto redundant;
			if ((r = isreg(REG(RT2))) >= 0 && isreg(REG(RT1)) == r)
				goto redundant;
			r = isreg(REG(RT1));
			r1 = isreg(REG(RT2));
			if (r >= 0 && r1 >= 0)
				S_SUBOP(p, 0);
			else if (SUBOP(p) == BYTE)
				goto dble;	/* not reached: a movb was cleared */
			repladdr(p, 0, flt);
			dest(REG(RT2), flt);
			if (r >= 0) {
				if (r1 >= 0) {
					if (r == 6)
						break;
					savereg((int16_t)(r1 + flt), REG(r + flt));
				} else
					savereg((int16_t)(r + flt), REG(RT2));
			} else {
				if (r1 >= 0)
					savereg((int16_t)(r1 + flt), REG(RT1));
				else
					setcon(REG(RT1), REG(RT2));
			}
			break;
		redundant:
			unlink_node(p);
			INCW(REDUNM);
			INCW(NCHANGE);
			break;

		case LEA:
			dualop(p);
			if (c_strcmp(S_PDI, REG(RT1)) == 0
			    || c_strcmp(S_PSI, REG(RT1)) == 0
			    || c_strcmp(S_PBP, REG(RT1)) == 0
			    || c_strcmp(S_PBX, REG(RT1)) == 0) {
				S_OP(p, MOV);
				q = CODE(p);
				while (rb(q++) != ',')
					;
				wb(q, rb((uint16_t)(q + 1)));
				wb((uint16_t)(q + 1), rb((uint16_t)(q + 2)));
				wb((uint16_t)(q + 2), 0);
			}
			r = isreg(REG(RT2));
			wb(REG(r), 0);
			wb(REG(r + NRT), 0);
			break;

		case IMUL:
		case IDIV:
		case DIV:
			singop(p);
			repladdr(p, 0, flt);
			wb(REG(6), 0);
			wb(REG(0), 0);
			wb(REG(7), 0);
			wb(REG(1), 0);
			wb(CCLOC, 0);
			break;

		case PUSH:
			singop(p);
			repladdr(p, 0, flt);
			break;

		case AND:
		case OR:
		case ADD:
		case SUB:
		case XOR:
		dble:
			dualop(p);
			if (OP(p) == SUB && (r = isreg(REG(RT1))) >= 0
			    && isreg(REG(RT2)) == r) {
				S_OP(p, MOV);
				c_strcpy(REG(RT1), S_CZERO1);
				S_CODE(p, copy2(REG(RT2), REG(RT1)));
				p = BACK(p);
				break;
			}
			if (OP(p) == OR && c_strcmp(REG(RT1), REG(RT2)) == 0) {
				S_OP(p, CMP);
				S_CODE(p, copy2(REG(RT2), S_CZERO2));
				INCW(NCHANGE);
				INCW(NORS);
				break;
			}
			repladdr(p, 0, flt);
			dest(REG(RT2), flt);
			wb(CCLOC, 0);
			break;

		case INC:
		case DEC:
		case NEG:
			singop(p);
			dest(REG(RT1), flt);
			wb(CCLOC, 0);
			break;

		case CWD:
			wb(REG(7), 0);
			wb(REG(1), 0);
			break;

		case CBW:
			wb(REG(6), 0);
			wb(REG(0), 0);
			break;

		case LOOP:
			wb(REG(11), 0);
			wb(REG(5), 0);
			break;

		case TST:
			dualop(p);
			if (c_strcmp(REG(RT1), REG(RT2)) == 0 && isreg(REG(RT1)) > 0) {
				S_OP(p, CMP);
				S_CODE(p, copy2(REG(RT2), S_CZERO3));
				break;
			}
			repladdr(p, 1, flt);
			if (equstr(REG(RT1), CCLOC)) {
				S_FORW(BACK(p), FORW(p));
				S_BACK(FORW(p), BACK(p));
				p = BACK(p);
				INCW(NRTST);
				INCW(NCHANGE);
			}
			break;

		case CMP:
			dualop(p);
			repladdr(p, 1, flt);
			wb(CCLOC, 0);
			break;

		case CBR:
			if (OP(BACK(p)) == CMP) {
				uint16_t a, b;

				dualop(BACK(p));
				a = findcon(RT1);
				b = findcon(RT2);
				r = compare(SUBOP(p), b, a);
				if (r == 0) {
					if (OP(FORW(p)) == CBR) {
						S_FORW(BACK(p), FORW(p));
						S_BACK(FORW(p), BACK(p));
					} else {
						S_FORW(BACK(BACK(p)), FORW(p));
						S_BACK(FORW(p), BACK(BACK(p)));
					}
					decref(REF(p));
					p = BACK(BACK(p));
					INCW(NCHANGE);
				} else if (r > 0) {
					S_OP(p, JBR);
					S_SUBOP(p, 0);
					S_FORW(BACK(BACK(p)), p);
					S_BACK(p, BACK(BACK(p)));
					p = BACK(p);
					INCW(NCHANGE);
				}
			}
			break;

		case JBR:
			redunbr(p);
			clearreg();
			break;

		case BAR:
		case SEG:
			break;

		default:
			clearreg();
			break;
		}
	}
}

/* jumpsw() (text 14c8) */
int16_t
jumpsw(void)
{
	uint16_t p, p1, tp;
	int16_t t, nj;

	t = 0;
	nj = 0;
	for (p = FORW(FIRST); p != 0; p = FORW(p))
		S_REFC(p, ++t);
	for (p = FORW(FIRST); p != 0; p = p1) {
		p1 = FORW(p);
		if (OP(p) == CBR && OP(p1) == JBR && REF(p) && REF(p1)
		    && iabs((int16_t)(uint16_t)((uint16_t)REFC(p) - (uint16_t)REFC(REF(p))))
		    > iabs((int16_t)(uint16_t)((uint16_t)REFC(p1)
		    - (uint16_t)REFC(REF(p1))))) {
			if (REF(p) == REF(p1))
				continue;
			S_SUBOP(p, rb((uint16_t)(REVBR + SUBOP(p))));
			tp = REF(p1);
			S_REF(p1, REF(p));
			S_REF(p, tp);
			t = LABNO(p1);
			S_LABNO(p1, LABNO(p));
			S_LABNO(p, t);
			INCW(NREVBR);
			nj++;
		}
	}
	return nj;
}

/*
 * shortbr() (text 1588, in v7's addsob()'s place): new. A long branch
 * (CBR: "beq" .. "bhis", JBR: "br") whose target is near enough becomes
 * the short one ("je" .. "jae", "j"), until no more change; not "br" to
 * a code target that starts with "@" (an indirect jump). Counted as
 * "long branches to short".
 */
void
shortbr(void)
{
	uint16_t p;
	int16_t changed, sh;

	changed = 1;
	while (changed) {
		changed = 0;
		for (p = FORW(FIRST); p != 0; p = FORW(p)) {
			sh = 0;
			if (OP(p) == CBR && !toofar(p)) {
				S_OP(p, SCBR);
				sh = 1;
			}
			if (!sh && OP(p) == JBR && rb(CODE(p)) != '@' && !toofar(p)) {
				S_OP(p, SJBR);
				sh = 1;
			}
			if (sh) {
				INCW(NSHORT);
				changed++;
			}
		}
	}
}

/* toofar()'s lengths of an operand pair's forms (labels of the binary) */
#define	L_REG	2	/* 1753 */
#define	L_DISP8	3	/* 167a */
#define	L_DISP16 4	/* 1681 */

/*
 * The length of an instruction with a register on one side and the
 * operand s on the other: 2 for "(r)", 3 for "*n.(r)", 4 otherwise
 * (toofar(), text 16b6 and its copies).
 */
static int16_t
memlen(uint16_t s)
{
	if (rb(s) == '(')
		return L_REG;
	if (rb(s) != '*')
		return L_DISP16;
	return c_index(s, '(') ? L_DISP8 : L_DISP16;
}

/* the same with the index test first (text 1793 and 17c2) */
static int16_t
memlen2(uint16_t s)
{
	if (rb(s) == '(')
		return L_REG;
	if (!c_index(s, '('))
		return L_DISP16;
	return rb(s) == '*' ? L_DISP8 : L_DISP16;
}

/* a single operand (text 1818): 2 for a register, else as memlen2() */
static int16_t
onelen(uint16_t s)
{
	if (isreg(s) >= 0)
		return L_REG;
	if (!c_index(s, '('))
		return L_DISP16;
	return rb(s) == '*' ? L_DISP8 : L_DISP16;
}

/*
 * toofar() (text 15e8, v7's toofar() with ilen() and adrlen() in it):
 * x86. The length of the code between a branch and its label, from the
 * nodes' text: 0 for a label or a comment, 2 for a short branch or
 * "loop", 5 for a long conditional branch, 3 for "br" and "call" (4 with
 * an indirect operand), 1 for cwd and cbw, and for the others a length
 * from the operands' forms (a register; "(r)"; "*n.(r)", a byte
 * displacement; anything else 2 bytes more) - 6 for any other node,
 * "|NREG" and the directives included. A forward branch counts from the
 * next node, a backward one from 2 (its own length) and the node before.
 * More than 127 is too far; a target without a label number (a code
 * target, "cret") 10000. The walk follows the list from the branch to
 * its label; the order of jumpsw()'s numbers in refc says which way.
 */
static int16_t
toofar(uint16_t p)
{
	uint16_t q;
	int16_t len, fwd, rs, rd;

	if (LABNO(p) == 0)
		return 10000;
	if (REFC(REF(p)) >= REFC(p)) {
		len = 0;
		fwd = 1;
		q = FORW(p);
	} else {
		len = 2;
		fwd = 0;
		q = BACK(p);
	}
	for (; q != REF(p); q = fwd ? FORW(q) : BACK(q)) {
		switch (OP(q)) {

		case CBR:
			len += 5;
			break;

		case LABEL:
		case BAR:
			break;

		case SCBR:
		case SJBR:
		case LOOP:
			len += 2;
			break;

		case AND:
		case OR:
		case CMP:
		case ADD:
		case SUB:
		case XOR:
		case ADC:
		case SBB:
			dualop(q);
			if (OP(q) == ADD && c_strcmp(REG(RT2), S_SP1) == 0) {
				len += rb(REG(RT1)) == '*' ? 3 : 4;
				break;
			}
			rs = isreg(REG(RT1));
			rd = isreg(REG(RT2));
			if (rs >= 0 && rd >= 0)
				len += 2;
			else if (rs >= 0)
				len += memlen(REG(RT2));
			else if (rd >= 0)
				len += memlen(REG(RT1));
			else {
				/* an immediate or memory into memory */
				len += 2;
				if (rb(REG(RT1)) == '*' && c_index(REG(RT1), '('))
					len += 1;
				else
					len += 2;
				if (rb(REG(RT2)) == '*' && c_index(REG(RT2), '('))
					len += 1;
				else if (rb(REG(RT2)) != '(')
					len += 2;
			}
			break;

		case MOV:
		case TST:
			dualop(q);
			rs = isreg(REG(RT1));
			rd = isreg(REG(RT2));
			if (rs >= 0 && rd >= 0)
				len += 2;
			else if (rs >= 0)
				len += memlen2(REG(RT2));
			else if (rd >= 0)
				len += memlen2(REG(RT1));
			else if (rb(REG(RT1)) == '(')
				len += 4;
			else if (c_index(REG(RT1), '(') && rb(REG(RT1)) == '*')
				len += 5;
			else
				len += 6;
			break;

		case SAL:
		case SAR:
		case SHR:
			dualop(q);
			len += onelen(REG(RT1));
			break;

		case JBR:
		case CALL:
			singop(q);
			len += c_index(REG(RT1), '(') ? 4 : 3;
			break;

		case XCHG:
			dualop(q);
			if ((rs = isreg(REG(RT1))) < 0)
				len += 4;
			else if ((rd = isreg(REG(RT2))) < 0)
				len += 4;
			else if (rs == 0 || rd == 0)
				len += 1;
			else
				len += 2;
			break;

		case CWD:
		case CBW:
			len += 1;
			break;

		case LEA:
			dualop(q);
			if (rb(REG(RT1)) == '(')
				len += 2;
			else if (rb(REG(RT1)) == '*')
				len += 3;
			else
				len += 4;
			break;

		case NEG:
		case IMUL:
		case IDIV:
		case NOT:
		case DIV:
			singop(q);
			len += onelen(REG(RT1));
			break;

		case POP:
		case PUSH:
		case INC:
		case DEC:
			singop(q);
			if (isreg(REG(RT1)) >= 0)
				len += 1;
			else
				len += memlen2(REG(RT1));
			break;

		default:
			len += 6;
			break;
		}
	}
	if (len < -0200 || len > 0177)
		return 1;
	return 0;
}

/* abs() (text 19b8) */
static int16_t
iabs(int16_t x)
{
	return x < 0 ? (int16_t)(uint16_t)(0u - (uint16_t)x) : x;
}

/* equop() (text 19d2) */
int16_t
equop(uint16_t ap1, uint16_t p2)
{
	uint16_t cp1, cp2, p1;

	p1 = ap1;
	if (OP(p1) != OP(p2) || SUBOP(p1) != SUBOP(p2))
		return 0;
	if (OP(p1) > 0 && OP(p1) < MOV)
		return 0;
	cp1 = CODE(p1);
	cp2 = CODE(p2);
	if (cp1 == 0 && cp2 == 0)
		return 1;
	if (cp1 == 0 || cp2 == 0)
		return 0;
	for (;;) {
		int16_t c = rb(cp2++);

		if (c != rb(cp1))
			return 0;
		if (rb(cp1++) == 0)
			return 1;
	}
}

/* decref() (text 1a4a) */
void
decref(uint16_t p)
{
	S_REFC(p, REFC(p) - 1);
	if (REFC(p) <= 0) {
		INCW(NRLAB);
		S_FORW(BACK(p), FORW(p));
		S_BACK(FORW(p), BACK(p));
	}
}

/* nonlab() (text 1a70) */
uint16_t
nonlab(uint16_t p)
{
	while (p && OP(p) == LABEL)
		p = FORW(p);
	return p;
}

/* alloc() (text 1a96): v7's; the test of the end is unsigned */
uint16_t
alloc(int16_t n)
{
	uint16_t un, p;

	un = (uint16_t)((uint16_t)n + 1) & 0xfffe;
	if ((uint16_t)(rw(LASTA) + un) >= rw(LASTR)) {
		if (c2_sbrk(2000) == -1) {
			c_fprintf(STDERR, S_NOSPACE, NULL, 0);
			c2_exit(1);
		}
		ww(LASTR, (uint16_t)(rw(LASTR) + 2000));
	}
	p = rw(LASTA);
	ww(LASTA, (uint16_t)(rw(LASTA) + un));
	return p;
}

/* clearreg() (text 1aea): regs[0..11], conloc, ccloc (v7: conval) */
void
clearreg(void)
{
	int16_t i;

	for (i = 0; i < 2 * NRT; i++)
		wb(REG(i), 0);
	wb(CONLOC, 0);
	wb(CCLOC, 0);
}

/*
 * savereg() (text 1b10): x86. Not for bp (6); an operand through a
 * register other than bp is not kept; the operand is cut at its comma
 * (v7's); a constant ("*n." without a register) is kept whole in
 * regs[ai+6] too. No source() first.
 */
static void
savereg(int16_t ai, uint16_t as)
{
	uint16_t p, s, sp;
	int16_t c;

	if (ai == 6)
		return;
	sp = p = REG(ai);
	s = as;
	for (;;) {
		c = rb(s);
		wb(p++, c);
		if (c == 0)
			break;
		if (rb(s) == '(' && isreg((uint16_t)(s + 1)) != 6) {
			wb(sp, 0);
			return;
		}
		if (rb(s++) == ',')
			break;
	}
	wb(--p, 0);
	if (rb(as) == '*' && c_index(as, '(') == 0)
		c_strcpy(REG(ai + NRT), as);
}

/*
 * dest() (text 1b96): x86. A destination register is forgotten with its
 * constant; every register that holds the destination is forgotten - but
 * findrand() names a match in regs[6..11] by the register's number, so
 * that one clears regs[n-6], and an empty destination (an instruction
 * without operands) matches every empty entry: then the match stays and
 * the loop never ends (C2-01, refused); a store through a register other
 * than bp forgets everything but the constants, and conloc (v7: not "$"
 * ones).
 */
static void
dest(uint16_t as, int16_t flt)
{
	uint16_t s;
	int16_t i;

	s = as;
	if ((i = isreg(s)) >= 0 && i < NRT) {
		wb(REG(i), 0);
		wb(REG(i + NRT), 0);
	}
	for (;;) {
		int16_t j;

		for (j = flt; j < 2 * NRT; j++)
			if (equstr(REG(j), s))
				break;
		if (j >= 2 * NRT)
			break;
		wb(REG(j % NRT), 0);
		if (equstr(REG(j), s))
			c2trap("dest(): a destination that stays equal to a register "
			    "entry (empty, or a constant's) - the original clears it "
			    "for ever (C2-01)");
	}
	while (rb(s) != 0) {
		if (rb(s) == '(' && isreg((uint16_t)(s + 1)) != 6) {
			for (i = 0; i < 2 * NRT; i++) {
				if (!(rb(REG(i)) == '*' && c_index(REG(i), '(') == 0))
					wb(REG(i), 0);
				wb(CONLOC, 0);
			}
			return;
		}
		s++;
	}
}

/* singop() (text 1c48) */
void
singop(uint16_t ap)
{
	uint16_t p1, p2;
	int16_t c;

	p1 = CODE(ap);
	p2 = REG(RT1);
	do {
		c = rb(p1++);
		wb(p2++, c);
	} while (c);
	wb(REG(RT2), 0);
}

/*
 * dualop() (text 1c6a): x86 - the operand before the comma (the
 * destination) goes to regs[RT2], the one after it to regs[RT1].
 */
void
dualop(uint16_t ap)
{
	uint16_t p1, p2;
	int16_t c;

	p1 = CODE(ap);
	p2 = REG(RT2);
	while (rb(p1) != 0 && rb(p1) != ',')
		wb(p2++, rb(p1++));
	wb(p2++, 0);
	p2 = REG(RT1);
	wb(p2, 0);
	if (rb(p1++) != ',')
		return;
	do {
		c = rb(p1++);
		wb(p2++, c);
	} while (c);
}

/*
 * findrand() (text 1cb2): all twelve regs[] entries from flt; the
 * register's number of the match, its index modulo 6.
 */
static int16_t
findrand(uint16_t as, int16_t flt)
{
	int16_t i;

	for (i = flt; i < 2 * NRT; i++)
		if (equstr(REG(i), as))
			return (int16_t)(i % NRT);
	return -1;
}

/*
 * isreg() (text 1cee): x86. The first two characters against the
 * register names of REGTAB (bp 6, cx 5, bx 4, di 3, si 2, dx 1, ax 0) -
 * what follows them is not looked at.
 */
int16_t
isreg(uint16_t as)
{
	int16_t i;
	uint16_t t, w;

	w = (uint16_t)((uint16_t)((uint16_t)rb(as) << 8)
	    + (uint16_t)rb((uint16_t)(as + 1)));
	t = REGTAB - 2;
	i = 8;
	for (;;) {
		if (--i == 0)
			break;
		t = (uint16_t)(t + 2);
		if (rw(t) == w)
			break;
	}
	return (int16_t)(i - 1);
}

/* convreg() (text 1d2a): new - a register's name into s */
static void
convreg(uint16_t s, int16_t r)
{
	static const uint16_t name[7] = { S_AX, S_DX, S_SI, S_DI, S_BX, S_CX,
	    S_BP };

	if ((uint16_t)r > 6) {
		uint16_t a = (uint16_t)r;

		c_fprintf(STDERR, S_BOTCH, &a, 1);
		return;
	}
	c_strcpy(s, name[r]);
}

/* bytereg() (text 1d9c): new - has register r a byte half (ax dx bx cx) */
static int16_t
bytereg(int16_t r)
{
	switch ((uint16_t)r) {
	case 0: case 1: case 4: case 5:
		return 1;
	default:
		return 0;
	}
}

/* check() (text 1dca): v7's; not called */
void
check(void)
{
	uint16_t p, lp;

	lp = FIRST;
	for (p = FORW(FIRST); p != 0; p = FORW(p)) {
		if (BACK(p) != lp)
			abort();
		lp = p;
	}
}

/*
 * repladdr() (text 1dee): x86. An operand a register holds is replaced
 * by the register's name - for a byte instruction only by ax, dx, bx or
 * cx -, the code rebuilt "dst,src" in rt1[50] and rt2[50] (copied into
 * without a check: C2-03).
 */
static void
repladdr(uint16_t p, int16_t f, int16_t flt)
{
	int16_t r, r1, c;
	uint16_t p1, p2;

	if (f)
		r1 = findrand(REG(RT2), flt);
	else
		r1 = -1;
	r = findrand(REG(RT1), flt);
	if (r1 >= NRT)
		r1 -= NRT;
	if (r >= NRT)
		r -= NRT;
	if (r < 0 && r1 < 0)
		return;
	if (SUBOP(p) == BYTE) {
		if (r >= 0 && !bytereg(r))
			return;
		if (r1 >= 0 && !bytereg(r1))
			return;
	}
	p2 = REG(RT1);
	p1 = RT1BUF;
	do {
		c = rb(p2++);
		wb(p1++, c);
	} while (c);
	if (rb(REG(RT2))) {
		p1 = RT2BUF;
		p2 = REG(RT2);
		do {
			c = rb(p2++);
			wb(p1++, c);
		} while (c);
	} else
		wb(RT2BUF, 0);
	if (r >= 0) {
		convreg(RT1BUF, r);
		INCW(NSADDR);
	}
	if (r1 >= 0) {
		convreg(RT2BUF, r1);
		INCW(NSADDR);
	}
	if (rb(RT2BUF)) {
		c_strcat(RT2BUF, S_COMMA1);
		S_CODE(p, copy2(RT2BUF, RT1BUF));
	} else
		S_CODE(p, copy1(RT1BUF));
}

/*
 * movedat() (text 1f24): v7's, but a .data that repeats the segment is
 * not removed. Its local node "data" is in the image's stack area
 * (c2_datanode()); the original reads data.forw uninitialized when no
 * .data moved - zero or not, the list comes out the same.
 */
void
movedat(void)
{
	uint16_t p1, p2, p3, datp, data;
	int16_t seg;

	if (FORW(FIRST) == 0)
		return;
	if (RI(LASTSEG) != TEXT && RI(LASTSEG) != -1) {
		p1 = alloc(NODESZ);
		S_OP(p1, RI(LASTSEG));
		S_SUBOP(p1, 0);
		S_CODE(p1, 0);
		S_FORW(p1, FORW(FIRST));
		S_BACK(p1, FIRST);
		S_BACK(FORW(FIRST), p1);
		S_FORW(FIRST, p1);
	}
	data = c2_datanode();
	datp = data;
	for (p1 = FORW(FIRST); p1 != 0; p1 = FORW(p1)) {
		if (OP(p1) == DATA) {
			p2 = FORW(p1);
			while (p2 && OP(p2) != TEXT)
				p2 = FORW(p2);
			if (p2 == 0)
				break;
			p3 = BACK(p1);
			S_FORW(BACK(p1), FORW(p2));
			S_BACK(FORW(p2), p3);
			S_FORW(p2, 0);
			S_FORW(datp, p1);
			S_BACK(p1, datp);
			p1 = p3;
			datp = p2;
		}
	}
	if (FORW(data)) {
		S_FORW(datp, FORW(FIRST));
		S_BACK(FORW(FIRST), datp);
		S_BACK(FORW(data), FIRST);
		S_FORW(FIRST, FORW(data));
	}
	seg = RI(LASTSEG);
	for (p1 = FORW(FIRST); p1 != 0; p1 = FORW(p1)) {
		if (OP(p1) == TEXT || OP(p1) == DATA || OP(p1) == BSS) {
			if ((p2 = FORW(p1)) != 0) {
				if (OP(p2) == TEXT || OP(p2) == DATA || OP(p2) == BSS)
					S_OP(p1, OP(p2));
			}
			if (OP(p1) == seg || (FORW(p1) && OP(FORW(p1)) == seg)) {
				if (OP(p1) == DATA)
					continue;
				S_FORW(BACK(p1), FORW(p1));
				S_BACK(FORW(p1), BACK(p1));
				p1 = BACK(p1);
				continue;
			}
			seg = OP(p1);
		}
	}
}

/*
 * redunbr() (text 207c): x86. A JBR to a "test" or "cmp" (both read with
 * dualop()) and a long conditional branch whose condition is known from
 * the constants takes that branch's target (not its code).
 */
static void
redunbr(uint16_t p)
{
	uint16_t p1, ap1, ap2;

	if ((p1 = REF(p)) == 0)
		return;
	p1 = nonlab(p1);
	if (OP(p1) == TST || OP(p1) == CMP)
		dualop(p1);
	else
		return;
	if (OP(FORW(p1)) != CBR)
		return;
	ap1 = findcon(RT1);
	ap2 = findcon(RT2);
	p1 = FORW(p1);
	if (compare(SUBOP(p1), ap2, ap1) > 0) {
		INCW(NREDUNJ);
		INCW(NCHANGE);
		decref(REF(p));
		S_REF(p, REF(p1));
		S_LABNO(p, LABNO(p1));
		S_REFC(REF(p), REFC(REF(p)) + 1);
	}
}

/*
 * findcon() (text 210c): x86 - a constant is "*n." without a register;
 * a register gives its constant (regs[r+6]); conloc gives conval.
 */
static uint16_t
findcon(int16_t i)
{
	uint16_t p;
	int16_t r;

	p = REG(i);
	if (rb(p) == '*' && c_index(p, '(') == 0)
		return p;
	if ((r = isreg(p)) >= 0)
		return REG(r + NRT);
	if (equstr(p, CONLOC))
		return CONVAL;
	return p;
}

/*
 * compare() (text 2162): x86 - both operands "*n." without a register,
 * the numbers decimal (v7: "$n", octal), a "." after them and then a "+"
 * skipped, the rest compared as text.
 */
static int16_t
compare(int16_t oper, uint16_t cp1, uint16_t cp2)
{
	int16_t n, n1, n2;

	if (rb(cp1) != '*')
		return -1;
	if (c_index(cp1++, '(') != 0)
		return -1;
	if (rb(cp2) != '*')
		return -1;
	if (c_index(cp2++, '(') != 0)
		return -1;
	n = 0;
	while (rb(cp2) >= '0' && rb(cp2) <= '9') {
		n = (int16_t)(uint16_t)((uint16_t)(n * 10)
		    + (uint16_t)(rb(cp2) - '0'));
		cp2++;
	}
	n2 = n;
	n = 0;
	while (rb(cp1) >= '0' && rb(cp1) <= '9') {
		n = (int16_t)(uint16_t)((uint16_t)(n * 10)
		    + (uint16_t)(rb(cp1) - '0'));
		cp1++;
	}
	n1 = n;
	if (rb(cp1) == '.')
		cp1++;
	if (rb(cp2) == '.')
		cp2++;
	if (rb(cp1) == '+')
		cp1++;
	if (rb(cp2) == '+')
		cp2++;
	for (;;) {
		int16_t c = rb(cp1++);

		if (c != rb(cp2))
			return -1;
		if (rb(cp2++) == 0)
			break;
	}
	switch ((uint16_t)oper) {
	case JEQ:
		return n1 == n2;
	case JNE:
		return n1 != n2;
	case JLE:
		return n1 <= n2;
	case JGE:
		return n1 >= n2;
	case JLT:
		return n1 < n2;
	case JGT:
		return n1 > n2;
	case JLO:
		return (uint16_t)n1 < (uint16_t)n2;
	case JHI:
		return (uint16_t)n1 > (uint16_t)n2;
	case JLOS:
		return (uint16_t)n1 <= (uint16_t)n2;
	case JHIS:
		return (uint16_t)n1 >= (uint16_t)n2;
	}
	return -1;
}

/* setcon() (text 22b6): x86 - the constant is "*n." */
static void
setcon(uint16_t ar1, uint16_t ar2)
{
	uint16_t cl, cv, p;
	int16_t c;

	cv = ar1;
	cl = ar2;
	if (rb(cv) != '*')
		return;
	if (!natural(cl))
		return;
	p = CONLOC;
	do {
		c = rb(cl++);
		wb(p++, c);
	} while (c);
	p = CONVAL;
	do {
		c = rb(cv++);
		wb(p++, c);
	} while (c);
}

/* equstr() (text 2300) */
int16_t
equstr(uint16_t ap1, uint16_t ap2)
{
	uint16_t p1 = ap1, p2 = ap2;

	for (;;) {
		int16_t c = rb(p1++);

		if (c != rb(p2))
			return 0;
		if (rb(p2++) == 0)
			return 1;
	}
}

/*
 * natural() (text 2328): x86 - "*..." without a register, or through bp.
 * (setcc() is gone: ccloc is never set.)
 */
static int16_t
natural(uint16_t ap)
{
	uint16_t q;

	if (rb(ap) != '*')
		return 0;
	if ((q = c_index(ap, '(')) == 0)
		return 1;
	if (isreg((uint16_t)(q + 1)) != 6)
		return 0;
	return 1;
}

/*
 * pushopt() (text 2362): new, after rmove(). From the last node back:
 * renback(), else renfwd() - a register move removed by renaming -; then
 * "mov r,x" "push r" becomes "push x" when r is not read before it is
 * written again (a call writes every register), x is no constant ("*" or
 * "#" without a register) and r is no register variable (regvar()):
 * counted as a redundant move (C2-02). Then, from the first node,
 * "push x" ... "pop r" becomes "mov r,x" ("pop/push converted to mov")
 * when nothing between reads or writes r, pushes, pops, branches or is
 * a label.
 */
void
pushopt(void)
{
	uint16_t p, s;
	int16_t r;

	p = regmask();
	while (p != FIRST) {
		if (!renback(p))
			renfwd(p);
		if (OP(p) != PUSH || OP(BACK(p)) != MOV || SUBOP(BACK(p)) != 0
		    || USE(p) == 0)
			goto next;
		singop(p);
		if (regvar(USE(p)))
			goto next;
		if ((r = isreg(REG(RT1))) < 0)
			goto next;
		s = BACK(p);
		dualop(s);
		if (isreg(REG(RT2)) != r)
			goto next;
		if (isreg(REG(RT1)) < 0) {
			if (c_index(REG(RT1), '*') || c_index(REG(RT1), '#')) {
				if (c_index(REG(RT1), '(') == 0)
					goto next;
			}
		}
		for (s = FORW(p); s != 0; s = FORW(s)) {
			if (OP(s) == CALL)
				break;
			if (USE(s) & USE(p))
				goto next;
			if (SET(s) & USE(p))
				break;
		}
		S_USE(p, USE(BACK(p)));
		S_CODE(p, copy1(REG(RT1)));
		S_FORW(BACK(BACK(p)), p);
		S_BACK(p, BACK(BACK(p)));
		INCW(REDUNM);
		INCW(NCHANGE);
	next:
		p = BACK(p);
	}
	for (p = FORW(FIRST); p != 0; p = FORW(p)) {
		if (OP(p) != POP || SET(p) == 0 || regvar(SET(p)))
			continue;
		for (s = BACK(p); s != FIRST; s = BACK(s)) {
			if (OP(s) == PUSH)
				break;
			if (OP(s) == POP || OP(s) == CBR || (USE(s) & SET(p))
			    || (SET(s) & SET(p)) || OP(s) == LABEL)
				goto cont;
		}
		if (s == FIRST)
			continue;
		c_strcpy(REG(RT1), CODE(p));
		c_strcat(REG(RT1), S_COMMA2);
		c_strcat(REG(RT1), CODE(s));
		S_CODE(s, copy1(REG(RT1)));
		S_OP(s, MOV);
		S_USE(s, USE(s) | USE(p));
		S_SET(s, SET(p));
		S_BACK(FORW(p), BACK(p));
		S_FORW(BACK(p), FORW(p));
		INCW(NPOP);
		INCW(NCHANGE);
	cont:
		;
	}
}

/*
 * indexopt() (text 2588): new, after the main loop. "add r,i" - r one of
 * si, di with i bx or bp, or r bx with i si or di - is removed when the
 * code after it, up to a label, a branch reading r, or a node writing r,
 * can use r as an address only: each "...(r)" there becomes "...(r)(i)"
 * (addindex()); not when r is a register variable.
 */
void
indexopt(void)
{
	uint16_t p, s, q;
	int16_t rs, rd, ms, md, flag, a, b;

	regmask();
	for (p = FORW(FIRST); p != 0; p = FORW(p)) {
		if (OP(p) != ADD || regvar(SET(p)))
			continue;
		dualop(p);
		if ((rs = isreg(REG(RT1))) < 0)
			continue;
		if ((rd = isreg(REG(RT2))) < 0)
			continue;
		ms = rbit(rs);
		md = rbit(rd);
		switch ((uint16_t)(rd - 2)) {
		case 0:		/* si */
		case 1:		/* di */
			if (rs != 4 && rs != 6)
				continue;
			break;
		case 2:		/* bx */
			if (rs != 2 && rs != 3)
				continue;
			break;
		default:
			continue;
		}
		flag = 0;
		for (s = FORW(p); s != 0; s = FORW(s)) {
			if (OP(s) == LABEL)
				goto nextp;
			if (USE(s) & md) {
				if (flag)
					goto nextp;
				dualop(s);
				if (isreg(REG(RT2)) == rd) {
					if (OP(s) != MOV)
						goto nextp;
					break;
				}
				if (SET(s) & md)
					goto nextp;
				if (isreg(REG(RT1)) == rd)
					goto nextp;
				if (OP(s) == CBR)
					goto nextp;
			} else if (SET(s) & md)
				break;
			flag = (flag != 0 || (SET(s) & ms) != 0) ? 1 : 0;
		}
		if (s)
			s = FORW(s);
		for (q = FORW(p); q != s; q = FORW(q)) {
			dualop(q);
			a = addindex(REG(RT2), rd, rs);
			b = addindex(REG(RT1), rd, rs);
			if (a | b) {
				if (rb(REG(RT1)))
					c_strcat(REG(RT2), S_COMMA3);
				S_CODE(q, copy2(REG(RT2), REG(RT1)));
				S_USE(q, USE(q) | ms);
			}
		}
		S_BACK(FORW(p), BACK(p));
		S_FORW(BACK(p), FORW(p));
		INCW(NCHANGE);
	nextp:
		;
	}
}

/*
 * addindex() (text 275e): new - "x(from)" in s becomes "x(from)(to)",
 * written after the ")" (the text is then 4 characters longer).
 */
static int16_t
addindex(uint16_t s, int16_t from, int16_t to)
{
	uint16_t q;

	if ((q = c_index(s, '(')) == 0)
		return 0;
	if (isreg((uint16_t)(q + 1)) != from)
		return 0;
	q = c_index(q, ')');
	q++;
	wb(q++, '(');
	convreg(q, to);
	wb((uint16_t)(q + 2), ')');
	wb((uint16_t)(q + 3), 0);
	return 1;
}

/* regmask()'s operand rules: a register read, as itself or in "(r)" */
static void
use_operand(uint16_t p, uint16_t opnd)
{
	uint16_t q;
	int16_t r;

	if ((r = isreg(opnd)) >= 0) {
		S_USE(p, USE(p) | rbit(r));
		return;
	}
	if ((q = c_index(opnd, '(')) != 0
	    && (r = isreg((uint16_t)(q + 1))) >= 0)
		S_USE(p, USE(p) | rbit(r));
}

/* a destination: a register is written (and read when rd), "(r)" read */
static void
dst_operand(uint16_t p, uint16_t opnd, int rd)
{
	uint16_t q;
	int16_t r;

	if ((r = isreg(opnd)) >= 0) {
		S_SET(p, SET(p) | rbit(r));
		if (rd)
			S_USE(p, USE(p) | rbit(r));
		return;
	}
	if ((q = c_index(opnd, '(')) != 0
	    && (r = isreg((uint16_t)(q + 1))) >= 0)
		S_USE(p, USE(p) | rbit(r));
}

/*
 * regmask() (text 27b6): new. For each node the registers it reads
 * (N_USE) and writes (N_SET), from its op and operands; a call, "|RTYP",
 * a branch to "cret" and every op it does not know read or write all
 * (with "cret" and "|RTYP 6" reading ax and dx, "|RTYP n" ax); a write
 * of sp writes all. Then, until nothing changes, a branch reads what is
 * read before it is written along its target's code up to a "br" or
 * "|RTYP" (and a "br" writes all). Returns the last node.
 */
uint16_t
regmask(void)
{
	uint16_t p, q, last;
	int16_t changed, mask, old, r, have = 0;

	last = 0;
	for (p = FORW(FIRST); p != 0; p = FORW(p)) {
		last = p;
		have = 1;
		S_SET(p, 0);
		S_USE(p, 0);
		switch (OP(p)) {

		case AND:
		case OR:
		case ADD:
		case SUB:
		case XOR:
		case ADC:
		case SBB:
			dualop(p);
			dst_operand(p, REG(RT2), 1);
			use_operand(p, REG(RT1));
			break;

		case SAL:
		case SAR:
		case SHR:
			dualop(p);
			if (c_strcmp(REG(RT1), S_CL1) == 0)
				S_USE(p, 0x20);
			dst_operand(p, REG(RT2), 1);
			break;

		case MOV:
		case LEA:
			dualop(p);
			if (c_strcmp(REG(RT2), S_SP2) == 0)
				S_SET(p, 0xff);
			dst_operand(p, REG(RT2), 0);
			use_operand(p, REG(RT1));
			break;

		case TST:
		case CMP:
			dualop(p);
			use_operand(p, REG(RT1));
			use_operand(p, REG(RT2));
			break;

		case XCHG:
			dualop(p);
			dst_operand(p, REG(RT2), 1);
			if ((r = isreg(REG(RT1))) >= 0) {
				S_SET(p, SET(p) | rbit(r));
				S_USE(p, USE(p) | rbit(r));
			} else
				use_operand(p, REG(RT1));
			break;

		case CALL:
			S_SET(p, 0xff);
			/* FALLTHROUGH */
		case JBR:
		case CBR:
		case JMP:
		case PUSH:
			singop(p);
			if (c_strcmp(S_CRET, REG(RT1)) == 0) {
				S_USE(p, 3);
				S_SET(p, 0xff);
				break;
			}
			use_operand(p, REG(RT1));
			break;

		case POP:
			singop(p);
			if ((r = isreg(REG(RT1))) >= 0) {
				S_SET(p, SET(p) | rbit(r));
				break;
			}
			use_operand(p, REG(RT1));
			break;

		case INC:
		case DEC:
		case NEG:
		case NOT:
			singop(p);
			dst_operand(p, REG(RT1), 1);
			break;

		case IMUL:
		case IDIV:
		case DIV:
			singop(p);
			S_SET(p, SET(p) | 3);
			S_USE(p, USE(p) | 1);
			use_operand(p, REG(RT1));
			break;

		case CBW:
			S_USE(p, USE(p) | 1);
			S_SET(p, SET(p) | 1);
			break;

		case CWD:
			S_USE(p, USE(p) | 1);
			S_SET(p, SET(p) | 2);
			break;

		case LOOP:
			S_SET(p, 0x20);
			S_USE(p, 0x20);
			break;

		case RTYP:
			if (c_atoi(CODE(p)) == 6)
				S_USE(p, 3);
			else
				S_USE(p, 1);
			S_SET(p, 0xff);
			break;

		case LABEL:
		case EROU:
		case NREGOP:
		case END:
		case BAR:
		case SEG:
			break;

		default:
			S_USE(p, 0xff);
			S_SET(p, 0xff);
			break;
		}
	}
	do {
		changed = 0;
		for (p = FORW(FIRST); p != 0; p = FORW(p)) {
			if ((OP(p) == JBR || OP(p) == CBR) && REF(p)) {
				mask = -1;
				q = REF(p);
				for (;;) {
					q = FORW(q);
					if (q == 0)
						break;
					old = USE(p);
					S_USE(p, USE(p) | (USE(q) & mask));
					if (USE(p) != old)
						changed = 1;
					mask = (int16_t)(mask & (int16_t)(signed char)
					    ~(SET(q) & ~USE(q)));
					if (OP(q) == JBR || OP(q) == RTYP)
						break;
				}
				if (OP(p) == JBR)
					S_SET(p, 0xff);
			}
		}
	} while (changed);
	if (!have)
		c2trap("regmask() of an empty list returns a word of its stack");
	return last;
}

/*
 * simplify() (text 2d48): new, before shortbr(). "mov r,*0." becomes
 * "sub r,r", "cmp r,*0." "test r,r", and "and ax,#255." (or dx) not
 * before a long conditional branch "subb ah,ah" (dh).
 */
void
simplify(void)
{
	uint16_t p;
	int16_t r;

	for (p = FORW(FIRST); p != 0; p = FORW(p)) {
		if (OP(p) == MOV) {
			dualop(p);
			if (isreg(REG(RT2)) >= 0 && c_strcmp(REG(RT1), S_ZERO1) == 0) {
				S_OP(p, SUB);
				c_strcpy(REG(RT1), REG(RT2));
				c_strcat(REG(RT2), S_COMMA4);
				S_CODE(p, copy2(REG(RT2), REG(RT1)));
			}
		}
		if (OP(p) == CMP) {
			dualop(p);
			if (isreg(REG(RT2)) >= 0 && c_strcmp(REG(RT1), S_ZERO2) == 0) {
				S_OP(p, TST);
				c_strcpy(REG(RT1), REG(RT2));
				c_strcat(REG(RT2), S_COMMA5);
				S_CODE(p, copy2(REG(RT2), REG(RT1)));
			}
		}
		if (OP(p) == AND && OP(FORW(p)) != CBR) {
			dualop(p);
			r = isreg(REG(RT2));
			if (r >= 0 && r <= 1 && c_strcmp(REG(RT1), S_255) == 0) {
				S_SUBOP(p, BYTE);
				wb(REG(RT2) + 1, 'h');
				S_OP(p, SUB);
				c_strcpy(REG(RT1), REG(RT2));
				c_strcat(REG(RT2), S_COMMA6);
				S_CODE(p, copy2(REG(RT2), REG(RT1)));
			}
		}
	}
}

/*
 * renback() (text 2ed0): new. "mov rd,rs" (or "xchg rd,ax") is removed
 * when rs was written by a node before it, back to which nothing reads
 * or writes rd (a node that only writes rd is removed on the way), and
 * rs is not read after it: rs is renamed rd from that node on. Not for
 * register variables, bp, or across a label, a node pinned to rs
 * (pinned()) or a long conditional branch reading rs.
 */
static int16_t
renback(uint16_t p)
{
	uint16_t s, t;
	int16_t rd, rs, found, acc, ms, md;

	if (!((OP(p) == MOV || OP(p) == XCHG) && SUBOP(p) == 0))
		return 0;
	if (USE(p) == 0)
		return 0;
	dualop(p);
	if ((rd = isreg(REG(RT2))) < 0)
		return 0;
	if ((rs = isreg(REG(RT1))) < 0)
		return 0;
	if (OP(p) == XCHG && rs != 0)
		return 0;
	if (regvar(USE(p)))
		return 0;
	ms = rbit(rs);
	md = rbit(rd);
	found = 0;
	acc = 0;
	for (s = BACK(p); s != FIRST; s = BACK(s)) {
		if (pinned(s, rs, rd))
			break;
		if ((SET(s) & ms) && !(USE(s) & ms)) {
			found = 1;
			break;
		}
		if (USE(s) & md)
			break;
		if (OP(s) == LABEL)
			break;
		if ((USE(s) & ms) && OP(s) == CBR)
			break;
		if (SET(s) & md) {
			if (UB(s + N_SET) != (uint16_t)md)
				break;
			if (OP(s) == XCHG)
				break;
			unlink_node(s);
			INCW(NCHANGE);
		} else
			acc = (int16_t)(acc | (USE(s) & ms));
	}
	if (!found)
		return 0;
	if (OP(p) != XCHG)
		for (t = FORW(p); t != 0; t = FORW(t)) {
			if (USE(t) & ms)
				return 0;
			if (SET(t) & ms)
				break;
			if (OP(t) == JBR)
				break;
		}
	while (s != p) {
		renreg(s, rs, rd);
		s = FORW(s);
	}
	unlink_node(p);
	if (OP(p) == MOV && acc)
		regmask();
	INCW(REDUNM);
	INCW(NCHANGE);
	return 1;
}

/*
 * pinned() (text 3074): new - can register "from" not be renamed "to" in
 * node s: bp either way; si or di into a byte instruction; never between
 * two of si, di, bx; else ax and dx for imul, idiv, div, cwd and "br",
 * ax for cbw, cx for loop, cx for a shift by cl, always for a call, and
 * "from" in an address "(from)". The switch on the op is hashed (op % 2,
 * then op / 2).
 */
static int16_t
pinned(uint16_t s, int16_t from, int16_t to)
{
	uint16_t v, q;

	if (to == 6 || from == 6)
		return 1;
	if (SUBOP(s) != 0 && (to == 2 || to == 3))
		return 1;
	if ((from == 2 || from == 3 || from == 4)
	    && (to == 2 || to == 3 || to == 4))
		return 0;
	v = (uint16_t)OP(s);
	if (v % 2 == 0) {
		switch (v / 2) {
		case IMUL / 2:
		case DIV / 2:
			goto axdx;
		case SAL / 2:
		case SHR / 2:
			goto shift;
		case CBW / 2:
			return from == 0;
		case LOOP / 2:
			return from == 5;
		default:
			goto other;
		}
	} else {
		switch (v / 2) {
		case JBR / 2:
		case CWD / 2:
		case IDIV / 2:
			goto axdx;
		case CALL / 2:
			return 1;
		case SAR / 2:
			goto shift;
		default:
			goto other;
		}
	}
axdx:
	return from == 0 || from == 1;
shift:
	dualop(s);
	if (c_strcmp(REG(RT1), S_CL2) == 0 && from == 5)
		return 1;
	if ((q = c_index(REG(RT2), '(')) != 0 && isreg((uint16_t)(q + 1)) == from)
		return 1;
	return 0;
other:
	dualop(s);
	if ((q = c_index(REG(RT1), '(')) != 0 && isreg((uint16_t)(q + 1)) == from)
		return 1;
	if ((q = c_index(REG(RT2), '(')) != 0 && isreg((uint16_t)(q + 1)) == from)
		return 1;
	return 0;
}

/*
 * renreg() (text 31f2): new - register "from" becomes "to" in node s: in
 * its masks, and in its text where a register name starts (at its start,
 * or after "(" or ","), from regnames[].
 */
static void
renreg(uint16_t s, int16_t from, int16_t to)
{
	uint16_t q, t;
	int16_t mf, mt;

	if (USE(s) & rbit(from)) {
		mt = rbit(to);
		mf = rbit(from);
		S_USE(s, (USE(s) & ~mf) | mt);
	}
	if (SET(s) & rbit(from)) {
		mt = rbit(to);
		mf = rbit(from);
		S_SET(s, (SET(s) & ~mf) | mt);
	}
	q = CODE(s);
	while (rb(q) != 0) {
		if (isreg(q) == from) {
			t = rw((uint16_t)(REGNAMES + 2 * to));
			while (rb(t) != 0) {
				wb(q, rb(t));
				q++;
				t++;
			}
		} else {
			while (rb(q) != 0 && rb(q) != '(' && rb(q) != ',')
				q++;
			if (rb(q) != 0)
				q++;
		}
	}
}

/*
 * renfwd() (text 32ae): new, when renback() did not apply. "mov rd,rs"
 * is removed when the code after it can read rs where it reads rd: up to
 * where rd or rs is written again (or a "br" after rd's), rd is renamed
 * rs - not across a node pinned to rd (pinned()), a label after which rd
 * is read, a long conditional branch reading rd, or for register
 * variables.
 */
static int16_t
renfwd(uint16_t p)
{
	uint16_t s, end;
	int16_t rs, rd, lab, a, b;

	if (OP(p) != MOV || USE(p) == 0)
		return 0;
	dualop(p);
	if ((rs = isreg(REG(RT1))) < 0)
		return 0;
	if ((rd = isreg(REG(RT2))) < 0)
		return 0;
	if (regvar(SET(p)))
		return 0;
	for (s = FORW(p); s != 0; s = FORW(s)) {
		if (SET(s) & USE(p))
			return 0;
		if (USE(s) & SET(p))
			break;
		if (OP(s) == LABEL)
			return 0;
	}
	if (s == 0)
		return 0;
	lab = 0;
	a = b = 0;
	end = 0;
	while (b >= 0 || a >= 0) {
		if (s == 0)
			break;
		if (USE(s) & USE(p)) {
			if (a)
				return 0;
			if (!b)
				b = (int16_t)(signed char)(SET(s) & USE(p));
		} else if (SET(s) & USE(p)) {
			b = -1;
			if (!end)
				end = s;
		}
		if (USE(s) & SET(p)) {
			if (b)
				return 0;
			if (!a) {
				a = (int16_t)(signed char)(SET(s) & SET(p));
				if (a && regvar(USE(p)))
					return 0;
			}
		} else if (SET(s) & SET(p)) {
			a = -1;
			if (!end)
				end = s;
		}
		if (!lab)
			lab = OP(s) == LABEL;
		else if (a >= 0 && (USE(s) & SET(p)))
			return 0;
		if (!end && pinned(s, rd, rs))
			return 0;
		if (OP(s) == JBR) {
			if (a == -1)
				break;
			return 0;
		}
		if (OP(s) == CBR && (USE(s) & SET(p)))
			return 0;
		s = FORW(s);
	}
	for (s = FORW(p); s != end; s = FORW(s))
		renreg(s, rd, rs);
	unlink_node(p);
	INCW(REDUNM);
	INCW(NCHANGE);
	return 1;
}
