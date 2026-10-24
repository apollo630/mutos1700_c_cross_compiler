/*
 * cc.h - the MUTOS 1700 C compiler driver, ported from the original binary.
 *
 * This is v7's cc (v7/cc/cc.c) as the original MUTOS 1700 binary has it
 * (mutos_bin/cc.golden.base64.txt, 0407, text 8,026 bytes, stripped of
 * its symbols). The port follows the method of the c0, c1, cpp and ld
 * ports (docs/C0_PORT_PLAN.md): the binary is the specification, v7's
 * source the skeleton. tests/realbin/ccmap.txt maps every function to its
 * text offset in the binary, and each function's comment in cc.c names
 * what the binary does differently from v7.
 *
 * HOST REPRESENTATION: A MEMORY IMAGE. cc keeps its file names in fixed
 * arrays and copies into them without a check: a -K argument is appended
 * to the 10-byte kopt[] and runs over pass0[] and the strings after it, a
 * -B prefix is copied into the 20-byte pass0[] .. passp[], a long file
 * name copied near the end of ts[] runs into av[], more than 50 arguments
 * for ld run past av[] into clist[] and llist[], and -P and -E store into
 * main()'s ptemp[] without a check - the 101st into main()'s pv itself.
 * It also reads through wild pointers: with -P, tmp0 stays 0 and copy(tmp0)
 * copies crt0's code from address 0; an "-o" whose name is never resolved
 * leaves outfile at -1 and ld gets the string at 0xffff. So the port keeps
 * the original's 64K segment as an image, ccm[], with the original's
 * layout: cc is 0407, one segment - the text at 0 (read only: the port
 * only reads it), the data at 1f5a (ccdata.c holds both, generated from
 * the binary), the BSS at 22d8, the heap from 2db0 (malloc()'s arenas, on
 * the image), main()'s frame and the arguments at the top, laid out as
 * crt0 and tests/realbin/mutosemu.py lay them out. Words are
 * little-endian and may be odd (the 8086 takes them there); chars are
 * signed, as the 8086 compiler's are (cbw).
 *
 * What the image does not model is refused (cctrap()): libc.a's stdio
 * structures and buffers (the port's printf is libc.a's, re-enacted -
 * cclib.c), its signal table and chkstk's limit, crt0's environ but for
 * reading, the frames below main()'s, a write into the text.
 *
 * PATHS. The original names the MUTOS file system by absolute paths: its
 * passes (/lib/cpp, /lib/c0, /lib/c1, /lib/c2, /bin/as, /bin/ld; -f's
 * /lib/c0f, /lib/c1f, /bin/asf; -t's /usr/src/cmd/c/), the startup file it
 * gives ld (/lib/crt0.o, /lib/mcrt0.o, /lib/crt2.o, /lib/crt20.o) and its
 * temporary files (/tmp/ctm...). Those are looked up under the MUTOS root
 * (src/h/mutos_root.h) - where cc itself uses them (execv(), creat(),
 * access(), unlink()) and where it passes them on to a pass, which gets
 * the host path. A name from the command line - a source, an object, -o's
 * name, -I's directory, -B's prefix - is the host's (the rule of
 * docs/TOOLCHAIN_PLAN.md, decision 4). The image tells the two apart by a
 * mark on every byte that came from the command line (cctaint[]), carried
 * along where cc copies a string (copy(), strcpy(), strcat()): an
 * absolute name is a MUTOS path when its '/' is not so marked.
 */
#ifndef MUTOS_CC_CC_H
#define MUTOS_CC_CC_H

#include <stdint.h>

/* ---- the memory image ------------------------------------------- */

#define	CC_TSIZE	0x1f5a	/* the text: 8026 bytes */
#define	CC_SEGSIZE	0x22d8	/* text and data: 8920 bytes (ccdata.c) */
#define	CC_BSS		0x22d8	/* the BSS: crt0's environ, then cc.o's */
#define	CC_END		0x2db0	/* data + BSS: the initial break (_end) */

extern	uint8_t	ccm[65536];		/* the image */
extern	uint8_t	ccattr[65536];		/* what each byte of it is */
extern	uint8_t	cctaint[65536];		/* 1: from the command line */
extern	const unsigned char cc_seg[CC_SEGSIZE];

/* ccattr[] */
#define	A_NONE	0	/* not modelled: any access is refused */
#define	A_MEM	1	/* modelled memory */
#define	A_RO	3	/* the text, crt0's words: read, a write is refused */

#if defined(__GNUC__)
#define	NORETURN __attribute__((noreturn))
#else
#define	NORETURN
#endif

NORETURN void	cctrap(const char *what);
NORETURN void	cctrap_at(uint16_t a, int write);

static inline int
cc_readable(uint16_t a)
{
	return ccattr[a] == A_MEM || ccattr[a] == A_RO;
}

/* a word: little-endian, at any address (the 8086 takes an odd one) */
static inline uint16_t
rw(uint16_t a)
{
	uint16_t b = (uint16_t)(a + 1);

	if (!cc_readable(a))
		cctrap_at(a, 0);
	if (!cc_readable(b))
		cctrap_at(b, 0);
	return (uint16_t)(ccm[a] | (ccm[b] << 8));
}

static inline void
ww(uint16_t a, uint16_t v)
{
	uint16_t b = (uint16_t)(a + 1);

	if (ccattr[a] != A_MEM)
		cctrap_at(a, 1);
	if (ccattr[b] != A_MEM)
		cctrap_at(b, 1);
	ccm[a] = (uint8_t)v;
	ccm[b] = (uint8_t)(v >> 8);
	cctaint[a] = cctaint[b] = 0;
}

#define	ri(a)	((int16_t)rw((uint16_t)(a)))

/* a byte: read as a (signed) char, as the original's cbw does */
static inline int16_t
rb(uint16_t a)
{
	if (!cc_readable(a))
		cctrap_at(a, 0);
	return (int16_t)(signed char)ccm[a];
}

/* a byte written: a constant, so not from the command line */
static inline void
wb(uint16_t a, int c)
{
	if (ccattr[a] != A_MEM)
		cctrap_at(a, 1);
	ccm[a] = (uint8_t)c;
	cctaint[a] = 0;
}

/* a byte copied from address s to address d, with its mark */
static inline int16_t
cpb(uint16_t d, uint16_t s)
{
	int16_t c = rb(s);
	uint8_t t = cctaint[s];

	if (ccattr[d] != A_MEM)
		cctrap_at(d, 1);
	ccm[d] = (uint8_t)c;
	cctaint[d] = t;
	return c;
}

/* ---- the segment (read from the code: tests/realbin/ccmap.txt) ----- */

/* crt0's words */
#define	CRT0_W		0x1f5a	/* 1f5a..1f63: the far pointers */

/* cc.o's initialized data */
#define	tsa		0x1f64	/* char *tsa = ts */
#define	tsp		0x1f66	/* char *tsp = ts */
#define	KOPT		0x1f68	/* char kopt[10] = "s": c1's options */
#define	PASS0		0x1f72	/* char pass0[20] = "/lib/c0" */
#define	PASS1		0x1f86	/* char pass1[20] = "/lib/c1" */
#define	PASS2		0x1f9a	/* char pass2[20] = "/lib/c2" */
#define	PASSP		0x1fae	/* char passp[20] = "/lib/cpp" */
#define	PASSA		0x1fc2	/* char passa[20] = "/bin/as" */
#define	pref		0x1fd6	/* char *pref = "/lib/crt0.o" */

/* libc.a's data */
#define	S_NULL		0x2200	/* "(null)" */
#define	IOB		0x2208	/* FILE _iob[20]: not modelled */
#define	alloch		0x22aa	/* malloc(): the ring's head */
#define	allocp		0x22ac	/* malloc(): where a search starts */
#define	S_SLASH		0x22ae	/* mktemp()'s "/" */
#define	curbrk		0x22b0	/* sbrk(): the break */
#define	LIBC_DATAEND	0x22d8

/* BSS */
#define	environ_	0x22d8	/* crt0's environ (read only) */
#define	tmp0		0x22da
#define	tmp1		0x22dc
#define	tmp2		0x22de
#define	tmp3		0x22e0
#define	tmp4		0x22e2
#define	tmp5		0x22e4
#define	outfile		0x22e6	/* -1 after "-o x.c" until a .o names it */
#define	TS		0x22e8	/* char ts[CHSPACE + 50] */
#define	AV		0x2702	/* char *av[50] */
#define	CLIST		0x2766	/* char *clist[MAXFIL] */
#define	LLIST		0x282e	/* char *llist[MAXLIB] */
#define	pflag		0x28f6
#define	sflag		0x28f8
#define	cflag		0x28fa
#define	eflag		0x28fc
#define	exflag		0x28fe
#define	oflag		0x2900
#define	proflag		0x2902
#define	aflag		0x2904	/* -a: "Executing ..." */
#define	fflag		0x2906	/* -f: c0f, c1f, asf, -lf */
#define	kflag		0x2908	/* -K: c1 gets kopt[] */
#define	chpass		0x290a
#define	npassname	0x290c
#define	CC_BSSEND	0x290e	/* cc.o's BSS ends; libc.a's follows */
#define	allocs		0x2daa	/* malloc(): the ring's first word */
#define	allocx		0x2dac	/* malloc(): realloc()'s word */
#define	mtemp		0x2dae	/* malloc()'s static temp */

#define	CHSPACE		1000
#define	MAXFIL		100
#define	MAXLIB		100
#define	MAXOPT		100

/* the strings the original passes (their addresses in its data) */
#define	S_CRT0		0x1fd8	/* "/lib/crt0.o" */
#define	S_CRT2		0x1fe4	/* "/lib/crt2.o" */
#define	S_CRT20		0x1ff0	/* "/lib/crt20.o" */
#define	S_DIUC		0x1ffd	/* "Too many DIUC options" */
#define	S_TOVER		0x2013	/* "-t overwrites earlier option" */
#define	S_012P1		0x2030	/* "012p" (-t) */
#define	S_BOVER		0x2035	/* "-B overwrites earlier option" */
#define	S_USRSRCO	0x2052	/* "/usr/src/cmd/c/o" (-B) */
#define	S_MANYSRC	0x2063	/* "Too many source files" */
#define	S_MANYOBJ	0x2079	/* "Too many object/library files" */
#define	S_012P2		0x2097	/* "012p" */
#define	S_USRSRC	0x209c	/* "/usr/src/cmd/c/" */
#define	S_C0		0x20ac	/* "c0" (-t) */
#define	S_C1		0x20af	/* "c1" */
#define	S_C2		0x20b2	/* "c2" */
#define	S_CPP		0x20b5	/* "cpp" */
#define	S_MCRT0		0x20b9	/* "/lib/mcrt0.o" */
#define	S_TMPNAME	0x20c6	/* "/tmp/ctm0XXXXXX" */
#define	S_NOTEMP	0x20d6	/* "cc: cannot create temp" */
#define	S_FILE		0x20ed	/* "%s:\n" */
#define	S_AVCPP		0x20f2	/* "cpp" */
#define	S_DASH		0x20f6	/* "-" */
#define	S_XCPP		0x20f8	/* "Executing cpp...\n" */
#define	S_AVC0F		0x210a	/* "c0f" */
#define	S_AVC0		0x210e	/* "c0" */
#define	S_DASHP		0x2111	/* "-P" */
#define	S_XC0		0x2114	/* "Executing %s...\n" */
#define	S_LIBC0F	0x2125	/* "/lib/c0f" */
#define	S_AVC1F		0x212e	/* "c1f" */
#define	S_AVC1		0x2132	/* "c1" */
#define	S_XC1		0x2135	/* "Executing %s...\n" */
#define	S_LIBC1F	0x2146	/* "/lib/c1f" */
#define	S_AVC2		0x214f	/* "c2" */
#define	S_XC2		0x2152	/* "Executing Optimizer...\n" */
#define	S_AVASF		0x216a	/* "asf" */
#define	S_AVAS		0x216e	/* "as" */
#define	S_GC		0x2171	/* "-gC" */
#define	S_DASHO1	0x2175	/* "-o" (as) */
#define	S_XAS		0x2178	/* "Assembler %s ...\n" */
#define	S_BINASF	0x218a	/* "/bin/asf" */
#define	S_AVLD		0x2193	/* "ld" */
#define	S_DASHX		0x2196	/* "-X" */
#define	S_DASHO2	0x2199	/* "-o" (ld) */
#define	S_L2		0x219c	/* "-l2" */
#define	S_LF		0x21a0	/* "-lf" */
#define	S_LC		0x21a4	/* "-lc" */
#define	S_XLD		0x21a8	/* "Loader...\n" */
#define	S_BINLD		0x21b3	/* "/bin/ld" */
#define	S_CANTFIND	0x21bb	/* "Can't find %s\n" */
#define	S_TRYAGAIN	0x21ca	/* "Try again\n" */
#define	S_FATAL		0x21d5	/* "Fatal error in %s\n" */
#define	S_NOSPACE	0x21e8	/* "no space for file names" */

/* ---- the machine (cclib.c) ----------------------------------------- */

/* stdout and stderr, as error() picks them (libc.a's &_iob[1], &_iob[2]) */
#define	STDOUT_	0x2210
#define	STDERR_	0x2218

void	cc_init(int argc, char **argv);
void	cc_printf(uint16_t fmt, const uint16_t *args, int nargs);
void	cc_fprintf(uint16_t iop, uint16_t fmt, const uint16_t *args, int nargs);
void	cc_putc(int c, uint16_t iop);
NORETURN void	cc_exit(int16_t status);
uint16_t cc_strcpy(uint16_t d, uint16_t s);
uint16_t cc_strcat(uint16_t d, uint16_t s);
uint16_t cc_malloc(uint16_t nbytes);
uint16_t cc_mktemp(uint16_t as);
int16_t	cc_creat(uint16_t name, int16_t mode);
int16_t	cc_close(int16_t fd);
int16_t	cc_unlink(uint16_t name);
void	cc_signals(void (*handler)(void));
void	cc_prepare_exec(uint16_t f, uint16_t v);
int16_t	cc_fork(void);
void	cc_execv(void);
int16_t	cc_wait(uint16_t *status);

/* ---- the original's functions -------------------------------------- */

extern	uint16_t cc_bp;		/* main()'s BP: its frame in the image */
int16_t	ccmain(void);
NORETURN void	dexit(void);

#endif /* MUTOS_CC_CC_H */
