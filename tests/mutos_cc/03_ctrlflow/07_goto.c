/*
 * 03_ctrlflow/07_goto.c
 */
main()
{
	int i, sum;

	i = 0;
	sum = 0;
loop:
	if (i >= 10)
		goto done;
	sum = sum + i;
	i = i + 1;
	goto loop;
done:
	return sum;
}
