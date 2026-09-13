/*
 * 03_ctrlflow/01_ifelse.c
 */
main()
{
	int a, r;

	a = 5;
	if (a > 0)
		r = 1;
	else if (a < 0)
		r = -1;
	else
		r = 0;
	return r;
}
