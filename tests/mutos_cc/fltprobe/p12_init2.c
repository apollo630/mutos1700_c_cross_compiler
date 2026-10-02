/*
 * fltprobe/p12_init2.c
 *
 * File-scope floating initializers mutos_c1 writes by inference from
 * p9_init (doinit(): the variable's directive, the constant converted to
 * its type): an int constant into a float ("CON" / "ITOF(FLOAT)"?), a
 * negated int constant (folded by c0: "CON -2"), a negated inexact
 * constant into a float (truncated: "-9.99999940395355225e-02"?), a
 * 'static float', and 0.3 into a float (libc.a's fstsp truncates - so
 * "2.99999982118606567e-01"?).
 * Result: 7 (6 if the floats are truncated, as mutos_c1 writes them).
 */
float gf = 4;
double gm = -2;
float gg = -0.1;
static float gs = 2.5;
float gt = 0.3;

main()
{
	double d;

	d = gf + gm + gs + gg * 10 + gt * 10;
	return (int) (d + 0.5);
}
