/*
 * c1.h - the MUTOS 1700 code generator, ported from the original binary.
 *
 * This is v7's c1.h (v7/cc/c1.h) with the MUTOS 1700 deltas the original
 * c1 (mutos_bin/c1.golden.base64.txt) shows: its new operators, its
 * register numbering and the floating-point stack model. The port follows
 * docs/C1_PORT_PLAN.md: the binary is the specification, v7's source the
 * skeleton; tests/realbin/c1map.txt maps every function to its text
 * offset in the binary, and each function's comment names the offsets
 * where the binary differs from v7.
 *
 * HOST REPRESENTATION. The original's tree nodes are v7's family of
 * structures that overlay one another (struct tnode, tname, xtname,
 * tconst, lconst, ftconst, fasgn - all start with op and type). The
 * original's code converts nodes from one kind into another in place
 * (an AMPER tnode becomes a CON, a NAME an OFFS or an AUTOI), so the
 * overlays matter. Here they are one host structure, struct tnode, whose
 * scalar fields keep the original's byte offsets 4..15 in a union - the
 * degree, a constant's value and a name's class and regno share the word
 * at offset 4 exactly as in the original - while the two tree pointers,
 * which are 16-bit words at offsets 6 and 8 in the original, are host
 * pointers beside the union. Nothing in the output depends on an address.
 *
 * Every int of the original that can reach the output is an int16_t
 * here (a long an int32_t), so that it wraps where the original wraps.
 */
#ifndef MUTOS_C1PORT_C1_H
#define MUTOS_C1PORT_C1_H

#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>

#define	NCPS	8

/*
 * Tree node: v7's struct tnode, tname, xtname, tconst, lconst, ftconst
 * and fasgn in one. The comments give each field's byte offset in the
 * original's 16-bit structures.
 */
struct tnode {
	int16_t	op;			/* 0 */
	int16_t	type;			/* 2 */
	union {
		struct {		/* tnode, fasgn */
			int16_t	degree;		/* 4 */
			int16_t	w_tr1, w_tr2;	/* 6, 8: tr1, tr2 in the original */
			int16_t	mask;		/* 10: fasgn's field mask */
		};
		struct {		/* tconst, ftconst */
			int16_t	value;		/* 4 */
			unsigned char fvalue[8];/* 6..13: a MUTOS double, lowest
						 * address first (see fltconst.h) */
		};
		struct {		/* tname, xtname */
			signed char class;	/* 4 */
			signed char regno;	/* 5 */
			int16_t	offset;		/* 6 */
			union {
				int16_t	nloc;		/* 8 */
				char	name[NCPS];	/* 8..15 (xtname) */
			};
		};
		struct {		/* lconst: high word first */
			int16_t	lvhi;		/* 4 */
			int16_t	lvlo;		/* 6 */
		};
	};
	struct tnode *tr1, *tr2;	/* 6, 8 in the original */
};

/* a long constant's value (lconst: the high word at offset 4) */
static inline int32_t lvalue(const struct tnode *p)
{
	return (int32_t)(((uint32_t)(uint16_t)p->lvhi << 16) | (uint16_t)p->lvlo);
}

static inline void setlvalue(struct tnode *p, int32_t v)
{
	p->lvhi = (int16_t)((uint32_t)v >> 16);
	p->lvlo = (int16_t)(uint32_t)v;
}

struct	optab {
	char	tabdeg1;
	char	tabtyp1;
	char	tabdeg2;
	char	tabtyp2;
	char	*tabstring;
};

struct	table {
	int	tabop;
	struct	optab *tabp;
};

struct	instab {
	int	iop;
	char	*str1;
	char	*str2;
};

struct	swtab {
	int16_t	swlab;
	int16_t	swval;
};

/* c1tab.c - generated from the binary by tests/realbin/c1tables.py --c */
extern	char	maprel[];
extern	char	notrel[];
extern	char	*regname[8];
extern	char	lrtab[2][3][6];
extern	int16_t	opdope[];
extern	char	*opntab[];
extern	struct	instab	instab[];
extern	struct	instab	branchtab[];
extern	struct	table	regtab[];
extern	struct	table	efftab[];
extern	struct	table	cctab[];
extern	struct	table	sptab[];

/* globals (data offsets in the binary, from tests/realbin/c1map.txt) */
extern	int16_t	nreg;		/* 0046: 3 */
extern	int16_t	isn;		/* 0048: 10000 */
extern	int16_t	line;		/* 4312 */
extern	int16_t	nerror;		/* 4314 */
extern	int16_t	nstack;		/* 4316 */
extern	int16_t	nfloat;		/* 4318 */
extern	int16_t	fpdepth;	/* 431a: the floating-point stack model */
extern	int16_t	fpkeep;		/* 000a: rcexpr()'s nesting counter */
extern	int16_t	stkthresh;	/* 04a6: 100, option s<n> */
extern	int16_t	dflag_a, dflag_b, dflag_c, dflag_d, dflag_r;
extern	struct	tnode	czero, cone;	/* 0030, 0036 */
extern	struct	tnode	sfuncr;		/* 003c */
extern	struct	table	*cregtab;	/* 4348 */
extern	int32_t	totspace;	/* 4332 */
extern	int16_t	xlab1, xlab2, xop, xzero;
extern	jmp_buf	fpejmp;		/* 434a */

/*
	operators
*/
#define	EOFC	0
#define	SEMI	1
#define	LBRACE	2
#define	RBRACE	3
#define	LBRACK	4
#define	RBRACK	5
#define	LPARN	6
#define	RPARN	7
#define	COLON	8
#define	COMMA	9
#define	FSEL	10
#define	FSELR	11
#define	FSELT	12
#define	FSELA	16
#define	ULSH	17
#define	ASULSH	18

#define	KEYW	19
#define	NAME	20
#define	CON	21
#define	STRING	22
#define	FCON	23
#define	SFCON	24
#define	LCON	25
#define	SLCON	26

#define	AUTOI	27
#define	AUTOD	28
#define	INCBEF	30
#define	DECBEF	31
#define	INCAFT	32
#define	DECAFT	33
#define	EXCLA	34
#define	AMPER	35
#define	STAR	36
#define	NEG	37
#define	COMPL	38

#define	DOT	39
#define	PLUS	40
#define	MINUS	41
#define	TIMES	42
#define	DIVIDE	43
#define	MOD	44
#define	RSHIFT	45
#define	LSHIFT	46
#define	AND	47
#define	ANDN	55
#define	OR	48
#define	EXOR	49
#define	ARROW	50
#define	ITOF	51
#define	FTOI	52
#define	LOGAND	53
#define	LOGOR	54
#define	FTOL	56
#define	LTOF	57
#define	ITOL	58
#define	LTOI	59
#define	ITOP	13
#define	PTOI	14
#define	LTOP	15

#define	EQUAL	60
#define	NEQUAL	61
#define	LESSEQ	62
#define	LESS	63
#define	GREATEQ	64
#define	GREAT	65
#define	LESSEQP	66
#define	LESSP	67
#define	GREATQP	68
#define	GREATP	69

#define	ASPLUS	70
#define	ASMINUS	71
#define	ASTIMES	72
#define	ASDIV	73
#define	ASMOD	74
#define	ASRSH	75
#define	ASLSH	76
#define	ASAND	77
#define	ASOR	78
#define	ASXOR	79
#define	ASSIGN	80
#define	TAND	81
#define	LTIMES	82
#define	LDIV	83
#define	LMOD	84
#define	ASANDN	85
#define	LASTIMES 86
#define	LASDIV	87
#define	LASMOD	88
#define	STARC	89	/* MUTOS: *(x+c), "*(x+c)" in opntab */

#define	QUEST	90
#define	MAX	93
#define	MAXP	94
#define	MIN	95
#define	MINP	96
#define	LLSHIFT	91
#define	ASLSHL	92
#define	SEQNC	97
#define	CALL1	98
#define	CALL2	99
#define	CALL	100
#define	MCALL	101
#define	JUMP	102
#define	CBRANCH	103
#define	INIT	104
#define	SETREG	105
#define	LOAD	106
#define	CTOL	107	/* MUTOS: char->long */
#define	LTOC	108	/* MUTOS: long->char */
#define	ITOC	109
#define	RFORCE	110

/*
 * Intermediate code operators
 */
#define	BRANCH	111
#define	LABEL	112
#define	NLABEL	113
#define	RLABEL	114
#define	STRASG	115
#define	STRSET	116
#define	UDIV	117	/* MUTOS: unsigned /, "udiv" */
#define	ASUDIV	118	/* MUTOS: unsigned /=, "=udiv" */
#define	UMOD	119	/* MUTOS: unsigned %, "umod" */
#define	ASUMOD	120	/* MUTOS: unsigned %=, "=umod" */
#define	CTOI	121	/* MUTOS: char->int */
#define	FSTARC	122	/* MUTOS: a floating STARC - only to look up a table */
#define	FSTAR	123	/* MUTOS: a floating STAR - only to look up a table */
#define	NOPS	122	/* opntab[]'s size; opdope[] has 124 */
#define	BDATA	200
#define	PROG	202
#define	DATA	203
#define	BSS	204
#define	CSPACE	205
#define	SSPACE	206
#define	SYMDEF	207
#define	SAVE	208
#define	RETRN	209
#define	EVEN	210
#define	PROFIL	212
#define	SWIT	213
#define	EXPR	214
#define	SNAME	215
#define	RNAME	216
#define	ANAME	217
#define	NULLOP	218
#define	SETSTK	219
#define	SINIT	220
#define	GLOBAL	221
#define	C3BRANCH	222

/*
 *	types
 */
#define	INT	0
#define	CHAR	1
#define	FLOAT	2
#define	DOUBLE	3
#define	STRUCT	4
#define	RSTRUCT	5
#define	LONG	6
#define	UNSIGN	7

#define	TYLEN	2
#define	TYPE	07
#define	XTYPE	(03<<3)
#define	PTR	010
#define	FUNC	020
#define	ARRAY	030

/*
	storage	classes
*/
#define	KEYWC	1
#define	MOS	10
#define	AUTO	11
#define	EXTERN	12
#define	STATIC	13
#define	REG	14
#define	STRTAG	15
#define	ARG	16
#define	OFFS	20
#define	XOFFS	21
#define	SOFFS	22

/*
 * MUTOS 1700 registers: the register numbers c1 uses, regname[]'s order.
 * They are handed out downwards from nreg (3): reg1 is reg-1, so a long
 * is in DI:SI or DX:AX.
 */
#define	AX	0
#define	DX	1
#define	SI	2
#define	DI	3
#define	CX	4
#define	BX	5
#define	BP	6
#define	SP	7

/*
	Flag	bits
*/

#define	BINARY	01
#define	LVALUE	02
#define	RELAT	04
#define	ASSGOP	010
#define	LWORD	020
#define	RWORD	040
#define	COMMUTE	0100
#define	RASSOC	0200
#define	LEAF	0400
#define	CNVRT	01000

/* c10.c */
int	main(int, char **);
void	fpeintr(int);
struct optab *match(struct tnode *, struct table *, int, int);
int	rcexpr(struct tnode *, struct table *, int);
int	cexpr(struct tnode *, struct table *, int);
int	reorder(struct tnode **, struct table *, int);
int	sreorder(struct tnode **, struct table *, int, int);
int	delay(struct tnode **, struct table *, int);
struct tnode *sdelay(struct tnode **);
struct tnode *ncopy(struct tnode *);
int	chkleaf(struct tnode *, struct table *, int);
int	comarg(struct tnode *, int);
struct tnode *strfunc(struct tnode *);
void	doinit(int, struct tnode *);
void	movreg(int, int, struct tnode *);

/* c11.c */
int	max(int, int);
int	degree(struct tnode *);
void	pname(struct tnode *, int);
void	regerr(void);
void	pbase(struct tnode *);
void	pnum(int);
int	ssize(int);
int	xdcalc(struct tnode *, int);
int	dcalc(struct tnode *, int);
int	notcompat(struct tnode *, int, int);
void	prins(int, int, struct instab *);
int	collcon(struct tnode *);
int	isfloat(struct tnode *);
int	arlength(int);
void	pswitch(struct swtab *, struct swtab *, int);
void	breq(int, int);
int	sort(struct swtab *, struct swtab *);
int	ispow2(struct tnode *);
struct tnode *pow2(struct tnode *);
void	cbranch(struct tnode *, int, int, int);
void	branch(int, int, int);
void	longrel(struct tnode *, int, int, int);
int	xlongrel(int);
void	label(int);
void	popstk(int);
void	error(const char *, ...);
void	getree(void);
int16_t	geti(void);
char	*outname(char *);
void	strasg(struct tnode *);
void	setype(struct tnode *, int);
int	decref(int);
int	incref(int);
void	tprint(struct tnode *, int);

/* c12.c */
struct tnode *optim(struct tnode *);
struct tnode *unoptim(struct tnode *);
struct tnode *lvfield(struct tnode *);
struct tnode *acommute(struct tnode *);
void	const_(int, int16_t *, int);
struct tnode *lconst(int, struct tnode *, struct tnode *);
struct tnode *tnode(int, int, struct tnode *, struct tnode *);
struct tnode *tconst(int, int);
void	*getblk(int);
void	blkinit(void);
extern	char	*funcbase, *curbase;	/* 431e, 4320 */
int	islong(int);
struct tnode *isconstant(struct tnode *);
struct tnode *fconchk(struct tnode *);
struct tnode *hardlongs(struct tnode *);
int	isregptr(struct tnode *);
void	werror(const char *, ...);

/* c1lib.c - the original's library where c1's output passes through it */
void	c1_atof(struct tnode *, const char *);
void	c1_itod(int, unsigned char *);
void	c1_ltof(int32_t, unsigned char *);
void	c1_fneg(unsigned char *);
int32_t	c1_aldiv(int32_t, int32_t);
int32_t	c1_alrem(int32_t, int32_t);
void	c1trap(const char *);
void	c1_pfloat(const unsigned char *);
int	c1_dtoi(const unsigned char *);
int32_t	c1_ftol(const unsigned char *);
char	*rname(int);
void	pnum0(int);
void	treedump(const char *, struct tnode *);

/*
 * The 8086's integer arithmetic, as the original's code does it: a
 * shift count is the low byte of the count and is not masked (from 16 on
 * a 16-bit shl gives 0, a sar the sign); idiv and div trap on a zero
 * divisor and on a quotient that does not fit (c1trap()) - for idiv on
 * the 8086 and the V30 that includes -32768 (-32768 / 1), which the
 * 80186 and the 80386 allow.
 */
static inline uint16_t shl16(uint16_t x, int n)
{
	n &= 0377;
	return n >= 16 ? 0 : (uint16_t)(x << n);
}

static inline int16_t sar16(int16_t x, int n)
{
	n &= 0377;
	return n >= 16 ? (int16_t)(x < 0 ? -1 : 0) : (int16_t)(x >> n);
}

static inline int16_t div16(int16_t a, int16_t b)
{
	if (b == 0 || (a == INT16_MIN && (b == -1 || b == 1)))
		c1trap("idiv");
	return (int16_t)(a / b);
}

static inline int16_t rem16(int16_t a, int16_t b)
{
	if (b == 0 || (a == INT16_MIN && (b == -1 || b == 1)))
		c1trap("idiv");
	return (int16_t)(a % b);
}

static inline uint16_t udiv16(int16_t a, int16_t b)
{
	if (b == 0)
		c1trap("div");
	return (uint16_t)((uint16_t)a / (uint16_t)b);
}

static inline uint16_t urem16(int16_t a, int16_t b)
{
	if (b == 0)
		c1trap("div");
	return (uint16_t)((uint16_t)a % (uint16_t)b);
}

#endif /* MUTOS_C1PORT_C1_H */
