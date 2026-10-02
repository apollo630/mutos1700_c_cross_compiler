/*
 * fltprobe/p11_itof2.c
 *
 * Ints converted to floating where only an inference says how: computed
 * in DI as any int is, then "mov ax,di" (p7_itofreg has a sum only) - a
 * shift, a difference with a constant, a negation by subtraction; a
 * quotient, in AX already ("cwd" / "idiv" / "call itof"?); after a
 * computed '/', a product by a constant ("mov ax,i" / "mov cx,*3." /
 * "imul cx"?); and "i += f()", an int call's result added in place like
 * an "ftoi" result ("add <i>,ax" - p8_misc's "i += d"). mutos_c1
 * compiles these by inference.
 * Result: 109.
 */
seven()
{
	return 7;
}

main()
{
	double d, e;
	int i, j, r;

	i = 13;
	j = 4;
	r = 0;
	e = 2.0;
	d = 0.5 + (i << 2);
	r = r + (int) d;
	d = e + (i - 6);
	r = r + (int) d;
	d = e + (0 - i);
	r = r + (int) d;
	d = e + i / j;
	r = r + (int) d;
	d = (d / e) + i * 3;
	r = r + (int) d;
	i += seven();
	i -= seven();
	return r + i;
}
