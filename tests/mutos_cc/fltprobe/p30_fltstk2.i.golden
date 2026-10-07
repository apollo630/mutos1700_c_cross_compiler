

















double half(x)
double x;
{
	return x / 2.0;
}

double two(x, y)
double x, y;
{
	return x + y;
}

f1()
{
	double d;
	int x;

	d = 1.0;
	x = half(d = 3.0);
	return x;
}

f2()
{
	double d;
	int x;

	d = 1.0;
	x = d;
	x = half(d = 3.0);
	return x;
}

f3()
{
	double d, e;

	e = half(d = 3.0) + 1.0;
	return e;
}

f4()
{
	double d;
	int x;

	x = 0;
	if (half(d = 3.0) > 1.0)
		x = 1;
	return x;
}

f5()
{
	double d;
	int x;

	x = half(d = 3.0) > 1.0;
	return x;
}

double f6()
{
	double d;

	return half(d = 3.0);
}

f7()
{
	double d, e;
	int x;

	x = two(d = 1.0, e = 2.0);
	return x;
}

main()
{
	return f1() + f2() + f3() + f4() + f5() + f7();
}
