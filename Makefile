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
#   make check-docs # check the Markdown docs for drift (see scripts/check_docs.py)

CC     = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 -g

.PHONY: all ld as cpp cc clean test check-docs

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
test: all
	@echo "== mutos_as goldens (kernel_nonopt) =="
	@cd tests/mutos_as/kernel_nonopt && MUTOS_AS=$(CURDIR)/src/mutos_as/mutos_as ../run_goldens.sh
	@echo "== mutos_as goldens (kernel_opt) =="
	@cd tests/mutos_as/kernel_opt && MUTOS_AS=$(CURDIR)/src/mutos_as/mutos_as ../run_goldens.sh
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
