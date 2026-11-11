/*
 * L-04 - aldiv, ldiv, alrem and lrem are not C's division
 * (docs/MUTOS_ORIGINAL_BUGS.md, L-04; docs/RULES.md).
 *
 * Hook: lconst() (c1port/c12.c), once it has folded a long constant
 * expression: for / and % with libc.a's aldiv and alrem, re-enacted on
 * their 16-bit words (c1_aldiv(), c1_alrem() in c1port/c1lib.c), whose
 * quotient is wrong for a divisor from 98304 to 131071, for a negative
 * divisor of one word once the quotient reaches 65536, and for -2**31.
 *
 * The correction (the corrected mode): the result of the corrected copy
 * of libc.a's routines (src/libc_fixed/fixed/aldiv.s, ldiv.s, alrem.s,
 * lrem.s) - C's quotient, truncated toward 0, and C's remainder, with the
 * sign of the dividend, for every divisor; -2**31 / -1, which C leaves
 * undefined, is -2**31 there (the quotient 2**31 wraps) and here -, so
 * that c1 folds the value the program's ldiv and lrem give at run time.
 * A divisor of 0 is "Divide check", as before.
 *
 * The rule log: a division whose result the two give differently - "met"
 * in the original mode, "applied" in the corrected mode (op=/ or %, the
 * operands l= and r=).
 *
 * State: active.
 */
#include "../c1port/c1.h"

/* the left operand as lconst() reads it */
static int32_t
dividend(struct tnode *lp)
{
	if (lp->op == LCON)
		return lvalue(lp);
	if (lp->op == ITOL)
		lp = lp->tr1;
	return lp->type == INT ? lp->value : (int32_t)(uint16_t)lp->value;
}

int32_t
rule_l_04(int op, struct tnode *lp, int32_t r, int32_t l)
{
	int32_t a, c;
	int64_t q;
	int rem;

	switch (op) {
	case DIVIDE:
	case LDIV:
		rem = 0;
		break;
	case MOD:
	case LMOD:
		rem = 1;
		break;
	default:
		return l;
	}
	if (r == 0)		/* "Divide check": nothing folded */
		return l;
	a = dividend(lp);
	q = (int64_t)a / r;	/* truncated toward 0 */
	if (rem)
		c = (int32_t)(a - q * r);
	else
		c = (int32_t)(uint32_t)(uint64_t)q;	/* -2**31 / -1: -2**31 */
	if (c == l)
		return l;
	c1_rule("L-04", c1_mode == MUTOS_CORRECTED, NULL, NULL, "op=%s l=%ld r=%ld",
	    rem ? "%" : "/", (long)a, (long)r);
	return c1_mode == MUTOS_CORRECTED ? c : l;
}
