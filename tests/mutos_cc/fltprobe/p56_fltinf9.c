/*
 * fltprobe/p56_fltinf9.c
 *
 * Floating shapes mutos_c1 compiles by inference only after the round-13
 * goldens. p52 showed a hoisted "+=" or "-=" stored WITHOUT a pop ("fstd
 * d" - its value left on the stack, used by nothing) whenever it is the
 * operand of a comparison whose VALUE is taken and its right operand is
 * not a leaf - "x = (d += ++w + 1.0) > 2.0", "x = (d += w + y) > 2.0", "x
 * = (d += a[i] * e) > 2.0" -, but "if ((d += ++w + ++y) > 2.0)" with a
 * pop, as "x = (d += 1.0) > 2.0" (p40); and "e = -(d += ++w + ++y)"
 * hoisting the "+=" out of the negation, its own right operand left to
 * right. Inferred: "x = (d += e) > 2.0", "x = (d -= e) < 0.0", "x = (d +=
 * *p) > 2.0", "x = (d += w) > (v += y)" (a leaf: "fstdp"?), "x = !((d +=
 * ++w + 1.0) > 2.0)", "x = ((d += w + y) > 2.0) ? 3 : 4", "x = (d += w +
 * y) > 2.0 && x", "while ((d += w + y) < 30.0)" (a branch: "fstdp"?), "x
 * = -(d += w + y) < 0.0" (hoisted from the negation: "fstdp"?), "e =
 * -(++d)", "e = -(d -= w * y)", "e = -(d += 1.0) * 2.0", "e = -(--d) + w"
 * (hoisted?), "x = (d += i) > 2.0", "x = x + ((d += w * y) > 2.0)" and
 * "e = (d -= e * 2.0) < 0.0" ("fstd"? - three values left on the stack,
 * so the real c1's model stays below its top).
 * Result: 179.
 */
main()
{
	double d, e, v, w, y, *p;
	int i, x, r;

	r = 0;
	i = 2;
	x = 1;
	p = &w;
	d = 1.5;
	e = 0.5;
	v = 3.0;
	w = 4.0;
	y = 10.0;
	x = (d += e) > 2.0;
	r = r + x + d;
	d = 1.5;
	x = (d -= e) < 0.0;
	r = r + x + d;
	d = 1.5;
	x = (d += *p) > 2.0;
	r = r + x + d;
	d = 1.5;
	x = (d += w) > (v += y);
	r = r + x + d + v;
	d = 1.5;
	x = !((d += ++w + 1.0) > 2.0);
	r = r + x + d;
	d = 1.5;
	w = 4.0;
	x = ((d += w + y) > 2.0) ? 3 : 4;
	r = r + x + d;
	d = 1.5;
	x = (d += w + y) > 2.0 && x;
	r = r + x + d;
	d = 1.5;
	while ((d += w + y) < 30.0)
		x++;
	r = r + x + d;
	d = 1.5;
	x = -(d += w + y) < 0.0;
	r = r + x + d;
	d = 1.5;
	e = -(++d);
	r = r + e * 2.0 + d;
	d = 1.5;
	e = -(d -= w * y);
	r = r + e + d;
	d = 1.5;
	e = -(d += 1.0) * 2.0;
	r = r + e + d;
	d = 1.5;
	e = -(--d) + w;
	r = r + e * 2.0 + d;
	d = 1.5;
	x = (d += i) > 2.0;
	r = r + x + d;
	d = 1.5;
	x = x + ((d += w * y) > 2.0);
	r = r + x + d;
	d = 1.5;
	e = 0.5;
	e = (d -= e * 2.0) < 0.0;
	r = r + e + d * 2.0;
	return r;
}
