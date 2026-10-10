





















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
