/*
 * 06_struct/09_typedef.c
 */
typedef struct point {
	int x;
	int y;
} POINT;

typedef int INTEGER;

main()
{
	POINT p;
	INTEGER n;

	p.x = 1;
	p.y = 2;
	n = p.x + p.y;
	return n;
}
