/*
 * cclib.c - the machine and the library around the original cc: the
 * memory image (cc.h) and its initial state - crt0's argument words and
 * main()'s frame -, libc.a's printf, fprintf and putc on stdout and
 * stderr (unbuffered, as cc makes them), strcpy() and strcat(), malloc()
 * and sbrk() on the image, mktemp(), the system calls cc makes (creat,
 * close, access, unlink, fork, execv, wait, signal, exit) on the host,
 * the MUTOS paths, and the refusals (cctrap()).
 */
#define _POSIX_C_SOURCE 200809L	/* sigaction(), setenv() under -std=c11 */

#include "cc.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "../../h/mutos_root.h"

uint8_t	ccm[65536];
uint8_t	ccattr[65536];
uint8_t	cctaint[65536];

/* the argument strings: crt0 finds them at the top of the segment */
#define	F_ARGTOP	0xfff0
#define	CC_ARGMIN	0x8000	/* they must leave the heap room below them */
/* the frames below main()'s that the heap must not reach (callsys(),
   printf() and _doprnt(), the system call stubs) */
#define	STACK_MARGIN	0x400

static	uint16_t heap_limit;	/* the break may not pass it */

/* ---- the image ----------------------------------------------------- */

static void
attr(uint32_t lo, uint32_t hi, uint8_t a)
{
	uint32_t i;

	for (i = lo; i < hi; i++)
		ccattr[i] = a;
}

/*
 * The image at the start: the binary's text and data at address 0, the
 * BSS zero, then crt0's work - the argument strings at the top of the
 * segment and below them the words main() is called with (argc, argv[],
 * 0, an empty environment, 0), laid out as tests/realbin/mutosemu.py lays
 * them out (argv[0] is "cc"); crt0 reserves two words below them (the
 * second gets envp, which it also stores in environ), pushes argv and
 * argc, and stores the code segment into the far pointers' segment words
 * 1f5e and 1f62 - under the emulator 0x1000, on the hardware wherever
 * the text was loaded. Then main()'s prologue: the return address into
 * crt0 (00ac), BP (0), DI (0) and SI (crt0 left the argument words'
 * address in it), and chkstk's 0xe4 bytes - so its locals are at BP -
 * 6 .. BP - 0xe8, as the binary's code addresses them.
 *
 * What the image models (cc.h): the text and crt0's words (read only),
 * cc.o's data and BSS, libc.a's "(null)", "/" and malloc()'s and sbrk()'s
 * words, the heap once sbrk() gives it, main()'s frame, the argument
 * words and strings, and the zeros above them. Not modelled: libc.a's
 * FILE structures, buffers, signal table and errno, chkstk's limit, the
 * frames below main()'s.
 */
void
cc_init(int argc, char **argv)
{
	uint16_t top = F_ARGTOP, words, pad, s, envp;
	uint16_t *ptrs;
	int i;

	memset(ccm, 0, sizeof(ccm));
	memset(ccattr, A_NONE, sizeof(ccattr));
	memset(cctaint, 0, sizeof(cctaint));
	memcpy(ccm, cc_seg, CC_SEGSIZE);
	attr(0x0000, CC_TSIZE, A_RO);		/* the text */
	attr(CRT0_W, CRT0_W + 10, A_RO);	/* crt0's words */
	ccm[0x1f5e] = 0x00;			/* their segments: */
	ccm[0x1f5f] = 0x10;			/* the code's, 0x1000 */
	ccm[0x1f62] = 0x00;
	ccm[0x1f63] = 0x10;
	attr(tsa, IOB, A_MEM);			/* cc.o's data, "(null)" */
	attr(alloch, curbrk + 2, A_MEM);	/* malloc's, "/", curbrk */
	attr(environ_, environ_ + 2, A_RO);	/* crt0's environ */
	attr(tmp0, CC_BSSEND, A_MEM);		/* cc.o's BSS */
	attr(allocs, CC_END, A_MEM);		/* malloc's words */
	attr(F_ARGTOP, 0x10000, A_MEM);		/* zeros above the strings */

	if (argc < 1 || argc > 4000)
		cctrap("an argument count the image cannot hold");
	ptrs = malloc(sizeof(*ptrs) * (size_t)argc);
	if (ptrs == NULL)
		cctrap("no memory for the argument list");
	for (i = 0; i < argc; i++) {
		const char *a = i == 0 ? "cc" : argv[i];
		size_t n = strlen(a) + 1;
		size_t k;

		if ((size_t)(top - CC_ARGMIN) < n + 2 * (size_t)(argc + 3) + 0x100)
			cctrap("arguments longer than the image's argument area");
		top = (uint16_t)(top - n);
		for (k = 0; k < n; k++) {
			ccattr[top + k] = A_MEM;
			wb((uint16_t)(top + k), a[k]);
			cctaint[top + k] = 1;	/* from the command line */
		}
		ptrs[i] = top;
	}
	pad = top;
	top &= (uint16_t)~1;
	if (pad != top) {			/* the byte between: 0 */
		ccattr[top] = A_MEM;
		wb(top, 0);
	}
	/* the words crt0 finds: argc, argv[], 0, envp[] (none), 0 */
	words = (uint16_t)(top - 2 * (argc + 3));
	attr(words, top, A_MEM);
	ww(words, (uint16_t)argc);
	for (i = 0; i < argc; i++)
		ww((uint16_t)(words + 2 + 2 * i), ptrs[i]);
	ww((uint16_t)(words + 2 + 2 * argc), 0);
	ww((uint16_t)(words + 4 + 2 * argc), 0);
	free(ptrs);
	envp = (uint16_t)(words + 4 + 2 * argc);

	/* crt0: sub sp,4; push argv; push argc; call main; main's prologue */
	s = words;
	cc_bp = (uint16_t)(s - 12);
	attr((uint16_t)(cc_bp - 0xea), s, A_MEM);
	ww((uint16_t)(s - 4), envp);
	ww((uint16_t)(s - 6), (uint16_t)(s + 2));	/* argv */
	ww((uint16_t)(s - 8), (uint16_t)argc);		/* argc */
	ww((uint16_t)(s - 10), 0x00ac);			/* into crt0 */
	ww((uint16_t)(s - 12), 0);			/* BP */
	ww((uint16_t)(s - 14), 0);			/* DI */
	ww((uint16_t)(s - 16), (uint16_t)(s - 8));	/* SI */
	ccm[environ_] = (uint8_t)envp;
	ccm[environ_ + 1] = (uint8_t)(envp >> 8);
	heap_limit = (uint16_t)(cc_bp - 0xea - STACK_MARGIN);
}

/* ---- stdout and stderr: libc.a's stdio, as cc uses it ------------------ */

/*
 * cc makes stdout unbuffered (setbuf(stdout, NULL)); stderr is. libc.a's
 * printf() and fprintf() on stdout lend it a buffer for the call
 * (_stbuf(): _sobuf, 512 bytes) and write it out at the end (_ftbuf()):
 * one write() per 512 bytes and one for the rest. On stderr every byte is
 * a write() of its own (_flsbuf() of an unbuffered FILE), and so is a
 * putc() on either. Their FILE structures are back where they were after
 * every call, so the port keeps none (and an overrun reaching them is
 * refused).
 */
#define	BUFSIZ_	512

struct sink {
	int	fd;		/* -1: render only, write nothing */
	int	buffered;	/* 1: in blocks of 512 */
	int	n;
	uint8_t	buf[BUFSIZ_];
};

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
sink_putc(struct sink *k, int c)
{
	uint8_t b = (uint8_t)c;

	if (k->fd < 0)
		return;
	if (!k->buffered) {
		host_write(k->fd, &b, 1);
		return;
	}
	if (k->n == BUFSIZ_) {
		host_write(k->fd, k->buf, BUFSIZ_);
		k->n = 0;
	}
	k->buf[k->n++] = b;
}

static void
sink_flush(struct sink *k)
{
	if (k->fd >= 0 && k->n > 0)
		host_write(k->fd, k->buf, (size_t)k->n);
	k->n = 0;
}

/* ---- libc.a's printf ------------------------------------------------- */

/*
 * libc.a's _doprnt() (text 108e, the object ld's port re-enacts too) for
 * what a format of cc's can hold: text, and '%' with an optional '-', an
 * optional '0' fill, a width and a precision (digits, or '*': the next
 * argument), then d, o, x, u (a word, no precision), c (a char; 0 writes
 * nothing) or s (a string in the image; a null pointer is "(null)"; the
 * precision is its most characters). Any other character after the '%'
 * is written itself, as _doprnt() does - also the format's closing 0,
 * after which it goes on reading. Its long and floating conversions (D,
 * O, X, U, l, L, e, f, g) and %r are refused. cc's own formats use %s;
 * others come only from a -K or -B argument copied over them.
 */
static int
fmt_num(uint16_t *fmt, const uint16_t *args, int nargs, int *k)
{
	int n = 0;

	if (rb(*fmt) == '*') {
		(*fmt)++;
		if (*k >= nargs)
			cctrap("a printf argument the original does not pass");
		return (int16_t)args[(*k)++];
	}
	while (rb(*fmt) >= '0' && rb(*fmt) <= '9') {
		n = n * 10 + rb(*fmt) - '0';
		if (n > 0x7fff)
			cctrap("a printf width the port does not model");
		(*fmt)++;
	}
	return n;
}

static void
doprnt(struct sink *out, uint16_t fmt, const uint16_t *args, int nargs)
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
			sink_putc(out, c);
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
			cctrap("a negative printf width (not modelled)");
		c = rb(fmt++);
		switch (c) {
		case 'd': case 'o': case 'x': case 'u':
			if (prec != 0)
				cctrap("a printf precision of a number (not "
				    "modelled)");
			/* FALLTHROUGH */
		case 'c': case 's':
			if (k >= nargs)
				cctrap("a printf argument the original does not pass");
			break;
		case 'D': case 'O': case 'X': case 'U': case 'l': case 'L':
		case 'e': case 'f': case 'g': case 'r':
			cctrap("a printf conversion the port's printf does not have");
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
				sink_putc(out, '-');
				s++;
				len--;
				width--;
			}
			for (i = len; i < width; i++)
				sink_putc(out, fill);
		}
		for (i = 0; i < len; i++)
			sink_putc(out, s != NULL ? (uint8_t)s[i]
			    : (uint8_t)rb((uint16_t)(str + i)));
		if (ljust)
			for (i = len; i < width; i++)
				sink_putc(out, fill);
	}
}

/* printf(): stdout, buffered for the call */
void
cc_printf(uint16_t fmt, const uint16_t *args, int nargs)
{
	static struct sink out;

	out.fd = 1;
	out.buffered = 1;
	out.n = 0;
	doprnt(&out, fmt, args, nargs);
	sink_flush(&out);
}

/* fprintf(): stdout as printf(), stderr a byte at a time */
void
cc_fprintf(uint16_t iop, uint16_t fmt, const uint16_t *args, int nargs)
{
	static struct sink out;

	out.fd = iop == STDOUT_ ? 1 : 2;
	out.buffered = iop == STDOUT_;
	out.n = 0;
	doprnt(&out, fmt, args, nargs);
	sink_flush(&out);
}

/* putc() on an unbuffered FILE: _flsbuf() writes the byte */
void
cc_putc(int c, uint16_t iop)
{
	uint8_t b = (uint8_t)c;

	host_write(iop == STDOUT_ ? 1 : 2, &b, 1);
}

/* what a printf() would write, worked out without writing it */
static void
printf_check(uint16_t fmt, const uint16_t *args, int nargs)
{
	struct sink out;

	out.fd = -1;
	out.buffered = 0;
	out.n = 0;
	doprnt(&out, fmt, args, nargs);
}

/* ---- strcpy(), strcat() ------------------------------------------------ */

/* libc.a's strcpy (text 1bf2): byte by byte, the 0 included */
uint16_t
cc_strcpy(uint16_t d, uint16_t s)
{
	uint16_t r = d;

	while (cpb(d++, s++) != 0)
		;
	return r;
}

/* libc.a's strcat (text 1bc4): to d's end, then as strcpy() */
uint16_t
cc_strcat(uint16_t d, uint16_t s)
{
	uint16_t r = d;

	while (rb(d++) != 0)
		;
	d--;
	while (cpb(d++, s++) != 0)
		;
	return r;
}

/* ---- malloc() and sbrk() ------------------------------------------------ */

/*
 * libc.a's sbrk (text 1c70): curbrk (22b0) moved by incr, the old break
 * returned; -1 (ENOMEM) if it would wrap. The new memory is zero, as the
 * kernel gives it. The emulator grants every break; MUTOS refuses one that
 * reaches the stack (v7's estabur()) - the port refuses it before the
 * heap comes near main()'s frame.
 */
static uint16_t
cc_sbrk(uint16_t incr)
{
	uint16_t old = rw(curbrk), nb;
	uint32_t a;

	if (incr == 0)
		return old;
	nb = (uint16_t)(old + incr);
	if (nb < old)
		return 0xffff;
	if (nb > heap_limit)
		cctrap("a break that reaches the stack (MUTOS refuses it, the "
		    "emulator gives it - not modelled)");
	for (a = old; a < nb; a++) {
		ccattr[a] = A_MEM;
		ccm[a] = 0;
		cctaint[a] = 0;
	}
	ww(curbrk, nb);
	return old;
}

/*
 * libc.a's ialloc (text 19be): the arena of nbytes at qp put into the
 * ring of blocks (a block's word points at the next one, bit 0 set when
 * the block is busy): its first word points at its last, which joins the
 * ring after the block that comes before it in memory - the ring's head
 * alloch (22aa) moved down to it if it is lower.
 */
static void
ialloc(uint16_t qp, uint16_t nbytes)
{
	uint16_t si = qp, end, np, di;

	end = (uint16_t)((uint16_t)((nbytes >> 1) << 1) + si - 2);
	ww(si, end);
	if (rw(allocs) == 0)
		ww(allocs, allocs);
	di = rw(alloch);
	for (;;) {
		np = rw(di) & 0xfffe;
		if (np == rw(alloch))
			break;
		if (np > end && di < si)
			break;
		di = np;
	}
	ww(di, (uint16_t)(di + 2) == si ? si : (uint16_t)(si | 1));
	ww(end, (uint16_t)(end + 2) == np ? np : (uint16_t)(np | 1));
	if (rw(alloch) > si)
		ww(alloch, si);
}

/*
 * libc.a's malloc (text 189c) - not v7's: the first free block of the
 * ring, from allocp (22ac) on, that holds (nbytes + 3) / 2 words, its
 * free successors joined to it first; twice round the ring without one,
 * an arena of whole 1024-word units from sbrk(), put in by ialloc().
 * Every word it reads and writes is in the image, at the binary's
 * addresses. cc calls it only from copy(), for CHSPACE+50 bytes.
 */
uint16_t
cc_malloc(uint16_t nbytes)
{
	uint16_t nw = (uint16_t)((uint16_t)(nbytes + 3) >> 1);
	uint16_t p, q, bx, r;
	int16_t t;

again:
	p = rw(allocp);
	if (rw(allocs) == 0)
		goto grow;
	ww(mtemp, 0);
	for (;;) {
		if (!(rw(p) & 1)) {
			for (;;) {		/* join the free blocks after p */
				q = rw(p);
				if (rw(q) & 1)
					break;
				ww(p, rw(q));
			}
			bx = (uint16_t)((uint16_t)(nw << 1) + p);
			if (!(bx > q) && !(bx < p))
				goto found;
		}
		q = p;
		p = rw(p) & 0xfffe;
		if (p > q)
			continue;
		if (p != rw(alloch))
			return 0;
		ww(mtemp, (uint16_t)(rw(mtemp) + 1));
		if (ri(mtemp) > 1)
			break;
	}
grow:
	t = (int16_t)(uint16_t)(nw + 0x200);
	t = (int16_t)(t / 0x200);
	ww(mtemp, (uint16_t)((uint16_t)t << 9));
	q = cc_sbrk(0);
	if ((uint16_t)((uint16_t)(rw(mtemp) << 1) + q) <= q)
		return 0;
	for (;;) {
		r = cc_sbrk((uint16_t)(rw(mtemp) << 1));
		if (r != 0xffff)
			break;
		ww(mtemp, (uint16_t)(rw(mtemp) - 0x10));
		if (!(ri(mtemp) > (int16_t)nw))
			return 0;
	}
	ialloc(r, (uint16_t)(rw(mtemp) << 1));
	ww(allocp, rw(alloch));
	goto again;
found:
	bx = (uint16_t)((uint16_t)(nw << 1) + p);
	ww(allocp, bx);
	if (q > bx) {
		ww(allocx, rw(bx));
		ww(bx, rw(p));
	}
	ww(p, (uint16_t)(rw(allocp) | 1));
	return (uint16_t)(p + 2);
}

/* ---- the files ---------------------------------------------------------- */

/*
 * Paths. An absolute name whose '/' did not come from the command line
 * is a path of the MUTOS file system - a pass (/lib/c0, -t's
 * /usr/src/cmd/c/c0), a startup file (/lib/crt0.o), a temporary file
 * (/tmp/ctm...) - and is looked up under the MUTOS root
 * (src/h/mutos_root.h); every other name is the host's: the command
 * line's (a source, -o's name, -B's prefix), and relative names, relative
 * to the current directory as on MUTOS. The same rule gives the names cc
 * passes to its passes, which take every name as the host's.
 */
static void
host_path(uint16_t name, char *buf, size_t n)
{
	char mpath[4096];
	size_t k = 0;
	uint16_t p = name;

	while (rb(p) != 0) {
		if (k + 1 >= sizeof(mpath))
			cctrap("a file name longer than the host takes");
		mpath[k++] = (char)(uint8_t)rb(p);
		p++;
	}
	mpath[k] = '\0';
	if (mpath[0] == '/' && !cctaint[name]) {
		if (mutos_path(buf, n, mpath) == NULL)
			cctrap("a file name longer than the host takes");
	} else {
		if (k + 1 > n)
			cctrap("a file name longer than the host takes");
		memcpy(buf, mpath, k + 1);
	}
}

static uint16_t
cc_getpid(void)
{
	uint16_t pid = (uint16_t)(getpid() & 0x7fff);

	return pid ? pid : 1;
}

/*
 * libc.a's mktemp (text 1b24): the trailing X's become the process
 * number's digits, then the first of them 'a', 'b', ... as long as the
 * name exists (access()); "/" when even 'y' exists. The process number is
 * the host's, as the emulator gives a program its own.
 */
uint16_t
cc_mktemp(uint16_t as)
{
	uint16_t s = as, pid = cc_getpid();
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
		host_path(as, path, sizeof(path));
		if (access(path, F_OK) != 0)
			return as;
		if (i == 'z')
			return S_SLASH;
		wb(s, i++);
	}
}

#define	NFD	20		/* v7's NOFILE */
static	int	hostfd[NFD] = {0, 1, 2, -1, -1, -1, -1, -1, -1, -1,
			       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static	int	usedfd[NFD] = {1, 1, 1};

/* creat(name, mode): mode less the umask for a new file, as the kernel */
int16_t
cc_creat(uint16_t name, int16_t mode)
{
	char path[4096 + 1024];
	int h, fd;

	host_path(name, path, sizeof(path));
	h = open(path, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)(mode & 07777));
	if (h < 0)
		return -1;
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

int16_t
cc_close(int16_t fd)
{
	if (fd < 0 || fd >= NFD || !usedfd[fd])
		return -1;
	if (fd > 2)
		close(hostfd[fd]);
	usedfd[fd] = 0;
	hostfd[fd] = -1;
	return 0;
}

int16_t
cc_unlink(uint16_t name)
{
	char path[4096 + 1024];

	host_path(name, path, sizeof(path));
	return unlink(path) == 0 ? 0 : -1;
}

/* ---- the passes: fork(), execv(), wait() ------------------------------- */

/*
 * callsys()'s execv(f, v), read from the image in the parent before the
 * fork() - the child changes nothing before it calls execv() - so that a
 * name the port cannot read is refused by cc itself: the path and the
 * arguments (the words at v up to a 0, as the kernel reads them; past
 * av[50] into clist[] where the original wrote them there), each made a
 * host name by the rule of host_path(). The message the child prints if
 * execv() returns is worked out here too, for the same reason. The
 * passes are given the host's environment (the original passes its own,
 * empty under the emulator), with MUTOS_ROOT set to cc's root, so that
 * they look up their own MUTOS paths where cc does.
 */
#define	MAXEXARG	4096

static	char	*ex_path;
static	char	**ex_argv;
static	pid_t	ex_child;

static char *
host_dup(uint16_t name)
{
	char path[4096 + 1024];
	char *r;

	host_path(name, path, sizeof(path));
	r = strdup(path);
	if (r == NULL)
		cctrap("no memory for a pass's arguments");
	return r;
}

void
cc_prepare_exec(uint16_t f, uint16_t v)
{
	size_t n = 0, i;
	uint16_t a;

	if (ex_argv != NULL) {
		for (i = 0; ex_argv[i] != NULL; i++)
			free(ex_argv[i]);
		free(ex_argv);
		free(ex_path);
	}
	ex_path = host_dup(f);
	for (a = v; rw(a) != 0; a = (uint16_t)(a + 2))
		if (++n > MAXEXARG)
			cctrap("an argument list longer than the port takes");
	ex_argv = malloc(sizeof(*ex_argv) * (n + 1));
	if (ex_argv == NULL)
		cctrap("no memory for a pass's arguments");
	for (i = 0, a = v; i < n; i++, a = (uint16_t)(a + 2))
		ex_argv[i] = host_dup(rw(a));
	ex_argv[n] = NULL;
	printf_check(S_CANTFIND, &f, 1);
}

/* fork(): 0 in the child, 1 in the parent (the child's number kept), -1 */
int16_t
cc_fork(void)
{
	pid_t p = fork();

	if (p < 0)
		return -1;
	if (p == 0)
		return 0;
	ex_child = p;
	return 1;
}

/* execv() in the child: returns only if it fails */
void
cc_execv(void)
{
	setenv(MUTOS_ROOT_ENV, mutos_root(), 1);
	execv(ex_path, ex_argv);
}

/*
 * wait(&status): 1 for the child cc_fork() started, 0 for another, -1 for
 * none (the original waits on: ECHILD makes its loop endless, as here).
 * The status word is the host's, which is v7's: the exit status times
 * 256, or the signal (0200: a core image).
 */
int16_t
cc_wait(uint16_t *status)
{
	int st;
	pid_t r = wait(&st);

	if (r < 0)
		return -1;
	*status = (uint16_t)(st & 0xffff);
	return r == ex_child ? 1 : 0;
}

/* ---- exit(), the signals -------------------------------------------------- */

/* exit(): nothing is buffered - stdout is unbuffered between printf()s */
void
cc_exit(int16_t status)
{
	exit((uint8_t)status);
}

static	void	(*cc_handler)(void);

static void
on_signal(int sig)
{
	(void)sig;
	cc_handler();
}

/*
 * main()'s
 *	if (signal(SIGINT, SIG_IGN) != SIG_IGN) signal(SIGINT, idexit);
 * and the same for SIGTERM.
 */
void
cc_signals(void (*handler)(void))
{
	static const int sigs[] = {SIGINT, SIGTERM};
	size_t i;

	cc_handler = handler;
	for (i = 0; i < sizeof(sigs) / sizeof(sigs[0]); i++) {
		struct sigaction ign, old, sa;

		memset(&ign, 0, sizeof(ign));
		ign.sa_handler = SIG_IGN;
		sigemptyset(&ign.sa_mask);
		if (sigaction(sigs[i], &ign, &old) != 0
		    || old.sa_handler == SIG_IGN)
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
 * not model. The files stay as they are (cc's temporary file included),
 * what has been written stays written, the message goes to stderr, the
 * status is 2.
 */
void
cctrap(const char *what)
{
	char m[400];
	int n;

	n = snprintf(m, sizeof(m), "cc: %s - the original depends on state "
	    "the port does not model here; refused\n", what);
	if (n > 0)
		host_write(2, (const uint8_t *)m, (size_t)n < sizeof(m) ? (size_t)n
		    : sizeof(m) - 1);
	exit(2);
}

void
cctrap_at(uint16_t a, int write)
{
	char what[200];
	const char *where;

	if (a < CC_TSIZE)
		where = "the text";
	else if (a < tsa)
		where = "crt0's far pointers";
	else if (a >= IOB && a < LIBC_DATAEND)
		where = "libc.a's data (the FILE structures, signal()'s table)";
	else if (a >= environ_ && a < environ_ + 2)
		where = "crt0's environ";
	else if (a >= CC_BSSEND && a < CC_END)
		where = "libc.a's BSS (the stdio buffers, errno)";
	else if (a < (uint16_t)(cc_bp - 0xea))
		where = "memory the original has not allocated (beyond the "
		    "break, the frames below main()'s)";
	else
		where = "memory the port does not model";
	snprintf(what, sizeof(what), "a %s %04x, %s",
	    write ? "write to" : "read of", a, where);
	cctrap(what);
}
