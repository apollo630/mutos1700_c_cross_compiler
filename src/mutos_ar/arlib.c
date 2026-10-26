/*
 * arlib.c - what the original ar gets from libc.a, for the port.
 *
 * mktemp(), perror() and ctime() as the MUTOS 1700 libc.a has them
 * (the offsets are those of ar's text, tests/realbin/armap.txt), the
 * v7 long as a byte string, the path of a MUTOS file under the MUTOS
 * root, and the refusals (artrap()). printf() and fprintf() need no
 * re-enactment: ar's formats are the host's ("%s", "%c", "%3d/%1d",
 * "%7D" - the host's "%7ld" -, "%-12.12s").
 */
#define _POSIX_C_SOURCE 200809L	/* localtime_r() under -std=c11 */

#include "ar.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "../h/mutos_root.h"

/* ardata.c, generated from the binary: libc.a's sys_errlist[] */
extern const int ar_nerr;
extern const char *const ar_errlist[];

/* ---- a v7 long: the high word first, each word little-endian ------------- */

int32_t
ar_getlong(const uint8_t *p)
{
	return (int32_t)(((uint32_t)p[1] << 24) | ((uint32_t)p[0] << 16) |
	    ((uint32_t)p[3] << 8) | (uint32_t)p[2]);
}

void
ar_putlong(uint8_t *p, int32_t v)
{
	uint32_t u = (uint32_t)v;

	p[0] = (uint8_t)(u >> 16);
	p[1] = (uint8_t)(u >> 24);
	p[2] = (uint8_t)u;
	p[3] = (uint8_t)(u >> 8);
}

/* ---- the refusals --------------------------------------------------------- */

/* a write to the standard error; a failure of it has nowhere to be told */
static void
errw(const char *p, size_t n)
{
	ssize_t r = write(2, p, n);

	(void)r;
}

void	(*ar_trapcleanup)(void);

void
artrap(const char *what)
{
	char m[400];
	int n;

	n = snprintf(m, sizeof(m), "ar: %s - the original depends on state "
	    "the port does not model here; refused\n", what);
	if (n > 0) {
		fflush(stdout);
		errw(m, (size_t)n < sizeof(m) ? (size_t)n : sizeof(m) - 1);
	}
	if (ar_trapcleanup)
		(*ar_trapcleanup)();
	exit(125);	/* not a status ar itself ends with */
}

/* ---- MUTOS paths ---------------------------------------------------------- */

const char *
ar_mpath(const char *mpath)
{
	static char buf[4096 + 1024];

	if (mutos_path(buf, sizeof(buf), mpath) == NULL)
		artrap("a file name longer than the host takes");
	return buf;
}

/* ---- mktemp() (text 1ec2) ------------------------------------------------- */

/*
 * v7's mktemp: the process number's digits replace the trailing X's, from
 * the last one; then, as long as the name exists (access()), its first
 * digit becomes 'a', 'b', ... - "/" when 'z' is reached. The process
 * number is 16 bits, as on the 8086 (div by 10 of an unsigned).
 */
char *
ar_mktemp(char *as)
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
	while (access(ar_mpath(as), F_OK) != -1) {
		if (i == 'z')
			return "/";
		*s = (char)i++;
	}
	return as;
}

/* ---- perror() (text 17c6) ------------------------------------------------- */

/*
 * libc.a's perror: the message, ": ", the error's text, a newline - four
 * write()s to fd 2 (two when the message is empty). The error numbers are
 * v7's: the host's up to 34 (ERANGE), as the emulator maps them, else
 * EINVAL; libc.a's table has 41 texts, "Unknown error" beyond.
 */
void
ar_perror(const char *s, int err)
{
	const char *msg = "Unknown error";
	size_t n;

	if (err < 0 || err > 34)
		err = EINVAL;
	if (err < ar_nerr)
		msg = ar_errlist[err];
	if (s != NULL && (n = strlen(s)) != 0) {
		errw(s, n);
		errw(": ", 2);
	}
	errw(msg, strlen(msg));
	errw("\n", 1);
}

/*
 * v7's _flsbuf() asks isatty(fileno(stdout)) when the first character goes
 * to stdout (it makes a terminal unbuffered); libc.a's isatty() is a gtty()
 * ioctl, which fails with ENOTTY for a file or a pipe and so sets errno to
 * 25. Nothing else in ar looks at errno but wrerr()'s perror(), and a write
 * that came back short leaves it as it was (stdout and the file-size limit).
 */
void
ar_out(void)
{
	static int asked;

	if (asked)
		return;
	asked = 1;
	if (!isatty(1))
		errno = ENOTTY;
}

/* ---- ctime() (text 1840: localtime(), gmtime(), asctime()) --------------- */

/*
 * What ar's "t" with "v" prints of a time: " %-12.12s %-4.4s " of
 * ctime()'s "Day Mon dd hh:mm:ss yyyy\n" from its fourth and twentieth
 * character on - "Mon dd hh:mm" (the day with a leading blank, the hour and
 * minute with a leading zero) and the year. libc.a's localtime() takes the
 * time zone from ftime(), and gmtime() and asctime() are v7's: for every
 * time a long can hold (1901 to 2038) they give what the host's calendar
 * gives, and the year's two last digits behind "19" or, from 100 on,
 * "20" - as asctime() writes it. Under the emulator, whose ftime() says
 * UTC unless told, the host runs with TZ=UTC for the comparison; with
 * another zone the port prints that zone's time, the DST rule of the
 * original libc (v7's, a fixed pair of days) being no match for it.
 */
void
ar_ctime(int32_t t, char *mon12, char *year4)
{
	static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
	struct tm tm;
	time_t tt = (time_t)t;
	int yy;

	if (localtime_r(&tt, &tm) == NULL)
		artrap("a time the host's calendar cannot give");
	snprintf(mon12, 13, "%.3s %2d %02d:%02d", months + 3 * tm.tm_mon,
	    tm.tm_mday, tm.tm_hour, tm.tm_min);
	yy = tm.tm_year + 100;
	year4[0] = tm.tm_year >= 100 ? '2' : '1';
	year4[1] = tm.tm_year >= 100 ? '0' : '9';
	year4[2] = (char)('0' + (yy / 10) % 10);
	year4[3] = (char)('0' + yy % 10);
	year4[4] = '\0';
}
