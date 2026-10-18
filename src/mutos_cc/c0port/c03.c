/*
 * c03.c - declarations: v7/cc/c03.c as the original MUTOS 1700 c0 has
 * it (text 3312..41ec, tests/realbin/c0map.txt). The MUTOS deltas are in
 * decl1() and getype() (a parameter list is read only right after the
 * declarator's name) and goodreg() (no char register variables; a SETREG
 * for each register).
 */
#include "c0.h"

/*
 * 3312 declist - same. Process a sequence of declaration statements.
 * The storage class is passed to getkeywords() by address, so it lives
 * in the image.
 */
int16_t
declist(int16_t sclass)
{
	int16_t sc, offset;
	uint16_t typer, psclass;

	typer = salloc(SZ_HSHTAB);
	psclass = salloc(2);
	WI(psclass) = sclass;
	offset = 0;
	sc = sclass;
	while (getkeywords(psclass, typer)) {
		offset = declare(WI(psclass), typer, offset);
		WI(psclass) = sc;
	}
	offset = (int16_t)(offset + align(INT, offset, 0));
	sfree(2);
	sfree(SZ_HSHTAB);
	return offset;
}

/*
 * 335a getkeywords - same (a jump table on cval). Read the keywords
 * introducing a declaration statement. Store back the storage class,
 * and fill in the type entry, which looks like a hash table entry.
 */
int16_t
getkeywords(uint16_t scptr, uint16_t tptr)
{
	int16_t skw, tkw, longf;
	int16_t o, isadecl, ismos, unsignf;

	isadecl = 0;
	longf = 0;
	unsignf = 0;
	HTYP(tptr) = INT;
	HSTRP(tptr) = 0;
	HSUBSP(tptr) = 0;
	tkw = -1;
	skw = WI(scptr);
	ismos = skw == MOS || skw == MOU;
	for (;;) {
		mosflg = ismos && isadecl;
		o = symbol();
		if (o == NAME && HCLASS(csym) == TYPEDEF && tkw < 0) {
			tkw = HTYP(csym);
			HSUBSP(tptr) = HSUBSP(csym);
			HSTRP(tptr) = HSTRP(csym);
			isadecl++;
			continue;
		}
		switch (o == KEYW ? cval : -1) {
		case AUTO:
		case STATIC:
		case EXTERN:
		case REG:
		case TYPEDEF:
			if (skw && skw != cval) {
				if (skw == ARG && cval == REG)
					cval = AREG;
				else
					error("Conflict in storage class");
			}
			skw = cval;
			break;

		case UNSIGN:
			unsignf++;
			break;

		case LONG:
			longf++;
			break;

		case ENUM:
			strdec(ismos, cval, skw);
			cval = INT;
			goto types;

		case UNION:
		case STRUCT:
			HSTRP(tptr) = strdec(ismos, cval, skw);
			cval = STRUCT;
			/* FALLTHROUGH */
		case INT:
		case CHAR:
		case FLOAT:
		case DOUBLE:
		types:
			if (tkw >= 0)
				error("Type clash");
			tkw = cval;
			break;

		default:
			peeksym = o;
			if (isadecl == 0)
				return 0;
			if (tkw < 0)
				tkw = INT;
			if (skw == 0)
				skw = blklev == 0 ? DEFXTRN : AUTO;
			if (unsignf) {
				if (tkw == INT)
					tkw = UNSIGN;
				else
					error("Misplaced 'unsigned'");
			}
			if (longf) {
				if (tkw == FLOAT)
					tkw = DOUBLE;
				else if (tkw == INT)
					tkw = LONG;
				else
					error("Misplaced 'long'");
			}
			WI(scptr) = skw;
			HTYP(tptr) = tkw;
			return 1;
		}
		isadecl++;
	}
}

/*
 * *memlist++ = v: a member into strdec()'s mems[NMEMS], an array in the
 * original's stack frame. "Too many structure members" moves memlist
 * back by NMEMS/2 when the members counted reach NMEMS, but structhole
 * entries (alignment gaps) are not counted: with enough of them memlist
 * runs past the array and over strdec()'s own frame - refused there.
 */
static uint16_t c0_mems_end;

static void
memlist_put(uint16_t v)
{
	if (memlist >= c0_mems_end)
		c0trap("a member list past the end of strdec()'s mems[] (the "
		    "original writes over its own stack frame)");
	WU(memlist) = v;
	memlist += 2;
}

/*
 * 3564 strdec - same. Process a structure, union, or enum declaration;
 * a subroutine of getkeywords. Its elsize is never set for an enum (v7
 * too): the original's register DI holds what its caller left there -
 * getkeywords()'s skw, passed here as di_elsize - and that becomes the
 * enum's ssize (seen only by "%.8s redeclared" when the same enum tag is
 * given a body twice).
 */
uint16_t
strdec(int16_t mosf, int16_t kind, int16_t di_elsize)
{
	int16_t elsize, o;
	uint16_t ssym;
	int16_t savebits;
	uint16_t savememlist;
	int16_t savenmems;
	uint16_t strp;
	uint16_t ds;
	uint16_t mems;
	uint16_t typer;
	int16_t tagkind;
	uint16_t savememsend;

	elsize = di_elsize;
	mems = salloc(NMEMS * 2);
	typer = salloc(SZ_HSHTAB);
	if (kind != ENUM) {
		tagkind = STRTAG;
		mosflg = 1;
	} else
		tagkind = ENUMTAG;
	ssym = 0;
	if ((o = symbol()) == NAME) {
		ssym = csym;
		mosflg = mosf;
		o = symbol();
		if (o == LBRACE && HBLKLEV(ssym) < blklev)
			pushdecl(ssym);
		if (HCLASS(ssym) == 0) {
			HCLASS(ssym) = (signed char)tagkind;
			HSTRP(ssym) = gblock(SZ_STR);
			funcbase = curbase;
			STRSIZE(HSTRP(ssym)) = 0;
			MEMLIST(HSTRP(ssym)) = 0;
		}
		if (HCLASS(ssym) != tagkind)
			redec();
		strp = HSTRP(ssym);
	} else {
		strp = gblock(SZ_STR);
		funcbase = curbase;
		STRSIZE(strp) = 0;
		MEMLIST(strp) = 0;
	}
	mosflg = 0;
	if (o != LBRACE) {
		if (ssym == 0)
			goto syntax;
		if (HCLASS(ssym) != tagkind)
			error("Bad structure/union/enum name");
		peeksym = o;
	} else {
		ds = defsym;
		mosflg = 0;
		savebits = bitoffs;
		savememlist = memlist;
		savenmems = nmems;
		savememsend = c0_mems_end;
		c0_mems_end = (uint16_t)(mems + NMEMS * 2);
		memlist = mems;
		nmems = 2;
		bitoffs = 0;
		c0_members++;
		if (kind == ENUM) {
			HTYP(typer) = INT;
			HSTRP(typer) = strp;
			declare(ENUM, typer, 0);
		} else
			elsize = declist(kind == UNION ? MOU : MOS);
		c0_members--;
		bitoffs = savebits;
		defsym = ds;
		if (STRSIZE(strp))
			error("%.8s redeclared", HNAME(ssym));
		STRSIZE(strp) = elsize;
		memlist_put(0);
		MEMLIST(strp) = gblock((int16_t)(memlist - mems));
		funcbase = curbase;
		for (o = 0; mems + 2 * o != memlist; o++)
			WU(MEMLIST(strp) + 2 * o) = WU(mems + 2 * o);
		memlist = savememlist;
		nmems = savenmems;
		c0_mems_end = savememsend;
		if ((o = symbol()) != RBRACE)
			goto syntax;
	}
	sfree(SZ_HSHTAB);
	sfree(NMEMS * 2);
	return strp;
syntax:
	decsyn(o);
	sfree(SZ_HSHTAB);
	sfree(NMEMS * 2);
	return 0;
}

/*
 * 37b6 declare - same. Process a comma-separated list of declarators.
 */
int16_t
declare(int16_t askw, uint16_t tptr, int16_t offset)
{
	int16_t o;
	int16_t skw, isunion;

	skw = askw;
	isunion = 0;
	if (skw == MOU) {
		skw = MOS;
		isunion++;
		mosflg = 1;
		if ((peeksym = symbol()) == SEMI) {
			o = length(tptr);
			if (o > offset)
				offset = o;
		}
	}
	do {
		if (skw == ENUM && (peeksym = symbol()) == RBRACE) {
			o = peeksym;
			peeksym = -1;
			break;
		}
		o = decl1(skw, tptr, isunion ? 0 : offset, 0);
		if (isunion) {
			o = (int16_t)(o + align(CHAR, o, 0));
			if (o > offset)
				offset = o;
		} else
			offset = (int16_t)(offset + o);
	} while ((o = symbol()) == COMMA);
	if (o == RBRACE) {
		peeksym = o;
		o = SEMI;
	}
	if (o != SEMI && (o != RPARN || skw != ARG1))
		decsyn(o);
	return offset;
}

/*
 * 3882 decl1 - delta: getype()'s name flag is cleared first. Process a
 * single declarator.
 *
 * As in v7, "redec(); goto syntax;" returns elsize before anything has
 * set it - a word of the original's stack, left by earlier calls. Inside
 * a structure's, union's or enum's list (strdec(), c0_members) that word
 * becomes the offset of what follows and the size: the port refuses;
 * elsewhere (a block's or the parameters' declarations, a parameter
 * list) the caller drops it.
 */
int16_t
decl1(int16_t askw, uint16_t atptr, int16_t offset, uint16_t absname)
{
	int16_t t1, chkoff, a, elsize;
	int16_t skw;
	int16_t type;
	uint16_t dsym;
	uint16_t tptr;
	uint16_t dim;
	uint16_t dp;
	int16_t isinit;

	elsize = 0;
	skw = askw;
	tptr = atptr;
	chkoff = 0;
	mosflg = skw == MOS;
	dim = salloc(SZ_TDIM);
	RANK(dim) = 0;
	if (((peeksym = symbol()) == SEMI || peeksym == RPARN) && absname == 0)
		goto out0;
	/*
	 * Filler field
	 */
	if (peeksym == COLON && skw == MOS) {
		peeksym = -1;
		t1 = conexp();
		elsize = align(HTYP(tptr), offset, t1);
		bitoffs = (int16_t)(bitoffs + t1);
		goto out;
	}
	getype_name = 0;
	t1 = getype(dim, absname);
	if (t1 == -1)
		goto out0;
	if (HSUBSP(tptr)) {
		type = HTYP(tptr);
		for (a = 0; type & XTYPE;) {
			if ((type & XTYPE) == ARRAY) {
				DIMENS(dim, RANK(dim)) = WI(HSUBSP(tptr) + 2 * a);
				RANK(dim)++;
				a++;
			}
			type = (int16_t)(type >> TYLEN);
		}
	}
	type = (int16_t)(HTYP(tptr) & ~TYPE);
	while (t1 & XTYPE) {
		if (type & BIGTYPE) {
			typov();
			type = t1 = 0;
		}
		type = (int16_t)(type << TYLEN | (t1 & XTYPE));
		t1 = (int16_t)(t1 >> TYLEN);
	}
	type = (int16_t)(type | (HTYP(tptr) & TYPE));
	if (absname)
		defsym = absname;
	dsym = defsym;
	if (HBLKLEV(dsym) < blklev)
		pushdecl(dsym);
	if (RANK(dim) == 0)
		HSUBSP(dsym) = 0;
	else {
		dp = gblock((int16_t)(RANK(dim) * 2));
		funcbase = curbase;
		if (skw == EXTERN)
			maxdecl = curbase;
		for (a = 0; a < RANK(dim); a++) {
			if ((t1 = WI(dp + 2 * a) = DIMENS(dim, a)) != 0
			 && (HTYP(dsym) & XTYPE) == ARRAY
			 && WI(HSUBSP(dsym) + 2 * a) && t1 != WI(HSUBSP(dsym) + 2 * a))
				redec();
		}
		HSUBSP(dsym) = dp;
	}
	if ((type & XTYPE) == FUNC) {
		if (skw == AUTO)
			skw = EXTERN;
		if ((skw != EXTERN && skw != TYPEDEF) && absname == 0)
			error("Bad func. storage class");
	}
	if (!(HCLASS(dsym) == 0
	   || ((skw == ARG || skw == AREG) && HCLASS(dsym) == ARG1)
	   || (skw == EXTERN && HCLASS(dsym) == EXTERN && HTYP(dsym) == type))) {
		if (skw == MOS && HCLASS(dsym) == MOS && HTYP(dsym) == type)
			chkoff = 1;
		else {
			redec();
			if (c0_members)
				c0trap("a redeclaration in a member or enum list: the "
				    "original returns an uninitialized word as "
				    "its size");
			goto out;
		}
	}
	if (HCLASS(dsym) && (HTYP(dsym) & TYPE) == STRUCT
	 && (type & TYPE) == STRUCT)
		if (HSTRP(dsym) != HSTRP(tptr)) {
			error("Warning: structure redeclaration");
			nerror--;
		}
	HTYP(dsym) = type;
	if (HSTRP(tptr))
		HSTRP(dsym) = HSTRP(tptr);
	if (skw == TYPEDEF) {
		HCLASS(dsym) = TYPEDEF;
		goto out0;
	}
	if (absname)
		goto out0;
	if (skw == ARG1) {
		if (paraml == 0)
			paraml = dsym;
		else
			HOFFSET(parame) = (int16_t)dsym;
		parame = dsym;
		HCLASS(dsym) = (signed char)skw;
		goto out0;
	}
	elsize = 0;
	if (skw == MOS) {
		elsize = length(dsym);
		if ((peeksym = symbol()) == COLON) {
			elsize = 0;
			peeksym = -1;
			t1 = conexp();
			a = align(type, offset, t1);
			if (HFLAG(dsym) & FFIELD) {
				if (BITOFFS(HSTRP(dsym)) != bitoffs
				 || FLEN(HSTRP(dsym)) != t1)
					redec();
			} else {
				HSTRP(dsym) = gblock(SZ_FIELD);
				funcbase = curbase;
			}
			HFLAG(dsym) = (signed char)(HFLAG(dsym) | FFIELD);
			BITOFFS(HSTRP(dsym)) = bitoffs;
			FLEN(HSTRP(dsym)) = t1;
			bitoffs = (int16_t)(bitoffs + t1);
		} else
			a = align(type, offset, 0);
		elsize = (int16_t)(elsize + a);
		offset = (int16_t)(offset + a);
		if (++nmems >= NMEMS) {
			error("Too many structure members");
			nmems -= NMEMS / 2;
			memlist -= NMEMS / 2 * 2;
		}
		if (a)
			memlist_put(STRUCTHOLE);
		if (chkoff && HOFFSET(dsym) != offset)
			redec();
		HOFFSET(dsym) = offset;
		memlist_put(dsym);
	}
	if (skw == REG)
		if ((HOFFSET(dsym) = goodreg(dsym)) < 0)
			skw = AUTO;
	HCLASS(dsym) = (signed char)skw;
	isinit = 0;
	if ((a = symbol()) != COMMA && a != SEMI && a != RBRACE)
		isinit++;
	if (a != ASSIGN)
		peeksym = a;
	if (skw == AUTO) {
		autolen = (uint16_t)(autolen - rlength(dsym));
		HOFFSET(dsym) = (int16_t)autolen;
		if (autolen < maxauto)
			maxauto = autolen;
		if (isinit)
			cinit(dsym, 0, AUTO);
	} else if (skw == STATIC) {
		HOFFSET(dsym) = isn;
		if (isinit) {
			outcode("BBN", DATA, LABEL, isn++);
			if (cinit(dsym, 1, STATIC) & ALIGN)
				outcode("B", EVEN);
		} else {
			a = rlength(dsym);
			outcode("BBNBN", BSS, LABEL, isn++, SSPACE, a);
		}
		outcode("B", PROG);
	} else if (skw == REG && isinit)
		cinit(dsym, 0, REG);
	else if (skw == ENUM) {
		if (type != INT)
			error("Illegal enumeration %.8s", HNAME(dsym));
		HCLASS(dsym) = ENUMCON;
		HOFFSET(dsym) = offset;
		if (isinit)
			cinit(dsym, 0, ENUMCON);
		elsize = (int16_t)(HOFFSET(dsym) - offset + 1);
	}
	prste(dsym);
out:
	sfree(SZ_TDIM);
	return elsize;
out0:
	sfree(SZ_TDIM);
	return 0;
}

/*
 * 3df0 pushdecl - same. Push down an outer-block declaration after
 * redeclaration in an inner block.
 */
void
pushdecl(uint16_t asp)
{
	uint16_t sp, nsp;

	sp = asp;
	nsp = gblock(SZ_PHSHTAB);
	maxdecl = funcbase = curbase;
	cpysymb(nsp, sp);
	HCLASS(sp) = 0;
	HFLAG(sp) = (signed char)(HFLAG(sp) & (FKEYW | FMOS));
	HTYP(sp) = 0;
	HOFFSET(sp) = 0;
	HBLKLEV(sp) = (signed char)blklev;
	HPDOWN(sp) = nsp;
}

/*
 * 3e36 cpysymb - same. Copy the non-name part of a symbol.
 */
void
cpysymb(uint16_t s1, uint16_t s2)
{
	uint16_t rs1, rs2;

	rs1 = s1;
	rs2 = s2;
	HCLASS(rs1) = HCLASS(rs2);
	HFLAG(rs1) = HFLAG(rs2);
	HTYP(rs1) = HTYP(rs2);
	HOFFSET(rs1) = HOFFSET(rs2);
	HSUBSP(rs1) = HSUBSP(rs2);
	HSTRP(rs1) = HSTRP(rs2);
	HBLKLEV(rs1) = HBLKLEV(rs2);
	HPDOWN(rs1) = HPDOWN(rs2);
}

/*
 * 3e72 getype - delta. Read a declarator and get the implied type.
 * MUTOS: a static flag (data 40c2) records that the declarator's name
 * has just been read; a parameter list "(...)" at the outer level is
 * declared only then - not after "[...]" (which clears the flag), and
 * only once (the list clears it too). Otherwise "(" must be followed by
 * ")".
 */
int16_t
getype(uint16_t adimp, uint16_t absname)
{
	int16_t type;
	int16_t o;
	uint16_t ds;
	uint16_t dimp;

	ds = defsym;
	dimp = adimp;
	type = 0;
	switch (o = symbol()) {

	case TIMES:
		type = getype(dimp, absname);
		if (type == -1)
			return type;
		if (type & BIGTYPE) {
			typov();
			type = 0;
		}
		return (int16_t)(type << TYLEN | PTR);

	case LPARN:
		if (absname == 0 || nextchar() != ')') {
			type = getype(dimp, absname);
			if (type == -1)
				return type;
			ds = defsym;
			if ((o = symbol()) != RPARN)
				goto syntax;
			goto getf;
		}
		/* FALLTHROUGH */

	default:
		peeksym = o;
		if (absname) {
			defsym = ds = absname;
			absname = 0;
			goto getf;
		}
		break;

	case NAME:
		getype_name = 1;
		defsym = ds = csym;
	getf:
		switch (o = symbol()) {

		case LPARN:
			if (blklev == 0 && getype_name) {
				int members = c0_members;

				c0_members = 0;	/* declare()'s result is dropped */
				blklev++;
				ds = defsym;
				declare(ARG1, ARGTYPE, 0);
				defsym = ds;
				blklev--;
				getype_name = 0;
				c0_members = members;
			} else
				if ((o = symbol()) != RPARN)
					goto syntax;
			if (type & BIGTYPE) {
				typov();
				type = 0;
			}
			type = (int16_t)(type << TYLEN | FUNC);
			goto getf;

		case LBRACK:
			getype_name = 0;
			if (RANK(dimp) >= 5) {
				error("Rank too large");
				RANK(dimp) = 4;
			}
			if ((o = symbol()) != RBRACK) {
				peeksym = o;
				cval = conexp();
				defsym = ds;
				if ((o = symbol()) != RBRACK)
					goto syntax;
			} else {
				if (RANK(dimp) != 0)
					error("Null dimension");
				cval = 0;
			}
			DIMENS(dimp, RANK(dimp)) = cval;
			RANK(dimp)++;
			if (type & BIGTYPE) {
				typov();
				type = 0;
			}
			type = (int16_t)(type << TYLEN | ARRAY);
			goto getf;
		}
		peeksym = o;
		return type;
	}
syntax:
	decsyn(o);
	return -1;
}

/*
 * 405a typov - same. More bits required for type than allowed.
 */
void
typov(void)
{
	error("Type is too complicated");
}

/*
 * 406c align - same. Enforce alignment restrictions in structures,
 * including bit-field considerations.
 */
int16_t
align(int16_t type, int16_t offset, int16_t aflen)
{
	int16_t a, t, flen;
	const char *ftl;

	flen = aflen;
	a = offset;
	t = type;
	ftl = "Field too long";
	if (flen == 0) {
		a = (int16_t)(a + (NBPC + bitoffs - 1) / NBPC);
		bitoffs = 0;
	}
	while ((t & XTYPE) == ARRAY)
		t = decref(t);
	if (t != CHAR) {
		a = (int16_t)((a + ALIGN) & ~ALIGN);
		if (a > offset)
			bitoffs = 0;
	}
	if (flen) {
		if (type == INT || type == UNSIGN) {
			if (flen > NBPW)
				error(ftl);
			if (flen + bitoffs > NBPW) {
				bitoffs = 0;
				a = (int16_t)(a + NCPW);
			}
		} else if (type == CHAR) {
			if (flen > NBPC)
				error(ftl);
			if (flen + bitoffs > NBPC) {
				bitoffs = 0;
				a = (int16_t)(a + 1);
			}
		} else
			error("Bad type for field");
	}
	return (int16_t)(a - offset);
}

/*
 * 4140 decsyn - same. Complain about syntax error in declaration.
 */
void
decsyn(int16_t o)
{
	error("Declaration syntax");
	errflush(o);
}

/*
 * 415c redec - same. Complain about a redeclaration.
 */
void
redec(void)
{
	error("%.8s redeclared", HNAME(defsym));
}

/*
 * 4176 goodreg - delta: a char is not a register candidate (v7 lets
 * one in), and every register handed out is announced to c1 at once by
 * "SETREG regvar". Determine if a variable is suitable for storage in a
 * register; if so return the register number.
 */
int16_t
goodreg(uint16_t hp)
{
	int16_t type;

	type = HTYP(hp);
	/*
	 * Special dispensation for unions
	 */
	if (type == STRUCT && length(hp) <= SZINT)
		type = INT;
	if ((type != INT && type != UNSIGN && (type & XTYPE) == 0)
	 || (type & XTYPE) > PTR || regvar < 3)
		return -1;
	--regvar;
	outcode("BN", SETREG, regvar);
	return regvar;
}
