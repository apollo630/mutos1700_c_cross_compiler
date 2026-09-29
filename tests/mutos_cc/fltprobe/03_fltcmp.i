










main()
{
	double d, e;
	float a, b;
	int i, r;

	d = 1.5;
	e = 2.5;
	a = 0.75;
	b = 3;
	i = 2;
	r = 0;
	if (d < e)
		r = r + 1;
	if (d >= e)
		r = r + 100;
	if (e > 0)
		r = r + 2;
	if (d != 0)
		r = r + 4;
	if (0 < d)
		r = r + 8;
	if (a < 1.5)
		r = r + 16;
	if (d < a)
		r = r + 1000;
	if (e < i)
		r = r + 32;
	if (i <= d)
		r = r + 2000;
	if (!(a > b))
		r = r + 64;
	while (d < b)
		d = d + e;
	return r;
}
