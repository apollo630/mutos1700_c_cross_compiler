/*
 * files.c - the file system: v7's files.c, which the original MUTOS 1700
 * make (text 157a..1cb8, 11 functions) has byte for byte but for lookarch(),
 * whose struct nlist has `char n_type; char n_other;` in MUTOS's a.out.h
 * where v7 has `int n_type` - the same 12 bytes, and no difference in what
 * the function does (n_type & N_EXT and n_type & ~N_EXT are zero or not
 * either way). The offset of each function in the original's text is in its
 * comment. builtin[] of the original's files.c is makedata.c's.
 *
 * The directory of a pattern is read the way a MUTOS directory looks to a
 * program (mklib.c, mk_dirread()): "." and "..", then the rest by byte
 * order. The archive a(b) / a((b)) is read from its bytes: the header
 * (struct ar_hdr, 26 bytes, the long ar_date high word first), the
 * a.out header of an object (16 bytes) and its symbols (12 bytes each).
 */
#include "make.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ar.h and a.out.h of MUTOS 1700 */
#define ARMAG 0177545			/* the first word of an archive: 65 ff */
#define AR_HDR 26			/* sizeof(struct ar_hdr) */
#define AR_NAME 0			/* char ar_name[14] */
#define AR_DATE 14			/* long ar_date */
#define AR_SIZE 22			/* long ar_size */
#define AOUT_HDR 16			/* sizeof(struct exec) */
#define NLIST_SIZE 12			/* sizeof(struct nlist) */
#define N_EXT 040
#define A_MAGIC1 0407
#define A_MAGIC2 0410
#define A_MAGIC3 0411
#define A_MAGIC4 0405

/* a v7 long as it lies in memory and in a file: the high word first */
static int32_t
getlong(const uint8_t *p)
{
	return (int32_t)(((uint32_t)(p[0] | (p[1] << 8)) << 16) |
	    (uint32_t)(p[2] | (p[3] << 8)));
}

static uint16_t
getword(const uint8_t *p)
{
	return (uint16_t)(p[0] | (p[1] << 8));
}

/* MUTOS cuts a name to 14 characters in each part of a path (DIRSIZ); the
 * host does not, so a longer name is another file here than there */
void
mk_checkname(const char *name)
{
	const char *p = name;

	while (*p) {
		size_t n = strcspn(p, "/");

		if (n > 14)
			mk_trap("a file name of more than 14 characters in a "
			    "path (MUTOS cuts it)");
		p += n;
		if (*p == '/')
			p++;
	}
}

/* 157a */
TIMETYPE
exists(char *filename)
{
	struct stat buf;
	char *s;

	for (s = filename; *s != '\0' && *s != '('; ++s)
		;

	if (*s == '(')
		return lookarch(filename);

	mk_checkname(filename);
	if (stat(MKPATH(filename), &buf) < 0)
		return 0;
	else
		return (TIMETYPE)buf.st_mtime;	/* v7's st_mtime is 32 bits */
}

/* 15c2 */
TIMETYPE
prestime(void)
{
	return (TIMETYPE)time(NULL);
}

static char n15[15];
static char *const n15end = &n15[14];

static mint amatch(const char *s, const char *p);
static mint umatch(const char *s, const char *p);

/*
 * 15de. The result is the chain of the names the pattern matches in the
 * directory, the last one found first; pat is cut at its last slash and
 * made whole again. The directory is opened once and kept (od): the
 * descriptors are the original's - virtual ones, mk_diropen() - and what
 * `-d` prints.
 */
struct depblock *
srchdir(char *pat, mint mkchain, struct depblock *nextdbl)
{
	int i, nread;
	char *dirname, *dirpref, *endir, *filepat, *p, temp[100];
	char fullname[100];
	const char *p1;
	char *p2;
	struct nameblock *q;
	struct depblock *thisdbl;
	struct opendir *od;
	struct pattern *patp;
	char **ent;
	int dirfd = -1, found = 0;

	mk_enter(754);
	thisdbl = NULL;

	if (mkchain == NO)
		for (patp = firstpat; patp; patp = patp->nxtpattern)
			if (!unequal(pat, patp->patval)) {
				mk_leave(754);
				return NULL;
			}

	patp = ALLOC(pattern);
	patp->nxtpattern = firstpat;
	firstpat = patp;
	patp->patval = copys(pat);

	endir = NULL;

	for (p = pat; *p != '\0'; ++p)
		if (*p == '/')
			endir = p;

	if (endir == NULL) {
		dirname = ".";
		dirpref = "";
		filepat = pat;
	} else {
		dirname = pat;
		*endir = '\0';
		dirpref = concat(dirname, "/", temp, sizeof(temp));
		filepat = endir + 1;
	}

	for (od = firstod; od; od = od->nxtopendir)
		if (!unequal(dirname, od->dirn)) {
			dirfd = od->dirfd;
			found = 1;
			break;
		}

	if (!found) {
		mk_checkname(dirname);
		if (!mk_diropen(dirname, &dirfd))
			dirfd = -1;
		od = ALLOC(opendir);
		od->nxtopendir = firstod;
		firstod = od;
		od->dirfd = dirfd;
		od->dirn = copys(dirname);
	}

	if (dirfd < 0) {
		mk_eprintf("Directory %s: ", dirname);
		fatal("Cannot open");
	}

	ent = mk_dirread(dirname, &nread);
	if (ent == NULL)
		fatal("Cannot open");
	for (i = 0; i < nread; ++i) {
		p1 = ent[i];
		p2 = n15;
		while ((p2 < n15end) && (*p2++ = *p1++) != '\0')
			;
		if (amatch(n15, filepat)) {
			concat(dirpref, n15, fullname, sizeof(fullname));
			if ((q = srchname(fullname)) == NULL)
				q = makename(copys(fullname));
			if (mkchain) {
				thisdbl = ALLOC(depblock);
				thisdbl->nxtdepblock = nextdbl;
				thisdbl->depname = q;
				nextdbl = thisdbl;
			}
		}
	}
	mk_dirfree(ent, nread);

	if (endir != NULL)
		*endir = '/';

	mk_leave(754);
	return thisdbl;
}

/* stolen from glob through find. 187e */
static mint
amatch(const char *s, const char *p)
{
	int cc, scc, k;
	int c, lc;
	mint r;

	mk_enter(18);
	scc = *s;
	lc = 077777;
	switch (c = *p) {

	case '[':
		k = 0;
		while ((cc = *++p) != 0) {
			switch (cc) {

			case ']':
				if (k)
					r = amatch(++s, ++p);
				else
					r = 0;
				mk_leave(18);
				return r;

			case '-':
				k |= (lc <= scc) & (scc <= (cc = p[1]));
			}
			if (scc == (lc = cc))
				k++;
		}
		mk_leave(18);
		return 0;

	case '?':
	caseq:
		if (scc)
			r = amatch(++s, ++p);
		else
			r = 0;
		mk_leave(18);
		return r;
	case '*':
		r = umatch(s, ++p);
		mk_leave(18);
		return r;
	case 0:
		mk_leave(18);
		return !scc;
	}
	if (c == scc)
		goto caseq;
	mk_leave(18);
	return 0;
}

/* 1960 */
static mint
umatch(const char *s, const char *p)
{
	mint r = 0;

	mk_enter(12);
	if (*p == 0)
		r = 1;
	else
		while (*s)
			if (amatch(s++, p)) {
				r = 1;
				break;
			}
	mk_leave(12);
	return r;
}

/*
 * look inside archives for notations a(b) and a((b))
 *	a(b)	is file member   b   in archive a
 *	a((b))	is entry point  _b  in object archive a
 *
 * The state of the original's archive reader: the open archive, the offset
 * of the header of the member, its length, and the header and the symbol
 * the original reads into static structures, which keep their bytes from
 * one read to the next where a read comes back short.
 */
static FILE *arfd;
static int arslot;			/* its file descriptor and stdio slot */
static int32_t arpos, arlen;
static uint8_t arhead[AR_HDR];
static uint8_t objhead[AOUT_HDR];
static uint8_t objentry[NLIST_SIZE];

/* fread(buf, n, 1, arfd): the bytes that are there are stored */
static void
arread(uint8_t *buf, size_t n)
{
	size_t i;
	int c;

	for (i = 0; i < n; i++) {
		if ((c = getc(arfd)) == EOF)
			return;
		buf[i] = (uint8_t)c;
	}
}

static mint eqstr(const char *a, const char *b, mint n);
static void clarch(void);
static void openarch(const char *f);
static mint getarch(void);
static void getobj(void);

/* 1996 */
TIMETYPE
lookarch(char *filename)
{
	char *p, *q, *send, s[15];
	mint i, nc, nsym, objarch;

	for (p = filename; *p != '('; ++p)
		;
	*p = '\0';
	openarch(filename);
	*p++ = '(';

	if (*p == '(') {
		objarch = YES;
		nc = 8;
		++p;
	} else {
		objarch = NO;
		nc = 14;
	}
	send = s + nc;

	for (q = s; q < send && *p != '\0' && *p != ')'; *q++ = *p++)
		;
	while (q < send)
		*q++ = '\0';
	while (getarch()) {
		if (objarch) {
			getobj();
			nsym = (mint)(getword(objhead + 8) / NLIST_SIZE);	/* a_syms */
			for (i = 0; i < nsym; ++i) {
				int8_t ntype;
				uint16_t nvalue;

				arread(objentry, NLIST_SIZE);
				ntype = (int8_t)objentry[8];
				nvalue = getword(objentry + 10);
				if ((ntype & N_EXT) && ((ntype & ~N_EXT) || nvalue) &&
				    eqstr((const char *)objentry, s, nc)) {
					TIMETYPE d = (TIMETYPE)getlong(arhead + AR_DATE);

					clarch();
					return d;
				}
			}
		}

		else if (eqstr((const char *)arhead + AR_NAME, s, nc)) {
			TIMETYPE d = (TIMETYPE)getlong(arhead + AR_DATE);

			clarch();
			return d;
		}
	}

	clarch();
	return 0L;
}

/* 1ae0 */
static void
clarch(void)
{
	fclose(arfd);
	mk_fclose_slot(arslot);
}

/* 1af2 */
static void
openarch(const char *f)
{
	struct stat buf;
	uint8_t word[2] = {0, 0};
	size_t n;
	int fd;

	mk_checkname(f);
	if (stat(MKPATH(f), &buf) < 0)
		arlen = 0;		/* the original's buf is stack garbage then,
					 * and the fopen() below fails */
	else
		arlen = (int32_t)buf.st_size;

	fd = mk_fopen_slot();
	arfd = fd < 0 ? NULL : fopen(MKPATH(f), "r");
	if (arfd == NULL) {
		if (fd >= 0)
			mk_fclose_slot(fd);
		fatal1("cannot open %s", f);
	}
	arslot = fd;
	n = fread(word, 1, 2, arfd);
	if (n == 1 && word[0] == (ARMAG & 0xff))
		/* the high byte of the original's word is whatever its stack
		 * held before */
		mk_trap("an archive of one byte, the first of the magic number");
	if (n < 2 || getword(word) != (ARMAG & 0xffff))
		fatal1("%s is not an archive", f);
	arpos = 0;
	memset(arhead, 0, sizeof(arhead));
	arhead[AR_SIZE] = 0xff;		/* ar_size = 2 - sizeof(arhead) */
	arhead[AR_SIZE + 1] = 0xff;
	arhead[AR_SIZE + 2] = 0xe8;
	arhead[AR_SIZE + 3] = 0xff;
}

/* 1b7c */
static mint
getarch(void)
{
	uint32_t size = (uint32_t)getlong(arhead + AR_SIZE);

	/* longs of 32 bits that wrap, as the original's do */
	arpos = (int32_t)((uint32_t)arpos + AR_HDR);
	arpos = (int32_t)((uint32_t)arpos + ((size + 1u) & ~1u));
	if (arpos >= arlen)
		return 0;
	if (arpos < 0)
		/* fseek() fails and the original reads on from where it was */
		mk_trap("a member size takes the archive position below zero");
	fseek(arfd, arpos, SEEK_SET);
	arread(arhead, AR_HDR);
	return 1;
}

/* 1bf8 */
static void
getobj(void)
{
	int32_t skip;
	uint16_t magic;

	arread(objhead, AOUT_HDR);
	magic = getword(objhead);
	if (magic != A_MAGIC1 && magic != A_MAGIC2 && magic != A_MAGIC3 &&
	    magic != A_MAGIC4) {
		char nm[15];

		if (memchr(arhead + AR_NAME, '\0', 14) == NULL)
			/* %s runs on into ar_date and the rest of the header */
			mk_trap("the 14 characters of a member name with no end "
			    "are printed");
		memcpy(nm, arhead + AR_NAME, 14);
		nm[14] = '\0';
		fatal1("%s is not an object module", nm);
	}
	/* a_text + a_data: the sum of two unsigned ints, then a long */
	skip = (uint16_t)(getword(objhead + 2) + getword(objhead + 4));
	if (!getword(objhead + 14))		/* a_flag */
		skip *= 2;
	fseek(arfd, skip, SEEK_CUR);
}

/* 1c82 */
static mint
eqstr(const char *a, const char *b, mint n)
{
	mint i;

	for (i = 0; i < n; ++i)
		if (*a++ != *b++)
			return NO;
	return YES;
}
