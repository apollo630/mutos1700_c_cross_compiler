/*
 * 01_expr/06_compasgn.c
 *
 * All compound assignment operators.
 */
main()
{
	int a;

	a = 10;
	a += 5;
	a -= 3;
	a *= 2;
	a /= 4;
	a %= 3;
	a <<= 1;
	a >>= 1;
	a &= 0x0F;
	a |= 0x30;
	a ^= 0x11;
	return a;
}
