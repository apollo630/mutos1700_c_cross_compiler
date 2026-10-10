





















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
