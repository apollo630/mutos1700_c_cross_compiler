/*
 * gram.c - the makefile's grammar: the parser yacc built from gram.y, its
 * actions, and the scanner. yylex(), retsh() and nextlin() are v7's, which
 * the original MUTOS 1700 make has byte for byte (text 1f40..22ce); the
 * actions are gram.y's but for NLEFTS (300 here, 40 in v7's defs); yyparse()
 * (22ce, with the action switch at 27ac) is yacc's parser skeleton - the
 * one of v7's yaccpar, with no yydebug - run on the eight tables the
 * binary has, tests/realbin/maketables.py --c writes them as makedata.c.
 *
 * The grammar (the tokens are 257 NAME, 258 SHELLINE, 259 START, 260
 * MACRODEF, 261 COLON, 262 DOUBLECOLON, 263 GREATER; "error" is 256):
 *
 *	 1 file:
 *	 2 file: file comline
 *	 3 comline: START
 *	 4 comline: MACRODEF
 *	 5 comline: START namelist deplist shellist		{ the rules }
 *	 6 comline: error
 *	 7 namelist: NAME					{ lefts[0] = $1; nlefts = 1; }
 *	 8 namelist: namelist NAME				{ lefts[nlefts++] = $2; }
 *	 9 deplist:						{ fatal1("Must be a separator ..."); }
 *	10 deplist: dlist
 *	11 dlist: sepchar					{ prevdep = 0; $$ = 0; }
 *	12 dlist: dlist NAME					{ a depblock }
 *	13 sepchar: COLON					{ sepc = ALLDEPS; }
 *	14 sepchar: DOUBLECOLON					{ sepc = SOMEDEPS; }
 *	15 shellist:						{ $$ = 0; }
 *	16 shellist: shlist					{ $$ = $1; }
 *	17 shlist: SHELLINE					{ $$ = $1; prevshp = $1; }
 *	18 shlist: shlist SHELLINE				{ the chain of lines }
 *
 * A syntax error ends make (yyerror() is fatal), so the error recovery of
 * yaccpar - rule 6, the three error states - is never entered, and the port
 * does not have it; the state the parser would reach by shifting `error`
 * (5) is unreachable.
 */
#include "make.h"

#include <stdlib.h>
#include <string.h>

#define NAME 257
#define SHELLINE 258
#define START 259
#define MACRODEF 260
#define COLON 261
#define DOUBLECOLON 262
#define GREATER 263

typedef union {
	struct shblock *yshblock;
	struct depblock *ydepblock;
	struct nameblock *ynameblock;
} YYSTYPE;

#define YYLAST 18
#define YYFLAG (-1000)
#define YYERRCODE 256
#define YYMAXDEPTH 150

static YYSTYPE yylval, yyval;

/* the variables of the grammar's declarations section */
static struct depblock *pp;
static struct shblock *prevshp;
static struct nameblock *lefts[NLEFTS];
static struct nameblock *leftp;
static mint nlefts;
static struct lineblock *lp, *lpp;
static struct depblock *prevdep;
static mint sepc;

char *zznextc;	/* zero if need another line; otherwise points to next char */
mint yylineno;

static mint nextlin(void);
static mint retsh(char *q);

/* 1f40 */
mint
yylex(void)
{
	char *p;
	char *q;
	char word[INMAX];
	mint r;

	mk_enter(1508);
	if (zznextc == NULL) {
		r = nextlin();
		mk_leave(1508);
		return r;
	}

	while (mk_isspace(*zznextc))
		++zznextc;

	if (*zznextc == '\0') {
		r = nextlin();
		mk_leave(1508);
		return r;
	}

	if (*zznextc == ':') {
		mk_leave(1508);
		if (*++zznextc == ':') {
			++zznextc;
			return DOUBLECOLON;
		} else
			return COLON;
	}

	if (*zznextc == '>') {
		++zznextc;
		mk_leave(1508);
		return GREATER;
	}

	if (*zznextc == ';') {
		r = retsh(zznextc);
		mk_leave(1508);
		return r;
	}

	p = zznextc;
	q = word;

	while (!(mk_funny(*p) & TERMINAL))
		*q++ = *p++;

	if (p != zznextc) {
		*q = '\0';
		if ((yylval.ynameblock = srchname(word)) == NULL)
			yylval.ynameblock = makename(word);
		zznextc = p;
		mk_leave(1508);
		return NAME;
	}

	else {
		mk_eprintf("Bad character %c (octal %o), line %d", *zznextc,
		    (unsigned)(*zznextc & 0xffff), yylineno);
		fatal(NULL);
	}
}

/* 204a */
static mint
retsh(char *q)
{
	char *p;
	struct shblock *sp;

	for (p = q + 1; *p == ' ' || *p == '\t'; ++p)
		;

	sp = ALLOC(shblock);
	sp->nxtshblock = NULL;
	sp->shbp = (fin == NULL ? p : copys(p));
	yylval.yshblock = sp;
	zznextc = NULL;
	return SHELLINE;
}

/* A line of the built-in rules is changed in place (eqsign() cuts a macro
 * definition at its equal sign) and kept (a command line is pointed to, not
 * copied): the original's strings are in its data; here they are copied
 * to memory that can be written, which the 67 lines do not count against
 * the heap of the original. */
static char *
builtin_line(const char *s)
{
	char *c = strdup(s);

	if (c == NULL)
		fatal("out of memory");
	return c;
}

static mint
nextlin1(void)
{
	static char yytext[INMAX];
	static char *const yytextl = yytext + INMAX;
	char *text, templin[INMAX];
	char c;
	char *p, *t;
	char lastch, *lastchp;
	mint incom;
	int kc;
	size_t l;

again:

	incom = NO;
	zznextc = NULL;

	if (fin == NULL) {
		const char *bl = *linesptr++;

		if (bl == NULL)
			return 0;
		text = builtin_line(bl);
		++yylineno;
	}

	else {
		for (p = text = yytext; p < yytextl; *p++ = (char)kc)
			switch (kc = getc(fin)) {
			case '\t':
				if (p != yytext)
					break;
				/* FALLTHROUGH */
			case ';':
				incom = YES;
				break;

			case '#':
				if (!incom)
					kc = '\0';
				break;

			case '\n':
				++yylineno;
				if (p == yytext || p[-1] != '\\') {
					*p = '\0';
					goto endloop;
				}
				p[-1] = ' ';
				while ((kc = getc(fin)) == '\t' || kc == ' ' || kc == '\n')
					if (kc == '\n')
						++yylineno;

				if (kc != EOF)
					break;
				/* FALLTHROUGH */
			case EOF:
				*p = '\0';
				return 0;
			}

		fatal("line too long");
	}

endloop:

	if ((c = text[0]) == '\t')
		return retsh(text);

	if (mk_isalpha(c) || mk_isdigit(c) || c == ' ' || c == '.') {
		for (p = text + 1; *p != '\0';)
			if (*p == ':')
				break;
			else if (*p++ == '=') {
				eqsign(text);
				return MACRODEF;
			}
	}

	/* substitute for macros on dependency line up to the semicolon if any */

	for (t = yytext; *t != '\0' && *t != ';'; ++t)
		;

	lastchp = t;
	lastch = *t;
	*t = '\0';

	subst(yytext, templin, sizeof(templin));	/* Substitute for macros on dependency lines */

	if (lastch) {
		l = strlen(templin);
		if (l + 1 + strlen(lastchp + 1) + 1 > sizeof(templin))
			mk_trap("a line would run over a buffer of the original's stack");
		for (t = templin; *t; ++t)
			;
		*t = lastch;
		while ((*++t = *++lastchp))
			;
	}

	p = templin;
	t = yytext;
	while ((*t++ = *p++))
		;

	for (p = zznextc = text; *p; ++p)
		if (*p != ' ' && *p != '\t')
			return START;
	goto again;
}

/* 20a0 */
static mint
nextlin(void)
{
	mint r;

	mk_enter(1520);
	r = nextlin1();
	mk_leave(1520);
	return r;
}

/* ---- the parser: yacc's skeleton on make_yy* ----------------------------- */

static mint yychar;
static mint yynerrs;
static mint yyerrflag;
static YYSTYPE yyv[YYMAXDEPTH + 2];

/* the actions of the rules, gram.y's: 5 is the rule of a line, the others
 * build its parts; rule 9 stops with a message */
static void
yyaction(mint yym, const YYSTYPE *yypvt)
{
	switch (yym) {

	case 5:		/* START namelist deplist shellist */
		/* $3 is yypvt[-1], $4 is yypvt[0] */
		while (--nlefts >= 0) {
			leftp = lefts[nlefts];
			if (leftp->septype == 0)
				leftp->septype = (int8_t)sepc;
			else if (leftp->septype != sepc)
				mk_eprintf("Inconsistent rules lines for `%s'\n",
				    leftp->namep);
			else if (sepc == ALLDEPS && *(leftp->namep) != '.' &&
			    yypvt[0].yshblock != NULL) {
				for (lp = leftp->linep; lp->nxtlineblock != NULL;
				    lp = lp->nxtlineblock)
					if (lp->shp)
						mk_eprintf("Multiple rules lines for `%s'\n",
						    leftp->namep);
			}

			lp = ALLOC(lineblock);
			lp->nxtlineblock = NULL;
			lp->depp = yypvt[-1].ydepblock;
			lp->shp = yypvt[0].yshblock;

			if (!unequal(leftp->namep, ".SUFFIXES") &&
			    yypvt[-1].ydepblock == NULL)
				leftp->linep = NULL;
			else if (leftp->linep == NULL)
				leftp->linep = lp;
			else {
				for (lpp = leftp->linep; lpp->nxtlineblock;
				    lpp = lpp->nxtlineblock)
					;
				if (sepc == ALLDEPS && leftp->namep[0] == '.')
					lpp->shp = NULL;
				lpp->nxtlineblock = lp;
			}
		}
		break;

	case 7:		/* namelist: NAME */
		lefts[0] = yypvt[0].ynameblock;
		nlefts = 1;
		break;

	case 8:		/* namelist: namelist NAME */
		if (nlefts >= NLEFTS) {
			/* The 301st name is stored over nlefts, which follows
			 * lefts[]: nlefts is then the name block's address - a
			 * positive int, above 300, while the heap is below
			 * 0x8000 - and the test below ends make (MK-03). */
			if (mk_break() >= 0x8000)
				mk_trap("more than 300 targets in one rule, "
				    "with the heap above 32K");
			fatal("Too many lefts");
		}
		lefts[nlefts++] = yypvt[0].ynameblock;
		if (nlefts > NLEFTS)
			fatal("Too many lefts");
		break;

	case 9: {	/* deplist: (empty) */
		char junk[10];

		snprintf(junk, sizeof(junk), "%d", yylineno);
		fatal1("Must be a separator on rules line %s", junk);
	}

	case 11:	/* dlist: sepchar */
		prevdep = NULL;
		yyval.ydepblock = NULL;
		break;

	case 12:	/* dlist: dlist NAME */
		pp = ALLOC(depblock);
		pp->nxtdepblock = NULL;
		pp->depname = yypvt[0].ynameblock;
		if (prevdep == NULL)
			yyval.ydepblock = pp;
		else
			prevdep->nxtdepblock = pp;
		prevdep = pp;
		break;

	case 13:	/* sepchar: COLON */
		sepc = ALLDEPS;
		break;

	case 14:	/* sepchar: DOUBLECOLON */
		sepc = SOMEDEPS;
		break;

	case 15:	/* shellist: (empty) */
		yyval.yshblock = NULL;
		break;

	case 16:	/* shellist: shlist */
		yyval.yshblock = yypvt[0].yshblock;
		break;

	case 17:	/* shlist: SHELLINE */
		yyval.yshblock = yypvt[0].yshblock;
		prevshp = yypvt[0].yshblock;
		break;

	case 18:	/* shlist: shlist SHELLINE */
		yyval.yshblock = yypvt[-1].yshblock;
		prevshp->nxtshblock = yypvt[0].yshblock;
		prevshp = yypvt[0].yshblock;
		break;
	}
}

/* 22ce. Parse the makefile: 0 when it is done, 1 on a stack overflow. */
mint
yyparse(void)
{
	int16_t yys[YYMAXDEPTH + 2];
	int yyps, yypv, yypvt;	/* indexes of the tops of the two stacks */
	mint yystate, yyn, yyj, yym;
	mint r;

	mk_enter(330);
	yystate = 0;
	yychar = -1;
	yynerrs = 0;
	yyerrflag = 0;
	yyps = -1;
	yypv = -1;

yystack:	/* put a state and value onto the stack */
	if (++yyps > YYMAXDEPTH) {
		yyerror("yacc stack overflow");
		r = 1;
		goto out;
	}
	yys[yyps] = yystate;
	++yypv;
	yyv[yypv] = yyval;

	/* (yaccpar's label yynewstate, the target of the error recovery, is gone) */
	yyn = make_yypact[yystate];

	if (yyn <= YYFLAG)
		goto yydefault;	/* simple state */

	if (yychar < 0)
		if ((yychar = yylex()) < 0)
			yychar = 0;
	if ((yyn = (mint)(yyn + yychar)) < 0 || yyn >= YYLAST)
		goto yydefault;

	if (make_yychk[yyn = make_yyact[yyn]] == yychar) {	/* valid shift */
		yychar = -1;
		yyval = yylval;
		yystate = yyn;
		if (yyerrflag > 0)
			--yyerrflag;
		goto yystack;
	}

yydefault:
	/* default state action */

	if ((yyn = make_yydef[yystate]) == -2) {
		const int16_t *yyxi;

		if (yychar < 0)
			if ((yychar = yylex()) < 0)
				yychar = 0;
		/* look through exception table */

		for (yyxi = make_yyexca; (*yyxi != -1) || (yyxi[1] != yystate); yyxi += 2)
			;

		while (*(yyxi += 2) >= 0)
			if (*yyxi == yychar)
				break;
		if ((yyn = yyxi[1]) < 0) {
			r = 0;	/* accept */
			goto out;
		}
	}

	if (yyn == 0) {	/* error */
		/* yyerrflag is 0 here: the first error ends make */
		yyerror("syntax error");
	}

	/* reduction by production yyn */

	yyps -= make_yyr2[yyn];
	yypvt = yypv;
	yypv -= make_yyr2[yyn];
	yyval = yyv[yypv + 1];
	yym = yyn;
	/* consult goto table to find next state */
	yyn = make_yyr1[yyn];
	yyj = (mint)(make_yypgo[yyn] + yys[yyps] + 1);
	if (yyj >= YYLAST || make_yychk[yystate = make_yyact[yyj]] != -yyn)
		yystate = make_yyact[make_yypgo[yyn]];
	yyaction(yym, &yyv[yypvt]);
	goto yystack;	/* stack new state and value */

out:
	mk_leave(330);
	return r;
}
