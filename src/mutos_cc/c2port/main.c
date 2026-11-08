/*
 * main.c - mutos_c2's entry: the image is set up as crt0 leaves it for
 * the original's main(), which then runs; it ends in exit(). EXTENSION: a
 * first argument "-1" (c2.h) is taken off before - the original would
 * read it as "-", its statistics.
 */
#include "c2.h"

#include <string.h>

int	c2_v30;

int
main(int argc, char **argv)
{
	uint16_t mutos_argv;

	if (argc > 1 && strcmp(argv[1], "-1") == 0) {
		c2_v30 = 1;
		argv[1] = argv[0];
		argc--;
		argv++;
	}
	c2_init(argc, argv, &mutos_argv);
	c2main((int16_t)argc, mutos_argv);
	c2_exit(0);
}
