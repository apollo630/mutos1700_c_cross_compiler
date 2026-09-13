/*
 * 06_struct/07_bitfield.c
 *
 * Struct bit-fields (v7/cc/c0.h models these via "struct field",
 * flen and bitoffs -- see the FSEL handling in v7/cc/c04.c's
 * treeout()).
 */
struct flags {
	unsigned ready : 1;
	unsigned error : 1;
	unsigned mode  : 2;
	unsigned count : 4;
};

main()
{
	struct flags f;

	f.ready = 1;
	f.error = 0;
	f.mode = 2;
	f.count = 9;
	return f.ready + f.mode + f.count;
}
