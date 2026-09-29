









main()
{
	double fl, flexp, exp5, t;
	float f;
	int c;

	fl = 0;
	c = '4';
	fl = 10*fl + (c-'0');
	c = '2';
	fl = 10*fl + (c-'0');
	flexp = 1;
	exp5 = 5;
	flexp *= exp5;
	exp5 *= exp5;
	flexp *= exp5;
	fl *= 2;
	fl /= 4;
	fl *= flexp;
	fl /= flexp;
	c = 2;
	t = (fl * 2) - c;
	t = (t + fl) / c;
	f = 3;
	f *= f;
	f /= 3;
	if ((exp5 = t) < fl)
		c = 0;
	c = t;
	return c + (int) f;
}
