/*
 * fltprobe/p46_long8.c
 *
 * No floating point: 'long' shapes mutos_c1 compiles by inference only
 * after the round-11 goldens, and some it still refuses. p42 showed "l =
 * -300" loaded as two words ("mov si,#-300." / "mov di,*-1." - v7's
 * optim() makes an ITOL of a negative CON an LCON, where "l = 300" goes
 * through "cwd"), "(int) ((l - m) + 3)" computed in one word ("mov di,l" /
 * "sub di,m" / "add di,*3." - unoptim() distributes the LTOI), "(l + m) *
 * 2" with the 2 pushed first and the sum pushed from DI:SI, and "(int) (l
 * * m * 2)" pushed like "l * m * 2". Inferred: "l = -5L" and "l = -1"
 * (two words?), a negative long constant passed ("lsub(-300L, 7L)": "mov
 * di,#-300." / "push di" / "mov di,*-1." / "push di"?) and returned
 * ("return -5L;"), "(int) (l - m)", "(int) (l + m) + 3", "r + (int) (l -
 * m)", "(int) ((l + m) - i)", "(int) (i + (l - m))", "(int) (l + m + l)",
 * "(int) (l - (m - l))" (all in one word?), "(l - m) * 3", "(l + m) / 3"
 * and "(l - m) % 7" (the constant pushed first, the sum from DI:SI?),
 * "(int) (l / m * 2)", "(int) ((l * m) % 9)", "(l + i) * 3", "(int) ((l +
 * m) * 3)". Refused: "x = (int) (l & m)", "x = (int) (l | m) + 1", "l =
 * (l + m) * m" and "l = (l << 2) + 3".
 * Result: 1682.
 */
long lsub(a, b)
long a, b;
{
	return a - b;
}

long lneg()
{
	return -5L;
}

main()
{
	long l, m;
	int i, x, r;

	r = 0;
	i = 3;
	l = -5L;
	r = r + (int) (l + 10);
	l = -1;
	r = r + (int) (l + 2);
	l = lsub(-300L, 7L);
	r = r + (int) (l + 400);
	l = lneg();
	r = r + (int) (l + 10);
	l = 40;
	m = 7;
	x = (int) (l - m);
	r = r + x;
	x = (int) (l + m) + 3;
	r = r + x;
	x = r + (int) (l - m);
	r = x;
	x = (int) ((l + m) - i);
	r = r + x;
	x = (int) (i + (l - m));
	r = r + x;
	x = (int) (l + m + l);
	r = r + x;
	x = (int) (l - (m - l));
	r = r + x;
	l = (l - m) * 3;
	r = r + (int) l;
	l = (l + m) / 3;
	r = r + (int) l;
	l = (l - m) % 7;
	r = r + (int) l;
	l = 40;
	x = (int) (l / m * 2);
	r = r + x;
	x = (int) ((l * m) % 9);
	r = r + x;
	l = (l + i) * 3;
	r = r + (int) l;
	x = (int) ((l + m) * 3);
	r = r + x;
	l = 40;
	x = (int) (l & m);
	r = r + x;
	x = (int) (l | m) + 1;
	r = r + x;
	l = (l + m) * m;
	r = r + (int) l;
	l = 40;
	l = (l << 2) + 3;
	r = r + (int) l;
	return r;
}
