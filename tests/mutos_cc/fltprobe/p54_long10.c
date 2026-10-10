/*
 * fltprobe/p54_long10.c
 *
 * No floating point: 'long' shapes mutos_c1 compiles by inference only
 * after the round-13 goldens, and some it still refuses. p50 showed "(int)
 * (l - (m + i))" adding m's low word to i ("mov si,i" / "add si,m" -
 * unoptim() optim()s the long tree before it distributes the LTOI, and
 * acommute() put the ITOL, degree 2, ahead of m), "(int) (-l + m)" in one
 * word ("neg di"), "lsub(l, m) * n" with n pushed first and the call's
 * DX:AX after it, "n * (l + m)" exchanged to "(l + m) * n", "(l + m) << 2"
 * shifted in DI:SI, and a long variable passed as an argument in two words
 * through DI. Inferred: "(int) (l + (m + i))", "(int) ((m + i) - l)",
 * "(int) (i - (m + i))", "(int) (l & i)", "(int) (l | i) + 1" (i first?),
 * "(int) (~l & m)", "(int) (-(l - m))", "(int) (-(l + m) + i)" (one
 * word?), "lsub(l, m) / n", "lsub(l, m) % 3", "lsub(l, m) * 3" (pushed
 * from DX:AX?), "n * (l / m)", "n * (l % m)", "n * lsub(l, m)", "n * (l -
 * m)" (n pushed first?), "(l - m) >> 2", "(l + m) << i", "(l - m) >> i"
 * (in DI:SI?), "lsub(n, l) - m", "lsub(l, 5L) - n". Refused: "l = 3 * (l +
 * m)", "l = lsub(l, 5L) + lsub(m, n)", "x = (int) (l & (m | i))" and "x =
 * (int) (l ^ (m + i))".
 * Result: 915.
 */
long lsub(a, b)
long a, b;
{
	return a - b;
}

main()
{
	long l, m, n;
	int i, x, r;

	r = 0;
	i = 3;
	l = 40;
	m = 7;
	n = 3;
	x = (int) (l + (m + i));
	r = r + x;
	x = (int) ((m + i) - l);
	r = r + x;
	x = (int) (i - (m + i));
	r = r + x;
	x = (int) (l & i);
	r = r + x;
	x = (int) (l | i) + 1;
	r = r + x;
	x = (int) (~l & m);
	r = r + x;
	x = (int) (-(l - m));
	r = r + x;
	x = (int) (-(l + m) + i);
	r = r + x;
	l = lsub(l, m) / n;
	r = r + (int) l;
	l = 40;
	l = lsub(l, m) % 3;
	r = r + (int) l;
	l = 40;
	l = lsub(l, m) * 3;
	r = r + (int) l;
	l = 40;
	l = n * (l / m);
	r = r + (int) l;
	l = 40;
	l = n * (l % m);
	r = r + (int) l;
	l = 40;
	l = n * lsub(l, m);
	r = r + (int) l;
	l = 40;
	l = n * (l - m);
	r = r + (int) l;
	l = 40;
	l = (l - m) >> 2;
	r = r + (int) l;
	l = 40;
	l = (l + m) << i;
	r = r + (int) l;
	l = 40;
	l = (l - m) >> i;
	r = r + (int) l;
	l = 40;
	l = lsub(n, l) - m;
	r = r + (int) l;
	l = 40;
	l = lsub(l, 5L) - n;
	r = r + (int) l;
	l = 40;
	l = 3 * (l + m);
	r = r + (int) l;
	l = 40;
	l = lsub(l, 5L) + lsub(m, n);
	r = r + (int) l;
	l = 40;
	x = (int) (l & (m | i));
	r = r + x;
	x = (int) (l ^ (m + i));
	r = r + x;
	return r;
}
