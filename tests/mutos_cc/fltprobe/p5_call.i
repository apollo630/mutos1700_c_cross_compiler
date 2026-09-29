









double tw(x)
double x;
{
	return x;
}

float ff(x)
float x;
{
	return x;
}

fi(d, i)
double d;
{
	return i;
}

main()
{
	double d, e;
	long l;
	char c;
	register int r;

	d = 2.5;
	e = 1.5;
	tw(d);
	e = tw(d + e);
	r = fi(d + e, 3);
	e = ff(d);
	l = 7;
	d = l;
	l = d;
	c = d;
	d = c;
	d = d + r;
	e = tw(d) + tw(e);
	return (int) e;
}
