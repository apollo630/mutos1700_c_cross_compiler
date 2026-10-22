# Makefile - MUTOS 1700 Cross Linker (mutos_ld)
#
# mutos_ld is a port of the original MUTOS 1700 ld binary (v7's ld with
# the MUTOS deltas), read from mutos_bin/ld.golden.base64.txt with
# v7/ld/ld.c as the skeleton; it runs on an image of the original's data
# segment (ld.h). lddata.c is generated from the binary by
# tests/realbin/ldtables.py --c and checked in. See README.md.

CC      = cc
# The MUTOS root's compiled-in default for src/h/mutos_root.h
# (docs/TOOLCHAIN_PLAN.md, decision 2): the build tree's mutos_root/.
# The top-level Makefile passes the same path.
MUTOS_ROOT_DEFAULT ?= $(abspath $(CURDIR)/../../mutos_root)
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g \
          -DMUTOS_ROOT_DEFAULT='"$(MUTOS_ROOT_DEFAULT)"'
SRCS    = main.c ld.c ldlib.c lddata.c
OBJS    = $(SRCS:.c=.o)
TARGET  = mutos_ld

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)

ldlib.o: ../h/mutos_root.h

%.o: %.c ld.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
