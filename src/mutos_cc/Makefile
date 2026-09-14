# Makefile - MUTOS 1700 Cross C Compiler front end/back end
# (mutos_c0 / mutos_c1)
#
# Builds on a Linux host, matching mutos_as/mutos_cpp/mutos_ld's
# style/tooling: plain C11, gcc/clang, no external build system.
#
# mutos_c0 (lex/parse/typecheck -> temp1/temp2 intermediate code) and
# mutos_c1 (temp1/temp2 -> mutos_as-syntax assembly text) are a
# from-scratch, modern-C11 reimplementation - not a literal port of
# v7/cc's pointer-arithmetic K&R source - validated byte-for-byte
# against real-hardware golden output, matching this project's
# established approach for mutos_as/mutos_cpp/mutos_ld. See README.md
# for current grammar/opcode coverage and STATUS.md for the
# currently-verified golden-diff results.
#
# mutos_cc (the driver that runs cpp|c0|c1|as|ld in sequence) and
# mutos_c2 (the peephole optimizer, Milestone 5) are not yet part of
# this Makefile - see README.md's "Next steps".

CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g

C0_SRCS = c0_lex.c c0_diag.c c0_outcode.c c0_sym.c c0_parser.c c0_main.c
C0_OBJS = $(C0_SRCS:.c=.o)
C0_TARGET = mutos_c0

C1_SRCS = c1_stream.c c1_gen.c c1_main.c
C1_OBJS = $(C1_SRCS:.c=.o)
C1_TARGET = mutos_c1

all: $(C0_TARGET) $(C1_TARGET)

$(C0_TARGET): $(C0_OBJS)
	$(CC) -o $@ $(C0_OBJS)

$(C1_TARGET): $(C1_OBJS)
	$(CC) -o $@ $(C1_OBJS)

c0_lex.o: c0_lex.c c0_lex.h c0_diag.h mutos_cc.h
c0_diag.o: c0_diag.c c0_diag.h
c0_outcode.o: c0_outcode.c c0_outcode.h mutos_cc.h
c0_sym.o: c0_sym.c c0_sym.h mutos_cc.h
c0_parser.o: c0_parser.c c0_parser.h c0_lex.h c0_diag.h c0_outcode.h c0_sym.h mutos_cc.h
c0_main.o: c0_main.c c0_parser.h c0_diag.h

c1_stream.o: c1_stream.c c1_stream.h
c1_gen.o: c1_gen.c c1_gen.h c1_stream.h mutos_cc.h
c1_main.o: c1_main.c c1_gen.h

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(C0_OBJS) $(C1_OBJS) $(C0_TARGET) $(C1_TARGET)

.PHONY: all clean
