/*
 * LIBC-F - the corrected mode links the corrected copy of libc.a
 * (docs/RULES.md, "The corrected copy of libc.a"; docs/TOOLCHAIN_PLAN.md,
 * decision 14, step F3b).
 *
 * Hook: getfile() (ld.c), where -lX's name has been written as
 * "/usr/lib/libX.a" and the original opens "/lib/libX.a" (the address
 * name), then "/usr/lib/libX.a".
 *
 * The corrected mode: "/lib/fixed/libX.a" first - the MUTOS root's
 * lib/fixed/, where the build puts the corrected copy of libc.a
 * (src/libc_fixed/); the place was decided by the project owner on
 * 2026-11-09. Without such a file the original's lookup goes on. The
 * name the messages give stays the original's "/lib/libX.a" (the
 * copy's name is not in the original's image).
 *
 * Logged as "applied" (lib=<the MUTOS path>) where the copy is taken (in
 * each of ld's two passes, which both open the libraries);
 * the original mode logs nothing - its lookup is no bug.
 *
 * State: active.
 */
#include "ldrules.h"
#include "../ld.h"

#include <stdio.h>
#include <string.h>

int16_t
rule_libc_f(uint16_t name)
{
	char orig[64], fixed[80];
	size_t k = 0;
	int16_t fd;

	if (ld_mode != MUTOS_CORRECTED)
		return -1;
	while (rb(name) != 0 && k + 1 < sizeof(orig))
		orig[k++] = (char)UB(name++);
	orig[k] = '\0';
	if (strncmp(orig, "/lib/", 5) != 0)
		return -1;
	snprintf(fixed, sizeof(fixed), "/lib/fixed/%s", orig + 5);
	fd = sys_open_root(fixed);
	if (fd >= 0)
		mutos_rulelog("ld", "LIBC-F", "applied", "lib=%s", fixed);
	return fd;
}
