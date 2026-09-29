









double gd;
float gf;
static double sd;
extern double xd;
double gi = 2.5;
float gfi = 1.5;
double ga[3];
double *gp;
struct pt {
	double x;
	float y;
} gs;

main()
{
	static double ld;
	double arr[2];
	double *p;

	gd = 1.5;
	gf = gd;
	sd = gf;
	ld = sd;
	xd = ld + gfi;
	gp = &gd;
	arr[1] = gd;
	p = arr;
	*p = 2.0;
	gs.x = *gp;
	gs.y = gs.x;
	ga[2] = arr[1] + xd;
	return (int) (gd + gi + ga[2] + arr[0] + gs.y);
}

double xd;
