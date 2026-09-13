/*
 * 10_integ/02_bubsort.c
 *
 * In-place bubble sort of an int array, passed by decaying to a
 * pointer; swapping via a helper that takes two "int *" parameters.
 */
swap(a, b)
int *a, *b;
{
	int t;

	t = *a;
	*a = *b;
	*b = t;
}

bsort(a, n)
int a[];
int n;
{
	int i, j;

	for (i = 0; i < n - 1; i = i + 1)
		for (j = 0; j < n - 1 - i; j = j + 1)
			if (a[j] > a[j + 1])
				swap(&a[j], &a[j + 1]);
}

main()
{
	int v[6];

	v[0] = 5;
	v[1] = 3;
	v[2] = 8;
	v[3] = 1;
	v[4] = 9;
	v[5] = 2;
	bsort(v, 6);
	return v[0] + v[5] * 10;
}
