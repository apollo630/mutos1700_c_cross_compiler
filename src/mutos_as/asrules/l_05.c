/*
 * L-05 - dmath.o's multiplication uses a wrong partial product, in the
 * conversion of a floating constant (docs/MUTOS_ORIGINAL_BUGS.md, L-05;
 * docs/RULES.md).
 *
 * Hook: assemble.c, where the operand of a .float or .double is
 * converted. The real assembler converts with libc.a's atof() on libc.a's
 * floating-point runtime, whose dmul forms its product from one wrong
 * partial product (fltconst.h); fltconst.c re-enacts it.
 *
 * The correction (the corrected mode): the conversion on the runtime of
 * the corrected copy of libc.a that the corrected mode links
 * (src/libc_fixed/, step F3b) - its dmul rounds the exact product -, so
 * that a constant assembles to what the copy's atof() gives for its text
 * at run time (flt_encode_with(..., FLT_LIBC_FIXED);
 * tests/mutos_as/float_coverage/libcatof.py copy). What the copy does not
 * correct stays: ldexp()'s wrapped exponent (L-06), the overflow of 5**k
 * (L-07), the bytes of a zero (fltconst.h).
 *
 * The rule log: in Pass 2, for each operand whose bytes (or whose
 * acceptance: an overflow of the product) the two runtimes give
 * differently - "met" in the original mode, "applied" in the corrected
 * mode (line= the source line, const= the operand). An operand both give
 * alike is not logged.
 *
 * State: active.
 */
#include "asrules.h"

#include <string.h>

FltStatus
rule_l_05(FpKind kind, const char *text, size_t len, unsigned char *out, int report, int line)
{
	unsigned char orig[8], fixed[8];
	FltStatus so, sf;
	size_t size = flt_size(kind);
	int corrected = as_mode == MUTOS_CORRECTED;

	so = flt_encode_with(kind, text, len, orig, FLT_LIBC_ORIGINAL);
	sf = flt_encode_with(kind, text, len, fixed, FLT_LIBC_FIXED);
	if (report && (so != sf || (so == FLT_OK && memcmp(orig, fixed, size) != 0)))
		mutos_rulelog("as", "L-05", corrected ? "applied" : "met",
		    "line=%d const=%.*s", line, (int)(len > 64 ? 64 : len), text);
	if (corrected) {
		if (sf == FLT_OK)
			memcpy(out, fixed, size);
		return sf;
	}
	if (so == FLT_OK)
		memcpy(out, orig, size);
	return so;
}
