





















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
