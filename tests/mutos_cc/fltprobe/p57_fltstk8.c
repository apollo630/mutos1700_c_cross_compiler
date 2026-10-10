/*
 * fltprobe/p57_fltstk8.c
 *
 * NOT a program to run, like p28_fltstk, p37_fltstk3, p41_fltstk4,
 * p45_fltstk5, p49_fltstk6 and p53_fltstk7: the real c1's model of the
 * floating-point stack after round 13. p52 showed its top - a push beyond
 * six values reports "Floating point stack overflow; simplify expression"
 * (the values "fstd" leaves piling up, the depth still counted past six) -
 * and p53 that the store of a hoisted "++w" below the bottom reports in
 * UPPER case, where an assignment's store reports in lower case. mutos_c1
 * infers that every compound assignment's and increment's store does,
 * hoisted or not, and that every push is checked at the top. f1 leaves the
 * model one below the bottom with no message (p28's "half(d = 3.0);").
 * f2's statements each start there: "d = 1.0;" (lower case), "d += 1.0;",
 * "d++;", "--d;", "e *= 2.0;" and "q->x += 1.0;" (upper case?), "e = d++;"
 * (the old value under the new one - "fdup" - so only the assignment's
 * store goes below: lower case?), "if ((d += w + y) > 2.0)" (the hoisted
 * "+=" popped, upper case?, then "fcmp", upper case - reported on the line
 * after the ')'). f3 raises the model to its top: seven "x = (d += w + y)
 * > 2.0;", each leaving one value (from one below the bottom to six - the
 * sixth reports once, the seventh twice), then at six "e = d++;" ("fldd"
 * and "fdup" beyond the top - two messages?) and "e = half(d) + 1.0;" (the
 * argument's push and the call's value - two?). Every function starts
 * where the one before left the model (p28: it is never reset) - please
 * bring back the messages ("round14.log") with the files.
 */
struct pt {
	double x, y;
};

double half(x)
double x;
{
	return x / 2.0;
}

f1()
{
	double d;

	half(d = 3.0);
	return 1;
}

f2()
{
	struct pt s, *q;
	double d, e, w, y;
	int x;

	q = &s;
	d = 1.0;
	d += 1.0;
	d++;
	--d;
	e = 2.0;
	e *= 2.0;
	s.x = 1.0;
	q->x += 1.0;
	e = d++;
	w = 4.0;
	y = 10.0;
	x = 0;
	if ((d += w + y) > 2.0)
		x = 1;
	return x;
}

f3()
{
	double d, e, w, y;
	int x;

	d = 1.5;
	w = 4.0;
	y = 10.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	x = (d += w + y) > 2.0;
	e = d++;
	e = half(d) + 1.0;
	return x;
}

main()
{
	return f1() + f2() + f3();
}
