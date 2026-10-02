










main()
{
	double d, *p;
	double arr[3];
	long l;
	int i;

	l = 70000;
	d = l;
	p = &arr[1];
	*p = 2.5;
	d = *p + 1.5;
	i = 2;
	i += d;
	i -= d;
	arr[i] = d;
	d = arr[i] + 0.5;
	return i + (int) d;
}
