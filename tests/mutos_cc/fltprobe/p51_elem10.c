/*
 * fltprobe/p51_elem10.c
 *
 * No floating point: int shapes mutos_c1 compiles by inference only after
 * the round-12 goldens, and some it still refuses. p47 showed a remainder
 * plus a variable or a constant added in DX ("add dx,x" - also as a
 * divisor, then "push dx"), every computed divisor pushed first ("y % (c -
 * s)" -> "mov di,c" / "sub di,s" / "push di" / "mov di,y" / "mov ax,di" /
 * "cwd" / "pop cx" / "idiv cx" - never "idiv di"), "x - f(1) * 3" with the
 * product pushed, "f(1) - (x < y)" with the call's value moved into DI
 * first and the comparison into SI ("sub di,si" - v7's "%n,e"), "c % d -
 * b[i]" with the element's address pushed ("sub dx,(bx)"), "c * 3 + f(1) *
 * 5" with the call's product moved into DI, "f(1) * 2 + f(2) * 3" with the
 * left product pushed first (its degree, 10, the lower). Inferred: "a - (c
 * - d)", "a - (c + 7)", "5 - (c + d)", "a - (c < d)", "a - (c > 2) * 4",
 * "f(1) - (c + d)", "(c < d) - (a - s)", "(a + c) - (d - s)" (the left one
 * into DI, the right one into SI?), "c % d + 1", "c % d - 1" ("inc dx",
 * "dec dx"?), "c % d + b[i]", "c / d - b[i]", "c / d + b[i]" (the address
 * pushed?), "y / (c + d)", "y % (c << 1)", "b[i] / (c - d)", "y / (-c)",
 * "(c + d) % (a - s)" (pushed?), "x - f(1) * 2", "f(1) * 3 + f(2) * 2",
 * "f(1) * 4 + f(2) * 8" (the right one pushed?), "c * 5 + f(1)", "c * d +
 * f(1) * 4", "x - c * d" (the product first, "mov di,x" after it?), "c % d
 * - (a - s)", "c % d * 3", "c % d + c % s", "f(1) * 3 - f(2) * 2", "a - (c
 * - d) * 2", "f(1) / (c - d)" ("idiv di"?). Refused: "x = c % d & b[i]",
 * "x = c % d | f(1)" and "x = c % d + f(1) * 2".
 * Result: 434.
 */
f(n)
{
	return n + 10;
}

main()
{
	int b[4];
	int i, x, y, c, d, a, s, r;

	r = 0;
	b[0] = 1;
	b[1] = 6;
	b[2] = 3;
	b[3] = 12;
	i = 1;
	a = 9;
	c = 17;
	d = 5;
	s = 2;
	x = a - (c - d);
	r = r + x;
	x = a - (c + 7);
	r = r + x;
	x = 5 - (c + d);
	r = r + x;
	x = a - (c < d);
	r = r + x;
	x = a - (c > 2) * 4;
	r = r + x;
	x = f(1) - (c + d);
	r = r + x;
	x = (c < d) - (a - s);
	r = r + x;
	x = (a + c) - (d - s);
	r = r + x;
	x = c % d + 1;
	r = r + x;
	x = c % d - 1;
	r = r + x;
	x = c % d + b[i];
	r = r + x;
	x = c / d - b[i];
	r = r + x;
	x = c / d + b[i];
	r = r + x;
	y = 300;
	y = y / (c + d);
	r = r + y;
	y = 300;
	y = y % (c << 1);
	r = r + y;
	y = b[i] / (c - d);
	r = r + y;
	y = 300;
	y = y / (-c);
	r = r + y;
	x = (c + d) % (a - s);
	r = r + x;
	x = 7;
	x = x - f(1) * 2;
	r = r + x;
	x = f(1) * 3 + f(2) * 2;
	r = r + x;
	x = f(1) * 4 + f(2) * 8;
	r = r + x;
	x = c * 5 + f(1);
	r = r + x;
	x = c * d + f(1) * 4;
	r = r + x;
	x = 50;
	x = x - c * d;
	r = r + x;
	x = c % d - (a - s);
	r = r + x;
	x = c % d * 3;
	r = r + x;
	x = c % d + c % s;
	r = r + x;
	x = f(1) * 3 - f(2) * 2;
	r = r + x;
	x = a - (c - d) * 2;
	r = r + x;
	x = f(1) / (c - d);
	r = r + x;
	x = c % d & b[i];
	r = r + x;
	x = c % d | f(1);
	r = r + x;
	x = c % d + f(1) * 2;
	r = r + x;
	return r;
}
