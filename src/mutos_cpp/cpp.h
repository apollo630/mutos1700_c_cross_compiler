/*
 * cpp.h - the MUTOS 1700 C preprocessor, ported from the original binary.
 *
 * This is John F. Reiser's cpp (v7/cpp/cpp.c, cpy.y, yylex.c) as the
 * original MUTOS 1700 binary has it (mutos_bin/cpp.golden.base64.txt,
 * "@(#)cpp.c 1.6 12/12/86" and "@(#)yylex.c 1.3 12/12/86"). The port
 * follows the method of the c0 and c1 ports (docs/C0_PORT_PLAN.md): the
 * binary is the specification, v7's source the skeleton.
 * tests/realbin/cppmap.txt maps every function to its text offset in the
 * binary - which, unlike the other originals, still has its symbol table -
 * and each function's comment here names what the binary does
 * differently from v7.
 *
 * HOST REPRESENTATION: A MEMORY IMAGE. cpp works on its own memory
 * through char pointers - a scanning buffer whose contents are slid,
 * saved and pushed back, a 32000-byte string space that holds macro
 * bodies, names, file names and saved buffer hunks - and where it goes
 * past an array it reads or writes its neighbours (a ninth -I option
 * writes dirs[10], which is pflag; a byte 0200..0377 indexes fastab[]
 * below its start, reading toktyp[]). So the port keeps the original's
 * 64K data segment as an image, cppmem[], with the original's layout:
 * the binary's initialized data at address 0 (cppdata.c, generated from
 * the binary), every global at its address (the binary's symbol table),
 * 16-bit pointers, 16-bit ints that wrap where the original's wrap, and
 * chars that are signed, as the 8086 compiler's are (cbw).
 *
 * What the image does not model is refused (cpptrap()): libc.a's stdio
 * state (the FILE structures, the stdout and stdin buffers, malloc's and
 * printf's own data), crt0's words, most of the original's stack. The
 * port keeps the original's stack pointer (see "the stack" below): a
 * local array the original passes on (filname[], formtxt[], stsym()'s
 * buf[], and subst()'s whole frame) is in the image where the original's
 * is, and running past it or reading a byte of it that was not written in
 * this activation is refused - the original reads or overwrites its own
 * stack frame there.
 */
#ifndef MUTOS_CPP_CPP_H
#define MUTOS_CPP_CPP_H

#include <stdint.h>
#include <stdio.h>

/* ---- the memory image ------------------------------------------- */

#define	CPP_DSIZE	2770	/* the binary's initialized data */
#define	CPP_END		0xa7b8	/* data + BSS: the initial break (_end) */

extern	uint16_t cppw[32768];		/* the image, as words */
extern	uint8_t	cppattr[65536];		/* what each byte of it is */
extern	const unsigned char cpp_data[CPP_DSIZE];
extern	int	cpp_bswap;		/* 1 on a big-endian host */

/* cppattr[] */
#define	A_NONE	0	/* not modelled: any access is refused */
#define	A_MEM	1	/* modelled memory */
#define	A_UNDEF	2	/* a frame array's byte not written yet */
#define	A_RO	3	/* crt0's words: read, a write is refused */

#if defined(__GNUC__)
#define	NORETURN __attribute__((noreturn))
#else
#define	NORETURN
#endif

NORETURN void	cpptrap(const char *what);
NORETURN void	cpptrap_at(uint16_t a, int write);

/* a word at a computed address (an array element, a pointer) */
static inline uint16_t *
cpp_wp(uint16_t a)
{
	if (a & 1)
		cpptrap("a word at an odd address (a wild pointer)");
	if (cppattr[a] != A_MEM || cppattr[(uint16_t)(a + 1)] != A_MEM)
		cpptrap_at(a, 2);
	return &cppw[a >> 1];
}

#define	WU(a)	(*cpp_wp((uint16_t)(a)))		/* unsigned word, pointer */
#define	WI(a)	(*(int16_t *)cpp_wp((uint16_t)(a)))	/* int */

/* a global word at its fixed, modelled address */
#define	GU(a)	(cppw[(a) >> 1])
#define	GI(a)	(*(int16_t *)&cppw[(a) >> 1])

/* a byte: read as a (signed) char, as the original's cbw does */
static inline int16_t
rb(uint16_t a)
{
	uint8_t t = cppattr[a];

	if (t != A_MEM && t != A_RO)
		cpptrap_at(a, 0);
	return (int16_t)(signed char)((uint8_t *)cppw)[a ^ cpp_bswap];
}

static inline void
wb(uint16_t a, int c)
{
	uint8_t t = cppattr[a];

	if (t != A_MEM) {
		if (t != A_UNDEF)
			cpptrap_at(a, 1);
		cppattr[a] = A_MEM;
	}
	((uint8_t *)cppw)[a ^ cpp_bswap] = (uint8_t)c;
}

#define	UB(a)	((uint8_t)rb(a))

/* ---- parameters (v7's cpp.c; the binary's values) ---------------- */

#define	BUFSIZ_	512
#define	SBSIZE	32000	/* v7: 12000 */
#define	SYMSIZ	1000	/* v7: 400 */
#define	MAXINC	10
#define	MAXFRE	14
#define	MAXFRM	31
#define	NPREDEF	20
#define	NAMESIG	128	/* significant characters of a name - v7: 8 */
#define	DROP	0xFE
#define	WARN	DROP
#define	SALT	'#'
#define	READ	0

#define	BLANK	1
#define	IDENT	2
#define	NUMBR	3

#define	IB	1
#define	SB	2
#define	NB	4
#define	CB	8
#define	QB	16
#define	WB	32

/* ---- the data segment (the binary's symbol table) ---------------- */

#define	ISGADR		0x0002		/* crt0: the far pointers 2..9 */
#define	YYDEF		0x000a		/* yacc's yydef[] */
#define	savch		GU(0x009e)
#define	WARNC		0x00a0		/* char warnc = WARN */
#define	fin		GI(0x00a2)
#define	fout		GU(0x00a4)	/* FILE *: an address in _iob[] */
#define	nd		GI(0x00a6)
#define	predef		GU(0x00a8)
#define	prund		GU(0x00aa)
#define	cot_state	GI(0x00ac)	/* cotoken()'s static state */
#define	S_MATCH		0x00ae		/* subst()'s match[] */
#define	YYL_IFDEF	GI(0x03fc)	/* yylex()'s static ifdef */
#define	YYL_OP2		0x03fe		/* char *op2[8] */
#define	YYL_VAL2	0x040e		/* int val2[8] */
#define	YYL_OPC		0x041e		/* char *opc */
#define	YYEXCA		0x0420
#define	YYACT		0x04ac
#define	YYPACT		0x0782
#define	YYPGO		0x07fe
#define	YYR1		0x0806
#define	YYR2		0x0842
#define	YYCHK		0x087e
#define	yychar		GI(0x08fa)
#define	yynerrs		GI(0x08fc)
#define	yyerrflag	GI(0x08fe)
#define	IOB		0x09a6		/* FILE _iob[20], 8 bytes each */
#define	STDOUT_F	0x09ae		/* &_iob[1] */
#define	STDERR_F	0x09b6		/* &_iob[2] */
#define	CTYPE		0x0a4c		/* char _ctype_[] (isspace, isdigit) */

#define	pbeg		GU(0x0ad4)
#define	pbuf		GU(0x0ad6)
#define	pend		GU(0x0ad8)
#define	outp		GU(0x0ada)
#define	inp		GU(0x0adc)
#define	newp		GU(0x0ade)
#define	CINIT		0x0ae0		/* char cinit */
#define	MACBIT		0x0ae2		/* char macbit[ALFSIZ+11] */
#define	TOKTYP		0x0bee		/* char toktyp[ALFSIZ] */
#define	FASTAB		0x0cee		/* char fastab[ALFSIZ] */
#define	SLOTAB		0x0dee		/* char slotab[ALFSIZ] */
#define	ptrtab		GU(0x0eee)
#define	BUFFER		0x0ef0		/* char buffer[128+BUFSIZ+BUFSIZ+128] */
#define	SBF		0x13f0		/* char sbf[SBSIZE] */
#define	mactop		GI(0x90f0)
#define	fretop		GI(0x90f2)
#define	INSTACK		0x90f4		/* char *instack[MAXFRE] */
#define	BUFSTACK	0x9110
#define	ENDBUF		0x912c
#define	plvl		GI(0x9148)
#define	maclin		GI(0x914a)
#define	macfil		GU(0x914c)
#define	macnam		GU(0x914e)
#define	maclvl		GI(0x9150)
#define	macforw		GU(0x9152)
#define	macdam		GI(0x9154)
#define	INCTOP		0x9156		/* int inctop[MAXINC] */
#define	FNAMES		0x916a		/* char *fnames[MAXINC] */
#define	DIRNAMS		0x917e
#define	FINS		0x9192
#define	LINENO		0x91a6
#define	DIRS		0x91ba		/* char *dirs[10] */
#define	pflag		GI(0x91ce)
#define	passcom		GI(0x91d0)
#define	rflag		GI(0x91d2)
#define	ifno		GI(0x91d4)
#define	PRESPC		0x91d6		/* char *prespc[NPREDEF] */
#define	PUNSPC		0x91fe
#define	exfail		GI(0x9226)
#define	lastsym		GU(0x9228)
#define	STAB		0x922a		/* struct symtab stab[SYMSIZ], 4 bytes */
#define	defloc		GU(0xa1ca)
#define	udfloc		GU(0xa1cc)
#define	incloc		GU(0xa1ce)
#define	ifloc		GU(0xa1d0)
#define	elsloc		GU(0xa1d2)
#define	eifloc		GU(0xa1d4)
#define	ifdloc		GU(0xa1d6)
#define	ifnloc		GU(0xa1d8)
#define	ysysloc		GU(0xa1da)
#define	lneloc		GU(0xa1de)
#define	ulnloc		GU(0xa1e0)
#define	uflloc		GU(0xa1e2)
#define	trulvl		GI(0xa1e4)
#define	flslvl		GI(0xa1e6)
#define	SOBUF		0xa1e8		/* libc.a's _sobuf[BUFSIZ] */
#define	yylval		GI(0xa3e8)
#define	yyval		GI(0xa3ea)
#define	YYV		0xa3ec		/* int yyv[150] */

/* the element i of a word array at a */
#define	AW(a, i)	WI((uint16_t)((a) + 2 * (uint16_t)(i)))
#define	AU(a, i)	WU((uint16_t)((a) + 2 * (uint16_t)(i)))

#define	lineno_(i)	AW(LINENO, i)
#define	fnames_(i)	AU(FNAMES, i)

/* struct symtab {char *name; char *value;} at sp */
#define	SNAME(sp)	WU(sp)
#define	SVALUE(sp)	WU((uint16_t)((sp) + 2))

/* ---- the strings the original passes (their addresses in its data) */

#define	S_SAYLINE	0x00c4	/* "# %d \"%s\"\n" */
#define	S_TOKLONG	0x00cf	/* "token too long" (refill) */
#define	S_UNTERM	0x00de	/* "%s: unterminated macro call" */
#define	S_MISSENDIF	0x00fa	/* "missing endif" */
#define	S_PUSHBACK	0x0108	/* "%s: too much pushback" */
#define	S_NOSPACE1	0x011e	/* "no space" (unfill) */
#define	S_TOKLONG2	0x0127	/* "token too long" (unfill) */
#define	S_BADINCL	0x0136	/* "bad include syntax" */
#define	S_INCNEST	0x0149	/* "Unreasonable include nesting" */
#define	S_NOSPACE2	0x0166	/* "no space" (doincl) */
#define	S_SLASH		0x016f	/* "/" */
#define	S_CANTFIND	0x0171	/* "Can't find include file %s" */
#define	S_TOOMUCHDEF	0x018c	/* "too much defining" */
#define	S_ILLMAC	0x019e	/* "illegal macro name" */
#define	S_MISSPAREN	0x01b1	/* "%s: missing )" */
#define	S_BADFORMAL	0x01bf	/* "bad formal: %s" */
#define	S_MANYFORM	0x01ce	/* "too many formals: %s" */
#define	S_REDEF		0x01e3	/* "%s redefined" */
#define	S_IFLESSENDIF	0x01f0	/* "If-less endif" */
#define	S_IFLESSELSE	0x01fe	/* "If-less else" */
#define	S_UNDEFCTL	0x020b	/* "undefined control" */
#define	S_ONE		0x021d	/* " 1" */
#define	S_FNAME		0x0220	/* "%s: " */
#define	S_LINE		0x0225	/* "%d: " */
#define	S_NL		0x022a	/* "\n" */
#define	S_MANYDEF	0x022c	/* "too many defines" */
#define	S_RECURSION	0x023d	/* "%s: macro recursion" */
#define	S_PCTD		0x0251	/* "%d" */
#define	S_QPCTS		0x0254	/* "\"%s\"" */
#define	S_ACTLONG	0x0259	/* "%s: actuals too long" */
#define	S_EMPTY1	0x026f	/* ""+1: a missing actual */
#define	S_IDCHARS	0x026f	/* "_$ABC...xyz0123456789" */
#define	S_NUMCHARS	0x02b0	/* "0123456789." */
#define	S_SBCHARS	0x02bc	/* "\n\"'/\\" */
#define	S_QBCHARS	0x02c2	/* "\n\"'\\" */
#define	S_CBCHARS	0x02c7	/* "*\n" */
#define	S_BLANKS	0x02ca	/* " \t\013\f\r" */
#define	S_EMPTY		0x02d0	/* "" */
#define	S_DOT		0x02d1	/* "." */
#define	S_MANYD		0x02d3	/* "too many -D options, ignoring %s" */
#define	S_MANYU		0x02f4	/* "too many -U options, ignoring %s" */
#define	S_EXCESSI	0x0315	/* "excessive -I file (%s) ignored" */
#define	S_UNKFLAG	0x0334	/* "unknown flag %s" */
#define	S_NOSOURCE	0x0344	/* "No source file %s" */
#define	S_CANTCREATE	0x0358	/* "Can't create %s" */
#define	S_EXTRANEOUS	0x0368	/* "extraneous name %s" */
#define	S_USRINCLUDE	0x037b	/* "/usr/include" */
#define	S_DEFINE	0x0388	/* "define" */
#define	S_UNDEF		0x038f	/* "undef" */
#define	S_INCLUDE	0x0395	/* "include" */
#define	S_ELSE		0x039d	/* "else" */
#define	S_ENDIF		0x03a2	/* "endif" */
#define	S_IFDEF		0x03a8	/* "ifdef" */
#define	S_IFNDEF	0x03ae	/* "ifndef" */
#define	S_IF		0x03b5	/* "if" */
#define	S_LINEKW	0x03b8	/* "line" */
#define	S_MUTOS		0x03bd	/* "mutos" */
#define	S_ULINE		0x03c3	/* "__LINE__" */
#define	S_UFILE		0x03cc	/* "__FILE__" */
#define	S_CMDLINE	0x03d5	/* "command line" */
#define	S_YYOPS1	0x0925	/* the one-character operators: + - * / % < > & ^ | ? : ! ~ ( ) , */
#define	S_DEFINED	0x0937	/* "defined" */
#define	S_BSNL		0x093f	/* "\\\n" */
#define	S_ILLCHAR	0x0942	/* "Illegal character %c in preprocessor if" */
#define	S_ILLNUM	0x096a	/* "Illegal number %s" */
#define	S_YYOVERFLOW	0x097c	/* "yacc stack overflow" */
#define	S_SYNTAX	0x0990	/* "syntax error" */

/* ---- the stack: the original's frames ---------------------------- */

/*
 * The port keeps the original's stack pointer: cpp_sp starts where crt0
 * leaves it - below the argument words, which cpp_init() lays out as
 * tests/realbin/mutosemu.py does - and every ported function moves it as
 * the original's code does: the caller's arguments, the return address,
 * BP, DI and SI, then the function's locals ("sub sp,N", or chkstk's N+2).
 * So a frame is at the address the original's is, which the original can
 * observe: an overrun of subst()'s acttxt[] leaves a pointer with its high
 * byte, the frame's own (CPP-05). Of a frame, the image models what the
 * original passes on or overruns - doincl()'s filname[], dodef()'s
 * formtxt[], stsym()'s buf[], subst()'s whole frame up to its saved BP
 * (at the original's offsets) -, undefined until written in that
 * activation (fr_array()); the rest of the stack is not modelled: running
 * past such an array, or reading a byte of it not written, is refused -
 * the original reads or overwrites its own frame there.
 *
 * Exact on every path that reaches one of those frames; a call with a
 * temporary pushed before its arguments (dirnams[ifno] = trmdir(...))
 * places its callee's frame two bytes off - none of those callees has
 * an array or calls a function that has.
 */
#define	F_ARGTOP	0xfff0	/* the argument strings go below this */
#define	CPP_STACKMIN	0xb000	/* the stack stays above, the heap below */

extern	uint16_t cpp_sp;

uint16_t fr_enter(int nargs, uint16_t locals);
void	fr_leave(uint16_t bp, int nargs);
void	fr_array(uint16_t a, uint16_t n);

/* ---- the machine (cpplib.c) --------------------------------------- */

void	cpp_init(int argc, char **argv, uint16_t *mutos_argv);
int16_t	cpp_open(uint16_t name, int rooted);
int16_t	cpp_read(int16_t fd, uint16_t buf, int16_t n);
void	cpp_close(int16_t fd);
int	cpp_rooted(uint16_t name);
void	cpp_set_rooted(uint16_t name, int r);
uint16_t cpp_fopen_w(uint16_t name);
void	cpp_fclose_stdout(void);
void	cpp_putc(int c, uint16_t f);
void	cpp_fprintf(uint16_t f, uint16_t fmt, const uint16_t *args, int nargs);
void	cpp_sprintf(uint16_t buf, uint16_t fmt, const uint16_t *args, int nargs);
uint16_t cpp_calloc(uint16_t n, uint16_t m);
NORETURN void	cpp_exit(int16_t status);
void	cpp_crt0(void);

int16_t	cstrcmp(uint16_t a, uint16_t b);
void	cstrcpy(uint16_t d, uint16_t s);
void	cstrcat(uint16_t d, uint16_t s);

/* ---- the original's functions -------------------------------------- */

void	sayline(void);
void	dump(void);
uint16_t refill(uint16_t p);
uint16_t cotoken(uint16_t p);
uint16_t skipbl(uint16_t p);
uint16_t unfill(uint16_t p);
uint16_t doincl(uint16_t p);
int16_t	equfrm(uint16_t a, uint16_t p1, uint16_t p2);
uint16_t dodef(uint16_t p);
uint16_t control(uint16_t p);
uint16_t savestr(uint16_t start, uint16_t finish);
uint16_t stsym(uint16_t s);
uint16_t ppsym(uint16_t s);
void	pperror(uint16_t s, uint16_t x, uint16_t y);
void	yyerror(uint16_t s, uint16_t a, uint16_t b);
void	ppwarn(uint16_t s, uint16_t x);
uint16_t lookup(uint16_t namep, int16_t enterf);
uint16_t slookup(uint16_t p1, uint16_t p2, int16_t enterf);
uint16_t subst(uint16_t p, uint16_t sp, uint16_t caller_di);
uint16_t trmdir(uint16_t s);
uint16_t copy(uint16_t s);
uint16_t strdex(uint16_t s, int16_t c);
int16_t	cppmain(int16_t argc, uint16_t argv);

int16_t	yylex(void);
int16_t	tobinary(uint16_t st, int16_t b);
int16_t	yyparse(void);

#endif /* MUTOS_CPP_CPP_H */
