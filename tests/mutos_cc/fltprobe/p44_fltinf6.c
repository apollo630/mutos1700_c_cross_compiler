/*
 * fltprobe/p44_fltinf6.c
 *
 * Floating shapes mutos_c1 compiles by inference only after the round-10
 * goldens, and some it still refuses. p40 showed v7's reorder() at work on
 * a '+': "e = (d += 2.0) + (e -= 1.0)" hoists "e -= 1.0" first (sreorder()
 * takes a '+''s right operand first), and "x = ++f > 2.0" compares the
 * hoisted f as the NAME it has become; "e = (q->x += d * e)" and "e =
 * (q->x *= d * e)" compute the right operand first and address q through
 * AX into BX after a '*'. Inferred from v7's reorder() and sreorder(): "e
 * = (d += 1.0) + (e -= 2.0) + (f += 0.5)" (f first, then d, then e?), "e =
 * w + ++d" and "e = w * ++d" (d sorted first by its degree before the
 * hoist, so "fldd d" / "faddd w"?), "e = (d += 1.0) * (e += 2.0) * 2.0"
 * (the product of the two first, then "fmuls 2.0"?), "e = y - ((d += 1.0)
 * + (e -= 1.0))" (both hoisted before y is loaded?), "d -= (e += 1.0) +
 * 2.0" (the hoist before d is loaded?), "return (a += 1.0) + (b -= 1.0);"
 * in a double function (a returned '+' is compiled as a node: a first?);
 * "x = ++f > g" (two float NAMEs: not exchanged?). Through a pointer in the
 * register context the right operand left: "e = (q->y += d * e)" ("lea
 * ax,*8.(bx)"?), "q->x *= d * e;", "e = (q->y *= d * e)", "q->y = d * e"
 * (through AX into BX?), "q->x = d + e" (in DI). "e = (a[i] += d * e)"
 * (the right operand first, then the element's address, as p36's "e =
 * (a[i] += d)"?); "e = (d += ++w + 1.0) * 2.0" (++w hoisted while d's
 * right operand is computed, inside the hoisted statement?). Refused: "e
 * = (d += ++w + ++y) * 2.0" (two hoists there).
 * Result: 120.
 */
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
