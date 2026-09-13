/*
 * 02_long/02_muldiv.c
 *
 * long * / % : the 8086 has no 32x32 hardware multiply/divide, so
 * this must go through the compiler's internal almul/aldiv/alrem
 * runtime helpers described in MUTOS_C_ABI.md sect. 1.8.
 */
main()
{
	long a, b, c;

	a = 123456L;
	b = 37L;
	c = a * b;
	c = a / b;
	c = a % b;
	return (int) c;
}
