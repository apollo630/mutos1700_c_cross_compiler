/*
 * 04_funcs/02_manyargs.c
 *
 * A function with six integer parameters, to probe parameter offsets
 * past bp+0xa (MUTOS_C_ABI.md sect. 1.3 only directly confirms
 * offsets up to the 4th parameter from real libc code -- this file
 * probes the 5th and 6th).
 */
sum6(a, b, c, d, e, f)
int a, b, c, d, e, f;
{
	return a + b + c + d + e + f;
}

main()
{
	return sum6(1, 2, 3, 4, 5, 6);
}
