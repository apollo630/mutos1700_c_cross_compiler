/*
 * 07_scope/02_shadow.c
 *
 * An inner-block "auto" declaration shadows the outer one --
 * exercises the block-level pushed-down namelist (struct phshtab /
 * hblklev in v7/cc/c0.h).
 */
main()
{
	int x;

	x = 1;
	{
		int x;

		x = 2;
	}
	return x;
}
