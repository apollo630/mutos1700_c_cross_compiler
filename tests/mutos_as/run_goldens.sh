#!/usr/bin/env bash
#
# run_goldens.sh - assembles every *.s file in the current directory
# with mutos_as, then diffs the result against its <name>.o.golden
# reference, and prints a three-way summary at the end:
#
#   1. Clean assemblies (mutos_as succeeded AND the diff matched)
#   2. Assemblies where mutos_as succeeded but the diff found a mismatch
#   3. Assemblies where mutos_as itself reported an error
#
# Usage:
#   ./run_goldens.sh                # uses "mutos_as" from $PATH
#   MUTOS_AS=../build/mutos_as ./run_goldens.sh   # use a specific binary
#
# Exit status: 0 if every file fell into category 1 (or had no golden
# to compare against), 1 if any file fell into category 2 or 3.

set -u

MUTOS_AS="${MUTOS_AS:-mutos_as}"

if ! command -v "$MUTOS_AS" >/dev/null 2>&1; then
    echo "error: '$MUTOS_AS' not found (set \$MUTOS_AS or add it to \$PATH)" >&2
    exit 1
fi

# Arrays collecting the .s filename for each outcome category.
ok=()
diff_fail=()
asm_fail=()
no_golden=()

# Captured mutos_as/diff output per file, keyed by filename, for the
# short diagnostic shown under each failing entry in the summary.
declare -A asm_fail_msg
declare -A diff_fail_msg

tmp_out="$(mktemp)"
trap 'rm -f "$tmp_out"' EXIT

shopt -s nullglob
for src in *.s; do
    base="${src%.s}"
    obj="${base}.o"
    golden="${base}.o.golden"

    if ! "$MUTOS_AS" "$src" -o "$obj" >"$tmp_out" 2>&1; then
        asm_fail+=("$src")
        asm_fail_msg["$src"]="$(tail -n 1 "$tmp_out")"
        continue
    fi

    if [ ! -f "$golden" ]; then
        no_golden+=("$src")
        continue
    fi

    if diff -q "$obj" "$golden" >"$tmp_out" 2>&1; then
        ok+=("$src")
    else
        diff_fail+=("$src")
        diff_fail_msg["$src"]="$(tail -n 1 "$tmp_out")"
    fi
done

echo "=============================================="
printf '1. Fehlerfreie Assemblierungen (%d)\n' "${#ok[@]}"
if [ "${#ok[@]}" -eq 0 ]; then
    printf '  (keine)\n'
else
    for f in "${ok[@]}"; do printf '  %s\n' "$f"; done
fi

echo ""
printf '2. Assemblierungen mit Fehler beim diff (%d)\n' "${#diff_fail[@]}"
if [ "${#diff_fail[@]}" -eq 0 ]; then
    printf '  (keine)\n'
else
    for f in "${diff_fail[@]}"; do
        printf '  %s\n      -> %s\n' "$f" "${diff_fail_msg[$f]}"
    done
fi

echo ""
printf '3. Assemblierungen mit Fehlern beim Aufruf von %s (%d)\n' "$MUTOS_AS" "${#asm_fail[@]}"
if [ "${#asm_fail[@]}" -eq 0 ]; then
    printf '  (keine)\n'
else
    for f in "${asm_fail[@]}"; do
        printf '  %s\n      -> %s\n' "$f" "${asm_fail_msg[$f]}"
    done
fi

if [ "${#no_golden[@]}" -gt 0 ]; then
    echo ""
    printf 'Hinweis: keine *.o.golden-Datei gefunden, nicht verglichen (%d)\n' "${#no_golden[@]}"
    for f in "${no_golden[@]}"; do printf '  %s\n' "$f"; done
fi
echo "=============================================="

[ "${#diff_fail[@]}" -eq 0 ] && [ "${#asm_fail[@]}" -eq 0 ]
