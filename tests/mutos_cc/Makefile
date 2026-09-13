# Makefile for the mutos_cc Milestone 4 K&R test corpus.
#
# TWO SEPARATE STEPS, on two different machines:
#
#   1. `make`  (this target only) -- run ON the real MUTOS 1700
#      hardware / emulator. Produces "<name>.s" next to every
#      "<name>.c". Every source file below is named so that both
#      "<name>.c" and "<name>.s" stay within MUTOS 1700's real
#      DIRSIZ=14 filename limit (see tests/mutos_cpp/h/dir.h and
#      h/param.h in the main repo: `char d_name[DIRSIZ]`, DIRSIZ=14
#      -- the classic V7 Unix directory-entry name limit MUTOS 1700
#      inherited verbatim). Longer names are silently truncated by a
#      real V7 filesystem, not rejected -- two different files whose
#      first 14 characters collide would clobber each other, so this
#      corpus stays comfortably under the limit everywhere.
#
#   2. `make goldens` -- run on the MODERN (Linux) side, AFTER
#      copying the ".s" files produced in step 1 back. This is where
#      each ".s" becomes "<name>.s.golden" (parallel to the existing
#      "*.i.golden" convention in tests/mutos_cpp/). That suffix is
#      long enough to blow past DIRSIZ=14 (e.g. "01_wordcount.s"
#      alone is already 14 characters; "01_wordcount.s.golden" would
#      be 22) -- which is fine on a modern filesystem (the existing
#      "tests/mutos_cpp/c/v30ide.i.golden" is already 15 characters
#      for the same reason) but must NOT be done on the real MUTOS
#      filesystem itself. Do not run `make goldens` on MUTOS.
#
# CC defaults to the real V7-heritage "cc" on MUTOS 1700 itself.
# Override on the command line if it lives somewhere else, e.g.:
#   make CC=/usr/bin/cc
#
# -P matches the flag the mutos_cpp golden corpus was generated with:
# CLAUDE.md notes that mutos_cpp never emits '# N "file"' line-marker
# output regardless of -P, because every real golden reference it is
# checked against was itself produced with cc's -P (which cc forwards
# verbatim to cpp). Using -P here keeps this corpus consistent with
# that same convention.
# -S stops cc after c1 (or c2 under -O -- not used yet, c2/Milestone 5
# has not started), before invoking the assembler: exactly the ".s"
# this corpus wants.
#
# IMPORTANT V7 cc QUIRK (confirmed by reading v7/cc/cc.c's setsuf()):
# "cc -S dir/foo.c" writes "foo.s" into the CURRENT directory, NOT
# "dir/foo.s" -- historic Unix cc strips the directory component of
# the source path when deriving the output name. The recipe below
# works around this by cd-ing into each source file's own directory
# before invoking cc, so every ".s" ends up next to its ".c" as
# expected. (mutos_cc's own driver will need to reproduce this exact
# stripping behaviour for full V7 CLI fidelity -- see CLAUDE.md's
# "CLI Interface" requirement.)

CC      = cc
CFLAGS  = -P -S

SRCS    := $(wildcard */*.c)
ASMS    := $(SRCS:.c=.s)
GOLDENS := $(SRCS:.c=.s.golden)

.SUFFIXES: .c .s

all: $(ASMS)

%.s: %.c
	cd $(dir $<) && $(CC) $(CFLAGS) $(notdir $<)

# Run only on the modern host, after copying the *.s files back --
# see the DIRSIZ=14 note above.
goldens: all
	@for f in $(ASMS); do \
		cp -v "$$f" "$$f.golden"; \
	done

clean:
	rm -f $(ASMS)

distclean: clean
	rm -f $(GOLDENS)

.PHONY: all goldens clean distclean
