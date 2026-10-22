/*
 * ldlib.c - the machine and the library around the original ld: the
 * memory image (ld.h) and its initial state, crt0's argument words, the
 * system calls ld makes (open, creat, read, write, lseek, close, unlink,
 * link, chmod, umask, fstat, access, getpid), libc.a's printf (to stdout,
 * buffered as stdio buffers it), atoi(), mktemp(), strncmp(), exit(),
 * the signals, and the refusals (ldtrap()).
 */
#define _POSIX_C_SOURCE 200809L	/* sigaction(), lstat() under -std=c11 */

#include "ld.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../h/mutos_root.h"

uint16_t ldw[32768];
uint8_t	ldattr[65536];
int	ld_bswap;

/* the argument strings: crt0 finds them at the top of the data segment */
#define	F_ARGTOP	0xfff0
#define	LD_ARGMIN	0xa000	/* they must leave the original room for a stack */

/* ---- the image ----------------------------------------------------- */

static void
attr(uint16_t lo, uint32_t hi, uint8_t a)
{
	uint32_t i;

	for (i = lo; i < hi; i++)
		ldattr[i] = a;
}

/*
 * The image at the start: the binary's initialized data at address 0, the
 * BSS zero, then crt0's work - the argument strings at the top of the data
 * segment and below them the words main() is called with (argc, argv[],
 * 0, an empty environment, 0), laid out as tests/realbin/mutosemu.py lays
 * them out (argv[0] is "ld"); crt0 stores the code segment into the far
 * pointers' segment words 4 and 8 - under the emulator 0x1000, on the
 * hardware wherever the text was loaded; ld reads word 8 through a null
 * pointer in ldrand(), where any value but 0x0020 has the same effect.
 * What the image models (ld.h): crt0's words (read only), ld.o's data and
 * its BSS, libc.a's string "(null)", the argument strings and words. Not
 * modelled: libc.a's other data and BSS, crt0's environ, the stack.
 */
void
ld_init(int argc, char **argv, uint16_t *mutos_argv)
{
	uint16_t one = 1, top = F_ARGTOP, words, pad;
	uint16_t *ptrs;
	int i;

	ld_bswap = *(unsigned char *)&one == 0;
	memset(ldw, 0, sizeof(ldw));
	memset(ldattr, A_NONE, sizeof(ldattr));
	for (i = 0; i < LD_DSIZE; i++)
		((uint8_t *)ldw)[i ^ ld_bswap] = ld_data[i];
	ldw[4 >> 1] = 0x1000;
	ldw[8 >> 1] = 0x1000;
	attr(0x0000, 0x000a, A_RO);		/* crt0's words */
	attr(0x000a, LD_DATAEND, A_MEM);	/* ld.o's data, "(null)" */
	attr(LD_BSS + 2, LD_BSSEND, A_MEM);	/* ld.o's BSS */

	if (argc < 1 || argc > 4000)
		ldtrap("an argument count the image cannot hold");
	ptrs = malloc(sizeof(*ptrs) * (size_t)argc);
	if (ptrs == NULL)
		ldtrap("no memory for the argument list");
	for (i = 0; i < argc; i++) {
		const char *a = i == 0 ? "ld" : argv[i];
		size_t n = strlen(a) + 1;
		size_t k;

		if ((size_t)(top - LD_ARGMIN) < n + 2 * (size_t)(argc + 3))
			ldtrap("arguments longer than the image's argument area");
		top = (uint16_t)(top - n);
		for (k = 0; k < n; k++) {
			ldattr[top + k] = A_MEM;
			wb((uint16_t)(top + k), a[k]);
		}
		ptrs[i] = top;
	}
	pad = top;
	top &= (uint16_t)~1;
	if (pad != top) {			/* the byte between: 0 */
		ldattr[top] = A_MEM;
		wb(top, 0);
	}
	words = (uint16_t)(top - 2 * (argc + 3));
	attr(words, top, A_MEM);
	WU(words) = (uint16_t)argc;
	for (i = 0; i < argc; i++)
		WU(words + 2 + 2 * i) = ptrs[i];
	WU(words + 2 + 2 * argc) = 0;		/* argv's end */
	WU(words + 4 + 2 * argc) = 0;		/* no environment */
	free(ptrs);
	*mutos_argv = (uint16_t)(words + 2);
}

/* ---- stdout: libc.a's stdio, as far as printf uses it ------------------ */

/*
 * printf writes to stdout, which libc.a buffers in blocks of BUFSIZ (512):
 * _flsbuf() writes the buffer when the 513th byte comes, exit() the rest.
 * A refusal writes nothing more: what the original would have written by
 * then is the blocks it had flushed.
 */
#define	BUFSIZ_	512
static	uint8_t	out_buf[BUFSIZ_];
static	int	out_n;

static void
host_write(int fd, const uint8_t *b, size_t n)
{
	while (n > 0) {
		ssize_t w = write(fd, b, n);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return;		/* the original ignores write errors */
		}
		b += w;
		n -= (size_t)w;
	}
}

static void
out_putc(int c)
{
	if (out_n == BUFSIZ_) {
		host_write(1, out_buf, BUFSIZ_);
		out_n = 0;
	}
	out_buf[out_n++] = (uint8_t)c;
}

/* ---- libc.a's printf ------------------------------------------------- */

/*
 * libc.a's _doprnt() (text 2112) for what a format of ld's can hold: text,
 * and '%' with an optional '-', an optional '0' fill, a width and a
 * precision (digits, or '*': the next argument), then d, o, x, u (a word,
 * no precision), c (a char; 0 writes nothing) or s (a string in the image;
 * a null pointer is "(null)"; the precision is its most characters). Any
 * other character after the '%' is written itself, as _doprnt() does -
 * also the format's closing 0, after which it goes on reading. Its long
 * and floating conversions (D, O, X, U, l, L, e, f, g) and %r are
 * refused. ld's own formats use %s and %.Ns; others come only from a -l
 * name written over them (getfile()).
 */
static int
fmt_num(uint16_t *fmt, const uint16_t *args, int nargs, int *k)
{
	int n = 0;

	if (rb(*fmt) == '*') {
		(*fmt)++;
		if (*k >= nargs)
			ldtrap("a printf argument the original does not pass");
		return (int16_t)args[(*k)++];
	}
	while (rb(*fmt) >= '0' && rb(*fmt) <= '9') {
		n = n * 10 + rb(*fmt) - '0';
		if (n > 0x7fff)
			ldtrap("a printf width the port does not model");
		(*fmt)++;
	}
	return n;
}

void
ld_printf(uint16_t fmt, const uint16_t *args, int nargs)
{
	int k = 0;

	for (;;) {
		char buf[40];
		const char *s = NULL;
		int len = 0, ljust = 0, width, prec, i;
		int fill = ' ';
		int16_t c = rb(fmt++);
		uint16_t str = 0;

		if (c == 0)
			return;
		if (c != '%') {
			out_putc(c);
			continue;
		}
		if (rb(fmt) == '-') {
			ljust = 1;
			fmt++;
		}
		if (rb(fmt) == '0') {
			fill = '0';
			fmt++;
		}
		width = fmt_num(&fmt, args, nargs, &k);
		prec = 0;
		if (rb(fmt) == '.') {
			fmt++;
			prec = fmt_num(&fmt, args, nargs, &k);
		}
		if (width < 0)
			ldtrap("a negative printf width (not modelled)");
		c = rb(fmt++);
		switch (c) {
		case 'd': case 'o': case 'x': case 'u':
			if (prec != 0)
				ldtrap("a printf precision of a number (not "
				    "modelled)");
			/* FALLTHROUGH */
		case 'c': case 's':
			if (k >= nargs)
				ldtrap("a printf argument the original does not pass");
			break;
		case 'D': case 'O': case 'X': case 'U': case 'l': case 'L':
		case 'e': case 'f': case 'g': case 'r':
			ldtrap("a printf conversion the port's printf does not have");
		default:
			buf[0] = (char)c;
			s = buf;
			len = 1;
			break;
		}
		switch (c) {
		case 'd':
			len = snprintf(buf, sizeof(buf), "%d", (int16_t)args[k++]);
			s = buf;
			break;
		case 'u':
			len = snprintf(buf, sizeof(buf), "%u", (unsigned)args[k++]);
			s = buf;
			break;
		case 'o':
			len = snprintf(buf, sizeof(buf), "%o", (unsigned)args[k++]);
			s = buf;
			break;
		case 'x':
			len = snprintf(buf, sizeof(buf), "%x", (unsigned)args[k++]);
			s = buf;
			break;
		case 'c':
			buf[0] = (char)(args[k++] & 0377);
			s = buf;
			len = buf[0] != 0;
			fill = ' ';
			break;
		case 's':
			fill = ' ';
			str = args[k++];
			if (str == 0)
				str = S_NULL;
			/* the precision counts down from 0 too: no limit */
			for (len = 0; rb((uint16_t)(str + len)) != 0; )
				if (++len == prec)
					break;
			break;
		}
		if (!ljust) {
			/* _strout(): a '-' goes before the zeros */
			if (fill == '0' && s != NULL && len > 0 && s[0] == '-'
			    && len < width) {
				out_putc('-');
				s++;
				len--;
				width--;
			}
			for (i = len; i < width; i++)
				out_putc(fill);
		}
		for (i = 0; i < len; i++)
			out_putc(s != NULL ? (uint8_t)s[i] : UB((uint16_t)(str + i)));
		if (ljust)
			for (i = len; i < width; i++)
				out_putc(fill);
	}
}

/* ---- atoi(), mktemp(), strncmp() --------------------------------------- */

/* libc.a's atoi (text 29b8): blanks and tabs, one sign, digits */
int16_t
ld_atoi(uint16_t s)
{
	int16_t n = 0, neg = 0;

	while (rb(s) == ' ' || rb(s) == '\t')
		s++;
	if (rb(s) == '+')
		s++;
	else if (rb(s) == '-') {
		neg++;
		s++;
	}
	while (rb(s) >= '0' && rb(s) <= '9') {
		n = (int16_t)((uint16_t)(n * 10) + (uint16_t)rb(s) - '0');
		s++;
	}
	return neg ? (int16_t)-n : n;
}

static uint16_t
ld_getpid(void)
{
	uint16_t pid = (uint16_t)(getpid() & 0x7fff);

	return pid ? pid : 1;
}

static int	host_path(uint16_t name, char *buf, size_t n);

/*
 * libc.a's mktemp (text 2cb2): the trailing X's become the process
 * number's digits, then the first of them 'a', 'b', ... as long as the
 * name exists (access()); "/" when even 'y' exists.
 */
uint16_t
ld_mktemp(uint16_t as)
{
	uint16_t s = as, pid = ld_getpid();
	int16_t i;
	char path[4096 + 1024];

	while (rb(s++) != 0)
		;
	s--;
	while (rb(--s) == 'X') {
		wb(s, (int)(pid % 10) + '0');
		pid /= 10;
	}
	s++;
	for (i = 'a';; ) {
		if (!host_path(as, path, sizeof(path)) || access(path, F_OK) != 0)
			return as;
		if (i == 'z')
			return 0x038e;		/* libc.a's "/" */
		wb(s, i++);
	}
}

/* libc.a's strncmp, for equality */
int16_t
cstrncmp(uint16_t a, uint16_t b, int16_t n)
{
	while (--n >= 0) {
		int16_t c = rb(a), d = rb(b);

		if (c != d)
			return (int16_t)(c - d);
		if (c == 0)
			return 0;
		a++;
		b++;
	}
	return 0;
}

/* ---- the files ---------------------------------------------------------- */

/*
 * Paths: the names the original has compiled in that are absolute paths
 * of the MUTOS file system - its temporary file "/tmp/ldaXXXXXX" and the
 * libraries -lX makes, "/lib/libX.a" and "/usr/lib/libX.a" (getfile()) -
 * are looked up under the MUTOS root (src/h/mutos_root.h); every other
 * name is the host's: the command line's (inputs, -o), and "a.out" and
 * the temporary "..ldtmpXXXXXX", relative to the current directory as on
 * MUTOS.
 */
static int
rooted(uint16_t name)
{
	return name == TFNAME || name == S_LIBNAME || name == S_LIBNAME + 4;
}

/* the host path of the name at image address name; 0 if it does not fit */
static int
host_path(uint16_t name, char *buf, size_t n)
{
	char mpath[4096];
	size_t k = 0;

	while (rb(name) != 0) {
		if (k + 1 >= sizeof(mpath))
			ldtrap("a file name longer than the host takes");
		mpath[k++] = (char)UB(name);
		name++;
	}
	mpath[k] = '\0';
	if (rooted((uint16_t)(name - k))) {
		if (mutos_path(buf, n, mpath) == NULL)
			ldtrap("a file name longer than the host takes");
	} else {
		if (k + 1 > n)
			ldtrap("a file name longer than the host takes");
		memcpy(buf, mpath, k + 1);
	}
	return 1;
}

#define	NFD	20		/* v7's NOFILE */
static	int	hostfd[NFD] = {0, 1, 2, -1, -1, -1, -1, -1, -1, -1,
			       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static	int	usedfd[NFD] = {1, 1, 1};

static int16_t
newfd(int h)
{
	int fd;

	for (fd = 0; fd < NFD && usedfd[fd]; fd++)
		;
	if (fd == NFD) {
		close(h);
		return -1;		/* EMFILE */
	}
	usedfd[fd] = 1;
	hostfd[fd] = h;
	return (int16_t)fd;
}

static int
hfd(int16_t fd)
{
	if (fd < 0 || fd >= NFD || !usedfd[fd])
		return -1;
	return hostfd[fd];
}

/*
 * open(name, mode): 0 reads, 2 reads and writes. A directory is refused:
 * MUTOS opens it and reads its entries, the host cannot read one as a
 * file (and the emulator says ENOENT).
 */
int16_t
sys_open(uint16_t name, int16_t mode)
{
	char path[4096 + 1024];
	struct stat st;
	int h;

	host_path(name, path, sizeof(path));
	h = open(path, mode == 0 ? O_RDONLY : mode == 1 ? O_WRONLY : O_RDWR);
	if (h < 0)
		return -1;
	if (fstat(h, &st) == 0 && S_ISDIR(st.st_mode)) {
		close(h);
		ldtrap("an open()ed directory - the original reads its entries");
	}
	return newfd(h);
}

/* creat(name, mode): mode less the umask for a new file, as the kernel */
int16_t
sys_creat(uint16_t name, int16_t mode)
{
	char path[4096 + 1024];
	int h;

	host_path(name, path, sizeof(path));
	h = open(path, O_RDWR | O_CREAT | O_TRUNC, (mode_t)(mode & 07777));
	if (h < 0)
		return -1;
	return newfd(h);
}

int16_t
sys_close(int16_t fd)
{
	if (hfd(fd) < 0)
		return -1;
	if (fd > 2)
		close(hostfd[fd]);
	usedfd[fd] = 0;
	hostfd[fd] = -1;
	return 0;
}

/*
 * read(fd, buf, n): n is an unsigned word (a negative count is a large
 * one, as the kernel takes it); as many bytes as the file has, into the
 * image - where they would land in memory the image does not model, the
 * read is refused.
 */
int16_t
sys_read(int16_t fd, uint16_t buf, uint16_t n)
{
	static uint8_t b[65536];
	int h = hfd(fd);
	uint32_t got = 0, i;

	if (h < 0)
		return -1;
	while (got < n) {
		ssize_t r = read(h, b + got, (size_t)(n - got));

		if (r < 0) {
			if (errno == EINTR)
				continue;
			if (got == 0)
				return -1;
			break;
		}
		if (r == 0)
			break;
		got += (uint32_t)r;
	}
	for (i = 0; i < got; i++) {
		uint32_t a = (uint32_t)buf + i;

		if (a > 0xffff || ldattr[a] != A_MEM)
			ldtrap_at((uint16_t)a, 1);
	}
	for (i = 0; i < got; i++)
		wb((uint16_t)(buf + i), b[i]);
	return (int16_t)got;
}

int16_t
sys_write(int16_t fd, uint16_t buf, uint16_t n)
{
	static uint8_t b[65536];
	int h = hfd(fd);
	uint32_t i, done = 0;

	if (h < 0)
		return -1;
	for (i = 0; i < n; i++)
		b[i] = UB((uint16_t)(buf + i));
	while (done < n) {
		ssize_t w = write(h, b + done, (size_t)(n - done));

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return done ? (int16_t)done : -1;
		}
		done += (uint32_t)w;
	}
	return (int16_t)n;
}

/*
 * lseek(fd, off, whence). A negative offset is refused: MUTOS's kernel (v7's
 * seek()) sets it and fails the read after it, the emulator fails the
 * seek - neither is modelled.
 */
int32_t
sys_lseek(int16_t fd, int32_t off, int16_t whence)
{
	int h = hfd(fd);
	off_t r;

	if (h < 0)
		return -1;
	if (whence == 0 && off < 0)
		ldtrap("an lseek() to a negative offset (not modelled)");
	r = lseek(h, (off_t)off, whence);
	return r < 0 ? -1 : (int32_t)r;
}

int16_t
sys_unlink(uint16_t name)
{
	char path[4096 + 1024];

	host_path(name, path, sizeof(path));
	return unlink(path) == 0 ? 0 : -1;
}

/*
 * link(name1, name2) - finishout()'s link of the temporary output name to
 * a.out, in one directory. MUTOS's file system always has hard links; a
 * host file system without them (FAT, some network shares: EPERM,
 * ENOTSUP, EXDEV) gets a rename() instead, which leaves the state the link
 * and delexit()'s unlink of name1 leave - a.out alone.
 */
int16_t
sys_link(uint16_t name1, uint16_t name2)
{
	char p1[4096 + 1024], p2[4096 + 1024];
	struct stat st;

	host_path(name1, p1, sizeof(p1));
	host_path(name2, p2, sizeof(p2));
	if (link(p1, p2) == 0)
		return 0;
	if ((errno == EPERM || errno == ENOTSUP || errno == EXDEV
	    || errno == EOPNOTSUPP) && lstat(p2, &st) != 0 && errno == ENOENT
	    && rename(p1, p2) == 0)
		return 0;
	return -1;
}

int16_t
sys_chmod(uint16_t name, int16_t mode)
{
	char path[4096 + 1024];

	host_path(name, path, sizeof(path));
	return chmod(path, (mode_t)(mode & 07777)) == 0 ? 0 : -1;
}

int16_t
sys_umask(int16_t mask)
{
	return (int16_t)umask((mode_t)(mask & 0777));
}

/* fstat(): the file's modification time, a v7 long */
int16_t
sys_fstat_mtime(int16_t fd, int32_t *mtime)
{
	struct stat st;
	int h = hfd(fd);

	if (h < 0 || fstat(h, &st) != 0) {
		*mtime = 0;
		return -1;
	}
	*mtime = (int32_t)(uint32_t)(st.st_mtime & 0xffffffff);
	return 0;
}

/* ---- exit(), the signals -------------------------------------------------- */

/* exit(): stdio's buffer flushed (the messages' last part), the status */
void
ld_exit(int16_t status)
{
	if (out_n > 0)
		host_write(1, out_buf, (size_t)out_n);
	out_n = 0;
	exit((uint8_t)status);
}

static void
on_signal(int sig)
{
	(void)sig;
	delexit();
}

/*
 * main()'s signal(SIGINT, delexit) and signal(SIGTERM, delexit), unless
 * the signal was ignored when ld started.
 */
void
ld_signals(void)
{
	static const int sigs[] = {SIGINT, SIGTERM};
	size_t i;

	for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
		struct sigaction old, sa;

		if (sigaction(sigs[i], NULL, &old) != 0 || old.sa_handler == SIG_IGN)
			continue;
		memset(&sa, 0, sizeof(sa));
		sa.sa_handler = on_signal;
		sigemptyset(&sa.sa_mask);
		sigaction(sigs[i], &sa, NULL);
	}
}

/* ---- refusals ---------------------------------------------------------- */

/*
 * A refusal: the original depends here on memory or state the port does
 * not model. The files stay as they are, stdout as the original's would be
 * by then (the full blocks written), the message goes to stderr, the
 * status is 2.
 */
void
ldtrap(const char *what)
{
	char m[400];
	int n;

	n = snprintf(m, sizeof(m), "ld: %s - the original depends on state "
	    "the port does not model here; refused\n", what);
	if (n > 0)
		host_write(2, (const uint8_t *)m, (size_t)n < sizeof(m) ? (size_t)n
		    : sizeof(m) - 1);
	exit(2);
}

void
ldtrap_at(uint16_t a, int write)
{
	char what[200];
	const char *where;

	if (a < 0x000a)
		where = "crt0's far pointers";
	else if (a >= LD_DATAEND && a < LD_BSS)
		where = "libc.a's data (the stdio structures, malloc's)";
	else if (a >= LD_BSS && a < LD_BSS + 2)
		where = "crt0's environ";
	else if (a >= LD_BSSEND && a < LD_END)
		where = "libc.a's BSS (printf's state, the stdout buffer)";
	else
		where = "memory the original has not allocated (the heap, the "
		    "stack)";
	snprintf(what, sizeof(what), "a %s %04x, %s",
	    write == 2 ? "word access at" : write ? "write to" : "read of",
	    a, where);
	ldtrap(what);
}
