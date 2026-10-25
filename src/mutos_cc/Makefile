# Makefile - MUTOS 1700 Cross C Compiler: the driver and the passes
# (mutos_cc / mutos_c0 / mutos_c1)
#
# Builds on a Linux host, matching mutos_as/mutos_cpp/mutos_ld's
# style/tooling: plain C11, gcc/clang, no external build system.
#
# mutos_c0 (lex/parse/typecheck -> temp1/temp2 intermediate code) is a
# port of the original MUTOS 1700 c0 - v7's c0, ported to the 8086 -
# read from its binary (c0port/, docs/C0_PORT_PLAN.md) and checked
# against the original run under an 8086 emulator ("make check-realbin",
# tests/realbin/). c0port/c0data.c, the original's initialized data
# segment, is generated from the binary by tests/realbin/c0tables.py --c
# and checked in.
#
# mutos_c1 (temp1/temp2 -> mutos_as-syntax assembly text) is a port of
# the original MUTOS 1700 c1 - v7's c1, ported to the 8086 by hand -
# read from its binary (c1port/, docs/C1_PORT_PLAN.md) and checked
# against the original run under an 8086 emulator ("make check-realbin",
# tests/realbin/). c1port/c1tab.c, its code tables, is generated from the
# binary by tests/realbin/c1tables.py --c and checked in, so that a build
# needs no Python.
#
# mutos_cc (the driver that runs cpp, c0, c1, c2, as and ld in sequence)
# is a port of the original MUTOS 1700 cc - v7's cc with the MUTOS
# deltas - read from its binary (ccport/, docs/TOOLCHAIN_PLAN.md, step 3)
# and checked against the original run under an 8086 emulator ("make
# check-realbin", tests/realbin/cccmp.py). ccport/ccdata.c, the
# original's initialized segment (text and data: cc is 0407), is
# generated from the binary by tests/realbin/cctables.py --c and checked
# in. It finds its passes through the MUTOS root ($MUTOS_ROOT/lib/c0, ...).
#
# mutos_c2 (the optimizer that "cc -O" runs between c1 and as) is a port
# of the original MUTOS 1700 c2 - v7's c2 rewritten for the 8086, with
# passes of its own - read from its binary (c2port/, docs/TOOLCHAIN_PLAN.md,
# step 4) and checked against the original run under an 8086 emulator
# ("make check-realbin", tests/realbin/c2cmp.py). c2port/c2data.c, the
# original's initialized data segment, is generated from the binary by
# tests/realbin/c2tables.py --c and checked in.
#
# See README.md and STATUS.md for the currently-verified results.

CC      = cc
# The MUTOS root's compiled-in default for src/h/mutos_root.h
# (docs/TOOLCHAIN_PLAN.md, decision 2): the build tree's mutos_root/.
# The top-level Makefile passes the same path.
MUTOS_ROOT_DEFAULT ?= $(abspath $(CURDIR)/../../mutos_root)
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g \
          -DMUTOS_ROOT_DEFAULT='"$(MUTOS_ROOT_DEFAULT)"'

CC_SRCS = ccport/main.c ccport/cc.c ccport/cclib.c ccport/ccdata.c
CC_OBJS = $(CC_SRCS:.c=.o)
CC_TARGET = mutos_cc

C0_SRCS = c0port/c00.c c0port/c01.c c0port/c02.c c0port/c03.c \
          c0port/c04.c c0port/c0data.c c0port/c0lib.c
C0_OBJS = $(C0_SRCS:.c=.o)
C0_TARGET = mutos_c0

C2_SRCS = c2port/main.c c2port/c20.c c2port/c21.c c2port/c2lib.c \
          c2port/c2data.c
C2_OBJS = $(C2_SRCS:.c=.o)
C2_TARGET = mutos_c2

C1_PORT = c1port/c10.c c1port/c11.c c1port/c12.c c1port/c1tab.c c1port/c1lib.c
# c1_fltdec.c: libc.a's ecvt() for c1's "%.17e"; mutos_as's
# floating-constant conversion (libc.a's atof(), exactly) compiled in as
# it is - see c1port/c1lib.c.
C1_SRCS = $(C1_PORT) c1_fltdec.c
FLTCONST = ../mutos_as/fltconst.c
C1_OBJS = $(C1_SRCS:.c=.o) c1_fltconst.o
C1_TARGET = mutos_c1

# fltdec_test: mutos_c1's floating-constant text for literals read from
# stdin (c1_fltdec.c) - compared with libc.a's own ecvt() by "make
# check-libcatof" (tests/mutos_as/float_coverage/libcatof.py ecvt).
FLTDEC_TEST = fltdec_test

all: $(CC_TARGET) $(C0_TARGET) $(C1_TARGET) $(C2_TARGET) $(FLTDEC_TEST)

$(CC_TARGET): $(CC_OBJS)
	$(CC) $(CFLAGS) -o $@ $(CC_OBJS)

$(FLTDEC_TEST): fltdec_test.o c1_fltdec.o c1_fltconst.o
	$(CC) -o $@ fltdec_test.o c1_fltdec.o c1_fltconst.o

$(C0_TARGET): $(C0_OBJS)
	$(CC) $(CFLAGS) -o $@ $(C0_OBJS)

$(C1_TARGET): $(C1_OBJS)
	$(CC) $(CFLAGS) -o $@ $(C1_OBJS)

$(C2_TARGET): $(C2_OBJS)
	$(CC) $(CFLAGS) -o $@ $(C2_OBJS)

$(CC_OBJS): ccport/cc.h
ccport/cclib.o: ../h/mutos_root.h
$(C0_OBJS): c0port/c0.h
$(C1_PORT:.c=.o): c1port/c1.h
$(C2_OBJS): c2port/c2.h
c1port/c1lib.o: ../mutos_as/fltconst.h c1_fltdec.h
c1_fltdec.o: c1_fltdec.c c1_fltdec.h ../mutos_as/fltconst.h
fltdec_test.o: fltdec_test.c c1_fltdec.h
c1_fltconst.o: $(FLTCONST) ../mutos_as/fltconst.h
	$(CC) $(CFLAGS) -c -o $@ $(FLTCONST)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(CC_OBJS) $(CC_TARGET) \
	      $(C0_OBJS) $(C1_OBJS) $(C0_TARGET) $(C1_TARGET) \
	      $(C2_OBJS) $(C2_TARGET) \
	      fltdec_test.o $(FLTDEC_TEST)

.PHONY: all clean
