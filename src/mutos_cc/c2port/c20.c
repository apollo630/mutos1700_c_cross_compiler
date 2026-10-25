/*
 * c20.c - the original MUTOS 1700 c2's functions from main() to
 * backjmp(), in the binary's order (tests/realbin/c2map.txt): v7's
 * c20.c, without reducelit() (gone in the binary). Each function's
 * comment gives its text offset and what the binary does differently from
 * v7; where nothing is said, it is v7's code. The data are in the memory
 * image (c2.h); the locals are host variables of the original's widths.
 *
 * The MUTOS deltas here, in short: the opcodes are the 8086's in the
 * MUTOS assembler's spelling, among them the short branches "j" and "je"
 * .. "jae", which input() reads as JBR and CBR and shortbr() writes back;
 * "jmp" is read as "br"; "loop" is a branch too; getline() drops an
 * empty line after a line; a branch takes over the code (a non-"L"
 * target) of the branch or label it is redirected to; "ret" ends a block
 * as "br" does; .bss stops the deletion of unreachable code; the least
 * "|NREG n" of a chunk limits the register passes; main()'s loop runs
 * pushopt() after rmove(), and indexopt(), simplify() and shortbr()
 * before output().
 */
#include <stddef.h>

#include "c2.h"

static	int16_t	input(void);
static	int16_t	getline(void);
static	int16_t	getnum(uint16_t ap);
static	void	output(void);
static	void	opsetup(void);
static	int16_t	oplook(void);
static	void	refcount(void);
static	void	iterate(void);
static	void	xjump(uint16_t p1);
static	uint16_t codemove(uint16_t p);
static	void	comjump(void);
static	void	backjmp(uint16_t ap1, uint16_t ap2);

/* the 8086's "sar": an arithmetic shift of a word */
static int16_t
sar16(uint16_t v, int n)
{
	return (int16_t)((int32_t)(int16_t)v >> n);
}

/* printf() with no argument and with one */
static void
pf0(uint16_t fmt)
{
	c_printf(fmt, NULL, 0);
}

static void
pf1(uint16_t fmt, uint16_t a)
{
	c_printf(fmt, &a, 1);
}

static void
epf1(uint16_t fmt, uint16_t a)
{
	c_fprintf(STDERR, fmt, &a, 1);
}

/* the hash of opsetup() and oplook(), on signed chars, in 16 bits */
static uint16_t
ophash(int16_t c0, int16_t c1, int16_t c2)
{
	uint16_t h = (uint16_t)((uint16_t)((uint16_t)c0 << 3)
	    + (uint16_t)((uint16_t)c1 << 1) + (uint16_t)c2);

	return (uint16_t)(OPHASH + 2 * ((h & 077777) % OPHS));
}

/*
 * main() (text 00b6): v7's. Its loop also runs pushopt() after rmove(),
 * then indexopt(), simplify() and shortbr() before output() (v7: addsob());
 * nreg is set to 3 for each chunk (refcount() lowers it to the least
 * "|NREG n"). The statistics of "-": v7's without "sob's added", with
 * "mov/push converted to push" (never counted, C2-02), "pop/push
 * converted to mov", "ors -> tests" and "long branches to short".
 */
int16_t
c2main(int16_t argc, uint16_t argv)
{
	int16_t niter, maxiter, isend, nflag;
	uint16_t v;

	if (argc > 1 && rb(rw((uint16_t)(argv + 2))) == '+') {
		argc--;
		argv = (uint16_t)(argv + 2);
		INCW(DEBUG);
	}
	nflag = 0;
	if (argc > 1 && rb(rw((uint16_t)(argv + 2))) == '-') {
		argc--;
		argv = (uint16_t)(argv + 2);
		nflag++;
	}
	if (argc > 1) {
		if (c_freopen(rw((uint16_t)(argv + 2)), S_RMODE, STDIN) == 0) {
			epf1(S_CANTFIND, rw((uint16_t)(argv + 2)));
			c2_exit(1);
		}
	}
	if (argc > 2) {
		if (c_freopen(rw((uint16_t)(argv + 4)), S_WMODE, STDOUT) == 0) {
			epf1(S_CANTCREATE, rw((uint16_t)(argv + 4)));
			c2_exit(1);
		}
	}
	v = (uint16_t)c2_sbrk(2);
	ww(LASTR, v);
	ww(FIRSTR, v);
	ww(LASTA, v);
	maxiter = 0;
	opsetup();
	do {
		isend = input();
		movedat();
		niter = 0;
		ww(NREG, 3);
		do {
			refcount();
			do {
				iterate();
				clearreg();
				niter++;
			} while (rw(NCHANGE));
			comjump();
			rmove();
			pushopt();
		} while (rw(NCHANGE) || jumpsw());
		indexopt();
		simplify();
		shortbr();
		output();
		if (niter > maxiter)
			maxiter = niter;
		ww(LASTA, rw(FIRSTR));
	} while (isend);
	if (nflag) {
		epf1(S_ITER, (uint16_t)maxiter);
		epf1(S_JJ, rw(NBRBR));
		epf1(S_IAJ, rw(IAFTBR));
		epf1(S_JP2, rw(NJP1));
		epf1(S_RLAB, rw(NRLAB));
		epf1(S_XJ, rw(NXJUMP));
		epf1(S_CMOT, rw(NCMOT));
		epf1(S_REVBR, rw(NREVBR));
		epf1(S_RMOV, rw(REDUNM));
		epf1(S_SADDR, rw(NSADDR));
		epf1(S_LOOPIV, rw(LOOPIV));
		epf1(S_RJ, rw(NREDUNJ));
		epf1(S_COMJ, rw(NCOMJ));
		epf1(S_SKIP, rw(NSKIP));
		epf1(S_RTST, rw(NRTST));
		epf1(S_LIT, rw(NLIT));
		epf1(S_PUSH, rw(NPUSH));
		epf1(S_POP, rw(NPOP));
		epf1(S_ORS, rw(NORS));
		epf1(S_SHORT, rw(NSHORT));
		epf1(S_CORE, (uint16_t)(sar16((uint16_t)(rw(LASTR) + 01777), 10)
		    & 077));
	}
	c2_exit(0);
}

/*
 * input() (text 037e): v7's, with the MUTOS ops: "j" (SJBR) is read as
 * JBR and "je" .. "jae" (SCBR) as CBR with their condition, "jmp" (JMP)
 * as JBR, "loop" is a branch. A node's refc and register masks are not
 * set: they are what the memory held (the arena is reused for each
 * chunk).
 */
static int16_t
input(void)
{
	uint16_t p, lastp, curlp;
	int16_t oper, n;

	lastp = FIRST;
	for (;;) {
		oper = getline();
		switch (oper & 0377) {

		case LABEL:
			p = alloc(NODESZ);
			if (rb(LINE) == 'L') {
				S_OP(p, LABEL);
				S_SUBOP(p, 0);
				S_LABNO(p, getnum(LINE + 1));
				S_CODE(p, 0);
			} else {
				S_OP(p, DLABEL);
				S_SUBOP(p, 0);
				S_LABNO(p, 0);
				S_CODE(p, copy1(LINE));
			}
			break;

		case SCBR:
			oper = (int16_t)((oper & 0xff00) | CBR);
			goto jump;

		case SJBR:
			oper = (int16_t)((oper & 0xff00) | JBR);
			/* FALLTHROUGH */
		case JBR:
		case CBR:
		case JMP:
		case JSW:
		case LOOP:
		jump:
			p = alloc(NODESZ);
			S_OP(p, oper & 0377);
			if (OP(p) == JMP)
				S_OP(p, JBR);
			S_SUBOP(p, sar16((uint16_t)oper, 8));
			curlp = rw(CURLP);
			if (rb(curlp) == 'L') {
				n = getnum((uint16_t)(curlp + 1));
				S_LABNO(p, n);
				if (n != 0) {
					S_CODE(p, 0);
					break;
				}
			}
			S_LABNO(p, 0);
			S_CODE(p, copy1(rw(CURLP)));
			break;

		default:
			p = alloc(NODESZ);
			S_OP(p, oper & 0377);
			S_SUBOP(p, sar16((uint16_t)oper, 8));
			S_LABNO(p, 0);
			S_CODE(p, copy1(rw(CURLP)));
			break;
		}
		S_FORW(p, 0);
		S_BACK(p, lastp);
		S_FORW(lastp, p);
		lastp = p;
		S_REF(p, 0);
		if (oper == EROU)
			return 1;
		if (oper == END)
			return 0;
	}
}

/*
 * getline() (text 04e6): v7's, but a linefeed is followed by a look at
 * the next character, which is given back unless it is a linefeed too:
 * an empty line after a line is dropped.
 */
static int16_t
getline(void)
{
	uint16_t lp = LINE;
	int16_t c;

	while ((c = c_getc()) == ' ' || c == '\t')
		;
	do {
		if (c == ':') {
			wb(lp++, 0);
			return LABEL;
		}
		if (c == '\n') {
			wb(lp++, 0);
			c = c_getc();
			if (c != '\n')
				c_ungetc(c, STDIN);
			return oplook();
		}
		if (lp >= LINELIM) {
			c_fprintf(STDERR, S_TOOLONG, NULL, 0);
			c2_exit(1);
		}
		wb(lp++, c);
	} while ((c = c_getc()) != -1);
	wb(lp++, 0);
	return END;
}

/* getnum() (text 05c2) */
static int16_t
getnum(uint16_t ap)
{
	uint16_t p = ap;
	int16_t n = 0, c;

	while ((c = rb(p++)) >= '0' && c <= '9')
		n = (int16_t)(uint16_t)((uint16_t)(n * 10) + (uint16_t)c - '0');
	if (rb(--p) != 0)
		return 0;
	return n;
}

/*
 * output() (text 0604): v7's without reducelit() and SOB; the short
 * branches (SJBR, SCBR) and "loop" are printed with their label like
 * JBR and CBR.
 */
static void
output(void)
{
	uint16_t t, oper;
	int16_t byte, op;

	t = FIRST;
	while ((t = FORW(t)) != 0) {
		switch (OP(t)) {

		case END:
			return;

		case LABEL:
			pf1(S_LABEL, (uint16_t)LABNO(t));
			continue;

		case DLABEL:
			pf1(S_DLABEL, CODE(t));
			continue;

		case TEXT:
		case DATA:
		case BSS:
			ww(LASTSEG, (uint16_t)OP(t));
			/* FALLTHROUGH */
		default:
			if ((byte = SUBOP(t)) == BYTE)
				S_SUBOP(t, 0);
			for (oper = OPTAB; rw(oper) != 0; oper = (uint16_t)(oper + 4))
				if ((int16_t)(rw((uint16_t)(oper + 2)) & 0377) == OP(t)
				    && sar16(rw((uint16_t)(oper + 2)), 8) == SUBOP(t)) {
					pf1(S_OPNAME, rw(oper));
					if (byte == BYTE)
						pf0(S_B);
					break;
				}
			op = OP(t);
			if (CODE(t))
				pf1(S_TABCODE, CODE(t));
			else if (op == JBR || OP(t) == CBR || OP(t) == SCBR
			    || OP(t) == SJBR || OP(t) == LOOP)
				pf1(S_TABL, (uint16_t)LABNO(t));
			else
				pf0(S_NL1);
			continue;

		case JSW:
			pf1(S_JSW, (uint16_t)LABNO(t));
			continue;

		case 0:
			if (CODE(t))
				pf1(S_CODE0, CODE(t));
			pf0(S_NL0);
			continue;
		}
	}
}

/*
 * copy() (text 0728): v7's; the port's copy1() and copy2() are its calls
 * with one string and with two. As in v7, an empty first string gives 0,
 * whatever the second.
 */
static uint16_t
copyn(int na, uint16_t ap, uint16_t ap2)
{
	uint16_t p, np, onp;
	int16_t n;

	p = ap;
	n = 0;
	if (rb(p) == 0)
		return 0;
	do
		n++;
	while (rb(p++) != 0);
	if (na > 1) {
		p = ap2;
		while (rb(p++) != 0)
			n++;
	}
	onp = np = alloc(n);
	p = ap;
	for (;;) {
		int16_t c = rb(p++);

		wb(np++, c);
		if (c == 0)
			break;
	}
	if (na > 1) {
		p = ap2;
		np--;
		for (;;) {
			int16_t c = rb(p++);

			wb(np++, c);
			if (c == 0)
				break;
		}
	}
	return onp;
}

uint16_t
copy1(uint16_t ap)
{
	return copyn(1, ap, 0);
}

uint16_t
copy2(uint16_t ap, uint16_t ap2)
{
	return copyn(2, ap, ap2);
}

/*
 * opsetup() (text 07a0): v7's with OPHS 79; the third character counts
 * only if the second is not the end (for "j"); the probe wraps at
 * &ophash[OPHS] (v7: past it).
 */
static void
opsetup(void)
{
	uint16_t optp, ophp, p, old;

	for (optp = OPTAB; (p = rw(optp)) != 0; optp = (uint16_t)(optp + 4)) {
		ophp = ophash(rb(p), rb((uint16_t)(p + 1)),
		    rb((uint16_t)(p + 1)) ? rb((uint16_t)(p + 2)) : 0);
		for (;;) {
			old = ophp;
			ophp = (uint16_t)(ophp + 2);
			if (rw(old) == 0)
				break;
			if (ophp >= OPHEND)
				ophp = OPHASH;
		}
		ophp = (uint16_t)(ophp - 2);
		ww(ophp, optp);
	}
}

/*
 * oplook() (text 081a): v7's; tmpop[2] is cleared first, so that a
 * one-character opcode hashes as opsetup() hashes it. The first token is
 * copied into tmpop[32] without a check (C2-03).
 */
static int16_t
oplook(void)
{
	uint16_t optp, lp, np, ophp;

	if (rb(LINE) == 0) {
		ww(CURLP, LINE);
		return 0;
	}
	wb(TMPOP + 2, 0);
	np = TMPOP;
	for (lp = LINE; rb(lp) != 0 && rb(lp) != ' ' && rb(lp) != '\t'; )
		wb(np++, rb(lp++));
	wb(np++, 0);
	while (rb(lp) == '\t' || rb(lp) == ' ')
		lp++;
	ww(CURLP, lp);
	ophp = ophash(rb(TMPOP), rb(TMPOP + 1), rb(TMPOP + 2));
	while ((optp = rw(ophp)) != 0) {
		np = rw(optp);
		lp = TMPOP;
		for (;;) {
			int16_t c = rb(np++);

			if (c != rb(lp))
				break;
			if (rb(lp++) == 0)
				return RI(optp + 2);
		}
		if (rb(lp++) == 'b' && rb(lp++) == 0 && rb(--np) == 0)
			return (int16_t)(rw((uint16_t)(optp + 2)) + (BYTE << 8));
		ophp = (uint16_t)(ophp + 2);
		if (ophp >= OPHEND)
			ophp = OPHASH;
	}
	if (rb(LINE) == 'L') {
		lp = LINE + 1;
		while (rb(lp) != 0)
			if (rb(lp) < '0' || rb(lp++) > '9')
				return 0;
		ww(CURLP, LINE);
		return JSW;
	}
	ww(CURLP, LINE);
	return 0;
}

/* labhash[labno % LABHS]: a negative labno indexes below the table */
static uint16_t
labslot(int16_t labno)
{
	return (uint16_t)(LABHASH + 2 * (labno % LABHS));
}

/*
 * refcount() (text 093e): v7's, with "loop" a branch; a "|NREG n" lowers
 * nreg to n (atoi() of its operand - of address 0 if it has none); a
 * branch redirected to the last label of a row takes that label's code
 * too.
 */
static void
refcount(void)
{
	uint16_t p, lp, hp, tp;

	for (hp = LABHASH; hp < LABHEND; hp = (uint16_t)(hp + 2))
		ww(hp, 0);
	for (p = FORW(FIRST); p != 0; p = FORW(p))
		if (OP(p) == LABEL) {
			ww(labslot(LABNO(p)), p);
			S_REFC(p, 0);
		}
	for (p = FORW(FIRST); p != 0; p = FORW(p)) {
		if (OP(p) == NREGOP) {
			if (c_atoi(CODE(p)) < RI(NREG))
				ww(NREG, (uint16_t)c_atoi(CODE(p)));
		} else if (OP(p) == JBR || OP(p) == CBR || OP(p) == JSW
		    || OP(p) == LOOP) {
			S_REF(p, 0);
			lp = rw(labslot(LABNO(p)));
			if (lp == 0 || LABNO(p) != LABNO(lp))
				for (lp = FORW(FIRST); lp != 0; lp = FORW(lp))
					if (OP(lp) == LABEL && LABNO(p) == LABNO(lp))
						break;
			if (lp) {
				tp = BACK(nonlab(lp));
				if (tp != lp) {
					S_LABNO(p, LABNO(tp));
					S_CODE(p, CODE(tp));
					lp = tp;
				}
				S_REF(p, lp);
				S_REFC(lp, REFC(lp) + 1);
			}
		}
	}
	for (p = FORW(FIRST); p != 0; p = FORW(p))
		if (OP(p) == LABEL && REFC(p) == 0
		    && OP(lp = nonlab(p)) != 0 && OP(lp) != JSW)
			decref(p);
}

/*
 * iterate() (text 0a7a): v7's, with "loop" a branch, "ret" ending a block
 * as JBR and JMP do, .bss stopping the deletion of the code after them
 * (with v7's .data, labels, .globl, .end and empty lines); a redirected
 * branch takes the code of its new target too. An empty line's node
 * (op 0, no code) equals the list's head in equop(): xjump() then puts a
 * label before the head and can leave the list in a circle, and the
 * deletion after a branch never ends (C2-07): refused once it has deleted
 * more nodes than the data segment can hold.
 */
static void
iterate(void)
{
	uint16_t p, rp, p1, f;

	ww(NCHANGE, 0);
	for (p = FORW(FIRST); p != 0; p = FORW(p)) {
		if ((OP(p) == JBR || OP(p) == CBR || OP(p) == JSW || OP(p) == LOOP)
		    && REF(p)) {
			rp = nonlab(REF(p));
			if (OP(rp) == JBR && LABNO(rp) && LABNO(p) != LABNO(rp)) {
				INCW(NBRBR);
				S_LABNO(p, LABNO(rp));
				S_CODE(p, CODE(rp));
				decref(REF(p));
				S_REFC(REF(rp), REFC(REF(rp)) + 1);
				S_REF(p, REF(rp));
				INCW(NCHANGE);
			}
		}
		if (OP(p) == CBR && OP(p1 = FORW(p)) == JBR) {
			rp = REF(p);
			do
				rp = BACK(rp);
			while (OP(rp) == LABEL);
			if (rp == p1) {
				decref(REF(p));
				S_REF(p, REF(p1));
				S_LABNO(p, LABNO(p1));
				S_CODE(p, CODE(p1));
				S_BACK(FORW(p1), p);
				S_FORW(p, FORW(p1));
				S_SUBOP(p, rb((uint16_t)(REVBR + SUBOP(p))));
				INCW(NCHANGE);
				INCW(NSKIP);
			}
		}
		if (OP(p) == JBR || OP(p) == JMP || OP(p) == RET) {
			int32_t ndel = 0;

			while (FORW(p) && OP(f = FORW(p)) != LABEL && OP(f) != DLABEL
			    && OP(f) != EROU && OP(f) != END && OP(f) != 0
			    && OP(f) != DATA && OP(f) != BSS) {
				if (++ndel > 65536)
					c2trap("iterate(): more nodes deleted after a branch "
					    "than the data segment holds - the list runs "
					    "in a circle, the original deletes for ever "
					    "(C2-07)");
				INCW(NCHANGE);
				INCW(IAFTBR);
				if (REF(FORW(p)))
					decref(REF(FORW(p)));
				S_FORW(p, FORW(FORW(p)));
				S_BACK(FORW(p), p);
			}
			rp = FORW(p);
			while (rp && OP(rp) == LABEL) {
				if (REF(p) == rp) {
					S_FORW(BACK(p), FORW(p));
					S_BACK(FORW(p), BACK(p));
					p = BACK(p);
					decref(rp);
					INCW(NCHANGE);
					INCW(NJP1);
					break;
				}
				rp = FORW(rp);
			}
		}
		if (OP(p) == JBR || OP(p) == JMP) {
			xjump(p);
			p = codemove(p);
		}
	}
}

/* xjump() (text 0c3c) */
static void
xjump(uint16_t p1)
{
	uint16_t p2, p3;

	if ((p2 = REF(p1)) == 0)
		return;
	for (;;) {
		while ((p1 = BACK(p1)) != 0 && OP(p1) == LABEL)
			;
		while ((p2 = BACK(p2)) != 0 && OP(p2) == LABEL)
			;
		if (!equop(p1, p2) || p1 == p2)
			return;
		p3 = insertl(p2);
		S_OP(p1, JBR);
		S_SUBOP(p1, 0);
		S_REF(p1, p3);
		S_LABNO(p1, LABNO(p3));
		S_CODE(p1, 0);
		INCW(NXJUMP);
		INCW(NCHANGE);
	}
}

/* insertl() (text 0cae) */
uint16_t
insertl(uint16_t oldp)
{
	uint16_t lp;

	if (OP(oldp) == LABEL) {
		S_REFC(oldp, REFC(oldp) + 1);
		return oldp;
	}
	if (OP(BACK(oldp)) == LABEL) {
		oldp = BACK(oldp);
		S_REFC(oldp, REFC(oldp) + 1);
		return oldp;
	}
	lp = alloc(NODESZ);
	S_OP(lp, LABEL);
	S_SUBOP(lp, 0);
	S_LABNO(lp, rw(ISN));
	INCW(ISN);
	S_REF(lp, 0);
	S_CODE(lp, 0);
	S_REFC(lp, 1);
	S_BACK(lp, BACK(oldp));
	S_FORW(lp, oldp);
	S_FORW(BACK(oldp), lp);
	S_BACK(oldp, lp);
	return lp;
}

/*
 * codemove() (text 0d14): v7's; a "ret" before the target ends a block
 * as JBR and JMP do (the moved block itself must still end in one of
 * those two); the inverted loop's branch takes the new label's code.
 */
static uint16_t
codemove(uint16_t p)
{
	uint16_t p1, p2, p3, t, tl;
	int16_t n;

	p1 = p;
	if (OP(p1) != JBR || (p2 = REF(p1)) == 0)
		return p1;
	while (OP(p2) == LABEL)
		if ((p2 = BACK(p2)) == 0)
			return p1;
	if (OP(p2) != JBR && OP(p2) != JMP && OP(p2) != RET)
		goto ivloop;
	p2 = FORW(p2);
	p3 = REF(p1);
	while (p3) {
		if (OP(p3) == JBR || OP(p3) == JMP) {
			if (p1 == p3)
				return p1;
			INCW(NCMOT);
			INCW(NCHANGE);
			S_FORW(BACK(p1), p2);
			S_BACK(FORW(p1), p3);
			S_FORW(BACK(p2), FORW(p3));
			S_BACK(FORW(p3), BACK(p2));
			S_BACK(p2, BACK(p1));
			S_FORW(p3, FORW(p1));
			decref(REF(p1));
			return p2;
		} else
			p3 = FORW(p3);
	}
	return p1;
ivloop:
	if (OP(FORW(p1)) != LABEL)
		return p1;
	p3 = p2 = FORW(p2);
	n = 16;
	do {
		if ((p3 = FORW(p3)) == 0 || p3 == p1 || --n == 0)
			return p1;
	} while (OP(p3) != CBR || LABNO(p3) != LABNO(FORW(p1)));
	do
		if ((p1 = BACK(p1)) == 0)
			return p;
	while (p1 != p3);
	p1 = p;
	tl = insertl(p1);
	S_SUBOP(p3, rb((uint16_t)(REVBR + SUBOP(p3))));
	decref(REF(p3));
	S_FORW(BACK(p2), p1);
	S_BACK(FORW(p3), p1);
	S_FORW(BACK(p1), p2);
	S_BACK(FORW(p1), p3);
	t = BACK(p1);
	S_BACK(p1, BACK(p2));
	S_BACK(p2, t);
	t = FORW(p1);
	S_FORW(p1, FORW(p3));
	S_FORW(p3, t);
	p2 = insertl(FORW(p1));
	S_LABNO(p3, LABNO(p2));
	S_CODE(p3, CODE(p2));
	S_REF(p3, p2);
	decref(tl);
	if (REFC(tl) <= 0)
		DECW(NRLAB);
	INCW(LOOPIV);
	INCW(NCHANGE);
	return p3;
}

/* comjump() (text 0ee6) */
static void
comjump(void)
{
	uint16_t p1, p2, p3;

	for (p1 = FORW(FIRST); p1 != 0; p1 = FORW(p1))
		if (OP(p1) == JBR && (p2 = REF(p1)) != 0 && REFC(p2) > 1)
			for (p3 = FORW(p1); p3 != 0; p3 = FORW(p3))
				if (OP(p3) == JBR && REF(p3) == p2)
					backjmp(p1, p3);
}

/* backjmp() (text 0f40): v7's; the redirected branch takes the code too */
static void
backjmp(uint16_t ap1, uint16_t ap2)
{
	uint16_t p1, p2, p3;

	p1 = ap1;
	p2 = ap2;
	for (;;) {
		while ((p1 = BACK(p1)) != 0 && OP(p1) == LABEL)
			;
		p2 = BACK(p2);
		if (equop(p1, p2)) {
			p3 = insertl(p1);
			S_FORW(BACK(p2), FORW(p2));
			S_BACK(FORW(p2), BACK(p2));
			p2 = FORW(p2);
			decref(REF(p2));
			S_LABNO(p2, LABNO(p3));
			S_CODE(p2, CODE(p3));
			S_REF(p2, p3);
			INCW(NCHANGE);
			INCW(NCOMJ);
		} else
			return;
	}
}
