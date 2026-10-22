/*
 * main.c - mutos_cpp's entry: the image is set up as crt0 leaves it for
 * the original's main(), which then runs; its return value is the exit
 * status, as crt0 passes it to exit().
 */
#include "cpp.h"

int
main(int argc, char **argv)
{
	uint16_t mutos_argv;

	cpp_init(argc, argv, &mutos_argv);
	cpp_exit(cppmain((int16_t)argc, mutos_argv));
}
