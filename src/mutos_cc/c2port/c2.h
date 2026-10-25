/*
 * c2.h - the MUTOS 1700 object code improver (the optimizer, "cc -O"),
 * ported from the original binary.
 *
 * This is the original MUTOS 1700 c2 (mutos_bin/c2.golden.base64.txt,
 * 0411, text 17,792 bytes, stripped of its symbols): v7's c2 (v7/cc/c2.h,
 * c20.c, c21.c) rewritten for the 8086 and the MUTOS assembler's syntax,
 * with new passes of its own. The port follows the method of the c0, c1,
 * cpp, ld and cc ports (docs/C0_PORT_PLAN.md): the binary is the
 * specification, v7's source the skeleton. tests/realbin/c2map.txt maps
 * every function to its text offset in the binary, and each function's
 * comment in c20.c and c21.c names what the binary does differently from
 * v7.
 *
 * HOST REPRESENTATION: A MEMORY IMAGE. c2 keeps its instructions as
 * nodes in memory it takes with sbrk(), the operands in fixed buffers of
 * 32 bytes it copies into without a check, and it runs past some of them:
 * a long operand is copied over regs[] into the buffers after it and on
 * into optab's hash table and the stdout buffer, a long first token runs
 * past oplook()'s tmpop[] into refcount()'s labhash[], repladdr()'s
 * rt1[] and rt2[] into libc.a's BSS and the heap; a jump to an undefined
 * label increments a word through a null pointer (optab[0]'s opcode). So
 * the port keeps the original's 64K data segment as an image, c2m[],
 * with the original's layout: the binary's initialized data at address 0
 * (c2data.c, generated from the binary), every global at its address
 * (read from the code: c2map.txt), the nodes in the heap from the
 * original's break, 16-bit pointers and ints that wrap where the
 * original's wrap, chars that are signed, as the 8086 compiler's are
 * (cbw). libc.a's stdio runs on the image too - the FILE structures at
 * their addresses, the input and output buffers (_sibuf, _sobuf) where
 * the original has them - so that an overrun into them does what it does
 * in the original.
 *
 * What the image does not model is refused (c2trap()): a system call
 * after crt0's far pointer (data words 6 and 8, which a null node
 * pointer reaches) was written over, memory past the break, the stack
 * but for the argument strings (and one frame's node, movedat()'s data,
 * whose address the original stores for a moment), a heap that would
 * reach the stack (MUTOS's own break limit is not known; the emulator
 * grants any break). Two endless loops of the original are refused where
 * the port sees them (C2-01, C2-07 in docs/MUTOS_ORIGINAL_BUGS.md).
 */
#ifndef MUTOS_C2_C2_H
#define MUTOS_C2_C2_H

#include <stdint.h>

/* ---- the memory image ------------------------------------------- */

#define	C2_DSIZE	1476	/* the binary's initialized data */
#define	C2_BSS		0x05c4	/* its BSS: crt0's environ, then c2's */
#define	C2_END		0x1120	/* data + BSS: the initial break (_end) */

extern	uint8_t	c2m[65536];		/* the image */
extern	uint8_t	c2a[65536];		/* what each byte of it is */
extern	const unsigned char c2_data[C2_DSIZE];

/* c2a[] */
#define	A_NONE	0	/* not modelled: any access is refused */
#define	A_MEM	1	/* modelled memory */
#define	A_RO	3	/* read only (not used) */

#if defined(__GNUC__)
#define	NORETURN __attribute__((noreturn))
#else
#define	NORETURN
#endif

NORETURN void	c2trap(const char *what);
NORETURN void	c2trap_at(uint16_t a, int write);

/* a byte: read as a (signed) char, as the original's cbw does */
static inline int16_t
rb(uint16_t a)
{
	uint8_t t = c2a[a];

	if (t != A_MEM && t != A_RO)
		c2trap_at(a, 0);
	return (int16_t)(signed char)c2m[a];
}

/* the same byte, unsigned (the original's "sub dh,dh") */
#define	UB(a)	((uint16_t)(uint8_t)rb(a))

static inline void
wb(uint16_t a, int c)
{
	if (c2a[a] != A_MEM)
		c2trap_at(a, 1);
	c2m[a] = (uint8_t)c;
}

/* a word, little-endian; the 8086 reads one at any address */
static inline uint16_t
rw(uint16_t a)
{
	return (uint16_t)(UB(a) | (UB((uint16_t)(a + 1)) << 8));
}

static inline void
ww(uint16_t a, uint16_t v)
{
	uint16_t b = (uint16_t)(a + 1);

	if (c2a[a] != A_MEM)
		c2trap_at(a, 1);
	if (c2a[b] != A_MEM)
		c2trap_at(b, 1);
	c2m[a] = (uint8_t)v;
	c2m[b] = (uint8_t)(v >> 8);
}

#define	RI(a)	((int16_t)rw((uint16_t)(a)))		/* an int */
#define	INCW(a)	ww((uint16_t)(a), (uint16_t)(rw((uint16_t)(a)) + 1))
#define	DECW(a)	ww((uint16_t)(a), (uint16_t)(rw((uint16_t)(a)) - 1))

/* the 8086's "shl reg,cl": the count is not masked, 16 or more gives 0 */
static inline uint16_t
shl16(uint16_t v, unsigned cnt)
{
	cnt &= 0xff;
	return cnt >= 16 ? 0 : (uint16_t)(v << cnt);
}

/* ---- the node (c2.h's struct node, 16 bytes: two more than v7's) ---- */

#define	NODESZ	16
#define	N_OP	0	/* char op */
#define	N_SUBOP	1	/* char subop */
#define	N_FORW	2	/* struct node *forw */
#define	N_BACK	4	/* struct node *back */
#define	N_REF	6	/* struct node *ref */
#define	N_LABNO	8	/* int labno */
#define	N_CODE	10	/* char *code */
#define	N_REFC	12	/* int refc */
#define	N_USE	14	/* char: the registers it reads (new) */
#define	N_SET	15	/* char: the registers it writes (new) */

#define	OP(p)		rb((uint16_t)(p) + N_OP)
#define	SUBOP(p)	rb((uint16_t)(p) + N_SUBOP)
#define	FORW(p)		rw((uint16_t)(p) + N_FORW)
#define	BACK(p)		rw((uint16_t)(p) + N_BACK)
#define	REF(p)		rw((uint16_t)(p) + N_REF)
#define	LABNO(p)	RI((uint16_t)(p) + N_LABNO)
#define	CODE(p)		rw((uint16_t)(p) + N_CODE)
#define	REFC(p)		RI((uint16_t)(p) + N_REFC)
#define	USE(p)		rb((uint16_t)(p) + N_USE)
#define	SET(p)		rb((uint16_t)(p) + N_SET)

#define	S_OP(p, v)	wb((uint16_t)(p) + N_OP, (v))
#define	S_SUBOP(p, v)	wb((uint16_t)(p) + N_SUBOP, (v))
#define	S_FORW(p, v)	ww((uint16_t)(p) + N_FORW, (uint16_t)(v))
#define	S_BACK(p, v)	ww((uint16_t)(p) + N_BACK, (uint16_t)(v))
#define	S_REF(p, v)	ww((uint16_t)(p) + N_REF, (uint16_t)(v))
#define	S_LABNO(p, v)	ww((uint16_t)(p) + N_LABNO, (uint16_t)(v))
#define	S_CODE(p, v)	ww((uint16_t)(p) + N_CODE, (uint16_t)(v))
#define	S_REFC(p, v)	ww((uint16_t)(p) + N_REFC, (uint16_t)(v))
#define	S_USE(p, v)	wb((uint16_t)(p) + N_USE, (v))
#define	S_SET(p, v)	wb((uint16_t)(p) + N_SET, (v))

/* ---- the ops: optab[]'s codes (tests/realbin/c2tables.py --optab) ---- */

#define	JBR	1	/* br (and jmp, read as br) */
#define	CBR	2	/* beq .. bhis: the long conditional branches */
#define	JMP	3	/* jmp: never kept, input() makes it JBR */
#define	LABEL	4
#define	DLABEL	5
#define	POP	6	/* new */
#define	EROU	7	/* .globl */
#define	PUSH	8	/* new */
#define	JSW	9
#define	MOV	10
#define	AND	11	/* v7's CLR */
#define	OR	12	/* v7's COM */
#define	INC	13
#define	DEC	14
#define	NEG	15
#define	TST	16	/* test */
#define	SCBR	17	/* je .. jae: the short conditional branches */
#define	SJBR	18	/* j: the short branch */
#define	CWD	19	/* v7's SXT */
#define	CMP	20
#define	ADD	21
#define	SUB	22
#define	IMUL	26	/* v7's MUL */
#define	IDIV	27	/* v7's DIV */
#define	XOR	29
#define	TEXT	30
#define	DATA	31
#define	BSS	32
#define	EVEN	33
#define	SAL	34	/* sal, shl */
#define	NREGOP	46	/* "|NREG" (v7's SOB) */
#define	CALL	47	/* v7's JSR */
#define	END	48
#define	BAR	49	/* "|": a comment */
#define	RET	50
#define	NOT	51
#define	XCHG	53
#define	CBW	54
#define	LEA	55
#define	ADC	56
#define	SBB	57
#define	LOOP	58
#define	SEG	59
#define	DIV	60
#define	SAR	61
#define	SHR	62
#define	RTYP	63	/* "|RTYP" */

#define	JEQ	0
#define	JNE	1
#define	JLE	2
#define	JGE	3
#define	JLT	4
#define	JGT	5
#define	JLO	6
#define	JHI	7
#define	JLOS	8
#define	JHIS	9

#define	BYTE	100
#define	LSIZE	512
#define	LABHS	127
#define	OPHS	79	/* v7: 57 */
#define	NRT	6	/* regs[0..5]: ax dx si di bx cx; 6..11 their constants */
#define	RT1	12	/* regs[12]: the source operand */
#define	RT2	13	/* regs[13]: the destination operand */
#define	REGSZ	32	/* v7: 20 */

/* ---- the data segment (read from the code: tests/realbin/c2map.txt) */

/* initialized data */
#define	CRT0_W4		0x0004		/* crt0's words 4 and 8: the code */
#define	CRT0_W8		0x0008		/* segment of the far pointers */
#define	OPTAB		0x000a		/* struct optab optab[], 4 bytes */
#define	REVBR		0x010e		/* char revbr[10] */
#define	ISN		0x0118		/* int isn = 20000 */
#define	LASTSEG		0x011a		/* int lastseg = -1 */
#define	REGTAB		0x0454		/* isreg()'s names, two chars a word,
					   reversed: bp cx bx di si dx ax */
#define	REGNAMES	0x04f2		/* char *regnames[6] (rename()) */

/* libc.a's data */
#define	S_NULL		0x0514		/* "(null)" */
#define	STDIN		0x051c		/* _iob[0] */
#define	STDOUT		0x0524		/* _iob[1] */
#define	STDERR		0x052c		/* _iob[2] */
#define	LASTBUF		0x05bc		/* _lastbuf = &_iob[20] */
#define	CURBRK		0x05c2		/* sbrk()'s break */

/* BSS */
#define	ENVIRON		0x05c4		/* crt0's environ */
#define	LINE		0x05c6		/* char line[LSIZE] */
#define	LINELIM		0x07c4		/* &line[LSIZE-2] */
#define	FIRST		0x07c6		/* struct node first */
#define	CURLP		0x07d6		/* char *curlp */
#define	NBRBR		0x07d8		/* the counters, in c2.h's order */
#define	NSADDR		0x07da
#define	REDUNM		0x07dc
#define	IAFTBR		0x07de
#define	NJP1		0x07e0
#define	NRLAB		0x07e2
#define	NXJUMP		0x07e4
#define	NCMOT		0x07e6
#define	NREVBR		0x07e8
#define	LOOPIV		0x07ea
#define	NREDUNJ		0x07ec
#define	NSKIP		0x07ee
#define	NCOMJ		0x07f0
#define	NSOB		0x07f2		/* never counted */
#define	NRTST		0x07f4
#define	NLIT		0x07f6		/* never counted */
#define	NPUSH		0x07f8		/* "mov/push converted to push":
					   never counted (C2-02) */
#define	NPOP		0x07fa		/* "pop/push converted to mov" */
#define	NORS		0x07fc		/* "ors -> tests" */
#define	NSHORT		0x07fe		/* "long branches to short" */
#define	NCHANGE		0x0800
#define	NREG		0x0802		/* the least "|NREG n" of the chunk */
#define	DEBUG		0x0804
#define	LASTA		0x0806
#define	LASTR		0x0808
#define	FIRSTR		0x080a
#define	REGS		0x080c		/* char regs[14][32] */
#define	CONLOC		0x09ec		/* char conloc[32] */
#define	CONVAL		0x0a0c		/* char conval[32] */
#define	CCLOC		0x0a2c		/* char ccloc[32]: only ever cleared */
#define	OPHASH		0x0a4c		/* struct optab *ophash[OPHS] */
#define	OPHEND		0x0aea		/* &ophash[OPHS] */
#define	SOBUF		0x0aea		/* libc.a's _sobuf[512] (stdout) */
#define	ERRNO		0x0cea
#define	SIBUF		0x0cec		/* libc.a's _sibuf[512] (stdin) */
#define	TMPOP		0x0eee		/* oplook()'s static char tmpop[32] */
#define	LABHASH		0x0f0e		/* refcount()'s static labhash[LABHS] */
#define	LABHEND		0x100c		/* &labhash[LABHS] */
#define	RT1BUF		0x100c		/* repladdr()'s static char rt1[50] */
#define	RT2BUF		0x103e		/* repladdr()'s static char rt2[50] */
#define	NBFBUF		0x1070		/* libc.a's one-byte buffers (IO_NBF) */

#define	REG(i)		((uint16_t)(REGS + (i) * REGSZ))	/* regs[i] */

/* the FILE structure: _ptr, _cnt, _base, _flag, _file */
#define	F_PTR		0
#define	F_CNT		2
#define	F_BASE		4
#define	F_FLAG		6
#define	F_FILE		7
#define	IO_READ		01
#define	IO_WRT		02
#define	IO_NBF		04
#define	IO_MYBUF	010
#define	IO_EOF		020
#define	IO_ERR		040
#define	IO_STRG		0100
#define	IO_RW		0200

/* ---- the strings the original passes (their addresses in its data) */

#define	S_RMODE		0x0229	/* "r" */
#define	S_CANTFIND	0x022b	/* "C2: can't find %s\n" */
#define	S_WMODE		0x023e	/* "w" */
#define	S_CANTCREATE	0x0240	/* "C2: can't create %s\n" */
#define	S_ITER		0x0255	/* "%d iterations\n" */
#define	S_JJ		0x0264	/* "%d jumps to jumps\n" */
#define	S_IAJ		0x0277	/* "%d inst. after jumps\n" */
#define	S_JP2		0x028d	/* "%d jumps to .+2\n" */
#define	S_RLAB		0x029e	/* "%d redundant labels\n" */
#define	S_XJ		0x02b3	/* "%d cross-jumps\n" */
#define	S_CMOT		0x02c3	/* "%d code motions\n" */
#define	S_REVBR		0x02d4	/* "%d branches reversed\n" */
#define	S_RMOV		0x02ea	/* "%d redundant moves\n" */
#define	S_SADDR		0x02fe	/* "%d simplified addresses\n" */
#define	S_LOOPIV	0x0317	/* "%d loops inverted\n" */
#define	S_RJ		0x032a	/* "%d redundant jumps\n" */
#define	S_COMJ		0x033e	/* "%d common seqs before jmp's\n" */
#define	S_SKIP		0x035b	/* "%d skips over jumps\n" */
#define	S_RTST		0x0370	/* "%d redundant tst's\n" */
#define	S_LIT		0x0384	/* "%d literals eliminated\n" */
#define	S_PUSH		0x039c	/* "%d mov/push converted to push\n" */
#define	S_POP		0x03bb	/* "%d pop/push converted to mov\n" */
#define	S_ORS		0x03d9	/* "%d ors -> tests\n" */
#define	S_SHORT		0x03ea	/* "%d long branches to short\n" */
#define	S_CORE		0x0405	/* "%dK core\n" */
#define	S_TOOLONG	0x040f	/* "C2: Sorry, input line too long\n" */
#define	S_LABEL		0x042f	/* "L%d:" */
#define	S_DLABEL	0x0434	/* "%s:" */
#define	S_OPNAME	0x0438	/* "%s" */
#define	S_B		0x043b	/* "b" */
#define	S_TABCODE	0x043d	/* "\t%s\n" */
#define	S_TABL		0x0442	/* "\tL%d\n" */
#define	S_NL1		0x0448	/* "\n" */
#define	S_JSW		0x044a	/* "L%d\n" */
#define	S_CODE0		0x044f	/* "%s" */
#define	S_NL0		0x0452	/* "\n" */
#define	S_PDI		0x0464	/* "(di)" */
#define	S_PSI		0x0469	/* "(si)" */
#define	S_PBP		0x046e	/* "(bp)" */
#define	S_PBX		0x0473	/* "(bx)" */
#define	S_CZERO1	0x0478	/* ",*0." (rmove: sub r,r) */
#define	S_CZERO2	0x047d	/* ",*0." (rmove: or r,r) */
#define	S_CZERO3	0x0482	/* ",*0." (rmove: test r,r) */
#define	S_SP1		0x0487	/* "sp" (toofar) */
#define	S_NOSPACE	0x048a	/* "C Optimizer: out of space\n" */
#define	S_AX		0x04a5	/* "ax" .. "bp" (convreg) */
#define	S_DX		0x04a8
#define	S_SI		0x04ab
#define	S_DI		0x04ae
#define	S_BX		0x04b1
#define	S_CX		0x04b4
#define	S_BP		0x04b7
#define	S_BOTCH		0x04ba	/* "CONVREG BOTCH %d\n" */
#define	S_COMMA1	0x04cc	/* "," (repladdr) */
#define	S_COMMA2	0x04ce	/* "," (pushopt) */
#define	S_COMMA3	0x04d0	/* "," (indexopt) */
#define	S_CL1		0x04d2	/* "cl" (regmask) */
#define	S_SP2		0x04d5	/* "sp" (regmask) */
#define	S_CRET		0x04d8	/* "cret" */
#define	S_ZERO1		0x04dd	/* "*0." (simplify: mov) */
#define	S_COMMA4	0x04e1	/* "," */
#define	S_ZERO2		0x04e3	/* "*0." (simplify: cmp) */
#define	S_COMMA5	0x04e7	/* "," */
#define	S_255		0x04e9	/* "#255." */
#define	S_COMMA6	0x04ef	/* "," */
#define	S_CL2		0x04fe	/* "cl" (pinned) */

/* ---- the machine (c2lib.c) ----------------------------------------- */

void	c2_init(int argc, char **argv, uint16_t *mutos_argv);
NORETURN void	c2_exit(int16_t status);
int16_t	c2_sbrk(int16_t n);
int16_t	c_getc(void);			/* getc(stdin) */
int16_t	c_ungetc(int16_t c, uint16_t iop);
uint16_t c_freopen(uint16_t name, uint16_t mode, uint16_t iop);
void	c_printf(uint16_t fmt, const uint16_t *args, int nargs);
void	c_fprintf(uint16_t iop, uint16_t fmt, const uint16_t *args, int nargs);
int16_t	c_atoi(uint16_t s);
uint16_t c_index(uint16_t s, int16_t c);
int16_t	c_strcmp(uint16_t s1, uint16_t s2);
uint16_t c_strcpy(uint16_t s1, uint16_t s2);
uint16_t c_strcat(uint16_t s1, uint16_t s2);
uint16_t c2_datanode(void);		/* movedat()'s local "data" */

/* ---- the original's functions ------------------------------------- */

/* c20.c */
int16_t	c2main(int16_t argc, uint16_t argv);
uint16_t copy1(uint16_t ap);
uint16_t copy2(uint16_t ap, uint16_t ap2);
uint16_t insertl(uint16_t oldp);

/* c21.c */
void	rmove(void);
int16_t	jumpsw(void);
void	shortbr(void);
int16_t	equop(uint16_t ap1, uint16_t p2);
void	decref(uint16_t p);
uint16_t nonlab(uint16_t p);
uint16_t alloc(int16_t n);
void	clearreg(void);
void	singop(uint16_t ap);
void	dualop(uint16_t ap);
int16_t	isreg(uint16_t as);
void	movedat(void);
int16_t	equstr(uint16_t ap1, uint16_t ap2);
void	pushopt(void);
void	indexopt(void);
uint16_t regmask(void);
void	simplify(void);

#endif /* MUTOS_C2_C2_H */
