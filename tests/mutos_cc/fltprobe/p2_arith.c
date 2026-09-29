/*
 * fltprobe/p2_arith.c
 *
 * Floating arithmetic mutos_c1 still refuses: an int right operand of
 * a variable ("d + i" - libc.a's "reversed" fsubrs/fdivrs...: are they
 * used here?), a computed right operand, a constant right operand of a
 * computed value, a double negation, '+=' / '-=', '/=' and '*=' by a
 * computed value or an int variable, a compound assignment's value
 * used, and an int target with a floating right-hand side.
 * Result: 17.
 */
main()
{
	double d, e;
	float a, b;
	int i, j;

	d = 4;
	e = 2;
	a = 1.5;
	b = 0.5;
	i = 3;
	j = 2;
	d = d + i;
	d = d - i;
	d = d * i;
	d = d / i;
	d = a - i;
	d = i - d;
	d = (d + e) * (i * j);
	d = b * (a + e);
	d = (a + b) * (d + e);
	d = (d + e) * 2.0;
	d = (d + e) - 0.5;
	d = -(-e);
	d += e;
	d -= e;
	d += 1;
	a -= 0.5;
	d /= (e + e);
	d *= i;
	e = (d *= e);
	i *= e;
	j = e;
	return i + j;
}
