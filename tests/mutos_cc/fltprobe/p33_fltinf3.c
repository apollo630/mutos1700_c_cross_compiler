/*
 * fltprobe/p33_fltinf3.c
 *
 * Floating shapes mutos_c1 compiles by inference only after the round-7
 * goldens, and some it still refuses. Inferred: "++f", "--f" and "f--"
 * on a float (p24's "f++": "flds" / "fadds" / "fstsp"), a '?:' as the
 * left operand of '+' and of '*' by a float, "q->y /= 2.0" (p24's "q->x
 * /= e": the divisor loaded, "fdiv"?), "q->x -= d * e" and "q->x /= d +
 * e" (p24's "q->x += d * e": the target pushed and loaded first), "a[i +
 * 1] += 1.0" and "a[i + 2] = a[j] * 2.0" (p24's "a[i + 1] = 2.0"), "d =
 * q->k * 2.0", "*pick(a, i) = d + e" (p24's "*pick(a, 1) = 2.5": "mov
 * di,ax" after a computed right-hand side too?). Refused: "e = f++" and
 * "d = ++f" (a float's '++' as a value), "e = (q->x += d)" (the value of
 * a compound assignment through a pointer), "e = ++d * 2.0" (a double's
 * prefix '++' as an operand - p24's "e = ++d" reloads the stored value;
 * and here?).
 * Result: 82.
 */
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
