/*
 * fltprobe/p5_call.c
 *
 * Calls and conversions mutos_c0/mutos_c1 still refuse: an unused
 * double result, a function returning a float, a computed floating
 * argument that is not the last one, two double results in one
 * expression, long <-> floating ("ltof"), char <-> floating, and a
 * register variable converted to floating.
 * Result: 12.
 */
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
