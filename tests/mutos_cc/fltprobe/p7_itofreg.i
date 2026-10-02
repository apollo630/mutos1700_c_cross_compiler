










double tw(x)
double x;
{
	return x;
}

main()
{
	double d, e, f;
	int i, j;

	d = 6.0;
	e = 1.5;
	i = 3;
	j = 2;
	f = (d / e) - i;
	f = f + (tw(d) - i);
	f = f + (-d + i);
	f = f + ((d * e) + (i + 1));
	f = f + ((d * e) - (j - 1));
	f = f + (d + (i + j));
	f = f + ((d * e) + (i + j));
	f = f + (d + i * 3);
	return (int) f;
}
