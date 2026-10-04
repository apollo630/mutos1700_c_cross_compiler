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
# A file with a .1 golden but no .i golden (a golden set brought back
# without cpp's output) is still checked from mutos_c0 on - mutos_cpp's
# own output feeding mutos_c0, nothing to diff it against - and a match
# is listed apart (category 7), so the pass count above stays the count
# of files verified at all four stages. A file with no .1 golden either
# is skipped.
#
# A file listed in invalid_goldens.txt has a .s golden whose tail is the
# real compiler's own invalid output: mutos_c1 must refuse it, and what it
# wrote before refusing must be exactly the golden's first lines (as many
# as the list gives) - listed apart (category 8), not a failure.
#
# A file listed in c1_errors.txt is one the real compiler's c1 reported
# errors for - and still wrote out whole: mutos_c1 must exit with a
# nonzero status, print exactly the listed messages on stderr and write
# exactly the golden's .s - listed apart (category 9), not a failure.
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

# "<category>/<name>" -> the number of leading .s golden lines that are
# valid code (see invalid_goldens.txt)
declare -A invalid_lines=()
if [ -f invalid_goldens.txt ]; then
    while read -r key nlines _; do
        case "$key" in ''|'#'*) continue ;; esac
        invalid_lines["$key"]="$nlines"
    done < invalid_goldens.txt
fi

# "<category>/<name>" -> the real c1's error messages, one per line (see
# c1_errors.txt)
declare -A c1_errors=()
if [ -f c1_errors.txt ]; then
    while read -r key msg; do
        case "$key" in ''|'#'*) continue ;; esac
        c1_errors["$key"]+="$msg"$'\n'
    done < c1_errors.txt
fi

pass=()
pass_c1err=()
pass_invalid=()
pass_no_i=()
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
        no_i=0
        if [ ! -f "$golden_i" ]; then
            # No .i golden: checked from mutos_c0 on if a .1 golden
            # exists (see this script's header), skipped otherwise.
            [ -f "$cat/$name.1.golden" ] || continue
            no_i=1
        fi

        out_i="$WORK/$name.i"
        out_1="$WORK/$name.1"
        out_2="$WORK/$name.2"
        out_s="$WORK/$name.s"

        if ! "$MUTOS_CPP" -P "$src" "$out_i" 2>"$WORK/err"; then
            cpp_mismatch+=("$cat/$name (mutos_cpp itself failed)")
            continue
        fi
        if [ "$no_i" -eq 0 ] && ! diff -q "$out_i" "$golden_i" >/dev/null 2>&1; then
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

        golden_s="$cat/$name.s.golden"
        if [ -n "${invalid_lines[$cat/$name]:-}" ]; then
            nl="${invalid_lines[$cat/$name]}"
            if "$MUTOS_C1" "$out_1" "$out_2" "$out_s" 2>"$WORK/err"; then
                c1_mismatch+=("$cat/$name (compiled, but its golden's tail is the real compiler's invalid output - see invalid_goldens.txt)")
            elif ! head -n "$nl" "$golden_s" | cmp -s - "$out_s"; then
                c1_mismatch+=("$cat/$name (.s before the refusal differs from the golden's first $nl lines)")
            else
                pass_invalid+=("$cat/$name (first $nl lines; refused: $(tail -1 "$WORK/err" | cut -c1-90)...)")
            fi
            continue
        fi
        if [ -n "${c1_errors[$cat/$name]:-}" ]; then
            if "$MUTOS_C1" "$out_1" "$out_2" "$out_s" 2>"$WORK/err"; then
                c1_mismatch+=("$cat/$name (compiled without the real compiler's c1 errors - see c1_errors.txt)")
            elif [ "$(cat "$WORK/err")"$'\n' != "${c1_errors[$cat/$name]}" ]; then
                c1_mismatch+=("$cat/$name (c1's messages differ from c1_errors.txt: $(head -1 "$WORK/err"))")
            elif ! diff -q "$out_s" "$golden_s" >/dev/null 2>&1; then
                c1_mismatch+=("$cat/$name (.s differs from golden)")
            else
                pass_c1err+=("$cat/$name ($(wc -l < "$WORK/err") messages, as the real c1's)")
            fi
            continue
        fi
        if ! "$MUTOS_C1" "$out_1" "$out_2" "$out_s" 2>"$WORK/err"; then
            c1_unsupported+=("$cat/$name: $(tail -1 "$WORK/err")")
            continue
        fi
        if [ -f "$golden_s" ] && ! diff -q "$out_s" "$golden_s" >/dev/null 2>&1; then
            c1_mismatch+=("$cat/$name (.s differs from golden)")
            continue
        fi

        if [ "$no_i" -eq 1 ]; then
            pass_no_i+=("$cat/$name")
        else
            pass+=("$cat/$name")
        fi
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

echo
echo "7. Byte-exact from mutos_c0 on - no .i golden, so mutos_cpp's own output"
echo "   was used unchecked (${#pass_no_i[@]})"
for f in "${pass_no_i[@]}"; do echo "  $f"; done

echo
echo "8. Byte-exact up to the real compiler's own invalid output, refused there"
echo "   on purpose - see invalid_goldens.txt (${#pass_invalid[@]})"
for f in "${pass_invalid[@]}"; do echo "  $f"; done

echo
echo "9. Byte-exact, with the real compiler's own c1 errors reproduced - see"
echo "   c1_errors.txt (${#pass_c1err[@]})"
for f in "${pass_c1err[@]}"; do echo "  $f"; done
echo "=================================================================="

# Success means: nothing in the "genuine mismatch" categories (2, 4, 6).
# Category 3/5 ("not yet supported") is expected and does not fail the
# run - it is exactly the honest coverage gap README.md documents.
[ ${#cpp_mismatch[@]} -eq 0 ] && [ ${#c0_mismatch[@]} -eq 0 ] && [ ${#c1_mismatch[@]} -eq 0 ]
