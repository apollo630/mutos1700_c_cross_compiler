/*
 * c02.c - external definitions, functions, initializers and statements:
 * v7/cc/c02.c as the original MUTOS 1700 c0 has it (text 2202..3312,
 * tests/realbin/c0map.txt). The MUTOS deltas: a static external without
 * v7's SYMDEF "", a function's code after an EVEN, its RETRN followed by
 * the function's type, two register variables (regvar 4) and a frame
 * starting at -4, the SETREG of the parameters written first and none
 * at a block's head (goodreg() writes them).
 */
#include "c0.h"

/*
 * 2202 extdef - delta (a static external: BSS NLABEL name SSPACE size,
 * without v7's leading SYMDEF ""). Process a single external
 * definition.
 */
void
extdef(void)
{
	int16_t o;
	uint16_t sclass;
	int16_t scflag;
	uint16_t cb;
	uint16_t typer;
	uint16_t ds;

	if (((o = symbol()) == EOFC) || o == SEMI)
		return;
	typer = salloc(SZ_HSHTAB);
	sclass = salloc(2);
	peeksym = o;
	WI(sclass) = 0;
	blklev = 0;
	if (getkeywords(sclass, typer) == 0) {
		WI(sclass) = EXTERN;
		if (peeksym != NAME)
			goto syntax;
	}
	scflag = 0;
	if (WI(sclass) == DEFXTRN) {
		scflag++;
		WI(sclass) = EXTERN;
	}
	if (WI(sclass) != EXTERN && WI(sclass) != STATIC && WI(sclass) != TYPEDEF)
		error("Illegal storage class");
	do {
		defsym = 0;
		paraml = 0;
		parame = 0;
		if (WI(sclass) == TYPEDEF) {
			decl1(TYPEDEF, typer, 0, 0);
			continue;
		}
		decl1(EXTERN, typer, 0, 0);
		if ((ds = defsym) == 0)
			goto out;
		funcsym = ds;
		if ((HTYP(ds) & XTYPE) == FUNC) {
			if ((peeksym = symbol()) == LBRACE || peeksym == KEYW
			 || (peeksym == NAME && HCLASS(csym) == TYPEDEF)) {
				TYP(FUNCBLK) = decref(HTYP(ds));
				STRP(FUNCBLK) = HSTRP(ds);
				setinit(ds);
				outcode("BS", SYMDEF,
				    WI(sclass) == EXTERN ? HNAME(ds) : EMPTYSTR);
				cfunc();
				goto out;
			}
			if (paraml)
				error("Inappropriate parameters");
		} else if ((o = symbol()) == COMMA || o == SEMI) {
			peeksym = o;
			o = (int16_t)((length(ds) + ALIGN) & ~ALIGN);
			if (WI(sclass) == STATIC) {
				setinit(ds);
				outcode("BBSBN", BSS, NLABEL, HNAME(ds), SSPACE, o);
			} else if (scflag)
				outcode("BSN", CSPACE, HNAME(ds), o);
		} else {
			if (o != ASSIGN)
				peeksym = o;
			setinit(ds);
			if (WI(sclass) == EXTERN)
				outcode("BS", SYMDEF, HNAME(ds));
			outcode("BBS", DATA, NLABEL, HNAME(ds));
			cb = funcbase;
			if (cinit(ds, 1, WI(sclass)) & ALIGN)
				outcode("B", EVEN);
			if (maxdecl > cb)
				cb = maxdecl;
			funcbase = cb;
		}
	} while ((o = symbol()) == COMMA);
	if (o == SEMI)
		goto out;
syntax:
	if (o == RBRACE) {
		error("Too many }'s");
		peeksym = 0;
		goto out;
	}
	error("External definition syntax");
	errflush(o);
	statement();
out:
	sfree(2);
	sfree(SZ_HSHTAB);
}

/*
 * 2482 cfunc - delta: "BBBS" PROG EVEN RLABEL name (v7: no EVEN);
 * regvar starts at 4 (v7: 5) and autolen at STAUTO -4 (v7: -6) - the
 * 8086's two register variables, DI and SI, saved below BP; the return
 * label's RETRN is followed by the function's type (funcblk.type).
 * Process a function definition.
 */
void
cfunc(void)
{
	uint16_t cb;
	int16_t sloc;

	sloc = isn;
	isn += 2;
	outcode("BBBS", PROG, EVEN, RLABEL, HNAME(funcsym));
	if (proflg)
		outcode("BN", PROFIL, isn++);
	cb = curbase;
	regvar = 4;
	autolen = (uint16_t)STAUTO;
	maxauto = (uint16_t)STAUTO;
	blklev = 1;
	declist(ARG);
	outcode("B", SAVE);
	funchead();
	branch(sloc);
	label((int16_t)(sloc + 1));
	retlab = isn++;
	blklev = 0;
	if ((peeksym = symbol()) != LBRACE)
		error("Compound statement required");
	statement();
	outcode("BNBN", LABEL, retlab, RETRN, TYP(FUNCBLK));
	label(sloc);
	outcode("BN", SETSTK, (int16_t)-maxauto);
	branch((int16_t)(sloc + 1));
	if (cb < maxdecl)
		cb = maxdecl;
	curbase = funcbase = cb;
}

/*
 * 2592 cinit - same. Process the initializers for an external
 * definition.
 */
int16_t
cinit(uint16_t anp, int16_t flex, int16_t sclass)
{
	uint16_t np;
	int16_t nel, ninit;
	int16_t width, isarray, o, brace, realtype;
	uint16_t cb;
	uint16_t s;

	cb = funcbase;
	np = gblock(SZ_PHSHTAB);
	funcbase = curbase;
	cpysymb(np, anp);
	realtype = HTYP(np);
	isarray = 0;
	if ((realtype & XTYPE) == ARRAY)
		isarray++;
	else
		flex = 0;
	width = length(np);
	nel = 1;
	/*
	 * If it's an array, find the number of elements.
	 * temporarily modify to look like kind of thing it's
	 * an array of.
	 */
	if (sclass == AUTO)
		if (isarray || realtype == STRUCT)
			error("No auto. aggregate initialization");
	if (isarray) {
		HTYP(np) = decref(realtype);
		HSUBSP(np) += 2;
		if (width == 0 && flex == 0)
			error("0-length row: %.8s", HNAME(anp));
		o = length(np);
		/* nel = ldiv(0, width, o); */
		if (o == 0)
			c0trap("an array of 0-length elements initialized (the "
			    "original divides by 0)");
		nel = (int16_t)((uint16_t)width / (uint16_t)o);
		width = o;
	}
	brace = 0;
	if ((peeksym = symbol()) == LBRACE && (isarray || HTYP(np) != STRUCT)) {
		peeksym = -1;
		brace++;
	}
	ninit = 0;
	do {
		if ((o = symbol()) == RBRACE)
			break;
		peeksym = o;
		if (o == STRING && realtype == ARRAY + CHAR) {
			if (sclass == AUTO)
				error("No strings in automatic");
			peeksym = -1;
			putstr(0, flex ? 10000 : nel);
			ninit = (int16_t)(ninit + nchstr);
			o = symbol();
			break;
		} else if (HTYP(np) == STRUCT) {
			strinit(np, sclass);
		} else if ((HTYP(np) & ARRAY) == ARRAY || peeksym == LBRACE)
			cinit(np, 0, sclass);
		else {
			initflg++;
			s = tree();
			initflg = 0;
			if (HFLAG(np) & FFIELD)
				error("No field initialization");
			WU(cp) = nblock(np);
			cp += 2;
			WU(cp) = s;
			cp += 2;
			build(ASSIGN);
			if (sclass == AUTO || sclass == REG) {
				cp -= 2;
				rcexpr(WU(cp));
			} else if (sclass == ENUMCON) {
				if (OP(s) != CON)
					error("Illegal enum constant for %.8s",
					    HNAME(anp));
				HOFFSET(anp) = VALUE(s);
			} else {
				cp -= 2;
				rcexpr(block(INIT, HTYP(np), 0, 0, TR2(WU(cp)), 0));
			}
		}
		ninit++;
		if ((ninit & 077) == 0 && sclass == EXTERN)
			outcode("BS", SYMDEF, EMPTYSTR);
	} while ((o = symbol()) == COMMA && (ninit < nel || brace || flex));
	if (brace == 0 || o != RBRACE)
		peeksym = o;
	/*
	 * If there are too few initializers, allocate
	 * more storage.
	 * If there are too many initializers, extend
	 * the declared size for benefit of "sizeof"
	 */
	if (ninit < nel && sclass != AUTO)
		outcode("BN", SSPACE, (int16_t)((nel - ninit) * width));
	else if (ninit > nel) {
		if (flex && nel == 0) {
			WI(HSUBSP(np) - 2) = ninit;
		} else
			error("Too many initializers: %.8s", HNAME(anp));
		nel = ninit;
	}
	curbase = funcbase = cb;
	return (int16_t)(nel * width);
}

/*
 * 288a strinit - same. Initialize a structure.
 */
void
strinit(uint16_t np, int16_t sclass)
{
	uint16_t mlp;
	int16_t o, brace;

	if ((mlp = MEMLIST(STRP(np))) == 0) {
		mlp = ZERLOC;
		error("Undefined structure initialization");
	}
	brace = 0;
	if ((o = symbol()) == LBRACE)
		brace++;
	else
		peeksym = o;
	do {
		if ((o = symbol()) == RBRACE)
			break;
		peeksym = o;
		if (WU(mlp) == 0) {
			error("Too many structure initializers");
			cinit(FUNCBLK, 0, sclass);
		} else {
			cinit(WU(mlp), 0, sclass);
			mlp += 2;
		}
		if (WU(mlp) == STRUCTHOLE) {
			outcode("B", EVEN);
			mlp += 2;
		}
	} while ((o = symbol()) == COMMA && (WU(mlp) || brace));
	if (sclass != AUTO && sclass != REG) {
		if (WU(mlp))
			outcode("BN", SSPACE,
			    (int16_t)(STRSIZE(STRP(np)) - HOFFSET(WU(mlp))));
		outcode("B", EVEN);
	}
	if (o != RBRACE || brace == 0)
		peeksym = o;
}

/*
 * 297e setinit - same. Mark already initialized.
 */
void
setinit(uint16_t anp)
{
	uint16_t np;

	np = anp;
	if (HFLAG(np) & FINIT)
		error("%s multiply defined", HNAME(np));
	HFLAG(np) = (signed char)(HFLAG(np) | FINIT);
}

static void statement1(void);

/*
 * 29a6 statement - same (jump tables on cval - GOTO). Process one
 * statement in a function. The port counts how deep statements nest
 * (c0_stmtdepth, for dogoto()).
 */
void
statement(void)
{
	c0_stmtdepth++;
	statement1();
	c0_stmtdepth--;
}

static void
statement1(void)
{
	int16_t o, o1, o2;
	int16_t o3;
	uint16_t np;
	int16_t sauto, sreg;
	uint16_t s1;

stmt:
	switch (o = symbol()) {

	case EOFC:
		error("Unexpected EOF");
		/* FALLTHROUGH */
	case SEMI:
		return;

	case LBRACE:
		sauto = (int16_t)autolen;
		sreg = regvar;
		blockhead();
		while (!eof) {
			if ((o = symbol()) == RBRACE) {
				autolen = (uint16_t)sauto;
				if (sreg != regvar)
					outcode("BN", SETREG, sreg);
				regvar = sreg;
				blkend();
				return;
			}
			peeksym = o;
			statement();
		}
		error("Missing '}'");
		return;

	case KEYW:
		switch (cval) {

		case GOTO:
			if ((o1 = simplegoto()) != 0)
				branch(o1);
			else
				dogoto();
			goto semi;

		case RETURN:
			doret();
			goto semi;

		case IF:
			np = pexpr();
			o2 = 0;
			if ((o1 = symbol()) == KEYW) switch (cval) {
			case GOTO:
				if ((o2 = simplegoto()) != 0)
					goto simpif;
				cbranch(np, o2 = isn++, 0);
				dogoto();
				label(o2);
				goto hardif;

			case RETURN:
				if (nextchar() == ';') {
					o2 = retlab;
					goto simpif;
				}
				cbranch(np, o1 = isn++, 0);
				doret();
				label(o1);
				o2++;
				goto hardif;

			case BREAK:
				o2 = brklab;
				goto simpif;

			case CONTIN:
				o2 = contlab;
			simpif:
				chconbrk(o2);
				cbranch(np, o2, 1);
			hardif:
				if ((o = symbol()) != SEMI)
					goto syntax;
				if ((o1 = symbol()) == KEYW && cval == ELSE)
					goto stmt;
				peeksym = o1;
				return;
			}
			peeksym = o1;
			cbranch(np, o1 = isn++, 0);
			statement();
			if ((o = symbol()) == KEYW && cval == ELSE) {
				o2 = isn++;
				branch(o2);
				label(o1);
				statement();
				label(o2);
				return;
			}
			peeksym = o;
			label(o1);
			return;

		case WHILE:
			o1 = contlab;
			o2 = brklab;
			label(contlab = isn++);
			brklab = isn++;		/* before pexpr(), as in the binary */
			s1 = pexpr();
			cbranch(s1, brklab, 0);
			statement();
			branch(contlab);
			label(brklab);
			contlab = o1;
			brklab = o2;
			return;

		case BREAK:
			chconbrk(brklab);
			branch(brklab);
			goto semi;

		case CONTIN:
			chconbrk(contlab);
			branch(contlab);
			goto semi;

		case DO:
			o1 = contlab;
			o2 = brklab;
			contlab = isn++;
			brklab = isn++;
			label(o3 = isn++);
			statement();
			label(contlab);
			contlab = o1;
			if ((o = symbol()) == KEYW && cval == WHILE) {
				cbranch(tree(), o3, 1);
				label(brklab);
				brklab = o2;
				goto semi;
			}
			goto syntax;

		case CASE:
			o1 = conexp();
			if ((o = symbol()) != COLON)
				goto syntax;
			if (swp == 0) {
				error("Case not in switch");
				goto stmt;
			}
			if (swp >= SWTAB + SWSIZ * 4) {
				error("Switch table overflow");
			} else {
				SWLAB(swp) = isn;
				SWVAL(swp) = o1;
				swp += 4;
				label(isn++);
			}
			goto stmt;

		case SWITCH:
			o1 = brklab;
			brklab = isn++;
			np = pexpr();
			chkw(np, -1);
			rcexpr(block(RFORCE, 0, 0, 0, np, 0));
			pswitch();
			brklab = o1;
			return;

		case DEFAULT:
			if (swp == 0)
				error("Default not in switch");
			if (deflab)
				error("More than 1 'default'");
			if ((o = symbol()) != COLON)
				goto syntax;
			label(deflab = isn++);
			goto stmt;

		case FOR:
			o1 = contlab;
			o2 = brklab;
			contlab = isn++;
			brklab = isn++;
			if ((o = forstmt()) != 0)
				goto syntax;
			label(brklab);
			contlab = o1;
			brklab = o2;
			return;

		case ELSE:
			error("Inappropriate 'else'");
			statement();
			return;
		}
		error("Unknown keyword");
		goto syntax;

	case NAME:
		if (nextchar() == ':') {
			peekc = 0;
			np = csym;
			if (HCLASS(np) > 0) {
				if (HBLKLEV(np) == 0) {
					pushdecl(np);
					HOFFSET(np) = 0;
				} else {
					defsym = np;
					redec();
					goto stmt;
				}
			}
			HCLASS(np) = STATIC;
			HTYP(np) = ARRAY;
			HFLAG(np) = (signed char)(HFLAG(np) | FLABL);
			if (HOFFSET(np) == 0)
				HOFFSET(np) = isn++;
			label(HOFFSET(np));
			goto stmt;
		}
	}
	peeksym = o;
	rcexpr(tree());

semi:
	if ((o = symbol()) == SEMI)
		return;
syntax:
	error("Statement syntax");
	errflush(o);
}

/*
 * 2e82 forstmt - same. Process a for statement.
 */
int16_t
forstmt(void)
{
	int16_t l, o, sline;
	int16_t sline1;
	uint16_t ss;
	uint16_t st;

	if ((o = symbol()) != LPARN)
		return o;
	if ((o = symbol()) != SEMI) {		/* init part */
		peeksym = o;
		rcexpr(tree());
		if ((o = symbol()) != SEMI)
			return o;
	}
	label(contlab);
	if ((o = symbol()) != SEMI) {		/* test part */
		peeksym = o;
		cbranch(tree(), brklab, 0);
		if ((o = symbol()) != SEMI)
			return o;
	}
	if ((peeksym = symbol()) == RPARN) {	/* incr part */
		peeksym = -1;
		statement();
		branch(contlab);
		return 0;
	}
	l = contlab;
	contlab = isn++;
	st = tree();
	sline = line;
	if ((o = symbol()) != RPARN)
		return o;
	ss = funcbase;
	funcbase = curbase;
	statement();
	sline1 = line;
	line = sline;
	label(contlab);
	rcexpr(st);
	line = sline1;
	if (ss < maxdecl)
		ss = maxdecl;
	curbase = funcbase = ss;
	branch(l);
	return 0;
}

/*
 * 2f94 pexpr - same. A parenthesized expression, as after "if".
 */
uint16_t
pexpr(void)
{
	int16_t o;
	uint16_t t;

	if ((o = symbol()) != LPARN)
		goto syntax;
	t = tree();
	if ((o = symbol()) != RPARN)
		goto syntax;
	return t;
syntax:
	error("Statement syntax");
	errflush(o);
	return 0;
}

/*
 * 2fce pswitch - same. The switch statement, which involves collecting
 * the constants and labels for the cases.
 */
void
pswitch(void)
{
	uint16_t cswp, sswp;
	int16_t dl, swlab;

	cswp = sswp = swp;
	if (swp == 0)
		cswp = swp = SWTAB;
	branch(swlab = isn++);
	dl = deflab;
	deflab = 0;
	statement();
	branch(brklab);
	label(swlab);
	if (deflab == 0)
		deflab = brklab;
	outcode("BNN", SWIT, deflab, line);
	for (; cswp < swp; cswp += 4)
		outcode("NN", SWLAB(cswp), SWVAL(cswp));
	outcode("0");
	label(brklab);
	deflab = dl;
	swp = sswp;
}

/*
 * 3082 funchead - delta: "SETREG regvar" is written first, before the
 * parameters (v7: after them). funchead is called at the start of each
 * function to process the arguments, which have been linked in a list
 * (through the symbols' hoffset words). This list is necessary because
 * in
 *	f(a, b) float b; int a; ...
 * the names are seen before the types. A register parameter is copied
 * into its register by an assignment from areg, a block standing for
 * the register (hreg its symbol).
 */
void
funchead(void)
{
	int16_t pl;
	uint16_t cs;
	uint16_t bstack;

	pl = STARG;
	outcode("BN", SETREG, regvar);
	while (paraml) {
		HOFFSET(parame) = 0;
		cs = paraml;
		paraml = (uint16_t)HOFFSET(paraml);
		if (HTYP(cs) == FLOAT)
			HTYP(cs) = DOUBLE;
		HOFFSET(cs) = pl;
		if ((HTYP(cs) & XTYPE) == ARRAY) {
			HTYP(cs) -= (ARRAY - PTR);	/* set ptr */
			HSUBSP(cs) += 2;		/* pop dims */
		}
		pl = (int16_t)(pl + rlength(cs));
		if (HCLASS(cs) == AREG && (HOFFSET(HREGSYM) = goodreg(cs)) >= 0) {
			bstack = salloc(4);
			WU(bstack) = AREGNODE;
			WU(bstack + 2) = nblock(cs);
			cp = (uint16_t)(bstack + 4);
			c0_cp_funchead = 1;
			TYP(AREGNODE) = HTYP(cs);
			HCLASS(cs) = AUTO;
			build(ASSIGN);
			rcexpr(WU(bstack));
			HOFFSET(cs) = HOFFSET(HREGSYM);
			HCLASS(cs) = REG;
			sfree(4);
		} else
			HCLASS(cs) = AUTO;
		prste(cs);
	}
	for (cs = HSHTAB; cs < HSHEND; cs += SZ_HSHTAB) {
		if (B(HNAME(cs)) == '\0')
			continue;
		if (HCLASS(cs) == ARG || HCLASS(cs) == AREG)
			error("Not an argument: %.8s", HNAME(cs));
	}
}

/*
 * 317e blockhead - delta: no SETREG (goodreg() writes one for every
 * register it hands out).
 */
void
blockhead(void)
{
	blklev++;
	declist(0);
}

/*
 * 3194 blkend - same. After the end of a block, delete local symbols;
 * save those that are external. Also complain about undefined labels.
 */
void
blkend(void)
{
	uint16_t cs, ncs;
	uint16_t endcs;
	int16_t i;

	blklev--;
	for (cs = HSHTAB; B(HNAME(cs)) && cs < HSHEND - SZ_HSHTAB;
	    cs += SZ_HSHTAB)
		;
	endcs = cs;
	do if (B(HNAME(cs))) {
		if (HBLKLEV(cs) <= blklev)
			continue;
		if ((HCLASS(cs) != EXTERN || blklev != 0)
		 && ((HFLAG(cs) & FLABL) == 0 || blklev == 0)) {
			if (HCLASS(cs) == 0)
				error("%.8s undefined", HNAME(cs));
			if ((ncs = HPDOWN(cs)) == 0) {
				B(HNAME(cs)) = '\0';
				hshused--;
				HFLAG(cs) = (signed char)(HFLAG(cs) & FKEYW);
			} else {
				cpysymb(cs, ncs);
			}
			continue;
		}
		/*
		 * Retained name; must rehash.
		 */
		for (i = 0; i < NCPS; i++)
			B(SYMBUF + i) = B(HNAME(cs) + i);
		mossym = HFLAG(cs) & FMOS;
		lookup();
		if ((ncs = csym) != cs) {
			B(HNAME(cs)) = '\0';
			hshused--;
			i = HFLAG(ncs);
			cpysymb(ncs, cs);
			HFLAG(ncs) = (signed char)(HFLAG(ncs) | (i & FKEYW));
			HFLAG(cs) = (signed char)(HFLAG(cs) & FKEYW);
		}
		if (HBLKLEV(ncs) > 1 || (HBLKLEV(ncs) > 0 && HCLASS(ncs) == EXTERN))
			HBLKLEV(ncs)--;
	} while ((cs = (cs < HSHEND - SZ_HSHTAB) ? cs + SZ_HSHTAB : HSHTAB)
	    != endcs);
}

/*
 * 32ac prste - same (a jump table on hclass - AUTO). Write out special
 * definitions of local symbols for the benefit of the debugger.
 */
void
prste(uint16_t acs)
{
	uint16_t cs;
	int16_t nkind;

	cs = acs;
	switch (HCLASS(cs)) {
	case REG:
		nkind = RNAME;
		break;

	case AUTO:
		nkind = ANAME;
		break;

	case STATIC:
		nkind = SNAME;
		break;

	default:
		return;

	}
	outcode("BSN", nkind, HNAME(cs), HOFFSET(cs));
}

/*
 * 32f6 errflush - same. In case of error, skip to the next statement
 * delimiter.
 */
void
errflush(int16_t ao)
{
	int16_t o;

	o = ao;
	while (o > RBRACE)	/* ; { } */
		o = symbol();
	peeksym = o;
}
