# Makefile - MUTOS 1700 Cross C Preprocessor (mutos_cpp)
#
# mutos_cpp is a port of the original MUTOS 1700 cpp binary (Reiser's cpp,
# "cpp.c 1.6 12/12/86"), read from mutos/mutos_bin/cpp.golden.base64.txt with
# v7/cpp as the skeleton; it runs on an image of the original's data
# segment (cpp.h). cppdata.c is generated from the binary by
# tests/realbin/cpptables.py --c and checked in. See README.md.

CC      = cc
# The MUTOS root's compiled-in default for src/h/mutos_root.h
# (docs/TOOLCHAIN_PLAN.md, decision 2): the build tree's mutos_root/.
# The top-level Makefile passes the same path.
MUTOS_ROOT_DEFAULT ?= $(abspath $(CURDIR)/../../mutos_root)
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g \
          -DMUTOS_ROOT_DEFAULT='"$(MUTOS_ROOT_DEFAULT)"'
SRCS    = main.c cpp.c cpy.c cpplib.c cppdata.c
OBJS    = $(SRCS:.c=.o)
TARGET  = mutos_cpp

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)

cpplib.o: ../h/mutos_root.h

%.o: %.c cpp.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
