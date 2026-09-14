/*
 * c0_main.c - mutos_c0 CLI entry point.
 *
 * Invocation matches v7/cc/cc.c's own driver call shape (see that
 * file's `av[0]="c0"; av[1]=<cpp output>; av[2]=tmp1; av[3]=tmp2;`):
 *
 *   mutos_c0 <source> <temp1> <temp2> [-P]
 *
 * <source> is normally mutos_cpp's output (already macro-expanded/
 * comment-stripped), not the raw .c file - see tests/mutos_cc/
 * README.md's documented pipeline (`cpp -P n.c > n.i` then
 * `/lib/c0 n.i n.1 n.2`). The lexer also tolerates a raw .c file
 * directly (it skips comments/whitespace itself), which is useful
 * for quick manual testing.
 *
 * The trailing "-P" (profiling flag) is accepted for CLI-shape
 * compatibility with the real driver but is not yet acted upon -
 * mutos_c0's current grammar coverage has no function bodies where
 * profiling instrumentation would matter yet.
 */

#include <stdio.h>
#include <stdlib.h>

#include "c0_diag.h"
#include "c0_parser.h"

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s source temp1 temp2 [-P]\n", prog);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    const char *source_path = argv[1];
    const char *temp1_path = argv[2];
    const char *temp2_path = argv[3];

    FILE *in = fopen(source_path, "r");
    if (!in) {
        fprintf(stderr, "%s: cannot open %s\n", argv[0], source_path);
        return 1;
    }

    FILE *temp1 = fopen(temp1_path, "wb");
    if (!temp1) {
        fprintf(stderr, "%s: cannot create %s\n", argv[0], temp1_path);
        fclose(in);
        return 1;
    }

    FILE *temp2 = fopen(temp2_path, "wb");
    if (!temp2) {
        fprintf(stderr, "%s: cannot create %s\n", argv[0], temp2_path);
        fclose(in);
        fclose(temp1);
        return 1;
    }

    c0_diag_filename = source_path;

    int had_errors = c0_compile(in, temp1, temp2);

    fclose(in);
    fclose(temp1);
    fclose(temp2);

    return had_errors ? 1 : 0;
}
