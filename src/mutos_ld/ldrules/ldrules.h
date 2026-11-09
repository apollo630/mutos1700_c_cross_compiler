/*
 * ldrules.h - mutos_ld's mode and its rules (docs/TOOLCHAIN_PLAN.md,
 * section 7; the list is docs/RULES.md).
 *
 * Each rule of ld - the correction of one bug of the original
 * (docs/MUTOS_ORIGINAL_BUGS.md) - is a file of its own in this directory,
 * named after the rule (LD-06: ld_06.c), with one function rule_<name>(),
 * called from one line in ld.c: its hook. Taking a rule out is deleting
 * its file, its hook and its line in docs/RULES.md; scripts/check_rules.py
 * checks that the three agree. ldrules.c holds the mode.
 */
#ifndef MUTOS_LDRULES_H
#define MUTOS_LDRULES_H

#include <stdint.h>

#include "../../h/mutos_mode.h"

/*
 * MUTOS_ORIGINAL or MUTOS_CORRECTED: -F or -H anywhere on the command line
 * (the last one counts), else MUTOS_MODE - ld_mode_init(), before the
 * original's main() reads its arguments one by one, so that the mode does
 * not depend on where the switch stands.
 */
extern	int	ld_mode;
void	ld_mode_init(int argc, char **argv);

/* ---- the rules (docs/RULES.md) --------------------------------------- */

/*
 * LD-06: error() sets errlev to the level of its message - returns the
 * new errlev (the corrected mode: never lower than the old one)
 */
int16_t	rule_ld_06(int16_t level, int16_t n);
/*
 * LD-08: middle() adds the six bytes of "jmp $" to the text - returns the
 * sum (the corrected mode: through add, ld.c's add(), which reports an
 * overflow)
 */
uint16_t rule_ld_08(uint16_t size, int16_t (*add)(int16_t, int16_t, uint16_t));
/*
 * LD-09: middle() rounds the text to 64 bytes for -i and -n - returns the
 * rounded size (the corrected mode: the sum through add, as LD-08's)
 */
uint16_t rule_ld_09(uint16_t size, int16_t (*add)(int16_t, int16_t, uint16_t));

#endif /* MUTOS_LDRULES_H */
