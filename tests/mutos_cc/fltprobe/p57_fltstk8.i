

























struct pt {
	double x, y;
};

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
	struct pt s, *q;
	double d, e, w, y;
	int x;

	q = &s;
	d = 1.0;
	d += 1.0;
	d++;
	--d;
	e = 2.0;
	e *= 2.0;
	s.x = 1.0;
	q->x += 1.0;
	e = d++;
	w = 4.0;
	y = 10.0;
	x = 0;
	if ((d += w + y) > 2.0)
		x = 1;
	return x;
}

f3()
{
	double d, e, w, y;
	int x;

	d = 1.5;
	w = 4.0;
	y = 10.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	e = d++;
	e = half(d) + 1.0;
	return x;
}

main()
{
	return f1() + f2() + f3();
}
