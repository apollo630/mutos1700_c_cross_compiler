# Makefile - MUTOS 1700 Cross Make (mutos_make)
#
# mutos_make is a port of the original MUTOS 1700 make binary (v7's make
# with the MUTOS representation, the text offsets of the original's
# functions in the comments), read from mutos/mutos_bin/make.golden.base64.txt
# with v7/make/ as the skeleton. makedata.c (the tables of yacc's parser and
# the built-in rules) is generated from the binary by
# tests/realbin/maketables.py --c and checked in. See README.md.

CC      = cc
# The MUTOS root's compiled-in default for src/h/mutos_root.h
# (docs/TOOLCHAIN_PLAN.md, decision 2): the build tree's mutos_root/.
# The top-level Makefile passes the same path.
MUTOS_ROOT_DEFAULT ?= $(abspath $(CURDIR)/../../mutos_root)
# -fsigned-char: the original's char is signed, and the code depends on it.
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g -fsigned-char \
          -D_GNU_SOURCE \
          -DMUTOS_ROOT_DEFAULT='"$(MUTOS_ROOT_DEFAULT)"'
SRCS    = main.c doname.c misc.c files.c dosys.c gram.c makedata.c mklib.c
OBJS    = $(SRCS:.c=.o)
TARGET  = mutos_make

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)

mklib.o: ../h/mutos_root.h

%.o: %.c make.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
