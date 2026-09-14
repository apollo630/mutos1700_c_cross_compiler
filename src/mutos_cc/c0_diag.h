/*
 * c0_diag.h - error/warning reporting for mutos_c0.
 */

#ifndef MUTOS_C0_DIAG_H
#define MUTOS_C0_DIAG_H

extern const char *c0_diag_filename;
extern int c0_diag_nerrors;

void c0_error_at(int line, const char *fmt, ...);
void c0_warn_at(int line, const char *fmt, ...);

#endif /* MUTOS_C0_DIAG_H */
