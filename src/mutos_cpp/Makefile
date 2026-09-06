# Makefile - MUTOS 1700 Cross C Preprocessor (mutos_cpp)
#
# Builds on a Linux host, matching mutos_as.c/mutos_ld.c's style/
# tooling: plain C11, gcc/clang, no external build system.
#
# `mutos_cpp` re-implements the observable behavior of the real MUTOS
# 1700 / V7 "fast cpp" (v7/cpp/cpp.c, John F. Reiser, 1978) from
# scratch in modern C11 - not a literal port of the original K&R,
# pointer-arithmetic-heavy source. See STATUS.md for current
# byte-for-byte golden-verification status.

CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g
SRCS    = util.c source.c macro.c ifexpr.c directive.c scan.c main.c
OBJS    = $(SRCS:.c=.o)
TARGET  = mutos_cpp

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)

%.o: %.c mutos_cpp.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
