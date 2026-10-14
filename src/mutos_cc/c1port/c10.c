/*
 * c10.c - the MUTOS 1700 code generator, part 1: main(), the code-table
 * matcher and the expression compiler (v7/cc/c10.c, ported from the
 * original binary - see c1.h and docs/C1_PORT_PLAN.md).
 *
 * Function order and the text offsets in the comments follow the
 * original (tests/realbin/c1map.txt).
 */
#include "c1.h"

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

int16_t	nreg = 3;		/* 0046 */
int16_t	isn = 10000;		/* 0048 */
int16_t	line;
int16_t	nerror;
int16_t	nstack;
int16_t	nfloat;
int16_t	fpdepth;
int16_t	fpkeep;
int16_t	stkthresh = 100;	/* 04a6 */
int16_t	dflag_a, dflag_b, dflag_c, dflag_d, dflag_r;
int32_t	totspace;
int16_t	xlab1, xlab2, xop, xzero;
struct	table	*cregtab;

/* 0030, 0036, 003c: v7's czero, cone and sfuncr */
struct	tnode	czero = { .op = CON, .type = INT, .value = 0 };
struct	tnode	cone = { .op = CON, .type = INT, .value = 1 };
struct	tnode	sfuncr = { .op = NAME, .type = STRUCT, .class = STATIC };

/* 434a: getree()'s FCON case sets it, fpeintr() returns to it */
jmp_buf	fpejmp;

/*
 * 00b6 fpeintr - new: the SIGFPE handler. libc.a's software
 * floating-point runtime raises SIGFPE when an operation overflows
 * (__ovfl: kill(getpid(), 8)); the only floating arithmetic c1 does
 * while reading is atof() in getree()'s FCON case, which has set
 * fpejmp. The handler is not re-armed: after one SIGFPE the signal is
 * at its default again, as v7's kernel leaves it (see c1lib.c's
 * c1_atof(), which models the signal).
 */
void
fpeintr(int sig)
{
	(void)sig;
	error("floating point over or under flow\n");
	longjmp(fpejmp, 1);
}

/* v7's atoi() (libc.a, text 6c66), for option s<n>: 16-bit arithmetic */
static int16_t
atoi16(const char *p)
{
	int16_t n = 0;
	int f = 0;

	for (;; p++) {
		switch (*p) {
		case ' ':
		case '\t':
			continue;
		case '-':
			f++;
			/* fall through */
		case '+':
			p++;
		}
		break;
	}
	while (*p >= '0' && *p <= '9')
		n = (int16_t)(n * 10 + *p++ - '0');
	return f ? (int16_t)-n : n;
}

/*
 * 00d6 main - delta: options after the three file names (a, b, c, d, r
 * switch on the debug traces, s<n> sets the frame size from which a
 * function calls chkstk); signal(SIGFPE, fpeintr); ".data" where v7
 * writes ".globl\n.data".
 */
int
main(int argc, char *argv[])
{
	int i;
	char *p;
	char c;

	if (argc < 4) {
		error("Arg count");
		exit(1);
	}
	if (freopen(argv[1], "r", stdin) == NULL) {
		error("Missing temp file");
		exit(1);
	}
	if (freopen(argv[3], "w", stdout) == NULL) {
		error("Can't create %s", argv[3]);
		exit(1);
	}
	for (i = 4; i < argc; i++)
		for (p = argv[i]; (c = *p) != 0; p++)
			switch (c) {
			case 'a':
				dflag_a = 1;
				break;
			case 'b':
				dflag_b = 1;
				break;
			case 'c':
				dflag_c = 1;
				break;
			case 'd':
				dflag_d = 1;
				break;
			case 'r':
				dflag_r = 1;
				break;
			case 's':
				stkthresh = atoi16(p + 1);
				break;
			}
	/*
	 * The original calls signal(SIGFPE, fpeintr) here. The port's atof()
	 * raises no host signal; c1_atof() calls fpeintr() where libc.a's
	 * atof() would raise SIGFPE.
	 */
	blkinit();	/* funcbase = curbase = coremax = sbrk(0); */
	getree();
	if (nfloat)
		printf(".globl\tfltused\n");
	printf(".data\n");
	if (*argv[2] != '-') {
		if (freopen(argv[2], "r", stdin) == NULL) {
			error("Missing temp file");
			exit(1);
		}
		getree();
	}
	if (totspace >= (int32_t)(uint16_t)56000) {
		error("Warning: possibly too much data");
		nerror--;
	}
	exit(nerror != 0);
}

/*
 * 02c2 match - delta: an operand that must be an indirection (a table
 * degree with the 0100 bit) may be STAR or op 89 (STARC); the table is
 * used as given (no lsptab).
 */
#define	NOCVL	1
#define	NOCVR	2

struct optab *
match(struct tnode *tree, struct table *table, int nrleft, int nocvt)
{
	int op, d1, d2 = 0, dope;
	struct tnode *p2 = 0;
	struct tnode *p1;
	struct optab *opt;

	if (tree == 0)
		return 0;
	if ((op = tree->op) == 0)
		return 0;
	dope = opdope[op];
	if ((dope & LEAF) == 0)
		p1 = tree->tr1;
	else
		p1 = tree;
	d1 = dcalc(p1, nrleft);
	if ((dope & BINARY) != 0 && (p2 = tree->tr2) != 0) {
		/*
		 * If a subtree starts off with a conversion operator,
		 * try for a match with the conversion eliminated.
		 */
		if (opdope[p2->op] & CNVRT && (nocvt & NOCVR) == 0
		 && (opdope[p2->tr1->op] & CNVRT) == 0) {
			tree->tr2 = p2->tr1;
			if ((opt = match(tree, table, nrleft, NOCVL)) != 0)
				return opt;
			tree->tr2 = p2;
		} else if (opdope[p1->op] & CNVRT && (nocvt & NOCVL) == 0
		 && (opdope[p1->tr1->op] & CNVRT) == 0) {
			tree->tr1 = p1->tr1;
			if ((opt = match(tree, table, nrleft, NOCVR)) != 0)
				return opt;
			tree->tr1 = p1;
		}
		d2 = dcalc(p2, nrleft);
	}
	for (; table->tabop != op; table++)
		if (table->tabop == 0)
			return 0;
	for (opt = table->tabp; opt->tabdeg1 != 0; opt++) {
		if (d1 > (opt->tabdeg1 & 077)
		 || (opt->tabdeg1 >= 0100 && p1->op != STAR && p1->op != STARC))
			continue;
		if (notcompat(p1, opt->tabtyp1, op))
			continue;
		if ((opdope[op] & BINARY) != 0 && p2 != 0) {
			if (d2 > (opt->tabdeg2 & 077)
			 || (opt->tabdeg2 >= 0100 && p2->op != STAR && p2->op != STARC))
				continue;
			if (notcompat(p2, opt->tabtyp2, 0))
				continue;
		}
		return opt;
	}
	return 0;
}

/*
 * 0498 rcexpr - x86. v7's structure; what the original does differently:
 *   - option r traces each call ("**rcexpr**" and the tree);
 *   - RFORCE: a floating value is stored into "fac" ("lea ax,fac / call
 *     fstdp / lea ax,fac" - the floating-point stack model popped), a
 *     long goes to DX:AX (register 1), an int to AX, a char then "cbw";
 *   - SEQNC saves and restores the floating-point stack model too;
 *   - CALL: the arguments through comarg(), always pushed; a floating
 *     result is loaded ("call fldd", the model pushed) unless efftab
 *     wants none; tr2 is cleared;
 *   - &x of an auto: "lea r,<offset>(bp)";
 *   - a long shift's count stripped of ITOL or converted with LTOI (no
 *     v7 LLSHIFT); op 121 (CTOI) compiles its operand;
 *   - cexpr() is called with fpkeep raised (the floating-point stack
 *     model's "is this the outermost evaluation" counter) the first
 *     time, not when retried with regtab;
 *   - fixup: sptab pushes ("push hi / push lo" for a long, "sub sp,*8 /
 *     mov ax,sp / call fstdp" for a floating value); cctab tests a
 *     register with "or r,r" (or "orb");
 *   - a long can never be tested through cctab by regtab: -1.
 */
int
rcexpr(struct tnode *atree, struct table *atable, int reg)
{
	int r, nargs, recurf;
	int16_t fd;
	struct tnode *tree;
	struct table *table;

	if (dflag_r) {
		printf("**rcexpr**\n");
		tprint(atree, 0);
	}
	table = atable;
	recurf = 0;
	if (reg < 0) {
		recurf++;
		reg = ~reg;
		if (reg >= 020) {
			reg -= 020;
			recurf++;
		}
	}
again:
	if ((tree = atree) == 0)
		return 0;
	if (opdope[tree->op] & RELAT && tree->tr2->op == CON
	 && tree->tr2->value == 0 && table == cctab)
		tree = atree = tree->tr1;
	/*
	 * fieldselect(...) : in efftab mode,
	 * ignore the select, otherwise
	 * do the shift and mask.
	 */
	if (tree->op == FSELT) {
		if (table == efftab)
			tree = tree->tr1;
		else {
			tree->op = FSEL;
			tree = optim(tree);
		}
		atree = tree;
	}
	switch (tree->op) {

	/*
	 * Structure assignments
	 */
	case STRASG:
		strasg(tree);
		return 0;

	/*
	 * An initializing expression
	 */
	case INIT:
		tree = optim(tree);
		doinit(tree->type, tree->tr1);
		return 0;

	/*
	 * Put the value of an expression in AX (DX:AX, "fac"),
	 * for a switch or a return
	 */
	case RFORCE:
		fpkeep++;
		if (tree->tr1->type == FLOAT || tree->tr1->type == DOUBLE) {
			rcexpr(tree->tr1, regtab, reg);
			fpkeep--;
			fpdepth--;
			printf("lea\tax,fac\ncall\tfstdp\nlea\tax,fac\n");
			return 0;
		}
		if (tree->tr1->type == LONG) {
			if ((r = rcexpr(tree->tr1, regtab, reg)) != DX)
				movreg(r, DX, tree->tr1);
			fpkeep--;
			return 1;
		}
		if ((r = rcexpr(tree->tr1, regtab, reg)) != AX)
			movreg(r, AX, tree->tr1);
		if (tree->tr1->type == CHAR)
			printf("cbw\n");
		fpkeep--;
		return 0;

	/*
	 * sequential execution
	 */
	case SEQNC:
		r = nstack;
		fd = fpdepth;
		rcexpr(tree->tr1, efftab, reg);
		nstack = (int16_t)r;
		fpdepth = fd;
		atree = tree = tree->tr2;
		goto again;

	/*
	 * Handle a subroutine call. It has to be done
	 * here because if cexpr got called twice, the
	 * arguments might be compiled twice.
	 */
	case CALL1:
	case CALL:
		r = 0;
		nargs = 0;
		if (tree->tr1->op != NAME || tree->tr1->class != EXTERN) {
			nargs++;
			nstack++;
		}
		tree = tree->tr2;
		if (tree->op) {
			while (tree->op == COMMA) {
				r += comarg(tree->tr2, reg);
				tree = tree->tr1;
				nargs++;
			}
			r += comarg(tree, reg);
			nargs++;
		}
		tree = atree;
		tree->op = tree->op == CALL ? CALL2 : CALL1;
		tree->tr2 = 0;
		if (cexpr(tree, regtab, reg) < 0)
			error("compiler botch: call");
		popstk(r);
		nstack -= nargs;
		if (isfloat(tree) && table != efftab) {
			printf("call\tfldd\n");
			fpdepth++;
			if (table == sptab)
				goto fixup;
			return 0;
		}
		r = tree->type == LONG ? 1 : 0;
		if (table == efftab || table == regtab)
			return r;
		goto fixup;

	case AMPER:
		if (tree->tr1->op == NAME && tree->tr1->class == OFFS) {
			r = reg;
			printf("lea\t%s,", rname(reg));
			pnum(tree->tr1->offset);
			printf("(%s)\n", rname(tree->tr1->regno));
			goto fixup;
		}
		/* fall through - an address is never long */

	/*
	 * Longs need special treatment.
	 */
	case ASLSH:
	case LSHIFT:
		if (tree->type == LONG) {
			if (tree->tr2->op == ITOL)
				tree->tr2 = tree->tr2->tr1;
			else if (tree->tr2->type == LONG)
				tree->tr2 = optim(tnode(LTOI, INT, tree->tr2, 0));
		}
		break;

	/*
	 * Try to change * to shift.
	 */
	case TIMES:
	case ASTIMES:
		tree = pow2(tree);
		break;

	case CTOI:
		fpkeep++;
		r = rcexpr(tree->tr1, table, reg);
		fpkeep--;
		return r;
	}
	/*
	 * Try to find postfix ++ and -- operators that can be
	 * pulled out and done after the rest of the expression
	 */
	if (table != cctab && table != cregtab && recurf < 2
	 && (opdope[tree->op] & LEAF) == 0) {
		if ((r = delay(&atree, table, reg)) != 0) {
			tree = atree;
			table = efftab;
			reg = r - 1;
		}
	}
	/*
	 * Basically, try to reorder the computation
	 * so  reg = x+y  is done as  reg = x; reg =+ y
	 */
	if (recurf == 0 && reorder(&atree, table, reg)) {
		if (table == cctab && atree->op == NAME)
			return reg;
	}
	tree = atree;
	if (table == efftab && tree->op == NAME)
		return reg;
	fpkeep++;
	r = cexpr(tree, table, reg);
	fpkeep--;
	if (r >= 0)
		return r;
	if (table == cctab && tree->type == LONG)
		return -1;
	if (table != regtab && (table != cctab || (opdope[tree->op] & RELAT) == 0)) {
		if ((r = cexpr(tree, regtab, reg)) >= 0) {
	fixup:
			if (table == sptab) {
				if (tree->type == LONG) {
					printf("push\t%s\n", rname(r - 1));
					printf("push\t%s\n", rname(r));
					nstack += 2;
				} else if (tree->type == DOUBLE || tree->type == FLOAT) {
					printf("sub\tsp,*8\nmov\tax,sp\ncall\tfstdp\n");
					fpdepth--;
					nstack += 4;
				} else {
					printf("push\t%s\n", rname(r));
					nstack++;
				}
			}
			if (table == cctab && tree->type != DOUBLE && tree->type != FLOAT) {
				printf(tree->type == CHAR ? "orb" : "or");
				printf("\t%s,%s\n", rname(r), rname(r));
			}
			return r;
		}
	}
	/*
	 * There's a last chance for this operator
	 */
	if (tree->op == LTOI) {
		fpkeep++;
		r = rcexpr(tree->tr1, regtab, reg);
		fpkeep--;
		if (r >= 0) {
			r--;
			goto fixup;
		}
	}
	if (tree->type == STRUCT)
		error("Illegal operation on structure");
	else if (tree->op > 0 && tree->op < RFORCE && opntab[tree->op])
		error("No code table for op: %s", opntab[tree->op]);
	else
		error("No code table for op %d", tree->op);
	return reg;
}

/*
 * The type of a node the template interpreter reads through a pointer
 * that may be null: the original reads the word at offset 2 of address
 * 0, crt0's data word there - 4.
 */
static int
ntype(struct tnode *p)
{
	return p ? p->type : 4;
}

static struct tnode *
nonnull(struct tnode *p)
{
	if (p == 0)
		c1trap("a code template names an operand the tree does not have");
	return p;
}

/*
 * 0a82 cexpr - x86. v7's macro interpreter over the binary's own
 * templates (c1tab.c), with these differences:
 *   - option c traces each call, each emitted line ("\t| op type" and
 *     an empty line) and the register returned;
 *   - a char tree, or a relation on a char, gets at most register 1;
 *   - the value of a relation is built through cbranch() from register
 *     1 when its operand is long and no higher register is free;
 *   - ?: drops the floating-point stack model's entry of one of its
 *     arms when both are floating (not for sptab);
 *   - matched with nrleft = reg (registers are handed out downwards); a
 *     floating STAR or STARC is looked up as op 123 or 122;
 *   - floating constants go to .data as ".double"/".float %.17e";
 *   - the macros: A B (address of an operand; ' for the second word, +
 *     for the high one), C D J (b/d/s suffix of an operand or the tree),
 *     L P, G K H (compute an operand / the tree: modifiers * S C 1 ! and
 *     MUTOS's d - DX:AX), I (a register: R, R* - BX unless SI or DI -,
 *     R1, R1*, with - and + moving up and down), M (an instruction; d
 *     after it: the result in DX:AX), # " (an indirection's offset; for
 *     op 89 from its own operands), T (the conversion before a division:
 *     cwd, sub dx,dx; mov dx,*0 / sbb dx,*0 for PTOI; ITOF's operand to
 *     AX), V (a long's high-word instruction - adc, sbb, the op itself,
 *     cwd - or the line skipped), Z (a field mask "%c/%x"), S ("#"), X
 *     (xlongrel()), U Q R (the floating-point stack model: push; pop;
 *     pop with "p" while fpkeep is 0), and a literal "push"/"pop"
 *     counted in nstack;
 *   - the result register at the end: AX for *, /, PTOI, FTOI, ITOC,
 *     LTOC and the unsigned /, DX for %, FTOL, CTOL and the unsigned %.
 */
int
cexpr(struct tnode *atree, struct table *table, int areg)
{
	int c, r;
	struct tnode *p = 0, *p1, *tree;
	struct table *ctable;
	struct tnode *p2;
	char *string;
	int reg, reg1, rreg, flag = 0, opd;
	struct optab *opt;
	int bxflag, bx1flag;

	if (dflag_c) {
		printf("**cexpr**\n");
		tprint(atree, 0);
	}
	tree = atree;
	reg = areg;
	c = tree->op;
	opd = opdope[c];
	p1 = tree->tr2;
	if (((opd & RELAT) && p1->type == CHAR) || tree->type == CHAR)
		if (reg >= 2)
			reg = 1;
	/*
	 * When the value of a relational or a logical expression is
	 * desired, more work must be done.
	 */
	if ((opd & RELAT || c == LOGAND || c == LOGOR || c == EXCLA) && table != cctab) {
		c = isn++;
		if (tree->tr1->type == LONG && reg <= 0)
			cbranch(tree, c, 1, 1);
		else
			cbranch(tree, c, 1, reg);
		rcexpr(&czero, table, reg);
		branch(isn, 0, 0);
		label(c);
		rcexpr(&cone, table, reg);
		label(isn++);
		goto ret;
	}
	if (c == QUEST) {
		if (table == cctab)
			return -1;
		cbranch(tree->tr1, c = isn++, 0, reg);
		flag = nstack;
		rreg = rcexpr(p1->tr1, table, reg);
		nstack = (int16_t)flag;
		branch(r = isn++, 0, 0);
		label(c);
		reg = rcexpr(p1->tr2, table, rreg);
		if (rreg != reg)
			movreg(reg, rreg, p1->tr2);
		label(r);
		if (table != sptab && isfloat(p1->tr1) && isfloat(p1->tr2))
			fpdepth--;
		reg = rreg;
		goto ret;
	}
	reg1 = reg - 1;
	/*
	 * long values take 2 registers.
	 */
	if ((tree->type == LONG || (opd & RELAT && tree->tr1->type == LONG))
	 && tree->op != ITOL)
		reg1--;
	/*
	 * Leaves of the expression tree
	 */
	if ((r = chkleaf(tree, table, reg)) >= 0)
		return r;
	/*
	 * x + (-1) is better done as x-1.
	 */
	if ((tree->op == PLUS || tree->op == ASPLUS)
	 && (p1 = tree->tr2)->op == CON && p1->value == -1) {
		p1->value = 1;
		tree->op++;
	}
	if (table == cregtab)
		table = regtab;
	r = reg;
	c = tree->op;
	if (c == STAR && (tree->type == FLOAT || tree->type == DOUBLE))
		tree->op = FSTAR;
	else if (c == STARC && (tree->type == FLOAT || tree->type == DOUBLE))
		tree->op = FSTARC;
	if ((opt = match(tree, table, r, 0)) == 0) {
		tree->op = (int16_t)c;
		return -1;
	}
	tree->op = (int16_t)c;
	string = opt->tabstring;
	p1 = tree->tr1;
	if (p1->op == FCON && p1->value > 0) {
		printf(".data\nL%d:\t.double ", p1->value);
		c1_pfloat(p1->fvalue);
		printf("\n.text\n");
		p1->value = (int16_t)-p1->value;
	}
	if (p1->op == SFCON && p1->value > 0) {
		printf(".data\nL%d:\t.float ", p1->value);
		c1_pfloat(p1->fvalue);
		printf("\n.text\n");
		p1->value = (int16_t)-p1->value;
	}
	p2 = 0;
	if (opdope[tree->op] & BINARY) {
		/*
		 * A call's tr2 is cleared by rcexpr(): the original reads the
		 * op at address 0 then, crt0's data word 0 - no constant.
		 */
		p2 = tree->tr2;
		if (p2 == 0)
			;
		else if (p2->op == FCON && p2->value > 0) {
			printf(".data\nL%d:\t.double ", p2->value);
			c1_pfloat(p2->fvalue);
			printf("\n.text\n");
			p2->value = (int16_t)-p2->value;
		} else if (p2->op == SFCON && p2->value > 0) {
			printf(".data\nL%d:\t.float ", p2->value);
			c1_pfloat(p2->fvalue);
			printf("\n.text\n");
			p2->value = (int16_t)-p2->value;
		}
	}
	bxflag = bx1flag = 0;
loop:
	/*
	 * The 0200 bit asks for a tab.
	 */
	if ((c = *string++) & 0200) {
		c &= 0177;
		putchar('\t');
	}
	switch (c) {

	case '\n':
		if (dflag_c)
			printf("\t| %s %o\n", opntab[tree->op], (uint16_t)tree->type);
		break;

	case '\0':
		switch (tree->op) {
		case PTOI:
		case TIMES:
		case DIVIDE:
		case FTOI:
		case ASTIMES:
		case ASDIV:
		case LTOC:
		case ITOC:
		case UDIV:
		case ASUDIV:
			reg = AX;
			break;
		case MOD:
		case FTOL:
		case ASMOD:
		case CTOL:
		case UMOD:
		case ASUMOD:
			reg = DX;
			break;
		}
		goto ret;

	/* A1 */
	case 'A':
		p = p1;
		goto adr;

	/* A2 */
	case 'B':
		p = nonnull(p2);
		goto adr;

	adr:
		c = 0;
		while (*string == '\'') {
			c++;
			string++;
		}
		if (*string == '+') {
			c = 100;
			string++;
		}
		pname(p, c);
		goto loop;

	/* I */
	case 'M':
		if ((c = *string) == '\'')
			string++;
		else
			c = 0;
		prins(tree->op, c, instab);
		if (*string == 'd') {
			reg = DX;
			reg1 = AX;
			string++;
		}
		goto loop;

	/* B1 */
	case 'C':
		p = p1;
		goto pbyte;

	case 'J':
		p = tree;
		goto pbyte;

	/* B2 */
	case 'D':
		p = p2;
	pbyte:
		if (ntype(p) == CHAR)
			putchar('b');
	pb1:
		if (ntype(p) == DOUBLE) {
			nfloat = 1;
			putchar('d');
		} else if (ntype(p) == FLOAT) {
			putchar('s');
			nfloat = 1;
		}
		p = tree;
		goto loop;

	/* BE */
	case 'L':
		if (p1->type == CHAR || ntype(p2) == CHAR)
			putchar('b');
		p = tree;
		goto pb1;

	/* BF */
	case 'P':
		p = tree;
		goto pb1;

	/* F */
	case 'G':
		p = p1;
		flag = 01;
		goto subtre;

	/* S */
	case 'K':
		p = nonnull(p2);
		flag = 02;
		goto subtre;

	/* H */
	case 'H':
		p = tree;
		flag = 04;

	subtre:
		ctable = regtab;
		if (flag & 04)
			ctable = cregtab;
		c = *string++ - 'A';
		if (*string == '!') {
			string++;
			c |= 020;	/* force right register */
		}
		if (*string == 'd') {
			string++;
			c |= 040;	/* the value in DX:AX */
			reg1 = AX;
			reg = DX;
		}
		if ((c & 02) != 0)
			ctable = sptab;
		if ((c & 04) != 0)
			ctable = cctab;
		if ((flag & 01) && ctable == regtab && (c & 01) == 0
		  && (tree->op == DIVIDE || tree->op == MOD
		   || tree->op == ASDIV || tree->op == ASMOD))
			ctable = cregtab;
		if ((c & 01) != 0) {
			p = p->tr1;
			if (collcon(p))
				p = p->tr1;
		}
		if (c & 010)
			r = reg1;
		else if (opdope[p->op] & LEAF || p->degree < 2)
			r = reg;
		else
			r = areg;
		rreg = rcexpr(p, ctable, r);
		if (ctable != regtab && ctable != cregtab)
			goto loop;
		/* a base register must be SI or DI if it can */
		if ((c & 01) && (c & 020) == 0 && rreg != SI && rreg != DI
		 && (r == SI || r == DI)) {
			movreg(rreg, r, p);
			rreg = r;
			if (c & 010)
				reg1 = rreg;
			else
				reg = rreg;
		}
		if (c & 050) {
			if ((c & 060) && reg1 != rreg)
				movreg(rreg, reg1, p);
			else
				reg1 = rreg;
		} else if (rreg != reg) {
			if ((c & 020) == 0 && tree->type != LONG
			 && !(tree->type == CHAR && rreg >= SI)
			 && (flag & 04
			  || (flag & 01 && xdcalc(p2, rreg - 1) <= (opt->tabdeg2 & 077))
			  || (flag & 02 && xdcalc(p1, rreg - 1) <= (opt->tabdeg1 & 077)))) {
				reg = rreg;
				reg1 = rreg - 1;
			} else {
				movreg(rreg, reg, p);
				rreg = reg;
			}
		}
		/* R*: a base register other than SI or DI goes to BX */
		if ((c & 01) && rreg != SI && rreg != DI) {
			movreg(rreg, BX, p);
			if (c & 010)
				bx1flag++;
			else
				bxflag++;
		}
		goto loop;

	/* R, R*, R1, R1* */
	case 'I':
		c = *string++ - 'A';
		if (c & 01) {
			if (c & 010)
				r = bx1flag ? BX : reg1;
			else
				r = bxflag ? BX : reg;
		} else if (c & 010)
			r = reg1;
		else {
			r = reg;
			if (*string == '-') {
				string++;
				r++;
			}
		}
		if (*string == '+') {
			string++;
			r--;
		}
		printf("%s", rname(r));
		goto loop;

	/* #1 */
	case '#':
		if (p1->op == STARC) {
			p = p1->tr2;
			if (p->op == CON) {
				if (p->value)
					printf("*%d.", p->value);
			} else if (p->op == AMPER)
				pname(p->tr1, 0);
			else
				printf("***");
			p = p1->tr1;
			if (p->op == PLUS || p->op == MINUS) {
				p = p->tr2;
				if (p->op == CON && p->value)
					printf("+%d.", p->value);
			}
			goto loop;
		}
		p = p1->tr1;
		goto nmbr;

	/* #2 */
	case '"':
		p2 = nonnull(p2);
		if (p2->op == STARC) {
			p = p2->tr2;
			if (p->op == CON) {
				if (p->value)
					printf("*%d.", p->value);
			} else if (p->op == AMPER)
				pname(p->tr1, 0);
			else
				printf("***");
			p = p2->tr1;
			if (p->op == PLUS || p->op == MINUS) {
				p = p->tr2;
				if (p->op == CON) {
					if (p->value)
						printf("+%d.", p->value);
				} else if (p->op == AMPER)
					pname(p->tr1, 0);
			}
			goto loop;
		}
		p = p2->tr1;

	nmbr:
		if (collcon(p)) {
			p = p->tr2;
			if (p->op == CON) {
				if (p->value)
					pnum0(p->value);
			} else if (p->op == AMPER)
				pname(p->tr1, 0);
		}
		goto loop;

	/*
	 * Certain adjustments for / % and PTOI
	 */
	case 'T':
		if (tree->op == PTOI) {
			printf("mov\tdx,*0\nsbb\tdx,*0\n");
			goto loop;
		}
		if (tree->op == ITOC) {
			/*
			 * The original tests the type of the node at address
			 * 0 here (DX, the switch's remainder, is 0): crt0's
			 * data word 4, never INT - no "cbw".
			 */
			goto loop;
		}
		if (tree->op == ITOF) {
			if (reg != AX)
				movreg(reg, AX, p1);
			goto loop;
		}
		if ((tree->type == UNSIGN || tree->type & XTYPE) && p1->type != LONG)
			printf("sub\tdx,dx\n");
		else
			printf("cwd\n");
		goto loop;

	case 'V':	/* adc sbb, the op, cwd, sub dx,dx as required for longs */
		switch (tree->op) {
		case PLUS:
		case ASPLUS:
		case INCBEF:
		case INCAFT:
			printf("adc");
			goto loop;

		case MINUS:
		case ASMINUS:
		case NEG:
		case DECBEF:
		case DECAFT:
			printf("sbb");
			goto loop;

		case AND:
		case OR:
		case EXOR:
		case ASAND:
		case ASOR:
		case ASXOR:
		case ASSIGN:
			prins(tree->op, 0, instab);
			goto loop;

		case ASDIV:
		case ASMOD:
		case ASULSH:
			p = tree->tr1;
			if (p->type != LONG) {
				if (p->type == UNSIGN || p->type & XTYPE)
					printf("sub\tdx,dx");
				else
					printf("cwd");
				goto loop;
			}
			break;

		case STAR:
		case STARC:
			if (bxflag == 0 && reg != SI && reg != DI) {
				movreg(reg, BX, nonnull(p));
				bxflag++;
			}
			if (tree->type == LONG) {
				if (reg == AX)
					reg = DX;
				printf("mov");
				goto loop;
			}
			break;
		}
		/* skip the line, its newline included */
		while ((c = *string++) != '\n' && c != '\0')
			;
		if (c == '\0')
			c1trap("a 'V' line ends its template");
		goto loop;

	/*
	 * Mask used in field assignments
	 */
	case 'Z':
		r = (int16_t)~tree->mask;
		printf("%c/%x", ssize(r), (uint16_t)r);
		goto loop;

	case 'S':
		printf("#");
		goto loop;

	/*
	 * Relational on long values.
	 * Might bug out early. E.g.,
	 * (long<0) can be determined with only 1 test.
	 */
	case 'X':
		if (xlongrel(*string++ - '0'))
			return reg;
		goto loop;

	/* the floating-point stack model */
	case 'U':
		if (++fpdepth > 7)
			error("Floating point stack overflow; simplify expression");
		goto loop;

	case 'Q':
		if (--fpdepth < 0)
			error("Floating point stack underflow");
		goto loop;

	case 'R':
		if (fpkeep == 0) {
			putchar('p');
			if (--fpdepth < 0)
				error("floating point stack underflow");
		}
		goto loop;

	/* a literal push or pop */
	case 'p':
		if (strncmp(string, "ush", 3) == 0)
			nstack++;
		else if (strncmp(string, "op", 2) == 0)
			nstack--;
		break;
	}
	putchar(c);
	goto loop;

ret:
	if (dflag_c)
		printf("***cexpr returning %s\n", rname(reg));
	return reg;
}

/*
 * 18d2 reorder - v7's.
 */
int
reorder(struct tnode **treep, struct table *table, int reg)
{
	int r, o;
	struct tnode *p;

	p = *treep;
	o = p->op;
	if (opdope[o] & LEAF || o == LOGOR || o == LOGAND)
		return 0;
	while (sreorder(&p->tr1, regtab, reg, 1))
		;
	if (opdope[o] & BINARY)
		while (sreorder(&p->tr2, regtab, reg, 1))
			;
	r = 0;
	if (table != cctab)
		while (sreorder(treep, table, reg, 0))
			r++;
	*treep = optim(*treep);
	return r;
}

/*
 * 1980 sreorder - v7's.
 */
int
sreorder(struct tnode **treep, struct table *table, int reg, int recurf)
{
	struct tnode *p, *p1;

	p = *treep;
	if (opdope[p->op] & LEAF)
		return 0;
	if (p->op == PLUS && recurf)
		if (reorder(&p->tr2, table, reg))
			*treep = p = optim(p);
	p1 = p->tr1;
	if (p->op == STAR || p->op == PLUS) {
		if (recurf && reorder(&p->tr1, table, reg))
			*treep = p = optim(p);
		p1 = p->tr1;
	}
	/* a NULLOP's missing operand: the original reads op 0 at address 0 */
	if (p1 != 0 && p1->op == NAME) switch (p->op) {
		case ASLSH:
		case ASRSH:
		case ASSIGN:
			if (p1->class != REG || p1->type == CHAR || isfloat(p->tr2))
				return 0;
			if (p->op == ASSIGN) switch (p->tr2->op) {
			case TIMES:
				if (!ispow2(p->tr2))
					break;
				p->tr2 = pow2(p->tr2);
				/* fall through */
			case PLUS:
			case MINUS:
			case AND:
			case ANDN:
			case OR:
			case EXOR:
			case LSHIFT:
			case RSHIFT:
				p1 = p->tr2->tr2;
				if (xdcalc(p1, 16) > 12
				 || (p1->op == NAME
				  && (p1->nloc == p->tr1->nloc
				   || p1->regno == p->tr1->nloc)))
					return 0;
				p1 = p->tr2;
				p->tr2 = p1->tr1;
				if (p1->tr1->op != NAME
				 || p1->tr1->class != REG
				 || p1->tr1->nloc != p->tr1->nloc)
					rcexpr(p, efftab, reg);
				p->tr2 = p1->tr2;
				p->op = (int16_t)(p1->op + ASPLUS - PLUS);
				*treep = p;
				return 1;
			}
			goto OK;

		case ASTIMES:
			if (!ispow2(p))
				return 0;
			/* fall through */
		case ASPLUS:
		case ASMINUS:
		case ASAND:
		case ASANDN:
		case ASOR:
		case ASXOR:
		case INCBEF:
		case DECBEF:
		OK:
			if (table == cctab || table == cregtab)
				reg += 020;
			rcexpr(optim(p), efftab, ~reg);
			*treep = p1;
			return 1;
	}
	return 0;
}

/*
 * 1bd0 delay - delta: option d traces each call; nothing is delayed in
 * a floating expression; v7's misplaced brace (sdelay() of a unary
 * tree's missing second operand) is fixed.
 */
int
delay(struct tnode **treep, struct table *table, int reg)
{
	struct tnode *p, *p1;
	int r;

	p = *treep;
	if (dflag_d) {
		printf("**delay**\n");
		tprint(p, 0);
	}
	if (p->type == FLOAT || p->type == DOUBLE)
		return 0;
	if ((p->op == INCAFT || p->op == DECAFT)
	 && p->tr1->op == NAME) {
		return 1 + rcexpr(p->tr1, table, reg);
	}
	p1 = 0;
	if (opdope[p->op] & BINARY) {
		if (p->op == LOGAND || p->op == LOGOR)
			return 0;
		p1 = sdelay(&p->tr2);
	}
	if (p1 == 0)
		p1 = sdelay(&p->tr1);
	if (p1) {
		r = rcexpr(optim(p), table, reg);
		*treep = p1;
		return r + 1;
	}
	return 0;
}

/*
 * 1c94 sdelay - v7's.
 */
struct tnode *
sdelay(struct tnode **ap)
{
	struct tnode *p, *p1;

	p = *ap;
	if ((p->op == INCAFT || p->op == DECAFT) && p->tr1->op == NAME) {
		*ap = ncopy(p->tr1);
		return p;
	}
	if (p->op == STAR || p->op == PLUS)
		if ((p1 = sdelay(&p->tr1)) != 0)
			return p1;
	if (p->op == PLUS)
		return sdelay(&p->tr2);
	return 0;
}

/*
 * 1cfe ncopy - v7's. The copy's regno is not set (getblk()'s memory).
 */
struct tnode *
ncopy(struct tnode *p)
{
	struct tnode *q;

	if (p->class != REG)
		return p;
	q = getblk(sizeof(*q));
	q->op = p->op;
	q->type = p->type;
	q->class = p->class;
	q->offset = p->offset;
	q->nloc = p->nloc;
	return q;
}

/*
 * 1d40 chkleaf - delta: dcalc() against the register number (the free
 * registers below it), for any tree (v7 lets an indirection through).
 * The LOAD node is on the stack; its tr2 is not set.
 */
int
chkleaf(struct tnode *tree, struct table *table, int reg)
{
	struct tnode lbuf;

	if (dcalc(tree, reg) > 12)
		return -1;
	memset(&lbuf, 0, sizeof(lbuf));
	lbuf.op = LOAD;
	lbuf.type = tree->type;
	lbuf.degree = tree->degree;
	lbuf.tr1 = tree;
	return rcexpr(&lbuf, table, reg);
}

/*
 * 1d8a comarg - x86: every argument is pushed through sptab (v7 puts the
 * first into (sp)); a char goes through ITOC first; a structure of more
 * than two words is pushed from its end by a loop through BX and CX.
 * Its second argument is the register (v7: a flag pointer).
 */
int
comarg(struct tnode *atree, int reg)
{
	struct tnode *tree;
	int retval;
	int i;
	int16_t size;

	tree = atree;
	if (tree->op == STRASG) {
		size = tree->mask;
		tree = tree->tr1;
		tree = strfunc(tree);
		if (size <= 2) {
			setype(tree, INT);
			goto normal;
		}
		if (size <= 4) {
			setype(tree, LONG);
			goto normal;
		}
		if (tree->op != NAME && tree->op != STAR) {
			error("Unimplemented structure assignment");
			return 0;
		}
		tree = tnode(AMPER, STRUCT + PTR, tree, 0);
		tree = tnode(PLUS, STRUCT + PTR, tree, tconst(size, INT));
		tree = optim(tree);
		retval = rcexpr(tree, regtab, reg);
		if (tree->type == DOUBLE || tree->type == FLOAT) {
			printf("sub\tsp,*8\nmov\tax,sp\ncall\tfstdp\n");
			fpdepth--;
			nstack += 4;
		} else {
			size >>= 1;
			printf("mov\tbx,%s\n", rname(retval));
			if (size <= 2) {
				for (i = 0; i < size; i++)
					printf("\tpush\t*0(bx)\n\tsub\tbx,*2\n");
			} else {
				printf("mov\tcx,*%d.\n", size);
				printf("L%d:\tsub\tbx,*2.\npush\t(bx)\nloop\tL%d\n", isn, isn);
				isn++;
			}
			nstack++;
		}
		return (int16_t)(size * 2);
	}
normal:
	if (tree->type == CHAR)
		tree = tnode(ITOC, INT, tree, 0);
	rcexpr(tree, sptab, reg);
	return arlength(tree->type);
}

/*
 * 1eea strfunc - delta: CALL1 as well as CALL.
 */
struct tnode *
strfunc(struct tnode *tp)
{
	if (tp->op != CALL && tp->op != CALL1)
		return tp;
	setype(tp, STRUCT + PTR);
	return tnode(STAR, STRUCT, tp, 0);
}

/*
 * 1f1e doinit - x86: an int as "/%x" (".byte " first for a char), an
 * address as a tab and the name, a float or double as "\t.float" or
 * "\t.double" with "%.17e", a long as "\t.word\t/%x,/%x" (high word
 * first); the conversions through libc.a's dtoi, itod, ltof, ftol and
 * fstsp (c1lib.c).
 */
void
doinit(int atype, struct tnode *atree)
{
	struct tnode *tree;
	int type;
	unsigned char fval[8];
	int32_t lval;

	tree = atree;
	type = atype;
	if (type == CHAR) {
		printf(".byte ");
		if (tree->type & XTYPE)
			goto illinit;
		type = INT;
	}
	if (type & XTYPE)
		type = INT;
	switch (type) {
	case INT:
	case UNSIGN:
		if (tree->op == FTOI) {
			if (tree->tr1->op != FCON && tree->tr1->op != SFCON)
				goto illinit;
			tree = tree->tr1;
			tree->value = c1_dtoi(tree->fvalue);
			tree->op = CON;
		} else if (tree->op == LTOI) {
			if (tree->tr1->op != LCON)
				goto illinit;
			tree = tree->tr1;
			lval = lvalue(tree);
			tree->op = CON;
			tree->value = (int16_t)lval;
		}
		if (tree->op == CON)
			printf("/%x\n", (uint16_t)tree->value);
		else if (tree->op == AMPER) {
			printf("\t");
			pname(tree->tr1, 0);
			putchar('\n');
		} else
			goto illinit;
		return;

	case DOUBLE:
	case FLOAT:
		if (tree->op == ITOF) {
			if (tree->tr1->op != CON)
				goto illinit;
			c1_itod(tree->tr1->value, fval);
		} else if (tree->op == FCON || tree->op == SFCON)
			memcpy(fval, tree->fvalue, 8);
		else if (tree->op == LTOF) {
			if (tree->tr1->op != LCON)
				goto illinit;
			c1_ltof(lvalue(tree->tr1), fval);
		} else
			goto illinit;
		if (type == FLOAT) {
			/* fstsp keeps the high half, flds clears the rest */
			memset(fval, 0, 4);
			printf("\t.float\t");
		} else
			printf("\t.double\t");
		c1_pfloat(fval);
		printf("\n");
		return;

	case LONG:
		if (tree->op == FTOL) {
			tree = tree->tr1;
			if (tree->op == SFCON)
				tree->op = FCON;
			if (tree->op != FCON)
				goto illinit;
			lval = c1_ftol(tree->fvalue);
		} else if (tree->op == ITOL) {
			if (tree->tr1->op != CON)
				goto illinit;
			lval = tree->tr1->value;
		} else if (tree->op == LCON)
			lval = lvalue(tree);
		else
			goto illinit;
		printf("\t.word\t/%x,/%x\n", (uint16_t)((uint32_t)lval >> 16),
		    (uint16_t)lval);
		return;
	}
illinit:
	error("Illegal initialization");
}

/*
 * 211c movreg - x86: "mov <to>,<from>" by register name; a long as two
 * moves, the order chosen so the pair is not overwritten.
 */
void
movreg(int r0, int r1, struct tnode *tree)
{
	char *s;

	if (r0 == r1)
		return;
	if (tree->type == LONG) {
		s = "mov\t%s,%s\nmov\t%s,%s\n";
		if (r0 > r1)
			printf(s, rname(r1 - 1), rname(r0 - 1), rname(r1), rname(r0));
		else
			printf(s, rname(r1), rname(r0), rname(r1 - 1), rname(r0 - 1));
		return;
	}
	printf("mov\t%s,%s\n", rname(r1), rname(r0));
}
