/*
 * fltprobe/p50_long9.c
 *
 * No floating point: 'long' shapes mutos_c1 compiles by inference only
 * after the round-12 goldens, and some it still refuses. p46 showed "(int)
 * (i + (l - m))" with the difference first ("mov di,l" / "sub di,m" / "add
 * di,i" - optim()'s acommute() of the int '+' unoptim() makes), "(int) (l -
 * (m - l))" with the left word loaded first and the difference into SI
 * ("mov di,l" / "mov si,m" / "sub si,l" / "sub di,si" - v7's "%n,e"),
 * "(int) (l & m)" and "(int) (l | m) + 1" in one word, "(l + m) * m" with m
 * pushed first ("mov di,m" / "push di" / "mov di,m+2" / "push di") and the
 * sum from DI:SI, "(l << 2) + 3" with the 3 pushed first. Inferred: "(int)
 * (i - (l - m))", "(int) (5 - (l - m))", "(int) (l - (m + i))" (the left
 * word into DI first, the right one into SI?), "(int) (l - (m - n) - i)",
 * "(int) ((l + m) - (l - n))", "(int) (l ^ m)", "(int) ((l & m) + i)",
 * "(int) (i + (l | m))" (one word?), "(l * m) * n", "(l - m) / n", "(l + m)
 * % n" (n pushed first?), "(l >> 2) + 3", "(l << i) + 7" (the constant
 * pushed first?), "(l << 2) + m" (m added from memory?). Refused: "x =
 * (int) (-l + m)", "l = lsub(l, m) * n", "l = n * (l + m)" and "l = (l +
 * m) << 2".
 * Result: 1870.
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
	x = (int) (i - (l - m));
	r = r + x;
	x = (int) (5 - (l - m));
	r = r + x;
	x = (int) (l - (m + i));
	r = r + x;
	x = (int) (l - (m - n) - i);
	r = r + x;
	x = (int) ((l + m) - (l - n));
	r = r + x;
	x = (int) (l ^ m);
	r = r + x;
	x = (int) ((l & m) + i);
	r = r + x;
	x = (int) (i + (l | m));
	r = r + x;
	l = (l * m) * n;
	r = r + (int) l;
	l = 40;
	l = (l - m) / n;
	r = r + (int) l;
	l = 40;
	l = (l + m) % n;
	r = r + (int) l;
	l = 40;
	l = (l >> 2) + 3;
	r = r + (int) l;
	l = 40;
	l = (l << i) + 7;
	r = r + (int) l;
	l = 40;
	l = (l << 2) + m;
	r = r + (int) l;
	l = 40;
	x = (int) (-l + m);
	r = r + x;
	l = lsub(l, m) * n;
	r = r + (int) l;
	l = 40;
	l = n * (l + m);
	r = r + (int) l;
	l = 40;
	l = (l + m) << 2;
	r = r + (int) l;
	return r;
}
