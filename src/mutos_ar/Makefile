# Makefile - MUTOS 1700 Cross Archiver (mutos_ar)
#
# mutos_ar is a port of the original MUTOS 1700 ar binary (v7's ar.c, the
# text offsets of the original's functions in the comments), read from
# mutos/mutos_bin/ar.golden.base64.txt with v7/ar/ar.c as the skeleton.
# ardata.c (libc.a's error texts) is generated from the binary by
# tests/realbin/artables.py --c and checked in. See README.md.

CC      = cc
# The MUTOS root's compiled-in default for src/h/mutos_root.h
# (docs/TOOLCHAIN_PLAN.md, decision 2): the build tree's mutos_root/.
# The top-level Makefile passes the same path.
MUTOS_ROOT_DEFAULT ?= $(abspath $(CURDIR)/../../mutos_root)
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g \
          -DMUTOS_ROOT_DEFAULT='"$(MUTOS_ROOT_DEFAULT)"'
SRCS    = ar.c arlib.c ardata.c
OBJS    = $(SRCS:.c=.o)
TARGET  = mutos_ar

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)

arlib.o ar.o: ../h/mutos_root.h

%.o: %.c ar.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
