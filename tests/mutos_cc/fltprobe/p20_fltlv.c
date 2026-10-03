/*
 * fltprobe/p20_fltlv.c
 *
 * Floating lvalues and operands beyond variables, elements of a local
 * array and a pointer variable read through, all refused by mutos_c0 or
 * mutos_c1 after the round-5 goldens: a double member of a struct (as a
 * target, an operand, through a pointer, with '+='), '+=' and '*=' into
 * an element subscripted by a variable, a pointer to double subscripted
 * ("p[2]"), with an offset ("*(p + i)") and incremented ("p++"), the
 * address of a double local passed to a function that stores through it,
 * and a function returning a pointer to double.
 * Result: 128.
 */
struct pt {
	double x, y;
};

double *pick(a, i)
double *a;
{
	return a + i;
}

twice(p)
double *p;
{
	*p = *p * 2.0;
}

main()
{
	struct pt s, *q;
	double a[3], *p, d;
	int i, r;

	i = 1;
	s.y = 2.5;
	s.x = s.y * 2.0;
	q = &s;
	q->y = q->x + 1.5;
	q->x += 0.5;
	a[0] = 1.0;
	a[1] = 2.5;
	a[2] = 4.0;
	a[i] += 1.5;
	a[i] *= s.y;
	p = a;
	d = p[2] + *(p + i);
	p++;
	d = d + *p;
	twice(&d);
	d = d + *pick(a, 2);
	r = d + q->y + q->x;
	return r;
}
