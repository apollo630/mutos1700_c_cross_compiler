/*
 * fltprobe/p52_fltinf8.c
 *
 * Floating shapes mutos_c1 compiles by inference only after the round-12
 * goldens. p48 showed a hoisted "+=" that is an operand of a '+' (reached
 * by reorder() - "e = (d += --w + --y) + 1.0") hoisting its right
 * operand's "--y" first, as a statement's "d += ++w + ++y;" does, while
 * one under a '*' or a comparison hoists left to right (its template's
 * reorder() of the '+' as a node) - under a '*' or a '-' inside it too;
 * and "x = (d += ++w + ++y) > 2.0" storing d WITHOUT a pop ("fstd d" -
 * the value left on the stack, used by nothing; "cc -S" printed no
 * message). Inferred: "e = (d += ++w * ++y) + 1.0", "e = (d += ++w - ++y)
 * + 1.0" (left to right: sreorder() of a '*' or '-' does nothing?), "e =
 * (d += ++w + ++y) - 1.0" (left to right?), "e = 1.0 + (d += ++w + ++y)",
 * "e = (d += --w + --y + 1.0) + 2.0", "e = (d += ++w + ++y) + (v -=
 * 1.0)" (right to left?), "x = (d += ++w + 1.0) > 2.0" (one hoist inside:
 * "fstdp"?), "x = (d -= ++w + ++y) < 0.0", "x = 2.0 < (d += ++w + ++y)",
 * "if ((d += ++w + ++y) > 2.0)" and "x = (d += ++w + ++y) == (v += 1.0)"
 * ("fstd"?), "x = (d += w + y) > 2.0" and "x = (d += a[i] * e) > 2.0" (no
 * hoist inside: "fstdp"?), "e = (d += ++w + ++y) / 2.0", "e = (d *= ++w +
 * ++y) + 1.0", "e = -(d += ++w + ++y)".
 * Result: 711.
 */
main()
{
	double a[3], d, e, v, w, y;
	int i, x, r;

	r = 0;
	i = 1;
	a[0] = 0.5;
	a[1] = 1.5;
	a[2] = 2.5;
	e = 0.5;
	v = 3.0;
	w = 4.0;
	y = 10.0;
	d = 1.5;
	e = (d += ++w * ++y) + 1.0;
	r = r + e + d + w + y;
	d = 1.5;
	e = (d += ++w - ++y) + 1.0;
	r = r + e + d + w + y;
	d = 1.5;
	e = (d += ++w + ++y) - 1.0;
	r = r + e + d + w + y;
	d = 1.5;
	e = 1.0 + (d += ++w + ++y);
	r = r + e + d + w + y;
	d = 1.5;
	e = (d += --w + --y + 1.0) + 2.0;
	r = r + e + d + w + y;
	d = 1.5;
	e = (d += ++w + ++y) + (v -= 1.0);
	r = r + e + d + v;
	d = 1.5;
	x = (d += ++w + 1.0) > 2.0;
	r = r + x + d;
	d = 1.5;
	x = (d -= ++w + ++y) < 0.0;
	r = r + x + d;
	d = 1.5;
	x = 2.0 < (d += ++w + ++y);
	r = r + x + d;
	d = 1.5;
	x = 0;
	if ((d += ++w + ++y) > 2.0)
		x = 1;
	r = r + x + d;
	d = 1.5;
	x = (d += ++w + ++y) == (v += 1.0);
	r = r + x + d + v;
	d = 1.5;
	x = (d += w + y) > 2.0;
	r = r + x + d;
	d = 1.5;
	e = 0.5;
	x = (d += a[i] * e) > 2.0;
	r = r + x + d * 4.0;
	d = 1.5;
	e = (d += ++w + ++y) / 2.0;
	r = r + e + d;
	d = 1.5;
	e = (d *= ++w + ++y) + 1.0;
	r = r + e + d;
	d = 1.5;
	e = -(d += ++w + ++y);
	r = r + e + d + w + y;
	return r;
}
