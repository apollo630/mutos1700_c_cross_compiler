/*
 * ar.c - mutos_ar, the MUTOS 1700 archiver: a port of the original ar.
 *
 * v7's ar.c (v7/ar/ar.c) as the original binary has it - the functions
 * in v7's order, each with the text offset of the original's
 * counterpart in its comment (tests/realbin/armap.txt). Read instruction
 * by instruction beside v7's source, none of the 33 functions of ar.o
 * differs from it in what it does; what differs is the representation
 * (ar.h: the header as 26 bytes, signed chars, the 32-bit struct stat
 * fields) and the libc.a underneath (arlib.c).
 *
 * The port reproduces the original's behaviour where it is odd: "ar r"
 * copies the old member when a named file cannot be opened and says so
 * only when names were given; a file named twice on the command line may
 * be put into the archive twice (the manual's FEHLERQUELLEN); names are
 * trimmed of trailing slashes in place, so a member's name is the last
 * component, cut at 14 characters, while "match" compares with the
 * untruncated command-line name (a longer name never matches and is
 * always added); the option counters are chars that wrap; "ar t" prints
 * uid and gid as signed chars.
 */
#define _POSIX_C_SOURCE 200809L	/* sigaction-free signal(), fstat() */

#include "ar.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define	SKIP	1
#define	IODD	2
#define	OODD	4
#define	HEAD	8

/* v7's struct stat as the original's fstat() fills it: what ar uses */
static struct {
	int32_t	size;
	int32_t	mtime;
	uint16_t mode;
	uint16_t uid;
	uint16_t gid;
} stbuf;

static uint8_t	arbuf[AR_HDR];		/* struct ar_hdr arbuf */

static char	*man = "mrxtdpq";
static char	*opt = "uvnbail";

static int	signum[] = { SIGHUP, SIGINT, SIGQUIT, 0 };
static int	(*comfun)(void);
static signed char flg[26];		/* the original's chars are signed */
static char	**namv;
static int	namc;
static char	*arnam;
static char	*ponam;
/* the temporary names, mktemp()ed in place: MUTOS paths (arlib.c) */
static char	tmpnam_d[]	= "/tmp/vXXXXX";
static char	tmp1nam_d[]	= "/tmp/v1XXXXX";
static char	tmp2nam_d[]	= "/tmp/v2XXXXX";
static char	tmpnam_l[]	= "vXXXXX";
static char	tmp1nam_l[]	= "v1XXXXX";
static char	tmp2nam_l[]	= "v2XXXXX";
static char	*tmpnam_	= tmpnam_d;
static char	*tmp1nam	= tmp1nam_d;
static char	*tmp2nam	= tmp2nam_d;
static char	*tfnam;
static char	*tf1nam;
static char	*tf2nam;
static char	*file;
static char	name[16];
static int	af;
static int	tf;
static int	tf1;
static int	tf2;
static int	qf;
static int	bastate;
static uint8_t	buf[512];

#define	ARSIZE()	ar_getlong(arbuf + AR_SIZE)
#define	ARDATE()	ar_getlong(arbuf + AR_DATE)

static void	usage(void);
static void	setcom(int (*fun)(void));
static int	rcmd(void);
static int	dcmd(void);
static int	xcmd(void);
static int	tcmd(void);
static int	pcmd(void);
static int	mcmd(void);
static int	qcmd(void);
static void	init(void);
static int	getaf(void);
static void	getqf(void);
static void	noar(void);
static void	sigdone(int);
static void	done(int);
static int	notfound(void);
static int	morefil(void);
static void	cleanup(void);
static void	install(void);
static void	movefil(int);
static int	stats(void);
static void	copyfil(int, int, int);
static int	getdir(void);
static int	match(void);
static void	bamatch(void);
static void	phserr(void);
static void	mesg(int);
static char	*trim(char *);
static void	longt(void);
static void	pmode(void);
static void	select_(const int *);
static void	wrerr(void);

/* the MUTOS path of a temporary file, in the three calls ar makes of it */
static int
m_creat(const char *mp, int mode)
{
	return creat(ar_mpath(mp), (mode_t)mode);
}

static int
m_open(const char *mp, int how)
{
	return open(ar_mpath(mp), how);
}

/* an opened directory: the original reads its entries (artrap) */
static void
nodir(int fd)
{
	struct stat st;

	if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode))
		artrap("an open()ed directory - the original reads its entries");
}

/* the temporary files away, for a refusal (artrap): done() without exit */
static void
rmtmp(void)
{
	if (tfnam)
		unlink(ar_mpath(tfnam));
	if (tf1nam)
		unlink(ar_mpath(tf1nam));
	if (tf2nam)
		unlink(ar_mpath(tf2nam));
}

/* main (text 00b6): v7's, the option switch a jump table at 0179 */
int
main(int argc, char **argv)
{
	int i;
	char *cp;

	ar_trapcleanup = rmtmp;
	for (i = 0; signum[i]; i++)
		if (signal(signum[i], SIG_IGN) != SIG_IGN)
			signal(signum[i], sigdone);
	if (argc < 3)
		usage();
	cp = argv[1];
	for (cp = argv[1]; *cp; cp++)
		switch (*cp) {
		case 'l':
		case 'v':
		case 'u':
		case 'n':
		case 'a':
		case 'b':
		case 'c':
		case 'i':
			flg[*cp - 'a']++;
			continue;

		case 'r':
			setcom(rcmd);
			continue;

		case 'd':
			setcom(dcmd);
			continue;

		case 'x':
			setcom(xcmd);
			continue;

		case 't':
			setcom(tcmd);
			continue;

		case 'p':
			setcom(pcmd);
			continue;

		case 'm':
			setcom(mcmd);
			continue;

		case 'q':
			setcom(qcmd);
			continue;

		default:
			fprintf(stderr, "ar: bad option `%c'\n", *cp);
			done(1);
		}
	if (flg['l' - 'a']) {
		tmpnam_ = tmpnam_l;
		tmp1nam = tmp1nam_l;
		tmp2nam = tmp2nam_l;
	}
	if (flg['i' - 'a'])
		flg['b' - 'a']++;
	if (flg['a' - 'a'] || flg['b' - 'a']) {
		bastate = 1;
		ponam = trim(argv[2]);
		argv++;
		argc--;
		if (argc < 3)
			usage();
	}
	arnam = argv[2];
	namv = argv + 3;
	namc = argc - 3;
	if (comfun == 0) {
		if (flg['u' - 'a'] == 0) {
			fprintf(stderr, "ar: one of [%s] must be specified\n", man);
			done(1);
		}
		setcom(rcmd);
	}
	(*comfun)();
	done(notfound());
	return 0;
}

/* setcom (026a) */
static void
setcom(int (*fun)(void))
{
	if (comfun != 0) {
		fprintf(stderr, "ar: only one of [%s] allowed\n", man);
		done(1);
	}
	comfun = fun;
}

/* rcmd (029c) */
static int
rcmd(void)
{
	int f;

	init();
	getaf();
	while (!getdir()) {
		bamatch();
		if (namc == 0 || match()) {
			f = stats();
			if (f < 0) {
				if (namc)
					fprintf(stderr, "ar: cannot open %s\n", file);
				goto cp;
			}
			if (flg['u' - 'a'])
				if (stbuf.mtime <= ARDATE()) {
					close(f);
					goto cp;
				}
			mesg('r');
			copyfil(af, -1, IODD + SKIP);
			movefil(f);
			continue;
		}
	cp:
		mesg('c');
		copyfil(af, tf, IODD + OODD + HEAD);
	}
	cleanup();
	return 0;
}

/* dcmd (0352) */
static int
dcmd(void)
{
	init();
	if (getaf())
		noar();
	while (!getdir()) {
		if (match()) {
			mesg('d');
			copyfil(af, -1, IODD + SKIP);
			continue;
		}
		mesg('c');
		copyfil(af, tf, IODD + OODD + HEAD);
	}
	install();
	return 0;
}

/* xcmd (03ae) */
static int
xcmd(void)
{
	int f;

	if (getaf())
		noar();
	while (!getdir()) {
		if (namc == 0 || match()) {
			f = creat(file, (mode_t)((arbuf[AR_MODE] |
			    (arbuf[AR_MODE + 1] << 8)) & 0777));
			if (f < 0) {
				fprintf(stderr, "ar: %s cannot create\n", file);
				goto sk;
			}
			mesg('x');
			copyfil(af, f, IODD);
			close(f);
			continue;
		}
	sk:
		mesg('c');
		copyfil(af, -1, IODD + SKIP);
		if (namc > 0 && !morefil())
			done(0);
	}
	return 0;
}

/* pcmd (045c) */
static int
pcmd(void)
{
	if (getaf())
		noar();
	while (!getdir()) {
		if (namc == 0 || match()) {
			if (flg['v' - 'a']) {
				ar_out();
				printf("\n<%s>\n\n", file);
				fflush(stdout);
			}
			copyfil(af, 1, IODD);
			continue;
		}
		copyfil(af, -1, IODD + SKIP);
	}
	return 0;
}

/* mcmd (04c4) */
static int
mcmd(void)
{
	init();
	if (getaf())
		noar();
	tf2nam = ar_mktemp(tmp2nam);
	close(m_creat(tf2nam, 0600));
	tf2 = m_open(tf2nam, 2);
	if (tf2 < 0) {
		fprintf(stderr, "ar: cannot create third temp\n");
		done(1);
	}
	while (!getdir()) {
		bamatch();
		if (match()) {
			mesg('m');
			copyfil(af, tf2, IODD + OODD + HEAD);
			continue;
		}
		mesg('c');
		copyfil(af, tf, IODD + OODD + HEAD);
	}
	install();
	return 0;
}

/* tcmd (0570) */
static int
tcmd(void)
{
	if (getaf())
		noar();
	while (!getdir()) {
		if (namc == 0 || match()) {
			if (flg['v' - 'a'])
				longt();
			ar_out();
			printf("%s\n", trim(file));
		}
		copyfil(af, -1, IODD + SKIP);
	}
	return 0;
}

/* qcmd (05ca) */
static int
qcmd(void)
{
	int i, f;

	if (flg['a' - 'a'] || flg['b' - 'a']) {
		fprintf(stderr, "ar: abi not allowed with q\n");
		done(1);
	}
	getqf();
	for (i = 0; signum[i]; i++)
		signal(signum[i], SIG_IGN);
	lseek(qf, 0, SEEK_END);
	for (i = 0; i < namc; i++) {
		file = namv[i];
		if (file == 0)
			continue;
		namv[i] = 0;
		mesg('q');
		f = stats();
		if (f < 0) {
			fprintf(stderr, "ar: %s cannot open\n", file);
			continue;
		}
		tf = qf;
		movefil(f);
		qf = tf;
	}
	return 0;
}

/* init (069a): the magic number into the first temporary file */
static void
init(void)
{
	static uint8_t mbuf[2] = { ARMAG & 0xff, (ARMAG >> 8) & 0xff };

	tfnam = ar_mktemp(tmpnam_);
	close(m_creat(tfnam, 0600));
	tf = m_open(tfnam, 2);
	if (tf < 0) {
		fprintf(stderr, "ar: cannot create temp file\n");
		done(1);
	}
	if (write(tf, mbuf, 2) != 2)
		wrerr();
}

/* getaf (070a): 1 when the archive cannot be opened */
static int
getaf(void)
{
	uint8_t mbuf[2];

	af = open(arnam, 0);
	if (af < 0)
		return 1;
	nodir(af);
	if (read(af, mbuf, 2) != 2 || (mbuf[0] | (mbuf[1] << 8)) != ARMAG) {
		fprintf(stderr, "ar: %s not in archive format\n", arnam);
		done(1);
	}
	return 0;
}

/* getqf (076e) */
static void
getqf(void)
{
	uint8_t mbuf[2];

	if ((qf = open(arnam, 2)) < 0) {
		if (!flg['c' - 'a'])
			fprintf(stderr, "ar: creating %s\n", arnam);
		close(creat(arnam, 0666));
		if ((qf = open(arnam, 2)) < 0) {
			fprintf(stderr, "ar: cannot create %s\n", arnam);
			done(1);
		}
		mbuf[0] = ARMAG & 0xff;
		mbuf[1] = (ARMAG >> 8) & 0xff;
		if (write(qf, mbuf, 2) != 2)
			wrerr();
	}
	else if (read(qf, mbuf, 2) != 2 || (mbuf[0] | (mbuf[1] << 8)) != ARMAG) {
		fprintf(stderr, "ar: %s not in archive format\n", arnam);
		done(1);
	}
}

/* usage (084c): printed to the standard output */
static void
usage(void)
{
	ar_out();
	printf("usage: ar [%s][%s] archive files ...\n", opt, man);
	done(1);
}

/* noar (0870) */
static void
noar(void)
{
	fprintf(stderr, "ar: %s does not exist\n", arnam);
	done(1);
}

/* sigdone (0894) */
static void
sigdone(int sig)
{
	(void)sig;
	done(100);
}

/* done (08a6) */
static void
done(int c)
{
	rmtmp();
	exit(c);
}

/* notfound (08ea) */
static int
notfound(void)
{
	int i, n;

	n = 0;
	for (i = 0; i < namc; i++)
		if (namv[i]) {
			fprintf(stderr, "ar: %s not found\n", namv[i]);
			n++;
		}
	return n;
}

/* morefil (0928) */
static int
morefil(void)
{
	int i, n;

	n = 0;
	for (i = 0; i < namc; i++)
		if (namv[i])
			n++;
	return n;
}

/* cleanup (094e) */
static void
cleanup(void)
{
	int i, f;

	for (i = 0; i < namc; i++) {
		file = namv[i];
		if (file == 0)
			continue;
		namv[i] = 0;
		mesg('a');
		f = stats();
		if (f < 0) {
			fprintf(stderr, "ar: %s cannot open\n", file);
			continue;
		}
		movefil(f);
	}
	install();
}

/* copy the temporary file(s) over the archive: install (09b2) */
static void
install(void)
{
	int i;

	for (i = 0; signum[i]; i++)
		signal(signum[i], SIG_IGN);
	if (af < 0)
		if (!flg['c' - 'a'])
			fprintf(stderr, "ar: creating %s\n", arnam);
	close(af);
	af = creat(arnam, 0666);
	if (af < 0) {
		fprintf(stderr, "ar: cannot create %s\n", arnam);
		done(1);
	}
	if (tfnam) {
		lseek(tf, 0, SEEK_SET);
		while ((i = (int)read(tf, buf, 512)) > 0)
			if (write(af, buf, (size_t)i) != i)
				wrerr();
	}
	if (tf2nam) {
		lseek(tf2, 0, SEEK_SET);
		while ((i = (int)read(tf2, buf, 512)) > 0)
			if (write(af, buf, (size_t)i) != i)
				wrerr();
	}
	if (tf1nam) {
		lseek(tf1, 0, SEEK_SET);
		while ((i = (int)read(tf1, buf, 512)) > 0)
			if (write(af, buf, (size_t)i) != i)
				wrerr();
	}
}

/*
 * insert the file 'file' into the temporary file: movefil (0b0e). The
 * header's fields come from the 32-bit struct stat: uid and gid cut to a
 * char, the mode a word.
 */
static void
movefil(int f)
{
	char *cp;
	int i;

	cp = trim(file);
	for (i = 0; i < 14; i++)
		if ((arbuf[AR_NAME + i] = (uint8_t)*cp) != 0)
			cp++;
	ar_putlong(arbuf + AR_SIZE, stbuf.size);
	ar_putlong(arbuf + AR_DATE, stbuf.mtime);
	arbuf[AR_UID] = (uint8_t)stbuf.uid;
	arbuf[AR_GID] = (uint8_t)stbuf.gid;
	arbuf[AR_MODE] = (uint8_t)(stbuf.mode & 0xff);
	arbuf[AR_MODE + 1] = (uint8_t)(stbuf.mode >> 8);
	copyfil(f, tf, OODD + HEAD);
	close(f);
}

/* stats (0b84): the open file, or -1 */
static int
stats(void)
{
	struct stat st;
	int f;

	f = open(file, 0);
	if (f < 0)
		return f;
	if (fstat(f, &st) < 0) {
		close(f);
		return -1;
	}
	if (S_ISDIR(st.st_mode))
		artrap("an open()ed directory - the original reads its entries");
	stbuf.size = (int32_t)(uint32_t)st.st_size;
	stbuf.mtime = (int32_t)(uint32_t)st.st_mtime;
	stbuf.mode = (uint16_t)st.st_mode;
	stbuf.uid = (uint16_t)st.st_uid;
	stbuf.gid = (uint16_t)st.st_gid;
	return f;
}

/*
 * copy next file, size given in arbuf: copyfil (0bc0). arbuf's size is
 * counted down in place (the callers do not use it afterwards); a read
 * that returns less than asked is a phase error, and the buffer - the
 * old contents of its end - is written all the same.
 */
static void
copyfil(int fi, int fo, int flag)
{
	int i, o;
	uint16_t pe;		/* the original's int: 65536 short reads are none */

	if (flag & HEAD)
		if (write(fo, arbuf, AR_HDR) != AR_HDR)
			wrerr();
	pe = 0;
	while (ARSIZE() > 0) {
		i = o = 512;
		if (ARSIZE() < i) {
			i = o = (int)ARSIZE();
			if (i & 1) {
				if (flag & IODD)
					i++;
				if (flag & OODD)
					o++;
			}
		}
		if (read(fi, buf, (size_t)i) != i)
			pe++;
		if ((flag & SKIP) == 0)
			if (write(fo, buf, (size_t)o) != o)
				wrerr();
		ar_putlong(arbuf + AR_SIZE, ARSIZE() - 512);
	}
	if (pe)
		phserr();
}

/* getdir (0c80): the next header, or 1 at the end (a short one too) */
static int
getdir(void)
{
	int i;

	i = (int)read(af, arbuf, AR_HDR);
	if (i != AR_HDR) {
		if (tf1nam) {
			i = tf;
			tf = tf1;
			tf1 = i;
		}
		return 1;
	}
	for (i = 0; i < 14; i++)
		name[i] = (char)arbuf[AR_NAME + i];
	file = name;
	return 0;
}

/* match (0cd8) */
static int
match(void)
{
	int i;

	for (i = 0; i < namc; i++) {
		if (namv[i] == 0)
			continue;
		if (strcmp(trim(namv[i]), file) == 0) {
			file = namv[i];
			namv[i] = 0;
			return 1;
		}
	}
	return 0;
}

/* bamatch (0d3c): the position given by a, b or i */
static void
bamatch(void)
{
	int f;

	switch (bastate) {

	case 1:
		if (strcmp(file, ponam) != 0)
			return;
		bastate = 2;
		if (flg['a' - 'a'])
			return;
		/* FALLTHROUGH */

	case 2:
		bastate = 0;
		tf1nam = ar_mktemp(tmp1nam);
		close(m_creat(tf1nam, 0600));
		f = m_open(tf1nam, 2);
		if (f < 0) {
			fprintf(stderr, "ar: cannot create second temp\n");
			return;
		}
		tf1 = tf;
		tf = f;
	}
}

/* phserr (0dd6) */
static void
phserr(void)
{
	fprintf(stderr, "ar: phase error on %s\n", file);
}

/* mesg (0df0): "v" prints it, "vv" also the copies ('c') */
static void
mesg(int c)
{
	if (flg['v' - 'a'])
		if (c != 'c' || flg['v' - 'a'] > 1) {
			ar_out();
			printf("%c - %s\n", c, file);
		}
}

/* trim (0e1e): the last component; trailing slashes cut off in place */
static char *
trim(char *s)
{
	char *p1, *p2;

	for (p1 = s; *p1; p1++)
		;
	while (p1 > s) {
		if (*--p1 != '/')
			break;
		*p1 = 0;
	}
	p2 = s;
	for (p1 = s; *p1; p1++)
		if (*p1 == '/')
			p2 = p1 + 1;
	return p2;
}

#define	ISARG	01000
#define	SUID	04000
#define	SGID	02000
#define	ROWN	0400
#define	WOWN	0200
#define	XOWN	0100
#define	RGRP	040
#define	WGRP	020
#define	XGRP	010
#define	ROTH	04
#define	WOTH	02
#define	XOTH	01
#define	STXT	01000

/* longt (0e58): the long listing's first columns; uid and gid as chars */
static void
longt(void)
{
	char mon12[16], year4[8];

	pmode();
	printf("%3d/%1d", (int)(signed char)arbuf[AR_UID],
	    (int)(signed char)arbuf[AR_GID]);
	printf("%7ld", (long)ARSIZE());
	ar_ctime(ARDATE(), mon12, year4);
	printf(" %-12.12s %-4.4s ", mon12, year4);
}

static const int m1[] = { 1, ROWN, 'r', '-' };
static const int m2[] = { 1, WOWN, 'w', '-' };
static const int m3[] = { 2, SUID, 's', XOWN, 'x', '-' };
static const int m4[] = { 1, RGRP, 'r', '-' };
static const int m5[] = { 1, WGRP, 'w', '-' };
static const int m6[] = { 2, SGID, 's', XGRP, 'x', '-' };
static const int m7[] = { 1, ROTH, 'r', '-' };
static const int m8[] = { 1, WOTH, 'w', '-' };
static const int m9[] = { 2, STXT, 't', XOTH, 'x', '-' };

static const int *const m[] = { m1, m2, m3, m4, m5, m6, m7, m8, m9 };

/* pmode (0eac) */
static void
pmode(void)
{
	const int *const *mp;

	for (mp = &m[0]; mp < &m[9];)
		select_(*mp++);
}

/* select (0eca): the first bit of the mode that the pair has, else the last */
static void
select_(const int *pairp)
{
	int n;
	const int *ap;
	int mode = arbuf[AR_MODE] | (arbuf[AR_MODE + 1] << 8);

	ap = pairp;
	n = *ap++;
	while (--n >= 0 && (mode & *ap++) == 0)
		ap++;
	ar_out();
	putchar(*ap);
}

/* wrerr (0f16) */
static void
wrerr(void)
{
	ar_perror("ar write error", errno);
	done(1);
}
