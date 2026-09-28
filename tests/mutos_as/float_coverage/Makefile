# Makefile for tests/mutos_as/float_coverage/'s real-hardware floating
# point goldens.
#
# THIS FILE REQUIRES GNU MAKE - same reasoning as tests/mutos_cc/Makefile
# (the real MUTOS 1700 make(1) has no $(wildcard) etc. - see that file's
# own header). For the real MUTOS 1700 hardware/emulator's OWN make, use
# ./Makefile.mutos instead (`make -f Makefile.mutos`).
#
# TWO STEPS, split across two machines:
#
#   1. `make -f Makefile.mutos [clean]` -- ON the real MUTOS 1700
#      hardware/emulator. Assembles every *.s here with the real "as",
#      producing "<name>.o" next to each "<name>.s" - see README.md
#      (all five assemble there, and mutos_as reproduces all five).
#   2. `make goldens` -- on the MODERN (Linux) side, after copying every
#      "*.o" produced in step 1 back here. Turns them into
#      "*.o.golden" (verbatim copy) plus "*.o.golden_base64.txt" (base64
#      text) - the SAME naming convention already used by
#      tests/mutos_as/kernel_opt/, kernel_nonopt/ and libc_recon/ (see
#      ../mk_goldenbase64.sh), unlike ../Makefile's "-L" experiment,
#      whose two competing variants are deliberately named differently
#      (*_base64.txt without ".golden") because they are candidates, not
#      yet-confirmed goldens - there is no such ambiguity here: each of
#      these five files has exactly one real-hardware answer.
#
# fltopen.s was generated in ../float_open/ (the directory for probes
# mutos_as still refuses) and moved here with its golden once mutos_as
# reproduced it. Any new golden here also feeds fltmodel.py, which
# re-derives the conversion model's unknowns from every golden in this
# directory (top-level "make test").
#
# goldens is intentionally prerequisite-free, same reasoning as
# tests/mutos_cc/Makefile's own "goldens" target: it only packages
# whatever "*.o" already exist on disk and must never try to (re)create
# them - that needs the real MUTOS 1700 toolchain, not this host's
# tools.

SRCS := fltaddr.s fltmulti.s fltzero.s fltdbl.s fltopen.s
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
