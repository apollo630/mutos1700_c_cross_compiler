/*
 * main.c - mutos_cc's entry: the image is set up as crt0 and main()'s
 * prologue leave it (cc_init()), then the original's main() runs; it
 * ends in dexit() - exit() with the status cc has come to.
 */
#include "cc.h"

int
main(int argc, char **argv)
{
	cc_init(argc, argv);
	ccmain();
	return 0;
}
