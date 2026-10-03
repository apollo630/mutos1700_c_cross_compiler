













double half(x)
double x;
{
	return x / 2.0;
}

main()
{
	double d, e, f;
	float g;
	long l;
	unsigned u;
	int i, j, x, r;
	char c;

	i = 3;
	j = 4;
	c = 2;
	l = 70000;
	d = e = 2.5;
	r = d + e;
	d++;
	++d;
	e = d--;
	r = r + e + d;
	d = d + c;
	e = c * 2.5;
	r = r + d + e;
	d = (double) (i + j);
	e = (double) (i * j) + 0.5;
	r = r + d + e;
	d = -i;
	e = l + 1;
	r = r + d + (e - 70000.0);
	d = 40000.5;
	u = (unsigned) d;
	r = r + u - 39990;
	x = 1;
	d = x ? d : e;
	g = (i = 2, d - 40000.0);
	r = r + i + g * 4.0;
	f = half(d = 3.0);
	r = r + f * 2.0 + d;
	return r;
}
