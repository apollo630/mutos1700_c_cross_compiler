



















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

ihalf(x)
double x;
{
	return x / 2.0;
}

f1()
{
	double d, f;

	f = half(half(d = 3.0));
	return f;
}

f2()
{
	double d, e;

	e = half(d = 1.0) + half(e = 2.0);
	return e;
}

f3()
{
	double d;
	int x;

	x = ihalf(d = 3.0);
	return x;
}

f4()
{
	double d;

	ihalf(d = 3.0);
	return d;
}

f5()
{
	double d, e;

	two(d = 1.0, e = 2.0);
	return d + e;
}

f6()
{
	double d, e;

	e = two(d = 1.0, 2.0);
	return e;
}

f7()
{
	double d;

	return half(d = 3.0);
}

f8()
{
	double d;
	float g;

	g = half(d = 3.0);
	return g;
}

main()
{
	return f1() + f2() + f3() + f4() + f5() + f6() + f7() + f8();
}
