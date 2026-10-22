/*
 * ld.h - the MUTOS 1700 link editor, ported from the original binary.
 *
 * This is v7's ld (v7/ld/ld.c) as the original MUTOS 1700 binary has it
 * (mutos_bin/ld.golden.base64.txt, 0411, text 12,480 bytes, stripped of
 * its symbols). The port follows the method of the c0, c1 and cpp ports
 * (docs/C0_PORT_PLAN.md): the binary is the specification, v7's source
 * the skeleton. tests/realbin/ldmap.txt maps every function to its text
 * offset in the binary, and each function's comment in ld.c names what
 * the binary does differently from v7.
 *
 * HOST REPRESENTATION: A MEMORY IMAGE. ld keeps everything in fixed
 * arrays of its data segment and runs past some of them without a check:
 * a 257th archive member is written past liblist[] into cursym and
 * symtab[], a 17th -v option past vnodes[] into the page buffers, a -l
 * name of 14 or more characters over the strings that follow
 * "/usr/lib/libxxxxxxxxxxxxxxx" ("cannot open", the formats error()
 * prints with), and ldrand() reads a symbol type through a null pointer
 * (crt0's word 8). So the port keeps the original's 64K data segment as
 * an image, ldw[], with the original's layout: the binary's initialized
 * data at address 0 (lddata.c, generated from the binary), every global
 * at its address (read from the code: ldmap.txt), 16-bit pointers and
 * ints that wrap where the original's wrap, chars that are signed, as the
 * 8086 compiler's are (cbw), and longs with their high word first.
 *
 * What the image does not model is refused (ldtrap()): libc.a's data
 * (the stdio structures, printf's and malloc's state, the stdout buffer),
 * crt0's words but for reading, the stack. The one local whose address
 * ld passes on - getfile()'s struct stat - only fstat() fills, so no frame
 * needs to be in the image.
 *
 * The extension: -r and -d, which the original refuses ("ld: -r and -d
 * flags not supported", exit status 4), are kept (docs/TOOLCHAIN_PLAN.md,
 * decision 5) - v7's code for them, put back where the original took it
 * out, on flags that live outside the image (ld.c, "the extension").
 * Without -r and -d the port is the original.
 */
#ifndef MUTOS_LD_LD_H
#define MUTOS_LD_LD_H

#include <stdint.h>

/* ---- the memory image ------------------------------------------- */

#define	LD_DSIZE	952	/* the binary's initialized data */
#define	LD_BSS		0x03b8	/* its BSS: crt0's environ, then ld.o's */
#define	LD_END		0x91d6	/* data + BSS: the initial break (_end) */

extern	uint16_t ldw[32768];		/* the image, as words */
extern	uint8_t	ldattr[65536];		/* what each byte of it is */
extern	const unsigned char ld_data[LD_DSIZE];
extern	int	ld_bswap;

/* ldattr[] */
#define	A_NONE	0	/* not modelled: any access is refused */
#define	A_MEM	1	/* modelled memory */
#define	A_RO	3	/* crt0's words: read, a write is refused */

#if defined(__GNUC__)
#define	NORETURN __attribute__((noreturn))
#else
#define	NORETURN
#endif

NORETURN void	ldtrap(const char *what);
NORETURN void	ldtrap_at(uint16_t a, int write);

/* a word at a computed address (an array element, a pointer, a field) */
static inline uint16_t *
ld_wp(uint16_t a)
{
	if (a & 1)
		ldtrap("a word at an odd address (a wild pointer)");
	if (ldattr[a] != A_MEM || ldattr[(uint16_t)(a + 1)] != A_MEM)
		ldtrap_at(a, 2);
	return &ldw[a >> 1];
}

/* a word read at a computed address: crt0's words may be read */
static inline uint16_t
ld_rw(uint16_t a)
{
	uint8_t t = ldattr[a];

	if (a & 1)
		ldtrap("a word at an odd address (a wild pointer)");
	if ((t != A_MEM && t != A_RO) || ldattr[(uint16_t)(a + 1)] != t)
		ldtrap_at(a, 0);
	return ldw[a >> 1];
}

#define	WU(a)	(*ld_wp((uint16_t)(a)))			/* unsigned word, pointer */
#define	WI(a)	(*(int16_t *)ld_wp((uint16_t)(a)))	/* int */
#define	RU(a)	(ld_rw((uint16_t)(a)))			/* a read only */

/* a global word at its fixed, modelled address */
#define	GU(a)	(ldw[(a) >> 1])
#define	GI(a)	(*(int16_t *)&ldw[(a) >> 1])

/* a byte: read as a (signed) char, as the original's cbw does */
static inline int16_t
rb(uint16_t a)
{
	uint8_t t = ldattr[a];

	if (t != A_MEM && t != A_RO)
		ldtrap_at(a, 0);
	return (int16_t)(signed char)((uint8_t *)ldw)[a ^ ld_bswap];
}

static inline void
wb(uint16_t a, int c)
{
	if (ldattr[a] != A_MEM)
		ldtrap_at(a, 1);
	((uint8_t *)ldw)[a ^ ld_bswap] = (uint8_t)c;
}

#define	UB(a)	((uint8_t)rb(a))

/* a long of the image: the high word first (the MUTOS compiler's order) */
static inline int32_t
rl(uint16_t a)
{
	return (int32_t)(((uint32_t)WU(a) << 16) | WU((uint16_t)(a + 2)));
}

static inline void
wl(uint16_t a, int32_t v)
{
	WU(a) = (uint16_t)((uint32_t)v >> 16);
	WU((uint16_t)(a + 2)) = (uint16_t)v;
}

/* ---- v7's constants (the binary's values) -------------------------- */

#define	ARCMAGIC 0177545
#define	OMAGIC	0405
#define	FMAGIC	0407
#define	NMAGIC	0410
#define	IMAGIC	0411

#define	EXTERN	040
#define	UNDEF	00
#define	ABS	01
#define	TEXT	02
#define	DATA	03
#define	BSS	04
#define	COMM	05

#define	RTEXT	02
#define	RDATA	04
#define	RBSS	06
#define	REXT	010

#define	RELFLG	01
#define	NSYM	1103
#define	NSYMPR	1000
#define	TABSZ	700

#define	SYMSZ	12		/* struct symbol */
#define	ARHSZ	26		/* the archive header */
#define	HDRSZ	16		/* the a.out header */

/* ---- the data segment (read from the code: tests/realbin/ldmap.txt) */

/* initialized data */
#define	CRT0_W8		0x0008		/* crt0's word 8: Iscadr's segment */
#define	PREMEOF		0x000a		/* char premeof[] "Premature EOF" */
#define	GOODNM		0x0018		/* char goodnm[] "__.SYMDEF" */
#define	libp		GU(0x0022)	/* struct liblist *libp = liblist */
#define	ofilename	GU(0x0024)	/* char *ofilename = "a.out" */
#define	outname		GU(0x0026)	/* the name the output is written
					   under: "..ldtmpXXXXXX", or -o's */
#define	delarg		GI(0x0028)	/* int delarg = 4 */
#define	TFNAME		0x002a		/* char tfname[] "/tmp/ldaXXXXXX" */
#define	br_rel		GU(0x003a)	/* bytrel()'s three statics */
#define	br_pend		GU(0x003c)
#define	br_carry	GU(0x003e)

/* BSS */
#define	TAB		0x03ba		/* struct tab tab[TABSZ], 12 bytes */
#define	tnum		GI(0x248a)
#define	vindex		GI(0x248c)
#define	VNODES		0x248e		/* struct overlay vnodes[16], 22 bytes */
#define	PAGE0		0x25ee		/* struct page page[2], 518 bytes */
#define	PAGE1		0x27f4
#define	FPAGE		0x29fa		/* struct { int nuser, bno; } fpage */
#define	TEXT_S		0x29fe		/* struct stream text */
#define	RELOC_S		0x2a08		/* struct stream reloc */
#define	ARCHDR		0x2a12		/* the archive header */
#define	FILHDR		0x2a2c		/* the a.out header */
#define	LIBLIST		0x2a3c		/* struct liblist liblist[256] */
#define	CURSYM		0x2e3c		/* struct symbol cursym */
#define	SYMTAB		0x2e48		/* struct symbol symtab[NSYM] */
#define	SYMHASH		0x61fc		/* struct symbol **symhash[NSYM] */
#define	lastsym		GU(0x6a9a)
#define	symindex	GI(0x6a9c)
#define	HSHTAB		0x6a9e		/* struct symbol *hshtab[NSYM+2] */
#define	HSHEND		0x7340		/* &hshtab[NSYM+2] */
#define	LOCAL		0x7340		/* struct local local[NSYMPR], 4 bytes */
#define	LOCEND		0x82e0		/* &local[NSYMPR] */
#define	p_etext		GU(0x82e0)
#define	p_edata		GU(0x82e2)
#define	p_end		GU(0x82e4)
#define	entrypt		GU(0x82e6)
#define	trace		GI(0x82e8)
#define	xflag		GI(0x82ea)
#define	Xflag		GI(0x82ec)
#define	Sflag		GI(0x82ee)
#define	sflag		GI(0x82f0)
#define	nflag		GI(0x82f2)	/* never set: -n sets iflag */
#define	Oflag		GI(0x82f4)
#define	iflag		GI(0x82f6)
#define	vflag		GI(0x82f8)
#define	ofilfnd		GI(0x82fa)
#define	infil		GI(0x82fc)
#define	filname		GU(0x82fe)
#define	tsize		GU(0x8300)
#define	dsize		GU(0x8302)
#define	bsize		GU(0x8304)
#define	ssize		GU(0x8306)
#define	ctrel		GU(0x8308)
#define	cdrel		GU(0x830a)
#define	cbrel		GU(0x830c)
#define	errlev		GI(0x830e)
#define	TOUTB		0x8310		/* struct buf, 518 bytes each */
#define	DOUTB		0x8516
#define	TROUTB		0x871c
#define	DROUTB		0x8922
#define	SOUTB		0x8b28
#define	nsym		GU(0x8d2e)
#define	torigin		GU(0x8d30)
#define	dorigin		GU(0x8d32)
#define	borigin		GU(0x8d34)
#define	LD_BSSEND	0x8d36		/* ld.o's BSS ends; libc.a's follows */

/* struct page: nuser, bno, nibuf, buff[256] */
#define	PG_NUSER	0
#define	PG_BNO		2
#define	PG_NIBUF	4
#define	PG_BUFF		6
/* struct stream: ptr, bno, nibuf, size, pno */
#define	ST_PTR		0
#define	ST_BNO		2
#define	ST_NIBUF	4
#define	ST_SIZE		6
#define	ST_PNO		8
/* struct buf: fildes, nleft, xnext, iobuf[256] */
#define	BF_FILDES	0
#define	BF_NLEFT	2
#define	BF_XNEXT	4
#define	BF_IOBUF	6
/* struct symbol: sname[8], stype (a word: type and spare), svalue */
#define	SY_TYPE		8
#define	SY_VALUE	10
/* struct overlay: argsav, symsav, libsav, vname, ctsav, cdsav, cbsav,
   offt, offd, offb, offs */
#define	OV_SIZE		22

/* the archive header: aname[14], atime, auid, agid, amode, asize */
#define	AR_ATIME	(ARCHDR + 14)
#define	AR_ASIZE	(ARCHDR + 22)
/* the a.out header's words */
#define	f_magic		GU(0x2a2c)
#define	f_tsize		GU(0x2a2e)
#define	f_dsize		GU(0x2a30)
#define	f_bsize		GU(0x2a32)
#define	f_ssize		GU(0x2a34)
#define	f_entry		GU(0x2a36)
#define	f_pad		GU(0x2a38)
#define	f_relflg	GU(0x2a3a)
/* cursym's type and value */
#define	cs_type		GU(0x2e44)
#define	cs_value	GU(0x2e46)
/* the streams' sizes and counts, as the original's code names them */
#define	text_ptr	GU(TEXT_S + ST_PTR)
#define	text_nibuf	GI(TEXT_S + ST_NIBUF)
#define	text_size	GI(TEXT_S + ST_SIZE)
#define	reloc_ptr	GU(RELOC_S + ST_PTR)
#define	reloc_nibuf	GI(RELOC_S + ST_NIBUF)
#define	reloc_size	GI(RELOC_S + ST_SIZE)

/* ---- the strings the original passes (their addresses in its data) */

#define	S_TRACE1	0x0054	/* "%s:\n" (main) */
#define	S_BADOUT	0x0059	/* "Bad output file" */
#define	S_BADUSE	0x0069	/* "Bad 'use' or 'entry'" */
#define	S_VMISS		0x007e	/* "-v: arg missing" */
#define	S_DMISS		0x008e	/* "-D: arg missing" */
#define	S_DSMALL	0x009e	/* "-D: too small" */
#define	S_RDFLAGS	0x00ac	/* " -r and -d flags not supported" */
#define	S_BADFLAG	0x00cb	/* "bad flag" */
#define	S_TRACE2	0x00d4	/* "%s:\n" (endload) */
#define	S_FASTLOAD	0x00d9	/* "fast load buffer too small" */
#define	S_OUTOFDATE	0x00f4	/* "out of date (warning)" */
#define	S_NORELOC	0x010a	/* "No relocation bits" */
#define	S_TEXTOVF	0x011d	/* "text overflow" */
#define	S_DATAOVF	0x012b	/* "data overflow" */
#define	S_BSSOVF1	0x0139	/* "bss overflow" (load1) */
#define	S_SYMOVF	0x0146	/* "symbol table overflow" */
#define	S_ETEXT		0x015c	/* "_etext" */
#define	S_EDATA		0x0163	/* "_edata" */
#define	S_END		0x016a	/* "_end" */
#define	S_BSSOVF2	0x016f	/* "bss overflow" (middle, a common) */
#define	S_UNDEFINED	0x017c	/* "Undefined:\n" */
#define	S_NAMENL	0x0188	/* "%.8s\n" */
#define	S_BSSOVF3	0x018e	/* "bss overflow" (middle, the total) */
#define	S_NAMEC1	0x019b	/* "%.8s: " (ldrsym) */
#define	S_MULTDEF1	0x01a2	/* "Multiply defined" (ldrsym) */
#define	S_ENTRY		0x01b3	/* "Entry point not in text" */
#define	S_CANTTEMP	0x01cb	/* "cannot create temp" */
#define	S_CANTOUT	0x01de	/* "cannot create output" */
#define	S_INTERNAL	0x01f3	/* "internal error: symbol not found" */
#define	S_LOCOVF	0x0214	/* "Local symbol overflow" */
#define	S_NAMEC2	0x022a	/* "%.8s: " (load2) */
#define	S_MULTDEF2	0x0231	/* "Multiply defined" (load2) */
#define	S_RELERR	0x0242	/* "Relocation error" */
#define	S_NOPAGES	0x0253	/* "No pages" */
#define	S_LA		0x025c	/* "-la" */
#define	S_LIBNAME	0x0260	/* "/usr/lib/libxxxxxxxxxxxxxxx" */
#define	S_CANTOPEN	0x027c	/* "cannot open" */
#define	S_SYMTOVF	0x0288	/* "Symbol table overflow" */
#define	S_LDCOLON	0x029e	/* "ld:" */
#define	S_PCTS		0x02a2	/* "%s" */
#define	S_ANAME		0x02a5	/* "(%.14s)" */
#define	S_COLON		0x02ad	/* ": " */
#define	S_PCTSNL	0x02b0	/* "%s\n" */
#define	S_LOCBOTCH	0x02b4	/* "Local symbol botch" */
#define	S_BADFMT	0x02c7	/* "Bad format" */
#define	S_OUTERR	0x02d2	/* "output error" */
#define	S_NULL		0x02e0	/* libc.a's "(null)" */
#define	LD_DATAEND	0x02e8	/* libc.a's FILE structures begin here */

/* ---- the machine (ldlib.c) ----------------------------------------- */

void	ld_init(int argc, char **argv, uint16_t *mutos_argv);
void	ld_signals(void);
NORETURN void	ld_exit(int16_t status);
void	ld_printf(uint16_t fmt, const uint16_t *args, int nargs);
int16_t	ld_atoi(uint16_t s);
uint16_t ld_mktemp(uint16_t as);

int16_t	sys_open(uint16_t name, int16_t mode);
int16_t	sys_creat(uint16_t name, int16_t mode);
int16_t	sys_close(int16_t fd);
int16_t	sys_read(int16_t fd, uint16_t buf, uint16_t n);
int16_t	sys_write(int16_t fd, uint16_t buf, uint16_t n);
int32_t	sys_lseek(int16_t fd, int32_t off, int16_t whence);
int16_t	sys_unlink(uint16_t name);
int16_t	sys_link(uint16_t name1, uint16_t name2);
int16_t	sys_chmod(uint16_t name, int16_t mode);
int16_t	sys_umask(int16_t mask);
int16_t	sys_fstat_mtime(int16_t fd, int32_t *mtime);
int16_t	cstrncmp(uint16_t a, uint16_t b, int16_t n);

/* ---- the extension: -r and -d (outside the image) -------------------- */

extern	int	x_rflag;	/* v7's rflag: preserve relocation bits */
extern	int	x_arflag;	/* v7's arflag: -r given */
extern	int	x_dflag;	/* v7's dflag: define common even with -r */

/* ---- the original's functions -------------------------------------- */

NORETURN void	delexit(void);
int16_t	ldmain(int16_t argc, uint16_t argv);

#endif /* MUTOS_LD_LD_H */
