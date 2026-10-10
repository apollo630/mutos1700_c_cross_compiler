
















struct pt {
	double x, y;
};

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
	double e;
	float f;

	f = 1.0;
	e = half(f) + 1.0;
	return e;
}

f3()
{
	struct pt s, *q;
	double a[3], e;
	int i;

	i = 1;
	a[1] = 2.0;
	s.y = 4.0;
	q = &s;
	e = half(a[i]) + 1.0;
	e = half(q->y) + 1.0;
	return e;
}

f4()
{
	double d, e;
	int i;

	d = 1.0;
	i = 2;
	e = half(-d) + 1.0;
	e = half((double) i) + 1.0;
	e = half(d * 2.0) + 1.0;
	return e;
}

f5()
{
	double d, e;

	d = 1.0;
	e = half(half(d) * 2.0) + 1.0;
	return e;
}

f6()
{
	double d, e;

	d = 1.0;
	e = 2.0;
	e = two(d, d + e) + 1.0;
	return e;
}

main()
{
	return f1() + f2() + f3() + f4() + f5() + f6();
}
