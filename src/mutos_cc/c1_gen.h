/*
 * c1_gen.h - mutos_c1 code generator entry point.
 */

#ifndef MUTOS_C1_GEN_H
#define MUTOS_C1_GEN_H

#include <stdio.h>

/* Reads the full temp1/temp2 stream and writes mutos_as-syntax
 * assembly text to `out`. Returns 0 on success. */
int c1_generate(FILE *temp1, FILE *temp2, FILE *out);

#endif /* MUTOS_C1_GEN_H */
