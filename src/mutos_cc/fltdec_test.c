/*
 * fltdec_test.c - reads floating literals, one per line, and prints the
 * text and size mutos_c1 writes for each (c1_fltdec.c): "<text> float
 * <rendering>", "<text> double <rendering>", or "<text> SYNTAX"/"RANGE"/
 * "UNKNOWN". A line starting with '-' is the literal negated. With -f,
 * the text of a 'float' variable's initializer instead (fdec_render_
 * single(): the value truncated to a float) - always "float". For the
 * comparison with libc.a's own ecvt() - tests/mutos_as/float_coverage/
 * libcatof.py's "ecvt" and "fecvt" subcommands.
 */
#include "c1_fltdec.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    int single = (argc > 1 && strcmp(argv[1], "-f") == 0);
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0])
            continue;
        int neg = (line[0] == '-');
        char out[64];
        int is_float = 1, is_zero;
        FdecStatus st = single
            ? fdec_render_single(line + neg, neg, out, sizeof out, &is_zero)
            : fdec_render(line + neg, neg, out, sizeof out, &is_float,
                          &is_zero);
        if (st == FDEC_OK)
            printf("%s %s %s\n", line, is_float ? "float" : "double", out);
        else
            printf("%s %s\n", line, st == FDEC_SYNTAX ? "SYNTAX" :
                                    st == FDEC_RANGE ? "RANGE" : "UNKNOWN");
    }
    return 0;
}
