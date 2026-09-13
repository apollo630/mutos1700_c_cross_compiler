/*
 * 02_long/01_addsub.c
 *
 * A local "long" variable: +, -, and comparison.  Directly probes
 * the PDP-11-style middle-endian word order MUTOS_C_ABI.md sect. 1.6
 * requires for every long local (high word at the LOWER stack
 * offset, low word at the HIGHER offset).
 */
main()
{
	long a, b, c;

	a = 100000;
	b = 23456;
	c = a + b;
	c = a - b;
	if (c > 0L)
		c = c + 1L;
	return (int) c;
}
