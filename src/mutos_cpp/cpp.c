/*
 * cpp.c - v7/cpp/cpp.c as the original MUTOS 1700 cpp binary has it
 * ("@(#)cpp.c 1.6 12/12/86"): every function of cpp.o, in the binary's
 * order (tests/realbin/cppmap.txt gives each one's text offset), read
 * from its code beside v7's source. A function's comment names what the
 * binary does differently from v7; where it says nothing, the binary does
 * what v7's source says.
 *
 * Conventions (cpp.h): pointers are 16-bit addresses in the image, ints
 * are int16_t and wrap as the original's do, a char is read sign-extended
 * (rb()) and indexes a table as the 8086 code does - with its sign, so a
 * byte 0200..0377 reads below the table. Pointer comparisons are
 * unsigned, as the compiled code's (jb, jae).
 */
#include "cpp.h"

#define	eob(a)	((uint16_t)(a) >= pend)
#define	bob(a)	(pbeg >= (uint16_t)(a))
#define	isid(c)		(rb((uint16_t)(FASTAB + (c))) & IB)
#define	isspc(c)	(rb((uint16_t)(ptrtab + (c))) & SB)
#define	isnum(c)	(rb((uint16_t)(FASTAB + (c))) & NB)
#define	iscom(c)	(rb((uint16_t)(FASTAB + (c))) & CB)
#define	isquo(c)	(rb((uint16_t)(FASTAB + (c))) & QB)
#define	iswarn(c)	(rb((uint16_t)(FASTAB + (c))) & WB)
#define	toktyp(c)	rb((uint16_t)(TOKTYP + (c)))
#define	isslo		(ptrtab == SLOTAB)
#define	fasscan()	(ptrtab = FASTAB)
#define	sloscan()	(ptrtab = SLOTAB)

#define	BEG	0
#define	LF	1

/* the arrays of cpp.c, their elements */
#define	instack_(i)	AU(INSTACK, i)
#define	bufstack_(i)	AU(BUFSTACK, i)
#define	endbuf_(i)	AU(ENDBUF, i)
#define	inctop_(i)	AW(INCTOP, i)
#define	dirnams_(i)	AU(DIRNAMS, i)
#define	fins_(i)	AW(FINS, i)
#define	dirs_(i)	AU(DIRS, i)

static void
pperr1(uint16_t s, uint16_t x)
{
	pperror(s, x, 0);
}

/* sayline (00b6) */
static void
sayline_(void)
{
	uint16_t a[2];

	if (pflag == 0) {
		a[0] = (uint16_t)lineno_(ifno);
		a[1] = fnames_(ifno);
		cpp_fprintf(fout, S_SAYLINE, a, 2);
	}
}

void
sayline(void)
{
	uint16_t bp = fr_enter(0, 0);

	sayline_();
	fr_leave(bp, 0);
}

/* dump (00e8) */
static void
dump_(void)
{
	uint16_t p1, f;

	if ((p1 = outp) == inp || flslvl != 0)
		return;
	f = fout;
	while (p1 < inp)
		cpp_putc(UB(p1++), f);
	outp = p1;
}

void
dump(void)
{
	uint16_t bp = fr_enter(0, 2);

	dump_();
	fr_leave(bp, 0);
}

/*
 * refill (0142). Delta: at the end of the input with an #if, #ifdef or
 * #ifndef still open (trulvl or flslvl not 0) it says "missing endif".
 */
static uint16_t
refill_(uint16_t p)
{
	uint16_t np, op;
	int16_t ninbuf;

	dump();
	np = (uint16_t)(pbuf - (uint16_t)(p - inp));
	op = inp;
	if (bob(np + 1)) {
		pperr1(S_TOKLONG, 0);
		np = pbeg;
		p = (uint16_t)(inp + BUFSIZ_);
	}
	macdam = (int16_t)(macdam + (uint16_t)(np - inp));
	outp = inp = np;
	while (op < p)
		wb(np++, rb(op++));
	p = np;
	for (;;) {
		if (mactop > inctop_(ifno)) {
			/* retrieve hunk of pushed-back macro text */
			int16_t c;

			op = instack_(--mactop);
			np = pbuf;
			do {
				do {
					c = rb(op++);
					wb(np++, c);
				} while (c != 0);
			} while (op < endbuf_(mactop));
			pend = (uint16_t)(np - 1);
			/* make buffer space avail for 'include' processing */
			if (fretop < MAXFRE)
				bufstack_(fretop++) = instack_(mactop);
			return p;
		}
		/* get more text from file(s) */
		maclvl = 0;
		if (0 < (ninbuf = cpp_read(fin, pbuf, BUFSIZ_))) {
			pend = (uint16_t)(pbuf + ninbuf);
			wb(pend, 0);
			return p;
		}
		/* end of #include file */
		if (ifno == 0) {	/* end of input */
			if (plvl != 0) {
				int16_t n = plvl, tlin = lineno_(ifno);
				uint16_t tfil = fnames_(ifno);

				lineno_(ifno) = maclin;
				fnames_(ifno) = macfil;
				pperr1(S_UNTERM, macnam);
				lineno_(ifno) = tlin;
				fnames_(ifno) = tfil;
				np = p;
				wb(np++, '\n');	/* shut off unterminated quoted string */
				while (--n >= 0)
					wb(np++, ')');	/* supply missing parens */
				pend = np;
				wb(np, 0);
				if (plvl < 0)
					plvl = 0;
				return p;
			}
			if (trulvl != 0 || flslvl != 0)
				pperr1(S_MISSENDIF, 0);
			inp = p;
			dump();
			cpp_exit(exfail);
		}
		cpp_close(fin);
		fin = fins_(--ifno);
		dirs_(0) = dirnams_(ifno);
		sayline();
	}
}

uint16_t
refill(uint16_t p)
{
	uint16_t bp = fr_enter(1, 0xa);
	uint16_t r = refill_(p);

	fr_leave(bp, 1);
	return r;
}

/*
 * cotoken (034a): the next token, from p. Delta: a comment longer than
 * BUFSIZ under -C is split into pieces, as in v7, but the "*" and "/"
 * that close a piece are written only outside a false #if (flslvl 0).
 */
static uint16_t
cotoken_(uint16_t p)
{
	int16_t c, i, quoc;

	if (cot_state != BEG)
		goto prevlf;
again:
	while (!isspc(rb(p++)))
		;
	inp = (uint16_t)(p - 1);
	c = rb(inp);
	if ((uint16_t)c > 0x7c)
		goto endsw;
	switch (c) {
	case 0:
		if (eob(--p)) {
			p = refill(p);
			goto again;
		}
		++p;	/* ignore null byte */
		break;
	case '|': case '&':
		for (;;) {	/* sloscan only */
			if (rb(p++) == rb(inp))
				break;
			if (eob(--p))
				p = refill(p);
			else
				break;
		}
		break;
	case '=': case '!':
		for (;;) {	/* sloscan only */
			if (rb(p++) == '=')
				break;
			if (eob(--p))
				p = refill(p);
			else
				break;
		}
		break;
	case '<': case '>':
		for (;;) {	/* sloscan only */
			if (rb(p++) == '=' || rb((uint16_t)(p - 2)) == rb((uint16_t)(p - 1)))
				break;
			if (eob(--p))
				p = refill(p);
			else
				break;
		}
		break;
	case '\\':
		for (;;) {
			if (rb(p++) == '\n') {
				++lineno_(ifno);
				break;
			}
			if (eob(--p))
				p = refill(p);
			else {
				++p;
				break;
			}
		}
		break;
	case '/':
		for (;;) {
			if (rb(p++) == '*') {	/* comment */
				if (!passcom) {
					inp = (uint16_t)(p - 2);
					dump();
					++flslvl;
				}
				for (;;) {
					while (!iscom(rb(p++)))
						;
					if (rb((uint16_t)(p - 1)) == '*')
						for (;;) {
							if (rb(p++) == '/')
								goto endcom;
							if (eob(--p)) {
								if (!passcom) {
									inp = p;
									p = refill(p);
								} else if ((int16_t)(p - inp) >= BUFSIZ_) {
									/* split long comment */
									inp = p;
									p = refill(p);	/* last char written is '*' */
									if (flslvl == 0)
										cpp_putc('/', fout);	/* terminate first part */
									/* and fake start of 2nd */
									outp = inp = p = (uint16_t)(p - 3);
									wb(p++, '/');
									wb(p++, '*');
									wb(p++, '*');
								} else
									p = refill(p);
							} else
								break;
						}
					else if (rb((uint16_t)(p - 1)) == '\n') {
						++lineno_(ifno);
						if (!passcom)
							cpp_putc('\n', fout);
					} else if (eob(--p)) {
						if (!passcom) {
							inp = p;
							p = refill(p);
						} else if ((int16_t)(p - inp) >= BUFSIZ_) {
							/* split long comment */
							inp = p;
							p = refill(p);
							if (flslvl == 0)
								cpp_putc('*', fout);
							if (flslvl == 0)
								cpp_putc('/', fout);
							outp = inp = p = (uint16_t)(p - 2);
							wb(p++, '/');
							wb(p++, '*');
						} else
							p = refill(p);
					} else
						++p;	/* ignore null byte */
				}
endcom:
				if (!passcom) {
					outp = inp = p;
					--flslvl;
					goto again;
				}
				break;
			}
			if (eob(--p))
				p = refill(p);
			else
				break;
		}
		break;
	case '"': case '\'':
		quoc = rb((uint16_t)(p - 1));
		for (;;) {
			while (!isquo(rb(p++)))
				;
			if ((int8_t)rb((uint16_t)(p - 1)) == (int8_t)quoc)
				break;
			if (rb((uint16_t)(p - 1)) == '\n') {	/* bare \n terminates quotation */
				--p;
				break;
			}
			if (rb((uint16_t)(p - 1)) == '\\')
				for (;;) {
					if (rb(p++) == '\n') {	/* escaped \n ignored */
						++lineno_(ifno);
						break;
					}
					if (eob(--p))
						p = refill(p);
					else {
						++p;
						break;
					}
				}
			else if (eob(--p))
				p = refill(p);
			else
				++p;	/* it was a different quote character */
		}
		break;
	case '\n':
		++lineno_(ifno);
		if (isslo) {
			cot_state = LF;
			return p;
		}
prevlf:
		cot_state = BEG;
		for (;;) {
			if (rb(p++) == '#')
				return p;
			if (eob(inp = --p))
				p = refill(p);
			else
				goto again;
		}
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
		for (;;) {
			while (isnum(rb(p++)))
				;
			if (eob(--p))
				p = refill(p);
			else
				break;
		}
		break;
	case 'A': case 'B': case 'C': case 'D': case 'E':
	case 'F': case 'G': case 'H': case 'I': case 'J':
	case 'K': case 'L': case 'M': case 'N': case 'O':
	case 'P': case 'Q': case 'R': case 'S': case 'T':
	case 'U': case 'V': case 'W': case 'X': case 'Y':
	case 'Z': case '_':
	case 'a': case 'b': case 'c': case 'd': case 'e':
	case 'f': case 'g': case 'h': case 'i': case 'j':
	case 'k': case 'l': case 'm': case 'n': case 'o':
	case 'p': case 'q': case 'r': case 's': case 't':
	case 'u': case 'v': case 'w': case 'x': case 'y':
	case 'z':
		/*
		 * The superimposed code (v7's scw1): the kth character of a
		 * macro name sets bit k of macbit[c]; a character whose bit
		 * is clear ends the test - no macro has it there.
		 */
#define	tmac1(c, bit)	if (!(rb((uint16_t)(MACBIT + (c))) & (bit))) goto nomac
		if (flslvl)
			goto nomac;
		for (;;) {
			c = rb((uint16_t)(p - 1));		tmac1(c, 0x01);
			i = rb(p++); if (!isid(i)) goto endid;	tmac1(i, 0x02);
			c = rb(p++); if (!isid(c)) goto endid;	tmac1(c, 0x04);
			i = rb(p++); if (!isid(i)) goto endid;	tmac1(i, 0x08);
			c = rb(p++); if (!isid(c)) goto endid;	tmac1(c, 0x10);
			i = rb(p++); if (!isid(i)) goto endid;	tmac1(i, 0x20);
			c = rb(p++); if (!isid(c)) goto endid;	tmac1(c, 0x40);
			i = rb(p++); if (!isid(i)) goto endid;	tmac1(i, 0x80);
			while (isid(rb(p++)))
				;
			if (eob(--p)) {
				refill(p);
				p = (uint16_t)(inp + 1);
				continue;
			}
			goto lokid;
endid:
			if (eob(--p)) {
				refill(p);
				p = (uint16_t)(inp + 1);
				continue;
			}
lokid:
			slookup(inp, p, 0);
			if (newp) {
				p = newp;
				goto again;
			}
			break;
nomac:
			while (isid(rb(p++)))
				;
			if (eob(--p)) {
				p = refill(p);
				goto nomac;
			}
			break;
		}
		break;
#undef	tmac1
	default:
		break;
	}	/* end of switch */
endsw:
	if (isslo)
		return p;
	goto again;
}

uint16_t
cotoken(uint16_t p)
{
	uint16_t bp = fr_enter(1, 4);
	uint16_t r = cotoken_(p);

	fr_leave(bp, 1);
	return r;
}

/* skipbl (0980): get next non-blank token */
static uint16_t
skipbl_(uint16_t p)
{
	do {
		outp = inp = p;
		p = cotoken(p);
	} while (toktyp(rb(inp)) == BLANK);
	return p;
}

uint16_t
skipbl(uint16_t p)
{
	uint16_t bp = fr_enter(1, 0);
	uint16_t r = skipbl_(p);

	fr_leave(bp, 1);
	return r;
}

/*
 * unfill (09ae): take <= BUFSIZ chars from right end of buffer and put
 * them on instack; slide rest of buffer to the right, update pointers,
 * return new p.
 */
static uint16_t
unfill_(uint16_t p)
{
	uint16_t np, op;
	int16_t d, c;

	if (mactop >= MAXFRE) {
		pperr1(S_PUSHBACK, macnam);
		p = inp = pend;
		dump();	/* begin flushing pushback */
		while (mactop > inctop_(ifno)) {
			p = refill(p);
			p = inp = pend;
			dump();
		}
	}
	if (fretop > 0)
		np = bufstack_(--fretop);
	else {
		np = savch;
		savch = (uint16_t)(savch + BUFSIZ_);
		if (savch >= (uint16_t)(SBF + SBSIZE)) {
			pperr1(S_NOSPACE1, 0);
			cpp_exit(exfail);
		}
		wb(savch++, 0);
	}
	instack_(mactop) = np;
	op = (uint16_t)(pend - BUFSIZ_);
	if (op < p)
		op = p;
	do {	/* out with old */
		do {
			c = rb(op++);
			wb(np++, c);
		} while (c != 0);
	} while (op < pend);
	endbuf_(mactop++) = np;	/* mark end of saved text */
	np = (uint16_t)(pbuf + BUFSIZ_);
	op = (uint16_t)(pend - BUFSIZ_);
	pend = np;
	if (op < p)
		op = p;
	while (outp < op) {	/* slide over new */
		--np;
		--op;
		wb(np, rb(op));
	}
	if (bob(np))
		pperr1(S_TOKLONG2, 0);
	d = (int16_t)(np - outp);
	outp = (uint16_t)(outp + d);
	inp = (uint16_t)(inp + d);
	macdam = (int16_t)(macdam + d);
	return (uint16_t)(p + d);
}

uint16_t
unfill(uint16_t p)
{
	uint16_t bp = fr_enter(1, 4);
	uint16_t r = unfill_(p);

	fr_leave(bp, 1);
	return r;
}

/*
 * doincl (0ae0). Deltas: between "<" and ">" no macro is expanded
 * (flslvl is raised while the name is read). The open() of a name: see
 * cpp_open() - which names are paths of the MUTOS file system, looked up
 * under the MUTOS root, is noted here as the name is built.
 */
static uint16_t
doincl_(uint16_t p, uint16_t bp)
{
	int16_t filok, inctype, fd;
	uint16_t cp, dirp, nfil;
	const uint16_t filname = (uint16_t)(bp - 0x20c);	/* char filname[BUFSIZ] */
	int rooted = 0;

	fr_array(filname, BUFSIZ_);
	p = skipbl(p);
	cp = filname;
	if (rb(inp++) == '<') {	/* special <> syntax */
		inctype = 1;
		++flslvl;
		for (;;) {
			outp = inp = p;
			p = cotoken(p);
			if (rb(inp) == '\n') {
				--p;
				wb(cp, 0);
				--flslvl;
				break;
			}
			if (rb(inp) == '>') {
				wb(cp, 0);
				--flslvl;
				break;
			}
			while (inp < p)
				wb(cp++, rb(inp++));
		}
	} else if (rb((uint16_t)(inp - 1)) == '"') {	/* regular "" syntax */
		inctype = 0;
		while (inp < p)
			wb(cp++, rb(inp++));
		if (rb(--cp) == '"')
			wb(cp, 0);
	} else {
		pperr1(S_BADINCL, 0);
		inctype = 2;
	}
	/* flush current file to \n , then write \n */
	++flslvl;
	do {
		outp = inp = p;
		p = cotoken(p);
	} while (rb(inp) != '\n');
	--flslvl;
	inp = p;
	dump();
	if (inctype == 2)
		goto out;
	/* look for included file */
	if (ifno + 1 >= MAXINC) {
		pperr1(S_INCNEST, 0);
		goto out;
	}
	if ((nfil = savch) > (uint16_t)(SBF + SBSIZE - BUFSIZ_)) {
		pperr1(S_NOSPACE2, 0);
		cpp_exit(exfail);
	}
	filok = 0;
	for (dirp = (uint16_t)(DIRS + 2 * inctype); WU(dirp) != 0; dirp += 2) {
		if (rb(filname) == '/' || rb(WU(dirp)) == '\0') {
			cstrcpy(nfil, filname);
			rooted = rb(filname) == '/';	/* a MUTOS path */
		} else {
			cstrcpy(nfil, WU(dirp));
			cstrcat(nfil, S_SLASH);
			cstrcat(nfil, filname);
			rooted = cpp_rooted(WU(dirp));
		}
		fd = cpp_open(nfil, rooted);
		AW(FINS, ifno + 1) = fd;
		if (0 < fd) {
			filok = 1;
			fin = fins_(++ifno);
			break;
		}
	}
	if (filok == 0)
		pperr1(S_CANTFIND, filname);
	else {
		uint16_t d;

		lineno_(ifno) = 1;
		fnames_(ifno) = cp = nfil;
		while (rb(cp++) != 0)
			;
		savch = cp;
		d = trmdir(copy(nfil));
		cpp_set_rooted(d, rooted);
		dirnams_(ifno) = d;
		dirs_(0) = d;
		sayline();
		/* save current contents of buffer */
		while (!eob(p))
			p = unfill(p);
		inctop_(ifno) = mactop;
	}
out:
	return p;
}

uint16_t
doincl(uint16_t p)
{
	uint16_t bp = fr_enter(1, 0x208 + 2);
	uint16_t r = doincl_(p, bp);

	fr_leave(bp, 1);
	return r;
}

/* equfrm (0d18) */
static int16_t
equfrm_(uint16_t a, uint16_t p1, uint16_t p2)
{
	int16_t c, flag;

	c = rb(p2);
	wb(p2, 0);
	flag = cstrcmp(a, p1);
	wb(p2, c);
	return flag == 0;
}

int16_t
equfrm(uint16_t a, uint16_t p1, uint16_t p2)
{
	uint16_t bp = fr_enter(3, 4);
	int16_t r = equfrm_(a, p1, p2);

	fr_leave(bp, 3);
	return r;
}

/*
 * dodef (0d5a): process '#define'. Delta: an escaped linefeed in the
 * body writes a linefeed to the output (v7 drops it), so that the output
 * keeps the input's line count. Kept from v7: an illegal macro name
 * returns with flslvl still raised - nothing more is written up to the
 * next #endif or #else that brings it back to 0 (CPP-02).
 */
static uint16_t
dodef_(uint16_t p, uint16_t bp)
{
	uint16_t pin, psav, cf, np, oldval, oldsavch;
	int16_t pf, qf, b, c, params, quoc;
	uint16_t formal[MAXFRM];	/* formal[n] is name of nth formal */
	const uint16_t formtxt = (uint16_t)(bp - 0x254);	/* space for formal names */

	if (savch > (uint16_t)(SBF + SBSIZE - BUFSIZ_)) {
		pperr1(S_TOOMUCHDEF, 0);
		return p;
	}
	fr_array(formtxt, BUFSIZ_);
	oldsavch = savch;	/* to reclaim space if redefinition */
	++flslvl;	/* prevent macro expansion during 'define' */
	p = skipbl(p);
	pin = inp;
	if (toktyp(rb(pin)) != IDENT) {
		ppwarn(S_ILLMAC, 0);
		while (rb(inp) != '\n')
			p = skipbl(p);
		return p;
	}
	np = slookup(pin, p, 1);
	if ((oldval = SVALUE(np)) != 0)
		savch = oldsavch;	/* was previously defined */
	b = 1;
	cf = pin;
	while (cf < p) {	/* update macbit */
		c = rb(cf++);
		wb((uint16_t)(MACBIT + c), rb((uint16_t)(MACBIT + c)) | (b & 0xff));
		b = (int16_t)((b + b) & 0xFF);
	}
	params = 0;
	outp = inp = p;
	p = cotoken(p);
	pin = inp;
	pf = 0;
	if (rb(pin) == '(') {	/* with parameters; identify the formals */
		cf = formtxt;
		for (;;) {
			p = skipbl(p);
			pin = inp;
			if (rb(pin) == '\n') {
				--lineno_(ifno);
				--p;
				pperr1(S_MISSPAREN, SNAME(np));
				break;
			}
			if (rb(pin) == ')')
				break;
			if (rb(pin) == ',')
				continue;
			if (toktyp(rb(pin)) != IDENT) {
				c = rb(p);
				wb(p, 0);
				pperr1(S_BADFORMAL, pin);
				wb(p, c);
			} else if (pf >= MAXFRM) {
				c = rb(p);
				wb(p, 0);
				pperr1(S_MANYFORM, pin);
				wb(p, c);
			} else {
				formal[pf++] = cf;
				while (pin < p)
					wb(cf++, rb(pin++));
				wb(cf++, 0);
				++params;
			}
		}
		if (params == 0)
			--params;	/* #define foo() ... */
	} else if (rb(pin) == '\n') {
		--lineno_(ifno);
		--p;
	}
	/*
	 * remember beginning of macro body, so that we can
	 * warn if a redefinition is different from old value.
	 */
	oldsavch = psav = savch;
	for (;;) {	/* accumulate definition until linefeed */
		outp = inp = p;
		p = cotoken(p);
		pin = inp;
		if (rb(pin) == '\\' && rb((uint16_t)(pin + 1)) == '\n') {
			cpp_putc('\n', fout);	/* MUTOS: the linefeed is kept */
			continue;
		}
		if (rb(pin) == '\n')
			break;
		if (params) {	/* mark the appearance of formals in the definiton */
			if (toktyp(rb(pin)) == IDENT) {
				for (qf = pf; --qf >= 0;) {
					if (equfrm(formal[qf], pin, p)) {
						wb(psav++, qf + 1);
						wb(psav++, WARN);
						pin = p;
						break;
					}
				}
			} else if (rb(pin) == '"' || rb(pin) == '\'') {
				/* inside quotation marks, too */
				quoc = rb(pin);
				for (wb(psav++, rb(pin++)); pin < p
				    && (int8_t)rb(pin) != (int8_t)quoc;) {
					while (pin < p && !isid(rb(pin)))
						wb(psav++, rb(pin++));
					cf = pin;
					while (cf < p && isid(rb(cf)))
						++cf;
					for (qf = pf; --qf >= 0;) {
						if (equfrm(formal[qf], pin, cf)) {
							wb(psav++, qf + 1);
							wb(psav++, WARN);
							pin = cf;
							break;
						}
					}
					while (pin < cf)
						wb(psav++, rb(pin++));
				}
			}
		}
		while (pin < p)
			wb(psav++, rb(pin++));
	}
	wb(psav++, params);
	wb(psav++, 0);
	if ((cf = oldval) != 0) {	/* redefinition */
		--cf;	/* skip no. of params, which may be zero */
		while (rb(--cf) != 0)
			;	/* go back to the beginning */
		if (0 != cstrcmp(++cf, oldsavch)) {
			/* redefinition different from old */
			--lineno_(ifno);
			ppwarn(S_REDEF, SNAME(np));
			++lineno_(ifno);
			SVALUE(np) = (uint16_t)(psav - 1);
		} else
			psav = oldsavch;	/* identical redef.; reclaim space */
	} else
		SVALUE(np) = (uint16_t)(psav - 1);
	--flslvl;
	inp = pin;
	savch = psav;
	return p;
}

uint16_t
dodef(uint16_t p)
{
	uint16_t bp = fr_enter(1, 0x252 + 2);
	uint16_t r = dodef_(p, bp);

	fr_leave(bp, 1);
	return r;
}

/*
 * control (1120): find and handle preprocessor control lines. Delta:
 * #line (outside a false #if, without -P) is passed on as "# rest", as in
 * v7, and its quoted file name - after blanks, digits and up to a '"' -
 * becomes the current file's name (savestr()); the line number is not
 * taken.
 */
static uint16_t
control_(uint16_t p)
{
	uint16_t np, cp, cp2;

	for (;;) {
		fasscan();
		p = cotoken(p);
		if (rb(inp) == '\n')
			++inp;
		dump();
		sloscan();
		p = skipbl(p);
		wb(--inp, SALT);
		outp = inp;
		++flslvl;
		np = slookup(inp, p, 0);
		--flslvl;
		if (np == defloc) {	/* define */
			if (flslvl == 0) {
				p = dodef(p);
				continue;
			}
		} else if (np == incloc) {	/* include */
			if (flslvl == 0) {
				p = doincl(p);
				continue;
			}
		} else if (np == ifnloc) {	/* ifndef */
			++flslvl;
			p = skipbl(p);
			np = slookup(inp, p, 0);
			--flslvl;
			if (flslvl == 0 && SVALUE(np) == 0)
				++trulvl;
			else
				++flslvl;
		} else if (np == ifdloc) {	/* ifdef */
			++flslvl;
			p = skipbl(p);
			np = slookup(inp, p, 0);
			--flslvl;
			if (flslvl == 0 && SVALUE(np) != 0)
				++trulvl;
			else
				++flslvl;
		} else if (np == eifloc) {	/* endif */
			if (flslvl) {
				if (--flslvl == 0)
					sayline();
			} else if (trulvl)
				--trulvl;
			else
				pperr1(S_IFLESSENDIF, 0);
		} else if (np == elsloc) {	/* else */
			if (flslvl) {
				if (--flslvl != 0)
					++flslvl;
				else {
					++trulvl;
					sayline();
				}
			} else if (trulvl) {
				++flslvl;
				--trulvl;
			} else
				pperr1(S_IFLESSELSE, 0);
		} else if (np == udfloc) {	/* undefine */
			if (flslvl == 0) {
				++flslvl;
				p = skipbl(p);
				slookup(inp, p, DROP);
				--flslvl;
			}
		} else if (np == ifloc) {	/* if */
			newp = p;
			if (flslvl == 0 && yyparse())
				++trulvl;
			else
				++flslvl;
			p = newp;
		} else if (np == lneloc) {	/* line */
			if (flslvl == 0 && pflag == 0) {
				outp = inp = p;
				wb(--outp, '#');
				while (rb(inp) != '\n')
					p = cotoken(p);
				/* MUTOS: the file name of "# line N "name"" */
				cp = (uint16_t)(outp + 1);
				while ((rb((uint16_t)(CTYPE + 1 + rb(cp))) & 010) && cp < inp)
					cp++;	/* isspace */
				while ((rb((uint16_t)(CTYPE + 1 + rb(cp))) & 04) && cp < inp)
					cp++;	/* isdigit */
				while (rb(cp) != '"' && cp < inp)
					cp++;
				if (cp < inp) {
					cp2 = ++cp;
					while (rb(cp2) != '"' && cp2 < inp)
						cp2++;
					fnames_(ifno) = savestr(cp, cp2);
				}
				continue;
			}
		} else if (rb(++inp) == '\n')
			outp = inp;	/* allows blank line after # */
		else
			pperr1(S_UNDEFCTL, 0);
		/* flush to lf */
		++flslvl;
		while (rb(inp) != '\n') {
			outp = inp = p;
			p = cotoken(p);
		}
		--flslvl;
	}
	/* NOTREACHED: the input's end exits in refill() */
	return p;
}

uint16_t
control(uint16_t p)
{
	uint16_t bp = fr_enter(1, 4);
	uint16_t r = control_(p);

	fr_leave(bp, 1);
	return r;
}

/* savestr (1420): new - a copy of start..finish in calloc()ed memory */
static uint16_t
savestr_(uint16_t start, uint16_t finish)
{
	uint16_t ptr, s;

	ptr = s = cpp_calloc((uint16_t)(finish - start + 1), 1);
	while (start < finish)
		wb(ptr++, rb(start++));
	wb(ptr, 0);
	return s;
}

uint16_t
savestr(uint16_t start, uint16_t finish)
{
	uint16_t bp = fr_enter(2, 4);
	uint16_t r = savestr_(start, finish);

	fr_leave(bp, 2);
	return r;
}

/* stsym (1462): make definition look exactly like end of #define line */
static uint16_t
stsym_(uint16_t s, uint16_t bp)
{
	const uint16_t buf = (uint16_t)(bp - 0x204);	/* char buf[BUFSIZ] */
	uint16_t p;
	int16_t c;

	fr_array(buf, BUFSIZ_);
	/* copy to avoid running off end of world when param list is at end */
	p = buf;
	do {
		c = rb(s++);
		wb(p++, c);
	} while (c != 0);
	p = buf;
	while (isid(rb(p++)))
		;	/* skip first identifier */
	if (rb(--p) == '=') {
		wb(p++, ' ');
		while (rb(p++) != 0)
			;
	} else {
		s = S_ONE;
		do {
			c = rb(s++);
			wb(p++, c);
		} while (c != 0);
	}
	pend = p;
	wb(--p, '\n');
	sloscan();
	dodef(buf);
	return lastsym;
}

uint16_t
stsym(uint16_t s)
{
	uint16_t bp = fr_enter(1, 0x200 + 2);
	uint16_t r = stsym_(s, bp);

	fr_leave(bp, 1);
	return r;
}

/* ppsym (14d6): kluge */
static uint16_t
ppsym_(uint16_t s)
{
	uint16_t sp;

	wb(CINIT, SALT);
	wb(savch++, SALT);
	sp = stsym(s);
	--SNAME(sp);
	wb(CINIT, 0);
	return sp;
}

uint16_t
ppsym(uint16_t s)
{
	uint16_t bp = fr_enter(1, 0);
	uint16_t r = ppsym_(s);

	fr_leave(bp, 1);
	return r;
}

/* pperror (1502) */
static void
pperror_(uint16_t s, uint16_t x, uint16_t y)
{
	uint16_t a[2];

	if (rb(fnames_(ifno)) != 0) {
		a[0] = fnames_(ifno);
		cpp_fprintf(STDERR_F, S_FNAME, a, 1);
	}
	a[0] = (uint16_t)lineno_(ifno);
	cpp_fprintf(STDERR_F, S_LINE, a, 1);
	a[0] = x;
	a[1] = y;
	cpp_fprintf(STDERR_F, s, a, 2);
	cpp_fprintf(STDERR_F, S_NL, a, 0);
	++exfail;
}

void
pperror(uint16_t s, uint16_t x, uint16_t y)
{
	uint16_t bp = fr_enter(3, 0);

	pperror_(s, x, y);
	fr_leave(bp, 3);
}

/* yyerror (1570) */
static void
yyerror_(uint16_t s, uint16_t a, uint16_t b)
{
	pperror(s, a, b);
}

void
yyerror(uint16_t s, uint16_t a, uint16_t b)
{
	uint16_t bp = fr_enter(3, 0);

	yyerror_(s, a, b);
	fr_leave(bp, 3);
}

/* ppwarn (1588) */
static void
ppwarn_(uint16_t s, uint16_t x)
{
	int16_t fail = exfail;

	exfail = -1;
	pperror(s, x, 0);
	exfail = fail;
}

void
ppwarn(uint16_t s, uint16_t x)
{
	uint16_t bp = fr_enter(2, 2);

	ppwarn_(s, x);
	fr_leave(bp, 2);
}

/*
 * lookup (15b4). The table has 1000 entries (v7: 400). Delta: a name not
 * found is entered only for enterf 1 - v7 enters it for any enterf > 0,
 * #undef's DROP included.
 */
static uint16_t
lookup_(uint16_t namep, int16_t enterf)
{
	uint16_t np, snp, sp;
	int16_t c, i, around;

	np = namep;
	around = 0;
	i = rb(CINIT);
	while ((c = rb(np++)) != 0)
		i = (int16_t)(i + (int16_t)(i + c));
	c = (int16_t)(i % SYMSIZ);
	if (c < 0)
		c = (int16_t)(c + SYMSIZ);
	sp = (uint16_t)(STAB + 4 * c);
	while ((snp = SNAME(sp)) != 0) {
		np = namep;
		while (rb(snp++) == rb(np))
			if (rb(np++) == '\0') {
				if (enterf == DROP) {
					wb(SNAME(sp), DROP);
					SVALUE(sp) = 0;
				}
				return lastsym = sp;
			}
		sp -= 4;
		if (sp < STAB) {
			if (around) {
				pperr1(S_MANYDEF, 0);
				cpp_exit(exfail);
			} else {
				++around;
				sp = (uint16_t)(STAB + 4 * (SYMSIZ - 1));
			}
		}
	}
	if (enterf == 1)
		SNAME(sp) = namep;
	return lastsym = sp;
}

uint16_t
lookup(uint16_t namep, int16_t enterf)
{
	uint16_t bp = fr_enter(2, 8);
	uint16_t r = lookup_(namep, enterf);

	fr_leave(bp, 2);
	return r;
}

/*
 * slookup (1686). Delta: a name is significant to 128 characters (v7:
 * 8).
 */
static uint16_t
slookup_(uint16_t p1, uint16_t p2, int16_t enterf)
{
	uint16_t p3, np;
	int16_t c2, c3;

	c2 = rb(p2);
	wb(p2, 0);	/* mark end of token */
	if ((int16_t)(p2 - p1) > NAMESIG)
		p3 = (uint16_t)(p1 + NAMESIG);
	else
		p3 = p2;
	c3 = rb(p3);
	wb(p3, 0);	/* truncate to 128 chars or less */
	if (enterf == 1)
		p1 = copy(p1);
	np = lookup(p1, enterf);
	wb(p3, c3);
	wb(p2, c2);
	if (SVALUE(np) != 0 && flslvl == 0)
		newp = subst(p2, np, p1);	/* p1: the original's DI */
	else
		newp = 0;
	return np;
}

uint16_t
slookup(uint16_t p1, uint16_t p2, int16_t enterf)
{
	uint16_t bp = fr_enter(3, 8);
	uint16_t r = slookup_(p1, p2, enterf);

	fr_leave(bp, 3);
	return r;
}

/*
 * A word of a frame in the image, read and written byte by byte: the
 * bytes are undefined until written (cpp.h, fr_array()).
 */
static uint16_t
frw(uint16_t a)
{
	return (uint16_t)(UB(a) | UB((uint16_t)(a + 1)) << 8);
}

static void
fww(uint16_t a, uint16_t v)
{
	wb(a, v & 0xff);
	wb((uint16_t)(a + 1), v >> 8);
}

/*
 * subst (1716). Deltas: the linefeeds of a macro call that spans lines.
 * lineno is set back to the call's line after the actuals are read, and
 * the linefeeds come back through the pushed-back text: the first time an
 * actual is pushed (the last place it is used, since the definition is
 * pushed from its end) its linefeeds go with it; where it is pushed
 * again, each linefeed is a blank, and a backslash before it is dropped
 * with it (actflg[]). The linefeeds no actual took back - in a comment
 * between the actuals, or in an actual the body does not use - are
 * pushed in front of the expansion, without a check against the
 * buffer's start.
 *
 * The frame is in the image, laid out as the original's and where the
 * original's is (cpp.h, the stack): acttxt[512] at bp-0x266, actflg[32] at bp-0x66, actual[31]
 * at bp-0x46, params at bp-8, vp at bp-6, the caller's SI and DI at bp-4
 * and bp-2 - slookup()'s p2 and p1, which it does not use again. The
 * original copies an actual without a bound and says "actuals too long"
 * only afterwards (CPP-05): the text runs on over actflg[], actual[],
 * params and vp, which the original then uses as they are - and so does
 * the port. Reaching the saved BP (the frame's end) is refused. A byte
 * 0376 in a macro body (WARN) takes the byte before it as a formal's
 * number, which indexes actual[] and actflg[] with it, wherever that
 * lands in the frame.
 */
static uint16_t
subst_(uint16_t p, uint16_t sp, uint16_t caller_di, uint16_t bp)
{
	const uint16_t acttxt = (uint16_t)(bp - 0x266);	/* space for actuals */
	const uint16_t actflg = (uint16_t)(bp - 0x66);
	const uint16_t actual = (uint16_t)(bp - 0x46);	/* actual[n] is text of nth actual */
	const uint16_t PARAMS = (uint16_t)(bp - 8), VP = (uint16_t)(bp - 6);
	uint16_t ca, pa, a[1];
	int16_t nlines, n;

	fr_array(acttxt, 0x266);
	fww((uint16_t)(bp - 4), p);		/* push si */
	fww((uint16_t)(bp - 2), caller_di);	/* push di */
	nlines = 0;
	fww(VP, SVALUE(sp));
	if (0 == frw(VP))
		goto out;
	if ((int16_t)(p - macforw) <= macdam) {
		if (++maclvl > SYMSIZ && !rflag) {
			pperr1(S_RECURSION, SNAME(sp));
			goto out;
		}
	} else
		maclvl = 0;	/* level decreased */
	macforw = p;
	macdam = 0;	/* new target for decrease in level */
	macnam = SNAME(sp);
	dump();
	if (sp == ulnloc) {
		fww(VP, acttxt);
		wb(frw(VP), 0);
		fww(VP, (uint16_t)(frw(VP) + 1));
		a[0] = (uint16_t)lineno_(ifno);
		cpp_sprintf(frw(VP), S_PCTD, a, 1);
		do
			fww(VP, (uint16_t)(frw(VP) + 1));
		while (rb((uint16_t)(frw(VP) - 1)) != 0);
	} else if (sp == uflloc) {
		fww(VP, acttxt);
		wb(frw(VP), 0);
		fww(VP, (uint16_t)(frw(VP) + 1));
		a[0] = fnames_(ifno);
		cpp_sprintf(frw(VP), S_QPCTS, a, 1);
		do
			fww(VP, (uint16_t)(frw(VP) + 1));
		while (rb((uint16_t)(frw(VP) - 1)) != 0);
	}
	fww(VP, (uint16_t)(frw(VP) - 1));
	fww(PARAMS, (uint16_t)(UB(frw(VP))));
	if (0 != frw(PARAMS)) {	/* definition calls for params */
		ca = acttxt;
		pa = actual;
		if (frw(PARAMS) == 0xFF)
			fww(PARAMS, 1);	/* #define foo() ... */
		sloscan();
		++flslvl;	/* no expansion during search for actuals */
		plvl = -1;
		do
			p = skipbl(p);
		while (rb(inp) == '\n');	/* skip \n too */
		if (rb(inp) == '(') {
			maclin = lineno_(ifno);
			macfil = fnames_(ifno);
			for (plvl = 1; plvl != 0;) {
				wb(ca++, '\0');
				for (;;) {
					outp = inp = p;
					p = cotoken(p);
					if (rb(inp) == '(')
						++plvl;
					if (rb(inp) == ')' && --plvl == 0)
						break;
					if (plvl == 1 && rb(inp) == ',')
						break;
					while (inp < p)
						wb(ca++, rb(inp++));
					if (ca > (uint16_t)(acttxt + BUFSIZ_))
						pperr1(S_ACTLONG, SNAME(sp));
				}
				fww(PARAMS, (uint16_t)(frw(PARAMS) - 1));
				if (pa >= (uint16_t)(actual + 2 * MAXFRM))
					ppwarn(S_MATCH, SNAME(sp));
				else {
					wb((uint16_t)(actflg + (int16_t)(pa - actual) / 2), 0);
					fww(pa, ca);
					pa += 2;
				}
			}
			nlines = (int16_t)(lineno_(ifno) - maclin);
			lineno_(ifno) = maclin;
		}
		if (frw(PARAMS) != 0)
			ppwarn(S_MATCH, SNAME(sp));
		for (;;) {	/* null string for missing actuals */
			fww(PARAMS, (uint16_t)(frw(PARAMS) - 1));
			if ((int16_t)frw(PARAMS) < 0)
				break;
			fww(pa, S_EMPTY1);
			pa += 2;
		}
		--flslvl;
		fasscan();
	}
	for (;;) {	/* push definition onto front of input stack */
		for (;;) {
			fww(VP, (uint16_t)(frw(VP) - 1));
			if (iswarn(rb(frw(VP))))
				break;
			if (bob(p)) {
				outp = inp = p;
				p = unfill(p);
			}
			wb(--p, rb(frw(VP)));
		}
		if (rb(frw(VP)) != rb(WARNC))
			break;
		/* insert actual param */
		fww(VP, (uint16_t)(frw(VP) - 1));
		n = rb(frw(VP));
		ca = frw((uint16_t)(actual + 2 * n - 2));
		while (rb(--ca) != 0) {
			if (bob(p)) {
				outp = inp = p;
				p = unfill(p);
			}
			if (rb(ca) == '\n'
			    && rb((uint16_t)(actflg + rb(frw(VP)) - 1)) != 0) {
				if (rb((uint16_t)(ca - 1)) == '\\')
					--ca;
				else
					wb(--p, ' ');
				continue;
			}
			wb(--p, rb(ca));
			if (rb(ca) == '\n')
				--nlines;
		}
		wb((uint16_t)(actflg + rb(frw(VP)) - 1), 1);
	}
	if (nlines > 0)
		while (nlines-- > 0)
			wb(--p, '\n');
	outp = inp = p;
out:
	return p;
}

uint16_t
subst(uint16_t p, uint16_t sp, uint16_t caller_di)
{
	uint16_t bp = fr_enter(2, 0x266 + 2);
	uint16_t r = subst_(p, sp, caller_di, bp);

	fr_leave(bp, 2);
	return r;
}

/* trmdir (1a76) */
static uint16_t
trmdir_(uint16_t s)
{
	uint16_t p = s;

	while (rb(p++) != 0)
		;
	--p;
	while (p > s && rb(--p) != '/')
		;
	if (p == s)
		wb(p++, '.');
	wb(p, '\0');
	return s;
}

uint16_t
trmdir(uint16_t s)
{
	uint16_t bp = fr_enter(1, 0);
	uint16_t r = trmdir_(s);

	fr_leave(bp, 1);
	return r;
}

/* copy (1aa4) */
static uint16_t
copy_(uint16_t s)
{
	uint16_t old;
	int16_t c;

	old = savch;
	do {
		c = rb(s++);
		wb(savch++, c);
	} while (c != 0);
	return old;
}

uint16_t
copy(uint16_t s)
{
	uint16_t bp = fr_enter(1, 0);
	uint16_t r = copy_(s);

	fr_leave(bp, 1);
	return r;
}

/* strdex (1ac6) */
static uint16_t
strdex_(uint16_t s, int16_t c)
{
	while (rb(s) != 0)
		if ((int8_t)rb(s++) == (int8_t)c)
			return --s;
	return 0;
}

uint16_t
strdex(uint16_t s, int16_t c)
{
	uint16_t bp = fr_enter(2, 0);
	uint16_t r = strdex_(s, c);

	fr_leave(bp, 2);
	return r;
}

/* yywrap (1af0) is never called. */

/*
 * main (1afc). Deltas: '$' is an identifier character; the input's
 * directory is "." until a file is named (v7: none); the predefined
 * name is "mutos" (v7: "unix" and "pdp11"); the scanning buffer starts
 * 128 bytes into buffer[] (v7: 8). Kept from v7: the 21st -D option is
 * stored in punspc[0], a 21st -U in exfail, a ninth -I in dirs[9] (the
 * default directory and the end mark go to dirs[10], which is pflag) -
 * the image has them where the original has them (CPP-03).
 */
static int16_t
cppmain_(int16_t argc, uint16_t argv)
{
	int16_t i, c;
	uint16_t p, tf, cp2, a;

	for (p = S_IDCHARS; (c = rb(p++)) != 0;) {
		wb((uint16_t)(FASTAB + c), rb((uint16_t)(FASTAB + c)) | (IB | NB | SB));
		wb((uint16_t)(TOKTYP + c), IDENT);
	}
	for (p = S_NUMCHARS; (c = rb(p++)) != 0;) {
		wb((uint16_t)(FASTAB + c), rb((uint16_t)(FASTAB + c)) | (NB | SB));
		wb((uint16_t)(TOKTYP + c), NUMBR);
	}
	for (p = S_SBCHARS; (c = rb(p++)) != 0;)
		wb((uint16_t)(FASTAB + c), rb((uint16_t)(FASTAB + c)) | SB);
	for (p = S_QBCHARS; (c = rb(p++)) != 0;)
		wb((uint16_t)(FASTAB + c), rb((uint16_t)(FASTAB + c)) | QB);
	for (p = S_CBCHARS; (c = rb(p++)) != 0;)
		wb((uint16_t)(FASTAB + c), rb((uint16_t)(FASTAB + c)) | CB);
	c = rb(WARNC);
	wb((uint16_t)(FASTAB + c), rb((uint16_t)(FASTAB + c)) | WB);
	wb(FASTAB, rb(FASTAB) | (CB | QB | SB | WB));
	for (i = 256; --i >= 0;)
		wb((uint16_t)(SLOTAB + i), rb((uint16_t)(FASTAB + i)) | SB);
	for (p = S_BLANKS; (c = rb(p++)) != 0;)
		wb((uint16_t)(TOKTYP + c), BLANK);

	ifno = 0;
	fnames_(0) = S_EMPTY;
	dirs_(0) = dirnams_(0) = S_DOT;
	for (i = 1; i < argc; i++) {
		a = AU(argv, i);
		if (rb(a) == '-') {
			switch (rb((uint16_t)(a + 1))) {
			case 'P':
				pflag++;
				continue;
			case 'E':
				continue;
			case 'R':
				++rflag;
				continue;
			case 'C':
				passcom++;
				continue;
			case 'D':
				if (predef > (uint16_t)(PRESPC + 2 * NPREDEF)) {
					pperr1(S_MANYD, a);
					continue;
				}
				/* ignore plain "-D" (no argument) */
				if (rb((uint16_t)(a + 2)) != 0) {
					WU(predef) = (uint16_t)(a + 2);
					predef += 2;
				}
				continue;
			case 'U':
				if (prund > (uint16_t)(PUNSPC + 2 * NPREDEF)) {
					pperr1(S_MANYU, a);
					continue;
				}
				WU(prund) = (uint16_t)(a + 2);
				prund += 2;
				continue;
			case 'I':
				if (nd > 8)
					pperr1(S_EXCESSI, a);
				else
					dirs_(nd++) = (uint16_t)(a + 2);
				continue;
			case '\0':
				continue;
			default:
				pperr1(S_UNKFLAG, a);
				continue;
			}
		}
		if (fin == 0) {
			if (0 > (fin = cpp_open(a, 0))) {
				pperr1(S_NOSOURCE, a);
				cpp_exit(8);
			}
			fnames_(ifno) = copy(a);
			dirnams_(ifno) = trmdir(a);
			dirs_(0) = dirnams_(ifno);
		} else if (fout == STDOUT_F) {
			if (0 == (fout = cpp_fopen_w(a))) {
				pperr1(S_CANTCREATE, a);
				cpp_exit(8);
			} else
				cpp_fclose_stdout();	/* and setbuf(fout, _sobuf) */
		} else
			pperr1(S_EXTRANEOUS, a);
	}

	fins_(ifno) = fin;
	exfail = 0;
	/* after user -I files here are the standard include libraries */
	dirs_(nd++) = S_USRINCLUDE;
	dirs_(nd++) = 0;
	defloc = ppsym(S_DEFINE);
	udfloc = ppsym(S_UNDEF);
	incloc = ppsym(S_INCLUDE);
	elsloc = ppsym(S_ELSE);
	eifloc = ppsym(S_ENDIF);
	ifdloc = ppsym(S_IFDEF);
	ifnloc = ppsym(S_IFNDEF);
	ifloc = ppsym(S_IF);
	lneloc = ppsym(S_LINEKW);
	for (i = 0x10b; --i >= 0;)
		wb((uint16_t)(MACBIT + i), 0);
	ysysloc = stsym(S_MUTOS);
	ulnloc = stsym(S_ULINE);
	uflloc = stsym(S_UFILE);

	tf = fnames_(ifno);
	fnames_(ifno) = S_CMDLINE;
	lineno_(ifno) = 1;
	cp2 = PRESPC;
	while (cp2 < predef) {
		uint16_t s = WU(cp2);

		cp2 += 2;
		stsym(s);
	}
	cp2 = PUNSPC;
	while (cp2 < prund) {
		if ((p = strdex(WU(cp2), '=')) != 0)
			wb(p++, '\0');
		lookup(WU(cp2), DROP);
		cp2 += 2;
	}
	fnames_(ifno) = tf;
	pbeg = (uint16_t)(BUFFER + 128);
	pbuf = (uint16_t)(pbeg + BUFSIZ_);
	pend = (uint16_t)(pbuf + BUFSIZ_);

	trulvl = 0;
	flslvl = 0;
	AW(LINENO, 0) = 1;
	sayline();
	outp = inp = pend;
	control(pend);
	return exfail;
}

int16_t
cppmain(int16_t argc, uint16_t argv)
{
	uint16_t bp = fr_enter(3, 6);
	int16_t r = cppmain_(argc, argv);

	fr_leave(bp, 3);
	return r;
}
