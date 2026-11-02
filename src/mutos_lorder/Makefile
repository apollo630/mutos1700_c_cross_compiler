# Makefile - MUTOS 1700 Cross Library Orderer (mutos_lorder)
#
# mutos_lorder is the original MUTOS 1700 lorder, a shell script
# (mutos/mutos_bin/lorder.golden.base64.txt), run by the host's sh; the
# script mutos_lorder.in is made executable with the MUTOS root's compiled-in
# default (docs/TOOLCHAIN_PLAN.md, decision 2) filled in. See README.md.

# The MUTOS root's compiled-in default (src/h/mutos_root.h): the build
# tree's mutos_root/. The top-level Makefile passes the same path.
MUTOS_ROOT_DEFAULT ?= $(abspath $(CURDIR)/../../mutos_root)
TARGET  = mutos_lorder

all: $(TARGET)

# the script is rewritten when the default changes, too
$(TARGET): mutos_lorder.in FORCE
	sed 's|@MUTOS_ROOT_DEFAULT@|$(MUTOS_ROOT_DEFAULT)|' mutos_lorder.in > $@.new
	@if cmp -s $@.new $@; then rm -f $@.new; else mv -f $@.new $@; fi
	chmod +x $@

clean:
	rm -f $(TARGET) $(TARGET).new

FORCE:

.PHONY: all clean FORCE
