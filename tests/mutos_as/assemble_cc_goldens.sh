#!/usr/bin/env bash
#
# assemble_cc_goldens.sh - assembles every real compiler output in
# tests/mutos_cc (each *.s.golden: the real MUTOS 1700 cc's own .s for a
# corpus file) with mutos_as, and reports any file mutos_as refuses.
#
# There are no reference objects for these (the real toolchain's .o for
# them was never captured), so this checks only that the real
# compiler's output ASSEMBLES - every construct mutos_c1 reproduces
# byte for byte is also one mutos_as accepts. The bytes themselves are
# checked elsewhere: tests/mutos_as/kernel_*/ and libc_recon/ against
# real objects.
#
# A golden listed in tests/mutos_cc/invalid_goldens.txt ends in the real
# compiler's own invalid output (a register name that is not one): it is
# expected NOT to assemble, and counted apart - a listed file that does
# assemble is reported as a failure, the list being out of date.
#
# Usage (from anywhere):
#   ./assemble_cc_goldens.sh                     # "mutos_as" from $PATH
#   MUTOS_AS=../../src/mutos_as/mutos_as ./assemble_cc_goldens.sh
#
# Exit status: 0 if every file assembles, 1 otherwise.

set -u

MUTOS_AS="${MUTOS_AS:-mutos_as}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CORPUS="$HERE/../mutos_cc"

if ! command -v "$MUTOS_AS" >/dev/null 2>&1; then
    echo "error: '$MUTOS_AS' not found (set \$MUTOS_AS or add it to \$PATH)" >&2
    exit 1
fi

obj="$(mktemp)"
log="$(mktemp)"
trap 'rm -f "$obj" "$log"' EXIT

declare -A invalid=()
if [ -f "$CORPUS/invalid_goldens.txt" ]; then
    while read -r key _; do
        case "$key" in ''|'#'*) continue ;; esac
        invalid["$key.s.golden"]=1
    done < "$CORPUS/invalid_goldens.txt"
fi

n=0
fail=0
ninv=0
while IFS= read -r src; do
    rel="${src#"$CORPUS"/}"
    if [ -n "${invalid[$rel]:-}" ]; then
        ninv=$((ninv + 1))
        if "$MUTOS_AS" -o "$obj" "$src" >"$log" 2>&1; then
            fail=$((fail + 1))
            printf '  FAIL  %s\n        -> listed in invalid_goldens.txt, but assembles\n' "$rel"
        else
            printf '  invalid (as listed)  %s\n        -> %s\n' "$rel" "$(grep -m1 error "$log")"
        fi
        continue
    fi
    n=$((n + 1))
    if ! "$MUTOS_AS" -o "$obj" "$src" >"$log" 2>&1; then
        fail=$((fail + 1))
        printf '  FAIL  %s\n        -> %s\n' "$rel" "$(grep -m1 error "$log")"
    fi
done < <(find "$CORPUS" -name '*.s.golden' | LC_ALL=C sort)

echo "mutos_cc .s goldens: $((n - fail))/$n assemble ($ninv more, the real compiler's own invalid output, refused as expected)"
[ "$fail" -eq 0 ]
