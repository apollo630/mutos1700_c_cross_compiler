

















double half(x)
double x;
{
	return x / 2.0;
}

f1()
{
	double d;

	half(d = 3.0);
	return 1;
}

f2()
{
	double d, e, w, y;
	int x;

	d = 1.5;
	w = 4.0;
	y = 10.0;
	x = (d += ++w + ++y) > 2.0;
	e = d + 1.0;
	d = e * 2.0;
	return x;
}

f3()
{
	double d, e;

	d = 1.0;
	e = d + 2.0;
	return e;
}

main()
{
	return f1() + f2() + f3();
}
