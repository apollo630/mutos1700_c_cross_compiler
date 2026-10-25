/*
 * c2lib.c - the machine and the library around the original c2: the
 * memory image (c2.h) and its initial state, crt0's argument words,
 * sbrk(), libc.a's stdio as c2 uses it - getc() and ungetc() on stdin,
 * printf() and fprintf() through _doprnt() and putc(), _filbuf(),
 * _flsbuf(), fflush(), fclose(), freopen() and exit()'s _cleanup(), each
 * on the FILE structures and buffers of the image -, the system calls
 * they make (open, creat, read, write, close), atoi(), index(), strcmp(),
 * strcpy(), strcat(), and the refusals (c2trap()).
 */
#define _POSIX_C_SOURCE 200809L

#include "c2.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

uint8_t	c2m[65536];
uint8_t	c2a[65536];

/* the argument strings: crt0 finds them at the top of the data segment */
#define	F_ARGTOP	0xfff0
#define	C2_ARGMIN	0x8000	/* they must leave the original room */
/* the stack below the argument words: the original's frames (not
   modelled but for movedat()'s node), which the heap must not reach */
#define	C2_STACK	0x0400

static	uint16_t heap_limit;	/* the break may not pass it */
static	uint16_t data_node;	/* movedat()'s local "data" */

/* ---- the image ----------------------------------------------------- */

static void
attr(uint16_t lo, uint32_t hi, uint8_t a)
{
	uint32_t i;

	for (i = lo; i < hi; i++)
		c2a[i] = a;
}

/*
 * The image at the start: the binary's initialized data at address 0, the
 * BSS zero, then crt0's work - the argument strings at the top of the data
 * segment and below them the words main() is called with (argc, argv[],
 * 0, an empty environment, 0), laid out as tests/realbin/mutosemu.py lays
 * them out (argv[0] is "c2"), environ pointing at the empty environment;
 * crt0 stores the code segment into the far pointers' segment words 4
 * and 8 - under the emulator 0x1000, on the hardware wherever the text
 * was loaded. crt0's words are memory to c2: it reads them through a null
 * node pointer (a node's back is word 4, the emulator's value taken) and
 * writes them so - word 8, the system calls' segment, gets word 4's value,
 * the same; a system call through a far pointer that was changed is
 * refused (check_farptr()). The whole data segment and BSS are modelled
 * (libc.a's stdio structures and buffers, printf's and malloc's words:
 * c2's overruns can reach them); not modelled: the stack, but for the
 * argument strings and words and movedat()'s node.
 */
void
c2_init(int argc, char **argv, uint16_t *mutos_argv)
{
	uint16_t top = F_ARGTOP, words, pad;
	uint16_t *ptrs;
	int i;

	memset(c2m, 0, sizeof(c2m));
	memset(c2a, A_NONE, sizeof(c2a));
	memcpy(c2m, c2_data, C2_DSIZE);
	c2m[CRT0_W4] = 0x00;
	c2m[CRT0_W4 + 1] = 0x10;
	c2m[CRT0_W8] = 0x00;
	c2m[CRT0_W8 + 1] = 0x10;
	attr(0x0000, C2_END, A_MEM);		/* crt0's words, the data, the
						   BSS */

	if (argc < 1 || argc > 4000)
		c2trap("an argument count the image cannot hold");
	ptrs = malloc(sizeof(*ptrs) * (size_t)argc);
	if (ptrs == NULL)
		c2trap("no memory for the argument list");
	for (i = 0; i < argc; i++) {
		const char *a = i == 0 ? "c2" : argv[i];
		size_t n = strlen(a) + 1;
		size_t k;

		if ((size_t)(top - C2_ARGMIN) < n + 2 * (size_t)(argc + 3))
			c2trap("arguments longer than the image's argument area");
		top = (uint16_t)(top - n);
		for (k = 0; k < n; k++) {
			c2a[top + k] = A_MEM;
			c2m[top + k] = (uint8_t)a[k];
		}
		ptrs[i] = top;
	}
	pad = top;
	top &= (uint16_t)~1;
	if (pad != top) {			/* the byte between: 0 */
		c2a[top] = A_MEM;
		c2m[top] = 0;
	}
	words = (uint16_t)(top - 2 * (argc + 3));
	attr(words, top, A_MEM);
	ww(words, (uint16_t)argc);
	for (i = 0; i < argc; i++)
		ww((uint16_t)(words + 2 + 2 * i), ptrs[i]);
	ww((uint16_t)(words + 2 + 2 * argc), 0);	/* argv's end */
	ww((uint16_t)(words + 4 + 2 * argc), 0);	/* no environment */
	ww(ENVIRON, (uint16_t)(words + 4 + 2 * argc));
	free(ptrs);
	*mutos_argv = (uint16_t)(words + 2);

	/* movedat()'s node in the stack area below, its forw zero */
	data_node = (uint16_t)((words - 0x40) & ~1);
	attr(data_node, (uint32_t)data_node + NODESZ, A_MEM);
	heap_limit = (uint16_t)(words - C2_STACK);
}

uint16_t
c2_datanode(void)
{
	return data_node;
}

/* ---- the refusals ---------------------------------------------------- */

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

/*
 * A refusal: what the original would have written by then is in the
 * output file (the blocks stdio wrote); the message goes to stderr, the
 * status is 2.
 */
void
c2trap(const char *what)
{
	char m[400];
	int n;

	n = snprintf(m, sizeof(m), "c2: %s - the original depends on state "
	    "the port does not model here; refused\n", what);
	if (n > 0)
		host_write(2, (const uint8_t *)m, (size_t)n < sizeof(m) ? (size_t)n
		    : sizeof(m) - 1);
	exit(2);
}

void
c2trap_at(uint16_t a, int write)
{
	char what[200];
	const char *where;

	if (a >= rw(CURBRK) && a < heap_limit)
		where = "memory past the break";
	else
		where = "the stack (the original's frames)";
	snprintf(what, sizeof(what), "a %s %04x, %s",
	    write ? "write to" : "read of", a, where);
	c2trap(what);
}

/*
 * Every system call goes through crt0's far pointer at word 6 (offset 2,
 * the trampoline Iscadr) and word 8 (the code segment): c2 can write them
 * through a null node pointer. Unchanged - the usual case, word 4's value
 * written into word 8 - the call is made; else the original jumps
 * elsewhere, which is not modelled.
 */
static void
check_farptr(void)
{
	if (rw(6) != 2 || rw(CRT0_W8) != 0x1000)
		c2trap("a system call through crt0's far pointer, written over");
}

/* ---- sbrk() ------------------------------------------------------------ */

/*
 * libc.a's sbrk (text 4380): 0 gives the break; a break past 64K is
 * ENOMEM (-1); else break() - which the emulator always grants, and
 * MUTOS's kernel refuses somewhere below the stack, at a limit not
 * known: a break that would come near the stack is refused here.
 */
int16_t
c2_sbrk(int16_t n)
{
	uint16_t old = rw(CURBRK);
	uint32_t nu = (uint32_t)old + (uint16_t)n;

	if (n == 0)
		return (int16_t)old;
	if (nu > 0xffff) {
		ww(ERRNO, 12);			/* ENOMEM */
		return -1;
	}
	check_farptr();
	if (nu > heap_limit)
		c2trap("a heap that reaches the stack (MUTOS's own break limit "
		    "is not known)");
	attr(old, nu, A_MEM);
	ww(CURBRK, (uint16_t)nu);
	return (int16_t)old;
}

/* ---- the system calls ------------------------------------------------- */

#define	NFD	20		/* v7's NOFILE */
static	int	hostfd[NFD] = {0, 1, 2, -1, -1, -1, -1, -1, -1, -1,
			       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

static void
set_errno(int e)
{
	ww(ERRNO, (uint16_t)(e > 0 && e <= 34 ? e : 22));
}

/* a file name of the image, from the command line: the host's */
static void
host_name(uint16_t name, char *buf, size_t n)
{
	size_t k = 0;

	while (rb(name) != 0) {
		if (k + 1 >= n)
			c2trap("a file name longer than the host takes");
		buf[k++] = (char)UB(name);
		name++;
	}
	buf[k] = '\0';
}

static int16_t
newfd(int h)
{
	int fd;

	for (fd = 0; fd < NFD; fd++)
		if (hostfd[fd] < 0) {
			hostfd[fd] = h;
			return (int16_t)fd;
		}
	close(h);
	set_errno(24);				/* EMFILE */
	return -1;
}

static int16_t
sys_open(uint16_t name, int16_t mode)
{
	char path[4096];
	struct stat st;
	int h;

	check_farptr();

	host_name(name, path, sizeof(path));
	if ((uint16_t)mode > 2) {
		set_errno(EINVAL);
		return -1;
	}
	h = open(path, mode == 0 ? O_RDONLY : mode == 1 ? O_WRONLY : O_RDWR);
	if (h < 0) {
		set_errno(errno);
		return -1;
	}
	if (fstat(h, &st) == 0 && S_ISDIR(st.st_mode))
		c2trap("a directory opened as a file");
	return newfd(h);
}

static int16_t
sys_creat(uint16_t name, int16_t mode)
{
	char path[4096];
	int h;

	check_farptr();

	host_name(name, path, sizeof(path));
	h = open(path, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)(mode & 07777));
	if (h < 0) {
		set_errno(errno);
		return -1;
	}
	return newfd(h);
}

static int16_t
sys_close(int16_t fd)
{

	check_farptr();
	if (fd < 0 || fd >= NFD || hostfd[fd] < 0) {
		set_errno(EBADF);
		return -1;
	}
	if (hostfd[fd] > 2)
		close(hostfd[fd]);
	hostfd[fd] = -1;
	return 0;
}

/*
 * read(fd, buf, n): as the emulator reads a file, as much as there is up
 * to n, into the image.
 */
static int16_t
sys_read(int16_t fd, uint16_t buf, uint16_t n)
{
	uint8_t b[512];
	uint16_t got = 0;

	check_farptr();

	if (fd < 0 || fd >= NFD || hostfd[fd] < 0) {
		set_errno(EBADF);
		return -1;
	}
	if (n > sizeof(b))
		c2trap("a read of more than one block");
	while (got < n) {
		ssize_t r = read(hostfd[fd], b + got, (size_t)(n - got));

		if (r < 0) {
			if (errno == EINTR)
				continue;
			if (got)
				break;
			set_errno(errno);
			return -1;
		}
		if (r == 0)
			break;
		got = (uint16_t)(got + r);
	}
	for (uint16_t i = 0; i < got; i++)
		wb((uint16_t)(buf + i), b[i]);
	return (int16_t)got;
}

static int16_t
sys_write(int16_t fd, uint16_t buf, uint16_t n)
{
	uint8_t b[512];
	uint16_t i;

	check_farptr();

	if (fd < 0 || fd >= NFD || hostfd[fd] < 0) {
		set_errno(EBADF);
		return -1;
	}
	if (n > sizeof(b))
		c2trap("a write of more than one block");
	for (i = 0; i < n; i++)
		b[i] = (uint8_t)UB((uint16_t)(buf + i));
	host_write(hostfd[fd], b, n);
	return (int16_t)n;
}

/*
 * isatty() (gtty()): the host's answer for the file the descriptor is.
 * The emulator says "not a terminal" for every file; c2 runs with files.
 */
static int16_t
sys_isatty(int16_t fd)
{

	check_farptr();
	if (fd < 0 || fd >= NFD || hostfd[fd] < 0)
		return 0;
	return isatty(hostfd[fd]) ? 1 : 0;
}

/* ---- libc.a's stdio on the image ---------------------------------------- */

#define	FLAG(iop)	UB((uint16_t)((iop) + F_FLAG))
#define	S_FLAG(iop, v)	wb((uint16_t)((iop) + F_FLAG), (v))
#define	FILENO(iop)	rb((uint16_t)((iop) + F_FILE))
#define	PTR(iop)	rw((uint16_t)((iop) + F_PTR))
#define	CNT(iop)	RI((uint16_t)((iop) + F_CNT))
#define	BASE(iop)	rw((uint16_t)((iop) + F_BASE))
#define	S_PTR(iop, v)	ww((uint16_t)((iop) + F_PTR), (uint16_t)(v))
#define	S_CNT(iop, v)	ww((uint16_t)((iop) + F_CNT), (uint16_t)(v))
#define	S_BASE(iop, v)	ww((uint16_t)((iop) + F_BASE), (uint16_t)(v))

/*
 * _filbuf() (text 3576). A FILE without a buffer that is not unbuffered
 * would get one from malloc(): not modelled (c2's stdin has _sibuf from
 * the start, and freopen() keeps it).
 */
static int16_t
filbuf(uint16_t iop)
{
	int16_t n;
	uint16_t p;

	if (FLAG(iop) & IO_RW)
		S_FLAG(iop, FLAG(iop) | IO_READ);
	if (!(FLAG(iop) & IO_READ))
		return -1;
	if (FLAG(iop) & IO_STRG)
		return -1;
	while (BASE(iop) == 0) {
		if (FLAG(iop) & IO_NBF)
			S_BASE(iop, NBFBUF + FILENO(iop));
		else
			c2trap("_filbuf(): a buffer from malloc()");
	}
	S_PTR(iop, BASE(iop));
	n = sys_read(FILENO(iop), PTR(iop), (FLAG(iop) & IO_NBF) ? 1 : 512);
	S_CNT(iop, n);
	S_CNT(iop, CNT(iop) - 1);
	if (CNT(iop) >= 0) {
		p = PTR(iop);
		n = (int16_t)UB(p);
		S_PTR(iop, p + 1);
		return n;
	}
	if (CNT(iop) == -1) {
		S_FLAG(iop, FLAG(iop) | IO_EOF);
		if (FLAG(iop) & IO_RW)
			S_FLAG(iop, FLAG(iop) & ~IO_READ);
	} else
		S_FLAG(iop, FLAG(iop) | IO_ERR);
	S_CNT(iop, 0);
	return -1;
}

/* getc(stdin), the macro getline() has */
int16_t
c_getc(void)
{
	uint16_t p;
	int16_t c;

	S_CNT(STDIN, CNT(STDIN) - 1);
	if (CNT(STDIN) < 0)
		return filbuf(STDIN);
	p = PTR(STDIN);
	c = (int16_t)UB(p);
	S_PTR(STDIN, p + 1);
	return c;
}

/* ungetc() (text 352a) */
int16_t
c_ungetc(int16_t c, uint16_t iop)
{
	if (c == -1)
		return -1;
	if (!((FLAG(iop) & IO_READ) && PTR(iop) > BASE(iop))) {
		if (PTR(iop) != BASE(iop))
			return -1;
		if (CNT(iop) != 0)
			return -1;
		(void)rb(PTR(iop));
		S_PTR(iop, PTR(iop) + 1);
	}
	S_CNT(iop, CNT(iop) + 1);
	S_PTR(iop, PTR(iop) - 1);
	wb(PTR(iop), c);
	return 0;
}

/* fflush() (text 3cfe) */
static int16_t
c_fflush(uint16_t iop)
{
	uint16_t base;
	int16_t n;

	if ((FLAG(iop) & (IO_WRT | IO_NBF)) == IO_WRT && (base = BASE(iop)) != 0
	    && (n = (int16_t)(uint16_t)(PTR(iop) - base)) > 0) {
		S_PTR(iop, base);
		S_CNT(iop, 512);
		if (sys_write(FILENO(iop), base, (uint16_t)n) != n) {
			S_FLAG(iop, FLAG(iop) | IO_ERR);
			return -1;
		}
	}
	return 0;
}

/*
 * _flsbuf() (text 3c16): stdout without a buffer gets _sobuf unless it is
 * a terminal (then it is unbuffered); another FILE would get one from
 * malloc(): not modelled (stderr is unbuffered).
 */
static int16_t
flsbuf(int16_t c, uint16_t iop)
{
	uint16_t base;
	int16_t n, w;

	if (FLAG(iop) & IO_RW) {
		S_FLAG(iop, FLAG(iop) | IO_WRT);
		S_FLAG(iop, FLAG(iop) & ~IO_EOF);
	}
	for (;;) {
		if (FLAG(iop) & IO_NBF) {
			uint8_t ch = (uint8_t)c;

			check_farptr();
			n = 1;
			if (FILENO(iop) < 0 || FILENO(iop) >= NFD
			    || hostfd[FILENO(iop)] < 0)
				w = -1;
			else {
				host_write(hostfd[FILENO(iop)], &ch, 1);
				w = 1;
			}
			S_CNT(iop, 0);
			break;
		}
		if ((base = BASE(iop)) == 0) {
			if (iop != STDOUT)
				c2trap("_flsbuf(): a buffer from malloc()");
			if (sys_isatty(FILENO(iop))) {
				S_FLAG(iop, FLAG(iop) | IO_NBF);
				continue;
			}
			S_BASE(iop, SOBUF);
			S_PTR(iop, SOBUF);
			continue;
		}
		n = w = (int16_t)(uint16_t)(PTR(iop) - base);
		if (n > 0) {
			S_PTR(iop, base);
			w = sys_write(FILENO(iop), base, (uint16_t)n);
		}
		S_CNT(iop, 0x1ff);
		wb(base, c);
		S_PTR(iop, base + 1);
		break;
	}
	if (n != w) {
		S_FLAG(iop, FLAG(iop) | IO_ERR);
		return -1;
	}
	return c;
}

/* putc(), the macro _doprnt() and _strout() have */
static void
c_putc(int16_t c, uint16_t iop)
{
	uint16_t p;

	S_CNT(iop, CNT(iop) - 1);
	if (CNT(iop) < 0) {
		flsbuf(c, iop);
		return;
	}
	p = PTR(iop);
	S_PTR(iop, p + 1);
	wb(p, c);
}

/* fclose() (text 3d72); free() of a malloc()'d buffer is not modelled */
static int16_t
c_fclose(uint16_t iop)
{
	int16_t r = -1;

	if ((FLAG(iop) & (IO_READ | IO_WRT | IO_RW)) && !(FLAG(iop) & IO_STRG)) {
		r = c_fflush(iop);
		if (sys_close(FILENO(iop)) < 0)
			r = -1;
		if (FLAG(iop) & IO_MYBUF)
			c2trap("fclose(): free() of a malloc()'d buffer");
		if (FLAG(iop) & (IO_MYBUF | IO_NBF))
			S_BASE(iop, 0);
	}
	S_FLAG(iop, 0);
	S_CNT(iop, 0);
	return r;
}

/* _create() (text 3f10): creat(name, 0666), reopened for "+" */
static int16_t
create(uint16_t name, int16_t plus)
{
	int16_t fd;

	fd = sys_creat(name, 0666);
	if (plus && fd >= 0) {
		sys_close(fd);
		fd = sys_open(name, 2);
	}
	return fd;
}

/* _endopen() (text 3e38) */
static uint16_t
endopen(uint16_t name, uint16_t mode, uint16_t iop)
{
	int16_t plus, fd;

	if (iop == 0)
		return 0;
	plus = rb((uint16_t)(mode + 1)) == '+';
	switch (rb(mode)) {
	case 'a':
		fd = sys_open(name, plus ? 2 : 1);
		if (fd >= 0) {
			if (hostfd[fd] >= 0)
				lseek(hostfd[fd], 0, SEEK_END);
		} else if (RI(ERRNO) == 2)
			fd = create(name, plus);
		break;
	case 'r':
		fd = sys_open(name, plus ? 2 : 0);
		break;
	case 'w':
		fd = create(name, plus);
		break;
	default:
		return 0;
	}
	if (fd < 0)
		return 0;
	S_CNT(iop, 0);
	wb((uint16_t)(iop + F_FILE), fd);
	if (plus)
		S_FLAG(iop, FLAG(iop) | IO_RW);
	else if (rb(mode) == 'r')
		S_FLAG(iop, FLAG(iop) | IO_READ);
	else
		S_FLAG(iop, FLAG(iop) | IO_WRT);
	return iop;
}

/* freopen() (text 3484) */
uint16_t
c_freopen(uint16_t name, uint16_t mode, uint16_t iop)
{
	c_fclose(iop);
	return endopen(name, mode, iop);
}

/* _stbuf() (text 3dd8): an unbuffered stdout gets _sobuf for one call */
static int16_t
stbuf(uint16_t iop)
{
	if (iop == STDOUT && (FLAG(iop) & IO_NBF) && BASE(iop) == 0) {
		S_FLAG(iop, FLAG(iop) & ~IO_NBF);
		S_PTR(iop, SOBUF);
		S_BASE(iop, SOBUF);
		S_CNT(iop, 0);
		return 1;
	}
	return 0;
}

/* _ftbuf() (text 3e12) */
static void
ftbuf(uint16_t iop, int16_t flag)
{
	if (flag) {
		c_fflush(iop);
		S_FLAG(iop, FLAG(iop) | IO_NBF);
		S_PTR(iop, 0);
		S_BASE(iop, 0);
		S_CNT(iop, 0);
	}
}

/* ---- libc.a's _doprnt() ------------------------------------------------- */

static int
fmt_num(uint16_t *fmt, const uint16_t *args, int nargs, int *k)
{
	int n = 0;

	if (rb(*fmt) == '*') {
		(*fmt)++;
		if (*k >= nargs)
			c2trap("a printf argument the original does not pass");
		return (int16_t)args[(*k)++];
	}
	while (rb(*fmt) >= '0' && rb(*fmt) <= '9') {
		n = n * 10 + rb(*fmt) - '0';
		if (n > 0x7fff)
			c2trap("a printf width the port does not model");
		(*fmt)++;
	}
	return n;
}

/*
 * libc.a's _doprnt() (text 3634) for what a format of c2's can hold: text,
 * and '%' with an optional '-', an optional '0' fill, a width and a
 * precision (digits, or '*': the next argument), then d, o, x, u (a word,
 * no precision), c (a char; 0 writes nothing) or s (a string in the image;
 * a null pointer is "(null)"; the precision is its most characters). Any
 * other character after the '%' is written itself, as _doprnt() does. Its
 * long and floating conversions (D, O, X, U, l, L, e, f, g) and %r are
 * refused. c2's own formats use %s and %d; others come only from a format
 * written over. The output goes through putc() into the FILE's buffer in
 * the image; _doprnt()'s own work area (libc.a's BSS) is not written -
 * nothing of c2 reads it.
 */
static void
doprnt(uint16_t fmt, const uint16_t *args, int nargs, uint16_t iop)
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
			c_putc((int16_t)(uint8_t)c, iop);
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
			c2trap("a negative printf width (not modelled)");
		c = rb(fmt++);
		switch (c) {
		case 'd': case 'o': case 'x': case 'u':
			if (prec != 0)
				c2trap("a printf precision of a number (not "
				    "modelled)");
			/* FALLTHROUGH */
		case 'c': case 's':
			if (k >= nargs)
				c2trap("a printf argument the original does not pass");
			break;
		case 'D': case 'O': case 'X': case 'U': case 'l': case 'L':
		case 'e': case 'f': case 'g': case 'r':
			c2trap("a printf conversion the port's printf does not have");
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
			for (len = 0; rb((uint16_t)(str + len)) != 0; )
				if (++len == prec)
					break;
			break;
		}
		if (!ljust) {
			if (fill == '0' && s != NULL && len > 0 && s[0] == '-'
			    && len < width) {
				c_putc('-', iop);
				s++;
				len--;
				width--;
			}
			for (i = len; i < width; i++)
				c_putc((int16_t)fill, iop);
		}
		for (i = 0; i < len; i++)
			c_putc(s != NULL ? (int16_t)(uint8_t)s[i]
			    : (int16_t)UB((uint16_t)(str + i)), iop);
		if (ljust)
			for (i = len; i < width; i++)
				c_putc((int16_t)fill, iop);
	}
}

/* printf() (text 34e6) */
void
c_printf(uint16_t fmt, const uint16_t *args, int nargs)
{
	int16_t f = stbuf(STDOUT);

	doprnt(fmt, args, nargs, STDOUT);
	ftbuf(STDOUT, f);
}

/* fprintf() (text 34a4) */
void
c_fprintf(uint16_t iop, uint16_t fmt, const uint16_t *args, int nargs)
{
	int16_t f = stbuf(iop);

	doprnt(fmt, args, nargs, iop);
	ftbuf(iop, f);
}

/* exit() (text 3f4a): _cleanup() closes every FILE up to _lastbuf */
void
c2_exit(int16_t status)
{
	uint16_t iop;

	for (iop = STDIN; iop < rw(LASTBUF); iop = (uint16_t)(iop + 8))
		c_fclose(iop);
	check_farptr();
	exit(status & 0377);
}

/* ---- atoi(), index(), strcmp(), strcpy(), strcat() ---------------------- */

/* libc.a's atoi (text 3f6a): blanks and tabs, one sign, digits */
int16_t
c_atoi(uint16_t s)
{
	int16_t n = 0, neg = 0;

	while (rb(s) == '\t' || rb(s) == ' ')
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
	return neg ? (int16_t)(uint16_t)(0u - (uint16_t)n) : n;
}

/* index() (text 4286): the character is looked for before the end */
uint16_t
c_index(uint16_t s, int16_t c)
{
	for (;;) {
		if (rb(s) == (int16_t)(signed char)c)
			return s;
		if (rb(s++) == 0)
			return 0;
	}
}

/* strcmp() (text 42d8) */
int16_t
c_strcmp(uint16_t s1, uint16_t s2)
{
	for (;;) {
		int16_t c = rb(s2++);

		if (c != rb(s1))
			return (int16_t)(rb(s1) - rb((uint16_t)(s2 - 1)));
		if (rb(s1++) == 0)
			return 0;
	}
}

/* strcpy() (text 4306), a byte at a time */
uint16_t
c_strcpy(uint16_t s1, uint16_t s2)
{
	uint16_t os1 = s1;
	int16_t c;

	do {
		c = rb(s2++);
		wb(s1++, c);
	} while (c);
	return os1;
}

/* strcat() (text 42aa) */
uint16_t
c_strcat(uint16_t s1, uint16_t s2)
{
	uint16_t os1 = s1;
	int16_t c;

	while (rb(s1++) != 0)
		;
	s1--;
	do {
		c = rb(s2++);
		wb(s1++, c);
	} while (c);
	return os1;
}
