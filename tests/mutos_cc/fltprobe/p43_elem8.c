/*
 * fltprobe/p43_elem8.c
 *
 * No floating point: int shapes mutos_c1 compiles by inference only after
 * the round-10 goldens, and some it still refuses. p39 showed "f(2) * f(3)
 * * 2" shifted where the product is ("sal ax,*1"), "x - f(1)" with the
 * call pushed first, "f(1) - x" in AX ("sub ax,x"), "(y > 2) + (x < y)"
 * in SI ("mov si,y" / "cmp x,si"), "f(1) & b[i]" with the element's
 * address pushed, "c * d + c * y" with the left product moved into DI,
 * "(y > 2) - (x < 3)" ("sub di,si"), "f(1) + g(2) - f(3)" and "f(1) |
 * g(2)" with the right call pushed. Inferred: "x = f(1) * 2", "x = f(1) *
 * 4", "x = f(1) * 8" (by CL?), "x = c / d * 2" ("sal ax,*1" after the
 * "idiv"?), "x = c % d * 2" ("sal dx,*1"?), "x = c * d * 2", "x = f(1) <<
 * 1", "x = f(1) >> 1", "x = c / d << 3" and "x = c % d >> 1" ("mov cx,*3."
 * / "sal ax,cl", "sar dx,*1"?); "x = 5 - f(1)", "x = x - c / d", "x = x - c % d"
 * ("push dx"?), "x = c * 3 - f(1)", "x = b[i] - f(1)" (the call pushed,
 * the element loaded after it?), "x = f(1) - b[i]" (the element's
 * address pushed first, "sub ax,(bx)"?); "x = c * d - x", "x = c / d - x"
 * and "x = c % d - x" (in AX, or DX?); "x = (y > 2) - (x < y)", "x = (y >
 * 2) - (x < 3) * 2", "x = (x < y) + (y > x)", "x = (x < y) - (y < 9)";
 * "x = f(1) | b[i]", "x = f(1) ^ b[i]", "x = f(1) & g(2)", "x = f(1) ^
 * g(2)"; "x = c * 3 + c * 5", "x = c * d + c * 3". Refused: "y = y % (c /
 * d - s)" (the divisor computed into AX), "x = f(1) * 2 + c / d", "x = f(1)
 * * 3 - f(2)" and "x = c * d + f(1) * 3".
 * Result: 1100.
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
	int i, x, y, c, d, s, r;

	r = 0;
	b[0] = 1;
	b[1] = 6;
	b[2] = 3;
	b[3] = 12;
	i = 1;
	x = 5;
	y = 3;
	c = 17;
	d = 5;
	s = 2;
	x = f(1) * 2;
	r = r + x;
	x = f(1) * 4;
	r = r + x;
	x = f(1) * 8;
	r = r + x;
	x = c / d * 2;
	r = r + x;
	x = c % d * 2;
	r = r + x;
	x = c * d * 2;
	r = r + x;
	x = f(1) << 1;
	r = r + x;
	x = f(1) >> 1;
	r = r + x;
	x = c / d << 3;
	r = r + x;
	x = c % d >> 1;
	r = r + x;
	x = 5 - f(1);
	r = r + x;
	x = 50;
	x = x - c / d;
	r = r + x;
	x = x - c % d;
	r = r + x;
	x = c * 3 - f(1);
	r = r + x;
	x = b[i] - f(1);
	r = r + x;
	x = f(1) - b[i];
	r = r + x;
	x = c * d - x;
	r = r + x;
	x = c / d - x;
	r = r + x;
	x = c % d - x;
	r = r + x;
	x = 2;
	x = (y > 2) - (x < y);
	r = r + x;
	x = (y > 2) - (x < 3) * 2;
	r = r + x;
	x = 4;
	x = (x < y) + (y > x);
	r = r + x;
	x = (x < y) - (y < 9);
	r = r + x;
	x = f(1) | b[i];
	r = r + x;
	x = f(1) ^ b[i];
	r = r + x;
	x = f(1) & g(2);
	r = r + x;
	x = f(1) ^ g(2);
	r = r + x;
	x = c * 3 + c * 5;
	r = r + x;
	x = c * d + c * 3;
	r = r + x;
	y = y % (c / d - s);
	r = r + y;
	x = f(1) * 2 + c / d;
	r = r + x;
	x = f(1) * 3 - f(2);
	r = r + x;
	x = c * d + f(1) * 3;
	r = r + x;
	return r;
}
