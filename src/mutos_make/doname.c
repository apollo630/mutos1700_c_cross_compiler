/*
 * doname.c - BASIC PROCEDURE. RECURSIVE. v7's doname.c, which the original
 * MUTOS 1700 make (text 0790..0fb6, 4 functions) has byte for byte. The
 * offset of each function in the original's text is in its comment.
 *
 * p->done = 0   don't know what to do yet
 * p->done = 1   file in process of being updated
 * p->done = 2   file already exists in current state
 * p->done = 3   file make failed
 *
 * The port's additions are those of misc.c: the buffers of the original's
 * stack frame are bounded (sourcename[100], prefix[100], temp[100],
 * concsuff[20], string[2500]), the frames are counted, and the prefix of
 * $* - an array of the frame, which the original leaves behind a returned
 * doname() - is known to be alive or not (doname_live()).
 */
#include "make.h"

#include <string.h>

/* The frames of the doname()s that have not returned, by serial number */
#define MAXLIVE 512
static int live[MAXLIVE];
static int nlive;
static int serial_next;

mint
doname_live(int serial)
{
	for (int i = 0; i < nlive; i++)
		if (live[i] == serial)
			return 1;
	return 0;
}

/* The name of a dependency block. expand() sets the name of a block it
 * replaced to 0, and the original reads it all the same where it prints or
 * collects names (the word at address 2 of its data segment, the gate of
 * its system calls, then); the port refuses to. */
static char *
depnamep(const struct depblock *q)
{
	if (q->depname == NULL)
		mk_trap("a name that expand() replaced is read through a null pointer");
	return q->depname->namep;
}

static mint doname1(struct nameblock *p, mint reclevel, TIMETYPE *tval, int serial);

/* 0790 */
mint
doname(struct nameblock *p, mint reclevel, TIMETYPE *tval)
{
	mint r;
	int serial;

	mk_enter(386);
	if (p != NULL) {
		if (nlive >= MAXLIVE)
			mk_trap("the doname() calls would not fit the original's stack");
		serial = ++serial_next;
		live[nlive++] = serial;
	} else
		serial = 0;
	r = doname1(p, reclevel, tval, serial);
	if (p != NULL)
		nlive--;
	mk_leave(386);
	return r;
}

static mint
doname1(struct nameblock *p, mint reclevel, TIMETYPE *tval, int serial)
{
	mint errstat;
	mint okdel1;
	mint didwork;
	TIMETYPE td, td1, tdep, ptime, ptime1;
	struct depblock *q;
	struct depblock *qtemp, *suffp, *suffp1;
	struct nameblock *p1, *p2;
	struct shblock *implcom, *explcom;
	struct lineblock *lp;
	struct lineblock *lp1, *lp2;
	char sourcename[100], prefix[100], temp[100], concsuff[20];
	char *pnamep, *p1namep;
	struct chain *qchain;

	if (p == NULL) {
		*tval = 0;
		return 0;
	}

	if (dbgflag) {
		mk_printf("doname(%s,%d)\n", p->namep, reclevel);
		mk_fflush();
	}

	if (p->done > 0) {
		*tval = p->modtime;
		return p->done == 3;
	}

	errstat = 0;
	tdep = 0;
	implcom = NULL;
	explcom = NULL;
	ptime = exists(p->namep);
	ptime1 = 0;
	didwork = NO;
	p->done = 1;	/* avoid infinite loops */

	qchain = NULL;

	/* Expand any names that have embedded metacharaters */

	for (lp = p->linep; lp; lp = lp->nxtlineblock)
		for (q = lp->depp; q; q = qtemp) {
			qtemp = q->nxtdepblock;
			expand(q);
		}

	/* make sure all dependents are up to date */

	for (lp = p->linep; lp; lp = lp->nxtlineblock) {
		td = 0;
		for (q = lp->depp; q; q = q->nxtdepblock) {
			errstat = (mint)(errstat + doname(q->depname, reclevel + 1, &td1));
			if (dbgflag)
				mk_printf("TIME(%s)=%ld\n", depnamep(q), (long)td1);
			if (td1 > td)
				td = td1;
			if (ptime < td1)
				qchain = appendq(qchain, depnamep(q));
		}
		if (p->septype == SOMEDEPS) {
			if (lp->shp != NULL)
				if (ptime < td || (ptime == 0 && td == 0) || lp->depp == NULL) {
					okdel1 = okdel;
					okdel = NO;
					setvar("@", p->namep);
					setvar("?", mkqlist(qchain));
					qchain = NULL;
					if (!questflag)
						errstat = (mint)(errstat + docom(lp->shp));
					setvar("@", NULL);
					okdel = okdel1;
					ptime1 = prestime();
					didwork = YES;
				}
		}

		else {
			if (lp->shp != NULL) {
				if (explcom)
					mk_eprintf("Too many command lines for `%s'\n",
					    p->namep);
				else
					explcom = lp->shp;
			}

			if (td > tdep)
				tdep = td;
		}
	}

	/* Look for implicit dependents, using suffix rules */

	for (lp = sufflist; lp; lp = lp->nxtlineblock)
		for (suffp = lp->depp; suffp; suffp = suffp->nxtdepblock) {
			pnamep = depnamep(suffp);
			if (suffix(p->namep, pnamep, prefix, sizeof(prefix))) {
				srchdir(concat(prefix, "*", temp, sizeof(temp)), NO, NULL);
				for (lp1 = sufflist; lp1; lp1 = lp1->nxtlineblock)
					for (suffp1 = lp1->depp; suffp1; suffp1 = suffp1->nxtdepblock) {
						p1namep = depnamep(suffp1);
						if ((p1 = srchname(concat(p1namep, pnamep, concsuff,
						    sizeof(concsuff)))) &&
						    (p2 = srchname(concat(prefix, p1namep, sourcename,
						    sizeof(sourcename))))) {
							errstat = (mint)(errstat +
							    doname(p2, reclevel + 1, &td));
							if (ptime < td)
								qchain = appendq(qchain, p2->namep);
							if (dbgflag)
								mk_printf("TIME(%s)=%ld\n", p2->namep, (long)td);
							if (td > tdep)
								tdep = td;
							setvar_star(prefix, serial);
							setvar("<", copys(sourcename));
							for (lp2 = p1->linep; lp2; lp2 = lp2->nxtlineblock)
								if ((implcom = lp2->shp))
									break;
							goto endloop;
						}
					}
			}
		}

endloop:

	if (errstat == 0 && (ptime < tdep || (ptime == 0 && tdep == 0))) {
		ptime = (tdep > 0 ? tdep : prestime());
		setvar("@", p->namep);
		setvar("?", mkqlist(qchain));
		if (explcom)
			errstat = (mint)(errstat + docom(explcom));
		else if (implcom)
			errstat = (mint)(errstat + docom(implcom));
		else if (p->septype == 0) {
			if ((p1 = srchname(".DEFAULT"))) {
				setvar("<", p->namep);
				for (lp2 = p1->linep; lp2; lp2 = lp2->nxtlineblock)
					if ((implcom = lp2->shp)) {
						errstat = (mint)(errstat + docom(implcom));
						break;
					}
			} else if (keepgoing) {
				mk_printf("Don't know how to make %s\n", p->namep);
				++errstat;
			} else
				fatal1(" Don't know how to make %s", p->namep);
		}

		setvar("@", NULL);
		if (noexflag || (ptime = exists(p->namep)) == 0)
			ptime = prestime();
	}

	else if (errstat != 0 && reclevel == 0)
		mk_printf("`%s' not remade because of errors\n", p->namep);

	else if (!questflag && reclevel == 0 && didwork == NO)
		mk_printf("`%s' is up to date.\n", p->namep);

	if (questflag && reclevel == 0)
		mk_exit(ndocoms > 0 ? -1 : 0);

	p->done = (int8_t)(errstat ? 3 : 2);
	if (ptime1 > ptime)
		ptime = ptime1;
	p->modtime = ptime;
	*tval = ptime;
	return errstat;
}

/* 0dd0 */
mint
docom(struct shblock *q)
{
	char *s;
	mint ign, nopr;
	char string[OUTMAX];
	mint r = NO;

	mk_enter(2516);
	++ndocoms;
	if (questflag) {
		mk_leave(2516);
		return NO;
	}

	if (touchflag) {
		s = (char *)varstr(varptr("@"));
		if (!silflag)
			mk_printf("touch(%s)\n", s);
		if (!noexflag)
			touch(YES, s);
	}

	else
		for (; q; q = q->nxtshblock) {
			subst(q->shbp, string, sizeof(string));

			ign = ignerr;
			nopr = NO;
			for (s = string; *s == '-' || *s == '@'; ++s)
				if (*s == '-')
					ign = YES;
				else
					nopr = YES;

			if (docom1(s, ign, nopr) && !ign) {
				if (keepgoing) {
					r = YES;
					break;
				} else
					fatal(NULL);
			}
		}
	mk_leave(2516);
	return r;
}

/* 0eba */
mint
docom1(char *comstring, mint nohalt, mint noprint)
{
	mint status;

	if (comstring[0] == '\0')
		return 0;

	if (!silflag && (!noprint || noexflag)) {
		mk_printf("%s%s\n", (noexflag ? "" : prompt), comstring);
		mk_fflush();
	}

	if (noexflag)
		return 0;

	if ((status = dosys(comstring, nohalt))) {
		if (status >> 8)
			mk_printf("*** Error code %d", status >> 8);
		else
			mk_printf("*** Termination code %d", status);

		if (nohalt)
			mk_printf(" (ignored)\n");
		else
			mk_printf("\n");
		mk_fflush();
	}

	return status;
}

/*
 * If there are any Shell meta characters in the name, expand into a list,
 * after searching directory. 0f66
 */
void
expand(struct depblock *q)
{
	char *s;
	char *s1;
	struct depblock *p;

	s1 = depnamep(q);
	for (s = s1;;)
		switch (*s++) {
		case '\0':
			return;

		case '*':
		case '?':
		case '[':
			if ((p = srchdir(s1, YES, q->nxtdepblock))) {
				q->nxtdepblock = p;
				q->depname = NULL;
			}
			return;
		}
}
