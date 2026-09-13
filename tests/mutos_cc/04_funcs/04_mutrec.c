/*
 * 04_funcs/04_mutrec.c
 *
 * Mutual recursion requires a forward declaration of "iseven" before
 * "isodd" is defined -- an ordinary K&R external declaration.
 */
int iseven();

isodd(n)
int n;
{
	if (n == 0)
		return 0;
	return iseven(n - 1);
}

iseven(n)
int n;
{
	if (n == 0)
		return 1;
	return isodd(n - 1);
}

main()
{
	return iseven(10);
}
