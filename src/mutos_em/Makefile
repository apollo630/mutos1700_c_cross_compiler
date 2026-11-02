# Makefile - MUTOS 1700 Cross Extended Text Editor (mutos_em)
#
# mutos_em is a port of the original MUTOS 1700 em binary - the original ed
# with a new main() and commands(), the text offset of the original's
# function in the comment -, read from mutos/mutos_bin/em.golden.base64.txt
# with src/mutos_ed/ed.c as the skeleton. See README.md.

CC      = cc
# The MUTOS root's compiled-in default for src/h/mutos_root.h
# (docs/TOOLCHAIN_PLAN.md, decision 2): the build tree's mutos_root/.
# The top-level Makefile passes the same path.
MUTOS_ROOT_DEFAULT ?= $(abspath $(CURDIR)/../../mutos_root)
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -O2 -g \
          -DMUTOS_ROOT_DEFAULT='"$(MUTOS_ROOT_DEFAULT)"'
SRCS    = em.c
OBJS    = $(SRCS:.c=.o)
TARGET  = mutos_em

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS)

%.o: %.c ../h/mutos_stdio.h ../h/mutos_root.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
