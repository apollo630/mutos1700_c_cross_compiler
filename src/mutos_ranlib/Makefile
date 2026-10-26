# Makefile - MUTOS 1700 Cross Archive Indexer (mutos_ranlib)
#
# mutos_ranlib is a port of the original MUTOS 1700 ranlib binary (v7's
# ranlib.c, the text offsets of the original's functions in the comments),
# read from mutos/mutos_bin/ranlib.golden.base64.txt with
# v7/ranlib/ranlib.c as the skeleton. See README.md.

CC      = cc
# The MUTOS root's compiled-in default for src/h/mutos_root.h
# (docs/TOOLCHAIN_PLAN.md, decision 2): the build tree's mutos_root/.
# The top-level Makefile passes the same path.
MUTOS_ROOT_DEFAULT ?= $(abspath $(CURDIR)/../../mutos_root)
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g \
          -DMUTOS_ROOT_DEFAULT='"$(MUTOS_ROOT_DEFAULT)"'
SRCS    = ranlib.c
OBJS    = $(SRCS:.c=.o)
TARGET  = mutos_ranlib

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)

%.o: %.c ../h/mutos_root.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
