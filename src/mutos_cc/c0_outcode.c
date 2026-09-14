/*
 * c0_outcode.c - temp1/temp2 tagged intermediate-code stream writer.
 * See c0_outcode.h for the wire format.
 */

#include <stdarg.h>

#include "mutos_cc.h"
#include "c0_outcode.h"

void outcode(FILE *dst, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    for (const char *s = fmt; *s; s++) {
        switch (*s) {
        case 'B': {
            int v = va_arg(ap, int);
            putc(v & 0xFF, dst);
            putc(0xFE, dst);
            break;
        }
        case 'N': {
            int v = va_arg(ap, int);
            putc(v & 0xFF, dst);
            putc((v >> 8) & 0xFF, dst);
            break;
        }
        case 'S': {
            const char *name = va_arg(ap, const char *);
            if (name[0] != '\0')
                putc('_', dst);
            int n = MCC_NCPS;
            const char *np = name;
            while (n-- && *np)
                putc((*np++) & 0x7F, dst);
            putc(0, dst);
            break;
        }
        case '1':
            putc(1, dst);
            putc(0, dst);
            break;
        case '0':
            putc(0, dst);
            putc(0, dst);
            break;
        default:
            /* Botch in outcode() format string - a compiler-internal
             * bug, not a user-facing error. */
            break;
        }
    }

    va_end(ap);
}
