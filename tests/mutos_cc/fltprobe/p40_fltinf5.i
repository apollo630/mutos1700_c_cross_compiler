























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
	e = (d -= 0.5) * 2.0;
	r = r + e + d;
	e = 3.0 - (d += 1.0);
	r = r + e * 2.0 + d;
	e = 0.5;
	e = (d += e * 2.0) * 3.0;
	r = r + e + d;
	e = (d += i) * 2.0;
	r = r + e + d;
	e = (s.x += 1.0) * 2.0;
	r = r + e + s.x;
	e = (f -= 0.5) * 2.0;
	r = r + e + f;
	d = 1.5;
	x = (d += 1.0) > 2.0;
	r = r + x;
	if ((d -= 1.0) < 1.0)
		r = r + 1;
	x = 1.0 < (d += 1.0);
	r = r + x;
	if (--d < 1.0)
		r = r + 1;
	x = ++f > 2.0;
	r = r + x;
	e = 1.0;
	e = (d += 2.0) + (e -= 1.0);
	r = r + e + d;
	d = 2.0;
	e = (d *= 2.0) * 3.0;
	r = r + e + d;
	e = (d /= 2.0) + 1.0;
	r = r + e + d;
	e = (q->y -= 0.5);
	r = r + e + s.y;
	e = (q->y /= 2.0);
	r = r + e * 4.0 + s.y;
	d = 1.0;
	e = 2.0;
	e = (q->x += d * e);
	r = r + e + s.x;
	e = 0.5;
	e = (q->y -= d * e);
	r = r + e * 4.0 + s.y;
	e = 1.0;
	e = (q->x /= d + e);
	r = r + e + s.x;
	e = (q->x *= d * e);
	r = r + e + s.x;
	e = (a[i] += 1.5);
	r = r + e + a[1];
	e = (a[i] -= 0.5);
	r = r + e + a[1];
	e = (a[i] /= 2.0);
	r = r + e * 2.0 + a[1];
	d = 2.0;
	e = d-- * 2.0;
	r = r + e + d;
	e = f++ * 2.0;
	r = r + e + f;
	e = d++ + e--;
	r = r + e + d;
	return r;
}
