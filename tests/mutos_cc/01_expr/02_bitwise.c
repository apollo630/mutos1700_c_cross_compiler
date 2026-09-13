/*
 * 01_expr/02_bitwise.c
 *
 * Bitwise AND, OR, XOR and one's complement.
 */
main()
{
	int a, b, c;

	a = 0xF0;
	b = 0x0F;
	c = a & b;
	c = a | b;
	c = a ^ b;
	c = ~a;
	return c;
}
