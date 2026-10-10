


















struct pt {
	double x, y;
};

double gd, ga[3];

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
	struct pt s;
	double e;

	gd = 1.0;
	ga[1] = 2.0;
	s.y = 4.0;
	e = half(gd) + 1.0;
	e = half(ga[1]) + 1.0;
	e = half(s.y) + 1.0;
	return e;
}

f3()
{
	static double sd;
	double e;

	sd = 2.0;
	e = half(sd) + 1.0;
	return e;
}

f4()
{
	double a[3], e, *p;
	float fa[3];
	int i;

	i = 1;
	a[0] = 1.0;
	a[1] = 2.0;
	fa[1] = 3.0;
	p = a;
	e = half(*p) + 1.0;
	e = half(p[1]) + 1.0;
	e = half(fa[i]) + 1.0;
	return e;
}

f5()
{
	struct pt s;
	double a[3], e;
	int i;

	i = 1;
	s.y = 1.0;
	a[1] = 2.0;
	e = two(s.y, a[i]) + 1.0;
	return e;
}

main()
{
	return f1() + f2() + f3() + f4() + f5();
}
