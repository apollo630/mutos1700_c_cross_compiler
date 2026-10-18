/*
 * c0lib.c - the machine and the library around the original c0: the
 * memory image (c0.h) and its initial state, the input, the heap
 * (sbrk()), the stack area for address-taken locals, libc.a's printf
 * for the error messages, exit() and the refusals (c0trap()).
 */
#include "c0.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

uint16_t c0mem[32768];
int	c0_bswap;

FILE	*c0_in;			/* the original's stdin (freopen()ed source) */
FILE	*c0_temp1, *c0_temp2;	/* its stdout and sbufp */

int	c0_members;
int	c0_stmtdepth;
int	c0_tree_stmtdepth;
int	c0_cp_funchead;

static	uint16_t c0_brk = C0_END;	/* the break */
static	uint16_t c0_sp = C0_STACKTOP;	/* the stack area's top */

/* the words crt0 sets at 2..9: Isgadr's and Iscadr's far pointers */
static	uint16_t crt0_words[4];

/*
 * The image at the start: the binary's initialized data at address 0,
 * the BSS zero (as the kernel hands it over), and what crt0 stores
 * before calling main(): the code segment into the far pointers' segment
 * words 4 and 8. Under tests/realbin/mutosemu.py the code segment is
 * 0x1000, so that is the value here - on the hardware it is wherever the
 * text was loaded; only a null tree's fields read it (conexp() after a
 * syntax error returns the word at 8).
 */
void
c0_init(void)
{
	uint16_t one = 1;
	unsigned i;

	c0_bswap = *(unsigned char *)&one == 0;
	memset(c0mem, 0, sizeof(c0mem));
	for (i = 0; i < C0_DSIZE; i++)
		((unsigned char *)c0mem)[i ^ c0_bswap] = c0_data[i];
	WU(4) = 0x1000;
	WU(8) = 0x1000;
	for (i = 0; i < 4; i++)
		crt0_words[i] = WU(2 + 2 * i);
}

/*
 * The original's words 2..9 are the far pointers its stack check and
 * system calls jump through: a write over them (through a null pointer)
 * makes its next function call jump somewhere else. Checked where the
 * port spends its time (gblock(), outcode()) - refused.
 */
void
c0_checkcrt0(void)
{
	unsigned i;

	for (i = 0; i < 4; i++)
		if (WU(2 + 2 * i) != crt0_words[i])
			c0trap("a write over crt0's far pointers (data 2..9) - the "
			    "original's next call jumps through them");
}

/*
 * A refusal: the original traps here, loops forever, or depends on state
 * the image does not model. What was written is flushed (a checker
 * compares it with what the original wrote), then exit status 2.
 */
void
c0trap(const char *what)
{
	if (c0_temp1)
		fflush(c0_temp1);
	if (c0_temp2)
		fflush(c0_temp2);
	fprintf(stderr, "%d: c0: %s - the original traps or depends on state "
	    "the port does not model here; refused\n", line, what);
	exit(2);
}

/*
 * getc(stdin): a byte 0..255, -1 at the end of the file (and at every
 * call after it, as libc.a's _filbuf() answers).
 */
int16_t
c0_getchar(void)
{
	int c = getc(c0_in);

	return c == EOF ? -1 : (int16_t)c;
}

/*
 * ctab[c]. c is a byte 0..255 or -1 (EOF); the original indexes the
 * 128-byte table with it whatever it is: -1 reads cvntab[15] (123,
 * LETTER), 128..135 libc.a's "(null)" string, 136..167 the FILE
 * structures of stdin, stdout, stderr and temp2 - their buffer pointers
 * and counts, which change as the original reads and writes; not
 * modelled, refused (c0_bp()) -, 168..255 FILEs nobody opens (zero).
 */
int16_t
ctab(int16_t c)
{
	return B(CTAB + c);
}

/*
 * An access to what the image does not model (c0.h, c0_unmodelled()).
 */
void
c0trap_region(uint16_t a)
{
	char what[160];

	snprintf(what, sizeof(what), "an access to %04x, %s", a,
	    a < 0x0ab4 ? "libc.a's FILE structures (an input byte 0210..0247 "
		"indexing ctab[], or a wild pointer)" :
	    a < 0x3c56 ? "temp2's stdio buffer" :
	    a < 0x40be ? "stdout's or stdin's stdio buffer (a number longer "
		"than numbuf, or a wild pointer)" :
	    "libc.a's own data");
	c0trap(what);
}

/*
 * libc.a's sbrk(): the old break, the break moved by n. The kernel's
 * limit is the stack; the port refuses well before the image's stack
 * area (no input in the corpus needs more than 2K of the heap).
 */
int16_t
c0_sbrk(int16_t n)
{
	uint16_t old = c0_brk;

	if ((uint32_t)c0_brk + (uint16_t)n > C0_HEAPMAX)
		c0trap("the heap would pass 0xc000 (where the original depends "
		    "on the kernel's stack placement)");
	c0_brk = (uint16_t)(c0_brk + n);
	return (int16_t)old;
}

/*
 * The stack area: the original's frames hold locals whose address it
 * passes on (typer, mems[], dim, the expression stack cmst[], funchead()'s
 * bstack); here they get n bytes below the previous ones, freed in the
 * reverse order. Their contents start as whatever the area held - as the
 * original's do; nothing the port writes depends on it.
 */
uint16_t
salloc(uint16_t n)
{
	n = (uint16_t)((n + 1) & ~1);
	if (c0_sp - n < C0_STACKMIN)
		c0trap("nesting too deep for the image's stack area");
	c0_sp = (uint16_t)(c0_sp - n);
	return c0_sp;
}

void
sfree(uint16_t n)
{
	c0_sp = (uint16_t)(c0_sp + ((n + 1) & ~1));
}

/*
 * tree()'s operand stack cmst[CMSIZ] for the next nesting depth
 * (d = 1, on entry) or back to the previous depth (d = -1, on return).
 */
uint16_t
tree_cmst(int d)
{
	static int depth;

	if (d < 0) {
		depth--;
		return 0;
	}
	if ((depth + 1) * CMSIZ * 2 > C0_STACKMIN - C0_HEAPMAX)
		c0trap("expressions nested too deep for the image's tree() stacks");
	return (uint16_t)(C0_HEAPMAX + depth++ * CMSIZ * 2);
}

/*
 * exit(): stdio's buffers flushed (temp1, temp2), then the status.
 */
void
c0_exit(int status)
{
	if (c0_temp1 && fclose(c0_temp1) != 0)
		status = 1;
	if (c0_temp2 && fclose(c0_temp2) != 0)
		status = 1;
	c0_temp1 = c0_temp2 = NULL;
	exit(status);
}

/*
 * libc.a's printf (text 4b98 on) for the formats c0's messages use: %d
 * (a signed word), %c (its low byte; a 0 writes nothing), %s and %.Ns (a
 * string in the image, at most N bytes; a null pointer is "(null)", and
 * a precision of 0 counts as none). The format itself is a host string -
 * the same text as the binary's - and the arguments are the words the
 * original pushes.
 */
void
c0_vfprintf(FILE *f, const char *fmt, const uint16_t *args)
{
	const char *p;
	uint16_t s;
	int prec;

	for (p = fmt; *p; p++) {
		if (*p != '%') {
			putc(*p, f);
			continue;
		}
		p++;
		prec = 0;
		if (*p == '.') {
			p++;
			while (*p >= '0' && *p <= '9')
				prec = prec * 10 + *p++ - '0';
		}
		switch (*p) {
		case 'd':
			fprintf(f, "%d", (int16_t)*args++);
			break;
		case 'c':
			if ((*args & 0377) != 0)
				putc(*args & 0377, f);
			args++;
			break;
		case 's':
			s = *args++;
			if (s == 0) {
				fputs("(null)", f);
				break;
			}
			while (UB(s) != 0) {
				putc(UB(s), f);
				s++;
				if (--prec == 0)
					break;
			}
			break;
		default:
			c0trap("a printf format the port's printf does not have");
		}
	}
}
