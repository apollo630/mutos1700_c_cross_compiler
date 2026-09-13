/*
 * 03_ctrlflow/03_dowhile.c
 */
main()
{
	int i, sum;

	i = 0;
	sum = 0;
	do {
		sum = sum + i;
		i = i + 1;
	} while (i < 10);
	return sum;
}
