

















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
	double a[4], d, e;
	float f;
	int i, j, x, r;

	i = 1;
	j = 2;
	x = 1;
	r = 0;
	s.x = 1.5;
	s.y = 8.0;
	s.k = 6;
	q = &s;
	a[0] = 0.5;
	a[1] = 1.75;
	a[2] = 3.0;
	a[3] = 0.25;
	d = 4.5;
	e = 0.5;
	f = 1.5;
	++f;
	--f;
	f--;
	r = r + f * 4.0;
	e = (x ? d : e) + 1.5;
	r = r + e;
	e = ((i > j) ? d : e) * f;
	r = r + e;
	q->y /= 2.0;
	r = r + s.y;
	d = 2.0;
	e = 0.5;
	q->x -= d * e;
	q->x /= d + e;
	r = r + s.x * 10.0;
	a[i + 1] += 1.0;
	a[i + 2] = a[j] * 2.0;
	r = r + a[2] + a[3];
	d = q->k * 2.0;
	r = r + d;
	*pick(a, i) = d + e;
	r = r + a[1] * 2.0;
	e = f++;
	r = r + e + f;
	d = ++f;
	r = r + d;
	e = (q->x += d);
	r = r + e;
	e = ++d * 2.0;
	r = r + e + d;
	return r;
}
