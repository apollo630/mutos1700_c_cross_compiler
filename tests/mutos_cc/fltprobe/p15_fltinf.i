












double tw(x)
double x;
{
	return x * 2;
}

main()
{
	double d, e, arr[3];
	int i, j, r;

	i = 1;
	j = 6;
	r = 0;
	d = 2.0;
	e = 0.5;
	arr[1] = 3.0;
	d = (d * d) + arr[i] + i;
	r = r + (int) d;
	d = (d * e) + (i << 3);
	r = r + (int) d;
	d = (d / e) - (j >> 1);
	r = r + (int) d;
	d = tw(e) + arr[i];
	r = r + (int) d;
	return r;
}
