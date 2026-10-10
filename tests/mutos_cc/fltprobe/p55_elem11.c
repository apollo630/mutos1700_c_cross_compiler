/*
 * fltprobe/p55_elem11.c
 *
 * No floating point: int shapes mutos_c1 compiles by inference only after
 * the round-13 goldens. p51 showed v7's "%n,e" reaching further - "x - c *
 * d" with x loaded into DI first and the product subtracted from AX ("sub
 * di,ax"), "c % d - (a - s)" with the remainder moved into DI ("mov
 * di,dx"), "a - (c - d) * 2" with the difference shifted in SI ("sal
 * si,*1") -, "f(1) / (c - d)" with the divisor pushed first ("mov ax,ax" /
 * "cwd" / "pop cx" / "idiv cx"), "f(1) * 4 + f(2) * 8" factored by v7's
 * distrib() ("(f(2) * 2 + f(1)) * 4" - f(1) pushed, f(2) shifted, "sal
 * ax,*1" twice after the sum), and a remainder or quotient pushed first
 * next to a call or a call's product ("c % d | f(1)", "c % d + f(1) * 2")
 * or after an element's address ("c % d & b[i]"). Inferred: "y - c * 3",
 * "5 - c * d", "f(1) - c * d" (the left one into DI first?), "c * d - (a
 * - s)", "c / d - (a - s)", "c / d - (a + 7)", "c % d - (s - 7)" ("mov
 * di,ax", "mov di,dx" first?), "a - (c + d) * 4", "f(1) - (c - d) * 2", "5
 * - (c + d) * 2" (shifted in SI?), "f(1) % (c + d)", "f(1) / (c * d)",
 * "f(1) % (c - 3)", "(c * d) / (a - s)", "(c / d) % (a - s)", "(c % d) /
 * (a - 8)" (the divisor pushed first, "mov ax,ax", "mov ax,dx"?), "f(1) *
 * 2 + f(2) * 4 + f(3) * 8", "f(1) + f(2) * 2 + f(3) * 4", "f(1) * 8 + f(2)
 * * 2", "f(1) * 2 + f(2) * 2", "f(1) * 4 + f(2) * 8 + x", "f(1) * 4 -
 * f(2) * 8" (distrib()?), "c / d | f(1)", "c % d ^ f(2) * 3", "c / d +
 * f(1) * 3", "c / d & f(1) * 2" (pushed first?), "c / d & b[i]", "c % d |
 * b[i]", "c % d ^ b[i]", "c / d ^ b[i]" (the address pushed?). Refused:
 * "x = c % d * 4 + f(1)".
 * Result: 705.
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
	y = 300;
	x = y - c * 3;
	r = r + x;
	x = 5 - c * d;
	r = r + x;
	x = f(1) - c * d;
	r = r + x;
	x = c * d - (a - s);
	r = r + x;
	x = c / d - (a - s);
	r = r + x;
	x = c / d - (a + 7);
	r = r + x;
	x = c % d - (s - 7);
	r = r + x;
	x = a - (c + d) * 4;
	r = r + x;
	x = f(1) - (c - d) * 2;
	r = r + x;
	x = 5 - (c + d) * 2;
	r = r + x;
	x = f(1) % (c + d);
	r = r + x;
	x = f(1) / (c * d);
	r = r + x;
	x = f(1) % (c - 3);
	r = r + x;
	x = (c * d) / (a - s);
	r = r + x;
	x = (c / d) % (a - s);
	r = r + x;
	x = (c % d) / (a - 8);
	r = r + x;
	x = f(1) * 2 + f(2) * 4 + f(3) * 8;
	r = r + x;
	x = f(1) + f(2) * 2 + f(3) * 4;
	r = r + x;
	x = f(1) * 8 + f(2) * 2;
	r = r + x;
	x = f(1) * 2 + f(2) * 2;
	r = r + x;
	x = 4;
	x = f(1) * 4 + f(2) * 8 + x;
	r = r + x;
	x = f(1) * 4 - f(2) * 8;
	r = r + x;
	x = c / d | f(1);
	r = r + x;
	x = c % d ^ f(2) * 3;
	r = r + x;
	x = c / d + f(1) * 3;
	r = r + x;
	x = c / d & f(1) * 2;
	r = r + x;
	x = c / d & b[i];
	r = r + x;
	x = c % d | b[i];
	r = r + x;
	x = c % d ^ b[i];
	r = r + x;
	x = c / d ^ b[i];
	r = r + x;
	x = c % d * 4 + f(1);
	r = r + x;
	return r;
}
