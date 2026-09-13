/*
 * 09_abiprobe/07_frame300.c
 *
 * ABI research probe (see docs/DEVLOG.md's Milestone 4 "Open item"
 * and MUTOS_C_ABI.md sect. 1.9): a local frame of exactly 300 bytes.
 * Sibling files (080/128/176/224/256/300 bytes) bisect the
 * empirically-bounded (76, 256] gap where it is not yet known from
 * the existing libc corpus whether real cc still emits a plain
 * "sub sp,N" or has already switched to "call chkstk".
 */
main()
{
	char buf[300];

	buf[0] = 1;
	buf[300 - 1] = 2;
	return buf[0] + buf[300 - 1];
}
