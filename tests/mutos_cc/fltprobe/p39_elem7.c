/*
 * fltprobe/p39_elem7.c
 *
 * No floating point: int shapes mutos_c1 compiles by inference only after
 * the round-9 goldens, and some it still refuses. Inferred: "x = c % d + c
 * % y" (p35's "c % d + c / d": the right one pushed from DX, "add
 * dx,bx"?), "x = f(1) - c / d", "x = c / d - f(1)", "x = c % d - c / d"
 * and "x = f(1) - c % d" (p35's "f(1) - g(2)": the right one pushed, "pop
 * bx" / "sub ax,bx"?), "x = f(1) + c * d" (p35's "c * d + f(1)" written
 * the other way round: "mov di,ax" / "imul" / "add di,ax"?), "x = f(1) +
 * b[i]" (p35's "b[i] + f(1)" the other way round: the element's address
 * pushed first?), "x = (y > 2) + (x <= 3) * 4" (the scaled comparison into
 * SI, "sal si,*1" twice?), "x = (y > 2) + (5 < x)" (a constant on the
 * left), "x = (y < 2) + (y > 1) * 2 + (x == 3)", "x = g(2) * 3 + f(1)"
 * (p35's "f(1) + g(2) * 3" the other way round), "x = c % d + f(1)"
 * (p35's "c / d + f(2)": the remainder first, pushed from DX?), "x = f(2)
 * * f(3) * 2" (p35's "f(1) * g(2)" doubled: "mov di,ax" / "sal di,*1"?),
 * "x = x - f(1)" (the call first, then "mov di,x" / "sub di,ax"?), "x =
 * f(1) - x" ("mov di,ax" / "sub di,x"?), "x = c * f(1) + d" ("mov ax,ax" /
 * "imul c" / "add ax,d"?) and "x = (y > 2) + (x < y)" (two variables
 * compared on the right: pushed?). Refused: "x = f(1) & b[i]", "x = c * d
 * + c * y", "x = c * d - f(1)", "x = f(1) + c * 3", "x = (y > 2) - (x <
 * 3)", "x = f(1) + g(2) - f(3)" and "x = f(1) | g(2)".
 * Result: 1141.
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
	int b[4];
	int i, x, y, c, d, r;

	r = 0;
	b[0] = 1;
	b[1] = 2;
	b[2] = 3;
	b[3] = 4;
	i = 1;
	x = 5;
	y = 3;
	c = 17;
	d = 5;
	x = c % d + c % y;
	r = r + x;
	x = f(1) - c / d;
	r = r + x;
	x = c / d - f(1);
	r = r + x;
	x = c % d - c / d;
	r = r + x;
	x = f(1) - c % d;
	r = r + x;
	x = f(1) + c * d;
	r = r + x;
	x = f(1) + b[i];
	r = r + x;
	x = (y > 2) + (x <= 3) * 4;
	r = r + x;
	x = (y > 2) + (5 < x);
	r = r + x;
	x = (y < 2) + (y > 1) * 2 + (x == 3);
	r = r + x;
	x = g(2) * 3 + f(1);
	r = r + x;
	x = c % d + f(1);
	r = r + x;
	x = f(2) * f(3) * 2;
	r = r + x;
	x = x - f(1);
	r = r + x;
	x = f(1) - x;
	r = r + x;
	x = c * f(1) + d;
	r = r + x;
	x = (y > 2) + (x < y);
	r = r + x;
	x = f(1) & b[i];
	r = r + x;
	x = c * d + c * y;
	r = r + x;
	x = c * d - f(1);
	r = r + x;
	x = f(1) + c * 3;
	r = r + x;
	x = (y > 2) - (x < 3);
	r = r + x;
	x = f(1) + g(2) - f(3);
	r = r + x;
	x = f(1) | g(2);
	r = r + x;
	return r;
}
