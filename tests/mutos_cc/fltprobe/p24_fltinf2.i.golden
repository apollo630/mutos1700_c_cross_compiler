















struct pt {
	double x, y;
	int k;
};

double *pick(a, i)
double *a;
{
	return a + i;
}

main()
{
	struct pt s, *q;
	double a[3], d, e;
	float f;
	int i, j, x, r;

	i = 1;
	j = 2;
	x = 5;
	r = 0;
	s.x = 1.5;
	s.y = 2.25;
	s.k = 6;
	q = &s;
	a[0] = 0.5;
	a[1] = 1.75;
	a[2] = 3.0;
	d = 4.5;
	e = 0.25;
	f = 1.5;
	e = ++d;
	r = r + e + d;
	e = --d;
	r = r + e + d;
	e = x ? 1.5 : 2.5;
	r = r + e * 2.0;
	d = (i > j) ? e : d;
	r = r + d * 2.0;
	d = -i * e;
	r = r + d * 2.0;
	q->y -= e;
	q->x /= e;
	r = r + (s.x + s.y) * 4.0;
	x = q->y * 4.0;
	r = r + x;
	if (q->x > e)
		r = r + 100;
	if (a[i] < q->y)
		r = r + 100;
	d = a[i] + q->x * s.y;
	r = r + d * 2.0;
	d = d + i * j;
	e = d * (i + 1);
	r = r + d * 2.0 + e;
	r = r + (x ? d : e) * 2.0;
	q->x += d * e;
	r = r + s.x * 2.0;
	a[i + 1] = 2.0;
	d = a[i + 1] + a[j];
	r = r + d;
	d = q->k;
	r = r + d;
	*pick(a, 1) = 2.5;
	r = r + a[1] * 2.0;
	f++;
	r = r + f * 2.0;
	return r;
}
