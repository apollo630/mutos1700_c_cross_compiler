
















double half(x)
double x;
{
	return x / 2.0;
}

f1()
{
	double d;
	int x;

	x = half(d = 3.0);
	return x;
}

f2()
{
	double d;

	d = 2.5;
	return d;
}

f3()
{
	double d, e;
	int x;

	d = 1.0;
	e = 2.0;
	half(d = 3.0);
	half(e = 4.0);
	x = (d + e) * (d - e);
	return x;
}

f4()
{
	double d, e;
	int x;

	d = 1.0;
	e = 2.0;
	half(d = 3.0);
	half(e = 4.0);
	x = 0;
	if (d + e > d - e)
		x = 1;
	return x;
}

double f5()
{
	double d;

	half(d = 3.0);
	return d;
}

main()
{
	return f1() + f2() + f3() + f4() + f5();
}
