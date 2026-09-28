# Makefile -- base64-text companions for the real-hardware "-L"/no-"-L"
# comparison objects gathered by kernel_opt/Makefile.mutos and
# kernel_nonopt/Makefile.mutos (see those two files, and this
# directory's own README section on the "-L" question, for the full
# methodology -- STATUS.md's open item 6).
#
# THIS FILE REQUIRES GNU MAKE -- same reasoning as tests/mutos_cc/Makefile
# (the real MUTOS 1700 make(1) has no $(wildcard) etc. -- see that
# file's own header). Runs on the MODERN (Linux) side only, AFTER
# copying every "*.o"/"*.oL" that kernel_opt/Makefile.mutos's and
# kernel_nonopt/Makefile.mutos's "noL"/"L" targets produced back into
# kernel_opt/ and kernel_nonopt/ respectively.
#
# "make base64" turns each "<name>.o" (the no-"-L" candidate) and
# "<name>.oL" (the "-L" candidate) into a "<name>.o_base64.txt" /
# "<name>.oL_base64.txt" companion -- the same "<file>_base64.txt"
# naming ./mk_goldenbase64.sh already uses for this directory's
# existing "*.o.golden" files, so an underscore (not a dot) right
# before "base64" is what marks these apart from a real golden's own
# "_base64.txt" companion at a glance.
#
# Deliberately does NOT rename or copy anything to "*.o.golden" --
# these are CANDIDATE objects, not (yet) confirmed real-hardware
# references. Once the two Makefile.mutos runs and a diff against the
# already-committed "<name>.o.golden" settle which variant the real
# kernel build actually used, promoting that variant is a separate,
# deliberate manual step -- never automated here, per CLAUDE.md's
# Golden Master Integrity rule (Workflow Guideline 2).
#
# "base64" is intentionally prerequisite-free, same reasoning as
# tests/mutos_cc/Makefile's own "goldens" target: it only packages
# whatever "*.o"/"*.oL" already exist on disk and must never try to
# (re)create them -- that needs the real MUTOS 1700 toolchain, not this
# host's tools.

DIRS := kernel_opt kernel_nonopt
NOL  := $(wildcard kernel_opt/*.o) $(wildcard kernel_nonopt/*.o)
L    := $(wildcard kernel_opt/*.oL) $(wildcard kernel_nonopt/*.oL)

base64:
	@for f in $(NOL) $(L); do \
		base64 "$$f" > "$${f}_base64.txt"; \
		echo "wrote $${f}_base64.txt"; \
	done

clean:
	@for d in $(DIRS); do rm -f "$$d"/*.o "$$d"/*.oL; done

distclean: clean
	@for d in $(DIRS); do rm -f "$$d"/*.o_base64.txt "$$d"/*.oL_base64.txt; done

.PHONY: base64 clean distclean
