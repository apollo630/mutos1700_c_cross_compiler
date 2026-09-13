/*
 * 04_funcs/06_regclass.c
 *
 * The "register" storage class hint on a loop induction variable.
 * A K&R compiler is free to ignore it (the ABI doesn't require it to
 * change codegen), but mutos_c1 must at least parse and accept it.
 */
main()
{
	register int i;
	int sum;

	sum = 0;
	for (i = 0; i < 100; i = i + 1)
		sum = sum + i;
	return sum;
}
