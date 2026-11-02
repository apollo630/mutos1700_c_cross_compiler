/*
 * strip.c - mutos_strip, the MUTOS 1700 symbol stripper: a port of the
 * original strip.
 *
 * v7's strip.c (v7/strip/strip.c) as the original binary has it - three
 * functions: main() at text 00b6, strip() at 016e and copy() at 02ce
 * (tests/realbin/stripmap.txt). Read instruction by instruction beside v7's
 * source, they do what v7's do; what differs is the representation (the
 * a.out header as bytes, the words 16 bits wide, the sizes longs of 32 bits)
 * and the libc.a underneath.
 *
 * What strip does: for every file named on the command line it reads the
 * 16 bytes of an a.out header; if the magic number is 0407, 0410, 0411 or
 * 0405 and the file has symbols (or its relocation is not stripped yet) it
 * writes the header with a_syms = 0 and the "relocation stripped" bit set,
 * followed by the text and the data, to the temporary file /tmp/sXXXXX
 * (mktemp(), one for the whole run), and then copies that back over the
 * file - creat() truncates it first. A file that cannot be opened ("cannot
 * open x"), has another magic number ("x not in a.out format"), is stripped
 * already ("x already stripped") or is shorter than its header says ("x
 * unexpected eof") is reported on standard output - strip has no standard
 * error - and the run goes on with the next file; "x cannot recreate" is a
 * file that creat() will not truncate. A write that fails while the file is
 * copied back ends the run (status 2) with the file cut.
 *
 * The exit status is v7's `status`: 0, 1 if any file went wrong, 2 for the
 * write that failed.
 *
 * What the port keeps of the original, where it is odd:
 *  - the header `head` is a global: a short read (a file of less than 16
 *    bytes, an empty one) leaves the rest of the bytes of the file before
 *    it - after a file that was stripped, a_syms 0 and the flag set: a
 *    file that begins with a magic number and nothing more "is already
 *    stripped" -, and the first file finds zeros;
 *  - hangup, interrupt and quit are ignored from the start;
 *  - the temporary file's name: the process number's five digits (16 bits,
 *    as on the 8086), the first digit 'a', 'b', ... as long as the name
 *    exists, "/" at 'z' - then the creat() and the open() fail:
 *    "cannot create temp file", status 2. The MUTOS path /tmp/sXXXXX is the
 *    MUTOS root's tmp/ (src/h/mutos_root.h); the command line's names are
 *    host paths;
 *  - the file is copied in 512-byte pieces, the last one the rest.
 *
 * What the port refuses (mu_trap(): exit status 125, the last line of the
 * messages ending in "refused"), where the original depends on state that
 * the port does not model:
 *  - a file name that is a directory (the original reads its entries).
 */
#define _POSIX_C_SOURCE 200809L	/* fileno(), signal() */

#define MU_TOOL "strip"
#include "../h/mutos_stdio.h"
#include "../h/mutos_root.h"

#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <sys/stat.h>

#define EXEC	16		/* sizeof(struct exec) */
#define A_MAGIC1	0407
#define A_MAGIC2	0410
#define A_MAGIC3	0411
#define A_MAGIC4	0405

/* the header's words, little-endian: offsets of struct exec */
#define H_MAGIC	0
#define H_TEXT	2
#define H_DATA	4
#define H_SYMS	8
#define H_FLAG	14

/* the globals of the original, in its data (stripmap.txt): zero at the start */
static const char *tname;		/* 137c */
static uint8_t head[EXEC];		/* 137e: struct exec head */
static const unsigned a_magic[] = { A_MAGIC1, A_MAGIC2, A_MAGIC3, A_MAGIC4, 0 };	/* 11f6 */
static int status;			/* 138e */
static int tf;				/* 1390 */

static unsigned
w16(const uint8_t *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* the MUTOS path as a host path (a name longer than the host takes: refused) */
static const char *
st_mpath(const char *mpath)
{
	static char buf[4096 + 1024];

	if (mutos_path(buf, sizeof(buf), mpath) == NULL)
		mu_trap("a file name longer than the host takes");
	return buf;
}

/*
 * mktemp() (libc.a text 0e2a): the process number's digits replace the
 * trailing X's, from the last one; then, as long as the name exists
 * (access()), its first digit becomes 'a', 'b', ... - "/" when 'z' is
 * reached. The process number is 16 bits, as on the 8086 (div by 10 of an
 * unsigned).
 */
static const char *
st_mktemp(char *as)
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
	while (access(st_mpath(as), F_OK) != -1) {
		if (i == 'z')
			return "/";
		*s = (char)i++;
	}
	return as;
}

/* open(name, 0): a directory opens in the original and reads its entries */
static int
st_open(const char *name)
{
	int fd = open(name, O_RDONLY);
	struct stat st;

	if (fd >= 0 && fstat(fd, &st) == 0 && S_ISDIR(st.st_mode))
		mu_trap("a directory named as a file - the original reads its entries");
	return fd;
}

/* copy (text 02ce): size bytes from fr to to, in pieces of 512; 1 if one fails */
static int
copy(const char *name, int fr, int to, int32_t size)
{
	uint8_t buf[512];
	int s;
	ssize_t n;

	while (size != 0) {
		s = 512;
		if (size < 512)
			s = (int)size;
		n = read(fr, buf, (size_t)s);
		if (n != s) {
			mu_printf("%s unexpected eof\n", name);
			return 1;
		}
		n = write(to, buf, (size_t)s);
		if (n != s) {
			mu_printf("%s unexpected write eof\n", name);
			return 1;
		}
		size -= s;
	}
	return 0;
}

/* strip (text 016e) */
static void
strip(const char *name)
{
	int f;
	int32_t size;
	int i;
	ssize_t r;

	f = st_open(name);
	if (f < 0) {
		mu_printf("cannot open %s\n", name);
		status = 1;
		goto out;
	}
	r = read(f, head, EXEC);	/* a short read leaves the old bytes */
	(void)r;
	for (i = 0; a_magic[i]; i++)
		if (a_magic[i] == w16(head + H_MAGIC))
			break;
	if (a_magic[i] == 0) {
		mu_printf("%s not in a.out format\n", name);
		status = 1;
		goto out;
	}
	if (w16(head + H_SYMS) == 0 && (w16(head + H_FLAG) & 1) != 0) {
		mu_printf("%s already stripped\n", name);
		goto out;
	}
	size = (int32_t)((uint32_t)w16(head + H_TEXT) + w16(head + H_DATA));
	head[H_SYMS] = head[H_SYMS + 1] = 0;
	head[H_FLAG] |= 1;

	(void)lseek(tf, 0, SEEK_SET);
	r = write(tf, head, EXEC);	/* the result is not looked at */
	(void)r;
	if (copy(name, f, tf, size)) {
		status = 1;
		goto out;
	}
	size += EXEC;
	close(f);
	f = creat(name, 0666);
	if (f < 0) {
		mu_printf("%s cannot recreate\n", name);
		status = 1;
		goto out;
	}
	(void)lseek(tf, 0, SEEK_SET);
	if (copy(name, tf, f, size))
		status = 2;

out:
	close(f);
}

/* main (text 00b6) */
int
main(int argc, char **argv)
{
	int i;
	char tmpl[] = "/tmp/sXXXXX";

	signal(SIGHUP, SIG_IGN);
	signal(SIGINT, SIG_IGN);
	signal(SIGQUIT, SIG_IGN);
	tname = st_mktemp(tmpl);
	close(creat(st_mpath(tname), 0600));
	tf = open(st_mpath(tname), O_RDWR);
	if (tf < 0) {
		mu_printf("cannot create temp file\n");
		mu_exit(2);
	}
	for (i = 1; i < argc; i++) {
		strip(argv[i]);
		if (status > 1)
			break;
	}
	close(tf);
	unlink(st_mpath(tname));
	mu_exit(status);
}
