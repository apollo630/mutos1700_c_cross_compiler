# Makefile - MUTOS 1700 Cross-Assembler (mutos_as)
#
# Builds on a Linux host, matching mutos_ld.c's style/tooling: plain
# C11, gcc/clang, no external build system.
#
# `mutos_as` is the real assembler: it reads a .s source file and
# writes a genuine MUTOS a.out relocatable object file (matching
# mutos_aout.h - header, text, data, relocation tables, symbol table),
# e.g. `mutos_as malloc.s` produces `malloc.o`. Validated BYTE-FOR-BYTE,
# full-file (header + text + data + text-reloc + data-reloc + symtab),
# against six real hardware-linked golden object files: malloc.o,
# mch.o, sys1nonopt.o, sys1opt.o, v30opt.o, tty.o. This includes the
# symbol table's exact output ORDER (creation order - the order each
# symbol is first mentioned, as either a reference or a definition,
# during the Pass 1 scan), every relocation entry (both PC-relative
# CALL/JMP/branch targets and non-PC-relative absolute references to
# a TEXT/DATA/BSS/external symbol's address), a real ".bss" segment
# (v30opt.s, via ".bss"/".blkb"), bare-identifier implicit data-value
# statements (tty.s, e.g. jump/pointer tables built from plain label
# names with no directive at all), and indirect CALL/JMP/branch
# through a computed memory address ("@disp(reg)", e.g. tty.s's
# "call @8.+_cdevsw(bx)" device-switch-table dispatch).
#
# Instruction coverage was additionally cross-checked against the
# COMPLETE Assembler_as.pdf Anlage A instruction table (all 5 pages,
# ~160 distinct mnemonics) and Anlage B branch table, not just the
# real corpus's actual usage - every documented mnemonic is either
# implemented or is a confirmed-real alternate-name divergence (e.g.
# real code uses "stow"/"stob"/"lodw"/"lodb" and "blos", not Anlage
# A's literal "stos"/"stosb"/"lods"/"lodsb"/"bloss" spellings). The
# one deliberate, documented gap is CALLI/JMPI's direct "d:s"
# (segment:offset literal) operand form, which needs an operand syntax
# (a colon-separated pair) neither the lexer nor the operand
# classifier currently parse at all, and which no real corpus sample
# ever uses - the indirect ("@reg"/"@disp(reg)") form of both is fully
# implemented. Newly-added opcodes with no real corpus sample (mostly
# standard, unambiguous 8086 opcodes with no MUTOS-specific encoding
# quirk possible) were spot-checked via a hand-verified smoke test
# comparing the assembler's raw byte output against a manual 8086 ISA
# encoding, not just "doesn't crash / doesn't break existing goldens".
#
# Two secondary developer tools are also built:
#   parse_dump     - Step 1 debug tool: dumps parsed statements only,
#                    no encoding (formerly misleadingly built AS
#                    "mutos_as" itself - renamed to avoid confusion
#                    with the real assembler above).
#   classify_test  - Step 2 debug tool: dumps operand-classification
#                    results (addressing modes, expressions).

# Instruction coverage was additionally cross-checked against the
# COMPLETE Assembler_as.pdf Anlage A instruction table (all 5 pages,
# ~160 distinct mnemonics) and Anlage B branch table, not just the
# real corpus's actual usage - every documented mnemonic is either
# implemented or is a confirmed-real alternate-name divergence (e.g.
# real code uses "stow"/"stob"/"lodw"/"lodb" and "blos", not Anlage
# A's literal "stos"/"stosb"/"lods"/"lodsb"/"bloss" spellings). The
# one deliberate, documented gap is CALLI/JMPI's direct "d:s"
# (segment:offset literal) operand form, which needs an operand syntax
# (a colon-separated pair) neither the lexer nor the operand
# classifier currently parse at all, and which no real corpus sample
# ever uses - the indirect ("@reg"/"@disp(reg)") form of both is fully
# implemented. Newly-added opcodes with no real corpus sample (mostly
# standard, unambiguous 8086 opcodes with no MUTOS-specific encoding
# quirk possible) were spot-checked via a hand-verified smoke test
# comparing the assembler's raw byte output against a manual 8086 ISA
# encoding, not just "doesn't crash / doesn't break existing goldens".
#
# Command-line interface is based on as_1.d (the real as/asf man
# page), with one deliberate deviation: "-o output" (output path,
# defaults to "a.out") and "-W" (suppress printed error messages;
# failures are still tracked and still block a successful build) match
# as_1.d exactly, as does multiple file arguments (concatenated as one
# continuous source), a stdin fallback when no files are given, and
# marking the output file executable (0755) only when no symbol
# remains unresolved, non-executable (0644) otherwise. The DEVIATION:
# as_1.d also documents a "-L" switch (include compiler-internal
# 'L'-prefixed labels in the written symbol table; excluded by
# default without it) - this project does NOT implement it as a
# switch. Every golden reference .o this project validates against has
# those labels present unconditionally, with no flag involved, so
# 'L'-prefixed labels are always included here too, matching the
# goldens - literal switch-for-switch parity with as_1.d was
# deliberately dropped in favor of always matching the golden files
# with a bare `mutos_as file.s`, no flags required. "asf" (a second
# program name, not a switch, for K1810WM87/8087 coprocessor programs
# per as_1.d) is recognized via argv[0] purely so invoking the binary
# under that name works rather than erroring - it has no other effect,
# since no project material documents a single concrete 8087
# instruction mnemonic or encoding to implement.

CC      ?= cc
CSTD    := -std=c11
WARN    := -Wall -Wextra -Wpedantic
OPT     := -O2 -g
CFLAGS  := $(CSTD) $(WARN) $(OPT)
LDFLAGS :=

ASM_SRCS      := lexer.c parser.c pass2.c symtab.c encode.c objwrite.c assemble.c
ASM_OBJS      := $(ASM_SRCS:.c=.o)
PARSE_SRCS    := lexer.c parser.c main.c
PARSE_OBJS    := $(PARSE_SRCS:.c=.o)
CLASSIFY_SRCS := lexer.c parser.c pass2.c classify_test.c
CLASSIFY_OBJS := $(CLASSIFY_SRCS:.c=.o)
HDRS          := mutos_as.h pass2.h encode.h symtab.h objwrite.h mutos_aout.h

TARGET    := mutos_as
PARSE     := parse_dump
CLASSIFY  := classify_test

# Real c1-generated .s corpus used to validate the assembler end-to-end
# (not shipped in this archive - drop your own .s files here, or the
# four golden-validated ones, to use these targets).
TEST_SRCS := malloc.s mch.s sys1nonopt.s sys1opt.s v30opt.s tty.s

.PHONY: all clean test test1 test2 distclean

all: $(TARGET) $(PARSE) $(CLASSIFY)

$(TARGET): $(ASM_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(ASM_OBJS)

$(PARSE): $(PARSE_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(PARSE_OBJS)

$(CLASSIFY): $(CLASSIFY_OBJS)
	$(CC) $(LDFLAGS) -o $@ $(CLASSIFY_OBJS)

%.o: %.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# Assembles every available file from TEST_SRCS into a real .o, so
# `make test` still works in a checkout that only has a subset of the
# corpus. Uses "-o <file>.o" so each file gets its own output instead
# of every iteration clobbering a shared "a.out".
test: $(TARGET)
	@for f in $(TEST_SRCS); do \
		[ -f "$$f" ] && ./$(TARGET) -o "$${f%.s}.o" "$$f"; \
	done

# Runs the Step 1 statement-parser dump over the same corpus.
test1: $(PARSE)
	@files=""; \
	for f in $(TEST_SRCS); do \
		[ -f "$$f" ] && files="$$files $$f"; \
	done; \
	if [ -z "$$files" ]; then \
		echo "no test .s files found in $$(pwd) (expected one or more of: $(TEST_SRCS))"; \
		exit 1; \
	fi; \
	./$(PARSE) $$files

# Runs the Step 2 operand-classifier dump over the same corpus.
test2: $(CLASSIFY)
	@files=""; \
	for f in $(TEST_SRCS); do \
		[ -f "$$f" ] && files="$$files $$f"; \
	done; \
	if [ -z "$$files" ]; then \
		echo "no test .s files found in $$(pwd) (expected one or more of: $(TEST_SRCS))"; \
		exit 1; \
	fi; \
	./$(CLASSIFY) $$files

clean:
	rm -f $(ASM_OBJS) $(PARSE_OBJS) $(CLASSIFY_OBJS)

distclean: clean
	rm -f $(TARGET) $(PARSE) $(CLASSIFY)
