



















struct pt {
	double x, y;
};

main()
{
	struct pt s, *q;
	double a[3], d, e, w, y;
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
	w = 4.0;
	y = 10.0;
	d += ++w + ++y;
	r = r + d + w + y;
	d = 1.5;
	e = (d -= ++w + ++y) * 2.0;
	r = r + e + d + w + y;
	d = 1.5;
	e = (d += ++w + ++y + 1.0) * 2.0;
	r = r + e + d + w + y;
	d = 1.5;
	e = (d += (w += 1.0) + ++y) * 3.0;
	r = r + e + d + w + y;
	d = 1.5;
	e = (d += --w + --y) + 1.0;
	r = r + e + d + w + y;
	d = 1.5;
	x = (d += ++w + ++y) > 2.0;
	r = r + x + d;
	e = (q->x += ++w + 1.0);
	r = r + e + s.x + w;
	e = (a[i] += ++w + 1.0);
	r = r + e + a[1] + w;
	d = 1.5;
	e = (d *= ++w + 1.0) * 2.0;
	r = r + e + d + w;
	d = 1.5;
	e = (++w + ++y) * (++d + 1.0);
	r = r + e + d + w + y;
	w = 4.0;
	y = 10.0;
	d = 1.5;
	e = (d += ++w * ++y) * 2.0;
	r = r + e + d + w + y;
	d = 1.5;
	e = (d += ++w - ++y) * 2.0;
	r = r + e + d + w + y;
	return r;
}
