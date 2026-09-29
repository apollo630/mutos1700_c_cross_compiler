/*
 * fltprobe/p8_misc.c
 *
 * A long converted with DI free (p5_call's "ltof" had a register
 * variable in DI: "mov si,<low>" / "push si" ... - DI here?), a double
 * read through a pointer as an operand ("*p + 1.5" - its degree), "i +=
 * d" and "i -= d" into an int (v7 converts d first, as for "i *= e"),
 * and a double array subscripted by a variable (an element of 8 bytes).
 * mutos_c1 refuses these.
 * Result: 6.
 */
main()
{
	double d, *p;
	double arr[3];
	long l;
	int i;

	l = 70000;
	d = l;
	p = &arr[1];
	*p = 2.5;
	d = *p + 1.5;
	i = 2;
	i += d;
	i -= d;
	arr[i] = d;
	d = arr[i] + 0.5;
	return i + (int) d;
}
