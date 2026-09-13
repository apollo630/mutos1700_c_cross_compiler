# Makefile for the mutos_cc Milestone 4 K&R test corpus.
#
# THIS FILE REQUIRES GNU MAKE. Confirmed from the real MUTOS 1700 make(1)
# manpage: that make has no "%.o: %.c" pattern rules, no $(wildcard)/
# $(dir)/$(notdir) functions, and no ":=" -- only plain "=" macros and
# two-suffix rules like ".c.o:". None of that is present here on purpose;
# this file is for a GNU-make-capable machine (the modern host, or an
# emulator setup that happens to bridge one in) ONLY.
#
# For the real MUTOS 1700 hardware / emulator's OWN make, use
# ./Makefile.mutos instead (`make -f Makefile.mutos`) -- a from-scratch
# rewrite using only what that make(1) manpage actually documents. For no
# make at all, use ./gen_mutos.sh -- a plain /bin/sh loop, verified
# against the real basename(1)/expr(1)/find(1) manpages too (see that
# script's own header for what was checked and one bug that was found
# and fixed: V7 find needs an explicit "-print" or it silently produces
# no output at all). All three approaches produce the same files and are
# interchangeable; pick whichever's easiest to run on your setup.
#
# THREE STEPS, split across two machines:
#
#   1. `make all`           -- ON the real MUTOS 1700 hardware / emulator,
#      IF it has GNU Make (see above -- otherwise use Makefile.mutos or
#      gen_mutos.sh instead). Produces "<name>.s" next to every
#      "<name>.c" (final assembly).
#   2. `make intermediates` -- ALSO ON the real MUTOS 1700 hardware /
#      emulator (same GNU Make caveat). Produces "<name>.i" (cpp's
#      output) and "<name>.1" / "<name>.2" (c0's raw temp1/temp2
#      intermediate-code streams) next to every "<name>.c" -- capturing
#      c0's actual output directly, independent of whatever mutos_c1
#      later does with it. See docs/DEVLOG.md's Milestone 4 "c0/c1
#      process split" section for why this boundary is worth having on
#      its own, separate from the final ".s". `make all intermediates`
#      runs both in one pass.
#   3. `make goldens`       -- on the MODERN (Linux) side, AFTER copying
#      every ".s"/".i"/".1"/".2" produced in steps 1-2 back (however they
#      were generated). Turns them into "*.s.golden"/"*.i.golden" (plain
#      text) and "*.1.golden"/"*.2.golden" plus base64 companions
#      (temp1/temp2 are raw tagged byte streams -- v7/cc/c04.c's
#      outcode() -- not text, so they get the same binary+base64.txt
#      treatment already used project-wide for tests/mutos1700_libc/*.o
#      and libc.a). This step needs GNU Make (or just run the equivalent
#      cp/base64 commands by hand -- it's a handful of lines).
#
# CC/CPP/C0 default to the real V7-heritage binaries on MUTOS 1700 itself
# (cc.c's own hardcoded paths: /lib/cpp, /lib/c0). Override if needed,
# e.g.: make CC=/usr/bin/cc
#
# BUG FIX (found on real hardware: "make all" produced no ".s" files at
# all): cc's -P and -S cannot be combined. Reading cc.c's actual control
# flow (not just its flag-parsing switch) shows -P makes cc stop dead
# right after cpp -- "if (pflag) { cflag++; continue; }" runs BEFORE the
# code that checks sflag (from -S) ever executes, for every source file,
# unconditionally. So "cc -P -S foo.c" never reaches c0, c1, or the
# ".s"-naming logic at all; it only ever produces a "foo.i". This
# contradicts what an earlier version of this comment claimed (that -P
# and -S combine safely) -- that was wrong. CFLAGS is now just "-S", no
# -P. This does NOT affect byte-parity with the mutos_cpp golden corpus:
# c0's own intermediate-code opcodes (v7/cc/c0.h) have nothing for
# source line/file tracking, so whether cpp inserted '# N "file"' line
# markers or not (the actual difference -P makes to cpp itself) has no
# way to show up in the resulting ".s" text. -P stays correct and
# necessary for the plain "%.i: %.c" rule below, which calls cpp
# directly (not through the cc driver), so none of this -P/-S
# interaction applies there.
#
# IMPORTANT: c0 must receive cpp's OUTPUT, never the raw .c file. Real
# cc.c never invokes c0 on the source file directly -- c0's "source" argv
# is always cc's own cpp-output temp file (see cc.c: av[1] = tmp4, where
# tmp4 is cpp's output, set *before* c0 is even exec'd). c0's own lexer
# happens to tolerate raw source with comments too (it has its own "/*"
# handling in c00.c, independent of cpp), so `/lib/c0 name.c ...` may
# often "work" by coincidence -- but it is not what the real pipeline
# does, so it is not guaranteed to produce byte-identical temp1/temp2 to
# what mutos_c1 will actually be diffed against. The %.1 rule below
# depends on %.i, not %.c, specifically to avoid that gap.
#
# IMPORTANT V7 cc QUIRK (confirmed by reading v7/cc/cc.c's setsuf()):
# "cc -S dir/foo.c" writes "foo.s" into the CURRENT directory, NOT
# "dir/foo.s" -- historic Unix cc strips the directory component of
# the source path when deriving the output name. The recipes below
# work around this by cd-ing into each source file's own directory
# before invoking cc/cpp/c0, so every output ends up next to its ".c"
# as expected. (mutos_cc's own driver will need to reproduce this exact
# stripping behaviour for full V7 CLI fidelity -- see CLAUDE.md's
# "CLI Interface" requirement.)

CC       = cc
CFLAGS   = -S
CPP      = /lib/cpp
CPPFLAGS = -P
C0       = /lib/c0

SRCS    := $(wildcard */*.c)
ASMS    := $(SRCS:.c=.s)
CPPOUTS := $(SRCS:.c=.i)
T1S     := $(SRCS:.c=.1)
T2S     := $(SRCS:.c=.2)

.SUFFIXES: .c .i .s .1 .2

all: $(ASMS)

%.s: %.c
	cd $(dir $<) && $(CC) $(CFLAGS) $(notdir $<)

%.i: %.c
	cd $(dir $<) && $(CPP) $(CPPFLAGS) $(notdir $<) > $(notdir $@)

# Produces both <name>.1 and <name>.2 in one c0 invocation; .2 is a
# side effect of this rule, not a separately buildable target (see the
# T2S file list below, used only for the goldens/clean bookkeeping).
%.1: %.i
	cd $(dir $<) && $(C0) $(notdir $<) $(notdir $(basename $<)).1 $(notdir $(basename $<)).2

intermediates: $(T1S)

# Run only on the modern host, after copying *.s/*.i/*.1/*.2 back --
# see README.md's DIRSIZ=14 note for why the longer suffixes below are
# deliberately never created on the real MUTOS filesystem.
goldens: all intermediates
	@for f in $(ASMS) $(CPPOUTS); do \
		cp -v "$$f" "$$f.golden"; \
	done
	@for f in $(T1S) $(T2S); do \
		cp -v "$$f" "$$f.golden"; \
		base64 "$$f" > "$$f.golden.base64.txt"; \
	done

clean:
	rm -f $(ASMS) $(CPPOUTS) $(T1S) $(T2S)

distclean: clean
	find . \( -name '*.golden' -o -name '*.golden.base64.txt' \) -exec rm -f {} +

.PHONY: all intermediates goldens clean distclean
