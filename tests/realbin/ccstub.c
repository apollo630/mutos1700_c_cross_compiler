/*
 * ccstub.c - a pass that only says it was run, for tests/realbin/cccmp.py.
 *
 * cccmp.py compares the port of cc (src/mutos_cc/ccport/) with the
 * original cc running under the emulator, which records every exece()
 * with its path and arguments (cctrace.py). The port runs real host
 * processes, so cccmp.py puts a copy of this program at each pass's
 * place in a MUTOS root made for the run (lib/cpp, lib/c0, ..., bin/ld)
 * and lets it record the same:
 *
 *   - it appends a line to the file $CCSTUB_LOG: its own path (from
 *     /proc/self/exe - the copy's, so the place it was run from) and its
 *     arguments, argv[0] included, each in hexadecimal, separated by
 *     blanks;
 *   - it writes a marker, "\001CCSTUB\001\n", to stdout and to stderr, so
 *     that cccmp.py can cut cc's output into the parts before and after
 *     each pass, as the emulator's trace has it;
 *   - then it ends as $CCSTUB_STATUS says for its path (a list of
 *     "PATH=WORD" separated by ';', PATH as the line gives it, WORD a
 *     v7 wait() status: an exit status times 256, or a signal number),
 *     by default with exit status 0 - or, if $CCSTUB_RUN is set, it runs
 *     that program with its own path and its arguments: "$CCSTUB_RUN
 *     PATH ARGV0 ARGV1 ..." (cccmp.py's runner, which runs the pass for
 *     real);
 *   - a pass that ends with exit status 0 and is not run leaves the files
 *     the real one would write, each holding its argv[0] and a linefeed,
 *     so that what cc does with them afterwards (removes them, leaves
 *     them) can be compared: cpp its second argument (not "-"), c0 its
 *     second and third, c1 its third, c2 its second, as and ld the
 *     argument after "-o" (ld without one: a.out) - cccmp.py's
 *     outputs() does the same for the original's traced passes.
 *
 * Linux only (/proc/self/exe); a test tool, not part of the toolchain.
 */
#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
put_hex(FILE *f, const char *s)
{
	if (*s == '\0')
		fputs("-", f);
	for (; *s; s++)
		fprintf(f, "%02x", (unsigned char)*s);
}

/* a pass's output file, as the stub leaves it */
static void
touch(const char *path, const char *argv0)
{
	int fd;

	if (path == NULL || *path == '\0')
		return;
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0)
		return;
	if (write(fd, argv0, strlen(argv0)) >= 0)
		(void)!write(fd, "\n", 1);
	close(fd);
}

/* the argument after the first "-o", or NULL */
static const char *
after_o(int argc, char **argv)
{
	int i;

	for (i = 1; i + 1 < argc; i++)
		if (strcmp(argv[i], "-o") == 0)
			return argv[i + 1];
	return NULL;
}

/* the files the pass named argv[0] writes (cccmp.py's outputs()) */
static void
outputs(int argc, char **argv)
{
	const char *n = argv[0];

	if (strcmp(n, "cpp") == 0) {
		if (argc > 2 && strcmp(argv[2], "-") != 0)
			touch(argv[2], n);
	} else if (strcmp(n, "c0") == 0 || strcmp(n, "c0f") == 0) {
		if (argc > 3) {
			touch(argv[2], n);
			touch(argv[3], n);
		}
	} else if (strcmp(n, "c1") == 0 || strcmp(n, "c1f") == 0) {
		if (argc > 3)
			touch(argv[3], n);
	} else if (strcmp(n, "c2") == 0) {
		if (argc > 2)
			touch(argv[2], n);
	} else if (strcmp(n, "as") == 0 || strcmp(n, "asf") == 0) {
		touch(after_o(argc, argv), n);
	} else if (strcmp(n, "ld") == 0) {
		const char *o = after_o(argc, argv);

		touch(o != NULL ? o : "a.out", n);
	}
}

/* the status word $CCSTUB_STATUS gives for path, or -1 */
static long
scripted(const char *path)
{
	const char *e = getenv("CCSTUB_STATUS");
	size_t n = strlen(path);

	while (e != NULL && *e) {
		const char *end = strchr(e, ';');
		size_t len = end ? (size_t)(end - e) : strlen(e);

		if (len > n && strncmp(e, path, n) == 0 && e[n] == '=')
			return strtol(e + n + 1, NULL, 0);
		e = end ? end + 1 : NULL;
	}
	return -1;
}

int
main(int argc, char **argv)
{
	static const char marker[] = "\001CCSTUB\001\n";
	char self[4096];
	const char *log = getenv("CCSTUB_LOG");
	const char *run = getenv("CCSTUB_RUN");
	ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
	long st;
	int i;

	if (n < 0)
		return 125;
	self[n] = '\0';
	if (log != NULL) {
		FILE *f = fopen(log, "a");

		if (f == NULL)
			return 125;
		put_hex(f, self);
		for (i = 0; i < argc; i++) {
			fputc(' ', f);
			put_hex(f, argv[i]);
		}
		fputc('\n', f);
		fclose(f);
	}
	if (write(1, marker, sizeof(marker) - 1) < 0
	    || write(2, marker, sizeof(marker) - 1) < 0)
		return 125;
	st = scripted(self);
	if (st >= 0) {
		if (st & 0x7f) {
			signal((int)(st & 0x7f), SIG_DFL);
			kill(getpid(), (int)(st & 0x7f));
			return 125;
		}
		if (((st >> 8) & 0xff) != 0)
			return (int)((st >> 8) & 0xff);
	}
	if (st < 0 && run != NULL && *run) {
		char **av = calloc((size_t)argc + 3, sizeof(*av));

		if (av == NULL)
			return 125;
		av[0] = (char *)run;
		av[1] = self;
		for (i = 0; i < argc; i++)
			av[2 + i] = argv[i];
		execv(run, av);
		return 125;
	}
	if (argc > 0)
		outputs(argc, argv);
	return 0;
}
