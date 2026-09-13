/*
 * 09_abiprobe/06_frame256.c
 *
 * ABI research probe (see docs/DEVLOG.md's Milestone 4 "Open item"
 * and MUTOS_C_ABI.md sect. 1.9): a local frame of exactly 256 bytes.
 * Sibling files (080/128/176/224/256/300 bytes) bisect the
 * empirically-bounded (76, 256] gap where it is not yet known from
 * the existing libc corpus whether real cc still emits a plain
 * "sub sp,N" or has already switched to "call chkstk".
 */
main()
{
	char buf[256];

	buf[0] = 1;
	buf[256 - 1] = 2;
	return buf[0] + buf[256 - 1];
}
