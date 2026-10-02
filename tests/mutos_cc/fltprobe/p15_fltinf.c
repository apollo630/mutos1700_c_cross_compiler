/*
 * fltprobe/p15_fltinf.c
 *
 * Floating shapes mutos_c1 compiles by inference since the round-3
 * goldens: a chain ordered by an element's degree - 2 for "arr[i]" of a
 * local array (p10_elem's "1.5 + arr[i]") - so "(d * d) + arr[i] + i"
 * loads the element first and converts i through DI?; a shift by more
 * than one and a right shift converted in AX ("(d * e) + (i << 3)" -
 * "mov cx,*3." / "sal ax,cl"?, "(d / e) - (j >> 1)" - "sar ax,*1"?, as
 * p13_open's "i << 1"); a call's result, then an element ("tw(e) +
 * arr[i]").
 * Result: 45.
 */
double tw(x)
double x;
{
	return x * 2;
}

main()
{
	double d, e, arr[3];
	int i, j, r;

	i = 1;
	j = 6;
	r = 0;
	d = 2.0;
	e = 0.5;
	arr[1] = 3.0;
	d = (d * d) + arr[i] + i;
	r = r + (int) d;
	d = (d * e) + (i << 3);
	r = r + (int) d;
	d = (d / e) - (j >> 1);
	r = r + (int) d;
	d = tw(e) + arr[i];
	r = r + (int) d;
	return r;
}
