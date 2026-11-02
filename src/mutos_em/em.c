/*
 * em.c - mutos_em, the MUTOS 1700 extended text editor: a port of the
 * original em, its line mode.
 *
 * The original binary (mutos/mutos_bin/em.golden.base64.txt, 0411, text
 * 27136, data 2106, bss 10966) has no source. It is the original ed
 * (src/mutos_ed/ed.c: v7's ed.c with the two MUTOS changes) - 33 of ed's 48
 * functions are in it instruction for instruction, ten more with small
 * changes (tests/realbin/emmap.txt) - with a new main() and commands(), an
 * input reader of its own, a session log, a few commands more and a screen
 * mode. The offset of each function in the original's text is in its
 * comment.
 *
 * What em does beyond ed (mutos/mutos_man/ed.1.d, appendix 1, and the
 * binary):
 *  - "Editor" at the start and ">" before every command read from the
 *    standard input (written at once, with write(1, ">", 1)) - neither with
 *    "-" or "-s", which are ed's "-"; "<" turns the prompt and the counts
 *    on, ">" off (both start the command loop over);
 *  - -e: only the file of the command line is edited ("no file" without
 *    one): no 'e', 'f', 'w' or a name to 'r' that is not given, 'q' writes
 *    the file and ends; no counts;
 *  - the session log: when the standard input is a terminal (and neither
 *    -e nor -l is given), every byte read is written to "<file>.elog" (the
 *    file's last component cut to 8 characters; "elog" without a file, a
 *    digit appended while the name exists), which begins "r <file>" and the
 *    current line; 'w' without a name and 'e' start a new log, the end of
 *    the session removes it, "q name" links it to name first; -r <log>
 *    replays a log (echoing it) before reading the terminal;
 *  - the standard input is read a byte at a time, or 128 bytes at a time
 *    from a terminal (a line); bytes of 0200 and above are taken as they
 *    are (8-bit text: a byte 0377 is the end of the input), 'l' shows them
 *    as \ooo;
 *  - 'q' and 'e' with changes not written ask "sure? " (y or n, read from
 *    the standard input) where ed refuses once; at the end of a standard
 *    input that is not a terminal em ends without asking, at the end of a
 *    terminal it goes on;
 *  - "s/../../n" prints the number of substitutions, 'z' is 's' asking for
 *    every match ('.' to substitute, newline not to - read from the
 *    standard error, the terminal), a pattern ended by the newline is the
 *    last one ("/" alone searches again), a search and a substitution that
 *    find nothing are "??";
 *  - '"' prints the screenful after the current line (.+1,.+lines-2), '&'
 *    the one before (.-lines+3,.), '%' the lines around it with rules of
 *    '_' around the current line - 20 lines of 80 columns without a
 *    terminal;
 *  - "|cmd" pipes the addressed lines (.,.) into the shell's command; "!cmd"
 *    runs the shell's command with "%" replaced by the file's name and "!!"
 *    for the last command (and echoes it then); both run the host's /bin/sh
 *    with the MUTOS root's bin/ first in the PATH;
 *  - 'h' lists /usr/lib/emhelp/commands (or "h<name>": /usr/lib/emhelp/<name>)
 *    of the MUTOS root to the standard error;
 *  - 'b+' / 'b-' set a "line break" flag (as does a file name that ends in
 *    ".n") that nothing in the original reads.
 *
 * What is the 8086's here, and kept (as in mutos_ed): 16-bit ints, the line
 * table malloc()ed in the 64K data segment at 0x3312 (measured), its 16-bit
 * pointers, the bss arrays one behind the other (the last shell command
 * 0840, savedfile 08a4, the log's name 08e4, file 0924, linebuf 0964,
 * rhsbuf 0b64, expbuf 0c64 - circfl at 0ce8), signed chars, error()'s seek
 * to the end of the standard input, the output a line at a time (to the
 * pipe of '|' while it runs).
 *
 * What the port refuses (mu_trap(): exit status 125, the last message ending
 * in "refused"), where the original depends on state the port does not
 * model:
 *  - 'o', the screen mode (ANSI terminal, function keys);
 *  - a write past circfl or genbuf through the arrays without a bound (a
 *    long file name, "!!" again and again, a long 'h' name) and the
 *    original's overruns of its stack (a "|" command of more than 79
 *    characters), a read past them (a NUL byte in a pattern);
 *  - a line table above 0xF000 in the segment;
 *  - where the original loops for ever: a 'z' answer or a "sure?" answer at
 *    the end of its input, an empty \(\) repeated by \1*, a read error of a
 *    help file; and a "sure?" whose first read gives nothing (the answer is
 *    the garbage of the stack);
 *  - the encryption (-x, 'x') as in mutos_ed.
 */
#define _POSIX_C_SOURCE 200809L	/* fileno(), sigaction(), setenv(), link() */

#define MU_TOOL "em"
#include "../h/mutos_stdio.h"
#include "../h/mutos_root.h"

#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <sys/ioctl.h>
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

/* the codes of a compiled pattern (<sys/ioctl.h> has a CEOF of its own) */
#undef CEOF
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

/* The line table: malloc(nlall * 2) at the start of the heap, behind the
 * bss (measured: 0x3312); it grows where it is, as in ed. */
#define EM_ZERO		0x3312
#define EM_SEGTOP	0xF000

static const char Q[] = "";
static const char T[] = "TMP";
static const char WRERR[] = "WRITE ERROR";

/* the bss of the original, in its order (0840 .. 0ce8) */
#define A_CMDBUF	0
#define CMDSIZE		100
#define A_SAVEDFILE	(A_CMDBUF + CMDSIZE)
#define A_LOGNAME	(A_SAVEDFILE + FNSIZE)
#define A_FILE		(A_LOGNAME + FNSIZE)
#define A_LINEBUF	(A_FILE + FNSIZE)
#define A_RHSBUF	(A_LINEBUF + LBSIZE)
#define A_EXPBUF	(A_RHSBUF + LBSIZE / 2)
#define AREA		(A_EXPBUF + ESIZE + 4)
static signed char area[AREA];
#define cmdbuf		(area + A_CMDBUF)
#define savedfile	(area + A_SAVEDFILE)
#define logname		(area + A_LOGNAME)
#define file		(area + A_FILE)
#define linebuf		(area + A_LINEBUF)
#define rhsbuf		(area + A_RHSBUF)
#define expbuf		(area + A_EXPBUF)

/* the data of em that ed has not (data 0026 .. 0038, 003a .. 00bc, 01ee ..) */
static int	noname = 1;	/* 0026: -e: filename() may take savedfile once */
static int	outfd = 1;	/* 0028: where putchr() writes */
static int	zflag;		/* 002a: 'z' */
static int	eflag;		/* 002c: -e */
static int	logflag = 1;	/* 002e: the session log */
static int	recov;		/* 0030: -r: the log is read */
static int	recfd = -1;	/* 0032 */
static int	logfd = -1;	/* 0034 */
static signed char inbuf[130];	/* 003a: what was read of the input */
static signed char *inp = inbuf; /* 00bc */
static signed char lines = 20;	/* 01ee: the terminal's lines */
static signed char cols = 80;	/* 01f0: and columns */
static int	lbreak;		/* 01f2: "line break" - read nowhere */
static int	fast;		/* 01f6: a terminal faster than 600 baud */

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

static uint16_t	ltab[(0x10000 - EM_ZERO) / 2];

static MU_NORETURN void error(const signed char *s);
static void commands(int arg);
static mptr address(void);
static signed char *getline_(uint16_t tl);
static int putline(void);
static signed char *getblock(uint16_t atl, int iof);
static void em_puts(const signed char *sp);
static void putchr(int ac);
static void putd(void);
static int getchr(void);
static void quit(void);
static int append(int (*f)(void), mptr a);
static void global(int k);
static void compile(int aeof);
static int execute(int gf, mptr addr);
static int advance(signed char *lp, signed char *ep);
static void makelog(void);

#define S(s)	((const signed char *)(s))

/* ---- the model of the segment -------------------------------------------- */

static mptr
LA(mptr p, int k)
{
	return (mptr)(p + 2 * k);
}

static mint
PD(mptr p, mptr q)
{
	int32_t d = (int32_t)p - (int32_t)q;

	return (mint)(d / 2);
}

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

static signed char
RD(const signed char *p)
{
	if (!in_area(p))
		mu_trap("a read past expbuf (a NUL byte in a pattern, or a run-on name)");
	return *p;
}

static void
WR(signed char *p, int c)
{
	if (!in_area(p))
		mu_trap("a write past expbuf, over the buffer's pointers");
	*p = (signed char)c;
}

/* a store into genbuf; past it (count, nextip, .. in the original): refused */
static void
WG(signed char *p, int c)
{
	uintptr_t a = (uintptr_t)p, b = (uintptr_t)genbuf;

	if (a < b || a >= b + LBSIZE)
		mu_trap("a write past genbuf");
	*p = (signed char)c;
}

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

static const char *
em_mpath(const char *mpath)
{
	static char buf[4096 + 1024];

	if (mutos_path(buf, sizeof(buf), mpath) == NULL)
		mu_trap("a file name longer than the host takes");
	return buf;
}

static void
em_signal(int sig, void (*h)(int), struct sigaction *old)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = h;
	sigemptyset(&sa.sa_mask);
	if (h != SIG_IGN && h != SIG_DFL)
		sa.sa_flags = SA_RESETHAND | SA_NODEFER;
	sigaction(sig, &sa, old);
}

/* mktemp() (libc.a text 6434): as ed's */
static const char *
em_mktemp(char *as)
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
	while (access(em_mpath(as), F_OK) != -1) {
		if (i == 'z')
			return "/";
		*s = (char)i++;
	}
	return as;
}

static const char *tfpath;

/* the PATH of the shell's commands: the MUTOS root's bin/ first */
static void
em_setpath(void)
{
	const char *old = getenv("PATH");
	const char *root = mutos_root();
	char *path;
	size_t pl;

	if (old == NULL)
		old = ":/bin:/usr/bin";
	pl = strlen(root) + strlen(old) + 8;
	if ((path = malloc(pl)) != NULL) {
		snprintf(path, pl, "%s/bin:%s", root, old);
		setenv("PATH", path, 1);
	}
}

/* isatty() (libc.a text 640e): gtty() succeeds */
static int
em_isatty(int fd)
{
	struct termios t;

	return tcgetattr(fd, &t) == 0;
}

/* ttymodes (text 3846): the terminal's modes; MUTOS's sgttyb has the
 * columns and the lines in its bytes 10 and 11 - here the host's window
 * size; a standard input that is not a terminal leaves 20 and 80 */
static void
ttymodes(void)
{
	struct termios t;
	struct winsize ws;

	if (tcgetattr(0, &t) != 0)
		return;
	if (ioctl(0, TIOCGWINSZ, &ws) == 0) {
		if ((signed char)ws.ws_col != 0)
			cols = (signed char)ws.ws_col;
		if ((signed char)ws.ws_row != 0)
			lines = (signed char)ws.ws_row;
	}
	fast = cfgetospeed(&t) > B600;
}

/* ---- the encryption (as in mutos_ed) ------------------------------------- */

static int
getkey(void)		/* text 2cc0 */
{
	struct termios b;
	struct sigaction sig;
	tcflag_t save;
	signed char *p;
	int c;

	em_signal(SIGINT, SIG_IGN, &sig);
	if (tcgetattr(0, &b) == -1)
		error(S("Input not tty"));
	save = b.c_lflag;
	b.c_lflag &= ~(tcflag_t)ECHO;
	tcsetattr(0, TCSANOW, &b);
	em_puts(S("Key:"));
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

static int
crinit(signed char *keyp)	/* text 2d6c */
{
	struct stat st;

	if (*keyp == 0)
		return 0;
	while (*keyp)
		*keyp++ = '\0';
	if (stat(em_mpath("/usr/lib/makekey"), &st) == 0 ||
	    stat(em_mpath("/lib/makekey"), &st) == 0)
		mu_trap("makekey, a MUTOS program, in the MUTOS root");
	error(S("crypt: cannot generate key"));
}

static MU_NORETURN void
makekey(void)		/* text 2ff4 */
{
	mu_trap("the encryption of the temporary file, keyed with the time");
}

/* ---- the input ----------------------------------------------------------- */

/* rdchr (text 1270): the next byte of fd's input - of the log while one is
 * replayed (echoed through stdio). A terminal is read 128 bytes at a time,
 * anything else a byte at a time; what is read is logged. The input's end
 * is EOF (and a byte 0377 is EOF too: the bytes come back signed). */
static int
rdchr(int fd)
{
	int eof = 0;
	ssize_t n;
	signed char c;

	if (*inp == 0) {
top:
		inp = inbuf;
		*inp = 0;
		n = read(recov ? recfd : fd, inbuf, em_isatty(0) ? 0x80 : 1);
		if (n <= 0) {
			*inp = (signed char)0377;
			eof++;
			n = 1;
		} else if (logflag) {
			ssize_t w = write(logfd, inp, (size_t)n);

			(void)w;
		}
		inp[n] = 0;
	}
	c = *inp++;
	if (eof) {
		if (recov) {
			/* the log is at its end: the input goes on - eof
			 * stays set, so that the first byte read now is
			 * lost and the caller gets EOF */
			recov = 0;
			close(recfd);
			goto top;
		}
		return EOF;
	}
	if (recov)
		mu_oputc((char)c);	/* putc(c, stdout) (text 5ef0) */
	return c;
}

static int
getchr(void)		/* text 11f4 */
{
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
	return lastc = rdchr(0);
}

/* ask (text 3074): msg, then the answer - read(0, buf, 128), the first
 * byte y or n - again and again */
static int
ask(const char *msg)
{
	signed char buf[0x80];
	int have = 0;
	ssize_t n, w;

	for (; *msg; msg++)
		w = write(outfd, msg, 1);
	(void)w;
	for (;;) {
		n = read(0, buf, 0x80);
		if (n > 0)
			have = 1;
		else if (!have)
			mu_trap("an answer to \"sure?\" at the end of the input (the stack's garbage)");
		else
			mu_trap("an answer to \"sure?\" at the end of the input (the original asks for ever)");
		if (buf[0] == 'n')
			return 0;
		if (buf[0] == 'y')
			return 1;
		em_puts(S("answer y or n - "));
	}
}

/* ---- the editor ---------------------------------------------------------- */

static void
init(void)		/* text 1b5c */
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
	if (logflag)
		makelog();
	close(creat(tfpath, 0600));
	tfile = open(tfpath, O_RDWR);
	if (xflag)
		makekey();
	dot = dol = zero;
}

static void
onintr(int sig)		/* text 10aa */
{
	(void)sig;
	em_signal(SIGINT, onintr, NULL);
	putchr('\n');
	lastc = '\n';
	error(S(Q));
}

static void putfile(void);

static void
onhup(int sig)		/* text 10da */
{
	(void)sig;
	em_signal(SIGINT, SIG_IGN, NULL);
	em_signal(SIGHUP, SIG_IGN, NULL);
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
onterm(int sig)
{
	(void)sig;
	quit();
}

static void
error(const signed char *s)	/* text 1136 */
{
	int c;

	wrapp = 0;
	listf = 0;
	putchr('?');
	em_puts(s);
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

static void
setdot(void)		/* text 0e6a */
{
	if (addr2 == 0)
		addr1 = addr2 = dot;
	if (addr1 > addr2)
		error(S(Q));
}

static void
setall(void)		/* text 0e9a */
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
setnoaddr(void)		/* text 0ece */
{
	if (addr2)
		error(S(Q));
}

static void
nonzero(void)		/* text 0ee8 */
{
	if (addr1 <= zero || addr2 > dol)
		error(S(Q));
}

/* range (text 0e34): addr1, addr2 = a, b, kept inside the buffer */
static void
range(mptr a, mptr b)
{
	addr1 = a;
	addr2 = b;
	if (addr1 <= zero)
		addr1 = LA(zero, 1);
	if (addr2 > dol)
		addr2 = dol;
}

static void
newline(void)		/* text 0f0e */
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

/* dotn (text 104e): -e, or a name that ends in ".n": the line break on */
static void
dotn(const signed char *p)
{
	if (eflag || (RD(p) == '.' && RD(p + 1) == 'n'))
		lbreak = 1;
}

/* filename (text 0f4a): ed's, but under -e the saved name only once (the
 * 'r' of the start, or 'q'), and 1 for a name given */
static int
filename(int comm)
{
	signed char *p1, *p2;
	int c;

	count = 0;
	c = getchr();
	if (c == '\n' || c == EOF) {
		if (eflag && noname == 0)
			error(S(Q));
		noname = 0;
		p1 = savedfile;
		if (*p1 == 0 && comm != 'f')
			error(S(Q));
		p2 = file;
		do {
			c = RD(p1++);
			WR(p2++, c);
		} while (c);
		return 0;
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
		dotn(p1 - 3);
	}
	return 1;
}

static void
exfile(void)		/* text 1076 */
{
	close(io);
	io = -1;
	if (vflag && !eflag) {
		putd();
		putchr('\n');
	}
}

static int
gettty(void)		/* text 134e: ed's, the byte not masked */
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
		if (c == 0)
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
getfile(void)		/* text 13c0: ed's, bytes of 0200 and above taken */
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
			/* crblock() if kflag - never, as in mutos_ed */
			fp = genbuf;
		}
		c = *fp++;
		if (c == '\0')
			continue;
		if (lp >= &linebuf[LBSIZE]) {
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
putfile(void)		/* text 1490 */
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
					em_puts(S(WRERR));
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
		em_puts(S(WRERR));
		error(S(Q));
	}
}

static int
append(int (*f)(void), mptr a)	/* text 15aa */
{
	mptr a1, a2, rdot;
	int nline;
	uint16_t tl;

	nline = 0;
	dot = a;
	while ((*f)() == 0) {
		if ((uint16_t)(PD(dol, zero) + 1) >= nlall) {
			nlall = (uint16_t)(nlall + 512);
			if ((uint32_t)zero + 2u * nlall > EM_SEGTOP)
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

/* callunix (text 169e): the rest of the line is the command, "%" the
 * file's name, "!" at its start the last command (both: echoed); the
 * host's sh -c runs it. The command's buffer (0840, 100 bytes) has no
 * bound: it runs on into savedfile, the log's name and file. */
static void
callunix(void)
{
	struct sigaction savint;
	pid_t pid, rpid;
	int retcode, echo = 0;
	signed char *p = cmdbuf;
	signed char c;

	c = (signed char)getchr();
	if (c == '!') {
		echo = 1;
		while (RD(p++))
			;
		p--;
		do {
			c = (signed char)getchr();
			WR(p++, c);
		} while (c != '\n');
		WR(p - 1, 0);
	} else {
		for (;;) {
			if (c == '%') {
				const signed char *s = savedfile;

				echo = 1;
				do
					WR(p, RD(s++));
				while (*p++);
				p--;
			} else
				WR(p++, c);
			c = (signed char)getchr();
			if (c == '\n')
				break;
		}
		WR(p, 0);
	}
	if (echo) {
		putchr('!');
		em_puts(cmdbuf);
	}
	setnoaddr();
	if ((pid = fork()) == 0) {
		sigaction(SIGHUP, &oldhup, NULL);
		sigaction(SIGQUIT, &oldquit, NULL);
		em_setpath();
		execl("/bin/sh", "sh", "-c", hname(cmdbuf), (char *)NULL);
		_exit(0100);
	}
	em_signal(SIGINT, SIG_IGN, &savint);
	while ((rpid = wait(&retcode)) != pid && rpid != -1)
		;
	sigaction(SIGINT, &savint, NULL);
	em_puts(S("!"));
}

/* quit (text 17d2): no question here - 'q' and 'e' ask */
static void
quit(void)
{
	unlink(tfpath);
	if (logflag)
		unlink(hname(logname));
	mu_exit(0);
}

static void
rdelete(mptr ad1, mptr ad2)	/* text 181e */
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
delete(void)		/* text 17fe */
{
	setdot();
	newline();
	nonzero();
	rdelete(addr1, addr2);
}

static void
gdelete(void)		/* text 1874 */
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
getline_(uint16_t tl)	/* getline, text 18d6 */
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
putline(void)		/* text 192e */
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
blkio(int b, signed char *buf, int wr)	/* text 1b14 */
{
	ssize_t n;

	lseek(tfile, (off_t)b << 9, SEEK_SET);
	n = wr ? write(tfile, buf, 512) : read(tfile, buf, 512);
	if (n != 512)
		error(S(T));
}

static signed char *
getblock(uint16_t atl, int iof)	/* text 19bc */
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

/* makelog (text 1bfc): a new session log - the old one removed; the name
 * is savedfile's, its last component cut to 8 characters, ".elog" behind
 * it ("elog" for no name), then a digit as long as the name exists; it
 * begins "r <savedfile>" and the number of the current line (if not 0) */
static void
makelog(void)
{
	signed char *di = savedfile;
	signed char *p = logname, *si;
	const signed char *e;
	signed char c;
	struct stat st;
	int save;

	if (RD(logname) != 0)
		unlink(hname(logname));
	if (logfd > 0)
		close(logfd);
	si = p;
	for (;;) {
		c = RD(di++);
		WR(p, c);
		if (c == 0)
			break;
		if (*p++ == '/')
			si = p - 1;
	}
	e = S(".elog");
	if (p - si > 8)
		p = si + 8;
	if (p == logname)
		e++;
	do
		WR(p, *e);
	while (*p++ && *e++);
	p--;
	c = '1';
	while (stat(hname(logname), &st) >= 0)
		WR(p, c++);
	WR(p + 1, 0);
	logfd = creat(hname(logname), 0666);
	if (RD(savedfile) != 0) {
		save = outfd;
		outfd = logfd;
		putchr('r');
		putchr(' ');
		em_puts(savedfile);
		count = PD(dot, zero) & 077777;
		if (count) {
			putd();
			putchr('\n');
		}
		outfd = save;
	}
}

static void
global(int k)		/* text 1d28 */
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
	if (globuf[0] == 'd' && globuf[1] == '\n' && globuf[2] == '\0') {
		gdelete();
		return;
	}
	for (a1 = zero; a1 <= dol; a1 = LA(a1, 1)) {
		if (*LW(a1) & 01) {
			*LW(a1) &= (uint16_t)~01;
			dot = a1;
			globp = globuf;
			commands(-1);
			a1 = zero;
		}
	}
}

static void
join(void)		/* text 1e60 */
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
getsub(void)		/* text 2258 */
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
place(signed char *sp, signed char *l1, signed char *l2)	/* text 2376 */
{
	while (l1 < l2) {
		*sp++ = RD(l1++);
		if (sp >= &genbuf[LBSIZE])
			error(S(Q));
	}
	return sp;
}

/* dosub (text 2288): ed's; cnt counts the substitution */
static void
dosub(int cnt)
{
	signed char *lp, *sp, *rp;
	int c;

	if (cnt)
		count++;
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

/* nosub (text 202e): the match replaced by itself ("&") - past it */
static void
nosub(void)
{
	signed char s0 = rhsbuf[0], s1 = rhsbuf[1];

	rhsbuf[0] = '&';
	rhsbuf[1] = 0;
	dosub(0);
	rhsbuf[0] = s0;
	rhsbuf[1] = s1;
}

/* rawputs (text 4d16): s at once to the standard output, the control
 * characters but ESC, ^U, ^X and ^Y as blanks (and DEL); no newline */
static void
rawputs(const signed char *s)
{
	char buf[0x204];
	size_t n = 0;
	signed char c;
	ssize_t w;

	while ((c = in_area(s) ? RD(s) : *s) != 0) {
		if (n >= sizeof(buf))
			mu_trap("a line past rawputs()'s stack buffer");
		if ((c > 0 && c < 0x20 && c != 0x1b && c != 0x15 && c != 0x18 &&
		    c != 0x19) || c == 0x7f)
			buf[n] = ' ';
		else
			buf[n] = (char)c;
		n++;
		s++;
	}
	w = write(1, buf, n);
	(void)w;
}

/* mark (text 20e6): the line with the match shown - '^' under it on a
 * fast terminal (the line was printed before), else '"' around it */
static void
mark(signed char *l, signed char *l1, signed char *l2, int ch)
{
	signed char *p = genbuf;
	signed char *s;

	if (ch != '"') {
		do {
			s = l;
			WG(p++, RD(l) == '\t' ? RD(l) : ' ');
			l++;
		} while (s < l1);
		p--;
		l--;
		while (l < l2) {
			WG(p++, ch);
			if (RD(l++) == '\t')
				WG(p++, '\t');
		}
		WG(p, 0);
	} else {
		do {
			if (l == l1 || l == l2)
				WG(p++, '"');
			WG(p++, RD(l++));
		} while (p[-1] != 0);
	}
	rawputs(genbuf);
}

/* confirm (text 205c): 'z' - the match shown, the answer read from the
 * standard error: '.' substitutes, a newline does not */
static int
confirm(void)
{
	signed char c = '.';
	int d;

	if (zflag) {
		do {
			if (fast)
				em_puts(linebuf);
			mark(linebuf, loc1, loc2, fast ? '^' : '"');
			c = (signed char)rdchr(2);
			if (c != '\n') {
				while ((d = rdchr(2)) != '\n')
					if (d == EOF)
						mu_trap("a 'z' answer at the end of the input (the original reads for ever)");
				if (c != '.')
					em_puts(S("? '.' to confirm"));
			}
		} while (c != '\n' && c != '.');
	}
	return c == '.';
}

/* compsub (text 2198): ed's; the flags 'g' (1) and 'n' (2) */
static int
compsub(void)
{
	int seof, c, f = 0;
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
		f |= 1;
	}
	if ((peekc = getchr()) == 'n') {
		peekc = 0;
		f |= 2;
	}
	newline();
	return f;
}

/* substitute (text 1eec): ed's; 'z' asks for every match, 'n' prints the
 * number of substitutions, no match is "??" */
static void
substitute(int inglob)
{
	int i, gsubf, printcount;
	mint nl;
	mptr a1;

	gsubf = compsub();
	printcount = gsubf > 1;
	count = 0;
	gsubf &= 1;
	gsubf |= zflag;
	for (a1 = addr1; a1 <= addr2; a1 = LA(a1, 1)) {
		if (execute(0, a1) == 0)
			continue;
		inglob |= 01;
		if (confirm())
			dosub(1);
		else
			nosub();
		if (gsubf) {
			while (RD(loc2)) {
				if (execute(1, 0) == 0)
					break;
				if (confirm())
					dosub(1);
				else
					nosub();
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
		nl = (mint)append(getsub, a1);
		a1 = LA(a1, nl);
		addr2 = LA(addr2, nl);
	}
	if (inglob == 0)
		error(S("?"));
	if (printcount) {
		putd();
		putchr('\n');
	}
}

static void
reverse(mptr a1, mptr a2)	/* text 24be */
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
getcopy(void)		/* text 24ea */
{
	if (addr1 > addr2)
		return EOF;
	getline_(*LW(addr1));
	addr1 = LA(addr1, 1);
	return 0;
}

static void
move(int cflag)		/* text 23a4: the MUTOS ed's */
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
		ad2 = dol;
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

/* compile (text 2516): ed's; a pattern ended by the newline is the last
 * one, and the newline is read again */
static void
compile(int aeof)
{
	int eof, c;
	signed char *ep;
	signed char *lastep;
	signed char bracket[NBRA], *bracketp;
	int cclcnt;

	ep = expbuf;
	eof = aeof;
	bracketp = bracket;
	if ((c = getchr()) == eof || c == '\n') {
		if (*ep == 0)
			error(S(Q));
		if (c == '\n')
			peekc = c;
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
execute(int gf, mptr addr)	/* text 27a0 */
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
	do {
		if (advance(p1, p2)) {
			loc1 = p1;
			return 1;
		}
	} while (RD(p1++));
	return 0;
}

static int
backref(int i, signed char *lp)	/* text 2a6e */
{
	signed char *bp;

	bp = braslist[i];
	while (RD(bp++) == RD(lp++))
		if (bp >= braelist[i])
			return 1;
	return 0;
}

static int
cclass(signed char *set, signed char c, int af)	/* text 2aae */
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

static int
bra(signed char i)
{
	if (i < 0 || i >= NBRA)
		mu_trap("a damaged pattern (a bracket number past braslist)");
	return i;
}

static int
advance(signed char *lp, signed char *ep)	/* text 2882 */
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
putd(void)		/* text 2ae4 */
{
	int r;

	r = (int)(count % 10);
	count /= 10;
	if (count)
		putd();
	putchr(r + '0');
}

static void
em_puts(const signed char *sp)	/* puts, text 2b34 */
{
	int ar = in_area(sp);

	col = 0;
	while (ar ? RD(sp) : *sp)
		putchr(*sp++);
	putchr('\n');
}

/* putchr (text 2b62): ed's to outfd; 'l' shows the bytes below 040 (but
 * the newline) and those of 0200 and above as \ooo */
static void
putchr(int ac)
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
			goto out1;
		}
		if ((c >= ' ' || c == '\n') && c <= 0177)
			goto out1;
		*lp++ = '\\';
		*lp++ = (signed char)(((c & 0300) >> 6) + '0');
		*lp++ = (signed char)(((c & 070) >> 3) + '0');
		*lp++ = (signed char)((c & 07) + '0');
		col += 3;
		goto out;
	}
out1:
	*lp++ = (signed char)c;
out:
	if (c == '\n' || lp >= &line[64]) {
		ssize_t w;

		linp = line;
		w = write(outfd, line, (size_t)(lp - line));
		(void)w;
		return;
	}
	linp = lp;
}

/* rule (text 2170): a line of cols-1 '_' */
static void
rule(void)
{
	int i = cols;

	while ((i = (mint)(i - 1)) != 0)
		putchr('_');
	putchr('\n');
}

/* help (text 51f8): the file to the standard error, or "<name> not found" */
static void
help(const signed char *name)
{
	int fd;
	ssize_t n, w;
	char buf[0x80];

	fd = open(em_mpath((const char *)name), O_RDONLY);
	if (fd < 0) {
		rawputs(name);
		em_puts(S(" not found"));
		return;
	}
	while ((n = read(fd, buf, sizeof(buf))) != 0) {
		if (n < 0)
			mu_trap("a help file that cannot be read (the original writes -1 bytes for ever)");
		w = write(2, buf, (size_t)n);
		(void)w;
	}
	close(fd);
}

static mptr
address(void)		/* text 0c58: ed's; a search that fails is "??" */
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
					error(S("?"));
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

/* the lines addr1 .. addr2 printed */
static void
printlines(void)
{
	mptr a1;

	for (a1 = addr1; a1 <= addr2; a1 = LA(a1, 1))
		em_puts(getline_(*LW(a1)));
}

/* "|cmd" (commands, text 08fd): the lines into the shell command's pipe */
static void
pipecmd(void)
{
	signed char buf[80];	/* [bp-5c] .. the pointer at [bp-0c] */
	int pf[2], c, n = 0;
	pid_t pid;
	int save;

	setdot();
	nonzero();
	while ((c = getchr()) == ' ' || c == '\t')
		;
	while (c != '\n') {
		if (n >= (int)sizeof(buf))
			mu_trap("a '|' command past its stack buffer");
		buf[n++] = (signed char)c;
		c = getchr();
	}
	if (n >= (int)sizeof(buf))
		mu_trap("a '|' command past its stack buffer");
	buf[n] = 0;
	if (pipe(pf) < 0)
		error(S("Pipe"));
	if ((pid = fork()) < 0)
		error(S("Fork"));
	if (pid == 0) {
		close(0);
		if (dup(pf[0]) < 0)
			_exit(0377);
		close(pf[0]);
		close(pf[1]);
		em_setpath();
		execl("/bin/sh", "sh", "-c", (char *)buf, (char *)NULL);
		_exit(0377);	/* the original's child goes on to error() */
	}
	em_signal(SIGPIPE, SIG_IGN, NULL);
	em_signal(SIGINT, SIG_IGN, NULL);
	em_signal(SIGHUP, SIG_IGN, NULL);
	close(pf[0]);
	save = outfd;
	outfd = pf[1];
	printlines();
	close(pf[1]);
	wait(NULL);
	outfd = save;
	em_signal(SIGINT, onintr, NULL);
	em_signal(SIGHUP, onhup, NULL);
	dot = addr2;
}

/* "h" (commands, text 046c): /usr/lib/emhelp/ and the name ("commands") */
static void
helpcmd(void)
{
	static const char dir[] = "/usr/lib/emhelp/";
	const char *s;
	signed char *p = genbuf;
	int c;

	while ((c = getchr()) == ' ')
		;
	for (s = dir; ; s++) {
		WG(p++, *s);
		if (*s == 0)
			break;
	}
	p--;
	if (c == '\n') {
		for (s = "commands"; ; s++) {
			WG(p++, *s);
			if (*s == 0)
				break;
		}
	} else {
		do {
			WG(p++, c);
			c = getchr();
		} while (c != '\n');
		WG(p, 0);
	}
	help(genbuf);
}

/* commands (text 02d0): arg is vflag from main() - the prompt when it is
 * positive -, -1 from global() */
static void
commands(int arg)
{
	mptr a1;
	int c;

	for (;;) {
	if (pflag) {
		pflag = 0;
		addr1 = addr2 = dot;
		goto print;
	}
	if (arg > 0 && globp == 0) {
		ssize_t w = write(1, ">", 1);	/* text 4c88 */

		(void)w;
	}
	addr1 = 0;
	addr2 = 0;
	zflag = 0;
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

	case 'b':
		c = getchr();
		peekc = c;
		if (c == '+' || c == '-')
			peekc = 0;
		else if (c != '\n')
			error(S(Q));
		lbreak = c != '-';
		newline();
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
		if (eflag)
			error(S(Q));
		if (fchange && vflag) {
			if (ask("sure? ") == 0) {
				*inp = 0;
				continue;
			}
		}
		setnoaddr();
		filename(c);
		init();
		fchange = 0;
		addr2 = zero;
		goto caseread;

	case 'f':
		if (eflag)
			error(S(Q));
		setnoaddr();
		filename(c);
		em_puts(savedfile);
		continue;

	case 'g':
		global(1);
		continue;

	case 'h':
		helpcmd();
		continue;

	case 'i':
		setdot();
		nonzero();
		newline();
		append(gettty, LA(addr2, -1));
		continue;

	case 'j':
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
			em_puts(getline_(tl));
		} while (a1 <= addr2);
		dot = addr2;
		listf = 0;
		continue;

	case 'Q':
		fchange = 0;
		/* FALLTHROUGH */
	case 'q':
		if (logflag) {
			peekc = getchr();
			if (peekc != '\n') {
				filename(c);
				if (link(hname(logname), hname(file)) < 0)
					error(logname);
				goto qdone;
			}
		}
		newline();
	qdone:
		if (eflag) {
			noname = 1;
			goto casewrite;
		}
		setnoaddr();
		if (fchange && vflag) {
			if (ask("sure? ") == 0) {
				*inp = 0;
				continue;
			}
		}
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

	case 'z':
		zflag = 1;
		/* FALLTHROUGH */
	case 's':
		setdot();
		nonzero();
		substitute(globp != 0);
		zflag = 0;
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
		if (eflag)
			error(S(Q));
	casewrite:
		setall();
		nonzero();
		if (eflag) {
			signed char *p1 = savedfile, *p2 = file;

			if (*p1 == 0)
				error(S(Q));
			do {
				c = RD(p1++);
				WR(p2++, c);
			} while (c);
		} else if (filename(c) == 0 && logflag)
			makelog();
		if (!wrapp ||
		    ((io = open(hname(file), O_WRONLY)) == -1) ||
		    ((lseek(io, (off_t)0, SEEK_END)) == -1))
			if ((io = creat(hname(file), 0666)) < 0)
				error(file);
		wrapp = 0;
		putfile();
		exfile();
		if (eflag)
			quit();
		if (addr1 == LA(zero, 1) && addr2 == dol)
			fchange = 0;
		continue;

	case 'x':
		setnoaddr();
		newline();
		xflag = 1;
		em_puts(S("Entering encrypting mode!"));
		getkey();
		kflag = crinit(key);
		continue;

	case '"':		/* .+1,.+lines-2 */
		setdot();
		newline();
		nonzero();
		dot = addr1;
		if (dot == dol)
			error(S(Q));
		range(LA(dot, 1), LA(dot, lines - 2));
		printlines();
		dot = addr2;
		continue;

	case '&':		/* .-lines+3,. */
		setdot();
		newline();
		nonzero();
		dot = addr1;
		range(LA(dot, -(lines - 3)), dot);
		printlines();
		dot = addr2;
		continue;

	case '%':		/* .-lines/2+3,.+lines/2-3, the current line ruled */
		newline();
		setdot();
		nonzero();
		dot = addr1;
		range(LA(dot, -(lines / 2 - 3)), LA(dot, lines / 2 - 3));
		for (a1 = addr1; a1 <= addr2; a1 = LA(a1, 1)) {
			if (a1 == dot)
				rule();
			em_puts(getline_(*LW(a1)));
			if (a1 == dot)
				rule();
		}
		continue;

	case '>':
		vflag = 0;
		longjmp(savej, 1);

	case '<':
		vflag = 1;
		longjmp(savej, 2);

	case '|':
		pipecmd();
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
		ttymodes();
		continue;

	case 'o':
		mu_trap("'o', the screen mode");

	case EOF:
		if (arg == -1)
			return;
		if (!em_isatty(0))
			return;
		continue;

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

	em_signal(SIGQUIT, SIG_IGN, &oldquit);
	em_signal(SIGHUP, SIG_IGN, &oldhup);
	em_signal(SIGINT, SIG_IGN, &oldintr);
	em_signal(SIGTERM, SIG_IGN, &oldterm);
	if (oldterm.sa_handler == SIG_DFL)
		em_signal(SIGTERM, onterm, NULL);
	argv++;
	ttymodes();
	while (argc > 1 && **argv == '-') {
		switch ((*argv)[1]) {

		case '\0':
		case 's':
			vflag = 0;
			break;

		case 'e':
			eflag = 1;
			break;

		case 'l':
			logflag = 0;
			break;

		case 'q':
			em_signal(SIGQUIT, SIG_DFL, NULL);
			break;

		case 'r':
			if (argc != 3 || (recfd = open(argv[1], O_RDONLY)) < 0) {
				em_puts(S("no recovery file"));
				mu_exit(-1);
			}
			recov = 1;
			break;

		case 'x':
			xflag = 1;
			break;
		}
		argv++;
		argc--;
	}
	if (!em_isatty(0) || eflag || !logflag)
		logflag = 0;
	if (xflag) {
		getkey();
		kflag = crinit(key);
	}
	if (argc > 1 && !recov) {
		int c;

		p1 = *argv;
		p2 = savedfile;
		do {
			c = (signed char)*p1++;
			WR(p2++, c);
		} while (c);
		globp = S("r");
		dotn(p2 - 3);
	} else if (eflag) {
		em_puts(S("no file"));
		mu_exit(-1);
	}
	zero = EM_ZERO;
	if (vflag && !eflag)
		em_puts(S("Editor"));
	tfpath = strdup(em_mpath(em_mktemp(tfname)));	/* the buffer is reused */
	if (tfpath == NULL)
		mu_trap("no memory for the temporary file's name");
	init();
	if (oldintr.sa_handler != SIG_IGN)
		em_signal(SIGINT, onintr, NULL);
	if (oldhup.sa_handler != SIG_IGN)
		em_signal(SIGHUP, onhup, NULL);
	setjmp(savej);
	savej_set = 1;
	commands(vflag);
	quit();
	return 0;
}
