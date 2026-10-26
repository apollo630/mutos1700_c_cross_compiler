# Makefile - MUTOS 1700 Cross-Compiler Toolchain (top level)
#
# Builds all toolchain components in one place, per CLAUDE.md's
# "Build Requirement" workflow rule. Each component keeps its own
# build logic in its own src/mutos_<tool>/ directory (see that
# directory's own Makefile, where one exists); this Makefile just
# aggregates them so `make` from the repo root builds everything.
#
# Components:
#   mutos_ld   - linker              (src/mutos_ld/, a port of the
#                                      original ld)
#   mutos_as   - cross-assembler     (src/mutos_as/)
#   mutos_cpp  - C preprocessor      (src/mutos_cpp/, a port of the
#                                      original cpp)
#   mutos_cc   - C compiler          (src/mutos_cc/, produces the
#                                      driver mutos_cc and the passes
#                                      mutos_c0/mutos_c1/mutos_c2 - ports
#                                      of the original cc, c0, c1 and
#                                      c2; see src/mutos_cc/README.md)
#   mutos_ar   - archiver            (src/mutos_ar/, a port of the
#                                      original ar)
#   mutos_ranlib - archive indexer   (src/mutos_ranlib/, a port of the
#                                      original ranlib; it runs mutos_ar
#                                      through the MUTOS root's bin/ar)
#   mutos_root/ - the MUTOS root     (scripts/mutos_root.sh: crt0.o,
#                                      libc.a, the passes as symbolic
#                                      links; docs/TOOLCHAIN_PLAN.md,
#                                      decisions 2 and 3)
#
# Usage:
#   make            # build everything, and make mutos_root/
#   make ld         # build just mutos_ld
#   make as         # build just mutos_as
#   make cpp        # build just mutos_cpp
#   make cc         # build just mutos_cc/mutos_c0/mutos_c1/mutos_c2
#   make ar         # build just mutos_ar
#   make ranlib     # build just mutos_ranlib
#   make root       # make (or bring up to date) mutos_root/
#   make clean      # clean every component, remove mutos_root/
#   make test       # run every component's golden-diff test suite
#   make fuzz       # semantic fuzzing of mutos_c0/mutos_c1 (see
#                   # tests/mutos_cc/fuzz/README.md); FUZZ_ARGS passes
#                   # options, e.g. make fuzz FUZZ_ARGS="-n 2000 -s 7"
#   make check-docs # check the Markdown docs for drift (see scripts/check_docs.py)
#   make check-libcatof # .float/.double against libc.a's own atof, and
#                   # mutos_c1's floating-constant and float-initializer
#                   # text against libc.a's own ecvt and fstsp, run under
#                   # an 8086 emulator (needs the
#                   # Python module "unicorn"; see
#                   # tests/mutos_as/float_coverage/libcatof.py)
#   make check-realbin # the original MUTOS c0 and c1 (mutos/mutos_bin/), run under
#                   # an 8086 emulator, against every tests/mutos_cc
#                   # golden, tests/realbin/c1_tables.txt against the c1
#                   # binary, and mutos_c1, mutos_c0 and mutos_cpp (the
#                   # ports of c1, c0 and cpp) against the emulated
#                   # originals; the
#                   # original cc, traced, with the original passes
#                   # against the goldens it made; mutos_ld (the port
#                   # of ld) against the original on every compiled
#                   # golden, hand-written cases and two fuzz runs, and
#                   # its -r/-d extension (tests/mutos_ld/); mutos_cc
#                   # (the port of cc) against the original, traced, with
#                   # the original passes and with the ported ones, and a
#                   # fuzz run; mutos_c2 (the port of c2) against the
#                   # original on every .s golden, hand-written cases and
#                   # four fuzz runs; mutos_as (not a port: a measurement)
#                   # against the original on every golden source and
#                   # hand-written case, the classes of the runs pinned
#                   # in tests/realbin/ascmp_*.txt, and a fuzz run over the
#                   # real compiler's output; mutos_ar and mutos_ranlib (the
#                   # ports of ar and ranlib) against the original on
#                   # hand-written cases and fuzz runs, and libc.a rebuilt
#                   # from its objects; the
#                   # original ld through mutos_root/ (needs the Python
#                   # module "unicorn" and, for cc's stub, a C compiler;
#                   # see tests/realbin/README.md)
#   make install-hooks # one-time per clone: run check-docs automatically
#                       # before every commit (see .githooks/pre-commit)

CC     = cc

# The MUTOS root (docs/TOOLCHAIN_PLAN.md, decisions 2 and 3): every tool
# looks up the absolute MUTOS paths (/lib/crt0.o, /lib/c0, /usr/include)
# under $MUTOS_ROOT, by default this tree's mutos_root/ - its absolute
# path, compiled in as MUTOS_ROOT_DEFAULT (src/h/mutos_root.h). Every
# component gets the same definition: passed to the sub-Makefiles (each
# compiles with -std=c11 -Wall -Wextra -Wpedantic), which work out the
# same path when run on their own. After moving the tree:
# rebuild, or set MUTOS_ROOT.
ifneq ($(words $(CURDIR)),1)
$(error the path of the build tree must not contain blanks: $(CURDIR))
endif
MUTOS_ROOT_DEFAULT = $(CURDIR)/mutos_root
SUBMAKE = $(MAKE) MUTOS_ROOT_DEFAULT='$(MUTOS_ROOT_DEFAULT)'

.PHONY: all ld as cpp cc ar ranlib root clean test fuzz check-docs check-libcatof check-realbin install-hooks

all: ld as cpp cc ar ranlib root

ld:
	$(SUBMAKE) -C src/mutos_ld

as:
	$(SUBMAKE) -C src/mutos_as

cpp:
	$(SUBMAKE) -C src/mutos_cpp

cc:
	$(SUBMAKE) -C src/mutos_cc

ar:
	$(SUBMAKE) -C src/mutos_ar

ranlib:
	$(SUBMAKE) -C src/mutos_ranlib

# mutos_root/: crt0.o and libc.a decoded from tests/ (libc.a with its
# __.SYMDEF time), the passes and tools as symbolic links into src/ (a
# link to a tool not built yet dangles), usr/lib/, usr/include/ and tmp/.
# scripts/mutos_root.sh rewrites only what differs. Ignored by git.
root:
	@sh scripts/mutos_root.sh

# "make clean" removes mutos_root/ too - with anything put into it (its
# usr/lib/ is for the user's own libraries) - and never touches tests/ or
# mutos/mutos_bin/ (CLAUDE.md, Workflow Guidelines 2 and 5).
clean:
	$(MAKE) -C src/mutos_ld clean
	$(MAKE) -C src/mutos_as clean
	$(MAKE) -C src/mutos_cpp clean
	$(MAKE) -C src/mutos_cc clean
	$(MAKE) -C src/mutos_ar clean
	$(MAKE) -C src/mutos_ranlib clean
	rm -rf ./mutos_root

# Runs every component's own golden-diff test suite. Each suite is
# independently authoritative about what "pass" means for that
# component at its current stage of completeness (see each
# component's own STATUS.md entry) - this target just runs them all
# from one place rather than re-implementing any of their logic.
# mutos_as's run_goldens.sh is per-directory (it assembles every *.s
# in the current directory), so it is run once per golden subdirectory.
# libc_recon/ adds check_floatdat.sh (".float" bytes against real
# libc.a constants), float_coverage/ holds real-hardware objects from
# hand-written .float/.double sources (zeros, doubles, inexact values,
# the rounding probes fltmode.s and fltmul.s and the range probe fltovf.s
# among them), and assemble_cc_goldens.sh checks that every real
# compiler output in tests/mutos_cc assembles (no reference objects),
# and check_switches.sh the switches -1 (the seven v30_speculative
# sources assemble with it, to their recorded objects, not without it)
# and -C (the local L symbols left out).
# float_coverage/fltmodel.py (Python 3, standard library only) re-derives
# the .float/.double conversion model's unknowns from those goldens and
# checks src/mutos_as/fltconst.c against its own, independent model.
# tests/mutos_as/float_open/ is deliberately NOT run here: it holds
# probes mutos_as still refuses - at present only fltsig.s, which the
# real "as" refuses too (see its README.md).
# tests/mutos_root/check_root.py checks mutos_root/ (its files against
# the references in tests/, libc.a's time, the links), the lookup of
# src/h/mutos_root.h, and that mutos_ld uses libc.a's __.SYMDEF without
# "out of date". tests/mutos_cc/check_driver.py runs the driver mutos_cc
# with the ported passes through mutos_root/ - "cc -S", "cc -P" against
# the goldens, "cc -O -S" (mutos_c2) against the kernel_opt sources,
# "cc -o" and "cc -O -o" on the construct corpus. tests/mutos_ar/
# check_libc.py rebuilds libc.a from its 167 objects with mutos_ar and
# mutos_ranlib (the same bytes but for __.SYMDEF's time, group and mode),
# lists, extracts and prints it, and runs ranlib twice.
test: all
	@echo "== mutos_as goldens (kernel_nonopt) =="
	@cd tests/mutos_as/kernel_nonopt && MUTOS_AS=$(CURDIR)/src/mutos_as/mutos_as ../run_goldens.sh
	@echo "== mutos_as goldens (kernel_opt) =="
	@cd tests/mutos_as/kernel_opt && MUTOS_AS=$(CURDIR)/src/mutos_as/mutos_as ../run_goldens.sh
	@echo "== mutos_as goldens (libc_recon - real libc.a objects) =="
	@cd tests/mutos_as/libc_recon && MUTOS_AS=$(CURDIR)/src/mutos_as/mutos_as ../run_goldens.sh
	@cd tests/mutos_as/libc_recon && MUTOS_AS=$(CURDIR)/src/mutos_as/mutos_as ./check_floatdat.sh
	@echo "== mutos_as goldens (float_coverage - .float/.double) =="
	@cd tests/mutos_as/float_coverage && MUTOS_AS=$(CURDIR)/src/mutos_as/mutos_as ../run_goldens.sh
	@echo "== mutos_as .float/.double conversion model (float_coverage/fltmodel.py) =="
	@python3 tests/mutos_as/float_coverage/fltmodel.py survivors
	@python3 tests/mutos_as/float_coverage/fltmodel.py check $(CURDIR)/src/mutos_as/fltconst_test
	@echo "== mutos_as on the real compiler's .s (tests/mutos_cc goldens) =="
	@MUTOS_AS=$(CURDIR)/src/mutos_as/mutos_as tests/mutos_as/assemble_cc_goldens.sh
	@echo "== mutos_as switches -1 and -C =="
	@MUTOS_AS=$(CURDIR)/src/mutos_as/mutos_as tests/mutos_as/check_switches.sh
	@echo "== mutos_cpp goldens =="
	@cd tests/mutos_cpp && MUTOS_CPP=$(CURDIR)/src/mutos_cpp/mutos_cpp ./run_goldens.sh
	@echo "== mutos_cc (c0/c1) goldens =="
	@cd tests/mutos_cc && MUTOS_CPP=$(CURDIR)/src/mutos_cpp/mutos_cpp MUTOS_C0=$(CURDIR)/src/mutos_cc/mutos_c0 MUTOS_C1=$(CURDIR)/src/mutos_cc/mutos_c1 ./run_goldens.sh
	@echo "== the MUTOS root (mutos_root/, src/h/mutos_root.h) =="
	@python3 tests/mutos_root/check_root.py --cc "$(CC)" $(MUTOS_ROOT_DEFAULT)
	@echo "== mutos_cc (the driver) with the ported passes =="
	@MUTOS_ROOT=$(MUTOS_ROOT_DEFAULT) python3 tests/mutos_cc/check_driver.py
	@echo "== mutos_ar and mutos_ranlib rebuild libc.a =="
	@python3 tests/mutos_ar/check_libc.py

# Checks every *.md file for the kind of drift a rebuild/regression run
# can't catch: numbers restated in more than one place that disagree,
# stale file references (renamed/typo'd filenames), broken internal
# links/anchors, and a "Last updated" stamp that doesn't match the file's
# actual last commit. Independent of the "test" target above - this is
# about the docs, not the toolchain build. See scripts/check_docs.py's own
# header for exactly what it checks and why each check is shaped that way.
check-docs:
	@python3 scripts/check_docs.py

# mutos_as's .float/.double conversion against libc.a's own atof() on
# libc.a's own floating-point runtime, run under an 8086 emulator
# (Unicorn): every golden constant, fltconst.c's accepted output on
# seeded texts, the runtime's dmul/ddiv/dadd against fltmodel.py's
# arithmetic, and mutos_c1's constant texts (fltdec_test, "-f" a float
# initializer's) against libc.a's ecvt() (of fstsp's float). Separate
# from "test" because it needs the Python module
# "unicorn" (pip install unicorn); LIBCATOF_N sets the number of texts.
LIBCATOF_N = 3000
check-libcatof: as ld cc
	@python3 tests/mutos_as/float_coverage/libcatof.py goldens
	@python3 tests/mutos_as/float_coverage/libcatof.py check $(CURDIR)/src/mutos_as/fltconst_test $(LIBCATOF_N)
	@python3 tests/mutos_as/float_coverage/libcatof.py ops
	@python3 tests/mutos_as/float_coverage/libcatof.py ecvt $(CURDIR)/src/mutos_cc/fltdec_test $(LIBCATOF_N)
	@python3 tests/mutos_as/float_coverage/libcatof.py fecvt $(CURDIR)/src/mutos_cc/fltdec_test $(LIBCATOF_N)

# The original MUTOS 1700 binaries as an oracle: the real c0 and c1 from
# mutos/mutos_bin/, run under an 8086 emulator (tests/realbin/mutosemu.py), must
# give back every tests/mutos_cc golden byte for byte - c1's messages and
# exit status included - and tests/realbin/c1_tables.txt must be what
# c1tables.py reads from the c1 binary; then the share of c1's code-table
# rows the goldens use; then mutos_c1, the port of c1 (src/mutos_cc/c1port/),
# against the emulated original: its generated tables (c1tab.c) current,
# and on every golden .1/.2 pair (tests/realbin/c1trees.py) its
# expression trees as read and after optim() (depths 1 and 2), its .s
# with all of the original's traces on (depth 3) and its .s, messages
# and exit status (depth 4) identical - p19_open3 refused where
# tests/mutos_cc/invalid_goldens.txt says; then a fixed-seed differential
# fuzz run against the original (tests/realbin/c1fuzz.py, depth 4); then
# mutos_c0, the port of c0 (src/mutos_cc/c0port/): its generated data
# segment (c0data.c) current, its .1, .2, messages and exit status the
# original's on every .i golden (tests/realbin/c0cmp.py), and two
# fixed-seed differential fuzz runs, one with mutated programs
# (tests/realbin/c0fuzz.py); then mutos_cpp, the port of cpp
# (src/mutos_cpp/): its generated data segment (cppdata.c) current, its
# output, messages and exit status the original's on every .i golden's
# source - with -P, without (the # N "file" lines), into an output file
# (tests/realbin/cppcmp.py) -, and two fixed-seed differential fuzz runs,
# one with mutated programs (tests/realbin/cppfuzz.py); then mutos_ld,
# the port of ld (src/mutos_ld/): its generated data segment (lddata.c)
# current, its messages, exit status and output files the original's on
# every compiled golden linked with a matrix of options and on the
# hand-written cases (tests/realbin/ldcmp.py), two fixed-seed
# differential fuzz runs, one with damaged files (tests/realbin/
# ldfuzz.py), and its extension -r/-d, which the original refuses,
# by running programs linked through a relocatable object
# (tests/mutos_ld/check_rflag.py); then the
# original cc (tests/realbin/
# cctrace.py, fork/exece/wait faked) running the original passes
# against the goldens it made on the hardware - "cc -S" (c1's messages
# and exit status included), "cc -P", "cc -O -S" (c2: the kernel_opt
# sources); then mutos_cc, the port of cc (src/mutos_cc/ccport/): its
# generated segment (ccdata.c) current, the passes it runs with their
# arguments, its messages, exit status and the files left the
# original's - on a matrix of command lines with the passes traced, on
# the construct corpus with the original passes run in both, and with
# the ported passes against the original chain (tests/realbin/
# cccmp.py) -, the ported chain against the goldens, and a fixed-seed
# differential fuzz run (tests/realbin/ccfuzz.py); then mutos_c2, the
# port of c2 (src/mutos_cc/c2port/): its generated data segment
# (c2data.c) current, its output, messages and exit status the
# original's on every .s golden three ways ("c2 in out", "c2 - in out",
# "c2 <in >out"; the 11_kernel ones the kernel_opt sources) and on the
# hand-written cases (tests/realbin/c2cmp.py), and four fixed-seed
# differential fuzz runs - the real compiler's output for random
# programs, assembly made for c2's passes, both damaged
# (tests/realbin/c2fuzz.py); then mutos_as (a re-implementation, not a
# port): its runs on every golden source and hand-written case put into
# classes against the emulated original and compared with the classes
# pinned in tests/realbin/ascmp_goldens.txt and ascmp_cases.txt (a new
# or a vanished difference fails), and a fuzz run in which the real
# compiler's output must give the same object (tests/realbin/ascmp.py,
# asfuzz.py); then mutos_ar and mutos_ranlib (the ports of ar and ranlib,
# src/mutos_ar/, src/mutos_ranlib/): ar's generated error texts (ardata.c)
# current, its messages, exit status and the files left (every byte and
# mode) the original's on hand-written command sequences (tests/realbin/
# arcmp.py) and on two fixed-seed differential fuzz runs, one with damaged
# archives (arfuzz.py); ranlib's the same on hand-written cases
# (ranlibcmp.py, the original ar under the emulator too) with libc.a
# rebuilt from its objects, and two fuzz runs (ranlibfuzz.py); then the
# original ld through mutos_root/ (check_root.py --realbin). Separate
# from "test" because it needs the Python module "unicorn" (pip install
# unicorn). See tests/realbin/README.md.
REALBIN_FUZZ_ARGS = -n 200 -s 1
REALBIN_C0FUZZ_ARGS = -n 200 -s 1
REALBIN_CPPFUZZ_ARGS = -n 200 -s 1
REALBIN_LDFUZZ_ARGS = -n 300 -s 1
REALBIN_CCFUZZ_ARGS = -n 1000 -s 1
REALBIN_C2FUZZ_ARGS = -n 100 -s 1
REALBIN_ASFUZZ_ARGS = -n 300 -s 1
REALBIN_ARFUZZ_ARGS = -n 300 -s 1
REALBIN_RANLIBFUZZ_ARGS = -n 150 -s 1
check-realbin: cc cpp as ld ar ranlib root
	@python3 tests/realbin/c1tables.py --check tests/realbin/c1_tables.txt
	@python3 tests/realbin/c1tables.py --check-c src/mutos_cc/c1port/c1tab.c
	@python3 tests/realbin/check_real.py goldens
	@python3 tests/realbin/check_real.py coverage
	@python3 tests/realbin/c1trees.py --depth 2
	@python3 tests/realbin/c1trees.py --depth 3
	@python3 tests/realbin/c1trees.py --depth 4
	@python3 tests/realbin/c1fuzz.py $(REALBIN_FUZZ_ARGS)
	@python3 tests/realbin/c0tables.py --check-c src/mutos_cc/c0port/c0data.c
	@python3 tests/realbin/c0cmp.py goldens
	@python3 tests/realbin/c0fuzz.py $(REALBIN_C0FUZZ_ARGS)
	@python3 tests/realbin/c0fuzz.py $(REALBIN_C0FUZZ_ARGS) --mutate 0.7
	@python3 tests/realbin/cpptables.py --check-c src/mutos_cpp/cppdata.c
	@python3 tests/realbin/cppcmp.py goldens
	@python3 tests/realbin/cppfuzz.py $(REALBIN_CPPFUZZ_ARGS)
	@python3 tests/realbin/cppfuzz.py $(REALBIN_CPPFUZZ_ARGS) --mutate 0.5
	@python3 tests/realbin/ldtables.py --check-c src/mutos_ld/lddata.c
	@python3 tests/realbin/ldcmp.py goldens
	@python3 tests/realbin/ldcmp.py cases
	@python3 tests/realbin/ldfuzz.py $(REALBIN_LDFUZZ_ARGS)
	@python3 tests/realbin/ldfuzz.py $(REALBIN_LDFUZZ_ARGS) --mutate 0.5
	@python3 tests/mutos_ld/check_rflag.py
	@python3 tests/realbin/cctrace.py goldens
	@python3 tests/realbin/cctables.py --check-c src/mutos_cc/ccport/ccdata.c
	@python3 tests/realbin/cccmp.py cases
	@python3 tests/realbin/cccmp.py run
	@python3 tests/realbin/cccmp.py programs
	@python3 tests/realbin/cccmp.py goldens
	@python3 tests/realbin/ccfuzz.py $(REALBIN_CCFUZZ_ARGS)
	@python3 tests/realbin/c2tables.py --check-c src/mutos_cc/c2port/c2data.c
	@python3 tests/realbin/c2cmp.py goldens
	@python3 tests/realbin/c2cmp.py cases
	@python3 tests/realbin/c2fuzz.py $(REALBIN_C2FUZZ_ARGS) --gen c1fuzz
	@python3 tests/realbin/c2fuzz.py $(REALBIN_C2FUZZ_ARGS) --gen fuzz_c
	@python3 tests/realbin/c2fuzz.py $(REALBIN_C2FUZZ_ARGS) --gen asm
	@python3 tests/realbin/c2fuzz.py $(REALBIN_C2FUZZ_ARGS) --gen mix --mutate 0.5
	@python3 tests/realbin/ascmp.py goldens --check tests/realbin/ascmp_goldens.txt
	@python3 tests/realbin/ascmp.py cases --check tests/realbin/ascmp_cases.txt
	@python3 tests/realbin/asfuzz.py $(REALBIN_ASFUZZ_ARGS) --gen cc --strict
	@python3 tests/realbin/artables.py --check-c src/mutos_ar/ardata.c
	@python3 tests/realbin/arcmp.py cases
	@python3 tests/realbin/arfuzz.py $(REALBIN_ARFUZZ_ARGS)
	@python3 tests/realbin/arfuzz.py $(REALBIN_ARFUZZ_ARGS) --mutate 0.12
	@python3 tests/realbin/ranlibcmp.py cases
	@python3 tests/realbin/ranlibcmp.py libc
	@python3 tests/realbin/ranlibfuzz.py $(REALBIN_RANLIBFUZZ_ARGS)
	@python3 tests/realbin/ranlibfuzz.py $(REALBIN_RANLIBFUZZ_ARGS) --mutate 0.15
	@python3 tests/mutos_root/check_root.py --cc "$(CC)" --realbin $(MUTOS_ROOT_DEFAULT)

# Random-program semantic fuzzing of mutos_c0/mutos_c1 - separate from
# "test" (whose golden diffs are exact and fast); a fixed default seed
# keeps a plain "make fuzz" repeatable.
FUZZ_ARGS = -n 500 -s 1
fuzz: cpp cc as
	@python3 tests/mutos_cc/fuzz/fuzz_c.py $(FUZZ_ARGS)

# One-time setup per clone: points git at the tracked .githooks/ directory
# instead of the untracked (and therefore un-shareable) .git/hooks/, so
# check-docs runs automatically before every commit from here on. Safe to
# re-run.
install-hooks:
	git config core.hooksPath .githooks
	chmod +x .githooks/pre-commit
	@echo "Git hooks installed (core.hooksPath -> .githooks/). 'make check-docs' now runs automatically before every commit."
