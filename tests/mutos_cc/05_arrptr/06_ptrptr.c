/*
 * 05_arrptr/06_ptrptr.c
 */
main()
{
	int x;
	int *p;
	int **pp;

	x = 5;
	p = &x;
	pp = &p;
	**pp = 6;
	return x;
}
