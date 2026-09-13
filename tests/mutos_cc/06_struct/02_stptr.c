/*
 * 06_struct/02_stptr.c
 *
 * Pointer to struct and the -> operator.
 */
struct point {
	int x;
	int y;
};

move(pp, dx, dy)
struct point *pp;
int dx, dy;
{
	pp->x = pp->x + dx;
	pp->y = pp->y + dy;
}

main()
{
	struct point p;

	p.x = 0;
	p.y = 0;
	move(&p, 5, 7);
	return p.x + p.y;
}
