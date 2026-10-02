/*
 * fltprobe/p14_axint.c
 *
 * Int shapes mutos_c1 compiles by inference since the round-3 goldens
 * (no floating point - kept with the probes whose goldens raised them): a
 * value in AX plus a constant stored to memory ("s = x / y + 3" - "add
 * ax,*3." / "mov s,ax"?, as p13_open's return value "r + (int) d - 30"
 * stayed in AX), "+ 1" and "- 1" on it ("inc ax" / "dec ax"?), an int "+
 * 0" and "- 0" (tossed by v7's acommute() - "mov di,j"?), a product of
 * two elements of int arrays subscripted by a variable (the left one
 * loaded first, as p10_elem's struct members - not spilled?), "&" of two
 * (the left loaded when?), "x /= f()" and "y %= f()" ("mov cx,ax" before
 * the target is loaded, as p13_open's "j /= e"?).
 * Result: 129.
 */
seven()
{
	return 7;
}

main()
{
	int a[3], b[3];
	int i, j, x, y, s, r;

	i = 1;
	j = 2;
	x = 17;
	y = 5;
	a[1] = 6;
	b[2] = 3;
	r = 0;
	s = x / y + 3;
	r = r + s;
	s = seven() + 1;
	r = r + s;
	s = x * y - 1;
	r = r + s;
	s = j + 0;
	r = r + s;
	s = j - 0;
	r = r + s;
	s = a[i] * b[j];
	r = r + s;
	s = a[i] & b[j];
	r = r + s;
	x /= seven();
	y %= seven();
	return r + x + y;
}
