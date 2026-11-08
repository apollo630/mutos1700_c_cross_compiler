/*
 * ldrules.c - what mutos_ld's rules share (ldrules.h): the mode.
 */
#include "ldrules.h"

#include <string.h>

int	ld_mode = MUTOS_MODE_DEFAULT;

/*
 * The options are read as the original's main() reads them (ld.c): an
 * argument starting with '-' is a group of letters; -o, -u, -e, -v and -D
 * take the next argument, -l the rest of its own. -F and -H are found
 * wherever they stand; without them, MUTOS_MODE counts ("ld:
 * MUTOS_MODE=...", exit status 1, for a bad value).
 */
void
ld_mode_init(int argc, char **argv)
{
	int i, mode = -1;
	const char *p;

	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			continue;
		for (p = argv[i] + 1; *p != '\0'; p++) {
			if (*p == 'l')
				break;
			if (strchr("oueDv", *p) != NULL && i + 1 < argc) {
				i++;
				continue;
			}
			if (mutos_mode_letter(*p) >= 0)
				mode = mutos_mode_letter(*p);
		}
	}
	ld_mode = mode >= 0 ? mode : mutos_mode_init("ld");
}
