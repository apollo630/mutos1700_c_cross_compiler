/*
 * 05_arrptr/02_array2d.c
 */
main()
{
	int m[3][4];
	int i, j, sum;

	for (i = 0; i < 3; i = i + 1)
		for (j = 0; j < 4; j = j + 1)
			m[i][j] = i * 10 + j;

	sum = 0;
	for (i = 0; i < 3; i = i + 1)
		for (j = 0; j < 4; j = j + 1)
			sum = sum + m[i][j];
	return sum;
}
