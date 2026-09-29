# Makefile for tests/mutos_as/float_open/'s real-hardware floating
# point probe (currently fltmul.s).
#
# THIS FILE REQUIRES GNU MAKE - same reasoning as
# ../float_coverage/Makefile (the real MUTOS 1700 make(1) has no
# $(wildcard) etc.). For the real MUTOS 1700 hardware/emulator's OWN
# make, use ./Makefile.mutos instead (`make -f Makefile.mutos`).
#
# TWO STEPS, split across two machines - same shape as
# ../float_coverage/Makefile:
#
#   1. `make -f Makefile.mutos [clean]` -- ON the real MUTOS 1700
#      hardware/emulator. Assembles fltmul.s with the real "as",
#      producing "fltmul.o" - see README.md for what it is expected
#      to do there (assemble cleanly; only mutos_as itself refuses it,
#      by design - see fltmul.s's own header).
#   2. `make goldens` -- on the MODERN (Linux) side, after copying the
#      "fltmul.o" produced in step 1 back here. Turns it into
#      "fltmul.o.golden" (verbatim copy) plus
#      "fltmul.o.golden_base64.txt" (base64 text) - the SAME naming
#      convention already used by ../kernel_opt/, ../kernel_nonopt/,
#      ../libc_recon/ and ../float_coverage/ (see ../mk_goldenbase64.sh).
#
# Once fltmul.o.golden exists, read it with
# `python3 ../float_coverage/fltmodel.py survivors ../float_coverage .`
# (see README.md), then update src/mutos_as/fltconst.c accordingly -
# this directory does NOT wire into the top-level "make test" (unlike
# ../float_coverage/), because the current mutos_as refuses most
# constants in fltmul.s by design until that's done.
#
# goldens is intentionally prerequisite-free, same reasoning as
# ../float_coverage/Makefile's own "goldens" target: it only packages
# whatever "fltmul.o" already exists on disk and must never try to
# (re)create it - that needs the real MUTOS 1700 toolchain, not this
# host's tools.

SRCS := fltmul.s
OBJS := $(SRCS:.s=.o)

goldens:
	@for f in $(OBJS); do \
		if [ -f "$$f" ]; then \
			cp -v "$$f" "$$f.golden"; \
			base64 "$$f" > "$$f.golden_base64.txt"; \
		else echo "skipping $$f (not found -- generate it on real MUTOS 1700 hardware first, see Makefile.mutos)"; fi; \
	done

clean:
	rm -f $(OBJS)

distclean: clean
	rm -f *.o.golden *.o.golden_base64.txt

.PHONY: goldens clean distclean
