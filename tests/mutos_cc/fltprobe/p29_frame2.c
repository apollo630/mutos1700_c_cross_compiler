/*
 * fltprobe/p29_frame2.c
 *
 * No floating point: the frames round 7 left open. p27_frame showed 82
 * and 90 bytes allocated with "sub sp,N", 100 to 128 with "mov ax,N" /
 * "call chkstk"; mutos_c1 refuses every frame in between. One function
 * per even size from 92 to 98 (a frame is always even) - where is the
 * switch?
 * Result: 50.
 */
f92()
{
	char buf[92];

	buf[0] = 1;
	buf[91] = 2;
	return buf[0] + buf[91];
}

f94()
{
	char buf[94];

	buf[0] = 3;
	buf[93] = 4;
	return buf[0] + buf[93];
}

f96()
{
	char buf[96];

	buf[0] = 5;
	buf[95] = 6;
	return buf[0] + buf[95];
}

f98()
{
	char buf[98];

	buf[0] = 7;
	buf[97] = 22;
	return buf[0] + buf[97];
}

main()
{
	return f92() + f94() + f96() + f98();
}
