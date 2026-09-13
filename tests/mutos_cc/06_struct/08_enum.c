/*
 * 06_struct/08_enum.c
 *
 * "enum" is a genuine keyword in this V7 cc snapshot (see ENUM /
 * ENUMTAG / ENUMCON in v7/cc/c0.h and their handling in c03.c) --
 * unlike "void", which this compiler has no concept of at all.
 */
enum color { RED, GREEN, BLUE };

main()
{
	enum color c;

	c = GREEN;
	return (int) c;
}
