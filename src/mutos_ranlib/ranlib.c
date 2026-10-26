/*
 * ranlib.c - mutos_ranlib, the MUTOS 1700 archive indexer: a port of the
 * original ranlib.
 *
 * v7's ranlib.c (v7/ranlib/ranlib.c) as the original binary has it - the
 * functions in v7's order, each with the text offset of the original's
 * counterpart in its comment (tests/realbin/ranlibmap.txt). Read
 * instruction by instruction beside v7's source, none of the six
 * functions of ranlib.o differs from it in what it does; what differs is
 * the representation (the a.out header and the archive header as bytes,
 * the longs as the 8086 compiler keeps them) and the libc.a underneath.
 *
 * What ranlib does: for every archive named on the command line it reads
 * the a.out members' symbol tables, writes "__.SYMDEF" - an entry per
 * external symbol that a member defines: its name (8 bytes) and the
 * offset of the member's header, long, high word first - and has ar put
 * it in front of the archive ("ar rlb first archive __.SYMDEF", or "ar rl
 * archive __.SYMDEF" if the archive has one already), then sets the date
 * of the archive's first member - __.SYMDEF - five seconds into the
 * future, so that ld finds the table newer than the archive.
 *
 * What the port keeps of the original, where it is odd:
 *  - arp and exp lie one behind the other in the original's BSS, and
 *    "nm: %s-- no name list" prints arp.ar_name - 14 bytes with no
 *    terminating 0 if the name is 14 characters long - on through the
 *    date, the owner and the size of the header (and, if there is no 0
 *    among those, the a.out header behind it): the port keeps the two in
 *    one array and prints what the original prints;
 *  - arp, exp and the symbol sym are not cleared between the members, nor
 *    between the archives: a read that comes back short leaves the
 *    rest of the old bytes (an empty argument file keeps the magic number
 *    of the one before), and so does sym;
 *  - a file that is not an archive is not closed ("not archive"): v7's
 *    stdio has twenty file slots, three of them taken, so the eighteenth
 *    argument that is not an archive cannot be opened ("nm: cannot open");
 *  - "nm: " is what the messages of ranlib begin with (v7's ranlib is
 *    nm's code), the symbol is taken if its type has the external bit and
 *    is not 0 (undefined), the table is 700 entries ("symbol table
 *    overflow", exit status 1), buf for the command line is 256 bytes.
 *
 * What the port refuses (ranlib_trap(): exit status 125, the last line of
 * the messages ending in "refused"), where the original depends on state
 * that the port does not model:
 *  - an argument that is a directory (the original reads its entries);
 *  - a seek to a negative offset (a negative member size): the original's
 *    lseek() fails and its stdio goes on from where the file was;
 *  - a symbol table that ends in the middle of an entry before any entry
 *    was read in full: the original uses what its stack held;
 *  - an ar_name with no 0 in the 42 bytes of arp and exp;
 *  - a command line of 256 characters or more: the original's sprintf()
 *    runs past buf[256] over the saved registers and the return address;
 *  - a name that the shell would take apart or expand (the original runs
 *    "ar ..." through /bin/sh -c), or that is empty;
 *  - no executable $MUTOS_ROOT/bin/ar, or an ar that refused.
 *
 * ar is run as $MUTOS_ROOT/bin/ar - the original finds "ar" through the
 * shell's PATH in /bin - in the current directory, with the words of the
 * command line as arguments, and not through a shell: for the names the
 * port accepts the shell would do the same.
 */
#define _POSIX_C_SOURCE 200809L	/* fileno(), fork(), signal() */

#include "../h/mutos_root.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#if defined(__GNUC__)
#define NORETURN __attribute__((noreturn))
#else
#define NORETURN
#endif

/* the layout of the original's BSS (text offsets are in the map) */
#define ARHDR	26		/* sizeof(struct ar_hdr): name[14], long date, ... */
#define EXEC	16		/* sizeof(struct exec) */
#define ARMAG	0177545
#define A_MAGIC1	0407
#define A_MAGIC2	0410
#define A_MAGIC3	0411
#define A_MAGIC4	0405
#define N_EXT	040
#define N_TYPE	037
#define NLIST	12		/* sizeof(struct nlist) */
#define TABSZ	700
#define TABENT	12		/* sizeof(struct tab): char cname[8]; long cloc */
#define NFILE_FREE 17		/* v7's stdio: 20 slots, stdin, stdout, stderr taken */

/* arp and exp, one behind the other as in the original's BSS */
static uint8_t	hdrs[ARHDR + EXEC];
#define ARP	(hdrs)
#define EXP	(hdrs + ARHDR)
/* struct ar_hdr: */
#define AR_NAME	0
#define AR_DATE	14
#define AR_SIZE	22
/* struct exec: */
#define A_MAGIC	0
#define A_TEXT	2
#define A_DATA	4
#define A_SYMS	8
#define A_FLAG	14

static FILE	*fi, *fo;
static int32_t	off, oldoff;
static uint8_t	tab[TABSZ][TABENT];
static int	tnum;
static int	isnew;			/* the original's "new" */
static const char tempnm[] = "__.SYMDEF";
static char	firstname[17];
static int32_t	offdelta;
static int	leaked;			/* files never closed (stdio slots held) */
static int	tempmade;		/* __.SYMDEF written, not yet unlinked */

static int	nextel(FILE *af);
static void	stash(const uint8_t *s);
static int	fixsize(void);
static void	fixdate(const char *s);

/* ---- the codecs of the 8086's words and longs ---------------------------- */

static unsigned
getw16(const uint8_t *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
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

/* ---- the refusals --------------------------------------------------------- */

static void
rmtemp(void)
{
	if (tempmade) {
		unlink(tempnm);
		tempmade = 0;
	}
}

static NORETURN void
ranlib_trap(const char *what)
{
	char m[512];
	int n;
	ssize_t r;

	n = snprintf(m, sizeof(m), "ranlib: %s - the original depends on state "
	    "the port does not model here; refused\n", what);
	if (n > 0) {
		rmtemp();
		r = write(2, m, (size_t)n < sizeof(m) ? (size_t)n : sizeof(m) - 1);
		(void)r;
	}
	exit(125);	/* not a status ranlib itself ends with */
}

/* ---- libc.a's stdio, as far as ranlib sees it ----------------------------- */

/*
 * fopen(): v7's stdio has twenty slots, three of them the standard streams;
 * a file left open (a name that is not an archive) holds its slot for good,
 * and past seventeen of them fopen() returns 0. A directory opens in the
 * original and reads its entries: refused.
 */
static FILE *
rl_fopen(const char *name, const char *mode)
{
	FILE *f;
	struct stat st;

	if (leaked >= NFILE_FREE)
		return NULL;
	f = fopen(name, mode);
	if (f == NULL)
		return NULL;
	if (mode[0] == 'r' && fstat(fileno(f), &st) == 0 && S_ISDIR(st.st_mode))
		ranlib_trap("an fopen()ed directory - the original reads its entries");
	return f;
}

/* fseek(): the original's lseek() to a negative offset fails and leaves the file where it was */
static void
rl_fseek(FILE *f, int32_t o, int whence)
{
	if (whence == SEEK_SET && o < 0)
		ranlib_trap("an fseek() to a negative offset");
	if (fseek(f, (long)o, whence) != 0 && whence == SEEK_CUR)
		ranlib_trap("an fseek() past what the host can seek to");
}

/* ---- main (text 00b6) ------------------------------------------------------- */

int
main(int argc, char **argv)
{
	uint8_t sym[NLIST];		/* the original's: a local of main() */
	int symvalid = 0;

	memset(sym, 0, sizeof(sym));
	--argc;
	while (argc--) {
		fi = rl_fopen(*++argv, "r");
		if (fi == NULL) {
			fprintf(stderr, "nm: cannot open %s\n", *argv);
			continue;
		}
		off = 2;			/* sizeof(exp.a_magic) */
		(void)!fread(EXP, 1, 2, fi);	/* get magic no. */
		if (getw16(EXP + A_MAGIC) != (unsigned)(ARMAG & 0xffff)) {
			fprintf(stderr, "not archive: %s\n", *argv);
			leaked++;		/* fi is not closed */
			continue;
		}
		rl_fseek(fi, 0, SEEK_SET);
		isnew = tnum = 0;
		if (nextel(fi) == 0) {
			fclose(fi);
			continue;
		}
		do {
			int32_t o;
			int n;
			unsigned mg;

			(void)!fread(EXP, 1, EXEC, fi);
			mg = getw16(EXP + A_MAGIC);
			if (mg != A_MAGIC1 && mg != A_MAGIC2 && mg != A_MAGIC3 &&
			    mg != A_MAGIC4)
				continue;	/* not in the proper format: skip it */
			o = (int32_t)((uint32_t)getw16(EXP + A_TEXT) +
			    getw16(EXP + A_DATA));
			if ((getw16(EXP + A_FLAG) & 01) == 0)
				o = (int32_t)((uint32_t)o * 2);
			rl_fseek(fi, o, SEEK_CUR);
			n = (int)(getw16(EXP + A_SYMS) / NLIST);
			if (n == 0) {
				/* %s of arp.ar_name: on through the bytes behind it */
				size_t len = 0;

				while (len < ARHDR + EXEC && ARP[len] != 0)
					len++;
				if (len == ARHDR + EXEC)
					ranlib_trap("an ar_name with no 0 behind it in arp and exp");
				fprintf(stderr, "nm: %s-- no name list\n", (char *)ARP);
				continue;
			}
			while (--n >= 0) {
				uint8_t b[NLIST];
				size_t r = fread(b, 1, NLIST, fi);

				if (r < NLIST && !symvalid)
					ranlib_trap("a symbol table that ends in the middle of "
					    "its first entry - the original uses its stack's old bytes");
				memcpy(sym, b, r);
				if (r == NLIST)
					symvalid = 1;
				/* the type is a word: the spare byte behind n_type counts */
				if ((getw16(sym + 8) & N_EXT) == 0)
					continue;
				switch (getw16(sym + 8) & N_TYPE) {

				case 0:		/* N_UNDF */
					continue;

				default:
					stash(sym);
					continue;
				}
			}
		} while (nextel(fi));
		isnew = fixsize();
		fclose(fi);
		fo = rl_fopen(tempnm, "w");
		if (fo == NULL) {
			fprintf(stderr, "can't create temporary\n");
			exit(1);
		}
		tempmade = 1;
		(void)!fwrite(tab, (size_t)tnum, TABENT, fo);
		fclose(fo);
		{
			char buf[256];
			int len;
			char *words[8];
			int nw = 0;
			char *fname = *argv;
			pid_t pid, w;
			int status = 0;
			void (*oint)(int), (*oquit)(int);
			char ar[4096];

			if (isnew)
				len = snprintf(buf, sizeof(buf), "ar rlb %s %s %s\n",
				    firstname, *argv, tempnm);
			else
				len = snprintf(buf, sizeof(buf), "ar rl %s %s\n",
				    *argv, tempnm);
			if (len >= (int)sizeof(buf))
				ranlib_trap("a command line of 256 characters or more - "
				    "the original's sprintf() runs past buf[256]");
			/* the names the shell would take apart or expand, or lose */
			{
				const char *chk[2] = { isnew ? firstname : NULL, fname };
				int c;

				for (c = 0; c < 2; c++) {
					const char *s = chk[c];

					if (s == NULL)
						continue;
					if (*s == 0)
						ranlib_trap("an empty name in the command line the "
						    "original hands to the shell");
					for (; *s; s++)
						if (!((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') ||
						    (*s >= '0' && *s <= '9') || strchr("._/+,:@%=-", *s)))
							ranlib_trap("a name that the shell the original "
							    "hands the command line to would take apart "
							    "or expand");
				}
			}
			if (mutos_path(ar, sizeof(ar), "/bin/ar") == NULL)
				ranlib_trap("a MUTOS root path that is too long");
			words[nw++] = ar;
			words[nw++] = isnew ? "rlb" : "rl";
			if (isnew)
				words[nw++] = firstname;
			words[nw++] = fname;
			words[nw++] = (char *)tempnm;
			words[nw] = NULL;
			if (access(ar, X_OK) != 0)
				ranlib_trap("no executable bin/ar in the MUTOS root");
			fflush(NULL);
			/* system(): fork(); the child execs; the parent ignores
			 * SIGINT and SIGQUIT and waits */
			pid = fork();
			if (pid == 0) {
				execv(ar, words);
				_exit(0x7f);
			}
			oint = signal(SIGINT, SIG_IGN);
			oquit = signal(SIGQUIT, SIG_IGN);
			while ((w = waitpid(pid, &status, 0)) < 0 && errno == EINTR)
				;
			if (w < 0)
				status = -1;
			signal(SIGINT, oint);
			signal(SIGQUIT, oquit);
			if (status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 125)
				ranlib_trap("an ar that refused");
			if (status != 0)
				fprintf(stderr, "can't execute %s\n", buf);
			else
				fixdate(*argv);
		}
		unlink(tempnm);
		tempmade = 0;
	}
	exit(0);
}

/* ---- nextel (text 0346) ----------------------------------------------------- */

static int
nextel(FILE *af)
{
	size_t r;
	uint32_t sz;

	oldoff = off;
	rl_fseek(af, off, SEEK_SET);
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

/* ---- stash (text 03d8) ------------------------------------------------------- */

static void
stash(const uint8_t *s)
{
	int i;

	if (tnum >= TABSZ) {
		fprintf(stderr, "symbol table overflow\n");
		exit(1);
	}
	for (i = 0; i < 8; i++)
		tab[tnum][i] = s[i];
	putlong(&tab[tnum][8], oldoff);
	tnum++;
}

/* ---- fixsize (text 0448) ------------------------------------------------------ */

static int
fixsize(void)
{
	int i;

	offdelta = tnum * TABENT + ARHDR;
	off = 2;			/* sizeof(MAGIC) */
	nextel(fi);
	if (strncmp((char *)ARP, tempnm, 14) == 0) {
		isnew = 0;
		offdelta = (int32_t)((uint32_t)offdelta -
		    ((uint32_t)ARHDR + (uint32_t)getlong(ARP + AR_SIZE)));
	} else {
		isnew = 1;
		/* strncpy(firstname, arp.ar_name, 14): up to the first 0, padded with 0 */
		for (i = 0; i < 14; i++)
			firstname[i] = (ARP[i] == 0 || (i > 0 && firstname[i - 1] == 0))
			    ? 0 : (char)ARP[i];
	}
	for (i = 0; i < tnum; i++)
		putlong(&tab[i][8], (int32_t)((uint32_t)getlong(&tab[i][8]) +
		    (uint32_t)offdelta));
	return isnew;
}

/* ---- fixdate (text 04fc): patch time ------------------------------------------- */

static void
fixdate(const char *s)
{
	int32_t timex;
	uint8_t b[4];
	int fd;

	fd = open(s, 1);
	if (fd < 0) {
		fprintf(stderr, "can't reopen %s\n", s);
		return;
	}
	timex = (int32_t)((uint32_t)time(NULL) + 5);	/* should be enough time */
	lseek(fd, 2 + AR_DATE, SEEK_SET);	/* sizeof(exp.a_magic) + offset of ar_date */
	putlong(b, timex);
	{
		ssize_t r = write(fd, b, 4);

		(void)r;
	}
	close(fd);
}
