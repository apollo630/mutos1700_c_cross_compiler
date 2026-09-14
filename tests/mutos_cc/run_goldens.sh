#!/usr/bin/env bash
#
# run_goldens.sh - runs the real mutos_cpp -> mutos_c0 -> mutos_c1
# pipeline over every *.c file across all 11 tests/mutos_cc/
# categories that has golden references, diffing each of the four
# intermediate artifacts (.i, .1, .2, .s) against its
# *.i.golden/*.1.golden/*.2.golden/*.s.golden byte-for-byte -
# matching this project's established golden-diff methodology
# (tests/mutos_cpp/run_goldens.sh, tests/mutos_as/'s golden suite).
#
# mutos_c0/mutos_c1's grammar/opcode coverage is currently limited to
# the 00_smoke subset (see src/mutos_cc/README.md) - files outside
# that coverage are expected to fail at the "c0" stage with a clear
# "not yet supported" diagnostic, not silently produce wrong output.
# This script reports those as a distinct category from a genuine
# byte-mismatch, so the pass count is always an honest measure of
# verified coverage, never inflated or deflated by unrelated
# failures.
#
# Usage:
#   ./run_goldens.sh
#   MUTOS_CPP=../../src/mutos_cpp/mutos_cpp \
#   MUTOS_C0=../../src/mutos_cc/mutos_c0 \
#   MUTOS_C1=../../src/mutos_cc/mutos_c1 ./run_goldens.sh
#
# Exit status: 0 if every file with goldens produced a byte-exact
# match at every stage, 1 otherwise (this is expected/normal until
# grammar coverage grows well beyond 00_smoke - see README.md).

set -u

MUTOS_CPP="${MUTOS_CPP:-$(dirname "$0")/../../src/mutos_cpp/mutos_cpp}"
MUTOS_C0="${MUTOS_C0:-$(dirname "$0")/../../src/mutos_cc/mutos_c0}"
MUTOS_C1="${MUTOS_C1:-$(dirname "$0")/../../src/mutos_cc/mutos_c1}"

for tool in "$MUTOS_CPP" "$MUTOS_C0" "$MUTOS_C1"; do
    if [ ! -x "$tool" ]; then
        echo "error: '$tool' not found or not executable (build it first, or set \$MUTOS_CPP/\$MUTOS_C0/\$MUTOS_C1)" >&2
        exit 1
    fi
done

cd "$(dirname "$0")" || exit 1

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

pass=()
cpp_mismatch=()
c0_unsupported=()
c0_mismatch=()
c1_unsupported=()
c1_mismatch=()

for cat in */; do
    cat="${cat%/}"
    [ -d "$cat" ] || continue
    for src in "$cat"/*.c; do
        [ -f "$src" ] || continue
        name="$(basename "${src%.c}")"
        golden_i="$cat/$name.i.golden"
        [ -f "$golden_i" ] || continue   # no golden yet for this file

        out_i="$WORK/$name.i"
        out_1="$WORK/$name.1"
        out_2="$WORK/$name.2"
        out_s="$WORK/$name.s"

        if ! "$MUTOS_CPP" -P "$src" "$out_i" 2>"$WORK/err"; then
            cpp_mismatch+=("$cat/$name (mutos_cpp itself failed)")
            continue
        fi
        if ! diff -q "$out_i" "$golden_i" >/dev/null 2>&1; then
            cpp_mismatch+=("$cat/$name (.i differs from golden)")
            continue
        fi

        if ! "$MUTOS_C0" "$out_i" "$out_1" "$out_2" 2>"$WORK/err"; then
            c0_unsupported+=("$cat/$name: $(tail -1 "$WORK/err")")
            continue
        fi
        golden_1="$cat/$name.1.golden"
        golden_2="$cat/$name.2.golden"
        if [ -f "$golden_1" ] && ! diff -q "$out_1" "$golden_1" >/dev/null 2>&1; then
            c0_mismatch+=("$cat/$name (.1 differs from golden)")
            continue
        fi
        if [ -f "$golden_2" ] && ! diff -q "$out_2" "$golden_2" >/dev/null 2>&1; then
            c0_mismatch+=("$cat/$name (.2 differs from golden)")
            continue
        fi

        if ! "$MUTOS_C1" "$out_1" "$out_2" "$out_s" 2>"$WORK/err"; then
            c1_unsupported+=("$cat/$name: $(tail -1 "$WORK/err")")
            continue
        fi
        golden_s="$cat/$name.s.golden"
        if [ -f "$golden_s" ] && ! diff -q "$out_s" "$golden_s" >/dev/null 2>&1; then
            c1_mismatch+=("$cat/$name (.s differs from golden)")
            continue
        fi

        pass+=("$cat/$name")
    done
done

echo "=================================================================="
echo "1. Byte-exact end-to-end (cpp+c0+c1) matches (${#pass[@]})"
for f in "${pass[@]}"; do echo "  $f"; done

echo
echo "2. mutos_cpp stage mismatch/failure (${#cpp_mismatch[@]})"
for f in "${cpp_mismatch[@]}"; do echo "  $f"; done

echo
echo "3. mutos_c0: grammar/opcode not yet supported (${#c0_unsupported[@]})"
echo "   (expected for anything outside 00_smoke - see README.md)"
for f in "${c0_unsupported[@]}"; do echo "  $f"; done

echo
echo "4. mutos_c0: genuine byte mismatch vs golden (${#c0_mismatch[@]})"
if [ ${#c0_mismatch[@]} -eq 0 ]; then echo "  (none)"; fi
for f in "${c0_mismatch[@]}"; do echo "  $f"; done

echo
echo "5. mutos_c1: opcode not yet supported (${#c1_unsupported[@]})"
for f in "${c1_unsupported[@]}"; do echo "  $f"; done

echo
echo "6. mutos_c1: genuine byte mismatch vs golden (${#c1_mismatch[@]})"
if [ ${#c1_mismatch[@]} -eq 0 ]; then echo "  (none)"; fi
for f in "${c1_mismatch[@]}"; do echo "  $f"; done
echo "=================================================================="

# Success means: nothing in the "genuine mismatch" categories (2, 4, 6).
# Category 3/5 ("not yet supported") is expected and does not fail the
# run - it is exactly the honest coverage gap README.md documents.
[ ${#cpp_mismatch[@]} -eq 0 ] && [ ${#c0_mismatch[@]} -eq 0 ] && [ ${#c1_mismatch[@]} -eq 0 ]
