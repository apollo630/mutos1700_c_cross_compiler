/*
 * c0_parser.h - mutos_c0 front-end driver entry point.
 */

#ifndef MUTOS_C0_PARSER_H
#define MUTOS_C0_PARSER_H

#include <stdio.h>

/*
 * Compiles the whole translation unit read from `in`, writing the
 * temp1/temp2 intermediate-code streams to `temp1`/`temp2`.
 * Returns 0 on success, nonzero if any error was reported (matching
 * mutos_c0's exit-status convention: exit(nerror != 0), per v7/cc/
 * c00.c's main()).
 */
int c0_compile(FILE *in, FILE *temp1, FILE *temp2);

#endif /* MUTOS_C0_PARSER_H */
