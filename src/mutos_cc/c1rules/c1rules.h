/*
 * c1rules.h - mutos_c1's mode, its switch -1 and its rules
 * (docs/TOOLCHAIN_PLAN.md, section 7; the list is docs/RULES.md).
 *
 * Each rule of c1 - the correction of one bug of the original
 * (docs/MUTOS_ORIGINAL_BUGS.md) or one 80186/V30 code rule - is a file of
 * its own in this directory, named after the rule (C1-03: c1_03.c), with
 * one function rule_<name>(), called from one line in the ported code: its
 * hook. Taking a rule out is deleting its file, its hook and its line in
 * docs/RULES.md; scripts/check_rules.py checks that the three agree.
 * c1rules.c holds what the rules share: the mode, -1, and the rule log.
 *
 * Included by c1port/c1.h, after the trees and the tables are declared.
 */
#ifndef MUTOS_C1RULES_H
#define MUTOS_C1RULES_H

#include <stddef.h>

#include "../../h/mutos_mode.h"

/* MUTOS_ORIGINAL or MUTOS_CORRECTED (MUTOS_MODE, set by cc's -F, -H) */
extern	int	c1_mode;
/* the argument "-1": 80186/V30 code (decision 15) */
extern	int	c1_v30;
/* the file c1 writes (its third argument), read back by c1_rule_mark() */
extern	const char *c1_outname;

/*
 * c1_rule - a rule's hook has been passed: the original's path taken
 * where the rule is hooked ("met", applied 0), or the rule's own
 * (applied 1). The entry is noted with the source line now (line) and
 * goes to the rule log (src/h/mutos_mode.h) at the next c1_rule_mark(),
 * with out= the bytes c1 wrote for the intermediate-code operation that
 * was being compiled: the whole statement, so that every byte a rule
 * changes lies in its range. check, when not NULL, is asked then whether
 * the entry counts (0: it is dropped) - with ctx, the rule's own pointer
 * (a tree node, say, whose fate the rule must see once the statement is
 * compiled), and with those bytes as text (NULL when they cannot be read
 * back); it may write keys of its own into keys (nkeys bytes). fmt, when
 * not NULL, gives keys at once. Nothing is done when the rule log is off.
 */
typedef	int	c1_rule_check(const void *ctx, const char *text, size_t n,
		char *keys, size_t nkeys);
void	c1_rule(const char *rule, int applied, c1_rule_check *check,
		const void *ctx, const char *fmt, ...);

/*
 * c1_rule_mark - at the top of getree()'s loop and at its EOFC: the
 * operation compiled since the last mark is done; its entries go to the
 * log, and the next operation's output starts here.
 */
void	c1_rule_mark(void);

/* ---- the rules (docs/RULES.md) --------------------------------------- */

struct	tnode;
struct	table;

/* C1-01: cexpr()'s template, once matched (c10.c) - returns the template */
char	*rule_c1_01(struct tnode *tree, struct table *table, char *tabstring);
/* C1-02: acommute()'s product or AND with 0 (c12.c) - returns the tree */
struct	tnode *rule_c1_02(struct tnode *zero, int op, struct tnode **ops, int n);
/* C1-03: delay() before the tree without its postfix ++/-- (c10.c) */
void	rule_c1_03(struct tnode *incdec);
/* C1-06: cexpr()'s template, once matched (c10.c) - returns the template */
char	*rule_c1_06(char *tabstring);

#endif /* MUTOS_C1RULES_H */
