/*
 * fltprobe/p34_long5.c
 *
 * No floating point: 'long' shapes mutos_c1 compiles by inference only
 * after the round-8 goldens, and some it still refuses. Inferred: "x =
 * ++l", "x = --l" and "x = l--" (p31's "x = l++": the low word as an int,
 * "mov di,l+2" / "inc l+2" - the prefix ones "inc" first?), "l += -5" and
 * "l -= -3" (a negative int constant widened - "mov ax,*-5." / "cwd" /
 * "mov di,dx" / "mov si,ax" / "add" / "adc", as p31's "l += 70000"?), "l =
 * l >> i" (p31's "l << i": "or cx,cx" / "jz .+8" / "sar di,*1" / "rcr
 * si,*1"?), "m = i * m" (p31's "m * i" written the other way round: m
 * pushed first all the same?), "l *= 3" ("almul" with a constant), "l =
 * -i" (an int negated, then widened), "l = l + m * 2" (a long times a power
 * of two), "l = l * m" (two long variables). Refused: "l <<= 3" and "l >>=
 * 2", "l /= m", "x = (int) (l * 2)" (the low word of a product), "l = l - m
 * - 1" and "l = 100000 - l" (a computed or constant long on the left of
 * '-').
 * Result: 248.
 */
main()
{
	long l, m;
	int i, x, r;

	r = 0;
	l = 300;
	m = 7;
	i = 2;
	x = ++l;
	r = r + x - 290;
	x = --l;
	r = r + x - 290;
	x = l--;
	r = r + x - 290;
	r = r + (int) (l - 290);
	l += -5;
	r = r + (int) (l - 290);
	l -= -3;
	r = r + (int) (l - 290);
	l = 70000;
	l = l >> i;
	r = r + (int) (l - 17490);
	m = i * m;
	r = r + (int) m;
	l = 5;
	l *= 3;
	r = r + (int) l;
	l = -i;
	r = r + (int) (l + 10);
	l = 4;
	l = l + m * 2;
	r = r + (int) l;
	l = l * m;
	r = r + (int) (l - 400);
	l = 3;
	l <<= 3;
	r = r + (int) l;
	l >>= 2;
	r = r + (int) l;
	l /= m;
	r = r + (int) l;
	l = 40;
	x = (int) (l * 2);
	r = r + x - 70;
	l = l - m - 1;
	r = r + (int) l;
	l = 100000 - l;
	r = r + (int) (l - 99970);
	return r;
}
