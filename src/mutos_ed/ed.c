/*
 * ed.c - mutos_ed, the MUTOS 1700 text editor: a port of the original ed.
 *
 * The original binary (mutos/mutos_bin/ed.golden.base64.txt, 0407, text
 * 11522, data 234, bss 4886) is v7's ed.c (v7/ed/ed.c) with two changes,
 * compiled with the MUTOS cc -O and linked with libc.a: v7's source with the
 * two changes, compiled by this project's own cc -O -s, is the binary byte
 * for byte (tests/realbin/edmap.txt). The two changes:
 *  - 'j' with one address joins that line and the next (",.+1"), with none
 *    ".,.+1" - v7 joins nothing then;
 *  - 'm' to the line before the first line moved, or to the first line
 *    itself, does nothing (v7 moves the lines onto themselves).
 * The offset of each function in the original's text is in its comment.
 *
 * What is the 8086's here, and kept:
 *  - int is 16 bits: the numbers of an address wrap ("1+65536" is line 1),
 *    the temporary file's line words (block << 8 | offset >> 1, the low bit
 *    the mark of 'g') are 16 bits, the counts are longs (32 bits);
 *  - the line table is malloc()ed in the 64K segment at 0x4104 (measured):
 *    a line's address is a 16-bit pointer, an address computed with "+n" or
 *    "-n" wraps around the segment before it is checked against the buffer;
 *    the difference of two pointers is the 8086's (exact, then halved);
 *  - savedfile, file, linebuf, rhsbuf and expbuf lie one behind the other in
 *    the bss, as in the original (0x2df2 .. 0x31f6): a file name of more than
 *    63 characters runs on into the next array, as the original's does
 *    (docs/MUTOS_ORIGINAL_BUGS.md, ED-01 ..);
 *  - chars are signed;
 *  - the standard input is read one byte at a time (read(0, &c, 1)), so
 *    that a command that runs the shell ('!') leaves the rest of the input
 *    to ed; after an error the standard input is sought to its end (v7's
 *    lseek(0, 0L, 2)), which ends a script read from a file at its first
 *    error;
 *  - the output is written a line at a time (at a newline or 64 bytes);
 *  - the temporary file is /tmp/eXXXXX - libc.a's mktemp() on the 16-bit
 *    process number -, the MUTOS root's tmp/ (src/h/mutos_root.h); the file
 *    names of the commands and of the command line are host paths.
 *
 * '!' runs the host's /bin/sh, which reads the rest of the line from the
 * standard input, as the original's "sh -t" does, with the MUTOS root's
 * bin/ first in the PATH (as mutos_make runs its commands).
 *
 * What the port refuses (mu_trap(): exit status 125, the last message ending
 * in "refused"), where the original depends on state that the port does not
 * model:
 *  - a write or read past expbuf's end through the run-on names (the
 *    original overwrites circfl and the pointers of the buffer), or through
 *    a pattern with a NUL byte that matches past a line's end;
 *  - a line table that would end above 0xF000 in the segment (where the
 *    kernel refuses the break - "MEM?" - is not measured; mutos_make draws
 *    the same line);
 *  - a pattern on which the original loops for ever (an empty \(\) repeated
 *    by \1*);
 *  - the encryption (-x, 'x'): the original runs /usr/lib/makekey, a MUTOS
 *    program, and keys the temporary file with the time; without a makekey
 *    in the MUTOS root the 'x' command fails as the original's does
 *    ("?crypt: cannot generate key"), and the next 'e' or 'E' - which keys
 *    the temporary file with the time - is refused, as is -x on the command
 *    line (the original jumps through an unset jmp_buf there).
 */
#define _POSIX_C_SOURCE 200809L	/* fileno(), sigaction(), setenv() */

#define MU_TOOL "ed"
#include "../h/mutos_stdio.h"
#include "../h/mutos_root.h"

#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>

typedef int16_t mint;		/* the 8086's int */
typedef uint16_t mptr;		/* a pointer into the line table: its address */

#define FNSIZE	64
#define LBSIZE	512
#define ESIZE	128
#define GBSIZE	256
#define NBRA	5
#define KSIZE	9

#define CBRA	1
#define CCHR	2
#define CDOT	4
#define CCL	6
#define NCCL	8
#define CDOL	10
#define CEOF	11
#define CKET	12
#define CBACK	14

#define STAR	01

#define READ	0
#define WRITE	1

/* The line table: malloc(nlall * 2) at the start of the heap (measured: the
 * break is 0x4102, the end of the bss, and V7's malloc() puts its block's
 * header word before the block). realloc() after free() extends the block
 * where it is: nothing else is malloc()ed, so zero never moves. */
#define ED_ZERO		0x4104
#define ED_SEGTOP	0xF000		/* the line table may end there, no further */

static const char Q[] = "";
static const char T[] = "TMP";
static const char WRERR[] = "WRITE ERROR";

/* the bss of the original, in its order: savedfile 2df2, file 2e32,
 * linebuf 2e72, rhsbuf 3072, expbuf 3172 (ESIZE+4), circfl 31f6 */
#define A_SAVEDFILE	0
#define A_FILE		(A_SAVEDFILE + FNSIZE)
#define A_LINEBUF	(A_FILE + FNSIZE)
#define A_RHSBUF	(A_LINEBUF + LBSIZE)
#define A_EXPBUF	(A_RHSBUF + LBSIZE / 2)
#define AREA		(A_EXPBUF + ESIZE + 4)
static signed char area[AREA];
#define savedfile	(area + A_SAVEDFILE)
#define file		(area + A_FILE)
#define linebuf		(area + A_LINEBUF)
#define rhsbuf		(area + A_RHSBUF)
#define expbuf		(area + A_EXPBUF)

static int	peekc;
static int	lastc;
static int	circfl;
static mptr	zero;
static mptr	dot;
static mptr	dol;
static mptr	addr1;
static mptr	addr2;
static signed char genbuf[LBSIZE];
static int32_t	count;
static signed char *nextip;
static signed char *linebp;
static int	ninbuf;
static int	io;
static int	pflag;
static struct sigaction oldhup;
static struct sigaction oldquit;
static int	vflag = 1;
static int	xflag;
static int	kflag;
static signed char key[KSIZE + 1];
static int	listf;
static int	col;
static const signed char *globp;
static int	tfile = -1;
static uint16_t	tline;
static char	tfname[] = "/tmp/eXXXXX";
static signed char *loc1;
static signed char *loc2;
static signed char *locs;
static signed char ibuff[512];
static int	iblock = -1;
static signed char obuff[512];
static int	oblock = -1;
static int	ichanged;
static int	nleft;
static uint16_t	names[26];
static int	anymarks;
static signed char *braslist[NBRA];
static signed char *braelist[NBRA];
static int	nbra;
static uint16_t	subnewa;
static uint16_t	subolda;
static int	fchange;
static int	wrapp;
static uint16_t	nlall = 128;
static jmp_buf	savej;
static int	savej_set;
static signed char line[70];
static signed char *linp = line;

/* the line table, the words of the heap from ED_ZERO on */
static uint16_t	ltab[(0x10000 - ED_ZERO) / 2];

static MU_NORETURN void error(const signed char *s);
static void commands(void);
static mptr address(void);
static signed char *getline_(uint16_t tl);
static int putline(void);
static signed char *getblock(uint16_t atl, int iof);
static void ed_puts(const signed char *sp);
static void putchr(int ac);
static void putd(void);
static int getchr(void);
static void quit(void);
static int append(int (*f)(void), mptr a);
static void global(int k);
static void compile(int aeof);
static int execute(int gf, mptr addr);
static int advance(signed char *lp, signed char *ep);

#define S(s)	((const signed char *)(s))

/* ---- the model of the segment -------------------------------------------- */

/* p + k words: the 8086's pointer arithmetic, modulo 64K */
static mptr
LA(mptr p, int k)
{
	return (mptr)(p + 2 * k);
}

/* p - q: the difference of two pointers as the original computes it - the
 * 32-bit difference (the borrow its high word), divided by 2 (idiv) */
static mint
PD(mptr p, mptr q)
{
	int32_t d = (int32_t)p - (int32_t)q;

	return (mint)(d / 2);
}

/* the word at p of the line table (malloc()ed: nlall words) */
static uint16_t *
LW(mptr p)
{
	uint32_t off = (uint32_t)(mptr)(p - zero);

	if (p < zero || off >= 2u * nlall || (off & 1))
		mu_trap("a word outside the line table");
	return &ltab[off / 2];
}

static int
in_area(const signed char *p)
{
	uintptr_t a = (uintptr_t)p, b = (uintptr_t)area;

	return a >= b && a < b + AREA;
}

/* the byte at p of the bss's run of arrays (a read past them: refused) */
static signed char
RD(const signed char *p)
{
	if (!in_area(p))
		mu_trap("a read past expbuf (a NUL byte in a pattern, or a run-on name)");
	return *p;
}

/* a store into the bss's run of arrays (past them: circfl and the buffer's
 * pointers in the original - refused) */
static void
WR(signed char *p, int c)
{
	if (!in_area(p))
		mu_trap("a write past expbuf, over the buffer's pointers");
	*p = (signed char)c;
}

/* a NUL-terminated name of the area as a host string */
static const char *
hname(const signed char *p)
{
	static char buf[AREA + 1];
	size_t n = 0;

	while (RD(p + n) != 0)
		n++;
	memcpy(buf, p, n);
	buf[n] = 0;
	return buf;
}

/* the MUTOS path as a host path */
static const char *
ed_mpath(const char *mpath)
{
	static char buf[4096 + 1024];

	if (mutos_path(buf, sizeof(buf), mpath) == NULL)
		mu_trap("a file name longer than the host takes");
	return buf;
}

/* signal(): V7's - a handler is reset to SIG_DFL when it runs, the signal
 * not held while it runs (the handlers jump back to the commands) */
static void
ed_signal(int sig, void (*h)(int), struct sigaction *old)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = h;
	sigemptyset(&sa.sa_mask);
	if (h != SIG_IGN && h != SIG_DFL)
		sa.sa_flags = SA_RESETHAND | SA_NODEFER;
	sigaction(sig, &sa, old);
}

/*
 * mktemp() (libc.a): the process number's digits replace the trailing X's,
 * from the last one; then, as long as the name exists, its first digit
 * becomes 'a', 'b', ... - "/" when 'z' is reached. The process number is 16
 * bits, as on the 8086.
 */
static const char *
ed_mktemp(char *as)
{
	char *s = as;
	uint16_t pid = (uint16_t)(getpid() & 0x7fff);
	int i;

	if (pid == 0)
		pid = 1;
	while (*s++ != 0)
		;
	s--;
	while (*--s == 'X') {
		*s = (char)(pid % 10 + '0');
		pid = (uint16_t)(pid / 10);
	}
	s++;
	i = 'a';
	while (access(ed_mpath(as), F_OK) != -1) {
		if (i == 'z')
			return "/";
		*s = (char)i++;
	}
	return as;
}

static const char *tfpath;	/* the temporary file's host path */

/* ---- the encryption ------------------------------------------------------ */

/* getkey (text 21a2): the key, read with the echo off; a standard input
 * that is not a terminal is "?Input not tty" */
static int
getkey(void)
{
	struct termios b;
	struct sigaction sig;
	tcflag_t save;
	signed char *p;
	int c;

	ed_signal(SIGINT, SIG_IGN, &sig);
	if (tcgetattr(0, &b) == -1)
		error(S("Input not tty"));
	save = b.c_lflag;
	b.c_lflag &= ~(tcflag_t)ECHO;
	tcsetattr(0, TCSANOW, &b);
	ed_puts(S("Key:"));
	p = key;
	while (((c = getchr()) != EOF) && (c != '\n')) {
		if (p < &key[KSIZE])
			*p++ = (signed char)c;
	}
	*p = 0;
	b.c_lflag = save;
	tcsetattr(0, TCSANOW, &b);
	sigaction(SIGINT, &sig, NULL);
	return key[0] != 0;
}

/*
 * crinit (text 224e): a null key is "no encryption" (0). Any other key is
 * zeroed and handed to /usr/lib/makekey (or /lib/makekey) through a pipe;
 * without one the child exits and the parent reads back the 10 bytes it
 * wrote itself, not 13: "crypt: cannot generate key". A makekey in the MUTOS
 * root is a MUTOS program the host cannot run: refused.
 */
static int
crinit(signed char *keyp)
{
	struct stat st;

	if (*keyp == 0)
		return 0;
	while (*keyp)
		*keyp++ = '\0';
	if (stat(ed_mpath("/usr/lib/makekey"), &st) == 0 ||
	    stat(ed_mpath("/lib/makekey"), &st) == 0)
		mu_trap("makekey, a MUTOS program, in the MUTOS root");
	error(S("crypt: cannot generate key"));
}

/* makekey (text 24d6) from init(): the temporary file's key is the key
 * xor'ed with the time and the process number */
static MU_NORETURN void
makekey(void)
{
	mu_trap("the encryption of the temporary file, keyed with the time");
}

/* ---- the editor ---------------------------------------------------------- */

static void
init(void)		/* text 1394 */
{
	int i;

	close(tfile);
	tline = 2;
	for (i = 0; i < 26; i++)
		names[i] = 0;
	subnewa = 0;
	anymarks = 0;
	iblock = -1;
	oblock = -1;
	ichanged = 0;
	close(creat(tfpath, 0600));
	tfile = open(tfpath, O_RDWR);
	if (xflag)
		makekey();
	dot = dol = zero;
}

static void
onintr(int sig)		/* text 0a6e */
{
	(void)sig;
	ed_signal(SIGINT, onintr, NULL);
	putchr('\n');
	lastc = '\n';
	error(S(Q));
}

static void putfile(void);

static void
onhup(int sig)		/* text 0a9e */
{
	(void)sig;
	ed_signal(SIGINT, SIG_IGN, NULL);
	ed_signal(SIGHUP, SIG_IGN, NULL);
	if (dol > zero) {
		addr1 = LA(zero, 1);
		addr2 = dol;
		io = creat("ed.hup", 0666);
		if (io > 0)
			putfile();
	}
	fchange = 0;
	quit();
}

static void
onterm(int sig)		/* signal(SIGTERM, quit) */
{
	(void)sig;
	quit();
}

static void
error(const signed char *s)	/* text 0afa */
{
	int c;

	wrapp = 0;
	listf = 0;
	putchr('?');
	ed_puts(s);
	count = 0;
	lseek(0, (off_t)0, SEEK_END);
	pflag = 0;
	if (globp)
		lastc = '\n';
	globp = 0;
	peekc = lastc;
	if (lastc)
		while ((c = getchr()) != '\n' && c != EOF)
			;
	if (io > 0) {
		close(io);
		io = -1;
	}
	if (!savej_set)
		mu_trap("an error before the commands (a jump through an unset jmp_buf)");
	longjmp(savej, 1);
}

static int
getchr(void)		/* text 0b9a */
{
	unsigned char c;

	if ((lastc = peekc) != 0) {
		peekc = 0;
		return lastc;
	}
	if (globp) {
		if ((lastc = *globp++) != 0)
			return lastc;
		globp = 0;
		return EOF;
	}
	if (read(0, &c, 1) <= 0)
		return lastc = EOF;
	lastc = c & 0177;
	return lastc;
}

static void
setdot(void)		/* text 0896 */
{
	if (addr2 == 0)
		addr1 = addr2 = dot;
	if (addr1 > addr2)
		error(S(Q));
}

static void
setall(void)		/* text 08c6 */
{
	if (addr2 == 0) {
		addr1 = LA(zero, 1);
		addr2 = dol;
		if (dol == zero)
			addr1 = zero;
	}
	setdot();
}

static void
setnoaddr(void)		/* text 08fa */
{
	if (addr2)
		error(S(Q));
}

static void
nonzero(void)		/* text 0914 */
{
	if (addr1 <= zero || addr2 > dol)
		error(S(Q));
}

static void
newline(void)		/* text 093a */
{
	int c;

	if ((c = getchr()) == '\n')
		return;
	if (c == 'p' || c == 'l') {
		pflag++;
		if (c == 'l')
			listf++;
		if (getchr() == '\n')
			return;
	}
	error(S(Q));
}

/* filename (text 0976): the name is read into file[] with no bound - a
 * long one runs on into linebuf, rhsbuf and expbuf, as in the original */
static void
filename(int comm)
{
	signed char *p1, *p2;
	int c;

	count = 0;
	c = getchr();
	if (c == '\n' || c == EOF) {
		p1 = savedfile;
		if (*p1 == 0 && comm != 'f')
			error(S(Q));
		p2 = file;
		do {
			c = RD(p1++);
			WR(p2++, c);
		} while (c);
		return;
	}
	if (c != ' ')
		error(S(Q));
	while ((c = getchr()) == ' ')
		;
	if (c == '\n')
		error(S(Q));
	p1 = file;
	do {
		WR(p1++, c);
		if (c == ' ' || c == EOF)
			error(S(Q));
	} while ((c = getchr()) != '\n');
	WR(p1++, 0);
	if (savedfile[0] == 0 || comm == 'e' || comm == 'f') {
		p1 = savedfile;
		p2 = file;
		do {
			c = RD(p2++);
			WR(p1++, c);
		} while (c);
	}
}

static void
exfile(void)		/* text 0a42 */
{
	close(io);
	io = -1;
	if (vflag) {
		putd();
		putchr('\n');
	}
}

static int
gettty(void)		/* text 0c0c */
{
	int c;
	const signed char *gf;
	signed char *p;

	p = linebuf;
	gf = globp;
	while ((c = getchr()) != '\n') {
		if (c == EOF) {
			if (gf)
				peekc = c;
			return c;
		}
		if ((c &= 0177) == 0)
			continue;
		*p++ = (signed char)c;
		if (p >= &linebuf[LBSIZE - 2])
			error(S(Q));
	}
	*p++ = 0;
	if (linebuf[0] == '.' && linebuf[1] == 0)
		return EOF;
	return 0;
}

static int
getfile(void)		/* text 0c80 */
{
	int c;
	signed char *lp, *fp;

	lp = linebuf;
	fp = nextip;
	do {
		if (--ninbuf < 0) {
			ssize_t n = read(io, genbuf, LBSIZE);

			if ((ninbuf = (int)n - 1) < 0)
				return EOF;
			/* a byte with the top bit set: crblock() if kflag
			 * - never, kflag is always 0 here (crinit()) */
			fp = genbuf;
		}
		c = *fp++;
		if (c == '\0')
			continue;
		if ((c & 0200) || lp >= &linebuf[LBSIZE]) {
			lastc = '\n';
			error(S(Q));
		}
		*lp++ = (signed char)c;
		count++;
	} while (c != '\n');
	*--lp = 0;
	nextip = fp;
	return 0;
}

static void
putfile(void)		/* text 0d56 */
{
	mptr a1;
	int n;
	signed char *fp, *lp;
	int nib;

	nib = 512;
	fp = genbuf;
	a1 = addr1;
	do {
		lp = getline_(*LW(a1));
		a1 = LA(a1, 1);
		for (;;) {
			if (--nib < 0) {
				n = (int)(fp - genbuf);
				if (write(io, genbuf, (size_t)n) != n) {
					ed_puts(S(WRERR));
					error(S(Q));
				}
				nib = 511;
				fp = genbuf;
			}
			count++;
			if ((*fp++ = RD(lp++)) == 0) {
				fp[-1] = '\n';
				break;
			}
		}
	} while (a1 <= addr2);
	n = (int)(fp - genbuf);
	if (write(io, genbuf, (size_t)n) != n) {
		ed_puts(S(WRERR));
		error(S(Q));
	}
}

static int
append(int (*f)(void), mptr a)	/* text 0e70 */
{
	mptr a1, a2, rdot;
	int nline;
	uint16_t tl;

	nline = 0;
	dot = a;
	while ((*f)() == 0) {
		if ((uint16_t)(PD(dol, zero) + 1) >= nlall) {
			/* free(); realloc(): the block grows where it is */
			nlall = (uint16_t)(nlall + 512);
			if ((uint32_t)zero + 2u * nlall > ED_SEGTOP)
				mu_trap("a line table above 0xF000 - where the break is refused is not measured");
		}
		tl = (uint16_t)putline();
		nline++;
		a1 = dol = LA(dol, 1);
		a2 = LA(a1, 1);
		rdot = dot = LA(dot, 1);
		while (a1 > rdot) {
			a2 = LA(a2, -1);
			a1 = LA(a1, -1);
			*LW(a2) = *LW(a1);
		}
		*LW(rdot) = tl;
	}
	return nline;
}

/* callunix (text 0f64): "sh -t" - the shell reads one line of the standard
 * input, a byte at a time, runs it and ends; here the child reads the line
 * and hands it to the host's sh */
static void
callunix(void)
{
	struct sigaction savint;
	pid_t pid, rpid;
	int retcode;

	setnoaddr();
	if ((pid = fork()) == 0) {
		char *cmd = NULL;
		size_t n = 0, cap = 0;
		unsigned char c;
		const char *old = getenv("PATH");
		const char *root = mutos_root();
		char *path;
		size_t pl;

		sigaction(SIGHUP, &oldhup, NULL);
		sigaction(SIGQUIT, &oldquit, NULL);
		for (;;) {
			if (n + 1 >= cap) {
				cap = cap ? 2 * cap : 128;
				if ((cmd = realloc(cmd, cap)) == NULL)
					_exit(0100);
			}
			if (read(0, &c, 1) <= 0 || c == '\n')
				break;
			cmd[n++] = (char)c;
		}
		cmd[n] = 0;
		if (old == NULL)
			old = ":/bin:/usr/bin";
		pl = strlen(root) + strlen(old) + 8;
		if ((path = malloc(pl)) != NULL) {
			snprintf(path, pl, "%s/bin:%s", root, old);
			setenv("PATH", path, 1);
		}
		execl("/bin/sh", "sh", "-c", cmd, (char *)NULL);
		_exit(0100);
	}
	ed_signal(SIGINT, SIG_IGN, &savint);
	while ((rpid = wait(&retcode)) != pid && rpid != -1)
		;
	sigaction(SIGINT, &savint, NULL);
	ed_puts(S("!"));
}

static void
quit(void)		/* text 0ff2 */
{
	if (vflag && fchange && dol != zero) {
		fchange = 0;
		error(S(Q));
	}
	unlink(tfpath);
	mu_exit(0);
}

static void
rdelete(mptr ad1, mptr ad2)	/* text 1056 */
{
	mptr a1, a2, a3;

	a1 = ad1;
	a2 = LA(ad2, 1);
	a3 = dol;
	dol = LA(dol, -PD(a2, a1));
	do {
		*LW(a1) = *LW(a2);
		a1 = LA(a1, 1);
		a2 = LA(a2, 1);
	} while (a2 <= a3);
	a1 = ad1;
	if (a1 > dol)
		a1 = dol;
	dot = a1;
	fchange = 1;
}

static void
delete(void)		/* text 1036 */
{
	setdot();
	newline();
	nonzero();
	rdelete(addr1, addr2);
}

static void
gdelete(void)		/* text 10ac */
{
	mptr a1, a2, a3;

	a3 = dol;
	for (a1 = LA(zero, 1); (*LW(a1) & 01) == 0; a1 = LA(a1, 1))
		if (a1 >= a3)
			return;
	for (a2 = LA(a1, 1); a2 <= a3;) {
		if (*LW(a2) & 01) {
			a2 = LA(a2, 1);
			dot = a1;
		} else {
			*LW(a1) = *LW(a2);
			a1 = LA(a1, 1);
			a2 = LA(a2, 1);
		}
	}
	dol = LA(a1, -1);
	if (dot > dol)
		dot = dol;
	fchange = 1;
}

static signed char *
getline_(uint16_t tl)	/* getline, text 110e */
{
	signed char *bp, *lp;
	int nl;
	signed char c;

	lp = linebuf;
	bp = getblock(tl, READ);
	nl = nleft;
	tl &= (uint16_t)~0377;
	for (;;) {
		c = *bp++;
		WR(lp++, c);
		if (c == 0)
			break;
		if (--nl == 0) {
			tl = (uint16_t)(tl + 0400);
			bp = getblock(tl, READ);
			nl = nleft;
		}
	}
	return linebuf;
}

static int
putline(void)		/* text 1166 */
{
	signed char *bp, *lp;
	int nl;
	uint16_t tl;

	fchange = 1;
	lp = linebuf;
	tl = tline;
	bp = getblock(tl, WRITE);
	nl = nleft;
	tl &= (uint16_t)~0377;
	while ((*bp = RD(lp++)) != 0) {
		if (*bp++ == '\n') {
			*--bp = 0;
			linebp = lp;
			break;
		}
		if (--nl == 0) {
			tl = (uint16_t)(tl + 0400);
			bp = getblock(tl, WRITE);
			nl = nleft;
		}
	}
	nl = tline;
	tline = (uint16_t)(tline + ((((mint)(lp - linebuf) + 03) >> 1) & 077776));
	return nl;
}

static void
blkio(int b, signed char *buf, int wr)	/* text 134c */
{
	ssize_t n;

	lseek(tfile, (off_t)b << 9, SEEK_SET);
	n = wr ? write(tfile, buf, 512) : read(tfile, buf, 512);
	if (n != 512)
		error(S(T));
}

static signed char *
getblock(uint16_t atl, int iof)	/* text 11f4 */
{
	int bno, off;

	bno = (atl >> 8) & 0377;
	off = (atl << 1) & 0774;
	if (bno >= 255) {
		lastc = '\n';
		error(S(T));
	}
	nleft = 512 - off;
	if (bno == iblock) {
		ichanged |= iof;
		return ibuff + off;
	}
	if (bno == oblock)
		return obuff + off;
	if (iof == READ) {
		if (ichanged)
			blkio(iblock, ibuff, 1);
		ichanged = 0;
		iblock = bno;
		blkio(bno, ibuff, 0);
		return ibuff + off;
	}
	if (oblock >= 0)
		blkio(oblock, obuff, 1);
	oblock = bno;
	return obuff + off;
}

static void
global(int k)		/* text 142a */
{
	signed char *gp;
	int c;
	mptr a1;
	signed char globuf[GBSIZE];

	if (globp)
		error(S(Q));
	setall();
	nonzero();
	if ((c = getchr()) == '\n')
		error(S(Q));
	compile(c);
	gp = globuf;
	while ((c = getchr()) != '\n') {
		if (c == EOF)
			error(S(Q));
		if (c == '\\') {
			c = getchr();
			if (c != '\n')
				*gp++ = '\\';
		}
		*gp++ = (signed char)c;
		if (gp >= &globuf[GBSIZE - 2])
			error(S(Q));
	}
	*gp++ = '\n';
	*gp++ = 0;
	for (a1 = zero; a1 <= dol; a1 = LA(a1, 1)) {
		*LW(a1) &= (uint16_t)~01;
		if (a1 >= addr1 && a1 <= addr2 && execute(0, a1) == k)
			*LW(a1) |= 01;
	}
	/* Special case: g/.../d (avoid n^2 algorithm) */
	if (globuf[0] == 'd' && globuf[1] == '\n' && globuf[2] == '\0') {
		gdelete();
		return;
	}
	for (a1 = zero; a1 <= dol; a1 = LA(a1, 1)) {
		if (*LW(a1) & 01) {
			*LW(a1) &= (uint16_t)~01;
			dot = a1;
			globp = globuf;
			commands();
			a1 = zero;
		}
	}
}

static void
join(void)		/* text 155c */
{
	signed char *gp, *lp;
	mptr a1;

	gp = genbuf;
	for (a1 = addr1; a1 <= addr2; a1 = LA(a1, 1)) {
		lp = getline_(*LW(a1));
		while ((*gp = RD(lp++)) != 0)
			if (gp++ >= &genbuf[LBSIZE - 2])
				error(S(Q));
	}
	lp = linebuf;
	gp = genbuf;
	do
		WR(lp, *gp++);
	while (*lp++);
	*LW(addr1) = (uint16_t)putline();
	if (addr1 < addr2)
		rdelete(LA(addr1, 1), addr2);
	dot = addr1;
}

static int
getsub(void)		/* text 1776 */
{
	signed char *p1, *p2;
	signed char c;

	p1 = linebuf;
	if ((p2 = linebp) == 0)
		return EOF;
	do {
		c = RD(p2++);
		WR(p1++, c);
	} while (c);
	linebp = 0;
	return 0;
}

static signed char *
place(signed char *sp, signed char *l1, signed char *l2)	/* text 1884 */
{
	while (l1 < l2) {
		*sp++ = RD(l1++);
		if (sp >= &genbuf[LBSIZE])
			error(S(Q));
	}
	return sp;
}

static void
dosub(void)		/* text 17a6 */
{
	signed char *lp, *sp, *rp;
	int c;

	lp = linebuf;
	sp = genbuf;
	rp = rhsbuf;
	while (lp < loc1) {
		if (sp >= &genbuf[LBSIZE])
			mu_trap("a match past genbuf");
		*sp++ = RD(lp++);
	}
	while ((c = RD(rp++) & 0377) != 0) {
		if (c == '&') {
			sp = place(sp, loc1, loc2);
			continue;
		} else if ((c & 0200) && (c &= 0177) >= '1' && c < nbra + '1') {
			sp = place(sp, braslist[c - '1'], braelist[c - '1']);
			continue;
		}
		*sp++ = (signed char)(c & 0177);
		if (sp >= &genbuf[LBSIZE])
			error(S(Q));
	}
	lp = loc2;
	loc2 = linebuf + (sp - genbuf);
	while ((*sp++ = RD(lp++)) != 0)
		if (sp >= &genbuf[LBSIZE])
			error(S(Q));
	lp = linebuf;
	sp = genbuf;
	do
		WR(lp, *sp++);
	while (*lp++);
}

static int
compsub(void)		/* text 16cc */
{
	int seof, c;
	signed char *p;

	if ((seof = getchr()) == '\n' || seof == ' ')
		error(S(Q));
	compile(seof);
	p = rhsbuf;
	for (;;) {
		c = getchr();
		if (c == '\\')
			c = getchr() | 0200;
		if (c == '\n') {
			if (globp)
				c |= 0200;
			else
				error(S(Q));
		}
		if (c == seof)
			break;
		*p++ = (signed char)c;
		if (p >= &rhsbuf[LBSIZE / 2])
			error(S(Q));
	}
	*p++ = 0;
	if ((peekc = getchr()) == 'g') {
		peekc = 0;
		newline();
		return 1;
	}
	newline();
	return 0;
}

static void
substitute(int inglob)	/* text 15e8 */
{
	int i, gsubf;
	mint nl;
	mptr a1;

	gsubf = compsub();
	for (a1 = addr1; a1 <= addr2; a1 = LA(a1, 1)) {
		if (execute(0, a1) == 0)
			continue;
		inglob |= 01;
		dosub();
		if (gsubf) {
			while (RD(loc2)) {
				if (execute(1, 0) == 0)
					break;
				dosub();
			}
		}
		subnewa = (uint16_t)putline();
		*LW(a1) &= (uint16_t)~01;
		if (anymarks) {
			for (i = 0; i < 26; i++)
				if (names[i] == *LW(a1))
					names[i] = subnewa;
		}
		subolda = *LW(a1);
		*LW(a1) = subnewa;
		nl = (mint)append(getsub, a1);	/* + (zero - ozero): 0 */
		a1 = LA(a1, nl);
		addr2 = LA(addr2, nl);
	}
	if (inglob == 0)
		error(S(Q));
}

static void
reverse(mptr a1, mptr a2)	/* text 19cc */
{
	uint16_t t;

	for (;;) {
		a2 = LA(a2, -1);
		t = *LW(a2);
		if (a2 <= a1)
			return;
		*LW(a2) = *LW(a1);
		*LW(a1) = t;
		a1 = LA(a1, 1);
	}
}

static int
getcopy(void)		/* text 19f8 */
{
	if (addr1 > addr2)
		return EOF;
	getline_(*LW(addr1));
	addr1 = LA(addr1, 1);
	return 0;
}

/* move (text 18b2): the MUTOS change - the lines moved after themselves or
 * after the line before them: nothing is done */
static void
move(int cflag)
{
	mptr adt, ad1, ad2;

	setdot();
	nonzero();
	if ((adt = address()) == 0)
		error(S(Q));
	newline();
	if (cflag) {
		ad1 = dol;
		append(getcopy, ad1);
		ad1 = LA(ad1, 1);
		ad2 = dol;		/* + delta (zero - ozero): 0 */
	} else {
		if (adt == addr1)
			return;
		if (adt == LA(addr1, -1))
			return;
		ad2 = addr2;
		for (ad1 = addr1; ad1 <= ad2; ad1 = LA(ad1, 1))
			*LW(ad1) &= (uint16_t)~01;
		ad1 = addr1;
	}
	ad2 = LA(ad2, 1);
	if (adt < ad1) {
		dot = LA(adt, PD(ad2, ad1));
		adt = LA(adt, 1);
		if (adt == ad1)
			return;
		reverse(adt, ad1);
		reverse(ad1, ad2);
		reverse(adt, ad2);
	} else if (adt >= ad2) {
		dot = adt;
		adt = LA(adt, 1);
		reverse(ad1, ad2);
		reverse(ad2, adt);
		reverse(ad1, adt);
	} else
		error(S(Q));
	fchange = 1;
}

static void
compile(int aeof)	/* text 1a24 */
{
	int eof, c;
	signed char *ep;
	signed char *lastep;
	signed char bracket[NBRA], *bracketp;
	int cclcnt;

	ep = expbuf;
	eof = aeof;
	bracketp = bracket;
	if ((c = getchr()) == eof) {
		if (*ep == 0)
			error(S(Q));
		return;
	}
	circfl = 0;
	nbra = 0;
	if (c == '^') {
		c = getchr();
		circfl++;
	}
	peekc = c;
	lastep = 0;
	for (;;) {
		if (ep >= &expbuf[ESIZE])
			goto cerror;
		c = getchr();
		if (c == eof) {
			if (bracketp != bracket)
				goto cerror;
			*ep++ = CEOF;
			return;
		}
		if (c != '*')
			lastep = ep;
		switch (c) {

		case '\\':
			if ((c = getchr()) == '(') {
				if (nbra >= NBRA)
					goto cerror;
				*bracketp++ = (signed char)nbra;
				*ep++ = CBRA;
				*ep++ = (signed char)nbra++;
				continue;
			}
			if (c == ')') {
				if (bracketp <= bracket)
					goto cerror;
				*ep++ = CKET;
				*ep++ = *--bracketp;
				continue;
			}
			if (c >= '1' && c < '1' + NBRA) {
				*ep++ = CBACK;
				*ep++ = (signed char)(c - '1');
				continue;
			}
			*ep++ = CCHR;
			if (c == '\n')
				goto cerror;
			*ep++ = (signed char)c;
			continue;

		case '.':
			*ep++ = CDOT;
			continue;

		case '\n':
			goto cerror;

		case '*':
			if (lastep == 0 || *lastep == CBRA || *lastep == CKET)
				goto defchar;
			*lastep |= STAR;
			continue;

		case '$':
			if ((peekc = getchr()) != eof)
				goto defchar;
			*ep++ = CDOL;
			continue;

		case '[':
			*ep++ = CCL;
			*ep++ = 0;
			cclcnt = 1;
			if ((c = getchr()) == '^') {
				c = getchr();
				ep[-2] = NCCL;
			}
			do {
				if (c == '\n')
					goto cerror;
				if (c == '-' && ep[-1] != 0) {
					if ((c = getchr()) == ']') {
						*ep++ = '-';
						cclcnt++;
						break;
					}
					while (ep[-1] < c) {
						*ep = (signed char)(ep[-1] + 1);
						ep++;
						cclcnt++;
						if (ep >= &expbuf[ESIZE])
							goto cerror;
					}
				}
				*ep++ = (signed char)c;
				cclcnt++;
				if (ep >= &expbuf[ESIZE])
					goto cerror;
			} while ((c = getchr()) != ']');
			lastep[1] = (signed char)cclcnt;
			continue;

		defchar:
		default:
			*ep++ = CCHR;
			*ep++ = (signed char)c;
		}
	}
cerror:
	expbuf[0] = 0;
	nbra = 0;
	error(S(Q));
}

static int
execute(int gf, mptr addr)	/* text 1ca0 */
{
	signed char *p1, *p2;
	signed char c;
	int i;

	for (i = 0; i < NBRA; i++) {
		braslist[i] = 0;
		braelist[i] = 0;
	}
	if (gf) {
		if (circfl)
			return 0;
		p1 = linebuf;
		p2 = genbuf;
		do
			WR(p1, *p2++);
		while (*p1++);
		locs = p1 = loc2;
	} else {
		if (addr == zero)
			return 0;
		p1 = getline_(*LW(addr));
		locs = 0;
	}
	p2 = expbuf;
	if (circfl) {
		loc1 = p1;
		return advance(p1, p2);
	}
	/* fast check for first character */
	if (*p2 == CCHR) {
		c = p2[1];
		do {
			if (RD(p1) != c)
				continue;
			if (advance(p1, p2)) {
				loc1 = p1;
				return 1;
			}
		} while (RD(p1++));
		return 0;
	}
	/* regular algorithm */
	do {
		if (advance(p1, p2)) {
			loc1 = p1;
			return 1;
		}
	} while (RD(p1++));
	return 0;
}

static int
backref(int i, signed char *lp)	/* text 1f6e */
{
	signed char *bp;

	bp = braslist[i];
	while (RD(bp++) == RD(lp++))
		if (bp >= braelist[i])
			return 1;
	return 0;
}

static int
cclass(signed char *set, signed char c, int af)	/* text 1fae */
{
	mint n;

	if (c == 0)
		return 0;
	n = RD(set++);
	while ((n = (mint)(n - 1)) != 0)
		if (RD(set++) == c)
			return af;
	return !af;
}

/* a bracket number of the pattern (only a damaged expbuf has another) */
static int
bra(signed char i)
{
	if (i < 0 || i >= NBRA)
		mu_trap("a damaged pattern (a bracket number past braslist)");
	return i;
}

static int
advance(signed char *lp, signed char *ep)	/* text 1d82 */
{
	signed char *curlp;
	int i;

	for (;;) switch (RD(ep++)) {

	case CCHR:
		if (RD(ep++) == RD(lp++))
			continue;
		return 0;

	case CDOT:
		if (RD(lp++))
			continue;
		return 0;

	case CDOL:
		if (RD(lp) == 0)
			continue;
		return 0;

	case CEOF:
		loc2 = lp;
		return 1;

	case CCL:
		if (cclass(ep, RD(lp++), 1)) {
			ep += RD(ep);
			continue;
		}
		return 0;

	case NCCL:
		if (cclass(ep, RD(lp++), 0)) {
			ep += RD(ep);
			continue;
		}
		return 0;

	case CBRA:
		braslist[bra(RD(ep++))] = lp;
		continue;

	case CKET:
		braelist[bra(RD(ep++))] = lp;
		continue;

	case CBACK:
		if (braelist[i = bra(RD(ep++))] == 0)
			error(S(Q));
		if (backref(i, lp)) {
			lp += braelist[i] - braslist[i];
			continue;
		}
		return 0;

	case CBACK | STAR:
		if (braelist[i = bra(RD(ep++))] == 0)
			error(S(Q));
		curlp = lp;
		if (braelist[i] == braslist[i]) {
			/* "while (backref(i, lp)) lp += 0;", and then
			 * "while (lp >= curlp) { advance(); lp -= 0; }" */
			if (backref(i, lp) || !advance(lp, ep))
				mu_trap("an empty \\(\\) repeated by \\1* (the original loops for ever)");
			return 1;
		}
		while (backref(i, lp))
			lp += braelist[i] - braslist[i];
		while (lp >= curlp) {
			if (advance(lp, ep))
				return 1;
			lp -= braelist[i] - braslist[i];
		}
		continue;

	case CDOT | STAR:
		curlp = lp;
		while (RD(lp++))
			;
		goto star;

	case CCHR | STAR:
		curlp = lp;
		while (RD(lp++) == RD(ep))
			;
		ep++;
		goto star;

	case CCL | STAR:
	case NCCL | STAR:
		curlp = lp;
		while (cclass(ep, RD(lp++), RD(ep - 1) == (CCL | STAR)))
			;
		ep += RD(ep);
		goto star;

	star:
		do {
			lp--;
			if (lp == locs)
				break;
			if (advance(lp, ep))
				return 1;
		} while (lp > curlp);
		return 0;

	default:
		error(S(Q));
	}
}

static void
putd(void)		/* text 1fe4 */
{
	int r;

	r = (int)(count % 10);
	count /= 10;
	if (count)
		putd();
	putchr(r + '0');
}

static void
ed_puts(const signed char *sp)	/* puts, text 2034 */
{
	int ar = in_area(sp);	/* savedfile, file: through the checks */

	col = 0;
	while (ar ? RD(sp) : *sp)
		putchr(*sp++);
	putchr('\n');
}

static void
putchr(int ac)		/* text 2062 */
{
	signed char *lp;
	int c;

	lp = linp;
	c = ac;
	if (listf) {
		col++;
		if (col >= 72) {
			col = 0;
			*lp++ = '\\';
			*lp++ = '\n';
		}
		if (c == '\t') {
			c = '>';
			goto esc;
		}
		if (c == '\b') {
			c = '<';
esc:
			*lp++ = '-';
			*lp++ = '\b';
			*lp++ = (signed char)c;
			goto out;
		}
		if (c < ' ' && c != '\n') {
			*lp++ = '\\';
			*lp++ = (signed char)((c >> 3) + '0');
			*lp++ = (signed char)((c & 07) + '0');
			col += 2;
			goto out;
		}
	}
	*lp++ = (signed char)c;
out:
	if (c == '\n' || lp >= &line[64]) {
		ssize_t w;

		linp = line;
		w = write(1, line, (size_t)(lp - line));
		(void)w;
		return;
	}
	linp = lp;
}

static mptr
address(void)		/* text 06ba */
{
	mptr a1;
	int minus, c;
	mint n;
	int relerr;

	minus = 0;
	a1 = 0;
	for (;;) {
		c = getchr();
		if ('0' <= c && c <= '9') {
			n = 0;
			do {
				n = (mint)(n * 10);
				n = (mint)(n + (c - '0'));
			} while ((c = getchr()) >= '0' && c <= '9');
			peekc = c;
			if (a1 == 0)
				a1 = zero;
			if (minus < 0)
				n = (mint)-n;
			a1 = LA(a1, n);
			minus = 0;
			continue;
		}
		relerr = 0;
		if (a1 || minus)
			relerr++;
		switch (c) {
		case ' ':
		case '\t':
			continue;

		case '+':
			minus++;
			if (a1 == 0)
				a1 = dot;
			continue;

		case '-':
		case '^':
			minus--;
			if (a1 == 0)
				a1 = dot;
			continue;

		case '?':
		case '/':
			compile(c);
			a1 = dot;
			for (;;) {
				if (c == '/') {
					a1 = LA(a1, 1);
					if (a1 > dol)
						a1 = zero;
				} else {
					a1 = LA(a1, -1);
					if (a1 < zero)
						a1 = dol;
				}
				if (execute(0, a1))
					break;
				if (a1 == dot)
					error(S(Q));
			}
			break;

		case '$':
			a1 = dol;
			break;

		case '.':
			a1 = dot;
			break;

		case '\'':
			if ((c = getchr()) < 'a' || c > 'z')
				error(S(Q));
			for (a1 = zero; a1 <= dol; a1 = LA(a1, 1))
				if (names[c - 'a'] == (*LW(a1) & (uint16_t)~01))
					break;
			break;

		default:
			peekc = c;
			if (a1 == 0)
				return 0;
			a1 = LA(a1, minus);
			if (a1 < zero || a1 > dol)
				error(S(Q));
			return a1;
		}
		if (relerr)
			error(S(Q));
	}
}

static void
commands(void)		/* text 0200 */
{
	mptr a1;
	int c;

	for (;;) {
	if (pflag) {
		pflag = 0;
		addr1 = addr2 = dot;
		goto print;
	}
	addr1 = 0;
	addr2 = 0;
	do {
		addr1 = addr2;
		if ((a1 = address()) == 0) {
			c = getchr();
			break;
		}
		addr2 = a1;
		if ((c = getchr()) == ';') {
			c = ',';
			dot = a1;
		}
	} while (c == ',');
	if (addr1 == 0)
		addr1 = addr2;
	switch (c) {

	case 'a':
		setdot();
		newline();
		append(gettty, addr2);
		continue;

	case 'c':
		delete();
		append(gettty, LA(addr1, -1));
		continue;

	case 'd':
		delete();
		continue;

	case 'E':
		fchange = 0;
		c = 'e';
		/* FALLTHROUGH */
	case 'e':
		setnoaddr();
		if (vflag && fchange) {
			fchange = 0;
			error(S(Q));
		}
		filename(c);
		init();
		addr2 = zero;
		goto caseread;

	case 'f':
		setnoaddr();
		filename(c);
		ed_puts(savedfile);
		continue;

	case 'g':
		global(1);
		continue;

	case 'i':
		setdot();
		nonzero();
		newline();
		append(gettty, LA(addr2, -1));
		continue;

	case 'j':		/* the MUTOS change: one address, it and the next */
		if (addr2 == addr1) {
			if (addr1 == 0)
				addr1 = dot;
			addr2 = LA(addr1, 1);
		}
		setdot();
		newline();
		nonzero();
		join();
		continue;

	case 'k':
		if ((c = getchr()) < 'a' || c > 'z')
			error(S(Q));
		newline();
		setdot();
		nonzero();
		names[c - 'a'] = *LW(addr2) & (uint16_t)~01;
		anymarks |= 01;
		continue;

	case 'm':
		move(0);
		continue;

	case '\n':
		if (addr2 == 0)
			addr2 = LA(dot, 1);
		addr1 = addr2;
		goto print;

	case 'l':
		listf++;
		/* FALLTHROUGH */
	case 'p':
	case 'P':
		newline();
	print:
		setdot();
		nonzero();
		a1 = addr1;
		do {
			uint16_t tl = *LW(a1);

			a1 = LA(a1, 1);
			ed_puts(getline_(tl));
		} while (a1 <= addr2);
		dot = addr2;
		listf = 0;
		continue;

	case 'Q':
		fchange = 0;
		/* FALLTHROUGH */
	case 'q':
		setnoaddr();
		newline();
		quit();
		continue;

	case 'r':
		filename(c);
	caseread:
		if ((io = open(hname(file), O_RDONLY)) < 0) {
			lastc = '\n';
			error(file);
		}
		setall();
		ninbuf = 0;
		c = zero != dol;
		append(getfile, addr2);
		exfile();
		fchange = c;
		continue;

	case 's':
		setdot();
		nonzero();
		substitute(globp != 0);
		continue;

	case 't':
		move(1);
		continue;

	case 'u':
		setdot();
		nonzero();
		newline();
		if ((*LW(addr2) & (uint16_t)~01) != subnewa)
			error(S(Q));
		*LW(addr2) = subolda;
		dot = addr2;
		continue;

	case 'v':
		global(0);
		continue;

	case 'W':
		wrapp++;
		/* FALLTHROUGH */
	case 'w':
		setall();
		nonzero();
		filename(c);
		if (!wrapp ||
		    ((io = open(hname(file), O_WRONLY)) == -1) ||
		    ((lseek(io, (off_t)0, SEEK_END)) == -1))
			if ((io = creat(hname(file), 0666)) < 0)
				error(file);
		wrapp = 0;
		putfile();
		exfile();
		if (addr1 == LA(zero, 1) && addr2 == dol)
			fchange = 0;
		continue;

	case 'x':
		setnoaddr();
		newline();
		xflag = 1;
		ed_puts(S("Entering encrypting mode!"));
		getkey();
		kflag = crinit(key);
		continue;

	case '=':
		setall();
		newline();
		count = PD(addr2, zero) & 077777;
		putd();
		putchr('\n');
		continue;

	case '!':
		callunix();
		continue;

	case EOF:
		return;

	}
	error(S(Q));
	}
}

int
main(int argc, char **argv)	/* text 00b6 */
{
	const char *p1;
	signed char *p2;
	struct sigaction oldintr, oldterm;

	ed_signal(SIGQUIT, SIG_IGN, &oldquit);
	ed_signal(SIGHUP, SIG_IGN, &oldhup);
	ed_signal(SIGINT, SIG_IGN, &oldintr);
	ed_signal(SIGTERM, SIG_IGN, &oldterm);
	if (oldterm.sa_handler == SIG_DFL)
		ed_signal(SIGTERM, onterm, NULL);
	argv++;
	while (argc > 1 && **argv == '-') {
		switch ((*argv)[1]) {

		case '\0':
			vflag = 0;
			break;

		case 'q':
			ed_signal(SIGQUIT, SIG_DFL, NULL);
			vflag = 1;
			break;

		case 'x':
			xflag = 1;
			break;
		}
		argv++;
		argc--;
	}
	if (xflag) {
		getkey();
		kflag = crinit(key);
	}

	if (argc > 1) {
		int c;

		p1 = *argv;
		p2 = savedfile;
		do {
			c = (signed char)*p1++;
			WR(p2++, c);
		} while (c);
		globp = S("r");
	}
	zero = ED_ZERO;
	tfpath = strdup(ed_mpath(ed_mktemp(tfname)));	/* the buffer is reused */
	if (tfpath == NULL)
		mu_trap("no memory for the temporary file's name");
	init();
	if (oldintr.sa_handler != SIG_IGN)
		ed_signal(SIGINT, onintr, NULL);
	if (oldhup.sa_handler != SIG_IGN)
		ed_signal(SIGHUP, onhup, NULL);
	setjmp(savej);
	savej_set = 1;
	commands();
	quit();
	return 0;
}
