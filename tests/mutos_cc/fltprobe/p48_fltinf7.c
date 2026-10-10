/*
 * fltprobe/p48_fltinf7.c
 *
 * Floating shapes mutos_c1 compiles by inference only after the round-11
 * goldens, and some it still refuses. p44 showed every hoist it asked about
 * as inferred, and "e = (d += ++w + ++y) * 2.0" hoisting ++w, then ++y,
 * inside the hoisted "d += ...": the hoisted statement's rcexpr() runs no
 * reorder() of its own, its template's rcexpr() of the right operand does,
 * and reorder() of a '+' as a node takes its left operand first.
 * Inferred: "d += ++w + ++y;" as a statement (its own reorder() sreorder()s
 * the '+' as an operand - the RIGHT one, ++y, first? Its 1.0 keeps the
 * label of its place in the tree?), "e = (d -= ++w + ++y) * 2.0", "e = (d +=
 * ++w + ++y + 1.0) * 2.0" (three terms), "e = (d += (w += 1.0) + ++y) *
 * 3.0", "e = (d += --w + --y) + 1.0", "x = (d += ++w + ++y) > 2.0", "e =
 * (q->x += ++w + 1.0)" and "e = (a[i] += ++w + 1.0)" (not hoisted - ++w
 * hoisted ahead of the whole statement?), "e = (d *= ++w + 1.0) * 2.0",
 * "e = (++w + ++y) * (++d + 1.0)". Refused: "e = (d += ++w * ++y) * 2.0"
 * and "e = (d += ++w - ++y) * 2.0" (two hoists under a '*' or a '-').
 * Result: 737.
 */
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
