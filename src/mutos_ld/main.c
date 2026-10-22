/*
 * main.c - mutos_ld's entry: the image is set up as crt0 leaves it for
 * the original's main(), which then runs; it ends in delexit() - exit()
 * with the status ld has come to.
 */
#include "ld.h"

int
main(int argc, char **argv)
{
	uint16_t mutos_argv;

	ld_init(argc, argv, &mutos_argv);
	ld_exit(ldmain((int16_t)argc, mutos_argv));
}
