/*
 * 03_ctrlflow/04_for.c
 */
main()
{
	int i, sum;

	sum = 0;
	for (i = 0; i < 10; i = i + 1)
		sum = sum + i;
	return sum;
}
