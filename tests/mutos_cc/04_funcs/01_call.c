/*
 * 04_funcs/01_call.c
 *
 * Old-style (K&R) function definition and a simple two-argument
 * call.  Exercises MUTOS_C_ABI.md sect. 1.1 (right-to-left push,
 * caller cleanup) and sect. 1.3 (bp+4 / bp+6 parameter offsets).
 */
add(a, b)
int a, b;
{
	return a + b;
}

main()
{
	return add(3, 4);
}
