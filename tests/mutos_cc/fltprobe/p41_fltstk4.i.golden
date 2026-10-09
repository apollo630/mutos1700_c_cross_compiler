


















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

	half(d = 3.0);
	return 1;
}

f2()
{
	double d, e;

	d = 1.0;
	e = half(d) + 1.0;
	return e;
}

f3()
{
	double d, e;

	d = 1.0;
	e = 2.0;
	e = half(d + e) + 1.0;
	return e;
}

f4()
{
	double e;
	int i;

	i = 4;
	e = half(i) + 1.0;
	return e;
}

f5()
{
	double d;
	int x;

	x = half(half(d = 3.0));
	return x;
}

f6()
{
	double d, e;

	two(d = 1.0, half(e = 2.0));
	return d + e;
}

f7()
{
	double d, e;

	e = two(half(d = 1.0), half(e = 2.0));
	return e;
}

f8()
{
	double d, e;
	int x;

	e = half(d = 3.0) * 2.0;
	e = -half(d = 3.0);
	x = half(d = 3.0) > 1.0 ? 1 : 2;
	return x;
}

f9()
{
	half(2.0);
	return 2;
}

main()
{
	return f1() + f2() + f3() + f4() + f5() + f6() + f7() + f8() + f9();
}
