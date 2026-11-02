/*
 * dosys.c - running the commands: v7's dosys.c, which the original MUTOS
 * 1700 make (text 1cb8..1f40, 7 functions) has byte for byte. The offset of
 * each function in the original's text is in its comment.
 *
 * A command goes to the shell when it has a character the shell treats
 * specially (funny[] & META), else it is split at its blanks and run
 * directly through execvp(). Both are the host's here: /bin/sh, and a
 * PATH that has the MUTOS root's bin/ first, where the tools of the port
 * (cc, as, ld, ar, ranlib, make) are (mk_setpath()).
 */
#include "make.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* 1cb8 */
mint
dosys(char *comstring, mint nohalt)
{
	mint status;

	if (metas(comstring))
		status = doshell(comstring, nohalt);
	else
		status = doexec(comstring);

	return status;
}

/* Are there any Shell meta-characters? The string is scanned up to its end
 * (funny['\0'] has META) and the character found comes back. 1ce6 */
mint
metas(const char *s)
{
	char c;

	while ((mk_funny(c = *s++) & META) == 0)
		;
	return c;
}

/* 1d0a */
mint
doshell(char *comstring, mint nohalt)
{
	pid_t pid = fork();

	if (pid < 0)
		mk_trap("fork() failed");
	waitpid_ = (mint)pid;
	if (pid == 0) {
		enbint(SIG_DFL);
		doclose();

		execl(SHELLCOM, "sh", (nohalt ? "-c" : "-ce"), comstring,
		    (char *)NULL);
		fatal("Couldn't load Shell");
	}

	return await();
}

/* The status is the word wait() gives, 16 bits: the exit status in the high
 * byte, the signal (and 0200 for a core dump) in the low. 1d5a */
mint
await(void)
{
	int status;
	pid_t pid;

	enbint(SIG_IGN);
	while ((pid = wait(&status)) != (pid_t)waitpid_)
		if (pid == -1)
			fatal("bad wait code");
	waitpid_ = 0;
	enbint(intrupt);
	return (mint)status;
}

/* Close open directory files before exec'ing. The directories make keeps
 * open are numbers (mk_diropen()), not descriptors of this process: there is
 * nothing to close. 1da2 */
void
doclose(void)
{
}

/* 1dc6 */
mint
doexec(char *str)
{
	char *t;
	char *argv[200];
	char **p;
	pid_t pid;

	while (*str == ' ' || *str == '\t')
		++str;
	if (*str == '\0')
		return -1;	/* no command */

	p = argv;
	for (t = str; *t;) {
		if (p >= argv + 199)
			/* the 200th pointer is the NULL; one more is written over
			 * the frame of the original */
			mk_trap("a command of more than 199 words");
		*p++ = t;
		while (*t != ' ' && *t != '\t' && *t != '\0')
			++t;
		if (*t)
			for (*t++ = '\0'; *t == ' ' || *t == '\t'; ++t)
				;
	}

	*p = NULL;

	mk_enter(412);
	pid = fork();
	if (pid < 0)
		mk_trap("fork() failed");
	waitpid_ = (mint)pid;
	if (pid == 0) {
		enbint(SIG_DFL);
		doclose();
		enbint(intrupt);
		execvp(str, argv);
		fatal1("Cannot load %s", str);
	}
	mk_leave(412);

	return await();
}

/* 1e72. Update the time of a file by writing its first byte over itself,
 * or make it. */
void
touch(mint force, char *name)
{
	struct stat stbuff;
	char junk[1];
	int fd;

	mk_checkname(name);
	if (stat(MKPATH(name), &stbuff) < 0) {
		if (force)
			goto create;
		else {
			mk_eprintf("touch: file %s does not exist.\n", name);
			return;
		}
	}

	if (stbuff.st_size == 0)
		goto create;

	if ((fd = open(MKPATH(name), O_RDWR)) < 0)
		goto bad;

	if (read(fd, junk, 1) < 1) {
		close(fd);
		goto bad;
	}
	lseek(fd, 0L, SEEK_SET);
	if (write(fd, junk, 1) < 1) {
		close(fd);
		goto bad;
	}
	close(fd);
	return;

bad:
	mk_eprintf("Cannot touch %s\n", name);
	return;

create:
	if ((fd = creat(MKPATH(name), 0666)) < 0)
		goto bad;
	close(fd);
}
