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
#   3. With -1 the one line on the standard error that reports the
#      80186/V30 instructions (docs/TOOLCHAIN_PLAN.md, decision 16) is the
#      one in v30_speculative/v30_with_1.report - its counts checked on
#      2026-11-08 against objdump -m i8086 on the objects' text, by opcode.
#
# -C (found 2026-10-25 by running the original, tests/realbin/ascmp.py): a
# symbol that is defined, not .globl and starts with a capital L is left out
# of the symbol table; the others stay. The original's own runs are pinned by
# make check-realbin (ascmp_goldens.txt); here, on the host only, a small
# source: 4 symbols without -C (48 bytes), 2 with it (24 bytes).
#
# Besides the switches (since 2026-11-15): a short jump that does not reach
# its target is refused, as by the original.
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
    want="$(awk -F'\t' -v s="$base.s" '$1 == s { print $2 }' v30_with_1.report)"
    if [ "$(cat "$tmp/log")" != "$want" ]; then
        echo "FAIL  $base.s: -1 reports \"$(cat "$tmp/log")\", recorded \"$want\""
        bad=$((bad + 1))
    fi
    rm -f "$tmp/$name"
    if "$MUTOS_AS" "$base.s" -o "$tmp/$name" >"$tmp/log" 2>&1 && \
       [ "$(md5sum < "$tmp/$name" | cut -d' ' -f1)" = "$sum" ]; then
        echo "FAIL  $base.s: without -1 the 80186 object is made"
        bad=$((bad + 1))
    fi
done < v30_with_1.md5

# push of a byte constant: 6A ib is pushed sign-extended, so a "*n" outside
# -128 to 127 is refused (with a message that says so), its ends are taken.
for v in '*200.' '*128.' '*-129.' '*/80'; do
    printf '.text\n\tpush\t%s\n' "$v" > "$tmp/p.s"
    if "$MUTOS_AS" -1 "$tmp/p.s" -o "$tmp/p.o" >"$tmp/log" 2>&1 || \
       ! grep -q 'outside -128 to 127' "$tmp/log"; then
        echo "FAIL  push $v: not refused as a byte out of range: $(head -n 1 "$tmp/log")"
        bad=$((bad + 1))
    fi
done
for v in '*127.' '*-128.' '#200.'; do
    printf '.text\n\tpush\t%s\n' "$v" > "$tmp/p.s"
    if ! "$MUTOS_AS" -1 "$tmp/p.s" -o "$tmp/p.o" >"$tmp/log" 2>&1; then
        echo "FAIL  push $v: refused: $(head -n 1 "$tmp/log")"
        bad=$((bad + 1))
    fi
done

# imul by a byte constant: 6B ib is multiplied sign-extended - the same.
for o in 'cx,ax,*200.' 'ax,*128.' 'dx,*-129.' 'bx,(si),*/80'; do
    printf '.text\n\timul\t%s\n' "$o" > "$tmp/p.s"
    if "$MUTOS_AS" -1 "$tmp/p.s" -o "$tmp/p.o" >"$tmp/log" 2>&1 || \
       ! grep -q 'imul by a byte constant .* outside -128 to 127' "$tmp/log"; then
        echo "FAIL  imul $o: not refused as a byte out of range: $(head -n 1 "$tmp/log")"
        bad=$((bad + 1))
    fi
done
for o in 'cx,ax,*127.' 'ax,*-128.' 'cx,ax,#200.' 'bx,(si),*/7F'; do
    printf '.text\n\timul\t%s\n' "$o" > "$tmp/p.s"
    if ! "$MUTOS_AS" -1 "$tmp/p.s" -o "$tmp/p.o" >"$tmp/log" 2>&1; then
        echo "FAIL  imul $o: refused: $(head -n 1 "$tmp/log")"
        bad=$((bad + 1))
    fi
done

# A shift by a constant other than 1 is one byte longer than the shift by 1:
# the labels after it (pass 1) are where pass 2 writes the code. "sal ax,*3."
# is C1 E0 03, so the "je" at 3 back to 0 is 74 FB; with a word of address
# ("sal _g,*7.", 5 bytes) 74 F9.
printf '.text\nLa:\tsal\tax,*3.\nLb:\tje\tLa\nLc:\tsal\t_g,*7.\n\tje\tLc\n.comm _g,2\n' > "$tmp/l.s"
if ! "$MUTOS_AS" -1 "$tmp/l.s" -o "$tmp/l.o" >"$tmp/log" 2>&1 || \
   [ "$(od -An -tx1 -j16 -N12 "$tmp/l.o" | tr -d ' \n')" != "c1e00374fbc12600000774f9" ]; then
    echo "FAIL  labels after a shift by a constant: $(od -An -tx1 -j16 -N12 "$tmp/l.o" 2>/dev/null) $(head -n 1 "$tmp/log")"
    bad=$((bad + 1))
fi
# a count that is a symbol: 1 is taken, any other value refused (its length
# is not known in pass 1)
printf 'N=3\n.text\n\tsal\tax,*N\n' > "$tmp/p.s"
if "$MUTOS_AS" -1 "$tmp/p.s" -o "$tmp/p.o" >"$tmp/log" 2>&1 || \
   ! grep -q 'not known in pass 1' "$tmp/log"; then
    echo "FAIL  sal ax,*N with N=3: not refused: $(head -n 1 "$tmp/log")"
    bad=$((bad + 1))
fi
printf 'N=1\n.text\n\tsal\tax,*N\n' > "$tmp/p.s"
if ! "$MUTOS_AS" -1 "$tmp/p.s" -o "$tmp/p.o" >"$tmp/log" 2>&1; then
    echo "FAIL  sal ax,*N with N=1: refused: $(head -n 1 "$tmp/log")"
    bad=$((bad + 1))
fi

# A short jump (the conditional jumps, j, the loops, jcxz) reaches 127 bytes
# ahead and 128 back; one that does not reach is refused, as the original
# as refuses it (measured, tests/realbin/ascmp.py cases, "short jump"), with
# or without -1. Before 2026-11-15 the low byte of the distance was written.
nops() { i=0; while [ "$i" -lt "$1" ]; do printf '\tnop\n'; i=$((i + 1)); done; }
for op in je j loop jcxz; do
    for c in "ahead 127 ok" "ahead 128 refused" "back 126 ok" "back 127 refused"; do
        set -- $c
        if [ "$1" = ahead ]; then
            { printf '.text\n\t%s\tL1\n' "$op"; nops "$2"; printf 'L1:\tnop\n'; } > "$tmp/j.s"
        else
            { printf '.text\nL1:\n'; nops "$2"; printf '\t%s\tL1\n\tnop\n' "$op"; } > "$tmp/j.s"
        fi
        "$MUTOS_AS" "$tmp/j.s" -o "$tmp/j.o" >"$tmp/log" 2>&1
        st=$?
        if [ "$3" = ok ] && [ "$st" -ne 0 ]; then
            echo "FAIL  $op over $2 bytes $1: refused: $(head -n 1 "$tmp/log")"
            bad=$((bad + 1))
        elif [ "$3" = refused ] && { [ "$st" -eq 0 ] || ! grep -q 'short jump out of range' "$tmp/log"; }; then
            echo "FAIL  $op over $2 bytes $1: not refused: $(head -n 1 "$tmp/log")"
            bad=$((bad + 1))
        fi
    done
done
# the displacement at the two ends: 7F ahead, 80 back
{ printf '.text\n\tje\tL1\n'; nops 127; printf 'L1:\tnop\n'; } > "$tmp/j.s"
"$MUTOS_AS" "$tmp/j.s" -o "$tmp/j.o" >/dev/null 2>&1
{ printf '.text\nL1:\n'; nops 126; printf '\tje\tL1\n'; } > "$tmp/k.s"
"$MUTOS_AS" "$tmp/k.s" -o "$tmp/k.o" >/dev/null 2>&1
if [ "$(od -An -tx1 -j16 -N2 "$tmp/j.o" | tr -d ' \n')" != "747f" ] || \
   [ "$(od -An -tx1 -j142 -N2 "$tmp/k.o" | tr -d ' \n')" != "7480" ]; then
    echo "FAIL  short jump at the ends of its range: $(od -An -tx1 -j16 -N2 "$tmp/j.o") $(od -An -tx1 -j142 -N2 "$tmp/k.o")"
    bad=$((bad + 1))
fi

# A short jump to a symbol the file does not define - undefined, .comm, or
# either plus a number - is refused too, as by the original ("Short jump to
# external not allowed"); a label of the file and a local label are taken.
for c in 'je _ext' 'j _ext' 'loop _ext' 'jcxz _ext' 'je _ext+2' 'je c'; do
    set -- $c
    printf '.text\n\t%s\t%s\n\tnop\n.comm c,2\n' "$1" "$2" > "$tmp/j.s"
    if "$MUTOS_AS" "$tmp/j.s" -o "$tmp/j.o" >"$tmp/log" 2>&1 || \
       ! grep -q 'short jump to an external symbol' "$tmp/log"; then
        echo "FAIL  $c: not refused: $(head -n 1 "$tmp/log")"
        bad=$((bad + 1))
    fi
done
printf '.text\nL1:\tnop\n\tje\tL1\n\tje\t1f\n1:\tje\t1b\n\tje\tL2+1\n\tnop\nL2:\tnop\n' > "$tmp/j.s"
if ! "$MUTOS_AS" "$tmp/j.s" -o "$tmp/j.o" >"$tmp/log" 2>&1; then
    echo "FAIL  short jumps to labels of the file: refused: $(head -n 1 "$tmp/log")"
    bad=$((bad + 1))
fi

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
    echo "switches: -1 gives the recorded object and report of $n/$n sources (not without it), and refuses push of and imul by a byte constant outside -128 to 127, and puts the labels after a shift by a constant where the code is; a short jump that does not reach is refused; -C leaves out the local L symbols"
    exit 0
fi
echo "switches: $bad failures"
exit 1
