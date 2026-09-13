/*
 * 01_expr/05_incdec.c
 *
 * Pre/post increment and decrement on a plain int and on a pointer.
 * The pointer case exercises pointer-scaled arithmetic (a "ptr to
 * int" must step by SZINT=2 bytes, see v7/cc/c0.h).
 */
main()
{
	int i, j;
	int a[4];
	int *p;

	i = 0;
	j = i++;
	j = ++i;
	j = i--;
	j = --i;

	p = a;
	*p++ = 1;
	*++p = 2;
	return j;
}
