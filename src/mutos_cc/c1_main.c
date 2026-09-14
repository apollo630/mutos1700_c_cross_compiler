/*
 * c1_main.c - mutos_c1 CLI entry point.
 *
 * Invocation matches v7/cc/cc.c's own driver call shape (see that
 * file: `av[0]="c1"; av[1]=tmp1; av[2]=tmp2; av[3]=<assembly.s>`):
 *
 *   mutos_c1 <temp1> <temp2> <output.s>
 */

#include <stdio.h>

#include "c1_gen.h"

static void usage(const char *prog)
{
    fprintf(stderr, "usage: %s temp1 temp2 output.s\n", prog);
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        usage(argv[0]);
        return 1;
    }

    FILE *temp1 = fopen(argv[1], "rb");
    if (!temp1) {
        fprintf(stderr, "%s: cannot open %s\n", argv[0], argv[1]);
        return 1;
    }
    FILE *temp2 = fopen(argv[2], "rb");
    if (!temp2) {
        fprintf(stderr, "%s: cannot open %s\n", argv[0], argv[2]);
        fclose(temp1);
        return 1;
    }
    FILE *out = fopen(argv[3], "w");
    if (!out) {
        fprintf(stderr, "%s: cannot create %s\n", argv[0], argv[3]);
        fclose(temp1);
        fclose(temp2);
        return 1;
    }

    int rc = c1_generate(temp1, temp2, out);

    fclose(temp1);
    fclose(temp2);
    fclose(out);

    return rc;
}
