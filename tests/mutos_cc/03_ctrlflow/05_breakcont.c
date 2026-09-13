/*
 * 03_ctrlflow/05_breakcont.c
 *
 * Nested loops with break/continue, exercising the compiler's
 * brklab/contlab label stack (see v7/cc/c0.h: brklab, contlab).
 */
main()
{
	int i, j, count;

	count = 0;
	for (i = 0; i < 5; i = i + 1) {
		for (j = 0; j < 5; j = j + 1) {
			if (j == 3)
				break;
			if (i == j)
				continue;
			count = count + 1;
		}
	}
	return count;
}
