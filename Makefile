# Makefile - MUTOS 1700 Cross-Compiler Toolchain (top level)
#
# Builds all toolchain components in one place, per CLAUDE.md's
# "Build Requirement" workflow rule. Each component keeps its own
# build logic in its own src/mutos_<tool>/ directory (see that
# directory's own Makefile, where one exists); this Makefile just
# aggregates them so `make` from the repo root builds everything.
#
# Components:
#   mutos_ld   - linker              (src/mutos_ld/,  single .c file,
#                                      no sub-Makefile of its own yet)
#   mutos_as   - cross-assembler     (src/mutos_as/)
#   mutos_cpp  - C preprocessor      (src/mutos_cpp/)
#   mutos_cc   - C compiler          (src/mutos_cc/, produces
#                                      mutos_c0/mutos_c1 - the
#                                      mutos_cc driver itself, and
#                                      mutos_c2, are not yet written;
#                                      see src/mutos_cc/README.md)
#
# Usage:
#   make            # build everything
#   make ld         # build just mutos_ld
#   make as         # build just mutos_as
#   make cpp        # build just mutos_cpp
#   make cc         # build just mutos_c0/mutos_c1
#   make clean      # clean every component
#   make test       # run every component's golden-diff test suite
#   make fuzz       # semantic fuzzing of mutos_c0/mutos_c1 (see
#                   # tests/mutos_cc/fuzz/README.md); FUZZ_ARGS passes
#                   # options, e.g. make fuzz FUZZ_ARGS="-n 2000 -s 7"
#   make check-docs # check the Markdown docs for drift (see scripts/check_docs.py)
#   make check-libcatof # .float/.double against libc.a's own atof, and
#                   # mutos_c1's floating-constant text against libc.a's
#                   # own ecvt, run under an 8086 emulator (needs the
#                   # Python module "unicorn"; see
#                   # tests/mutos_as/float_coverage/libcatof.py)
#   make install-hooks # one-time per clone: run check-docs automatically
#                       # before every commit (see .githooks/pre-commit)

CC     = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -g

.PHONY: all ld as cpp cc clean test fuzz check-docs check-libcatof install-hooks

all: ld as cpp cc

ld: src/mutos_ld/mutos_ld

src/mutos_ld/mutos_ld: src/mutos_ld/mutos_ld.c
	$(CC) $(CFLAGS) -o $@ $<

as:
	$(MAKE) -C src/mutos_as

cpp:
	$(MAKE) -C src/mutos_cpp

cc:
	$(MAKE) -C src/mutos_cc

clean:
	rm -f src/mutos_ld/mutos_ld
	$(MAKE) -C src/mutos_as clean
	$(MAKE) -C src/mutos_cpp clean
	$(MAKE) -C src/mutos_cc clean

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
# compiler output in tests/mutos_cc assembles (no reference objects).
# float_coverage/fltmodel.py (Python 3, standard library only) re-derives
# the .float/.double conversion model's unknowns from those goldens and
# checks src/mutos_as/fltconst.c against its own, independent model.
# tests/mutos_as/float_open/ is deliberately NOT run here: it holds
# probes mutos_as still refuses - at present only fltsig.s, which the
# real "as" refuses too (see its README.md).
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
	@echo "== mutos_cpp goldens =="
	@cd tests/mutos_cpp && MUTOS_CPP=$(CURDIR)/src/mutos_cpp/mutos_cpp ./run_goldens.sh
	@echo "== mutos_cc (c0/c1) goldens =="
	@cd tests/mutos_cc && MUTOS_CPP=$(CURDIR)/src/mutos_cpp/mutos_cpp MUTOS_C0=$(CURDIR)/src/mutos_cc/mutos_c0 MUTOS_C1=$(CURDIR)/src/mutos_cc/mutos_c1 ./run_goldens.sh

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
# seeded texts, and the runtime's dmul/ddiv/dadd against fltmodel.py's
# arithmetic. Separate from "test" because it needs the Python module
# "unicorn" (pip install unicorn); LIBCATOF_N sets the number of texts.
LIBCATOF_N = 3000
check-libcatof: as ld cc
	@python3 tests/mutos_as/float_coverage/libcatof.py goldens
	@python3 tests/mutos_as/float_coverage/libcatof.py check $(CURDIR)/src/mutos_as/fltconst_test $(LIBCATOF_N)
	@python3 tests/mutos_as/float_coverage/libcatof.py ops
	@python3 tests/mutos_as/float_coverage/libcatof.py ecvt $(CURDIR)/src/mutos_cc/fltdec_test $(LIBCATOF_N)

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
