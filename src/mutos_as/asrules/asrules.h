/*
 * asrules.h - mutos_as's mode and its rules (docs/TOOLCHAIN_PLAN.md,
 * section 7; the list is docs/RULES.md).
 *
 * Each rule of as - the correction of one bug of the original
 * (docs/MUTOS_ORIGINAL_BUGS.md) - is a file of its own in this directory,
 * named after the rule (L-05: l_05.c), with one function rule_<name>(),
 * called from one line in assemble.c: its hook. Taking a rule out is
 * deleting its file, its hook and its line in docs/RULES.md;
 * scripts/check_rules.py checks that the three agree. asrules.c holds the
 * mode.
 */
#ifndef MUTOS_ASRULES_H
#define MUTOS_ASRULES_H

#include <stddef.h>

#include "../fltconst.h"
#include "../../h/mutos_mode.h"

/*
 * MUTOS_ORIGINAL or MUTOS_CORRECTED: -F or -H (the last one given counts),
 * else MUTOS_MODE - set by assemble.c's main() before the source is read.
 */
extern	int	as_mode;

/* ---- the rules (docs/RULES.md) --------------------------------------- */

/*
 * L-05: the operand text[0..len) of a .float or .double converted -
 * flt_encode()'s result, on libc.a's runtime in the original mode and on
 * the corrected copy's in the corrected mode; logged (in Pass 2, report
 * set; line the source line) where the two differ
 */
FltStatus rule_l_05(FpKind kind, const char *text, size_t len, unsigned char *out,
		    int report, int line);

#endif /* MUTOS_ASRULES_H */
