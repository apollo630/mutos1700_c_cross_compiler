

















main()
{
	double d, e;
	float a;
	int i, r;

	d = 1.5;
	e = 2.5;
	a = 0.75;
	i = 2;
	r = 0;
	if (a < 0)
		r = r + 100;
	if (a == 0)
		r = r + 200;
	if ((d + e) < 0)
		r = r + 300;
	if ((d - e) != 0)
		r = r + 1;
	r = r + (d < e);
	if (d < e && e < d)
		r = r + 500;
	if ((d + e) < 1.5)
		r = r + 600;
	if ((d + e) > (e - d))
		r = r + 4;
	if (1.5 < i)
		r = r + 8;
	if (i < 1.5)
		r = r + 700;
	if (e > i + 0.25)
		r = r + 16;
	return r;
}
