/*
 * 06_struct/05_nestst.c
 */
struct point {
	int x;
	int y;
};

struct rect {
	struct point topleft;
	struct point botright;
};

area(rp)
struct rect *rp;
{
	int w, h;

	w = rp->botright.x - rp->topleft.x;
	h = rp->botright.y - rp->topleft.y;
	return w * h;
}

main()
{
	struct rect r;

	r.topleft.x = 0;
	r.topleft.y = 0;
	r.botright.x = 10;
	r.botright.y = 4;
	return area(&r);
}
