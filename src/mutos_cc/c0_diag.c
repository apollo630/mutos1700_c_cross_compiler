/*
 * c0_diag.c - error/warning reporting for mutos_c0.
 */

#include <stdarg.h>
#include <stdio.h>

#include "c0_diag.h"

const char *c0_diag_filename = "<stdin>";
int c0_diag_nerrors = 0;

void c0_error_at(int line, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s:%d: error: ", c0_diag_filename, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    c0_diag_nerrors++;
}

void c0_warn_at(int line, const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "%s:%d: warning: ", c0_diag_filename, line);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
}
