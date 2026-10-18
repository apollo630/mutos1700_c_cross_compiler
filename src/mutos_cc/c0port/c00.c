/*
 * c00.c - main, the lexer and the expression parser: v7/cc/c00.c as the
 * original MUTOS 1700 c0 has it (text 00b6..140e, tests/realbin/
 * c0map.txt). Every function here does what v7's does; the MUTOS values
 * (HSHSIZ 500) come from c0.h.
 *
 * Called the way cc calls it:
 *   c0 source temp1 temp2 [ profileflag ]
 * temp1 gets most of the intermediate code; strings are put on temp2,
 * which c1 reads after temp1.
 */
#include "c0.h"

#include <stdlib.h>

extern	FILE	*c0_in;

/*
 * 00b6 main - same. The source becomes stdin, temp1 stdout, temp2
 * sbufp; the keywords' hash slots are marked; then extdef() to the end
 * of the file and two EOFC, one in each temp file.
 */
int
main(int argc, char **argv)
{
	uint16_t sp;
	int16_t i;
	uint16_t ip;

	c0_init();
	if (argc < 4) {
		error("Arg count");
		c0_exit(1);
	}
	if ((c0_in = fopen(argv[1], "r")) == NULL) {
		/* the original prints argv[1] from its own stack: here a copy */
		uint16_t a = salloc((uint16_t)64);
		int k;

		for (k = 0; k < 63 && argv[1][k]; k++)
			B(a + k) = argv[1][k];
		B(a + k) = 0;
		error("Can't find %s", a);
		c0_exit(1);
	}
	if ((c0_temp1 = fopen(argv[2], "wb")) == NULL
	 || (c0_temp2 = fopen(argv[3], "wb")) == NULL) {
		error("Can't create temp");
		c0_exit(1);
	}
	if (argc > 4)
		proflg++;
	/*
	 * The hash table locations of the keywords
	 * are marked; if an identifier hashes to one of
	 * these locations, it is looked up in in the keyword
	 * table first.
	 */
	for (ip = KWTAB; (sp = WU(ip)) != 0; ip += 4) {
		i = 0;
		while (B(sp))
			i = (int16_t)(i + B(sp++));
		HFLAG(HSHTAB + (i % HSHSIZ) * SZ_HSHTAB) = FKEYW;
	}
	coremax = funcbase = curbase = (uint16_t)c0_sbrk(0);
	while (!eof)
		extdef();
	outcode("B", EOFC);
	strflg++;
	outcode("B", EOFC);
	c0_exit(nerror != 0);
	return 0;
}

/*
 * 01f8 lookup - same. Look up the identifier in symbuf in the symbol
 * table. If it hashes to the same spot as a keyword, try the keyword
 * table first. Return is KEYW or NAME (the entry in csym).
 */
int16_t
lookup(void)
{
	int16_t ihash;
	uint16_t rp;
	uint16_t sp, np;

	ihash = 0;
	sp = SYMBUF;
	while (sp < SYMBUF + NCPS)
		ihash = (int16_t)(ihash + (B(sp++) & 0177));
	rp = (uint16_t)(HSHTAB + (ihash % HSHSIZ) * SZ_HSHTAB);
	if (HFLAG(rp) & FKEYW)
		if (findkw())
			return KEYW;
	while (B(np = HNAME(rp))) {
		for (sp = SYMBUF; sp < SYMBUF + NCPS;)
			if (B(np++) != B(sp++))
				goto no;
		if (mossym != (HFLAG(rp) & FMOS))
			goto no;
		csym = rp;
		return NAME;
	no:
		if ((rp += SZ_HSHTAB) >= HSHEND)
			rp = HSHTAB;
	}
	if (++hshused >= HSHSIZ) {
		error("Symbol table overflow");
		c0_exit(1);
	}
	HCLASS(rp) = 0;
	HTYP(rp) = 0;
	HOFFSET(rp) = 0;
	HSUBSP(rp) = 0;
	HSTRP(rp) = 0;
	HPDOWN(rp) = 0;
	HBLKLEV(rp) = (signed char)blklev;
	HFLAG(rp) = (signed char)(HFLAG(rp) | mossym);
	sp = SYMBUF;
	for (np = HNAME(rp); sp < SYMBUF + NCPS;)
		B(np++) = B(sp++);
	csym = rp;
	return NAME;
}

/*
 * 0300 findkw - same. Search the keyword table.
 */
int16_t
findkw(void)
{
	uint16_t kp;
	uint16_t p1, p2;
	uint16_t wp;
	int16_t firstc;

	wp = SYMBUF;
	firstc = B(wp);
	for (kp = KWTAB; (p2 = WU(kp)) != 0; kp += 4) {
		p1 = wp;
		while (B(p1) == B(p2++))
			if (B(p1++) == '\0') {
				cval = WI(kp + 2);
				return 1;
			}
	}
	B(wp) = (signed char)firstc;
	return 0;
}

/*
 * 035e symbol - same (a switch hashed on ctab[c] % 5 in the binary).
 * Return the next symbol from the input. peeksym is a pushed-back
 * symbol, peekc is a pushed-back character (after peeksym). mosflg means
 * that the next symbol, if an identifier, is a member of structure or a
 * structure tag.
 */
int16_t
symbol(void)
{
	int16_t c;
	uint16_t sp;
	int16_t tline;

	if (peeksym >= 0) {
		c = peeksym;
		peeksym = -1;
		if (c == NAME)
			mosflg = 0;
		return c;
	}
	if (peekc) {
		c = peekc;
		peekc = 0;
	} else
		if (eof)
			return EOFC;
		else
			c = c0_getchar();
loop:
	if (c == -1) {
		eof++;
		return EOFC;
	}
	switch (ctab(c)) {

	case SHARP:
		if ((c = symbol()) != CON) {
			error("Illegal #");
			return c;
		}
		tline = cval;
		while (ctab(peekc) == SPACE)
			peekc = c0_getchar();
		if (peekc == '"') {
			sp = FILENAME;
			while ((c = mapch('"')) >= 0)
				B(sp++) = (signed char)c;
			B(sp++) = 0;
			peekc = c0_getchar();
		}
		if (peekc != '\n') {
			error("Illegal #");
			while (c0_getchar() != '\n' && eof == 0)
				if (feof(c0_in))
					c0trap("a '#' line ends the file: the original "
					    "reads past the end for ever");
		}
		peekc = 0;
		line = tline;
		return symbol();

	case INSERT:		/* ignore newlines */
		inhdr = 1;
		c = c0_getchar();
		goto loop;

	case NEWLN:
		if (!inhdr)
			line++;
		inhdr = 0;
		/* FALLTHROUGH */

	case SPACE:
		c = c0_getchar();
		goto loop;

	case PLUS:
		return subseq(c, PLUS, INCBEF);

	case MINUS:
		return subseq(c, subseq('>', MINUS, ARROW), DECBEF);

	case ASSIGN:
		c = spnextchar();
		peekc = 0;
		if (c == '=')
			return EQUAL;
		if (c == ' ')
			return ASSIGN;
		if (c == '<' || c == '>') {
			if (spnextchar() != c) {
				peeksym = ctab(c);
				return ASSIGN;
			}
			peekc = 0;
			return c == '<' ? ASLSH : ASRSH;
		}
		if (ctab(c) >= PLUS && ctab(c) <= EXOR) {
			if (spnextchar() != ' '
			 && (c == '-' || c == '&' || c == '*')) {
				error("Warning: %c= operator assumed", c);
				nerror--;
			}
			c = ctab(c);
			return (int16_t)(c + ASPLUS - PLUS);
		}
		peekc = c;
		return ASSIGN;

	case LESS:
		if (subseq(c, 0, 1))
			return LSHIFT;
		return subseq('=', LESS, LESSEQ);

	case GREAT:
		if (subseq(c, 0, 1))
			return RSHIFT;
		return subseq('=', GREAT, GREATEQ);

	case EXCLA:
		return subseq('=', EXCLA, NEQUAL);

	case BSLASH:
		if (subseq('/', 0, 1))
			return MAX;
		goto unkn;

	case DIVIDE:
		if (subseq('\\', 0, 1))
			return MIN;
		if (subseq('*', 1, 0))
			return DIVIDE;
		while ((c = spnextchar()) != EOFC) {
			if (c == -1)
				c0trap("a comment ends the file: the original reads "
				    "past the end for ever (EOF is not EOFC)");
			peekc = 0;
			if (c == '*') {
				if (spnextchar() == '/') {
					peekc = 0;
					c = c0_getchar();
					goto loop;
				}
			}
		}
		eof++;
		error("Nonterminated comment");
		return 0;

	case PERIOD:
	case DIGIT:
		peekc = c;
		return getnum();

	case DQUOTE:
		cval = isn++;
		return STRING;

	case SQUOTE:
		return getcc();

	case LETTER:
		sp = SYMBUF;
		while (ctab(c) == LETTER || ctab(c) == DIGIT) {
			if (c == -1)
				c0trap("a name ends the file: ctab[-1] is LETTER, "
				    "the original reads past the end for ever");
			if (sp < SYMBUF + NCPS)
				B(sp++) = (signed char)c;
			c = c0_getchar();
		}
		while (sp < SYMBUF + NCPS)
			B(sp++) = '\0';
		mossym = 0;
		if (mosflg) {
			mossym = FMOS;
			mosflg = 0;
		}
		peekc = c;
		if ((c = lookup()) == KEYW && cval == SIZEOF)
			c = SIZEOF;
		return c;

	case AND:
		return subseq('&', AND, LOGAND);

	case OR:
		return subseq('|', OR, LOGOR);

	case UNKN:
	unkn:
		error("Unknown character");
		c = c0_getchar();
		goto loop;

	}
	return ctab(c);
}

/*
 * 0852 getnum - same. Read a number. Return kind.
 */
int16_t
getnum(void)
{
	uint16_t np;
	int16_t c, base;
	int16_t expseen, sym, ndigit;
	const char *nsyn;
	int16_t maxdigit;
	uint32_t lv;

	nsyn = "Number syntax";
	lcval_set(0);
	base = 10;
	maxdigit = 0;
	np = NUMBUF;
	ndigit = 0;
	sym = CON;
	expseen = 0;
	if ((c = spnextchar()) == '0')
		base = 8;
	for (;; c = c0_getchar()) {
		B(np++) = (signed char)c;
		if (ctab(c) == DIGIT || ((base == 16) && (('a' <= c && c <= 'f')
		    || ('A' <= c && c <= 'F')))) {
			lv = (uint32_t)lcval_get();
			if (base == 8)
				lv <<= 3;
			else if (base == 10)
				lv = ((lv << 2) + lv) << 1;
			else
				lv <<= 4;
			if (ctab(c) == DIGIT)
				c -= '0';
			else if (c >= 'a')
				c -= 'a' - 10;
			else
				c -= 'A' - 10;
			lv += (uint32_t)(int32_t)c;
			lcval_set((int32_t)lv);
			ndigit++;
			if (c > maxdigit)
				maxdigit = c;
			continue;
		}
		if (c == '.') {
			if (base == 16 || sym == FCON)
				error(nsyn);
			sym = FCON;
			base = 10;
			continue;
		}
		if (ndigit == 0) {
			sym = DOT;
			break;
		}
		if ((c == 'e' || c == 'E') && expseen == 0) {
			expseen++;
			sym = FCON;
			if (base == 16 || maxdigit >= 10)
				error(nsyn);
			base = 10;
			B(np++) = (signed char)(c = c0_getchar());
			if (c != '+' && c != '-' && ctab(c) != DIGIT)
				break;
		} else if (c == 'x' || c == 'X') {
			if (base != 8 || lcval_get() != 0 || sym != CON)
				error(nsyn);
			base = 16;
		} else if ((c == 'l' || c == 'L') && sym == CON) {
			c = c0_getchar();
			sym = LCON;
			break;
		} else
			break;
	}
	peekc = c;
	if (maxdigit >= base)
		error(nsyn);
	if (sym == FCON) {
		B(np - 1) = 0;
		cval = (int16_t)(np - NUMBUF);
		return FCON;
	}
	if (sym == CON && (lcval_get() < 0 || (lcval_get() > MAXINT && base == 10)
	    || (lcval_get() >> 1) > MAXINT)) {
		sym = LCON;
	}
	cval = (int16_t)lcval_get();
	return sym;
}

/*
 * 0b12 subseq - same. If the next input character is c, return b and
 * advance. Otherwise push back the character and return a.
 */
int16_t
subseq(int16_t c, int16_t a, int16_t b)
{
	if (spnextchar() != c)
		return a;
	peekc = 0;
	return b;
}

/*
 * 0b32 putstr - same. Write out a string, either in-line or in the
 * string temp file labelled by lab.
 */
void
putstr(int16_t lab, int16_t amax)
{
	int16_t c, max;

	nchstr = 0;
	max = amax;
	if (lab) {
		strflg++;
		outcode("BNB", LABEL, lab, BDATA);
		max = 10000;
	} else
		outcode("B", BDATA);
	while ((c = mapch('"')) >= 0) {
		if (nchstr < max) {
			nchstr++;
			if (nchstr % 15 == 0)
				outcode("0B", BDATA);
			outcode("1N", c & 0377);
		}
	}
	if (nchstr < max) {
		nchstr++;
		outcode("10");
	}
	outcode("0");
	strflg = 0;
}

/*
 * 0bda getcc - same. Read a single-quoted character constant. The
 * routine is sensitive to the layout of characters in a word: the first
 * character is cval's low byte, the second its high byte.
 */
int16_t
getcc(void)
{
	int16_t c, cc;
	uint16_t ccp;
	signed char realc;

	cval = 0;
	ccp = CVAL;
	cc = 0;
	while ((c = mapch('\'')) >= 0)
		if (cc++ < LNCPW)
			B(ccp++) = (signed char)c;
	if (cc > LNCPW)
		error("Long character constant");
	if (cc == 1) {
		realc = (signed char)cval;
		cval = realc;
	}
	return CON;
}

/*
 * 0c3e mapch - same (a switch hashed on the escape character % 3 in the
 * binary). Read a character in a string or character constant,
 * detecting the end of the string. It implements the escape sequences.
 */
int16_t
mapch(int16_t ac)
{
	int16_t a, c, n;

	c = ac;
	if ((a = mpeek) != 0)
		mpeek = 0;
	else
		a = c0_getchar();
loop:
	if (a == c)
		return -1;
	switch (a) {

	case '\n':
	case '\0':
		error("Nonterminated string");
		peekc = a;
		return -1;

	case '\\':
		switch (a = c0_getchar()) {

		case 't':
			return '\t';

		case 'n':
			return '\n';

		case 'b':
			return '\b';

		case 'f':
			return '\014';

		case 'v':
			return '\013';

		case '0': case '1': case '2': case '3':
		case '4': case '5': case '6': case '7':
			n = 0;
			c = 0;
			while (++c <= 3 && '0' <= a && a <= '7') {
				n = (int16_t)(n << 3);
				n = (int16_t)(n + a - '0');
				a = c0_getchar();
			}
			mpeek = a;
			return n;

		case 'r':
			return '\r';

		case '\n':
			if (!inhdr)
				line++;
			inhdr = 0;
			a = c0_getchar();
			goto loop;
		}
	}
	return a;
}

/*
 * 0df8 tree - same (the token switch hashed % 7, the stacked operator's
 * % 2). Read an expression and return a pointer to its tree. It's the
 * classical bottom-up, priority-driven scheme. The initflg prevents the
 * parse from going past "," or ":" because those delimiters are special
 * in initializer (and some other) expressions.
 *
 * The operator and priority stacks are the original's own (only this
 * function sees them); the operand stack cmst[] is in the image, since
 * build() and the others work on it through cp - and keep working on it
 * after tree() has returned: cinit(), doret() and dogoto() push onto
 * the dead frame's cmst[] and build() there. So each nesting depth of
 * tree() has its own cmst[] in the image, which only the next tree() at
 * that depth reuses (tree_cmst()).
 */
uint16_t
tree(void)
{
	int16_t opst[SSIZE], prst[SSIZE];
	int16_t *op, *pp;
	int16_t andflg, o;
	uint16_t cs;
	int16_t p, ps, os;
	uint16_t cmst;
	uint16_t lcp;
	uint16_t r;

	cmst = tree_cmst(1);
	c0_tree_stmtdepth = c0_stmtdepth;
	c0_cp_funchead = 0;
	curbase = funcbase;
	op = opst;
	pp = prst;
	cp = cmst;
	*op = SEOF;
	*pp = 06;
	andflg = 0;

advanc:
	switch (o = symbol()) {

	case NAME:
		cs = csym;
		if (HCLASS(cs) == TYPEDEF)
			goto atype;
		if (HCLASS(cs) == ENUMCON) {
			WU(cp) = cblock(HOFFSET(cs));
			cp += 2;
			goto tand;
		}
		if (HCLASS(cs) == 0 && HTYP(cs) == 0) {
			if (nextchar() == '(') {
				/* set function */
				HCLASS(cs) = EXTERN;
				HTYP(cs) = FUNC;
			} else {
				HCLASS(cs) = STATIC;
				error("%.8s undefined; func. %.8s", HNAME(cs),
				    HNAME(funcsym));
				if (initflg) {
					HCLASS(cs) = EXTERN;
					error("(Warning only)");
					nerror -= 2;
				}
			}
		}
		WU(cp) = nblock(cs);
		cp += 2;
		goto tand;

	case FCON:
		WU(cp) = fblock(DOUBLE, copnum(cval));
		cp += 2;
		goto tand;

	case LCON:
		lcp = gblock(SZ_LNODE);
		OP(lcp) = LCON;
		TYP(lcp) = LONG;
		LVLO(lcp) = WI(LCVAL + 2);
		LVHI(lcp) = WI(LCVAL);
		WU(cp) = lcp;
		cp += 2;
		goto tand;

	case CON:
		WU(cp) = cblock(cval);
		cp += 2;
		goto tand;

	/* fake a static char array */
	case STRING:
		putstr(cval, 0);
		cs = gblock(SZ_HSHTAB);
		HCLASS(cs) = STATIC;
		HOFFSET(cs) = cval;
		WU(cp) = block(NAME, ARRAY + CHAR, NCHSTR, 0, cs, 0);
		cp += 2;

	tand:
		if (cp >= cmst + CMSIZ * 2) {
			error("Expression overflow");
			c0_exit(1);
		}
		if (andflg)
			goto syntax;
		andflg = 1;
		goto advanc;

	case KEYW:
	atype:
		if (*op != LPARN || andflg)
			goto syntax;
		peeksym = o;
		WU(cp) = xprtype(gblock(SZ_HSHTAB));
		cp += 2;
		if ((o = symbol()) != RPARN)
			goto syntax;
		o = CAST;
		--op;
		--pp;
		if (*op == SIZEOF) {
			andflg = 1;
			*pp = 100;
			goto advanc;
		}
		goto oponst;

	case INCBEF:
	case DECBEF:
		if (andflg)
			o += 2;
		goto oponst;

	case COMPL:
	case EXCLA:
	case SIZEOF:
		if (andflg)
			goto syntax;
		goto oponst;

	case MINUS:
		if (!andflg)
			o = NEG;
		andflg = 0;
		goto oponst;

	case AND:
	case TIMES:
		if (andflg)
			andflg = 0;
		else if (o == AND)
			o = AMPER;
		else
			o = STAR;
		goto oponst;

	case LPARN:
		if (andflg) {
			o = symbol();
			if (o == RPARN)
				o = MCALL;
			else {
				peeksym = o;
				o = CALL;
				andflg = 0;
			}
		}
		goto oponst;

	case RBRACK:
	case RPARN:
		if (!andflg)
			goto syntax;
		goto oponst;

	case DOT:
	case ARROW:
		mosflg++;
		break;

	case ASSIGN:
		if (andflg == 0 && PLUS <= *op && *op <= EXOR) {
			o = (int16_t)(*op-- + ASPLUS - PLUS);
			pp--;
			goto oponst;
		}
		break;

	}
	/* binaries */
	if (andflg == 0)
		goto syntax;
	andflg = 0;

oponst:
	p = (int16_t)((opdope(o) >> 9) & 077);
opon1:
	ps = *pp;
	if (p > ps || (p == ps && (opdope(o) & RASSOC) != 0)) {
		switch (o) {

		case INCAFT:
		case DECAFT:
			p = 37;
			break;
		case LPARN:
		case LBRACK:
		case CALL:
			p = 04;
		}
		if (initflg) {
			if ((o == COMMA && *op != LPARN && *op != CALL)
			 || (o == COLON && *op != QUEST)) {
				p = 00;
				goto opon1;
			}
		}
		if (op >= &opst[SSIZE - 1]) {
			error("expression overflow");
			c0_exit(1);
		}
		*++op = o;
		*++pp = p;
		goto advanc;
	}
	--pp;
	switch (os = *op--) {

	case SEOF:
		peeksym = o;
		build(0);		/* flush conversions */
		cp -= 2;
		r = WU(cp);
		tree_cmst(-1);
		return r;

	case COMMA:
		if (*op != CALL)
			os = SEQNC;
		break;

	case CALL:
		if (o != RPARN)
			goto syntax;
		build(os);
		goto advanc;

	case MCALL:
		WU(cp) = 0;	/* empty arglist */
		cp += 2;
		os = CALL;
		break;

	case INCBEF:
	case INCAFT:
	case DECBEF:
	case DECAFT:
		WU(cp) = cblock(1);
		cp += 2;
		break;

	case LPARN:
		if (o != RPARN)
			goto syntax;
		goto advanc;

	case LBRACK:
		if (o != RBRACK)
			goto syntax;
		build(LBRACK);
		goto advanc;
	}
	build(os);
	goto opon1;

syntax:
	error("Expression syntax");
	errflush(o);
	tree_cmst(-1);
	return 0;
}

/*
 * 1364 xprtype - same. The type of a cast or of sizeof(type), built in
 * a struct hshtab from gblock() and turned into a tree node with op
 * ETYPE. As in v7, decl1() gets &sc - an address - as its storage class:
 * any address works (it is never a class), the port passes the stack
 * area's.
 */
uint16_t
xprtype(uint16_t atyb)
{
	uint16_t tyb;
	uint16_t typer, sc;
	uint16_t md, fb;
	uint16_t scp;

	typer = salloc(SZ_HSHTAB);
	sc = salloc(2);
	tyb = atyb;
	fb = funcbase;
	md = maxdecl;
	scp = cp;
	funcbase = curbase;
	WI(sc) = DEFXTRN;	/* will cause error if class mentioned */
	getkeywords(sc, typer);
	HCLASS(tyb) = 0;
	HBLKLEV(tyb) = 0;
	decl1((int16_t)sc, typer, 0, tyb);
	funcbase = fb;
	maxdecl = md;
	cp = scp;
	OP(tyb) = ETYPE;
	sfree(2);
	sfree(SZ_HSHTAB);
	return tyb;
}

/*
 * 13d4 copnum - same. A floating constant's text (numbuf) into the heap.
 */
uint16_t
copnum(int16_t len)
{
	uint16_t s1, s2, s3;
	signed char ch;

	s1 = s2 = gblock((int16_t)((len + LNCPW - 1) & ~(LNCPW - 1)));
	s3 = NUMBUF;
	do {
		ch = B(s3++);
		B(s2++) = ch;
	} while (ch);
	return s1;
}
