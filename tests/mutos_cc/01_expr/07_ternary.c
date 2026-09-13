/*
 * 01_expr/07_ternary.c
 *
 * The ?: conditional operator and the comma operator.
 */
main()
{
	int a, b, m;

	a = 4;
	b = 9;
	m = a > b ? a : b;
	m = (a = a + 1, b = b + 1, a + b);
	return m;
}
