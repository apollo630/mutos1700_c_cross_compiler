# src/libc_fixed/ - the corrected copy of libc.a (docs/TOOLCHAIN_PLAN.md,
# section 7.3, step F3b; decision 14): the original archive with the
# members whose corrected source is in fixed/ replaced in place
# (fix_archive.py, with mutos_as, mutos_ar and mutos_ranlib). The MUTOS
# root holds it as lib/fixed/libc.a (scripts/mutos_root.sh). The same for
# the kernel's conf/arith.a, whose long helpers are libc.a's: arith.a. See
# README.md.

LIBC_ORIG = ../../tests/mutos1700_libc/libc.a.base64.txt
ARITH_ORIG = ../../tests/mutos1700_arith/arith.a.base64.txt
TOOLS = ../mutos_as/mutos_as ../mutos_ar/mutos_ar ../mutos_ranlib/mutos_ranlib

all: libc.a arith.a

libc.a: fix_archive.py $(wildcard fixed/*.s) $(LIBC_ORIG) $(TOOLS)
	python3 fix_archive.py -o libc.a

arith.a: fix_archive.py $(wildcard fixed/*.s) $(ARITH_ORIG) $(TOOLS)
	python3 fix_archive.py -o arith.a $(ARITH_ORIG)

test: libc.a arith.a
	python3 check_copy.py

clean:
	rm -f libc.a libc.a.new arith.a arith.a.new

.PHONY: all test clean
