/*
 * 06_struct/04_stassign.c
 *
 * Whole-struct assignment: p2 = p1;  This is a distinct
 * intermediate-code operator (STRASG, see v7/cc/c0.h and c1.h) from
 * ordinary scalar assignment and deserves its own dedicated golden
 * case.
 */
struct point {
	int x;
	int y;
};

main()
{
	struct point p1, p2;

	p1.x = 11;
	p1.y = 22;
	p2 = p1;
	return p2.x + p2.y;
}
