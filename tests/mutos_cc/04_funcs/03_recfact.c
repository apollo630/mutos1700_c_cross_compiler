/*
 * 04_funcs/03_recfact.c
 */
fact(n)
int n;
{
	if (n <= 1)
		return 1;
	return n * fact(n - 1);
}

main()
{
	return fact(6);
}
