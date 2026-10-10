


























struct pt {
	double x, y;
};

double dd(a, b)
double a, b;
{
	return (a += 1.0) + (b -= 1.0);
}

main()
{
	struct pt s, *q;
	double a[3], d, e, w, y;
	float f, g;
	int i, x, r;

	r = 0;
	i = 1;
	s.x = 1.5;
	s.y = 2.0;
	q = &s;
	a[0] = 0.5;
	a[1] = 1.5;
	a[2] = 2.5;
	d = 1.5;
	e = 0.5;
	f = 2.5;
	g = 3.0;
	w = 4.0;
	y = 10.0;
	e = (d += 1.0) + (e -= 2.0) + (f += 0.5);
	r = r + e + d + f;
	e = w + ++d;
	r = r + e + d;
	e = w * ++d;
	r = r + e / 4.0 + d;
	d = 1.5;
	e = 0.5;
	e = (d += 1.0) * (e += 2.0) * 2.0;
	r = r + e + d;
	d = 1.5;
	e = 0.5;
	e = y - ((d += 1.0) + (e -= 1.0));
	r = r + e + d;
	d -= (e += 1.0) + 2.0;
	r = r + e + d;
	e = dd(d, e);
	r = r + e;
	x = ++f > g;
	r = r + x;
	x = ++f > g;
	r = r + x;
	d = 1.0;
	e = 2.0;
	e = (q->y += d * e);
	r = r + e + s.y;
	q->x *= d * e;
	r = r + s.x;
	e = 0.5;
	e = (q->y *= d * e);
	r = r + e + s.y;
	q->y = d * e;
	r = r + s.y * 4.0;
	q->x = d + e;
	r = r + s.x * 2.0;
	e = (a[i] += d * e);
	r = r + e + a[1];
	e = (d += ++w + 1.0) * 2.0;
	r = r + e / 4.0 + d / 4.0 + w;
	e = (d += ++w + ++y) * 2.0;
	r = r + e / 10.0 + d / 10.0 + w + y / 4.0;
	return r;
}
