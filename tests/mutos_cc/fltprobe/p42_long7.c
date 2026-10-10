/*
 * fltprobe/p42_long7.c
 *
 * No floating point: 'long' shapes mutos_c1 compiles by inference only
 * after the round-10 goldens, and some it still refuses. p38 showed "l <<=
 * 1" shifted in place ("sal" / "rcl" on the variable, then the value
 * loaded into DI:SI), "l <<= 0" no code at all, "l <<= i" with "jz .+16",
 * "5 - l" through "cwd" and "(l - m) + 3" and "l * m * 2" with the
 * constant pushed first. Inferred: "l >>= 1" ("sar" / "rcr" in place, the
 * high word first?), "l >>= 0" (nothing?), "l >>= i" after "l = -300"
 * (the sign shifted in), "l += 300L" and "l -= 300" (in place, "adc
 * ...,*0"?), "l = 300 - l" ("mov ax,#300." / "cwd"?), "l = (l + m) + 300",
 * "l = l * m + 3" and "l = l / m + 7" (the constant pushed first, the
 * left one moved into DI:SI?), "l = l * m * 3", "l = l / m * 2", "l = (l
 * * m) / 3" and "l = (l * m) % 5" (the product's or the quotient's DX:AX
 * pushed straight after the constant, then "lmul", "ldiv", "lrem"?), "l =
 * l * m + l" (no constant). Refused: "x = (int) ((l - m) + 3)", "l = (l +
 * m) * 2" and "x = (int) (l * m * 2)".
 * Result: 9554.
 */
main()
{
	long l, m;
	int i, x, r;

	r = 0;
	i = 3;
	l = 300;
	m = 7;
	l <<= 1;
	r = r + (int) (l - 590);
	l >>= 1;
	r = r + (int) (l - 290);
	l <<= 0;
	r = r + (int) (l - 290);
	l >>= 0;
	r = r + (int) (l - 290);
	l = -300;
	l >>= 1;
	r = r + (int) (l + 160);
	l <<= 1;
	r = r + (int) (l + 290);
	l <<= i;
	r = r + (int) (l + 2390);
	l >>= i;
	r = r + (int) (l + 290);
	l = 40;
	l += 5L;
	r = r + (int) l;
	l -= 5L;
	r = r + (int) l;
	l += 300L;
	r = r + (int) (l - 300);
	l -= 300;
	r = r + (int) l;
	l = 5L - l;
	r = r + (int) (l + 30);
	l = 5 - l;
	r = r + (int) l;
	l = 300 - l;
	r = r + (int) (l - 200);
	l = -5L - l;
	r = r + (int) (l + 300);
	l = 40;
	l = (l - m) + 3;
	r = r + (int) l;
	l = (l + m) + 300;
	r = r + (int) (l - 300);
	l = l * m + 3;
	r = r + (int) (l - 300);
	l = l / m + 7;
	r = r + (int) l;
	l = l * m * 2;
	r = r + (int) (l - 800);
	l = 40;
	l = l * m * 3;
	r = r + (int) (l - 800);
	l = l / m * 2;
	r = r + (int) l;
	l = (l * m) / 3;
	r = r + (int) l;
	l = (l * m) % 5;
	r = r + (int) l;
	l = 40;
	l = l * m + l;
	r = r + (int) l;
	l = 40;
	x = (int) ((l - m) + 3);
	r = r + x;
	l = (l + m) * 2;
	r = r + (int) l;
	x = (int) (l * m * 2);
	r = r + x;
	return r;
}
