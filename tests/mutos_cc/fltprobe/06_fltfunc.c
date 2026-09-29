/*
 * fltprobe/06_fltfunc.c
 *
 * Double parameters (8 bytes of the frame - ecvt.o's "double arg"),
 * a float parameter (a double - v7's funchead()), double arguments
 * ("sub sp,*8." / "mov ax,sp" / "call fstdp"), functions returning a
 * double ("lea ax,fac" / "call fstdp" / "lea ax,fac" - atof.o's
 * "return(fl)") and their callers ("call fldd" after the call). The
 * golden checks "|RTYP 3" and where the return sequence goes relative
 * to "jmp L<n>" (libc.a's optimized code cannot show that).
 * Result: 26.
 */
double scale(x, n)
double x;
int n;
{
	x *= 2;
	return (x + x) * n;
}

double mid(a, b)
float a;
double b;
{
	double two;

	two = 2;
	return (a + b) / two;
}

double half(x)
double x;
{
	return x / 2;
}

main()
{
	double d, e;
	float f;
	int i;

	d = 1.5;
	f = 2.5;
	i = 3;
	e = scale(d, i);
	d = mid(f, e);
	e = half(d) * d;
	i = half(e);
	return i;
}
