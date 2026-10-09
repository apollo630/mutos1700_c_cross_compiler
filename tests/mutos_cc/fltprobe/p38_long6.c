/*
 * fltprobe/p38_long6.c
 *
 * No floating point: 'long' shapes mutos_c1 compiles by inference only
 * after the round-9 goldens, and some it still refuses. Inferred: "l <<=
 * 1" (p34's "l <<= 3" and "l >>= 2": the count into CX and the pair
 * looped - for 1 too?), "l <<= i" and "l >>= i" (p31's "l << i": "or
 * cx,cx" / "jz .+8"?), "l += 5L" (an in-range long constant: "mov si,*5."
 * / "mov di,*0." as p34's "l += -5"?), "l -= 70000", "l /= i" (p34's "l /=
 * m" with an int: pushed from DX:AX, "aldiv"?), "l %= m" and "l %= 7"
 * ("alrem"?), "l *= m" ("almul" with a long variable), "x = (int) (l /
 * m)" and "x = (int) (l % 7)" (p34's "(int) (l * 2)": the low word stored
 * straight from AX?), "l = (l + m) - 3" (p34's "l - m - 1"), "l = (l - m)
 * + 70000", "l = l * m - 1", "l = l / m + l", "l = 5L - l" and "l = -5L -
 * l" (p34's "100000 - l"), "l = m + -i" and "l = m - -i" (p34's "l = -i":
 * negated in AX, then "cwd"?). Refused: "l = (l - m) + 3" (a non-negative
 * int constant added to a computed long), "l = 5 - l" (an int constant on
 * the left of '-'), "l = l * m * 2" (a product times a constant) and "l
 * <<= 0".
 * Result: 525.
 */
main()
{
	long l, m;
	int i, x, r;

	r = 0;
	l = 300;
	m = 7;
	i = 3;
	l <<= 1;
	r = r + (int) (l - 590);
	l <<= i;
	r = r + (int) (l - 4790);
	l >>= i;
	r = r + (int) (l - 590);
	l += 5L;
	r = r + (int) (l - 600);
	l -= 70000;
	r = r + (int) (l + 69400);
	l = 1000;
	l /= i;
	r = r + (int) (l - 300);
	l %= m;
	r = r + (int) l;
	l = 50;
	l %= 7;
	r = r + (int) l;
	l = 6;
	l *= m;
	r = r + (int) l;
	l = 100;
	x = (int) (l / m);
	r = r + x;
	x = (int) (l % 7);
	r = r + x;
	l = 40;
	l = (l + m) - 3;
	r = r + (int) l;
	l = (l - m) + 70000;
	r = r + (int) (l - 70000);
	l = l * m - 1;
	r = r + (int) (l - 490200);
	l = l / m + l;
	r = r + (int) (l - 560200);
	l = 40;
	l = 5L - l;
	r = r + (int) (l + 50);
	l = -5L - l;
	r = r + (int) (l + 20);
	l = m + -i;
	r = r + (int) l;
	l = m - -i;
	r = r + (int) l;
	l = 40;
	l = (l - m) + 3;
	r = r + (int) l;
	l = 5 - l;
	r = r + (int) (l + 40);
	l = l * m * 2;
	r = r + (int) (l + 500);
	l <<= 0;
	r = r + (int) (l + 400);
	return r;
}
