/*
 * mklib.c - what the original make gets from libc.a and from its 64K
 * segment, for the port.
 *
 * Standard output as libc.a's stdio does it, standard error without a
 * buffer, the refusals (mk_trap()), the segment's size (the originals'
 * heap blocks and stack frames are counted), what a char above 0x7f does
 * when the original uses it as an index, the file slots of stdio and the
 * file descriptors of the directories make keeps open, the entries of a
 * directory as a MUTOS directory gives them, and the path of the
 * commands. printf()'s formats are the host's; the arguments that are
 * 16-bit in the original are passed as such by the callers.
 */


#include "make.h"

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../h/mutos_root.h"

/* ---- the refusals ------------------------------------------------------ */

void
mk_trap(const char *what)
{
	char m[512];
	int n;

	n = snprintf(m, sizeof(m), "Make: %s - the original depends on state "
	    "the port does not model here; refused\n", what);
	mk_fflush();
	if (n > 0) {
		ssize_t r = write(2, m, (size_t)n < sizeof(m) ? (size_t)n : sizeof(m) - 1);

		(void)r;
	}
	_exit(125);	/* not a status make itself ends with */
}

/* ---- standard output: libc.a's stdio ----------------------------------- */

#define BUFSIZ_ 512		/* BUFSIZ of MUTOS's stdio.h */

static char obuf[BUFSIZ_];
static size_t ocnt;
static int otty = -1;

static void
owrite(const char *p, size_t n)
{
	while (n > 0) {
		ssize_t w = write(1, p, n);

		if (w < 0) {
			if (errno == EINTR)
				continue;
			return;		/* a failed write has nowhere to be told */
		}
		p += w;
		n -= (size_t)w;
	}
}

void
mk_fflush(void)
{
	if (ocnt > 0)
		owrite(obuf, ocnt);
	ocnt = 0;
}

/* putc(c, stdout): a full buffer is written when the next character comes
 * (_flsbuf); a terminal gets a line at a time (isatty() at the first one) */
static void
oputc(char c)
{
	if (otty < 0)
		otty = isatty(1);
	if (ocnt == BUFSIZ_)
		mk_fflush();
	obuf[ocnt++] = c;
	if (otty && c == '\n')
		mk_fflush();
}

void
mk_printf(const char *fmt, ...)
{
	char *b = NULL;
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vasprintf(&b, fmt, ap);
	va_end(ap);
	if (n < 0 || b == NULL)
		mk_trap("out of memory for a message");
	for (int i = 0; i < n; i++)
		oputc(b[i]);
	free(b);
}

void
mk_eprintf(const char *fmt, ...)
{
	char *b = NULL;
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = vasprintf(&b, fmt, ap);
	va_end(ap);
	if (n < 0 || b == NULL)
		_exit(125);
	{
		const char *p = b;
		size_t left = (size_t)n;

		while (left > 0) {
			ssize_t w = write(2, p, left);

			if (w < 0) {
				if (errno == EINTR)
					continue;
				break;
			}
			p += w;
			left -= (size_t)w;
		}
	}
	free(b);
}

void
mk_exit(int status)
{
	mk_fflush();		/* exit(): _cleanup() writes what stdout holds */
	_exit(status & 0xff);
}

/* ---- the 64K segment ---------------------------------------------------- */

/*
 * The original's data segment holds, besides the 9,718 bytes of its data
 * and bss, the heap (calloc(), whose blocks have a header word) and, from
 * the other end, the stack. Where the break and the stack meet is not
 * known; the port refuses when what it counts reaches 0xF000 of the 0x10000,
 * a margin of 4K for what it does not count (stdio's buffers, the C
 * runtime's frames).
 */
#define SEG_USED 0x25F6
#define SEG_LIMIT 0xF000

static size_t heap_used, stack_used;

static void
check_segment(void)
{
	if (SEG_USED + heap_used + stack_used >= SEG_LIMIT)
		mk_trap("the heap and the stack would not fit the original's "
		    "64K segment");
}

void
mk_account(size_t n)
{
	heap_used += 2 + ((n + 1) & ~(size_t)1);
	check_segment();
}

/* Where the original's break is, as an address in its data segment: the
 * address of the next block calloc() gives, to a first approximation (the
 * allocator's free list is not modelled). */
unsigned
mk_break(void)
{
	return (unsigned)(SEG_USED + heap_used);
}

void
mk_enter(size_t frame)
{
	stack_used += frame;
	check_segment();
}

void
mk_leave(size_t frame)
{
	stack_used -= frame;
}

/* ---- a char of the original as an index --------------------------------- */

/*
 * funny[128] and (_ctype_+1)[c]: the original's chars are signed, so a byte
 * above 0x7f is a negative index, which reads the memory in front of the
 * table - in funny's case the bss of main.o, in the ctype table's the data of
 * libc.a. A char of -1 (the byte 0xff) is the ctype table's EOF entry (zero:
 * no class), and the one valid negative index there.
 */
int
mk_funny(int c)
{
	if (c < 0)
		mk_trap("a byte above 0x7f indexes funny[]");
	return funny[c];
}

static int
ctype_class(int c)
{
	if (c < -1)
		mk_trap("a byte above 0x7f indexes the ctype table");
	if (c < 0 || c > 127)
		return 0;
	return c;
}

int
mk_isspace(int c)
{
	c = ctype_class(c);
	return c == ' ' || (c >= 9 && c <= 13);
}

int
mk_isalpha(int c)
{
	c = ctype_class(c);
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int
mk_isdigit(int c)
{
	c = ctype_class(c);
	return c >= '0' && c <= '9';
}

/* ---- stdio's slots and the file descriptors ------------------------------- */

/*
 * stdio has twenty slots (stdin, stdout, stderr and seventeen more), and
 * the kernel hands out the lowest free descriptor: the directories make
 * keeps open are 3, 4, 5, ..., what -d prints as "Open directories:", and
 * the eighteenth file that is open at a time cannot be opened.
 */
#define NFILES 17

static unsigned long fdmask;

int
mk_fopen_slot(void)
{
	for (int i = 0; i < NFILES; i++)
		if (!(fdmask & (1UL << i))) {
			fdmask |= 1UL << i;
			return 3 + i;
		}
	return -1;
}

void
mk_fclose_slot(int fd)
{
	if (fd >= 3 && fd < 3 + NFILES)
		fdmask &= ~(1UL << (fd - 3));
}

int
mk_diropen(const char *name, int *fd)
{
	struct stat st;
	DIR *d;
	int s;

	name = MKPATH(name);
	if (stat(name, &st) < 0)
		return 0;
	if (!S_ISDIR(st.st_mode)) {
		/* the original reads the bytes of the file as directory entries */
		if (access(name, R_OK) < 0)
			return 0;
		mk_trap("a pattern's directory part is a file");
	}
	d = opendir(name);
	if (d == NULL)
		return 0;
	closedir(d);
	s = mk_fopen_slot();
	if (s < 0)
		return 0;
	*fd = s;
	return 1;
}

static int
bytecmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

char **
mk_dirread(const char *name, int *n)
{
	DIR *d = opendir(MKPATH(name));
	struct dirent *e;
	char **l = NULL;
	int cnt = 0, cap = 0;

	*n = 0;
	if (d == NULL)
		return NULL;
	while ((e = readdir(d)) != NULL) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
			continue;
		if (strlen(e->d_name) > 14)	/* not a MUTOS name (DIRSIZ) */
			continue;
		if (cnt == cap) {
			cap = cap ? cap * 2 : 64;
			l = realloc(l, (size_t)cap * sizeof(*l));
			if (l == NULL)
				mk_trap("out of memory for a directory");
		}
		l[cnt] = strdup(e->d_name);
		if (l[cnt] == NULL)
			mk_trap("out of memory for a directory");
		cnt++;
	}
	closedir(d);
	if (cnt > 1)
		qsort(l, (size_t)cnt, sizeof(*l), bytecmp);
	/* the entries "." and ".." come first, as in every MUTOS directory */
	l = realloc(l, (size_t)(cnt + 2) * sizeof(*l));
	if (l == NULL)
		mk_trap("out of memory for a directory");
	memmove(l + 2, l, (size_t)cnt * sizeof(*l));
	l[0] = strdup(".");
	l[1] = strdup("..");
	*n = cnt + 2;
	return l;
}

void
mk_dirfree(char **l, int n)
{
	for (int i = 0; i < n; i++)
		free(l[i]);
	free(l);
}

/* ---- the commands' path --------------------------------------------------- */

/*
 * The originals the commands name - cc, as, ld, ar - are the tools of the
 * MUTOS root: its bin/ is first in the PATH of every command, in front of
 * the host's (or libc's default ":/bin:/usr/bin", where the empty name is
 * the current directory, when there is none).
 */
void
mk_setpath(void)
{
	const char *old = getenv("PATH");
	const char *root = mutos_root();
	size_t n;
	char *p;

	if (old == NULL)
		old = ":/bin:/usr/bin";
	n = strlen(root) + strlen(old) + 8;
	p = malloc(n);
	if (p == NULL)
		return;
	snprintf(p, n, "%s/bin:%s", root, old);
	setenv("PATH", p, 1);
	free(p);
}
