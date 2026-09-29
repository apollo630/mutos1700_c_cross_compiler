/*
 * fltdec_test.c - reads floating literals, one per line, and prints the
 * text and size mutos_c1 writes for each (c1_fltdec.c): "<text> float
 * <rendering>", "<text> double <rendering>", or "<text> SYNTAX"/"RANGE"/
 * "UNKNOWN". A line starting with '-' is the literal negated. For the
 * comparison with libc.a's own ecvt() - tests/mutos_as/float_coverage/
 * libcatof.py's "ecvt" subcommand.
 */
#include "c1_fltdec.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    char line[256];
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!line[0])
            continue;
        int neg = (line[0] == '-');
        char out[64];
        int is_float, is_zero;
        FdecStatus st = fdec_render(line + neg, neg, out, sizeof out,
                                    &is_float, &is_zero);
        if (st == FDEC_OK)
            printf("%s %s %s\n", line, is_float ? "float" : "double", out);
        else
            printf("%s %s\n", line, st == FDEC_SYNTAX ? "SYNTAX" :
                                    st == FDEC_RANGE ? "RANGE" : "UNKNOWN");
    }
    return 0;
}
