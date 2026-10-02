
















main()
{
	long l, m;
	int a[3], b[3];
	int i, j, x, s, r;
	unsigned u;
	double d, e;
	char c;

	l = 100000;
	m = 65537;
	i = 3;
	u = 40000;
	r = 0;
	l = l + i;
	r = r + (int) (l - 100000);
	l = i + l;
	r = r + (int) (l - 100000);
	if (l > i)
		r = r + 1;
	if (l == i)
		r = r + 100;
	l += i;
	r = r + (int) (l - 100000);
	l = u;
	r = r + (int) (l - 39990);
	l = m & 255;
	r = r + (int) l;
	l = m | 6;
	r = r + (int) (l - 65530);
	r = r + (u > 39999);
	i = 1;
	j = 2;
	x = 20;
	a[1] = 6;
	b[0] = 5;
	b[2] = 3;
	s = a[i] * b[j - 2];
	r = r + s;
	s = x - b[j];
	r = r + s;
	c = 2;
	u = 3;
	d = 9.5;
	e = 2.0;
	d -= c;
	r = r + (int) d;
	d -= e * 2;
	r = r + (int) d;
	d = (d * e) + u;
	r = r + (int) d;
	return r;
}
