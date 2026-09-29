/*
 * fltprobe/p9_init.c
 *
 * File-scope floating initializers p3_global does not have: an int
 * constant ("double gi = 2;" - an ITOF(CON) under INIT: its text, and
 * does it take a c1 label?), a constant that is not exactly a float, into
 * a double and into a float (which text, which directive?), a negated
 * one, a 'static' one (no ".globl"?), and a code constant after them (its
 * label number). mutos_c0/mutos_c1 refuse these.
 * Result: 6.
 */
double gi = 2;
double gx = 0.1;
float gy = 0.1;
double gn = -1.5;
static double gs = 2.5;

main()
{
	double d;

	d = gi + gx + gy + gn + gs + 3.5;
	return (int) d;
}
