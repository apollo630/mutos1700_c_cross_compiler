/*
 * 00_smoke/03_retexpr.c
 *
 * Return the value of a simple constant expression, forcing the
 * expression evaluator to fold/emit at least one arithmetic op
 * before the final "mov ax,<value>" / jmp cret.
 */
main()
{
	return 6 * 7;
}
