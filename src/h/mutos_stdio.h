/*
 * mutos_stdio.h - libc.a's stdio as the ports of nm, size, strip and tsort
 * (step 8 of docs/TOOLCHAIN_PLAN.md) see it.
 *
 * The originals print through printf() and fprintf(stderr, ...) of the
 * MUTOS 1700 libc.a. What a caller can tell of that stdio, and what the
 * ports therefore keep:
 *  - standard output is a 512-byte block (BUFSIZ of MUTOS's <stdio.h>):
 *    nothing is written until the block is full - the write comes when the
 *    next character arrives (_flsbuf) - or the program ends (exit() flushes);
 *    a terminal gets a line at a time. The messages of a program that
 *    writes to standard output and to standard error come out in the
 *    original's order when both go to one file or pipe;
 *  - standard error has no buffer: one write() for every fprintf();
 *  - a write() that does not take all the bytes sets the stream's error flag
 *    (_IOERR) and the rest is lost; printf() returns EOF (-1) once the flag
 *    is set, 0 until then (v7's printf: "ferror(stdout) ? EOF : 0"). The
 *    return value is not always lost: a main() that does not end in
 *    exit() leaves the last value in AX, which crt0 passes to exit()
 *    (size and tsort);
 *  - exit() ends with a flush, _exit() without.
 *
 * "refused": where the original depends on state the port does not model
 * (a directory it opens and reads the entries of, a seek to a negative
 * offset, a name with no 0 behind it, ...), mu_trap() ends the run with
 * exit status 125 and a last message that ends in "refused" - a status the
 * originals never end with. The comparators (tests/realbin/) count such a
 * run apart: what the port wrote must be a prefix of what the original
 * wrote.
 *
 * Header only (static inline functions), so that every tool can include it
 * without a library of its own. The including file defines MU_TOOL, the
 * tool's name, before it.
 */

#ifndef MUTOS_STDIO_H
#define MUTOS_STDIO_H

#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef MU_TOOL
#error "define MU_TOOL (the tool's name) before including mutos_stdio.h"
#endif

#if defined(__GNUC__)
#define MU_NORETURN __attribute__((noreturn))
#else
#define MU_NORETURN
#endif

#define MU_BUFSIZ 512

static char	mu_obuf[MU_BUFSIZ];
static size_t	mu_ocnt;
static int	mu_otty = -1;
static int	mu_oerr;		/* stdout's _IOERR */

/* one write() of the block; a short write (or an error) sets _IOERR and drops the rest */
static inline void
mu_flush(void)
{
	if (mu_ocnt > 0) {
		ssize_t w;

		do {
			w = write(1, mu_obuf, mu_ocnt);
		} while (w < 0 && errno == EINTR);
		if (w < 0 || (size_t)w != mu_ocnt)
			mu_oerr = 1;
	}
	mu_ocnt = 0;
}

/* putc(c, stdout): a full block is written when the next character comes
 * (_flsbuf); a terminal gets a line at a time (isatty() at the first) */
static inline void
mu_oputc(char c)
{
	if (mu_otty < 0)
		mu_otty = isatty(1);
	if (mu_ocnt == MU_BUFSIZ)
		mu_flush();
	mu_obuf[mu_ocnt++] = c;
	if (mu_otty && c == '\n')
		mu_flush();
}

static inline void
mu_oputs(const char *s, size_t n)
{
	for (size_t i = 0; i < n; i++)
		mu_oputc(s[i]);
}

/* the text of a format, in a block malloc()ed (vasprintf() is not C11) */
static inline char *
mu_format(int *len, const char *fmt, va_list ap)
{
	va_list ap2;
	int n;
	char *b;

	va_copy(ap2, ap);
	n = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if (n < 0 || (b = malloc((size_t)n + 1)) == NULL)
		_exit(125);
	vsnprintf(b, (size_t)n + 1, fmt, ap);
	*len = n;
	return b;
}

/* printf(): the host's formats; the value is the original's */
static inline int
mu_printf(const char *fmt, ...)
{
	char *b;
	va_list ap;
	int n;

	va_start(ap, fmt);
	b = mu_format(&n, fmt, ap);
	va_end(ap);
	mu_oputs(b, (size_t)n);
	free(b);
	return mu_oerr ? -1 : 0;
}

/* fprintf(stderr, ...): no buffer, one write() */
static inline void
mu_eprintf(const char *fmt, ...)
{
	char *b;
	va_list ap;
	int n;
	ssize_t w;

	va_start(ap, fmt);
	b = mu_format(&n, fmt, ap);
	va_end(ap);
	do {
		w = write(2, b, (size_t)n);
	} while (w < 0 && errno == EINTR);
	free(b);
}

/* exit(): the block standard output holds is written */
static inline MU_NORETURN void
mu_exit(int status)
{
	mu_flush();
	_exit(status & 0xff);
}

/* a refusal: see the head of this file */
static inline MU_NORETURN void
mu_trap(const char *what)
{
	mu_flush();
	mu_eprintf("%s: %s - the original depends on state the port does not "
	    "model here; refused\n", MU_TOOL, what);
	_exit(125);
}

#endif /* MUTOS_STDIO_H */
