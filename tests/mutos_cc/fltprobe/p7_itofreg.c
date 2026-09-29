/*
 * fltprobe/p7_itofreg.c
 *
 * The register an int converted to floating is loaded into after a
 * computed operand: DI after a '+' ("(t + fl) / c", 05_fltasop), AX after
 * a '*' ("(fl * 2) - c") - and after a '/', a call, a negation, another
 * conversion? Also "x + 1" / "x - 1" converted in AX ("inc ax"?), the
 * sum of two variables and a product by a constant converted. mutos_c1
 * refuses these.
 * Result: 62.
 */
double tw(x)
double x;
{
	return x;
}

main()
{
	double d, e, f;
	int i, j;

	d = 6.0;
	e = 1.5;
	i = 3;
	j = 2;
	f = (d / e) - i;
	f = f + (tw(d) - i);
	f = f + (-d + i);
	f = f + ((d * e) + (i + 1));
	f = f + ((d * e) - (j - 1));
	f = f + (d + (i + j));
	f = f + ((d * e) + (i + j));
	f = f + (d + i * 3);
	return (int) f;
}
