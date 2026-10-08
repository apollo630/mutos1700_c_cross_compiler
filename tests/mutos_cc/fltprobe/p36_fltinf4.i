


















struct pt {
	double x, y;
};

main()
{
	struct pt s, *q;
	double a[3], d, e;
	float f;
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
	e = (q->x *= 2.0);
	r = r + e + s.x;
	d = 0.5;
	e = 1.0;
	e = (q->x *= d + e);
	r = r + e + s.x;
	d = 1.5;
	e = (a[i] += d);
	r = r + e + a[1];
	e = (a[i] *= 2.0);
	r = r + e + a[1];
	e = f--;
	r = r + e + f;
	d = --f;
	r = r + d + f;
	d = 2.0;
	e = ++d + 1.5;
	r = r + e + d;
	e = 10.0 - --d;
	r = r + e + d;
	e = ++d / 4.0;
	r = r + e * 4.0 + d;
	e = 1.0;
	e = ++d * ++e;
	r = r + e + d;
	e = ++f * 2.0;
	r = r + e + f;
	e = (d += 1.0) * 2.0;
	r = r + e + d;
	e = -(++d);
	r = r - e + d;
	e = d * (e = 2.0);
	r = r + e;
	e = (d = 2.0) * 3.0;
	r = r + e + d;
	e = (q->y -= d);
	r = r + e + s.y;
	e = (q->y /= d);
	r = r + e + s.y;
	e = d++ * 2.0;
	r = r + e + d;
	x = ++d > 2.0;
	r = r + x;
	if (++d > 2.0)
		r = r + 1;
	return r;
}
