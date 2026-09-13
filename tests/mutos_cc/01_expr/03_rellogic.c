/*
 * 01_expr/03_rellogic.c
 *
 * Relational and logical operators: <, <=, >, >=, ==, !=, &&, ||, !
 */
main()
{
	int a, b, r;

	a = 3;
	b = 7;
	r = a < b;
	r = a <= b;
	r = a > b;
	r = a >= b;
	r = a == b;
	r = a != b;
	r = (a < b) && (b > 0);
	r = (a < 0) || (b > 0);
	r = !r;
	return r;
}
