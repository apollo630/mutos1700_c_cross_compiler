/*
 * 03_ctrlflow/06_switch.c
 *
 * switch/case with an explicit default and a deliberate fallthrough,
 * exercising the SWIT intermediate-code opcode and pswitch()'s jump
 * table generation (v7/cc/c11.c).
 */
main()
{
	int x, r;

	x = 2;
	r = 0;
	switch (x) {
	case 1:
		r = 10;
		break;
	case 2:
	case 3:
		r = 20;
		/* fall through */
	case 4:
		r = r + 1;
		break;
	default:
		r = -1;
		break;
	}
	return r;
}
