/*
 * c2rules.h - mutos_c2's switch -1 and its rules (docs/TOOLCHAIN_PLAN.md,
 * section 7; the list is docs/RULES.md).
 *
 * Each rule of c2 - an 80186/V30 code rule; c2 has no correction
 * (decision 13) and so no mode - is a file of its own in this directory,
 * named after the rule (V30-PUSH: v30_push.c), with one function
 * rule_<name>(), called from one line in the ported code: its hook.
 * Taking a rule out is deleting its file, its hook and its line in
 * docs/RULES.md; scripts/check_rules.py checks that the three agree.
 * c2rules.c holds what the rules share: the rule log.
 *
 * Included by c2port/c2.h, after the image and the nodes are declared.
 */
#ifndef MUTOS_C2RULES_H
#define MUTOS_C2RULES_H

#include <stdint.h>

#include "../../h/mutos_mode.h"

/* the first argument "-1": 80186/V30 code (decision 15; c2port/main.c) */
extern	int	c2_v30;
/* the bytes c2 has written to its standard output (c2lib.c's putc()) */
extern	long	c2_outpos;

/*
 * c2_rule - a rule's hook has been passed for the node: the original's
 * path taken where the rule is hooked ("met", applied 0), or the rule's
 * own (applied 1). The entry waits for output(): it goes to the rule log
 * (src/h/mutos_mode.h) with out= the bytes c2 writes for the node - none
 * when the node is not written (a later pass took it out of the list).
 * fmt, when not NULL, gives keys. Nothing is done when the rule log is
 * off.
 *
 * What an applied rule changes besides its node: c2 improves a chunk -
 * a function - as a whole, and its passes run again after the rule. A
 * node a rule made shorter can make a branch short that was long
 * (shortbr() measures by the nodes between a branch and its label), and
 * the passes that join common code and move blocks (comjump(),
 * codemove(), backjmp()) may decide otherwise for the changed nodes,
 * which moves code and turns branches around. So when the last node of a chunk is written,
 * each rule applied in it gets one more entry, "applied" with out= the
 * whole chunk and the key chunk=1: within that function c2's output may
 * differ anywhere, outside it nowhere.
 */
void	c2_rule(const char *rule, int applied, uint16_t node,
		const char *fmt, ...);

/*
 * c2_rule_node - output() is about to write the node (0: it has written
 * the last one of the chunk). The bytes written since the call before
 * are the node of that call's; the first call after a 0 starts a chunk.
 */
void	c2_rule_node(uint16_t node);

/* the bytes of a displacement in the operand at s (up to a comma or the end) */
int	c2_displen(uint16_t s);

/* ---- the rules ------------------------------------------------------ */

/* V30-PUSH (v30_push.c): "push" of a constant; 1 if pushopt() makes it */
int	rule_v30_push(uint16_t p);

/*
 * V30-IMUL (v30_imul.c): the bytes toofar() must add to what it counted
 * (base) for the "imul" node q when that is an "imul" by a constant
 */
int	rule_v30_imul(uint16_t q, int base);

/*
 * V30-SHIFT (v30_shift.c): the bytes toofar() must add to what it counted
 * (base) for the shift node q when that is a shift by a constant
 */
int	rule_v30_shift(uint16_t q, int base);

#endif /* MUTOS_C2RULES_H */
