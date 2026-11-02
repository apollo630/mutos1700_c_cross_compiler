/*
 * nm.c - mutos_nm, the MUTOS 1700 name list: a port of the original nm.
 *
 * v7's nm.c (v7/nm/nm.c) as the original binary has it - main() at text
 * 00b6, compare() at 058e and nextel() at 05f6, with libc.a's qsort() at
 * 1660 (tests/realbin/nmmap.txt). Read instruction by instruction beside
 * v7's source, the three functions do what v7's do; what differs is the
 * representation (the headers as bytes, the words 16 bits wide, the longs as
 * the 8086 compiler keeps them), FORMAT, which is "%04x" where v7's is
 * "%06o", and the libc.a underneath.
 *
 * What nm does:
 *
 *	nm [-gnopru] [file ...]
 *
 * For every file named (a.out if there is none) it prints the symbol table
 * of the file, or of every member of an archive that is an a.out file:
 *
 *	0010 T _main
 *	     U _printf
 *
 * the value (four hex digits, blank for an undefined symbol), the type as a
 * letter (a absolute, b bss, c common, d data, f file name, r register, t
 * text, u undefined; upper case for an external symbol) and the name, at most
 * eight characters. The options are read from the first argument only: -g
 * external symbols only, -n sort by value (and then by name), -o prepend the
 * file's (and the archive member's) name to every line, -p do not sort,
 * -r sort in reverse order, -u undefined symbols only (the name only).
 * Messages that are not part of the list - "nm: x-- bad format", "nm: x--
 * no name list", "nm: cannot open x" - go to the standard error.
 *
 * What the port keeps of the original, where it is odd:
 *  - arp and exp lie one behind the other in the original's BSS, and the
 *    names of archive members that nm prints - the "%s" of arp.ar_name -
 *    run on through the date, the owner and the size of the header (and, if
 *    there is no 0 among those, the a.out header behind it) when the name
 *    has 14 characters: the port keeps the two in one array and prints what
 *    the original prints;
 *  - arch_flg is never reset: the files that follow an archive on the
 *    command line are read as archives, too - an a.out file is "an archive
 *    of one member", read from the offset 2 on, and its name is not printed;
 *  - arp, exp and the symbol sym are not cleared between the members, nor
 *    between the files: a read that comes back short leaves the rest of the
 *    old bytes (an empty file keeps the magic number of the one before; the
 *    first file finds zeros), and so does sym;
 *  - n_type is a word, the type of the symbol and the spare byte behind it:
 *    -g tests the bit 040 of the whole word, and the letter is the type
 *    masked with 037, 24 being a register and 037 a file name, every other
 *    number one that is not named an absolute symbol (a);
 *  - the names are compared as signed characters (a name with a byte of 0200
 *    or more sorts before the others), the values as unsigned numbers;
 *  - the sort is libc.a's qsort(), V7's: when elements compare equal the
 *    order they come out in is that of its swaps - the port has the same
 *    algorithm, in the 16-bit arithmetic of the original (pointers 16 bits
 *    wide, lp-a >= l-hp a signed comparison of differences);
 *  - an undefined symbol is printed as six blanks, where the value of any
 *    other symbol is four digits and a blank (the columns of the list do not
 *    line up, the type letters of the undefined symbols come two columns
 *    to the right);
 *  - a file that is not an archive, and not an a.out file, is not closed
 *    ("bad format"): v7's stdio has twenty file slots, three of them taken,
 *    so the eighteenth file of that kind is read into a "slot" behind the
 *    table (refused, see nm_fopen());
 *  - the exit status is 0, whatever happened to the files (1 for an invalid
 *    option, 2 for no memory).
 *
 * What the port refuses (mu_trap(): exit status 125, the last line of the
 * messages ending in "refused"), where the original depends on state that
 * the port does not model:
 *  - a file name that is a directory (the original reads its entries);
 *  - the eighteenth file that is open at the same time (nm_fopen());
 *  - a seek to a negative offset (a negative member size): the original's
 *    lseek() fails and its stdio goes on from where the file was;
 *  - an ar_name with no 0 in the 42 bytes of arp and exp;
 *  - more than NM_MAXSYM symbols in one a.out file: the original's heap and
 *    its stack share the 64K of the process, and where they meet is not
 *    measurable here.
 */
#define _POSIX_C_SOURCE 200809L	/* fileno() */

#define MU_TOOL "nm"
#include "../h/mutos_stdio.h"

#include <stdint.h>
#include <sys/stat.h>

/* the layout of the original's BSS (text offsets are in the map) */
#define ARHDR	26		/* sizeof(struct ar_hdr): name[14], long date, ... */
#define EXEC	16		/* sizeof(struct exec) */
#define NLIST	12		/* sizeof(struct nlist) */
#define ARMAG	0177545
#define A_MAGIC1	0407
#define A_MAGIC2	0410
#define A_MAGIC3	0411
#define A_MAGIC4	0405
#define N_EXT	040
#define N_TYPE	037
#define N_UNDF	0
#define N_ABS	01
#define N_TEXT	02
#define N_DATA	03
#define N_BSS	04
#define N_REG	024
#define N_FN	037
#define NFILE_FREE 17		/* v7's stdio: 20 slots, stdin, stdout, stderr taken */
#ifndef NM_MAXSYM
#define NM_MAXSYM 2000		/* see the head of the file */
#endif

/* arp and exp, one behind the other as in the original's BSS (1c28, 1c42) */
static uint8_t	hdrs[ARHDR + EXEC];
#define ARP	(hdrs)
#define EXP	(hdrs + ARHDR)
/* struct ar_hdr: */
#define AR_SIZE	22
/* struct exec: */
#define A_MAGIC	0
#define A_TEXT	2
#define A_DATA	4
#define A_SYMS	8
#define A_FLAG	14
/* struct nlist: */
#define N_NAME	0
#define N_TYPEW	8
#define N_VALUE	10

static int	numsort_flg;		/* 1c1c */
static int	undef_flg;		/* 1c1e */
static int	revsort_flg = 1;	/* 1ac2 (data) */
static int	globl_flg;		/* 1c20 */
static int	nosort_flg;		/* 1c22 */
static int	arch_flg;		/* 1c24 */
static int	prep_flg;		/* 1c26 */
static FILE	*fi;			/* 1c52 */
static int32_t	off;			/* 1c54 */
static int	leaked;			/* files never closed (stdio slots held) */

/* ---- the codecs of the 8086's words and longs ---------------------------- */

static unsigned
getw16(const uint8_t *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

static void
putw16(uint8_t *p, unsigned v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

/* a long as v7 keeps it: the high word first, each word little-endian */
static int32_t
getlong(const uint8_t *p)
{
	return (int32_t)(((uint32_t)getw16(p) << 16) | getw16(p + 2));
}

static void
putlong(uint8_t *p, int32_t v)
{
	uint32_t u = (uint32_t)v;

	p[0] = (uint8_t)(u >> 16);
	p[1] = (uint8_t)(u >> 24);
	p[2] = (uint8_t)u;
	p[3] = (uint8_t)(u >> 8);
}

/* ---- libc.a's stdio, as far as nm sees it ----------------------------------- */

/*
 * fopen(): v7's stdio has twenty slots, three of them the standard streams;
 * a file left open (one that is not in the proper format) holds its slot for
 * good. fopen() finds a free slot by running on from _iob until it meets a
 * _flag of 0, and gives up only when it has passed _lastbuf: with the
 * twenty slots taken it reads the _flag of a twenty-first, behind the table,
 * and where that is 0 - in nm's BSS it is: the table is followed by the
 * stdin buffer - the "slot" is that memory. The eighteenth file left open
 * is read into a FILE that lies on libc.a's stdin buffer, and nm's malloc()
 * fails on the next symbol ("nm: out of memory on x") while a file that is
 * not in the proper format goes on with "bad format" for the next
 * twenty-odd files: refused. (ranlib's BSS has a non-zero word there, and
 * its eighteenth fopen() returns 0.) A directory opens in the original and
 * reads its entries: refused.
 */
static FILE *
nm_fopen(const char *name)
{
	FILE *f;
	struct stat st;

	f = fopen(name, "r");
	if (f == NULL)
		return NULL;
	if (fstat(fileno(f), &st) == 0 && S_ISDIR(st.st_mode))
		mu_trap("an fopen()ed directory - the original reads its entries");
	if (leaked >= NFILE_FREE)
		mu_trap("an eighteenth file left open - the original's FILE lies "
		    "behind its table of slots");
	return f;
}

/* fseek(): the original's lseek() to a negative offset fails and leaves the file where it was */
static void
nm_fseek(FILE *f, int32_t o, int whence)
{
	if (whence == SEEK_SET && o < 0)
		mu_trap("an fseek() to a negative offset");
	if (fseek(f, (long)o, whence) != 0 && whence == SEEK_CUR)
		mu_trap("an fseek() past what the host can seek to");
}

/* ---- nextel (text 05f6) ------------------------------------------------------- */

static int
nextel(FILE *af)
{
	size_t r;
	uint32_t sz;

	nm_fseek(af, off, SEEK_SET);
	r = fread(ARP, 1, ARHDR, af);		/* read archive header */
	if ((int)r <= 0)
		return 0;
	sz = (uint32_t)getlong(ARP + AR_SIZE);
	if (sz & 1)
		++sz;
	putlong(ARP + AR_SIZE, (int32_t)sz);
	off = (int32_t)((uint32_t)ftell(af) + sz);	/* offset to next element */
	return 1;
}

/* ---- the sort: compare (text 058e) and libc.a's qsort (text 1660) ----------------- */

/* the array symp[] of main(), and the 16-bit address the original's pointers into it start at */
static uint8_t	*qarr;
#define QBASE	0x4000u

static uint8_t *
qat(uint16_t p)
{
	return qarr + (uint16_t)(p - QBASE);
}

static int
compare(const uint8_t *p1, const uint8_t *p2)
{
	int i;

	if (numsort_flg) {
		if (getw16(p1 + N_VALUE) > getw16(p2 + N_VALUE))
			return revsort_flg;
		if (getw16(p1 + N_VALUE) < getw16(p2 + N_VALUE))
			return -revsort_flg;
	}
	for (i = 0; i < 8; i++)
		if (p1[N_NAME + i] != p2[N_NAME + i]) {
			if ((int8_t)p1[N_NAME + i] > (int8_t)p2[N_NAME + i])
				return revsort_flg;
			else
				return -revsort_flg;
		}
	return 0;
}

/* qsexc (text 17ca): exchange two elements */
static void
qsexc(uint16_t i, uint16_t j)
{
	uint8_t *ri = qat(i), *rj = qat(j), c;
	int n = NLIST;

	do {
		c = *ri;
		*ri++ = *rj;
		*rj++ = c;
	} while (--n);
}

/* qstexc (text 17f8): rotate three elements - i gets k's, k gets j's, j gets i's */
static void
qstexc(uint16_t i, uint16_t j, uint16_t k)
{
	uint8_t *ri = qat(i), *rj = qat(j), *rk = qat(k), c;
	int n = NLIST;

	do {
		c = *ri;
		*ri++ = *rk;
		*rk++ = *rj;
		*rj++ = c;
	} while (--n);
}

/* qs1 (text 168a): the pointers are the original's, 16 bits wide */
static void
qs1(uint16_t a, uint16_t l)
{
	uint16_t i, j, lp, hp, n;
	const uint16_t es = NLIST;
	int c;

start:
	n = (uint16_t)(l - a);
	if (n <= es)
		return;
	n = (uint16_t)(es * (n / (uint16_t)(2 * es)));
	hp = lp = (uint16_t)(a + n);
	i = a;
	j = (uint16_t)(l - es);
	for (;;) {
		if (i < lp) {
			if ((c = compare(qat(i), qat(lp))) == 0) {
				lp = (uint16_t)(lp - es);
				qsexc(i, lp);
				continue;
			}
			if (c < 0) {
				i = (uint16_t)(i + es);
				continue;
			}
		}
loop:
		if (j > hp) {
			if ((c = compare(qat(hp), qat(j))) == 0) {
				hp = (uint16_t)(hp + es);
				qsexc(hp, j);
				goto loop;
			}
			if (c > 0) {
				if (i == lp) {
					hp = (uint16_t)(hp + es);
					qstexc(i, hp, j);
					lp = (uint16_t)(lp + es);
					i = lp;
					goto loop;
				}
				qsexc(i, j);
				j = (uint16_t)(j - es);
				i = (uint16_t)(i + es);
				continue;
			}
			j = (uint16_t)(j - es);
			goto loop;
		}
		if (i == lp) {
			/* lp-a >= l-hp: the differences are signed 16-bit words (jl) */
			if ((int16_t)(uint16_t)(lp - a) >= (int16_t)(uint16_t)(l - hp)) {
				qs1((uint16_t)(hp + es), l);
				l = lp;
			} else {
				qs1(a, lp);
				a = (uint16_t)(hp + es);
			}
			goto start;
		}
		lp = (uint16_t)(lp - es);
		qstexc(j, lp, i);
		hp = (uint16_t)(hp - es);
		j = hp;
	}
}

static void
nm_qsort(uint8_t *a, int n)
{
	qarr = a;
	qs1(QBASE, (uint16_t)(QBASE + (uint16_t)(n * NLIST)));
}

/* ---- main (text 00b6) ------------------------------------------------------------- */

/* SELECT: arch_flg ? arp.ar_name : *argv - the "%s" of arp.ar_name runs on through arp and exp */
static const char *
select_name(const char *arg)
{
	size_t len = 0;

	if (!arch_flg)
		return arg;
	while (len < ARHDR + EXEC && ARP[len] != 0)
		len++;
	if (len == ARHDR + EXEC)
		mu_trap("an ar_name with no 0 behind it in arp and exp");
	return (const char *)ARP;
}

static int
bad_magic(void)
{
	unsigned m = getw16(EXP + A_MAGIC);

	return m != A_MAGIC1 && m != A_MAGIC2 && m != A_MAGIC3 && m != A_MAGIC4;
}

int
main(int argc, char **argv)
{
	static uint8_t sym[NLIST];	/* struct nlist sym: a local of main(), zero at the start */
	static char aout[] = "a.out";
	char *dflt[1];
	char **names = argv + 1;
	int narg;

	if (--argc > 0 && argv[1][0] == '-' && argv[1][1] != 0) {
		const char *p = argv[1];

		names = argv + 2;
		while (*++p) switch (*p) {
		case 'n':		/* sort numerically */
			numsort_flg++;
			continue;

		case 'g':		/* globl symbols only */
			globl_flg++;
			continue;

		case 'u':		/* undefined symbols only */
			undef_flg++;
			continue;

		case 'r':		/* sort in reverse order */
			revsort_flg = -1;
			continue;

		case 'p':		/* don't sort -- symbol table order */
			nosort_flg++;
			continue;

		case 'o':		/* prepend a name to each line */
			prep_flg++;
			continue;

		default:		/* oops */
			mu_eprintf("nm: invalid argument -%c\n", *p);
			mu_exit(1);
		}
		argc--;
	}
	if (argc == 0) {
		argc = 1;
		dflt[0] = aout;
		names = dflt;
	}
	narg = argc;
	while (argc--) {
		const char *name = *names++;

		fi = nm_fopen(name);
		if (fi == NULL) {
			mu_eprintf("nm: cannot open %s\n", name);
			continue;
		}
		off = 2;			/* sizeof(exp.a_magic) */
		(void)!fread(EXP, 1, 2, fi);	/* get magic no. */
		if (getw16(EXP + A_MAGIC) == (unsigned)(ARMAG & 0xffff))
			arch_flg++;
		else if (bad_magic()) {
			mu_eprintf("nm: %s-- bad format\n", name);
			leaked++;		/* fi is not closed */
			continue;
		}
		nm_fseek(fi, 0, SEEK_SET);
		if (arch_flg) {
			nextel(fi);
			if (narg > 1)
				mu_printf("\n%s:\n", name);
		}
		do {
			int32_t o;
			int i, n, c, k;
			uint8_t *symp = NULL;

			(void)!fread(EXP, 1, EXEC, fi);
			if (bad_magic())	/* archive element not in proper format - skip it */
				continue;
			o = (int32_t)((uint32_t)getw16(EXP + A_TEXT) + getw16(EXP + A_DATA));
			if ((getw16(EXP + A_FLAG) & 01) == 0)
				o = (int32_t)((uint32_t)o * 2);
			nm_fseek(fi, o, SEEK_CUR);
			n = (int)(getw16(EXP + A_SYMS) / NLIST);
			if (n == 0) {
				mu_eprintf("nm: %s-- no name list\n", select_name(name));
				continue;
			}
			i = 0;
			while (--n >= 0) {
				uint8_t b[NLIST];
				size_t r = fread(b, 1, NLIST, fi);
				unsigned t;

				memcpy(sym, b, r);	/* a short read leaves the old bytes */
				t = getw16(sym + N_TYPEW);
				if (globl_flg && (t & N_EXT) == 0)
					continue;
				switch (t & N_TYPE) {

				case N_UNDF:
					c = 'u';
					if (getw16(sym + N_VALUE))
						c = 'c';
					break;

				default:
				case N_ABS:
					c = 'a';
					break;

				case N_TEXT:
					c = 't';
					break;

				case N_DATA:
					c = 'd';
					break;

				case N_BSS:
					c = 'b';
					break;

				case N_FN:
					c = 'f';
					break;

				case N_REG:
					c = 'r';
					break;
				}
				if (undef_flg && c != 'u')
					continue;
				if (t & N_EXT)
					c -= 0x20;	/* toupper(c) */
				putw16(sym + N_TYPEW, (unsigned)c);
				if (i >= NM_MAXSYM)
					mu_trap("more symbols in one file than the port knows the "
					    "original's heap to hold");
				symp = realloc(symp, (size_t)(i + 1) * NLIST);
				if (symp == NULL)
					mu_trap("the host has no memory");
				memcpy(symp + (size_t)i * NLIST, sym, NLIST);
				i++;
			}
			if (nosort_flg == 0 && i > 0)
				nm_qsort(symp, i);
			if ((arch_flg || narg > 1) && prep_flg == 0)
				mu_printf("\n%s:\n", select_name(name));
			for (k = 0; k < i; k++) {
				const uint8_t *e = symp + (size_t)k * NLIST;

				if (prep_flg) {
					if (arch_flg)
						mu_printf("%s:", name);
					mu_printf("%s:", select_name(name));
				}
				c = (int)getw16(e + N_TYPEW);
				if (!undef_flg) {
					if (c == 'u' || c == 'U')
						mu_printf("      ");
					else
						mu_printf("%04x", getw16(e + N_VALUE));
					mu_printf(" %c ", c);
				}
				mu_printf("%.8s\n", (const char *)e);
			}
			free(symp);
		} while (arch_flg && nextel(fi));
		fclose(fi);
	}
	mu_exit(0);
}
