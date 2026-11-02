/*
 * main.c - the command: v7's main.c, which the original MUTOS 1700 make
 * (text 00b6..0790, 7 functions) has byte for byte but for the metering
 * (METERFILE is not defined in the original), and its global variables. The
 * offset of each function in the original's text is in its comment.
 *
 * command make to update programs.
 * Flags:	'd'  print out debugging comments
 *		'p'  print out a version of the input graph
 *		's'  silent mode--don't print out commands
 *		'f'  the next argument is the name of the description file;
 *		     "makefile" is the default
 *		'i'  ignore error codes from the shell
 *		'S'  stop after any command fails (normally do parallel work)
 *		'k'  keep going: do the other targets after an error
 *		'r'  do not read the built-in rules
 *		'n'  don't issue, just print, commands
 *		't'  touch (update time of) files but don't issue command
 *		'q'  don't do anything, but check if object is up to date;
 *		     returns exit code 0 if up to date, -1 if not
 */
#include "make.h"

#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct nameblock *mainname;
struct nameblock *firstname;
struct lineblock *sufflist;
struct varblock *firstvar;
struct pattern *firstpat;
struct opendir *firstod;

mint sigivalue = 0;
mint sigqvalue = 0;
mint waitpid_ = 0;		/* v7's `waitpid`, which the host has as a function */

mint dbgflag = NO;
mint prtrflag = NO;
mint silflag = NO;
mint noexflag = NO;
mint keepgoing = NO;
mint noruleflag = NO;
mint touchflag = NO;
mint questflag = NO;
mint ndocoms = NO;
mint ignerr = NO;		/* default is to stop on error */
mint okdel = YES;
mint inarglist;
const char *prompt = "";	/* other systems -- pick what you want */
char junkname[20];
char funny[128];

const char *const *linesptr = make_builtin;

FILE *fin;
static int finslot = -1;	/* the descriptor of fin (3 and up), -1: stdin or none */
static mint firstrd = 0;

/* 00b6 */
int
main(int argc, char *argv[])
{
	struct nameblock *p;
	mint i, j;
	mint descset, nfargs;
	TIMETYPE tjunk;
	char c, *s;
	static char onechar[2] = "X";

	mk_setpath();
	mk_enter(28);

	descset = 0;

	funny[0] = (META | TERMINAL);
	for (s = "=|^();&<>*?[]:$`'\"\\\n"; *s; ++s)
		funny[(int)*s] |= META;
	for (s = "\n\t :;&>|"; *s; ++s)
		funny[(int)*s] |= TERMINAL;

	inarglist = 1;
	for (i = 1; i < argc; ++i)
		if (argv[i] != NULL && argv[i][0] != '-' && eqsign(argv[i]))
			argv[i] = NULL;

	setvar("$", "$");
	inarglist = 0;

	for (i = 1; i < argc; ++i)
		if (argv[i] != NULL && argv[i][0] == '-') {
			for (j = 1; (c = argv[i][j]) != '\0'; ++j)
				switch (c) {
				case 'd':
					dbgflag = YES;
					break;

				case 'p':
					prtrflag = YES;
					break;

				case 's':
					silflag = YES;
					break;

				case 'i':
					ignerr = YES;
					break;

				case 'S':
					keepgoing = NO;
					break;

				case 'k':
					keepgoing = YES;
					break;

				case 'n':
					noexflag = YES;
					break;

				case 'r':
					noruleflag = YES;
					break;

				case 't':
					touchflag = YES;
					break;

				case 'q':
					questflag = YES;
					break;

				case 'f':
					if (i >= argc - 1)
						fatal("No description argument after -f flag");
					if (rddescf(argv[i + 1]))
						fatal1("Cannot open %s", argv[i + 1]);
					argv[i + 1] = NULL;
					++descset;
					break;

				default:
					onechar[0] = c;	/* to make lint happy */
					fatal1("Unknown flag argument %s", onechar);
				}

			argv[i] = NULL;
		}

	if (!descset)
		if (rddescf("makefile"))
			rddescf("Makefile");

	if (prtrflag)
		printdesc(NO);

	if (srchname(".IGNORE"))
		++ignerr;
	if (srchname(".SILENT"))
		silflag = 1;
	if ((p = srchname(".SUFFIXES")))
		sufflist = p->linep;
	if (!sufflist)
		mk_eprintf("No suffix list.\n");

	sigivalue = (mint)((intptr_t)signal(SIGINT, SIG_IGN) & 01);
	sigqvalue = (mint)((intptr_t)signal(SIGQUIT, SIG_IGN) & 01);
	enbint(intrupt);

	nfargs = 0;

	for (i = 1; i < argc; ++i)
		if ((s = argv[i]) != NULL) {
			if ((p = srchname(s)) == NULL) {
				p = makename(s);
			}
			++nfargs;
			doname(p, 0, &tjunk);
			if (dbgflag)
				printdesc(YES);
		}

	/*
	 * If no file arguments have been encountered, make the first
	 * name encountered that doesn't start with a dot
	 */

	if (nfargs == 0) {
		if (mainname == NULL)
			fatal("No arguments or description file");
		else {
			doname(mainname, 0, &tjunk);
			if (dbgflag)
				printdesc(YES);
		}
	}

	mk_exit(0);
}

/* 0450. A signal ends make; the target it was making goes with it unless
 * it is precious. */
void
intrupt(int sig)
{
	char *p;

	(void)sig;
	if (okdel && !noexflag && !touchflag &&
	    (p = varptr("@")->varval) && exists(p) > 0 && !isprecious(p)) {
		mk_eprintf("\n***  %s removed.", p);
		unlink(p);
	}

	if (junkname[0])
		unlink(junkname);
	mk_eprintf("\n");
	mk_exit(2);
}

/* 04ea */
mint
isprecious(const char *p)
{
	struct lineblock *lp;
	struct depblock *dp;
	struct nameblock *np;

	if ((np = srchname(".PRECIOUS"))) {
		for (lp = np->linep; lp; lp = lp->nxtlineblock)
			for (dp = lp->depp; dp; dp = dp->nxtdepblock) {
				if (dp->depname == NULL)
					mk_trap("a name expanded away is read as a name");
				if (!unequal(p, dp->depname->namep))
					return YES;
			}
	}

	return NO;
}

/* 053c */
void
enbint(void (*k)(int))
{
	if (sigivalue == 0)
		signal(SIGINT, k);
	if (sigqvalue == 0)
		signal(SIGQUIT, k);
}

/* read and parse description. 056c */
mint
rddescf(const char *descfile)
{
	FILE *k;
	struct stat st;
	int slot;

	if (!firstrd++) {
		if (!noruleflag)
			rdd1(NULL);
	}
	if (!unequal(descfile, "-"))
		return rdd1(stdin);

	if (stat(MKPATH(descfile), &st) == 0 && S_ISDIR(st.st_mode))
		/* MUTOS lets fopen() open a directory: the makefile is its entries */
		mk_trap("a directory as the description file");
	slot = mk_fopen_slot();
	k = slot < 0 ? NULL : fopen(MKPATH(descfile), "r");
	if (k != NULL) {
		finslot = slot;
		return rdd1(k);
	}
	if (slot >= 0)
		mk_fclose_slot(slot);
	return 1;
}

/* 05cc */
mint
rdd1(FILE *k)
{
	fin = k;
	yylineno = 0;
	zznextc = NULL;

	if (yyparse())
		fatal("Description file error");

	if (fin != NULL)
		fclose(fin);
	if (finslot >= 0)
		mk_fclose_slot(finslot);
	finslot = -1;

	return 0;
}

/* The state of a variable block as the original prints it: a macro whose
 * value was cleared (the "@" between two commands) prints as "(null)", which
 * libc.a's printf makes of a null %s (measured with -p and -d) - see
 * varstr(). */
/* 060c */
void
printdesc(mint prntflag)
{
	struct nameblock *p;
	struct depblock *dp;
	struct varblock *vp;
	struct opendir *od;
	struct shblock *sp;
	struct lineblock *lp;

	if (prntflag) {
		mk_printf("Open directories:\n");
		for (od = firstod; od; od = od->nxtopendir)
			mk_printf("\t%d: %s\n", od->dirfd, od->dirn);
	}

	if (firstvar != NULL)
		mk_printf("Macros:\n");
	for (vp = firstvar; vp; vp = vp->nxtvarblock)
		mk_printf("\t%s = %s\n", vp->varname, varstr(vp));

	for (p = firstname; p; p = p->nxtnameblock) {
		mk_printf("\n\n%s", p->namep);
		if (p->linep != NULL)
			mk_printf(":");
		if (prntflag)
			mk_printf("  done=%d", p->done);
		if (p == mainname)
			mk_printf("  (MAIN NAME)");
		for (lp = p->linep; lp; lp = lp->nxtlineblock) {
			if ((dp = lp->depp)) {
				mk_printf("\n depends on:");
				for (; dp; dp = dp->nxtdepblock)
					if (dp->depname != NULL)
						mk_printf(" %s ", dp->depname->namep);
			}

			if ((sp = lp->shp)) {
				mk_printf("\n commands:\n");
				for (; sp != NULL; sp = sp->nxtshblock)
					mk_printf("\t%s\n", sp->shbp);
			}
		}
	}
	mk_printf("\n");
	mk_fflush();
}
