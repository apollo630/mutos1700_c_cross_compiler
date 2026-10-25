/*
 * main.c - mutos_c2's entry: the image is set up as crt0 leaves it for
 * the original's main(), which then runs; it ends in exit().
 */
#include "c2.h"

int
main(int argc, char **argv)
{
	uint16_t mutos_argv;

	c2_init(argc, argv, &mutos_argv);
	c2main((int16_t)argc, mutos_argv);
	c2_exit(0);
}
