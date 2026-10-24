/*
 * cc.c - the original MUTOS 1700 cc's functions (cc.o), ported.
 *
 * v7's cc.c (v7/cc/cc.c) as the binary has it: its ten functions in the
 * binary's order, main() to cunlink(), each read instruction by
 * instruction from the text (tests/realbin/ccmap.txt, ccdis.py) beside
 * v7's source. Every global lives at its address in the memory image
 * (cc.h), and so does main()'s frame - its locals at their offsets from
 * BP, its arguments above them -, because the original stores into
 * ptemp[] without a check (-P and -E) and its 101st entry is main()'s pv
 * itself. The other functions' locals are not in the image: none of them
 * has its address taken, but for callsys()'s status, which only wait()
 * writes.
 *
 * Where a statement reads and writes the image in an order that matters
 * for an overrun (an index computed, then incremented in memory, then
 * the store), the port does it in the binary's order.
 */
#include "cc.h"

#include <stddef.h>

/* main()'s BP (set by cc_init(): crt0's and main()'s prologue) */
uint16_t cc_bp;

#define	BP		cc_bp
#define	P_argc		((uint16_t)(BP + 4))
#define	P_argv		((uint16_t)(BP + 6))
#define	L_t		((uint16_t)(BP - 0x06))	/* char *t (also -o's c) */
#define	L_savetsp	((uint16_t)(BP - 0x08))
#define	L_assource	((uint16_t)(BP - 0x0a))
#define	L_pv		((uint16_t)(BP - 0x0c))	/* = &ptemp[MAXOPT] */
#define	L_ptemp		((uint16_t)(BP - 0xd4))	/* char *ptemp[MAXOPT] */
#define	L_pvt		((uint16_t)(BP - 0xd6))
#define	L_nc		((uint16_t)(BP - 0xd8))
#define	L_nl		((uint16_t)(BP - 0xda))
#define	L_i		((uint16_t)(BP - 0xdc))
#define	L_j		((uint16_t)(BP - 0xde))
#define	L_c		((uint16_t)(BP - 0xe0))
#define	L_f20		((uint16_t)(BP - 0xe2))
#define	L_nxo		((uint16_t)(BP - 0xe4))
#define	L_na		((uint16_t)(BP - 0xe6))
#define	L_fd		((uint16_t)(BP - 0xe8))	/* creat()'s result */

/* a word incremented in memory (inc word ptr [...]) */
static void
inc(uint16_t a)
{
	ww(a, (uint16_t)(rw(a) + 1));
}

/* the address of argv[i], as the code computes it: i << 1, plus argv */
static uint16_t
argvp(void)
{
	return (uint16_t)((uint16_t)(rw(L_i) << 1) + rw(P_argv));
}

/* &tab[n++], the index taken before the count is incremented in memory */
static uint16_t
slot(uint16_t tab, uint16_t n)
{
	uint16_t a = (uint16_t)(rw(n) << 1);

	inc(n);
	return (uint16_t)(a + tab);
}

static void	idexit(void);
static void	error(uint16_t s, uint16_t x);
static int16_t	getsuf(uint16_t as);
static uint16_t	setsuf(uint16_t as, int16_t ch);
static int16_t	callsys(uint16_t f, uint16_t v);
static uint16_t	copy(uint16_t as);
static int16_t	nodup(uint16_t l, uint16_t os);
static void	cunlink(uint16_t f);

static void
printf1(uint16_t fmt, uint16_t arg)
{
	cc_printf(fmt, &arg, 1);
}

static void
printf0(uint16_t fmt)
{
	cc_printf(fmt, NULL, 0);
}

/*
 * 00b6 main - delta. The options (a hashed switch on argv[i][1]: c % 5,
 * then c / 5): v7's S, o, O, p, E, P, c, f, 2, D, I, U, C, t, B, and two
 * new ones - -a, which prints "Executing ..." before each pass, and -K<n>,
 * which appends n to kopt[] ("s", no length check) and gives it to c1 as
 * its fourth argument (c1's s<n>: the frame size from which a function
 * calls chkstk). -f only sets fflag: c0f, c1f and asf for c0, c1 and as
 * (strcpy'd into pass0, pass1, passa before each call), -lf before -lc;
 * no fcrt0.o, and no "-f overwrites earlier option". -o is not refused
 * for a .c or .o name: a name that is one, or that starts with '-', is
 * read again as an argument (i--) and outfile set to -1; the first .o
 * put into llist after that - "x.o" of "x.c" - becomes the output's name
 * without its suffix (setsuf(t, 0), the '.' cut off). The temporary
 * file: mktemp("/tmp/ctm0XXXXXX") and creat(tmp0, 0400) once (v7 tries
 * /tmp/ctm0a, b, ...). For each source: cpp without -P (v7 the same), c0
 * (the failure unlinks tmp4), c1 with kopt[] after -K (the failure
 * unlinks tmp1 and tmp2), c2, then "as -gC -o x.o" (v7: as -u) with no
 * cunlink() of tmp1, tmp2, tmp4 before it. The link: "ld -X pref [-o
 * out] files [-l2 | [-lf] -lc]" (v7's -lfpsim after pref is gone).
 */
int16_t
ccmain(void)
{
	uint16_t a, v;
	int16_t c;

	/* i = nc = nl = f20 = nxo = 0 (stored nxo, f20, nl, nc, i) */
	ww(L_nxo, 0);
	ww(L_f20, 0);
	ww(L_nl, 0);
	ww(L_nc, 0);
	ww(L_i, 0);
	/*
	 * setbuf(stdout, NULL): libc.a's stdout is unbuffered - a printf()
	 * goes out at its end (cc_printf()), a putc() at once.
	 */
	ww(L_pv, L_ptemp);
	for (;;) {
		inc(L_i);
		if (!(ri(L_i) < ri(P_argc)))
			break;
		if (rb(rw(argvp())) != '-')
			goto passa;
		c = rb((uint16_t)(rw(argvp()) + 1));
		switch (c) {
		default:
			goto passa;
		case 'S':
			inc(sflag);
			inc(cflag);
			break;
		case 'o':
			inc(L_i);
			if (ri(L_i) < ri(P_argc)) {
				ww(outfile, rw(argvp()));
				ww(L_t, (uint16_t)getsuf(rw(outfile)));
				if (ri(L_t) == 'c' || ri(L_t) == 'o'
				    || rb(rw(argvp())) == '-') {
					ww(L_i, (uint16_t)(rw(L_i) - 1));
					ww(outfile, 0xffff);
				}
			}
			break;
		case 'O':
			inc(oflag);
			break;
		case 'p':
			inc(proflag);
			break;
		case 'a':
			inc(aflag);
			break;
		case 'f':
			inc(fflag);
			break;
		case 'E':
			inc(exflag);
			/* FALLTHROUGH */
		case 'P':
			inc(pflag);
			a = rw(L_pv);			/* *pv++ = argv[i] */
			v = rw(argvp());
			ww(a, v);
			ww(L_pv, (uint16_t)(rw(L_pv) + 2));
			/* FALLTHROUGH */
		case 'c':
			inc(cflag);
			break;
		case 'K':
			a = argvp();			/* argv[i] += 2 */
			ww(a, (uint16_t)(rw(a) + 1));
			a = argvp();
			ww(a, (uint16_t)(rw(a) + 1));
			cc_strcat(KOPT + 1, rw(a));
			inc(kflag);
			break;
		case '2':
			if (rb((uint16_t)(rw(argvp()) + 2)) == 0)
				ww(pref, S_CRT2);
			else {
				ww(pref, S_CRT20);
				ww(L_f20, 1);
			}
			break;
		case 'D':
		case 'I':
		case 'U':
		case 'C':
			a = rw(L_pv);			/* *pv++ = argv[i] */
			v = rw(argvp());
			ww(a, v);
			ww(L_pv, (uint16_t)(rw(L_pv) + 2));
			if (L_pv <= rw(L_pv)) {		/* pv >= ptemp+MAXOPT */
				error(S_DIUC, 0);
				ww(L_pv, (uint16_t)(rw(L_pv) - 2));
			}
			break;
		case 't':
			if (rw(chpass) != 0)
				error(S_TOVER, 0);
			ww(chpass, (uint16_t)(rw(argvp()) + 2));
			if (rb(rw(chpass)) == 0)
				ww(chpass, S_012P1);
			break;
		case 'B':
			if (rw(npassname) != 0)
				error(S_BOVER, 0);
			ww(npassname, (uint16_t)(rw(argvp()) + 2));
			if (rb(rw(npassname)) == 0)
				ww(npassname, S_USRSRCO);
			break;
		}
		continue;
passa:
		ww(L_t, rw(argvp()));
		ww(L_c, (uint16_t)getsuf(rw(L_t)));
		if (ri(L_c) == 'c' || ri(L_c) == 's' || ri(exflag) != 0) {
			v = rw(L_t);			/* clist[nc++] = t */
			ww(slot(CLIST, L_nc), v);
			if (ri(L_nc) >= MAXFIL) {
				error(S_MANYSRC, 0);
				cc_exit(1);
			}
			ww(L_t, setsuf(rw(L_t), 'o'));
		}
		if (nodup(LLIST, rw(L_t))) {
			v = rw(L_t);			/* llist[nl++] = t */
			ww(slot(LLIST, L_nl), v);
			if (ri(L_nl) >= MAXLIB) {
				error(S_MANYOBJ, 0);
				cc_exit(1);
			}
			if (getsuf(rw(L_t)) == 'o') {
				inc(L_nxo);
				if (rw(outfile) == 0xffff) {
					/* the output: the name, its suffix cut off */
					v = setsuf(rw(L_t), 0);
					ww(outfile, v);
					ww(L_t, v);
					do {
						a = rw(L_t);
						inc(L_t);
					} while (rb(a) != 0);
					wb((uint16_t)(rw(L_t) - 2), 0);
				}
			}
		}
	}

	if (rw(npassname) != 0 && rw(chpass) == 0)
		ww(chpass, S_012P2);
	if (rw(chpass) != 0 && rw(npassname) == 0)
		ww(npassname, S_USRSRC);
	if (rw(chpass) != 0) {
		for (ww(L_t, rw(chpass)); rb(rw(L_t)) != 0; inc(L_t)) {
			switch (rb(rw(L_t))) {
			case '0':
				cc_strcpy(PASS0, rw(npassname));
				cc_strcat(PASS0, S_C0);
				break;
			case '1':
				cc_strcpy(PASS1, rw(npassname));
				cc_strcat(PASS1, S_C1);
				break;
			case '2':
				cc_strcpy(PASS2, rw(npassname));
				cc_strcat(PASS2, S_C2);
				break;
			case 'p':
				cc_strcpy(PASSP, rw(npassname));
				cc_strcat(PASSP, S_CPP);
				break;
			}
		}
	}
	if (rw(proflag) != 0)
		ww(pref, S_MCRT0);
	if (rw(L_nc) == 0)
		goto nocom;
	if (rw(pflag) == 0) {
		ww(tmp0, copy(S_TMPNAME));
		cc_mktemp(rw(tmp0));
		ww(L_fd, (uint16_t)cc_creat(rw(tmp0), 0400));
		if (ri(L_fd) < 0) {
			error(S_NOTEMP, 0);
			cc_exit(1);
		}
		cc_close(ri(L_fd));
	}
	cc_signals(idexit);
	/* tmpN = copy(tmp0), its 9th character N - with -P, copies of 0 */
	ww(tmp1, copy(rw(tmp0)));
	wb((uint16_t)(rw(tmp1) + 8), '1');
	ww(tmp2, copy(rw(tmp0)));
	wb((uint16_t)(rw(tmp2) + 8), '2');
	ww(tmp3, copy(rw(tmp0)));
	wb((uint16_t)(rw(tmp3) + 8), '3');
	if (rw(oflag) != 0) {
		ww(tmp5, copy(rw(tmp0)));
		wb((uint16_t)(rw(tmp5) + 8), '5');
	}
	if (rw(pflag) == 0) {
		ww(tmp4, copy(rw(tmp0)));
		wb((uint16_t)(rw(tmp4) + 8), '4');
	}
	ww(L_pvt, rw(L_pv));
	for (ww(L_i, 0); ri(L_i) < ri(L_nc); inc(L_i)) {
#define	CLIST_I	rw((uint16_t)((uint16_t)(rw(L_i) << 1) + CLIST))
		if (ri(L_nc) > 1)
			printf1(S_FILE, CLIST_I);
		if (getsuf(CLIST_I) == 's') {
			ww(L_assource, CLIST_I);
			goto assemble;
		}
		ww(L_assource, rw(tmp3));
		if (rw(pflag) != 0)
			ww(tmp4, setsuf(CLIST_I, 'i'));
		ww(L_savetsp, rw(tsp));
		ww(AV + 0, S_AVCPP);
		ww(AV + 2, CLIST_I);
		ww(AV + 4, rw(exflag) != 0 ? S_DASH : rw(tmp4));
		ww(L_na, 3);
		for (ww(L_pv, L_ptemp); rw(L_pv) < rw(L_pvt);
		    ww(L_pv, (uint16_t)(rw(L_pv) + 2))) {
			v = rw(rw(L_pv));		/* av[na++] = *pv */
			ww(slot(AV, L_na), v);
		}
		ww(slot(AV, L_na), 0);
		if (rw(aflag) != 0)
			printf0(S_XCPP);
		if (callsys(PASSP, AV)) {
			inc(cflag);
			inc(eflag);
			continue;
		}
		ww(AV + 2, rw(tmp4));
		ww(tsp, rw(L_savetsp));
		ww(AV + 0, rw(fflag) != 0 ? S_AVC0F : S_AVC0);
		if (rw(pflag) != 0) {
			inc(cflag);
			continue;
		}
		ww(AV + 4, rw(tmp1));
		ww(AV + 6, rw(tmp2));
		if (rw(proflag) != 0) {
			ww(AV + 8, S_DASHP);
			ww(AV + 10, 0);
		} else
			ww(AV + 8, 0);
		if (rw(aflag) != 0)
			printf1(S_XC0, rw(AV + 0));
		if (rw(fflag) != 0)
			cc_strcpy(PASS0, S_LIBC0F);
		if (callsys(PASS0, AV)) {
			inc(cflag);
			inc(eflag);
			cc_unlink(rw(tmp4));
			continue;
		}
		ww(AV + 0, rw(fflag) != 0 ? S_AVC1F : S_AVC1);
		ww(AV + 2, rw(tmp1));
		ww(AV + 4, rw(tmp2));
		if (rw(sflag) != 0) {
			v = setsuf(CLIST_I, 's');
			ww(tmp3, v);
			ww(L_assource, v);
		}
		ww(AV + 6, rw(tmp3));
		if (rw(oflag) != 0)
			ww(AV + 6, rw(tmp5));
		if (rw(kflag) != 0) {
			ww(AV + 8, KOPT);
			ww(AV + 10, 0);
		} else
			ww(AV + 8, 0);
		if (rw(aflag) != 0)
			printf1(S_XC1, rw(AV + 0));
		if (rw(fflag) != 0)
			cc_strcpy(PASS1, S_LIBC1F);
		if (callsys(PASS1, AV)) {
			inc(cflag);
			inc(eflag);
			cc_unlink(rw(tmp1));
			cc_unlink(rw(tmp2));
			continue;
		}
		if (rw(oflag) != 0) {
			ww(AV + 0, S_AVC2);
			ww(AV + 2, rw(tmp5));
			ww(AV + 4, rw(tmp3));
			ww(AV + 6, 0);
			if (rw(aflag) != 0)
				printf0(S_XC2);
			if (callsys(PASS2, AV)) {
				cc_unlink(rw(tmp3));
				v = rw(tmp5);
				ww(L_assource, v);
				ww(tmp3, v);
			} else
				cc_unlink(rw(tmp5));
		}
		if (rw(sflag) != 0)
			continue;
assemble:
		ww(AV + 0, rw(fflag) != 0 ? S_AVASF : S_AVAS);
		ww(AV + 2, S_GC);
		ww(AV + 4, S_DASHO1);
		ww(AV + 6, setsuf(CLIST_I, 'o'));
		ww(AV + 8, rw(L_assource));
		ww(AV + 10, 0);
		if (rw(aflag) != 0)
			printf1(S_XAS, rw(AV + 0));
		if (rw(fflag) != 0)
			cc_strcpy(PASSA, S_BINASF);
		if (callsys(PASSA, AV) > 1) {
			inc(cflag);
			inc(eflag);
		}
#undef	CLIST_I
	}
nocom:
	if (rw(cflag) == 0 && rw(L_nl) != 0) {
		ww(L_i, 0);
		ww(AV + 0, S_AVLD);
		ww(AV + 2, S_DASHX);
		ww(AV + 4, rw(pref));
		ww(L_j, 3);
		if (rw(outfile) != 0) {
			ww(slot(AV, L_j), S_DASHO2);
			v = rw(outfile);
			ww(slot(AV, L_j), v);
		}
		while (ri(L_i) < ri(L_nl)) {		/* av[j++] = llist[i++] */
			v = rw(slot(LLIST, L_i));
			ww(slot(AV, L_j), v);
		}
		if (rw(L_f20) != 0)
			ww(slot(AV, L_j), S_L2);
		else {
			if (rw(fflag) != 0)
				ww(slot(AV, L_j), S_LF);
			ww(slot(AV, L_j), S_LC);
		}
		ww(slot(AV, L_j), 0);
		if (rw(aflag) != 0)
			printf0(S_XLD);
		ww(eflag, (uint16_t)(rw(eflag) | (uint16_t)callsys(S_BINLD, AV)));
		if (ri(L_nc) == 1 && ri(L_nxo) == 1 && rw(eflag) == 0)
			cunlink(setsuf(rw(CLIST), 'o'));
	}
	dexit();
}

/* 0c3c idexit - same: the handler of SIGINT and SIGTERM */
static void
idexit(void)
{
	ww(eflag, 100);
	dexit();
}

/* 0c54 dexit - same */
void
dexit(void)
{
	if (rw(pflag) == 0) {
		cunlink(rw(tmp1));
		cunlink(rw(tmp2));
		if (rw(sflag) == 0)
			cunlink(rw(tmp3));
		cunlink(rw(tmp4));
		cunlink(rw(tmp5));
		cunlink(rw(tmp0));
	}
	cc_exit(ri(eflag));
}

/*
 * 0cbc error - same: fprintf() and putc('\n') on stderr with -E, else on
 * stdout; it counts as an error (cflag, eflag) - also for "-t overwrites
 * earlier option" and the like.
 */
static void
error(uint16_t s, uint16_t x)
{
	cc_fprintf(rw(exflag) != 0 ? STDERR_ : STDOUT_, s, &x, 1);
	cc_putc('\n', rw(exflag) != 0 ? STDERR_ : STDOUT_);
	inc(cflag);
	inc(eflag);
}

/*
 * 0d54 getsuf - same: the character after a '.' that is the last but one
 * of a name of 3 to 14 characters after its last '/'; else 0. Chars are
 * signed (a suffix 0200..0377 is negative).
 */
static int16_t
getsuf(uint16_t as)
{
	uint16_t s = as;
	int16_t c = 0, t;

	while ((t = rb(s++)) != 0) {
		if (t == '/')
			c = 0;
		else
			c = (int16_t)(uint16_t)((uint16_t)c + 1);
	}
	s = (uint16_t)(s - 3);
	if (c <= 14 && c > 2 && rb(s++) == '.')
		return rb(s);
	return 0;
}

/*
 * 0dba setsuf - same: a copy of the name after its last '/', its last
 * character replaced by ch.
 */
static uint16_t
setsuf(uint16_t as, int16_t ch)
{
	uint16_t s, s1;

	s = s1 = copy(as);
	while (rb(s) != 0)
		if (rb(s++) == '/')
			s1 = s;
	wb((uint16_t)(s - 1), ch);
	return s1;
}

/*
 * 0dfc callsys - same: fork(), execv() in the child ("Can't find %s" and
 * exit(100) if it returns), wait() for that child; a child killed by a
 * signal other than SIGINT is "Fatal error in %s" (eflag 8), and either
 * ends cc (dexit()); else the child's exit status. The port reads the
 * path and the arguments from the image before the fork() - nothing
 * changes them before the child's execv() (cc_prepare_exec()).
 */
static int16_t
callsys(uint16_t f, uint16_t v)
{
	int16_t t;
	uint16_t status;

	cc_prepare_exec(f, v);
	t = cc_fork();			/* 1 in the parent: the port keeps the */
	if (t == 0) {			/* host's process number itself */
		cc_execv();
		printf1(S_CANTFIND, f);
		cc_exit(100);
	} else if (t == -1) {
		printf0(S_TRYAGAIN);
		return 100;
	}
	while (cc_wait(&status) != 1)	/* 1: that child */
		;
	if ((t = (int16_t)(status & 0377)) != 0) {
		if (t != 2) {			/* SIGINT */
			printf1(S_FATAL, f);
			ww(eflag, 8);
		}
		dexit();
	}
	return (int16_t)((status >> 8) & 0377);
}

/*
 * 0eb2 copy - same: the string copied to tsp; when tsp has passed tsa +
 * CHSPACE (compared unsigned), tsa and tsp start a new block of
 * CHSPACE+50 bytes from malloc() - a name longer than what is left of the
 * 50 runs past the block first (past ts[] into av[]).
 */
static uint16_t
copy(uint16_t as)
{
	uint16_t otsp = rw(tsp), s = as, a;
	int16_t c;

	do {
		a = rw(tsp);
		ww(tsp, (uint16_t)(a + 1));
		c = cpb(a, s);
		s++;
	} while (c != 0);
	if ((uint16_t)(rw(tsa) + CHSPACE) < rw(tsp)) {
		a = cc_malloc(CHSPACE + 50);
		ww(tsa, a);
		ww(tsp, a);
		if (rw(tsp) == 0) {
			error(S_NOSPACE, 0);
			dexit();
		}
	}
	return otsp;
}

/*
 * 0f22 nodup - same: 0 if os, an object (".o"), is in the list l already.
 */
static int16_t
nodup(uint16_t l, uint16_t os)
{
	uint16_t s = os, t;
	int16_t c;

	if (getsuf(s) != 'o')
		return 1;
	for (;;) {
		t = rw(l);
		l = (uint16_t)(l + 2);
		if (t == 0)
			return 1;
		for (;;) {
			c = rb(s++);
			if (c == 0)
				break;
			if (rb(t++) != c)
				break;
		}
		if (rb(t) == 0 && c == 0)
			return 0;
		s = os;
	}
}

/* 0fa6 cunlink - same */
static void
cunlink(uint16_t f)
{
	if (f == 0)
		return;
	cc_unlink(f);
}
