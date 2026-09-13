/*
 * 03_ctrlflow/02_while.c
 */
main()
{
	int i, sum;

	i = 0;
	sum = 0;
	while (i < 10) {
		sum = sum + i;
		i = i + 1;
	}
	return sum;
}
