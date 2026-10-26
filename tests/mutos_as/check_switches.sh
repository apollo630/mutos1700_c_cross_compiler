#!/usr/bin/env bash
#
# check_switches.sh - mutos_as's switches -1 and -C.
#
# -1 (docs/TOOLCHAIN_PLAN.md, section 4):
#
# The seven sources in v30_speculative/ use the 80186/V30 instructions the original as does
# not have. Two things must hold:
#
#   1. With -1 each assembles, and to the object recorded in
#      v30_speculative/v30_with_1.md5
#      (the bytes of 2026-10-20, before the switch existed, checked against
#      the build of the day before; independently decoded and cross-checked
#      against NASM, see README.md).
#   2. Without -1 the object is not that one: the mnemonics are words
#      referring to undefined externals and the 80186 forms of push, imul
#      and the shifts are refused, as the original does (AS-02 in
#      docs/MUTOS_ORIGINAL_BUGS.md; tests/realbin/ascmp.py compares the
#      two runs with the emulated original).
#
# -C (found 2026-10-25 by running the original, tests/realbin/ascmp.py): a
# symbol that is defined, not .globl and starts with a capital L is left out
# of the symbol table; the others stay. The original's own runs are pinned by
# make check-realbin (ascmp_goldens.txt); here, on the host only, a small
# source: 4 symbols without -C (48 bytes), 2 with it (24 bytes).
#
# Usage: MUTOS_AS=../../src/mutos_as/mutos_as ./check_switches.sh
# Exit status 0 if everything holds.

set -u
cd "$(dirname "$0")/v30_speculative"
MUTOS_AS="${MUTOS_AS:-mutos_as}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

bad=0
n=0
while read -r sum name; do
    base="${name%.o}"
    n=$((n + 1))
    if ! "$MUTOS_AS" -1 "$base.s" -o "$tmp/$name" >"$tmp/log" 2>&1; then
        echo "FAIL  $base.s: -1 does not assemble: $(tail -n 1 "$tmp/log")"
        bad=$((bad + 1))
        continue
    fi
    got="$(md5sum < "$tmp/$name" | cut -d' ' -f1)"
    if [ "$got" != "$sum" ]; then
        echo "FAIL  $base.s: -1 gives another object ($got, recorded $sum)"
        bad=$((bad + 1))
    fi
    rm -f "$tmp/$name"
    if "$MUTOS_AS" "$base.s" -o "$tmp/$name" >"$tmp/log" 2>&1 && \
       [ "$(md5sum < "$tmp/$name" | cut -d' ' -f1)" = "$sum" ]; then
        echo "FAIL  $base.s: without -1 the 80186 object is made"
        bad=$((bad + 1))
    fi
done < v30_with_1.md5

# -C: the symbol table's size is in the header (a_syms, bytes 8 and 9).
printf '.globl _f\n.text\n_f:\tjmp\tL1\nL1:\tret\n.data\nLd:\t.word\t1\n_d:\t.word\t2\n' > "$tmp/c.s"
syms() { od -An -tu2 -j8 -N2 "$1" | tr -d ' '; }
"$MUTOS_AS" "$tmp/c.s" -o "$tmp/c0.o" >/dev/null 2>&1
"$MUTOS_AS" -C "$tmp/c.s" -o "$tmp/c1.o" >/dev/null 2>&1
"$MUTOS_AS" -gC "$tmp/c.s" -o "$tmp/c2.o" >/dev/null 2>&1
if [ "$(syms "$tmp/c0.o")" != 48 ] || [ "$(syms "$tmp/c1.o")" != 24 ] || \
   [ "$(syms "$tmp/c2.o")" != 24 ]; then
    echo "FAIL  -C: symbol table bytes $(syms "$tmp/c0.o") / $(syms "$tmp/c1.o") / $(syms "$tmp/c2.o"), expected 48 / 24 / 24"
    bad=$((bad + 1))
fi

if [ "$bad" -eq 0 ]; then
    echo "switches: -1 gives the recorded object of $n/$n sources (not without it); -C leaves out the local L symbols"
    exit 0
fi
echo "switches: $bad failures"
exit 1
