/*
 * cpy.c - v7/cpp/cpy.y and yylex.c as the original MUTOS 1700 cpp binary
 * has them ("@(#)yylex.c 1.3 12/12/86"): the #if expression's lexer,
 * tobinary(), and yacc's parser yyparse() with the grammar's actions.
 * The parser's tables (yyexca[], yyact[], yypact[], yypgo[], yyr1[],
 * yyr2[], yychk[], yydef[]) are the binary's, in the image (cppdata.c).
 */
#include "cpp.h"

/* the tokens (cpy.y's %term), as yacc numbered them */
#define	NUMBER		257
#define	STOP		258
#define	DEFINED		259
#define	YYERRCODE	256
#define	YYFLAG		(-1000)
#define	YYLAST		363
#define	YYMAXDEPTH	150

/*
 * yylex (1fbe). Deltas: comments in the #if line are removed even under
 * -C (passcom is 0 while skipbl() reads); tobinary() reads hex digits
 * a-f and A-F right (v7's yylex.c gives c - 'a').
 *
 * And the compiled code's own bug (CPP-01): for an escaped character
 * constant that is not one of \b \t \n \f \r \\, the test "inp[2] >=
 * '0'" and the octal conversion use a pointer whose high byte is newp's
 * and whose low byte is inp[2] - the register that held inp was reused
 * for newp, and its low byte reloaded with inp[2], by the compiler. So
 * '\0' looks at whatever lies in the 256-byte page of newp.
 */
static int16_t
yylex_(void)
{
	int16_t savc, val, sav;
	uint16_t s, p2, sp, t;

	for (;;) {
		sav = passcom;
		passcom = 0;
		newp = skipbl(newp);
		passcom = sav;
		if (rb(inp) == '\n')
			return STOP;	/* end of #if */
		savc = rb(newp);
		wb(newp, '\0');
		for (p2 = YYL_OP2 + 16; (p2 -= 2) >= YYL_OP2;)	/* check 2-char ops */
			if (0 == cstrcmp(WU(p2), inp)) {
				val = AW(YYL_VAL2, (p2 - YYL_OP2) / 2);
				goto ret;
			}
		s = S_YYOPS1;	/* check 1-char ops */
		while (rb(s) != 0)
			if (rb(s++) == rb(inp)) {
				val = rb(--s);
				goto ret;
			}
		if (rb(inp) <= '9' && rb(inp) >= '0') {	/* a number */
			if (rb(inp) == '0')
				yylval = (rb((uint16_t)(inp + 1)) == 'x'
				    || rb((uint16_t)(inp + 1)) == 'X') ?
				    tobinary((uint16_t)(inp + 2), 16) :
				    tobinary((uint16_t)(inp + 1), 8);
			else
				yylval = tobinary(inp, 10);
			val = NUMBER;
		} else if (rb((uint16_t)(FASTAB + rb(inp))) & IB) {
			if (0 == cstrcmp(inp, S_DEFINED)) {
				YYL_IFDEF = 1;
				++flslvl;
				val = DEFINED;
			} else {
				sp = lookup(inp, -1);
				if (YYL_IFDEF != 0) {
					YYL_IFDEF = 0;
					--flslvl;
				}
				yylval = (SVALUE(sp) == 0) ? 0 : 1;
				val = NUMBER;
			}
		} else if (rb(inp) == '\'') {	/* character constant */
			val = NUMBER;
			if (rb((uint16_t)(inp + 1)) == '\\') {	/* escaped */
				if (rb((uint16_t)(newp - 1)) == '\'')
					wb((uint16_t)(newp - 1), '\0');
				t = newp;
				s = WU(YYL_OPC);
				while (rb(s) != 0) {
					t = (uint16_t)((t & 0xff00) | UB((uint16_t)(inp + 2)));
					if (rb(s++) != rb((uint16_t)(inp + 2)))
						++s;
					else {
						yylval = rb(s);
						goto ret;
					}
				}
				if (rb((uint16_t)(inp + 2)) <= '9'
				    && rb((uint16_t)(t + 2)) >= '0')
					yylval = (int8_t)tobinary((uint16_t)(t + 2), 8);
				else
					yylval = rb((uint16_t)(inp + 2));
			} else
				yylval = rb((uint16_t)(inp + 1));
		} else if (0 == cstrcmp(S_BSNL, inp)) {
			wb(newp, savc);
			continue;
		} else {
			wb(newp, savc);
			pperror(S_ILLCHAR, (uint16_t)rb(inp), 0);
			continue;
		}
ret:
		wb(newp, savc);
		outp = inp = newp;
		return val;
	}
}

int16_t
yylex(void)
{
	uint16_t bp = fr_enter(0, 0xa);
	int16_t r = yylex_();

	fr_leave(bp, 0);
	return r;
}

/*
 * tobinary (2202). Delta: a-f and A-F are 10..15 (v7: c - 'a', c - 'A').
 * As v7: no digit is checked against the base ("08" is 8), and an 'l'
 * or 'L' at the end is allowed.
 */
static int16_t
tobinary_(uint16_t st, int16_t b)
{
	int16_t n, c, t;
	uint16_t s;

	n = 0;
	s = st;
	while ((c = rb(s++)) != 0) {
		switch (c) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			t = (int16_t)(c - '0');
			break;
		case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
			t = (int16_t)(c - 'a' + 10);
			if (b > 10)
				break;
			/* fall through */
		case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
			t = (int16_t)(c - 'A' + 10);
			if (b > 10)
				break;
			/* fall through */
		default:
			t = -1;
			if (c == 'l' || c == 'L')
				if (rb(s) == '\0')
					break;
			pperror(S_ILLNUM, st, 0);
		}
		if (t < 0)
			break;
		n = (int16_t)(n * b + t);
	}
	return n;
}

int16_t
tobinary(uint16_t st, int16_t b)
{
	uint16_t bp = fr_enter(2, 8);
	int16_t r = tobinary_(st, b);

	fr_leave(bp, 2);
	return r;
}

/* the 8086's shifts by CL: the whole count, not its low five bits */
static int16_t
shl86(int16_t v, int16_t n)
{
	unsigned cl = (uint8_t)n;

	return cl >= 16 ? 0 : (int16_t)(uint16_t)((uint16_t)v << cl);
}

static int16_t
sar86(int16_t v, int16_t n)
{
	unsigned cl = (uint8_t)n;

	if (cl >= 16)
		return v < 0 ? -1 : 0;
	return (int16_t)(v >> cl);
}

/* idiv: the 8086 faults on a 0 divisor and on a quotient outside -32767..32767 */
static void
idiv_check(int16_t a, int16_t b)
{
	if (b == 0)
		cpptrap("#if divides by 0 (the 8086's idiv faults)");
	if (a == -32768 && (b == 1 || b == -1))
		cpptrap("#if divides -32768 by 1 or -1 (the 8086's idiv faults: "
		    "the quotient's magnitude is 32768)");
}

/*
 * yyparse (2322): v7's yacc driver (yaccpar) with cpy.y's actions. Its
 * state stack yys[] holds YYMAXDEPTH entries and the overflow test lets
 * one more through - the word above them in the original's frame, the
 * caller's saved SI, which control() does not use again; the value stack
 * yyv[] is global, and its 151st entry is libc.a's errno (CPP-04).
 */
static int16_t
yyparse_(void)
{
	int16_t yys[YYMAXDEPTH + 1];
	int16_t yyj, yym, yystate, yyn, yyps;
	uint16_t yypv, yypvt, yyxi;

	yystate = 0;
	yychar = -1;
	yynerrs = 0;
	yyerrflag = 0;
	yyps = -1;
	yypv = (uint16_t)(YYV - 2);

yystack:	/* put a state and value onto the stack */
	if (++yyps > YYMAXDEPTH) {
		yyerror(S_YYOVERFLOW, 0, 0);
		return 1;
	}
	yys[yyps] = yystate;
	yypv += 2;
	WI(yypv) = yyval;

yynewstate:
	yyn = AW(YYPACT, yystate);
	if (yyn <= YYFLAG)
		goto yydefault;	/* simple state */
	if (yychar < 0)
		if ((yychar = yylex()) < 0)
			yychar = 0;
	if ((yyn = (int16_t)(yyn + yychar)) < 0 || yyn >= YYLAST)
		goto yydefault;
	if (AW(YYCHK, yyn = AW(YYACT, yyn)) == yychar) {	/* valid shift */
		yychar = -1;
		yyval = yylval;
		yystate = yyn;
		if (yyerrflag > 0)
			--yyerrflag;
		goto yystack;
	}
yydefault:
	/* default state action */
	if ((yyn = AW(YYDEF, yystate)) == -2) {
		if (yychar < 0)
			if ((yychar = yylex()) < 0)
				yychar = 0;
		/* look through exception table */
		for (yyxi = YYEXCA; WI(yyxi) != -1 || WI(yyxi + 2) != yystate;
		    yyxi += 4)
			;
		while (WI(yyxi += 4) >= 0)
			if (WI(yyxi) == yychar)
				break;
		if ((yyn = WI(yyxi + 2)) < 0)
			return 0;	/* accept */
	}
	if (yyn == 0) {	/* error */
		/* error ... attempt to resume parsing */
		switch ((uint16_t)yyerrflag) {
		case 0:	/* brand new error */
			yyerror(S_SYNTAX, 0, 0);
			++yynerrs;
			/* fall through */
		case 1:
		case 2:	/* incompletely recovered error ... try again */
			yyerrflag = 3;
			/* find a state where "error" is a legal shift action */
			while (yyps >= 0) {
				yyn = (int16_t)(AW(YYPACT, yys[yyps]) + YYERRCODE);
				if (yyn >= 0 && yyn < YYLAST
				    && AW(YYCHK, AW(YYACT, yyn)) == YYERRCODE) {
					/* simulate a shift of "error" */
					yystate = AW(YYACT, yyn);
					goto yystack;
				}
				yyn = AW(YYPACT, yys[yyps]);
				/* the current yyps has no shift on "error", pop stack */
				--yyps;
				yypv -= 2;
			}
			/* there is no state on the stack with an error shift ... abort */
			return 1;
		case 3:	/* no shift yet; clobber input char */
			if (yychar == 0)
				return 1;	/* don't discard EOF, quit */
			yychar = -1;
			goto yynewstate;	/* try again in the same state */
		default:
			break;	/* the compiled switch: a reduction by 0 */
		}
	}
	/* reduction by production yyn */
	{
		int16_t r2 = AW(YYR2, yyn);

		yyps = (int16_t)(yyps - r2);
		yypvt = yypv;
		yypv = (uint16_t)(yypv - 2 * r2);
		yyval = WI(yypv + 2);
		yym = yyn;
		/* consult goto table to find next state */
		yyn = AW(YYR1, yyn);
		if (yyps < 0 || yyps > YYMAXDEPTH)
			cpptrap("yacc's state stack popped below its start");
		yyj = (int16_t)(AW(YYPGO, yyn) + yys[yyps] + 1);
		if (yyj >= YYLAST || AW(YYCHK, yystate = AW(YYACT, yyj)) != -yyn)
			yystate = AW(YYACT, AW(YYPGO, yyn));
	}
#define	V(k)	WI((uint16_t)(yypvt + 2 * (k)))
	switch (yym) {
	case 1:	/* S: e stop */
		return V(-1);
	case 2:	/* e '*' e */
		yyval = (int16_t)(V(-2) * V(0));
		break;
	case 3:	/* e '/' e */
		idiv_check(V(-2), V(0));
		yyval = (int16_t)(V(-2) / V(0));
		break;
	case 4:	/* e '%' e */
		idiv_check(V(-2), V(0));
		yyval = (int16_t)(V(-2) % V(0));
		break;
	case 5:	/* e '+' e */
		yyval = (int16_t)(V(-2) + V(0));
		break;
	case 6:	/* e '-' e */
		yyval = (int16_t)(V(-2) - V(0));
		break;
	case 7:	/* e LS e */
		yyval = shl86(V(-2), V(0));
		break;
	case 8:	/* e RS e */
		yyval = sar86(V(-2), V(0));
		break;
	case 9:	/* e '<' e */
		yyval = V(-2) < V(0);
		break;
	case 10:	/* e '>' e */
		yyval = V(-2) > V(0);
		break;
	case 11:	/* e LE e */
		yyval = V(-2) <= V(0);
		break;
	case 12:	/* e GE e */
		yyval = V(-2) >= V(0);
		break;
	case 13:	/* e EQ e */
		yyval = V(-2) == V(0);
		break;
	case 14:	/* e NE e */
		yyval = V(-2) != V(0);
		break;
	case 15:	/* e '&' e */
		yyval = (int16_t)(V(-2) & V(0));
		break;
	case 16:	/* e '^' e */
		yyval = (int16_t)(V(-2) ^ V(0));
		break;
	case 17:	/* e '|' e */
		yyval = (int16_t)(V(-2) | V(0));
		break;
	case 18:	/* e ANDAND e */
		yyval = V(-2) && V(0);
		break;
	case 19:	/* e OROR e */
		yyval = V(-2) || V(0);
		break;
	case 20:	/* e '?' e ':' e */
		yyval = V(-4) ? V(-2) : V(0);
		break;
	case 21:	/* e ',' e */
		yyval = V(0);
		break;
	case 22:	/* e: term */
		yyval = V(0);
		break;
	case 23:	/* '-' term */
		yyval = (int16_t)-(uint16_t)V(0);
		break;
	case 24:	/* '!' term */
		yyval = !V(0);
		break;
	case 25:	/* '~' term */
		yyval = (int16_t)~V(0);
		break;
	case 26:	/* '(' e ')' */
		yyval = V(-1);
		break;
	case 27:	/* DEFINED '(' number ')' */
		yyval = V(-1);
		break;
	case 28:	/* DEFINED number */
		yyval = V(0);
		break;
	case 29:	/* number */
		yyval = V(0);
		break;
	}
#undef	V
	goto yystack;	/* stack new state and value */
}

int16_t
yyparse(void)
{
	uint16_t bp = fr_enter(0, 0x13a + 2);
	int16_t r = yyparse_();

	fr_leave(bp, 0);
	return r;
}
