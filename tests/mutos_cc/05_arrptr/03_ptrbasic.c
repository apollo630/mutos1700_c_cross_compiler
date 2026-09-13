/*
 * 05_arrptr/03_ptrbasic.c
 */
main()
{
	int x, y;
	int *p;

	x = 10;
	p = &x;
	*p = 20;
	y = *p;
	p = &y;
	*p = *p + 1;
	return y;
}
