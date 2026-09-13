/*
 * 06_struct/03_starray.c
 */
struct point {
	int x;
	int y;
};

main()
{
	struct point pts[3];
	int i, sum;

	for (i = 0; i < 3; i = i + 1) {
		pts[i].x = i;
		pts[i].y = i * 2;
	}

	sum = 0;
	for (i = 0; i < 3; i = i + 1)
		sum = sum + pts[i].x + pts[i].y;
	return sum;
}
