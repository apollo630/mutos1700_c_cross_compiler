/*
 * 10_integ/05_matmul.c
 *
 * 2x2 integer matrix multiply: nested loops, 2D array indexing and
 * an accumulator inside the innermost loop -- a reasonably heavy
 * register-pressure case for the code generator's tree matcher.
 */
main()
{
	int a[2][2], b[2][2], c[2][2];
	int i, j, k, sum;

	a[0][0] = 1;
	a[0][1] = 2;
	a[1][0] = 3;
	a[1][1] = 4;
	b[0][0] = 5;
	b[0][1] = 6;
	b[1][0] = 7;
	b[1][1] = 8;

	for (i = 0; i < 2; i = i + 1) {
		for (j = 0; j < 2; j = j + 1) {
			sum = 0;
			for (k = 0; k < 2; k = k + 1)
				sum = sum + a[i][k] * b[k][j];
			c[i][j] = sum;
		}
	}
	return c[0][0] + c[0][1] + c[1][0] + c[1][1];
}
