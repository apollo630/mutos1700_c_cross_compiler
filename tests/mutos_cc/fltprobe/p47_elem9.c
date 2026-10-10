/*
 * fltprobe/p47_elem9.c
 *
 * No floating point: int shapes mutos_c1 compiles by inference only after
 * the round-11 goldens, and some it still refuses. p43 showed "y % (c / d
 * - s)" with the divisor computed in AX and pushed ("sub ax,s" / "push ax"
 * / "mov di,y" / "mov ax,di" / "cwd" / "pop cx" / "idiv cx"), "c % d - x"
 * subtracted in DX ("sub dx,x"), "f(1) * 2 + c / d" and "f(1) * 3 - f(2)"
 * with the right one pushed first, "c * d + f(1) * 3" with the call's
 * product moved into DI. Inferred: "y / (c % d + s)", "y / f(1)", "y %
 * f(2)", "y / (c / d)", "b[i] / (c / d - s)", "y % (f(1) - s)" (pushed the
 * same way?); "y % (c - s)" and "y % (c * d - s)" (computed into DI first,
 * "idiv di" - no golden yet either); "c % d + x" and "c % d + 3" (moved
 * into DI - or added in DX, as p43's '-'?); "f(1) * 2 + c % d", "f(1) * 3
 * - c / d", "c * d + f(1) * 2", "f(1) * 3 - x", "x - f(1) * 3", "f(2) * 4
 * - f(1)", "f(1) % c", "x * (c / d)", "f(1) - (x < y)". Refused: "x = y %
 * (c * d)", "x = c % d - b[i]", "x = c * 3 + f(1) * 5" and "x = f(1) * 2 +
 * f(2) * 3".
 * Result: 563.
 */
f(n)
{
	return n + 10;
}

main()
{
	int b[4];
	int i, x, y, c, d, s, r;

	r = 0;
	b[0] = 1;
	b[1] = 60;
	b[2] = 3;
	b[3] = 12;
	i = 1;
	x = 5;
	c = 17;
	d = 5;
	s = 2;
	y = 300;
	y = y / (c % d + s);
	r = r + y;
	y = y / f(1);
	r = r + y;
	y = 300;
	y = y % f(2);
	r = r + y;
	y = 300;
	y = y / (c / d);
	r = r + y;
	x = b[i] / (c / d - s);
	r = r + x;
	y = 300;
	y = y % (f(1) - s);
	r = r + y;
	y = 300;
	y = y % (c - s);
	r = r + y;
	y = 300;
	y = y % (c * d - s);
	r = r + y;
	x = 50;
	x = c % d + x;
	r = r + x;
	x = c % d + 3;
	r = r + x;
	x = f(1) * 2 + c % d;
	r = r + x;
	x = f(1) * 3 - c / d;
	r = r + x;
	x = c * d + f(1) * 2;
	r = r + x;
	x = f(1) * 3 - x;
	r = r + x;
	x = x - f(1) * 3;
	r = r + x;
	x = f(2) * 4 - f(1);
	r = r + x;
	x = f(1) % c;
	r = r + x;
	x = 7;
	x = x * (c / d);
	r = r + x;
	y = 3;
	x = f(1) - (x < y);
	r = r + x;
	y = 300;
	x = y % (c * d);
	r = r + x;
	x = c % d - b[i];
	r = r + x;
	x = c * 3 + f(1) * 5;
	r = r + x;
	x = f(1) * 2 + f(2) * 3;
	r = r + x;
	return r;
}
