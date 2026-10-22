/*
 * ld.c - the original MUTOS 1700 ld's functions, in the binary's order
 * (tests/realbin/ldmap.txt): v7/ld/ld.c's, with bytrel() new after
 * load2td(). Each function's comment gives its text offset and what the
 * binary does differently from v7; where nothing is said, it is v7's
 * code. The data are in the memory image (ld.h); the locals are host
 * variables of the original's widths.
 *
 * The MUTOS deltas, in short: an 8086 relocation word can mark a word
 * that starts one byte after its own position (bit 15, bytrel()); data
 * and BSS symbols are segment-local offsets (readhdr() clears cdrel and
 * cbrel); symbol types are compared as words, the spare byte included;
 * the text gets 6 bytes of "jmp $" (eb fe eb fe eb fe) at its end; -r and
 * -d are refused and all of v7's code for them is gone; -n sets iflag
 * (a 0411 output); the output is written under a temporary name in the
 * current directory and linked to a.out at the end; -lX tries /lib, then
 * /usr/lib; the entry point is the symbol's value (v7: | 1).
 *
 * The extension (docs/TOOLCHAIN_PLAN.md, decision 5): -r and -d do what
 * v7's do, with the binary's own relocation words (load2td() still
 * computes them, bit 15 kept). Every place is marked "EXTENSION"; without
 * -r and -d none of it runs.
 */
#include "ld.h"

int	x_rflag, x_arflag, x_dflag;

static	void	endload(int16_t argc, uint16_t argv);
static	void	record(int16_t c, uint16_t nam);
static	void	restore(int16_t vscan);
static	void	load1arg(uint16_t acp);
static	int16_t	step(int32_t nloc);
static	int16_t	ldrand(void);
static	int16_t	add(int16_t a, int16_t b, uint16_t s);
static	int16_t	load1(int16_t libflg, int32_t loc);
static	void	middle(void);
static	void	ldrsym(uint16_t asp, int16_t val, int16_t type);
static	void	setupout(void);
static	void	tcreat(uint16_t buf, int16_t tempflg);
static	void	load2arg(uint16_t acp);
static	void	load2(int32_t loc);
static	void	load2td(uint16_t lp, uint16_t creloc, uint16_t b1, uint16_t b2);
static	uint16_t bytrel(uint16_t t, uint16_t rel, uint16_t r, uint16_t creloc);
NORETURN static	void	finishout(void);
static	void	copy(uint16_t buf);
static	void	mkfsym(uint16_t s);
static	void	mget(uint16_t aloc, int16_t an);
static	void	mput(uint16_t buf, uint16_t aloc, int16_t an);
static	void	dseek(uint16_t asp, int32_t aloc, int16_t s);
static	int16_t	half(int16_t i);
static	uint16_t get(uint16_t asp);
static	int16_t	getfile(uint16_t acp);
static	uint16_t lookup(void);
static	uint16_t slookup(uint16_t s);
static	int16_t	enter(uint16_t hp);
static	void	symreloc(void);
static	void	error(int16_t n, uint16_t s);
static	uint16_t lookloc(uint16_t alp, uint16_t r);
static	void	readhdr(int32_t loc);
static	void	cp8c(uint16_t from, uint16_t to);
static	int16_t	eq(uint16_t s1, uint16_t s2);
static	void	putw(uint16_t w, uint16_t b);
static	void	flush(uint16_t b);

/* printf with one or two word arguments, as the original pushes them */
static void
pf1(uint16_t fmt, uint16_t a)
{
	ld_printf(fmt, &a, 1);
}

static void
pf0(uint16_t fmt)
{
	ld_printf(fmt, 0, 0);
}

/* a long shifted right by n: the original's sar/rcr pairs */
static int32_t
lsar(int32_t v, int n)
{
	uint32_t u = (uint32_t)v;

	while (n-- > 0)
		u = (u >> 1) | (u & 0x80000000u);
	return (int32_t)u;
}

/* (archdr.asize + sizeof archdr + 1) >> 1: a member's length in words */
static int32_t
member_words(void)
{
	return lsar((int32_t)((uint32_t)rl(AR_ASIZE) + 27u), 1);
}

/*
 * delexit (00b6). v7 unlinked "l.out"; the binary unlinks the name the
 * output was written under, if that is not the output's own name (no -o:
 * the temporary "..ldtmp" file, which finishout() has linked to a.out).
 */
void
delexit(void)
{
	if (outname != ofilename)
		sys_unlink(outname);
	if (delarg == 0)
		sys_chmod(ofilename, (int16_t)(0777 & ~sys_umask(0)));
	ld_exit(delarg);
}

/*
 * main (010a). v7's, but: mktemp() of the output's temporary name first
 * (outname, "..ldtmpXXXXXX"); -o sets outname to the output's name too;
 * -r and -d are refused - EXTENSION: here they are v7's; -n sets iflag
 * (the binary has no NMAGIC output; nflag is never set). The switch is a
 * hashed one (c % 6) - the same cases.
 */
int16_t
ldmain(int16_t argc, uint16_t argv)
{
	int16_t c, i, num, found, vscan;
	int16_t save;
	uint16_t ap, p;

	ld_signals();
	if (argc == 1)
		ld_exit(4);
	p = (uint16_t)(argv + 2);
	ld_mktemp(outname);
	for (c = 1; c < argc; c++) {
		if (trace)
			pf1(S_TRACE1, WU(p));
		filname = 0;
		ap = WU(p);
		p = (uint16_t)(p + 2);
		if (rb(ap) != '-') {
			load1arg(ap);
			continue;
		}
		for (i = 1; rb((uint16_t)(ap + i)) != 0; i++) {
			switch (rb((uint16_t)(ap + i))) {
			case 'o':
				if (++c >= argc)
					error(2, S_BADOUT);
				ofilename = WU(p);
				p = (uint16_t)(p + 2);
				outname = ofilename;
				ofilfnd++;
				continue;

			case 'u':
			case 'e': {
				uint16_t name;

				if (++c >= argc)
					error(2, S_BADUSE);
				name = WU(p);
				p = (uint16_t)(p + 2);
				enter(slookup(name));
				if (rb((uint16_t)(ap + i)) == 'e')
					entrypt = lastsym;
				continue;
			}

			case 'v':
				if (++c >= argc)
					error(2, S_VMISS);
				vflag = 1;
				vscan = vindex;
				found = 0;
				while (--vscan >= 0 && found == 0)
					found = eq(WU(VNODES + (uint16_t)(vscan * OV_SIZE) + 6),
					    WU(p));
				if (found) {
					endload(c, argv);
					restore(vscan);
				} else
					record(c, WU(p));
				p = (uint16_t)(p + 2);
				continue;

			case 'D':
				if (++c >= argc)
					error(2, S_DMISS);
				num = ld_atoi(WU(p));
				p = (uint16_t)(p + 2);
				if ((int16_t)dsize > num)
					error(2, S_DSMALL);
				dsize = (uint16_t)num;
				continue;

			case 'l':
				save = rb((uint16_t)(ap + --i));
				wb((uint16_t)(ap + i), '-');
				load1arg((uint16_t)(ap + i));
				wb((uint16_t)(ap + i), save);
				break;

			case 'x':
				xflag++;
				continue;

			case 'X':
				Xflag++;
				continue;

			case 'S':
				Sflag++;
				continue;

			case 'r':
				/* EXTENSION: the original refuses -r and -d -
				   error(2, " -r and -d flags not supported") */
				x_rflag++;
				x_arflag++;
				continue;

			case 'd':
				x_dflag++;	/* EXTENSION, as -r */
				continue;

			case 's':
				sflag++;
				xflag++;
				continue;

			case 'n':
			case 'i':
				iflag++;
				continue;

			case 'O':
				Oflag++;
				continue;

			case 't':
				trace++;
				continue;

			default:
				error(2, S_BADFLAG);
			}
			break;
		}
	}
	endload(argc, argv);
	return 0;	/* not reached: endload() ends in delexit() */
}

/*
 * endload (04b6). v7's; -D pads the data without relocation words
 * (EXTENSION: with them under -r, as v7).
 */
static void
endload(int16_t argc, uint16_t argv)
{
	int16_t c, i, dnum;
	uint16_t ap, p;

	filname = 0;
	middle();
	setupout();
	p = (uint16_t)(argv + 2);
	libp = LIBLIST;
	for (c = 1; c < argc; c++) {
		ap = WU(p);
		p = (uint16_t)(p + 2);
		if (trace)
			pf1(S_TRACE2, ap);
		if (rb(ap) != '-') {
			load2arg(ap);
			continue;
		}
		for (i = 1; rb((uint16_t)(ap + i)) != 0; i++) {
			int16_t ch = rb((uint16_t)(ap + i));

			if (ch == 'l') {
				wb((uint16_t)(ap + --i), '-');
				load2arg((uint16_t)(ap + i));
				break;
			}
			if (ch == 'D') {
				for (dnum = ld_atoi(WU(p)); (int16_t)dorigin < dnum;
				    dorigin = (uint16_t)(dorigin + 2)) {
					putw(0, DOUTB);
					if (x_rflag)		/* EXTENSION */
						putw(0, DROUTB);
				}
			}
			if (ch == 'D' || ch == 'e' || ch == 'o' || ch == 'u'
			    || ch == 'v') {
				++c;
				p = (uint16_t)(p + 2);
			}
		}
	}
	finishout();
}

/* record (05ea) */
static void
record(int16_t c, uint16_t nam)
{
	uint16_t v = (uint16_t)(VNODES + (uint16_t)(vindex * OV_SIZE));

	vindex++;
	WI(v) = c;
	WI(v + 2) = symindex;
	WU(v + 4) = libp;
	WU(v + 6) = nam;
	WU(v + 14) = tsize;
	WU(v + 16) = dsize;
	WU(v + 18) = bsize;
	WU(v + 20) = ssize;
	WU(v + 8) = ctrel;
	WU(v + 10) = cdrel;
	WU(v + 12) = cbrel;
}

/* restore (0654) - not reached: endload() before it does not return */
static void
restore(int16_t vscan)
{
	uint16_t v = (uint16_t)(VNODES + (uint16_t)(vscan * OV_SIZE));
	int16_t saved;

	vindex = (int16_t)(vscan + 1);
	libp = WU(v + 4);
	ctrel = WU(v + 8);
	cdrel = WU(v + 10);
	cbrel = WU(v + 12);
	tsize = WU(v + 14);
	dsize = WU(v + 16);
	bsize = WU(v + 18);
	ssize = WU(v + 20);
	saved = WI(v + 2);
	while (symindex > saved) {
		symindex--;
		WU(WU(SYMHASH + 2 * (uint16_t)symindex)) = 0;
	}
}

/*
 * load1arg (06d6). v7's. The table of contents is read with tnum * 12
 * as its byte count, tnum the low word of asize / 12 (libc.a's ldiv) -
 * a negative one reads as far as the file goes (sys_read()).
 */
static void
load1arg(uint16_t acp)
{
	uint16_t cp = acp;
	int32_t nloc;

	switch ((uint16_t)getfile(cp)) {
	case 0:
		load1(0, 0);
		break;

	case 1:		/* regular archive */
		nloc = 1;
		while (step(nloc))
			nloc = (int32_t)((uint32_t)nloc + (uint32_t)member_words());
		break;

	case 2: {	/* table of contents */
		int32_t a = rl(AR_ASIZE);

		/* ldiv's short path: |a| / 12, the sign put back */
		uint32_t q = (a < 0 ? 0u - (uint32_t)a : (uint32_t)a) / 12u;

		tnum = (int16_t)(a < 0 ? 0u - q : q);
		if (tnum >= TABSZ)
			error(2, S_FASTLOAD);
		sys_lseek(infil, 2 + ARHSZ, 0);
		sys_read(infil, TAB, (uint16_t)(tnum * 12));
		while (ldrand())
			;
		wl(libp, -1);
		libp = (uint16_t)(libp + 4);
		break;
	}

	case 3:		/* out of date table of contents */
		error(0, S_OUTOFDATE);
		for (nloc = (int32_t)(1u + (uint32_t)member_words()); step(nloc);
		    nloc = (int32_t)((uint32_t)nloc + (uint32_t)member_words()))
			;
		break;
	}
	sys_close(infil);
}

/* step (085e). v7's; libp runs past liblist[] unchecked, as in v7. */
static int16_t
step(int32_t nloc)
{
	dseek(TEXT_S, nloc, ARHSZ);
	if (text_size <= 0) {
		wl(libp, -1);
		libp = (uint16_t)(libp + 4);
		return 0;
	}
	mget(ARCHDR, ARHSZ);
	if (load1(1, (int32_t)((uint32_t)nloc + ARHSZ / 2))) {
		wl(libp, nloc);
		libp = (uint16_t)(libp + 4);
	}
	return 1;
}

/*
 * ldrand (0904). v7's. A name not in the symbol table gives a null
 * symbol pointer, and the type is read through it: crt0's word 8 (the
 * segment of Iscadr, never EXTERN+UNDEF), so the entry is skipped.
 */
static int16_t
ldrand(void)
{
	int16_t i;
	uint16_t pp, sp, oldp = libp;

	for (i = 0; i < tnum; i++) {
		uint16_t t = (uint16_t)(TAB + (uint16_t)(i * 12));

		if ((pp = slookup(t)) == 0)
			continue;
		sp = WU(pp);
		if (RU(sp + SY_TYPE) != EXTERN + UNDEF)
			continue;
		step(lsar(rl((uint16_t)(t + 8)), 1));
	}
	return oldp != libp;
}

/* add (0994) */
static int16_t
add(int16_t a, int16_t b, uint16_t s)
{
	uint32_t r = (uint32_t)(uint16_t)a + (uint16_t)b;

	if (r >= 0200000)
		error(1, s);
	return (int16_t)r;
}

/*
 * load1 (09f0). v7's, with the symbol type a word (cursym.stype and the
 * spare byte): EXTERN+UNDEF etc. compare with both. The symbol table's
 * position is loc + 8 + tsize + dsize, the sum in 16 bits.
 */
static int16_t
load1(int16_t libflg, int32_t loc)
{
	uint16_t sp;
	int16_t savindex, ndef, nloc, type, mtype;

	readhdr(loc);
	ctrel = tsize;
	cdrel = (uint16_t)(cdrel + dsize);
	cbrel = (uint16_t)(cbrel + bsize);
	ndef = 0;
	nloc = SYMSZ;
	savindex = symindex;
	if ((f_relflg & RELFLG) == 1) {
		error(1, S_NORELOC);
		return 0;
	}
	loc = (int32_t)((uint32_t)loc + (uint16_t)(f_tsize + f_dsize + HDRSZ / 2));
	dseek(TEXT_S, loc, (int16_t)f_ssize);
	while (text_size > 0) {
		mget(CURSYM, SYMSZ);
		type = (int16_t)cs_type;
		if (Sflag) {
			mtype = type & 037;
			if (mtype == 1 || mtype > 4)
				continue;
		}
		if ((type & EXTERN) == 0) {
			if (Xflag == 0 || rb(CURSYM) != 'L')
				nloc = (int16_t)(nloc + SYMSZ);
			continue;
		}
		symreloc();
		if (enter(lookup()))
			continue;
		if (WU((sp = lastsym) + SY_TYPE) != EXTERN + UNDEF)
			continue;
		if (cs_type == EXTERN + UNDEF) {
			if ((int16_t)cs_value > WI(sp + SY_VALUE))
				WU(sp + SY_VALUE) = cs_value;
			continue;
		}
		if (WU(sp + SY_VALUE) != 0 && cs_type == EXTERN + TEXT)
			continue;
		ndef++;
		WU(sp + SY_TYPE) = cs_type;
		WU(sp + SY_VALUE) = cs_value;
	}
	if (libflg == 0 || ndef) {
		tsize = (uint16_t)add((int16_t)tsize, (int16_t)f_tsize, S_TEXTOVF);
		dsize = (uint16_t)add((int16_t)dsize, (int16_t)f_dsize, S_DATAOVF);
		bsize = (uint16_t)add((int16_t)bsize, (int16_t)f_bsize, S_BSSOVF1);
		ssize = (uint16_t)add((int16_t)ssize, nloc, S_SYMOVF);
		return 1;
	}
	/*
	 * No symbols defined by this library member.
	 * Rip out the hash table entries and reset the symbol table.
	 */
	while (symindex > savindex) {
		symindex--;
		WU(WU(SYMHASH + 2 * (uint16_t)symindex)) = 0;
	}
	return 0;
}

/*
 * middle (0bf8). v7's without rflag (no relocation bits are kept for
 * undefined symbols; the commons are always assigned; every undefined
 * symbol is listed), and the text gets 6 more bytes - the three "jmp $"
 * finishout() writes - before it is rounded for -i; -i's (and -n's)
 * rounding to 64 bytes; nflag's 4K page (v7: 8K) is never reached.
 * EXTENSION: v7's rflag, dflag and arflag where v7 has them.
 */
static void
middle(void)
{
	uint16_t sp, symp;
	int16_t t, csize, nund, corigin;

	torigin = 0;
	dorigin = 0;
	borigin = 0;

	p_etext = WU(slookup(S_ETEXT));
	p_edata = WU(slookup(S_EDATA));
	p_end = WU(slookup(S_END));
	symp = (uint16_t)(SYMTAB + (uint16_t)(symindex * SYMSZ));
	if (x_rflag)				/* EXTENSION */
		nflag = sflag = iflag = Oflag = 0;
	/*
	 * Assign common locations.
	 */
	csize = 0;
	if (x_dflag || x_rflag == 0) {		/* EXTENSION: the test */
		ldrsym(p_etext, (int16_t)tsize, EXTERN + TEXT);
		ldrsym(p_edata, (int16_t)dsize, EXTERN + DATA);
		ldrsym(p_end, (int16_t)bsize, EXTERN + BSS);
		for (sp = SYMTAB; sp < symp; sp = (uint16_t)(sp + SYMSZ))
			if (WU(sp + SY_TYPE) == EXTERN + UNDEF
			    && (t = WI(sp + SY_VALUE)) != 0) {
				t = (int16_t)((t + 1) & ~01);
				WI(sp + SY_VALUE) = csize;
				WU(sp + SY_TYPE) = EXTERN + COMM;
				csize = add(csize, t, S_BSSOVF2);
			}
	}
	/*
	 * Now set symbols to their final value
	 */
	tsize = (uint16_t)(tsize + 6);
	if (nflag || iflag)
		tsize = (uint16_t)((tsize + 077) & ~077);
	dorigin = tsize;
	if (nflag)
		dorigin = (uint16_t)((tsize + 07777) & ~07777);
	if (iflag)
		dorigin = 0;
	corigin = (int16_t)(dorigin + dsize);
	borigin = (uint16_t)(corigin + csize);
	if (x_rflag) {
		/*
		 * EXTENSION: a relocatable output keeps MUTOS's convention
		 * for objects - a data or BSS value is an offset in its own
		 * segment (readhdr() reads it so): the data at 0, the BSS at
		 * 0 with -d's commons first. (v7's are addresses after the
		 * text, which its readhdr() takes off again.)
		 */
		dorigin = 0;
		corigin = 0;
		borigin = (uint16_t)csize;
	}
	nund = 0;
	for (sp = SYMTAB; sp < symp; sp = (uint16_t)(sp + SYMSZ)) {
		switch (WU(sp + SY_TYPE)) {
		case EXTERN + UNDEF:
			errlev |= 01;
			if (x_arflag == 0 && WU(sp + SY_VALUE) == 0) {
				/* EXTENSION: arflag, v7's */
				if (nund == 0)
					pf0(S_UNDEFINED);
				nund++;
				pf1(S_NAMENL, sp);
			}
			continue;

		case EXTERN + ABS:
		default:
			continue;

		case EXTERN + TEXT:
			WU(sp + SY_VALUE) = (uint16_t)(WU(sp + SY_VALUE) + torigin);
			continue;

		case EXTERN + DATA:
			WU(sp + SY_VALUE) = (uint16_t)(WU(sp + SY_VALUE) + dorigin);
			continue;

		case EXTERN + BSS:
			WU(sp + SY_VALUE) = (uint16_t)(WU(sp + SY_VALUE) + borigin);
			continue;

		case EXTERN + COMM:
			WU(sp + SY_TYPE) = EXTERN + BSS;
			WU(sp + SY_VALUE) = (uint16_t)(WU(sp + SY_VALUE) + corigin);
			continue;
		}
	}
	if (sflag || xflag)
		ssize = 0;
	bsize = (uint16_t)add((int16_t)bsize, csize, S_BSSOVF3);
	nsym = (uint16_t)(ssize / SYMSZ);
}

/* ldrsym (0e34) */
static void
ldrsym(uint16_t asp, int16_t val, int16_t type)
{
	uint16_t sp;

	if ((sp = asp) == 0)
		return;
	if (WU(sp + SY_TYPE) != EXTERN + UNDEF || WU(sp + SY_VALUE) != 0) {
		pf1(S_NAMEC1, sp);
		error(1, S_MULTDEF1);
		return;
	}
	WI(sp + SY_TYPE) = type;
	WI(sp + SY_VALUE) = val;
}

/*
 * setupout (0e88). v7's without the relocation buffers (EXTENSION: with
 * them under -r) and with the entry point the symbol's value (v7: | 1);
 * "Entry point not in text" leaves the header's entry word what the last
 * header read had. The output is created under outname.
 */
static void
setupout(void)
{
	tcreat(TOUTB, 0);
	ld_mktemp(TFNAME);
	tcreat(DOUTB, 1);
	if (sflag == 0 || xflag == 0)
		tcreat(SOUTB, 1);
	if (x_rflag) {				/* EXTENSION */
		tcreat(TROUTB, 1);
		tcreat(DROUTB, 1);
	}
	f_magic = Oflag ? OMAGIC : iflag ? IMAGIC : nflag ? NMAGIC : FMAGIC;
	f_tsize = tsize;
	f_dsize = dsize;
	f_bsize = bsize;
	f_ssize = sflag ? 0 : (uint16_t)(ssize + (uint16_t)(SYMSZ * symindex));
	if (entrypt) {
		if (WU(entrypt + SY_TYPE) != EXTERN + TEXT)
			error(1, S_ENTRY);
		else
			f_entry = WU(entrypt + SY_VALUE);
	} else
		f_entry = 0;
	f_pad = 0;
	f_relflg = x_rflag == 0;		/* EXTENSION: v7's; the binary: 1 */
	mput(TOUTB, FILHDR, HDRSZ);
}

/* tcreat (0faa) */
static void
tcreat(uint16_t buf, int16_t tempflg)
{
	int16_t ufd;
	uint16_t nam;

	nam = tempflg ? TFNAME : outname;
	if ((ufd = sys_creat(nam, 0666)) < 0)
		error(2, tempflg ? S_CANTTEMP : S_CANTOUT);
	sys_close(ufd);
	WI(buf + BF_FILDES) = sys_open(nam, 2);
	if (tempflg)
		sys_unlink(TFNAME);
	WI(buf + BF_NLEFT) = 256;
	WU(buf + BF_XNEXT) = (uint16_t)(buf + BF_IOBUF);
}

/*
 * load2arg (1042). v7's: the file symbol's name is the part after the
 * last '/', found from the end - a name without one is scanned back to
 * the byte before it.
 */
static void
load2arg(uint16_t acp)
{
	uint16_t cp = acp, lp;

	if (getfile(cp) == 0) {
		while (rb(cp) != 0)
			cp++;
		while (cp >= acp && rb(--cp) != '/')
			;
		mkfsym(++cp);
		load2(0);
	} else {	/* scan archive members referenced */
		for (lp = libp; !(WU(lp) == 0xffff && WU(lp + 2) == 0xffff);
		    lp = (uint16_t)(lp + 4)) {
			dseek(TEXT_S, rl(lp), ARHSZ);
			mget(ARCHDR, ARHSZ);
			mkfsym(ARCHDR);
			load2((int32_t)((uint32_t)rl(lp) + ARHSZ / 2));
		}
		libp = (uint16_t)(lp + 4);
	}
	sys_close(infil);
}

/*
 * load2 (1110). v7's, the types words. The symbol table is at loc + 8 +
 * tsize + dsize with each size sign-extended (load1() adds them in 16
 * bits); the relocation of the data at loc + 8 + tsize + dsize/2, tsize
 * sign-extended.
 */
static void
load2(int32_t loc)
{
	uint16_t sp, lp;
	int16_t symno, type, mtype;

	readhdr(loc);
	ctrel = torigin;
	cdrel = (uint16_t)(cdrel + dorigin);
	cbrel = (uint16_t)(cbrel + borigin);
	/*
	 * Reread the symbol table, recording the numbering
	 * of symbols for fixing external references.
	 */
	lp = LOCAL;
	symno = -1;
	loc = (int32_t)((uint32_t)loc + HDRSZ / 2);
	dseek(TEXT_S, (int32_t)((uint32_t)loc + (uint32_t)(int32_t)(int16_t)f_tsize
	    + (uint32_t)(int32_t)(int16_t)f_dsize), (int16_t)f_ssize);
	while (text_size > 0) {
		symno++;
		mget(CURSYM, SYMSZ);
		symreloc();
		type = (int16_t)cs_type;
		if (Sflag) {
			mtype = type & 037;
			if (mtype == 1 || mtype > 4)
				continue;
		}
		if ((type & EXTERN) == 0) {
			if (!sflag && !xflag && (!Xflag || rb(CURSYM) != 'L'))
				mput(SOUTB, CURSYM, SYMSZ);
			continue;
		}
		if ((sp = WU(lookup())) == 0)
			error(2, S_INTERNAL);
		if (cs_type == EXTERN + UNDEF) {
			if (lp >= LOCEND)
				error(2, S_LOCOVF);
			WI(lp) = symno;
			WU(lp + 2) = sp;
			lp = (uint16_t)(lp + 4);
			continue;
		}
		if (cs_type != WU(sp + SY_TYPE) || cs_value != WU(sp + SY_VALUE)) {
			pf1(S_NAMEC2, CURSYM);
			error(1, S_MULTDEF2);
		}
	}
	dseek(TEXT_S, loc, (int16_t)f_tsize);
	dseek(RELOC_S, (int32_t)((uint32_t)loc + (uint32_t)(int32_t)
	    half((int16_t)(f_tsize + f_dsize))), (int16_t)f_tsize);
	load2td(lp, ctrel, TOUTB, TROUTB);
	dseek(TEXT_S, (int32_t)((uint32_t)loc + (uint32_t)(int32_t)
	    half((int16_t)f_tsize)), (int16_t)f_dsize);
	dseek(RELOC_S, (int32_t)((uint32_t)loc + (uint32_t)(int32_t)(int16_t)f_tsize
	    + (uint32_t)(int32_t)half((int16_t)f_dsize)), (int16_t)f_dsize);
	load2td(lp, cdrel, DOUTB, DROUTB);
	torigin = (uint16_t)(torigin + f_tsize);
	dorigin = (uint16_t)(dorigin + f_dsize);
	borigin = (uint16_t)(borigin + f_bsize);
}

/*
 * load2td (1362). v7's word loop; each relocation goes through bytrel()
 * (bit 15: the word starts at the next byte; bit 0: relative to the
 * segment's place) - except an external reference to an undefined
 * symbol, whose word is written as it is: not through bytrel(), so a
 * byte-shifted word before it is completed only by the word after. The
 * symbol index is the relocation word's bits 4 to 14. The relocation
 * word for -r is still computed, bit 15 kept (r & 0100001), and not
 * written - EXTENSION: written under -r, as v7.
 */
static void
load2td(uint16_t lp, uint16_t creloc, uint16_t b1, uint16_t b2)
{
	uint16_t r, t, sp;

	for (;;) {
		/* next text or data word */
		if (--text_size <= 0) {
			if (text_size < 0)
				break;
			text_size++;
			t = get(TEXT_S);
		} else if (--text_nibuf < 0) {
			text_nibuf++;
			text_size++;
			t = get(TEXT_S);
		} else {
			t = WU(text_ptr);
			text_ptr = (uint16_t)(text_ptr + 2);
		}

		/* next relocation word */
		if (--reloc_size <= 0) {
			if (reloc_size < 0)
				error(2, S_RELERR);
			reloc_size++;
			r = get(RELOC_S);
		} else if (--reloc_nibuf < 0) {
			reloc_nibuf++;
			reloc_size++;
			r = get(RELOC_S);
		} else {
			r = WU(reloc_ptr);
			reloc_ptr = (uint16_t)(reloc_ptr + 2);
		}

		switch (r & 016) {
		case RTEXT:
			t = bytrel(t, ctrel, r, creloc);
			break;

		case RDATA:
			t = bytrel(t, cdrel, r, creloc);
			break;

		case RBSS:
			t = bytrel(t, cbrel, r, creloc);
			break;

		case REXT:
			sp = lookloc(lp, r & 077777);
			if (WU(sp + SY_TYPE) == EXTERN + UNDEF) {
				/*
				 * EXTENSION: under -r the word stays relative
				 * to the output (v7: t -= creloc for a
				 * pc-relative one), through bytrel(), which
				 * also completes a byte-shifted word before it
				 */
				if (x_rflag)
					t = bytrel(t, 0, r, creloc);
				r = (uint16_t)((r & 0100001)
				    + (uint16_t)((uint16_t)(nsym + (uint16_t)
				    ((sp - SYMTAB) / SYMSZ)) << 4) + REXT);
				break;
			}
			t = bytrel(t, WU(sp + SY_VALUE), r, creloc);
			r = (uint16_t)((r & 0100001) + (uint16_t)(WU(sp + SY_TYPE) << 1)
			    - 2 * (EXTERN + ABS));
			break;

		default:
			t = bytrel(t, 0, r, 0);
			break;
		}
		putw(t, b1);
		if (x_rflag)			/* EXTENSION */
			putw(r, b2);
	}
}

/*
 * bytrel (153a) - new: adds rel to the word t, or - relocation bit 15 -
 * to the word that starts at t's second byte: its low byte is t's high
 * byte, its high byte the next word's low byte, which the next call
 * completes (with the carry out of the low byte) before it relocates
 * that word itself. Bit 0: minus creloc. The state is three statics.
 */
static uint16_t
bytrel(uint16_t t, uint16_t rel, uint16_t r, uint16_t creloc)
{
	uint16_t x;

	if (br_pend) {
		x = br_carry;
		br_carry = 0;
		x = (uint16_t)(x + ((br_rel >> 8) & 0xff));
		x = (uint16_t)(x + (t & 0xff));
		x = (uint16_t)((t & 0xff00) | (x & 0xff));
		br_carry = br_pend = br_rel = 0;
		t = x;
	}
	if ((r & 0x8000) == 0) {
		if ((r & 1) == 0)
			return (uint16_t)(t + rel);
		return (uint16_t)(t + rel - creloc);
	}
	if (r & 1)
		rel = (uint16_t)(rel - creloc);
	br_rel = rel;
	br_pend = 1;
	x = (uint16_t)(((t >> 8) & 0xff) + (rel & 0xff));
	br_carry = (uint16_t)(x >> 8);
	return (uint16_t)((uint16_t)(x << 8) | (t & 0xff));
}

/*
 * finishout (162c). Three "jmp $" (eb fe) after the text (middle() has
 * counted them), then -i's zero words to 64 bytes; no relocation copies
 * (EXTENSION: v7's under -r). Without -o the output, written as outname,
 * is linked to a.out: "a.out" is removed first.
 */
static void
finishout(void)
{
	int16_t i;
	uint16_t n, p;

	for (i = 0; i != 3; i++) {
		putw(0xfeeb, TOUTB);
		if (x_rflag)			/* EXTENSION */
			putw(0, TROUTB);
	}
	torigin = (uint16_t)(torigin + 6);
	if (nflag || iflag) {
		n = torigin;
		while (n & 077) {
			n = (uint16_t)(n + 2);
			putw(0, TOUTB);
		}
	}
	copy(DOUTB);
	if (x_rflag) {				/* EXTENSION */
		copy(TROUTB);
		copy(DROUTB);
	}
	if (sflag == 0) {
		if (xflag == 0)
			copy(SOUTB);
		for (p = SYMTAB; (uint16_t)(SYMTAB + (uint16_t)(symindex * SYMSZ)) > p;
		    p = (uint16_t)(p + 2))
			putw(WU(p), TOUTB);
	}
	flush(TOUTB);
	sys_close(WI(TOUTB + BF_FILDES));
	if (outname != ofilename) {
		sys_unlink(ofilename);
		sys_link(outname, ofilename);
	}
	delarg = errlev;
	delexit();
}

/* copy (1724) */
static void
copy(uint16_t buf)
{
	int16_t f, n;
	uint16_t p;

	flush(buf);
	sys_lseek(f = WI(buf + BF_FILDES), 0, 0);
	while ((n = sys_read(f, DOUTB + BF_IOBUF, 512)) > 1) {
		n >>= 1;
		p = DOUTB + BF_IOBUF;
		do {
			putw(WU(p), TOUTB);
			p = (uint16_t)(p + 2);
		} while (--n);
	}
	sys_close(f);
}

/* mkfsym (179a). The type is written as a word: 037, spare 0. */
static void
mkfsym(uint16_t s)
{
	if (sflag || xflag)
		return;
	cp8c(s, CURSYM);
	cs_type = 037;
	cs_value = torigin;
	mput(SOUTB, CURSYM, SYMSZ);
}

/* mget (17ea) */
static void
mget(uint16_t aloc, int16_t an)
{
	uint16_t loc, p;
	int16_t n;

	n = (int16_t)(an >> 1);
	loc = aloc;
	if ((text_nibuf = (int16_t)(text_nibuf - n)) >= 0) {
		if ((text_size = (int16_t)(text_size - n)) > 0) {
			p = text_ptr;
			do {
				WU(loc) = WU(p);
				loc = (uint16_t)(loc + 2);
				p = (uint16_t)(p + 2);
			} while (--n);
			text_ptr = p;
			return;
		} else
			text_size = (int16_t)(text_size + n);
	}
	text_nibuf = (int16_t)(text_nibuf + n);
	do {
		WU(loc) = get(TEXT_S);
		loc = (uint16_t)(loc + 2);
	} while (--n);
}

/* mput (1860) */
static void
mput(uint16_t buf, uint16_t aloc, int16_t an)
{
	uint16_t loc = aloc;
	int16_t n = (int16_t)(an >> 1);

	do {
		putw(WU(loc), buf);
		loc = (uint16_t)(loc + 2);
	} while (--n);
}

/*
 * dseek (188a). v7's. A page's block number is compared sign-extended
 * with the long b; the read's count of words is the byte count >> 1.
 */
static void
dseek(uint16_t asp, int32_t aloc, int16_t s)
{
	uint16_t sp, p;
	int32_t b;
	uint16_t o;
	int16_t n;

	b = lsar(aloc, 8);
	o = (uint16_t)(aloc & 0377);
	sp = asp;
	WI(WU(sp + ST_PNO) + PG_NUSER) -= 1;
	if ((int32_t)WI((p = PAGE0) + PG_BNO) != b
	    && (int32_t)WI((p = PAGE1) + PG_BNO) != b) {
		if (WI(p + PG_NUSER) == 0 || WI((p = PAGE0) + PG_NUSER) == 0) {
			if (WI(PAGE0 + PG_NUSER) == 0 && WI(PAGE1 + PG_NUSER) == 0)
				if (WI(PAGE0 + PG_BNO) < WI(PAGE1 + PG_BNO))
					p = PAGE0;
			WU(p + PG_BNO) = (uint16_t)b;
			sys_lseek(infil, (int32_t)(((uint32_t)aloc & ~0377u) << 1), 0);
			if ((n = (int16_t)(sys_read(infil, (uint16_t)(p + PG_BUFF), 512)
			    >> 1)) < 0)
				n = 0;
			WI(p + PG_NIBUF) = n;
		} else
			error(2, S_NOPAGES);
	}
	WI(p + PG_NUSER) += 1;
	WU(sp + ST_BNO) = (uint16_t)b;
	WU(sp + ST_PNO) = p;
	WU(sp + ST_PTR) = (uint16_t)(p + PG_BUFF + (uint16_t)(o << 1));
	if (s != -1)
		WI(sp + ST_SIZE) = half(s);
	if ((WI(sp + ST_NIBUF) = (int16_t)(WI(p + PG_NIBUF) - (int16_t)o)) <= 0)
		WI(sp + ST_SIZE) = 0;
}

/* half (19e6) */
static int16_t
half(int16_t i)
{
	return (int16_t)((i >> 1) & 077777);
}

/* get (1a02) */
static uint16_t
get(uint16_t asp)
{
	uint16_t sp = asp, w;

	if (--WI(sp + ST_NIBUF) < 0) {
		dseek(sp, (int32_t)((uint32_t)(int32_t)(int16_t)(WI(sp + ST_BNO) + 1)
		    << 8), -1);
		--WI(sp + ST_NIBUF);
	}
	if (--WI(sp + ST_SIZE) <= 0) {
		if (WI(sp + ST_SIZE) < 0)
			error(2, PREMEOF);
		WI(FPAGE + PG_NUSER) += 1;
		WI(WU(sp + ST_PNO) + PG_NUSER) -= 1;
		WU(sp + ST_PNO) = FPAGE;
	}
	w = WU(WU(sp + ST_PTR));
	WU(sp + ST_PTR) = (uint16_t)(WU(sp + ST_PTR) + 2);
	return w;
}

/*
 * getfile (1a76). v7's. -lX: "/lib/libX.a", then "/usr/lib/libX.a",
 * written into the string "/usr/lib/libxxxxxxxxxxxxxxx" - a name of 14
 * characters or more runs over the strings after it. The archive's
 * time is compared as a signed long with the file's.
 */
static int16_t
getfile(uint16_t acp)
{
	uint16_t cp;
	int16_t c;
	int32_t mtime;

	cp = acp;
	infil = -1;
	wb(ARCHDR, 0);
	filname = cp;
	if (rb(cp) == '-' && rb((uint16_t)(cp + 1)) == 'l') {
		if (rb((uint16_t)(cp + 2)) == '\0')
			cp = S_LA;
		filname = S_LIBNAME;
		for (c = 0; rb((uint16_t)(cp + c + 2)) != 0; c++)
			wb((uint16_t)(filname + c + 12), rb((uint16_t)(cp + c + 2)));
		wb((uint16_t)(filname + c + 12), '.');
		wb((uint16_t)(filname + c + 13), 'a');
		wb((uint16_t)(filname + c + 14), '\0');
		if ((infil = sys_open((uint16_t)(filname + 4), 0)) >= 0)
			filname = (uint16_t)(filname + 4);
	}
	if (infil == -1 && (infil = sys_open(filname, 0)) < 0)
		error(2, S_CANTOPEN);
	WI(PAGE0 + PG_BNO) = WI(PAGE1 + PG_BNO) = -1;
	WI(PAGE0 + PG_NUSER) = WI(PAGE1 + PG_NUSER) = 0;
	WU(TEXT_S + ST_PNO) = WU(RELOC_S + ST_PNO) = FPAGE;
	WI(FPAGE + PG_NUSER) = 2;
	dseek(TEXT_S, 0, 2);
	if (text_size <= 0)
		error(2, PREMEOF);
	if (get(TEXT_S) != ARCMAGIC)
		return 0;	/* regular file */
	dseek(TEXT_S, 1, ARHSZ);	/* word addressing */
	if (text_size <= 0)
		return 1;	/* regular archive */
	mget(ARCHDR, ARHSZ);
	if (cstrncmp(ARCHDR, GOODNM, 14) != 0)
		return 1;	/* regular archive */
	sys_fstat_mtime(infil, &mtime);
	if (mtime > rl(AR_ATIME))
		return 3;
	return 2;
}

/* lookup (1c60). v7's: the hash of the 8 bytes as signed chars. */
static uint16_t
lookup(void)
{
	int16_t i, clash;
	uint16_t hp, cp, cp1;

	i = 0;
	for (cp = CURSYM; cp < CURSYM + 8;)
		i = (int16_t)((uint16_t)((uint16_t)i << 1) + (uint16_t)rb(cp++));
	for (hp = (uint16_t)(HSHTAB + 2 * ((i & 077777) % NSYM + 2)); WU(hp) != 0;) {
		cp1 = WU(hp);
		clash = 0;
		for (cp = CURSYM; cp < CURSYM + 8;)
			if (rb(cp++) != rb(cp1++)) {
				clash = 1;
				break;
			}
		if (clash) {
			hp = (uint16_t)(hp + 2);
			if (hp >= HSHEND)
				hp = HSHTAB;
		} else
			break;
	}
	return hp;
}

/* slookup (1d14). The type is written as a word, spare 0. */
static uint16_t
slookup(uint16_t s)
{
	cp8c(s, CURSYM);
	cs_type = EXTERN + UNDEF;
	cs_value = 0;
	return lookup();
}

/* enter (1d42). The type is copied as a word, the spare byte with it. */
static int16_t
enter(uint16_t hp)
{
	uint16_t sp;

	if (WU(hp) == 0) {
		if (symindex >= NSYM)
			error(2, S_SYMTOVF);
		WU(SYMHASH + 2 * (uint16_t)symindex) = hp;
		sp = (uint16_t)(SYMTAB + (uint16_t)(symindex * SYMSZ));
		symindex++;
		lastsym = sp;
		WU(hp) = sp;
		cp8c(CURSYM, sp);
		WU(sp + SY_TYPE) = cs_type;
		WU(sp + SY_VALUE) = cs_value;
		return 1;
	}
	lastsym = WU(hp);
	return 0;
}

/*
 * symreloc (1dd8). v7's, the type a word: a spare byte other than 0
 * makes any type "default" - EXTERN+ABS if the EXTERN bit is set.
 */
static void
symreloc(void)
{
	switch (cs_type) {
	case TEXT:
	case EXTERN + TEXT:
		cs_value = (uint16_t)(cs_value + ctrel);
		return;

	case DATA:
	case EXTERN + DATA:
		cs_value = (uint16_t)(cs_value + cdrel);
		return;

	case BSS:
	case EXTERN + BSS:
		cs_value = (uint16_t)(cs_value + cbrel);
		return;

	case EXTERN + UNDEF:
		return;
	}
	if (cs_type & EXTERN)
		cs_type = EXTERN + ABS;
}

/*
 * error (1e62). v7's; the messages go to stdout (printf). A warning
 * (n 0) sets errlev back to 0.
 */
static void
error(int16_t n, uint16_t s)
{
	if (errlev == 0)
		pf0(S_LDCOLON);
	if (filname) {
		pf1(S_PCTS, filname);
		if (rb(ARCHDR))
			pf1(S_ANAME, ARCHDR);
		pf0(S_COLON);
	}
	pf1(S_PCTSNL, s);
	if (n > 1)
		delexit();
	errlev = n;
}

/* lookloc (1ede) */
static uint16_t
lookloc(uint16_t alp, uint16_t r)
{
	uint16_t clp, lp = alp;
	int16_t sn;

	sn = (int16_t)(((int16_t)r >> 4) & 07777);
	for (clp = LOCAL; clp < lp; clp = (uint16_t)(clp + 4))
		if (WI(clp) == sn)
			return WU(clp + 2);
	error(2, S_LOCBOTCH);
	return 0;	/* not reached */
}

/*
 * readhdr (1f34). cdrel and cbrel are set to 0 (v7: -tsize and
 * -(tsize+dsize)): a data or BSS symbol's value is its offset in its own
 * segment.
 */
static void
readhdr(int32_t loc)
{
	dseek(TEXT_S, loc, HDRSZ);
	mget(FILHDR, HDRSZ);
	if (f_magic != FMAGIC)
		error(2, S_BADFMT);
	f_tsize = (uint16_t)((f_tsize + 01) & ~01);
	cdrel = 0;
	cbrel = 0;
	f_bsize = (uint16_t)((f_bsize + 01) & ~01);
}

/* cp8c (1fae) */
static void
cp8c(uint16_t from, uint16_t to)
{
	uint16_t f = from, t = to, te = (uint16_t)(to + 8);
	int16_t c;

	do {
		c = rb(f++);
		wb(t++, c);
	} while (c != 0 && t < te);
	while (t < te)
		wb(t++, 0);
}

/* eq (1ffa) */
static int16_t
eq(uint16_t s1, uint16_t s2)
{
	while (rb(s1) == rb(s2++))
		if (rb(s1++) == 0)
			return 1;
	return 0;
}

/* putw (203a) */
static void
putw(uint16_t w, uint16_t b)
{
	WU(WU(b + BF_XNEXT)) = w;
	WU(b + BF_XNEXT) = (uint16_t)(WU(b + BF_XNEXT) + 2);
	if (--WI(b + BF_NLEFT) <= 0)
		flush(b);
}

/* flush (2068) */
static void
flush(uint16_t b)
{
	int16_t n;

	if ((n = (int16_t)(WU(b + BF_XNEXT) - (uint16_t)(b + BF_IOBUF))) > 0)
		if (sys_write(WI(b + BF_FILDES), (uint16_t)(b + BF_IOBUF),
		    (uint16_t)n) != n)
			error(2, S_OUTERR);
	WU(b + BF_XNEXT) = (uint16_t)(b + BF_IOBUF);
	WI(b + BF_NLEFT) = 256;
}
