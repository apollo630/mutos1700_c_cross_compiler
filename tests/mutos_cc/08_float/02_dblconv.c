/*
 * 08_float/02_dblconv.c
 *
 * int<->double and double->long conversions (ITOF/FTOI/LTOF/FTOL
 * style conversion ops in v7/cc/c0.h).
 */
main()
{
	double d;
	int i;
	long l;

	i = 7;
	d = i;
	d = d / 2.0;
	l = (long) d;
	i = (int) d;
	return i;
}
