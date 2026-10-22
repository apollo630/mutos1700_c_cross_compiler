/*
 * cpplib.c - the machine and the library around the original cpp: the
 * memory image (cpp.h) and its initial state, crt0's argument words, the
 * files (libc.a's open/read/close and the stdio stream the output goes
 * through), libc.a's printf for the messages, calloc(), exit(), the
 * frames' arrays and the refusals (cpptrap()).
 */
#include "cpp.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../h/mutos_root.h"

uint16_t cppw[32768];
uint8_t	cppattr[65536];
int	cpp_bswap;

/* the image addresses whose string is a path of the MUTOS file system */
static	uint8_t	rootmap[65536];

static	uint16_t cpp_brk = CPP_END;	/* calloc()'s next block */

uint16_t cpp_sp;			/* the original's stack pointer */
static	uint16_t cpp_sp0;		/* crt0's: the argument words */

/* ---- the image ----------------------------------------------------- */

static void
attr(uint16_t lo, uint32_t hi, uint8_t a)
{
	uint32_t i;

	for (i = lo; i < hi; i++)
		cppattr[i] = a;
}

/*
 * The image at the start: the binary's initialized data at address 0, the
 * BSS zero, then crt0's work - the argument strings at the top of the data
 * segment and below them the words main() is called with (argc, argv[],
 * 0, an empty environment, 0), laid out as tests/realbin/mutosemu.py lays
 * them out. What the image models (cpp.h): cpp's and yacc's data, the BSS
 * of cpp.o and cpy.o, libc.a's _ctype_[] (read through isspace() and
 * isdigit()), the heap calloc() hands out, the frames' arrays while their
 * function runs, the argument strings, and libc.a's errno: yyv[150] is
 * errno, written by a #if 150 states deep and read back (no failed system
 * call, which would set it, comes between). Not modelled: libc.a's stdio
 * (_iob[], _sobuf[], _sibuf[]), malloc's and printf's data, crt0's words
 * (read only), environ, the stack.
 */
void
cpp_init(int argc, char **argv, uint16_t *mutos_argv)
{
	uint16_t one = 1, top = F_ARGTOP, words;
	uint16_t ptrs[256];
	int i;

	cpp_bswap = *(unsigned char *)&one == 0;
	memset(cppw, 0, sizeof(cppw));
	memset(cppattr, A_NONE, sizeof(cppattr));
	for (i = 0; i < CPP_DSIZE; i++)
		((uint8_t *)cppw)[i ^ cpp_bswap] = cpp_data[i];
	attr(0x0000, 0x000a, A_RO);	/* crt0's words */
	attr(0x000a, IOB, A_MEM);	/* cpp's and yacc's data */
	attr(CTYPE, 0x0ace, A_MEM);	/* _ctype_[] */
	attr(0x0ad4, SOBUF, A_MEM);	/* cpp.o's BSS */
	attr(0xa3e8, 0xa51a, A_MEM);	/* yylval, yyval, yyv[], errno */
	memset(rootmap, 0, sizeof(rootmap));
	rootmap[S_USRINCLUDE] = 1;	/* the default directory */

	if (argc > 255)
		cpptrap("more than 255 arguments");
	for (i = 0; i < argc; i++) {
		/* argv[0] is "cpp", as the original cc and the emulator name it:
		 * the strings' length places the stack */
		const char *a = i == 0 ? "cpp" : argv[i];
		size_t n = strlen(a) + 1;
		size_t k;

		if ((size_t)(top - CPP_STACKMIN) < n + 2 * (size_t)(argc + 3) + 0x1000)
			cpptrap("arguments longer than the image's argument area");
		top = (uint16_t)(top - n);
		for (k = 0; k < n; k++) {
			cppattr[top + k] = A_MEM;
			wb((uint16_t)(top + k), a[k]);
		}
		ptrs[i] = top;
	}
	top &= (uint16_t)~1;
	words = (uint16_t)(top - 2 * (argc + 3));
	attr(words, top, A_MEM);
	WU(words) = (uint16_t)argc;
	for (i = 0; i < argc; i++)
		WU(words + 2 + 2 * i) = ptrs[i];
	WU(words + 2 + 2 * argc) = 0;		/* argv's end */
	WU(words + 4 + 2 * argc) = 0;		/* no environment */
	*mutos_argv = (uint16_t)(words + 2);
	cpp_sp0 = words;
	cpp_crt0();
}

/*
 * crt0 (text 0080): it stores the code segment into the far pointers'
 * segment words 4 and 8 - under tests/realbin/mutosemu.py 0x1000, on the
 * hardware wherever the text was loaded; nothing in cpp reads them -,
 * reserves two words below the argument words, pushes argv and argc
 * (envp goes into the reserved word), and calls main(): the arguments
 * are below sp0 - 2.
 */
void
cpp_crt0(void)
{
	cppw[4 >> 1] = 0x1000;
	cppw[8 >> 1] = 0x1000;
	cpp_sp = (uint16_t)(cpp_sp0 - 2);
}

/*
 * The original's stack (cpp.h). fr_enter(): a call with nargs argument
 * words - the return address, BP (the frame's bp, returned), DI, SI, the
 * locals. fr_leave(): back to the caller's stack pointer before it pushed
 * the arguments; the frame's modelled arrays are gone. fr_array(): an
 * array of the current frame in the image, writable, undefined until
 * written.
 */
#define	MAXFRAMES	64
static	struct frame {
	uint16_t bp, lo, hi;		/* [lo,hi): the frame's arrays */
} frames[MAXFRAMES];
static	int	nframes;

uint16_t
fr_enter(int nargs, uint16_t locals)
{
	uint16_t bp;

	if (nframes == MAXFRAMES)
		cpptrap("calls nested deeper than the port's frame list");
	cpp_sp = (uint16_t)(cpp_sp - 2 * nargs - 4);
	bp = cpp_sp;
	if ((uint32_t)cpp_sp < (uint32_t)CPP_STACKMIN + 4 + locals)
		cpptrap("a stack below the image's stack area (the original's "
		    "chkstk would grow it into the heap's space)");
	cpp_sp = (uint16_t)(cpp_sp - 4 - locals);
	frames[nframes].bp = bp;
	frames[nframes].lo = frames[nframes].hi = 0;
	nframes++;
	return bp;
}

void
fr_leave(uint16_t bp, int nargs)
{
	struct frame *f;

	if (nframes == 0 || frames[nframes - 1].bp != bp)
		cpptrap("the port's frames out of step (a port error)");
	f = &frames[--nframes];
	if (f->hi > f->lo)
		attr(f->lo, f->hi, A_NONE);
	cpp_sp = (uint16_t)(bp + 4 + 2 * nargs);
}

void
fr_array(uint16_t a, uint16_t n)
{
	struct frame *f = &frames[nframes - 1];

	attr(a, (uint32_t)a + n, A_UNDEF);
	if (f->hi == f->lo) {
		f->lo = a;
		f->hi = (uint16_t)(a + n);
	} else {
		if (a < f->lo)
			f->lo = a;
		if ((uint16_t)(a + n) > f->hi)
			f->hi = (uint16_t)(a + n);
	}
}

/* ---- the output: libc.a's stdio, as far as cpp uses it ------------- */

/*
 * The stream the output goes through - stdout, or the file named second
 * on the command line (fopen()ed, setbuf() to _sobuf) - is buffered in
 * blocks of BUFSIZ: libc.a's _flsbuf() writes the buffer when the 513th
 * byte comes, and exit() the rest. stderr is unbuffered. A refusal writes
 * nothing more: what the original would have written by then is the
 * blocks it had flushed.
 */
static	int	out_fd = 1;		/* host fd of the original's fout */
static	uint8_t	out_buf[BUFSIZ_];
static	int	out_n;
static	uint16_t out_file;		/* fout's FILE address, 0: none open */

static void
out_write(const uint8_t *b, size_t n)
{
	while (n > 0) {
		ssize_t w = write(out_fd, b, n);

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
err_write(const uint8_t *b, size_t n)
{
	while (n > 0) {
		ssize_t w = write(2, b, n);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return;
		}
		b += w;
		n -= (size_t)w;
	}
}

/* putc(c, f): f is stdout or the output file; stderr writes at once */
void
cpp_putc(int c, uint16_t f)
{
	uint8_t b = (uint8_t)c;

	if (f == STDERR_F) {
		err_write(&b, 1);
		return;
	}
	if (f != STDOUT_F && f != out_file)
		cpptrap("putc() on a stream the port does not have (a wild fout)");
	if (out_n == BUFSIZ_) {
		out_write(out_buf, BUFSIZ_);
		out_n = 0;
	}
	out_buf[out_n++] = b;
}

/*
 * fopen(name, "w") of the output file: creat() with mode 0666 (less the
 * umask). The name is the command line's, a host path. The FILE is
 * _iob[3], the first free one (stdin, stdout and stderr are open). 0 if
 * it cannot be created.
 */
uint16_t
cpp_fopen_w(uint16_t name)
{
	char path[4096];
	size_t n = 0;
	int fd;

	while (rb(name) != 0) {
		if (n + 1 >= sizeof(path))
			cpptrap("an output file name longer than the host takes");
		path[n++] = (char)UB(name);
		name++;
	}
	path[n] = '\0';
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return 0;
	out_fd = fd;
	out_file = (uint16_t)(IOB + 3 * 8);
	return out_file;
}

/* fclose(stdout): nothing has been written to it yet */
void
cpp_fclose_stdout(void)
{
	out_n = 0;
}

/* ---- the input: open(), read(), close() --------------------------- */

#define	NFD	20		/* v7's NOFILE */
static	int	hostfd[NFD] = {0, -1, -1, -1, -1, -1, -1, -1, -1, -1,
			       -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static	int	usedfd[NFD] = {1, 1, 1};

int
cpp_rooted(uint16_t name)
{
	return rootmap[name];
}

void
cpp_set_rooted(uint16_t name, int r)
{
	rootmap[name] = (uint8_t)(r != 0);
}

/*
 * open(name, 0). A name the port knows to be a path of the MUTOS file
 * system (doincl(): the default directory /usr/include, an absolute
 * #include name, and what is found through them) is looked up under the
 * MUTOS root (src/h/mutos_root.h) - if it is absolute; any other name is
 * the host's, relative to the current directory as on MUTOS. The fd is
 * the lowest free one, as the kernel's.
 */
int16_t
cpp_open(uint16_t name, int rooted)
{
	char mpath[4096], hpath[4096 + 1024];
	size_t n = 0;
	struct stat st;
	int fd, h;

	while (rb(name) != 0) {
		if (n + 1 >= sizeof(mpath))
			cpptrap("a file name longer than the host takes");
		mpath[n++] = (char)UB(name);
		name++;
	}
	mpath[n] = '\0';
	if (rooted) {
		if (mutos_path(hpath, sizeof(hpath), mpath) == NULL)
			cpptrap("a file name longer than the host takes");
	} else
		strcpy(hpath, mpath);
	for (fd = 0; fd < NFD && usedfd[fd]; fd++)
		;
	h = open(hpath, O_RDONLY);
	if (h < 0)
		return -1;
	if (fstat(h, &st) == 0 && S_ISDIR(st.st_mode)) {
		close(h);
		cpptrap("an open()ed directory - the original reads its entries");
	}
	if (fd == NFD) {
		close(h);
		return -1;		/* EMFILE */
	}
	usedfd[fd] = 1;
	hostfd[fd] = h;
	return (int16_t)fd;
}

/*
 * read(fd, buf, n): as from a file, up to n bytes, fewer only at its end;
 * -1 for a bad fd.
 */
int16_t
cpp_read(int16_t fd, uint16_t buf, int16_t n)
{
	uint8_t b[BUFSIZ_];
	int16_t got = 0;
	int i;

	if (fd < 0 || fd >= NFD || !usedfd[fd] || hostfd[fd] < 0 || n < 0)
		return -1;
	if (n > BUFSIZ_)
		cpptrap("a read of more than BUFSIZ");
	while (got < n) {
		ssize_t r = read(hostfd[fd], b + got, (size_t)(n - got));

		if (r < 0) {
			if (errno == EINTR)
				continue;
			if (got == 0)
				return -1;
			break;
		}
		if (r == 0)
			break;
		got = (int16_t)(got + r);
	}
	for (i = 0; i < got; i++)
		wb((uint16_t)(buf + i), b[i]);
	return got;
}

void
cpp_close(int16_t fd)
{
	if (fd < 0 || fd >= NFD || !usedfd[fd])
		return;
	if (fd > 2 && hostfd[fd] >= 0)
		close(hostfd[fd]);
	if (fd > 2) {
		usedfd[fd] = 0;
		hostfd[fd] = -1;
	}
}

/* ---- libc.a's printf ----------------------------------------------- */

/*
 * libc.a's _doprnt() for the formats cpp uses: %d (a signed word), %c
 * (its low byte; a 0 writes nothing), %s (a string in the image; a null
 * pointer is "(null)"). The format is a string in the image - the
 * original's own - and the arguments are the words the original pushes.
 * Each byte goes to emit().
 */
static void
doprnt(void (*emit)(int, void *), void *ctx, uint16_t fmt,
    const uint16_t *args, int nargs)
{
	char num[8];
	uint16_t s;
	int16_t c;
	int k = 0, i;

	for (;; fmt++) {
		c = rb(fmt);
		if (c == 0)
			return;
		if (c != '%') {
			emit(c, ctx);
			continue;
		}
		fmt++;
		c = rb(fmt);
		if (k >= nargs && (c == 'd' || c == 'c' || c == 's'))
			cpptrap("a printf argument the original does not pass");
		switch (c) {
		case 'd':
			snprintf(num, sizeof(num), "%d", (int16_t)args[k++]);
			for (i = 0; num[i]; i++)
				emit(num[i], ctx);
			break;
		case 'c':
			if ((args[k] & 0377) != 0)
				emit(args[k] & 0377, ctx);
			k++;
			break;
		case 's':
			s = args[k++];
			if (s == 0) {
				const char *n = "(null)";

				while (*n)
					emit(*n++, ctx);
				break;
			}
			while (rb(s) != 0) {
				emit(rb(s), ctx);
				s++;
			}
			break;
		default:
			cpptrap("a printf format the port's printf does not have");
		}
	}
}

static void
emit_file(int c, void *ctx)
{
	cpp_putc(c, *(uint16_t *)ctx);
}

static void
emit_mem(int c, void *ctx)
{
	uint16_t *p = ctx;

	wb(*p, c);
	(*p)++;
}

void
cpp_fprintf(uint16_t f, uint16_t fmt, const uint16_t *args, int nargs)
{
	doprnt(emit_file, &f, fmt, args, nargs);
}

void
cpp_sprintf(uint16_t buf, uint16_t fmt, const uint16_t *args, int nargs)
{
	doprnt(emit_mem, &buf, fmt, args, nargs);
	wb(buf, 0);
}

/* ---- calloc(), exit() ---------------------------------------------- */

/*
 * calloc(n, m): zeroed memory of the heap above the BSS (savestr() is the
 * one caller). Its addresses are not the original's - libc.a's malloc()
 * keeps headers between the blocks -, which nothing that is compared
 * depends on.
 */
uint16_t
cpp_calloc(uint16_t n, uint16_t m)
{
	uint32_t size = (uint32_t)n * m;
	uint16_t a = cpp_brk;
	uint32_t i;

	if (size > 0xffff || (uint32_t)cpp_brk + size + 2 > CPP_STACKMIN)
		cpptrap("a heap past the image's heap area");
	for (i = 0; i < size; i++) {
		cppattr[a + i] = A_MEM;
		((uint8_t *)cppw)[(a + i) ^ cpp_bswap] = 0;
	}
	cpp_brk = (uint16_t)((cpp_brk + size + 1) & ~1u);
	return a;
}

/* exit(): stdio's buffer flushed (the output's last part), the status */
void
cpp_exit(int16_t status)
{
	if (out_n > 0)
		out_write(out_buf, (size_t)out_n);
	out_n = 0;
	if (out_fd != 1)
		close(out_fd);
	exit((uint8_t)status);
}

/* ---- refusals -------------------------------------------------------- */

/*
 * A refusal: the original traps here, reads or writes its own stack
 * frame, or depends on state the image does not model. The output stays
 * as the original's would be by then (the full blocks written), the
 * message goes to stderr, the status is 2.
 */
void
cpptrap(const char *what)
{
	char m[400];
	int n;

	n = snprintf(m, sizeof(m), "cpp: %s - the original traps or depends "
	    "on state the port does not model here; refused\n", what);
	if (n > 0)
		err_write((const uint8_t *)m, (size_t)n < sizeof(m) ? (size_t)n
		    : sizeof(m) - 1);
	if (out_fd != 1)
		close(out_fd);
	exit(2);
}

void
cpptrap_at(uint16_t a, int write)
{
	char what[200];
	const char *where;

	if (a >= CPP_STACKMIN && a < cpp_sp0)
		where = cppattr[a] == A_UNDEF ? "a byte of a frame's array not "
		    "written yet (the original reads stack garbage)" :
		    a >= cpp_sp ? "the stack beyond a frame's array (the "
		    "original overwrites or reads its stack frame)" :
		    "the stack below its pointer (the original's stack garbage)";
	else if (a < 0x000a)
		where = "crt0's far pointers";
	else if (a >= IOB && a < CTYPE)
		where = "libc.a's FILE structures or malloc's data";
	else if (a >= 0x0ace && a < 0x0ad4)
		where = "crt0's and chkstk's data";
	else if (a >= SOBUF && a < 0xa3e8)
		where = "the output's stdio buffer";
	else if (a >= 0xa51a && a < CPP_END)
		where = "libc.a's own data (stdin's buffer, printf's)";
	else
		where = "memory the original has not allocated (the heap, the "
		    "stack)";
	snprintf(what, sizeof(what), "a %s %04x, %s",
	    write == 2 ? "word access at" : write ? "write to" : "read of",
	    a, where);
	cpptrap(what);
}

/* ---- libc.a's string functions on the image ------------------------- */

int16_t
cstrcmp(uint16_t a, uint16_t b)
{
	for (;; a++, b++) {
		int16_t c = rb(a), d = rb(b);

		if (c != d)
			return (int16_t)(c - d);
		if (c == 0)
			return 0;
	}
}

void
cstrcpy(uint16_t d, uint16_t s)
{
	int16_t c;

	do {
		c = rb(s++);
		wb(d++, c);
	} while (c != 0);
}

void
cstrcat(uint16_t d, uint16_t s)
{
	while (rb(d) != 0)
		d++;
	cstrcpy(d, s);
}
