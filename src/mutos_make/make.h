/*
 * make.h - the MUTOS 1700 make, ported from the original binary.
 *
 * This is v7's make (v7/make/) as the original MUTOS 1700 binary has it
 * (mutos/mutos_bin/make.golden.base64.txt, 0411, text 16,192 bytes,
 * stripped of its symbols). The port follows the method of the other
 * ports (docs/C0_PORT_PLAN.md): the binary is the specification, v7's
 * source the skeleton. tests/realbin/makemap.txt maps every function to
 * its text offset in the binary; the comment of each function in the
 * port's sources names what the binary does differently from v7.
 *
 * What reading the binary showed (2026-11-02): v7's main.c, doname.c,
 * misc.c, files.c, dosys.c and gram.y's yylex(), retsh() and nextlin()
 * compile - with the project's own port of the original cc, `cc -O
 * -Dunix` - to the original's text byte for byte, with three exceptions
 * that are no difference of source but of the header files the original
 * was compiled with: METERFILE is not defined (no meter(), no -D...), the
 * header a.out.h has `char n_type; char n_other;` in struct nlist where
 * MUTOS's and v7's say `int n_type`, and gram.y's NLEFTS is 300 where
 * v7's defs says 40. Everything else - the 50 functions of v7's make.o
 * - is v7's, so the port is v7's C with the 16-bit representation and
 * the environment made explicit.
 *
 * HOST REPRESENTATION. Where the original is 16-bit, the port is:
 *
 *   - int is mint (int16_t): sums, the hash, the status word wait()
 *     gives (status>>8 of 0xC800 is -56), errstat;
 *   - char is signed (the port is built with -fsigned-char);
 *   - TIMETYPE is a 32-bit long, the times of stat() cut to it;
 *   - the structures are the originals' in what they hold; the heap
 *     they live in is the host's, but it is *counted* in the
 *     originals' sizes (nameblock 12 bytes, lineblock 6, depblock 4,
 *     shblock 4, varblock 8, pattern 4, opendir 6, chain 4, a string its
 *     length and a zero, each with the two bytes of malloc's header),
 *     with the stack frames of the recursive functions, against the
 *     64K segment of the original (mk_account(), mk_enter());
 *   - stdout is libc.a's stdio: 512-byte blocks written when the 513th
 *     character comes, fflush() and exit() write what is there, stderr
 *     is not buffered; the order of what make and the commands it runs
 *     write to one file is the original's.
 *
 * What the port does not model - the original reads or writes memory it
 * does not own, or depends on what a MUTOS directory looks like - is
 * refused (mk_trap(): exit status 125, "Make: ... refused"): a byte
 * above 0x7f that the original uses as an index into funny[] or into
 * the ctype table, a buffer of the original's stack that a name or a
 * command would run over, more than 300 targets in one rule, a
 * dangling $*, a dependency chain or a makefile that would not fit the
 * 64K segment of the original.
 *
 * The directory a pattern is looked up in is read the way a MUTOS
 * directory looks to a program: the entries ".", "..", then the rest in
 * byte order (the original sees the entries in the order of the disk);
 * a name of more than 14 characters, which MUTOS's file system cannot
 * have, is not an entry.
 */
#ifndef MUTOS_MAKE_MAKE_H
#define MUTOS_MAKE_MAKE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#if defined(__GNUC__)
#define NORETURN __attribute__((noreturn))
#define PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define NORETURN
#define PRINTF(a, b)
#endif

typedef int16_t mint;		/* the original's int */
typedef int32_t TIMETYPE;	/* defs: typedef long int TIMETYPE */

#define SHELLCOM "/bin/sh"

#define NO 0
#define YES 1

#define unequal strcmp
#define HASHSIZE 509
#define NLEFTS 300		/* v7's defs: 40; the original's gram.y: 300 */
#define INMAX 1500
#define OUTMAX 2500
#define QBUFMAX 1500

#define ALLDEPS 1
#define SOMEDEPS 2

#define META 01
#define TERMINAL 02

/* ---- the structures of defs, as the original's heap counts them ------- */

struct nameblock {
	struct nameblock *nxtnameblock;
	char *namep;
	struct lineblock *linep;
	int8_t done;		/* int done:3 */
	int8_t septype;		/* int septype:3 */
	TIMETYPE modtime;
};
#define SZ_nameblock 12

struct lineblock {
	struct lineblock *nxtlineblock;
	struct depblock *depp;
	struct shblock *shp;
};
#define SZ_lineblock 6

struct depblock {
	struct depblock *nxtdepblock;
	struct nameblock *depname;
};
#define SZ_depblock 4

struct shblock {
	struct shblock *nxtshblock;
	char *shbp;
};
#define SZ_shblock 4

struct varblock {
	struct varblock *nxtvarblock;
	char *varname;
	char *varval;
	int8_t noreset;		/* int noreset:1 */
	int8_t used;		/* int used:1 */
	int star;		/* the varval is the $* of a doname() frame: its serial */
};
#define SZ_varblock 8

struct pattern {
	struct pattern *nxtpattern;
	char *patval;
};
#define SZ_pattern 4

struct opendir {
	struct opendir *nxtopendir;
	int dirfd;		/* what fileno(dirfc) is in the original */
	char *dirn;
};
#define SZ_opendir 6

struct chain {
	struct chain *nextp;
	char *datap;
};
#define SZ_chain 4

/* ---- the globals of main.c ----------------------------------------- */

extern struct nameblock *mainname;
extern struct nameblock *firstname;
extern struct lineblock *sufflist;
extern struct varblock *firstvar;
extern struct pattern *firstpat;
extern struct opendir *firstod;

extern mint sigivalue, sigqvalue, waitpid_, dbgflag, prtrflag, silflag;
extern mint noexflag, keepgoing, noruleflag, touchflag, questflag;
extern mint ndocoms, ignerr, okdel, inarglist;
extern const char *prompt;
extern char junkname[20];
extern char funny[128];

/* gram.c */
extern char *zznextc;
extern mint yylineno;
extern FILE *fin;
extern const char *const *linesptr;
mint yyparse(void);
mint yylex(void);

/* main.c */
void intrupt(int sig);
mint isprecious(const char *p);
void enbint(void (*k)(int));
mint rddescf(const char *descfile);
mint rdd1(FILE *k);
void printdesc(mint prntflag);

/* doname.c */
mint doname(struct nameblock *p, mint reclevel, TIMETYPE *tval);
mint docom(struct shblock *q);
mint docom1(char *comstring, mint nohalt, mint noprint);
void expand(struct depblock *q);

/* misc.c */
mint hashloc(const char *s);
struct nameblock *srchname(const char *s);
struct nameblock *makename(const char *s);
mint hasslash(const char *s);
char *copys(const char *s);
char *concat(const char *a, const char *b, char *c, size_t cap);
mint suffix(const char *a, const char *b, char *p, size_t cap);
void *ckalloc(mint n, size_t hostsize);
char *subst(const char *a, char *b, size_t cap);
const char *varstr(const struct varblock *vp);
mint doname_live(int serial);
void setvar(const char *v, char *s);
void setvar_star(char *s, int serial);
mint eqsign(char *a);
struct varblock *varptr(const char *v);
NORETURN void fatal1(const char *s, const char *t);	/* s has one %s */
NORETURN void fatal(const char *s);
NORETURN void yyerror(const char *s);
struct chain *appendq(struct chain *head, char *tail);
char *mkqlist(struct chain *p);
#define ALLOC(x) ((struct x *)ckalloc(SZ_##x, sizeof(struct x)))

/* files.c */
TIMETYPE exists(char *filename);
TIMETYPE prestime(void);
struct depblock *srchdir(char *pat, mint mkchain, struct depblock *nextdbl);
TIMETYPE lookarch(char *filename);
/* V7 reads an empty path name as the current directory (its namei()), and so
 * does the emulator; the host would refuse it */
#define MKPATH(n) (*(n) != '\0' ? (n) : ".")
void mk_checkname(const char *name);	/* a path part of more than 14 characters: refused */

/* dosys.c */
mint dosys(char *comstring, mint nohalt);
mint metas(const char *s);
mint doshell(char *comstring, mint nohalt);
mint await(void);
void doclose(void);
mint doexec(char *str);
void touch(mint force, char *name);

/* ---- mklib.c: what libc.a and the 64K segment do for the original ----- */

NORETURN void mk_trap(const char *what);
void mk_printf(const char *fmt, ...) PRINTF(1, 2);	/* stdout, buffered */
void mk_eprintf(const char *fmt, ...) PRINTF(1, 2);	/* stderr, not */
void mk_fflush(void);					/* fflush(stdout) */
NORETURN void mk_exit(int status);			/* exit(): flushes stdout */
void mk_account(size_t n);	/* the heap: a block of n bytes of the original's */
unsigned mk_break(void);	/* the original's break, an address in its segment */
void mk_enter(size_t frame);	/* the stack: a frame of that many bytes */
void mk_leave(size_t frame);
int mk_funny(int c);		/* funny[c], c a char of the original */
int mk_isspace(int c);		/* the ctype macros with a char argument */
int mk_isalpha(int c);
int mk_isdigit(int c);
int mk_fopen_slot(void);	/* a stdio slot (and file descriptor) for a file */
void mk_fclose_slot(int fd);
int mk_diropen(const char *name, int *fd);	/* a directory: 1, or 0: cannot open */
/* the entries of a directory as the original's fread() gives them: "." and
 * ".." first, then the rest by byte order; mk_dirfree() frees the list */
char **mk_dirread(const char *name, int *n);
void mk_dirfree(char **l, int n);
void mk_setpath(void);		/* $MUTOS_ROOT/bin first in the PATH of the commands */

/* gram.c's tables and makedata.c's (generated, tests/realbin/maketables.py) */
extern const int16_t make_yyexca[], make_yyact[], make_yypact[], make_yypgo[];
extern const int16_t make_yyr1[], make_yyr2[], make_yychk[], make_yydef[];
extern const char *const make_builtin[];

#endif /* MUTOS_MAKE_MAKE_H */
