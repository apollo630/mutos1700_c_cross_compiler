/*
 * c0.h - the MUTOS 1700 C front end, ported from the original binary.
 *
 * This is v7's c0 (v7/cc/c00.c .. c05.c, c0.h) with the MUTOS 1700 deltas
 * the original c0 (mutos/mutos_bin/c0.golden.base64.txt) shows. The port follows
 * docs/C0_PORT_PLAN.md: the binary is the specification, v7's source the
 * skeleton; tests/realbin/c0map.txt maps every function to its text
 * offset in the binary, and each function's comment names what the
 * binary does differently from v7.
 *
 * HOST REPRESENTATION: A MEMORY IMAGE. c0 lays its own structures over one
 * another - a struct hshtab (a type built for a cast) is used as a tree
 * node with op ETYPE, the tree node funcblk is initialized as a symbol,
 * a parameter list is linked through the symbols' hoffset words - and in
 * error paths it reads fields of a null tree (conexp() after a syntax
 * error returns the word at address 8). So the port keeps the original's
 * 64K data segment as an image, c0mem[], with the original's layout: the
 * binary's initialized data at address 0 (c0data.c, generated), its
 * globals at their addresses, the heap growing from the original's break
 * (0x418a) in the original's block sizes, and structure fields at their
 * byte offsets. Pointers are 16-bit addresses in the image; WI() and WU()
 * are a word as int and as unsigned, B() a (signed) char. Locals whose
 * address the original passes on (typer, mems[], dim, the expression
 * stack) live in a stack area at the top of the image (salloc()).
 *
 * Words are always at even addresses in the original's own structures;
 * a word access at an odd address - only a wild pointer makes one - is
 * refused (c0trap()), as is anything else the image does not model:
 * input bytes whose ctab[] entry is libc.a's live stdio state, a write
 * over crt0's trampoline words, the original's endless loops at end of
 * file, a CPU divide fault in fold(), a heap or stack past the image.
 *
 * Every int of the original is an int16_t here (uint16_t where it is
 * unsigned or a pointer), long an int32_t: arithmetic wraps where the
 * original's wraps.
 */
#ifndef MUTOS_C0PORT_C0_H
#define MUTOS_C0PORT_C0_H

#include <stdint.h>
#include <stdio.h>

/* ---- the memory image ------------------------------------------- */

#define	C0_DSIZE	2878	/* the binary's initialized data */
#define	C0_END		0x418a	/* data + BSS: the initial break */

extern	uint16_t c0mem[32768];
extern	const unsigned char c0_data[C0_DSIZE];
extern	int	c0_bswap;	/* 1 on a big-endian host */

#if defined(__GNUC__)
__attribute__((noreturn))
#endif
void	c0trap(const char *what);

/*
 * The parts of the original's data segment the image does not model:
 * libc.a's stdio - the FILEs of stdin, stdout, stderr and temp2 (their
 * buffer pointers and counts), temp2's buffer sbuf, the buffers of
 * stdout and stdin - and libc.a's own BSS (printf's statics, malloc's
 * state). The port does its I/O on the host; an access there - a long
 * number running over numbuf into stdout's buffer, ctab[] indexed with
 * a byte that reaches stdin's FILE - is refused (c0trap_region()).
 */
#if defined(__GNUC__)
__attribute__((noreturn))
#endif
void	c0trap_region(uint16_t a);

static inline int
c0_unmodelled(uint16_t a)
{
	return (a >= 0x0a94 && a < 0x0ab4) || (a >= 0x3a56 && a < 0x3c56)
	    || (a >= 0x3cba && a < 0x40be) || (a >= 0x40da && a < C0_END);
}

static inline uint16_t *
c0_wp(uint16_t a)
{
	if (a & 1)
		c0trap("a word at an odd address (a wild pointer)");
	if (c0_unmodelled(a))
		c0trap_region(a);
	return &c0mem[a >> 1];
}

static inline signed char *
c0_bp(uint16_t a)
{
	if (c0_unmodelled(a))
		c0trap_region(a);
	return &((signed char *)c0mem)[a ^ c0_bswap];
}

#define	WU(a)	(*c0_wp((uint16_t)(a)))			/* unsigned word, pointer */
#define	WI(a)	(*(int16_t *)c0_wp((uint16_t)(a)))	/* int */
#define	B(a)	(*c0_bp((uint16_t)(a)))			/* char (signed) */
#define	UB(a)	((unsigned char)B(a))

/* ---- tree nodes and symbols: v7's structures, byte offsets -------- */

/* struct tnode (12 bytes), cnode (10), lnode (12), fnode (10) */
#define	OP(p)		WI((p) + 0)
#define	TYP(p)		WI((p) + 2)	/* type: v7's TYPE is the mask below */
#define	SUBSP(p)	WU((p) + 4)
#define	STRP(p)		WU((p) + 6)
#define	TR1(p)		WU((p) + 8)
#define	TR2(p)		WU((p) + 10)
#define	VALUE(p)	WI((p) + 8)	/* cnode */
#define	LVHI(p)		WI((p) + 8)	/* lnode: the long, high word first */
#define	LVLO(p)		WI((p) + 10)
#define	CSTR(p)		WU((p) + 8)	/* fnode */
#define	SZ_TNODE	12
#define	SZ_CNODE	10
#define	SZ_LNODE	12
#define	SZ_FNODE	10

/* struct hshtab (22 bytes) and phshtab (the first 14) */
#define	HCLASS(p)	B((p) + 0)
#define	HFLAG(p)	B((p) + 1)
#define	HTYP(p)		WI((p) + 2)
#define	HSUBSP(p)	WU((p) + 4)
#define	HSTRP(p)	WU((p) + 6)
#define	HOFFSET(p)	WI((p) + 8)
#define	HPDOWN(p)	WU((p) + 10)
#define	HBLKLEV(p)	B((p) + 12)
#define	HNAME(p)	((uint16_t)((p) + 13))	/* char name[NCPS] */
#define	SZ_HSHTAB	22
#define	SZ_PHSHTAB	14

/* struct str (4 bytes), struct field (4 bytes) */
#define	STRSIZE(p)	WI((p) + 0)	/* ssize */
#define	MEMLIST(p)	WU((p) + 2)
#define	FLEN(p)		WI((p) + 0)
#define	BITOFFS(p)	WI((p) + 2)
#define	SZ_STR		4
#define	SZ_FIELD	4

/* struct swtab (4 bytes) */
#define	SWLAB(p)	WI((p) + 0)
#define	SWVAL(p)	WI((p) + 2)

/* struct tdim (12 bytes): rank, dimens[5] */
#define	RANK(p)		WI((p) + 0)
#define	DIMENS(p, i)	WI((p) + 2 + 2 * (i))
#define	SZ_TDIM		12

/* ---- parameters (v7's c0.h; MUTOS values where they differ) ------ */

#define	MAXINT	077777
#define	NCPS	8
#define	HSHSIZ	500	/* v7: 400 */
#define	CMSIZ	40
#define	SSIZE	20
#define	SWSIZ	230
#define	NMEMS	128
#define	NBPW	16
#define	NBPC	8
#define	NCPW	2
#define	LNCPW	2
#define	STAUTO	(-4)	/* v7: -6 (the 8086 frame saves DI and SI) */
#define	STARG	4

#define	SZCHAR	1
#define	SZINT	2
#define	SZPTR	2
#define	SZFLOAT	4
#define	SZLONG	4
#define	SZDOUB	8

/* ---- globals, at the original's data addresses (c0map.txt) -------- */

#define	isn		WI(0x000a)
#define	peeksym		WI(0x000c)
#define	line		WI(0x000e)
#define	FUNCBLK		0x0010		/* struct tnode funcblk */
#define	KWTAB		0x001c		/* struct kwtab {char *kwname; int kwval;} */
#define	HREGSYM		0x03ba		/* funchead()'s struct hshtab hreg */
#define	AREGNODE	0x03d0		/* funchead()'s struct tnode areg */
#define	OPDOPE		0x08ec
#define	CVTAB		0x09ca		/* int cvtab[5][5] */
#define	CVNTAB		0x09fc		/* char cvntab[16] */
#define	CTAB		0x0a0c
#define	FILENAME	0x0b40		/* char filename[64] */
#define	SYMBUF		0x0b80		/* char symbuf[NCPS+2] */
#define	hshused		WI(0x0b8a)
#define	HSHTAB		0x0b8c		/* struct hshtab hshtab[HSHSIZ] */
#define	HSHEND		(HSHTAB + HSHSIZ * SZ_HSHTAB)	/* 0x3684 */
#define	cp		WU(0x3684)
#define	SWTAB		0x3686		/* struct swtab swtab[SWSIZ] */
#define	swp		WU(0x3a1e)
#define	contlab		WI(0x3a20)
#define	brklab		WI(0x3a22)
#define	retlab		WI(0x3a24)
#define	deflab		WI(0x3a26)
#define	autolen		WU(0x3a28)	/* unsigned in v7's c0.h */
#define	maxauto		WU(0x3a2a)
#define	peekc		WI(0x3a2c)
#define	eof		WI(0x3a2e)
#define	funcbase	WU(0x3a30)
#define	curbase		WU(0x3a32)
#define	coremax		WU(0x3a34)
#define	maxdecl		WU(0x3a36)
#define	defsym		WU(0x3a38)
#define	funcsym		WU(0x3a3a)
#define	proflg		WI(0x3a3c)
#define	csym		WU(0x3a3e)
#define	cval		WI(0x3a40)
#define	CVAL		0x3a40		/* getcc() writes its bytes */
#define	LCVAL		0x3a42		/* long lcval: high word, low word */
#define	nchstr		WI(0x3a46)
#define	NCHSTR		0x3a46		/* a string's subsp points here */
#define	nerror		WI(0x3a48)
#define	paraml		WU(0x3a4a)
#define	parame		WU(0x3a4c)
#define	strflg		WI(0x3a4e)
#define	mosflg		WI(0x3a50)
#define	initflg		WI(0x3a52)
#define	inhdr		WI(0x3a54)
#define	regvar		WI(0x3c58)
#define	bitoffs		WI(0x3c5a)
#define	NUMBUF		0x3c5c		/* char numbuf[64] */
#define	memlist		WU(0x3c9c)
#define	nmems		WI(0x3c9e)
#define	STRUCTHOLE	0x3ca0		/* struct hshtab structhole */
#define	blklev		WI(0x3cb6)
#define	mossym		WI(0x3cb8)
#define	mpeek		WI(0x40be)	/* mapch()'s static */
#define	ZERLOC		0x40c0		/* strinit()'s static zerloc */
#define	getype_name	B(0x40c2)	/* getype()'s name flag (MUTOS) */
#define	ARGTYPE		0x40c4		/* getype()'s static struct hshtab */

/* a "" in the image (the original passes its own copies, at 03f5, 04ff) */
#define	EMPTYSTR	0x03f5

static inline int32_t
lcval_get(void)
{
	return (int32_t)(((uint32_t)WU(LCVAL) << 16) | WU(LCVAL + 2));
}

static inline void
lcval_set(int32_t v)
{
	WU(LCVAL) = (uint16_t)((uint32_t)v >> 16);
	WU(LCVAL + 2) = (uint16_t)v;
}

/* ---- operators ---------------------------------------------------- */

#define	EOFC	0
#define	NULLOP	218
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
#define	CAST	11
#define	ETYPE	12

#define	KEYW	19
#define	NAME	20
#define	CON	21
#define	STRING	22
#define	FCON	23
#define	SFCON	24
#define	LCON	25
#define	SLCON	26

#define	SIZEOF	91
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
#define	ASSAND	77
#define	ASOR	78
#define	ASXOR	79
#define	ASSIGN	80

#define	QUEST	90
#define	MAX	93
#define	MAXP	94
#define	MIN	95
#define	MINP	96
#define	SEQNC	97
#define	CALL	100
#define	MCALL	101
#define	JUMP	102
#define	CBRANCH	103
#define	INIT	104
#define	SETREG	105
#define	ITOC	109	/* MUTOS: a char conversion, its type the result's */
#define	RFORCE	110
#define	BRANCH	111
#define	LABEL	112
#define	NLABEL	113
#define	RLABEL	114
#define	STRASG	115
#define	FTOC_P	122	/* MUTOS pseudo ops of cvntab[], resolved by convert() */
#define	CTOF_P	123
#define	SEOF	200

/* types */
#define	INT	0
#define	CHAR	1
#define	FLOAT	2
#define	DOUBLE	3
#define	STRUCT	4
#define	LONG	6
#define	UNSIGN	7
#define	UNION	8

#define	ALIGN	01
#define	TYPE	07
#define	BIGTYPE	060000
#define	TYLEN	2
#define	XTYPE	(03<<3)
#define	PTR	010
#define	FUNC	020
#define	ARRAY	030

/* storage classes */
#define	KEYWC	1
#define	DEFXTRN	20
#define	TYPEDEF	9
#define	MOS	10
#define	AUTO	11
#define	EXTERN	12
#define	STATIC	13
#define	REG	14
#define	STRTAG	15
#define	ARG	16
#define	ARG1	17
#define	AREG	18
#define	MOU	21
#define	ENUMTAG	22
#define	ENUMCON	24

/* keywords */
#define	GOTO	20
#define	RETURN	21
#define	IF	22
#define	WHILE	23
#define	ELSE	24
#define	SWITCH	25
#define	CASE	26
#define	BREAK	27
#define	CONTIN	28
#define	DO	29
#define	DEFAULT	30
#define	FOR	31
#define	ENUM	32

/* characters */
#define	BSLASH	117
#define	SHARP	118
#define	INSERT	119
#define	PERIOD	120
#define	SQUOTE	121
#define	DQUOTE	122
#define	LETTER	123
#define	DIGIT	124
#define	NEWLN	125
#define	SPACE	126
#define	UNKN	127

/* special operators in intermediate code */
#define	BDATA	200
#define	WDATA	201
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
#define	SETSTK	219
#define	SINIT	220

/* opdope flag bits */
#define	BINARY	01
#define	LVALUE	02
#define	RELAT	04
#define	ASSGOP	010
#define	LWORD	020
#define	RWORD	040
#define	COMMUTE	0100
#define	RASSOC	0200
#define	LEAF	0400

/* conversion codes (cvtab[], cvntab[]) */
#define	ITF	1
#define	ITL	2
#define	LTF	3
#define	ITP	4
#define	PTI	5
#define	FTI	6
#define	LTI	7
#define	FTL	8
#define	LTP	9
#define	ITC	10
#define	XX	16	/* v7: 15 - the MUTOS codes are five bits */

/* symbol table flags */
#define	FMOS	01
#define	FKEYW	04
#define	FFIELD	020
#define	FINIT	040
#define	FLABL	0100

/* the tables, read from the image */
#define	opdope(o)	WI(OPDOPE + 2 * (int16_t)(o))
#define	cvtab(i, j)	WI(CVTAB + 10 * (i) + 2 * (j))
#define	cvntab(i)	B(CVNTAB + (int16_t)(i))
int16_t	ctab(int16_t c);

/* ---- functions (v7's, in its file order) -------------------------- */

/* c00.c */
int16_t	lookup(void);
int16_t	findkw(void);
int16_t	symbol(void);
int16_t	getnum(void);
int16_t	subseq(int16_t c, int16_t a, int16_t b);
void	putstr(int16_t lab, int16_t amax);
int16_t	getcc(void);
int16_t	mapch(int16_t ac);
uint16_t tree(void);
uint16_t xprtype(uint16_t atyb);
uint16_t copnum(int16_t len);

/* c01.c */
void	build(int16_t op);
uint16_t convert(uint16_t p, int16_t t, int16_t cvn, int16_t len);
void	setype(uint16_t ap, int16_t at, uint16_t anewp);
uint16_t chkfun(uint16_t ap);
uint16_t disarray(uint16_t ap);
void	chkw(uint16_t p, int16_t okt);
int16_t	lintyp(int16_t t);
void	error(const char *s, ...);
void	werror(const char *s, ...);
uint16_t block(int16_t op, int16_t t, uint16_t subs, uint16_t str,
	    uint16_t p1, uint16_t p2);
uint16_t nblock(uint16_t ads);
uint16_t cblock(int16_t v);
uint16_t fblock(int16_t t, uint16_t string);
uint16_t gblock(int16_t n);
void	chklval(uint16_t ap);
int16_t	fold(int16_t op, uint16_t ap1, uint16_t ap2);
int16_t	conexp(void);

/* c02.c */
void	extdef(void);
void	cfunc(void);
int16_t	cinit(uint16_t anp, int16_t flex, int16_t sclass);
void	strinit(uint16_t np, int16_t sclass);
void	setinit(uint16_t anp);
void	statement(void);
int16_t	forstmt(void);
uint16_t pexpr(void);
void	pswitch(void);
void	funchead(void);
void	blockhead(void);
void	blkend(void);
void	prste(uint16_t acs);
void	errflush(int16_t ao);

/* c03.c */
int16_t	declist(int16_t sclass);
int16_t	getkeywords(uint16_t scptr, uint16_t tptr);
uint16_t strdec(int16_t mosf, int16_t kind, int16_t di_elsize);
int16_t	declare(int16_t askw, uint16_t tptr, int16_t offset);
int16_t	decl1(int16_t askw, uint16_t atptr, int16_t offset, uint16_t absname);
void	pushdecl(uint16_t asp);
void	cpysymb(uint16_t s1, uint16_t s2);
int16_t	getype(uint16_t adimp, uint16_t absname);
void	typov(void);
int16_t	align(int16_t type, int16_t offset, int16_t aflen);
void	decsyn(int16_t o);
void	redec(void);
int16_t	goodreg(uint16_t hp);

/* c04.c */
int16_t	decref(int16_t at);
int16_t	incref(int16_t t);
void	cbranch(uint16_t t, int16_t lbl, int16_t cond);
void	rcexpr(uint16_t atp);
void	treeout(uint16_t atp, int16_t isstruct);
void	branch(int16_t lab);
void	label(int16_t l);
int16_t	plength(uint16_t ap);
int16_t	length(uint16_t acs);
int16_t	rlength(uint16_t cs);
int16_t	simplegoto(void);
int16_t	nextchar(void);
int16_t	spnextchar(void);
void	chconbrk(int16_t l);
void	dogoto(void);
void	doret(void);
void	outcode(const char *s, ...);

/* c0lib.c: the library and the machine around the program */
extern	FILE	*c0_temp1, *c0_temp2;	/* stdout and sbufp */
int16_t	c0_getchar(void);
int16_t	c0_sbrk(int16_t n);
uint16_t salloc(uint16_t n);
void	sfree(uint16_t n);
uint16_t tree_cmst(int d);
extern	int	c0_members;	/* strdec()'s member and enum lists, nested */
extern	int	c0_stmtdepth;	/* statement()s nested (dogoto()) */
extern	int	c0_tree_stmtdepth;	/* ... when tree() was last called */
extern	int	c0_cp_funchead;	/* cp last set by funchead() */
void	c0_exit(int status);
void	c0_vfprintf(FILE *f, const char *fmt, const uint16_t *args);
void	c0_init(void);
void	c0_checkcrt0(void);

/*
 * The image above the heap: tree()'s operand stacks, one per nesting
 * depth, from C0_HEAPMAX; the stack area for the other locals whose
 * address the original passes on, from C0_STACKMIN up to C0_STACKTOP.
 */
#define	C0_HEAPMAX	0xc000
#define	C0_STACKMIN	0xd000
#define	C0_STACKTOP	0xfff0

#endif
