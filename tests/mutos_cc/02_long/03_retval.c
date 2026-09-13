/*
 * 02_long/03_retval.c
 *
 * A function returning "long" must return it in DX:AX (DX = high
 * word), per MUTOS_C_ABI.md sect. 1.5 -- directly analogous to the
 * real _atol()/aldiv() evidence cited there.
 */
long
addlong(a, b)
long a, b;
{
	return a + b;
}

main()
{
	long r;

	r = addlong(100000L, 5L);
	return (int) r;
}
