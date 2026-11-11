/*
 * fltconst_test.c - driver for fltconst.c's flt_encode(), for checking
 * it against an independent model (tests/mutos_as/float_coverage/
 * fltmodel.py) without assembling a whole source per constant.
 *
 * Reads one constant per line from stdin: "f <text>" (.float) or
 * "d <text>" (.double), <text> exactly as it would follow the
 * directive. Writes one line per input:
 *
 *     OK xx xx xx xx [xx xx xx xx]   - the bytes, lowest address first
 *     SYNTAX | ROUNDING | RANGE | UNKNOWN
 *
 * With the argument -F, the conversion on the corrected copy of libc.a
 * (flt_encode_with(..., FLT_LIBC_FIXED): docs/RULES.md, L-05), as
 * mutos_as and mutos_c1 convert in the corrected mode.
 *
 * Exit status 0; 1 on a malformed input line or argument.
 */

#include <stdio.h>
#include <string.h>

#include "fltconst.h"

int main(int argc, char **argv)
{
    static char line[1 << 16];
    static const char *names[] = { "OK", "SYNTAX", "ROUNDING", "RANGE", "UNKNOWN" };
    FltLibc libc = FLT_LIBC_ORIGINAL;

    if (argc == 2 && strcmp(argv[1], "-F") == 0) {
        libc = FLT_LIBC_FIXED;
    } else if (argc != 1) {
        fprintf(stderr, "usage: fltconst_test [-F] <lines\n");
        return 1;
    }

    while (fgets(line, sizeof line, stdin)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        if (n < 2 || (line[0] != 'f' && line[0] != 'd') || line[1] != ' ') {
            fprintf(stderr, "fltconst_test: bad input line: %s\n", line);
            return 1;
        }
        FpKind kind = (line[0] == 'd') ? FP_DOUBLE : FP_FLOAT;
        unsigned char out[8];
        FltStatus st = flt_encode_with(kind, line + 2, n - 2, out, libc);
        if (st == FLT_OK) {
            fputs("OK", stdout);
            for (size_t i = 0; i < flt_size(kind); i++)
                printf(" %02x", out[i]);
            putchar('\n');
        } else {
            puts(names[st]);
        }
    }
    return 0;
}
