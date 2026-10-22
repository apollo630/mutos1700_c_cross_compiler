# Makefile - MUTOS 1700 Cross C Compiler front end/back end
# (mutos_c0 / mutos_c1)
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
# See README.md and STATUS.md for the currently-verified results.
#
# mutos_cc (the driver that runs cpp|c0|c1|as|ld in sequence) and
# mutos_c2 (the peephole optimizer, Milestone 5) are not yet part of
# this Makefile - see README.md's "Next steps".

CC      = cc
# The MUTOS root's compiled-in default for src/h/mutos_root.h
# (docs/TOOLCHAIN_PLAN.md, decision 2): the build tree's mutos_root/.
# The top-level Makefile passes the same path.
MUTOS_ROOT_DEFAULT ?= $(abspath $(CURDIR)/../../mutos_root)
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g \
          -DMUTOS_ROOT_DEFAULT='"$(MUTOS_ROOT_DEFAULT)"'

C0_SRCS = c0port/c00.c c0port/c01.c c0port/c02.c c0port/c03.c \
          c0port/c04.c c0port/c0data.c c0port/c0lib.c
C0_OBJS = $(C0_SRCS:.c=.o)
C0_TARGET = mutos_c0

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

all: $(C0_TARGET) $(C1_TARGET) $(FLTDEC_TEST)

$(FLTDEC_TEST): fltdec_test.o c1_fltdec.o c1_fltconst.o
	$(CC) -o $@ fltdec_test.o c1_fltdec.o c1_fltconst.o

$(C0_TARGET): $(C0_OBJS)
	$(CC) $(CFLAGS) -o $@ $(C0_OBJS)

$(C1_TARGET): $(C1_OBJS)
	$(CC) $(CFLAGS) -o $@ $(C1_OBJS)

$(C0_OBJS): c0port/c0.h
$(C1_PORT:.c=.o): c1port/c1.h
c1port/c1lib.o: ../mutos_as/fltconst.h c1_fltdec.h
c1_fltdec.o: c1_fltdec.c c1_fltdec.h ../mutos_as/fltconst.h
fltdec_test.o: fltdec_test.c c1_fltdec.h
c1_fltconst.o: $(FLTCONST) ../mutos_as/fltconst.h
	$(CC) $(CFLAGS) -c -o $@ $(FLTCONST)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(C0_OBJS) $(C1_OBJS) $(C0_TARGET) $(C1_TARGET) \
	      fltdec_test.o $(FLTDEC_TEST)

.PHONY: all clean
