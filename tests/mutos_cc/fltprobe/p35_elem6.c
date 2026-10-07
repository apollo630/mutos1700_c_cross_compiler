/*
 * fltprobe/p35_elem6.c
 *
 * No floating point: int shapes mutos_c1 compiles by inference only after
 * the round-8 goldens, and some it still refuses. Inferred: "*ip -= y",
 * "*ip |= b[i]" and "*ip &= x + 1" (p32's "*ip += x": the pointer pushed,
 * "pop bx" / "sub (bx),di"?), "*ip += f(1)" and "b[i] += f(1)" (a call's
 * value added through a pointer, into an element), "x += y * 2", "x -=
 * b[i]" and "x &= b[i] + 1" (p32's "b[3] ^= b[i]": computed into DI, then
 * "sub x,di"?), "x = x + f(1) + y" (one call, two variables - the call
 * first, p32's "x + f(1) + g(2)"?), "x = f(1) + g(2) + y + f(3)" (the
 * three calls first), "x = f(1) + c / d", "x = c / d + f(2)", "x = f(1) +
 * c % d" and "x = c / d + c / y" (p32's quotient plus remainder: the right
 * one pushed), "x = (y > 2) + (y < 9) * 2 + y" (one scaled comparison) and
 * "x = (y > 2) + (x < 3)" (p32's comparison into SI after one in DI?).
 * Refused: "x = c % d + c / d" (a remainder on the left), "x = c * d +
 * f(1)", "x = f(1) - g(2)", "x = f(1) * g(2)", "x = b[i] + f(1)" and "x =
 * f(1) + g(2) * 3".
 * Result: 856.
 */
f(n)
{
	return n + 10;
}

g(n)
{
	return n * 20;
}

main()
{
	int b[4], *ip;
	int i, x, y, c, d, r;

	r = 0;
	b[0] = 1;
	b[1] = 2;
	b[2] = 3;
	b[3] = 4;
	ip = &b[2];
	i = 1;
	x = 5;
	y = 3;
	c = 17;
	d = 5;
	*ip -= y;
	r = r + b[2];
	*ip |= b[i];
	r = r + b[2];
	*ip &= x + 1;
	r = r + b[2];
	*ip += f(1);
	r = r + b[2];
	b[i] += f(1);
	r = r + b[1];
	x += y * 2;
	r = r + x;
	x -= b[i];
	r = r + x;
	x = 7;
	x &= b[i] + 1;
	r = r + x;
	x = x + f(1) + y;
	r = r + x;
	x = f(1) + g(2) + y + f(3);
	r = r + x;
	x = f(1) + c / d;
	r = r + x;
	x = c / d + f(2);
	r = r + x;
	x = f(1) + c % d;
	r = r + x;
	x = c / d + c / y;
	r = r + x;
	x = (y > 2) + (y < 9) * 2 + y;
	r = r + x;
	x = (y > 2) + (x < 3);
	r = r + x;
	x = c % d + c / d;
	r = r + x;
	x = c * d + f(1);
	r = r + x;
	x = f(1) - g(2);
	r = r + x;
	x = f(1) * g(2);
	r = r + x;
	x = b[i] + f(1);
	r = r + x;
	x = f(1) + g(2) * 3;
	r = r + x;
	return r;
}
