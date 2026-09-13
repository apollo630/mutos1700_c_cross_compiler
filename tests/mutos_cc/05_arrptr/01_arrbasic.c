/*
 * 05_arrptr/01_arrbasic.c
 */
main()
{
	int a[5];
	int i, sum;

	for (i = 0; i < 5; i = i + 1)
		a[i] = i * i;

	sum = 0;
	for (i = 0; i < 5; i = i + 1)
		sum = sum + a[i];
	return sum;
}
