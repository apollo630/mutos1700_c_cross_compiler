/*
 * 06_struct/01_stbasic.c
 */
struct point {
	int x;
	int y;
};

main()
{
	struct point p;

	p.x = 3;
	p.y = 4;
	return p.x + p.y;
}
