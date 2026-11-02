/*
 * size.c - mutos_size, the MUTOS 1700 segment sizes: a port of the original
 * size.
 *
 * v7's size.c (v7/size/size.c) as the original binary has it - its one
 * function, main(), at text 00b6 (tests/realbin/sizemap.txt). Read
 * instruction by instruction beside v7's source, it does what v7's does;
 * what differs is the representation (the a.out header as bytes, the
 * words 16 bits wide, the long a 32-bit value) and the libc.a underneath.
 *
 * What size does: for every file named on the command line (a.out when
 * there is none) it reads the 16 bytes of an a.out header, and if the magic
 * number is 0407, 0410, 0411 or 0405 prints
 *
 *	[name: ]text+data+bss = <sum>b = 0<sum in octal>b
 *
 * the name only when more than one file is given (v7 counts "gorp" as
 * argc, a.out's included). A file that cannot be opened ("size: x not
 * found") or has another magic number ("size: x not an object file") is
 * reported on standard output - size has no standard error - and goes on
 * with the next one.
 *
 * What the port keeps of the original, where it is odd:
 *  - the header buffer is a local of main(): a short read (a file of less
 *    than 16 bytes, an empty one) leaves the rest of the bytes of the file
 *    before it, and the first file finds zeros;
 *  - main() has no exit(): the value left in AX is the exit status. After
 *    a file that was read it is fclose()'s, 0; after a file that was not
 *    found it is printf()'s - 0, or -1 (status 255) once standard output
 *    has failed to take a block (the error flag of libc.a's stdio, see
 *    ../h/mutos_stdio.h);
 *  - the sizes are 16-bit unsigned numbers, the sum a long: up to 196605.
 *
 * What the port refuses (mu_trap(): exit status 125, the last line of the
 * messages ending in "refused"), where the original depends on state that
 * the port does not model:
 *  - a file name that is a directory (the original reads its entries).
 */
#define _POSIX_C_SOURCE 200809L	/* fileno() */

#define MU_TOOL "size"
#include "../h/mutos_stdio.h"

#include <stdint.h>
#include <sys/stat.h>

#define EXEC	16		/* sizeof(struct exec) */
#define A_MAGIC1	0407
#define A_MAGIC2	0410
#define A_MAGIC3	0411
#define A_MAGIC4	0405

/* the header's words, little-endian */
static unsigned
w16(const uint8_t *p)
{
	return (unsigned)p[0] | ((unsigned)p[1] << 8);
}

/* a_magic[] of the original, in its data at 11bc */
static const unsigned a_magic[] = { A_MAGIC1, A_MAGIC2, A_MAGIC3, A_MAGIC4, 0 };

/* fopen(name, "r"): a directory opens in the original and reads its entries */
static FILE *
sz_fopen(const char *name)
{
	FILE *f = fopen(name, "r");
	struct stat st;

	if (f != NULL && fstat(fileno(f), &st) == 0 && S_ISDIR(st.st_mode))
		mu_trap("a directory named as a file - the original reads its entries");
	return f;
}

/* main (text 00b6) */
int
main(int argc, char **argv)
{
	static uint8_t buf[EXEC];	/* struct exec buf: main()'s frame, zero at the start */
	int32_t sum;
	int gorp, i, n;
	int ax = 0;			/* what the original leaves in AX */
	FILE *f;
	static char aout[] = "a.out";
	char *dflt[1];
	char **names = argv + 1;

	n = argc - 1;
	gorp = argc;
	if (argc == 1) {
		dflt[0] = aout;		/* *argv = "a.out"; argc++; --argv */
		names = dflt;
		n = 1;
		gorp = 2;
	}
	for (; n > 0; n--, names++) {
		if ((f = sz_fopen(*names)) == NULL) {
			ax = mu_printf("size: %s not found\n", *names);
			continue;
		}
		{
			size_t r = fread(buf, 1, EXEC, f);

			(void)r;	/* a short read leaves the old bytes */
		}
		for (i = 0; a_magic[i]; i++)
			if (a_magic[i] == w16(buf))
				break;
		if (a_magic[i] == 0) {
			mu_printf("size: %s not an object file\n", *names);
			fclose(f);
			ax = 0;		/* fclose() of a stream that was read */
			continue;
		}
		if (gorp > 2)
			mu_printf("%s: ", *names);
		mu_printf("%u+%u+%u = ", w16(buf + 2), w16(buf + 4), w16(buf + 6));
		sum = (int32_t)((uint32_t)w16(buf + 2) + w16(buf + 4) + w16(buf + 6));
		mu_printf("%ldb = 0%lob\n", (long)sum, (long)sum);
		fclose(f);
		ax = 0;
	}
	mu_exit(ax);
}
