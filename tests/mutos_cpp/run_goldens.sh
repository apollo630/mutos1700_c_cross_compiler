#!/usr/bin/env bash
#
# run_goldens.sh - preprocesses every *.c file in tests/mutos_cpp/c/
# with mutos_cpp, using the exact flags the golden references were
# generated with ("cc -P -DM7100 -DASK -DIFSS -DV24 -DV30IDE <name>.c"
# on real MUTOS 1700 hardware - the "-P" is cc's own "stop after
# preprocessing" flag, which cc also forwards verbatim to cpp, where it
# means "suppress '# N \"file\"' line markers"; see v7/cc/cc.c's option
# parsing and src/mutos_cpp/README), then diffs the result against its
# <n>.i.golden reference. Include search is relative to each source
# file's own directory (matching every golden's "../h/foo.h" include
# style), which mutos_cpp's default quote-include resolution already
# handles with no -I needed.
#
# Prints a three-way summary:
#   1. Clean preprocesses (mutos_cpp succeeded AND the diff matched)
#   2. Preprocesses where mutos_cpp succeeded but the diff mismatched
#   3. Preprocesses where mutos_cpp itself reported an error
#
# Usage:
#   ./run_goldens.sh                       # uses "mutos_cpp" from $PATH
#   MUTOS_CPP=../../src/mutos_cpp/mutos_cpp ./run_goldens.sh
#
# Exit status: 0 if every file fell into category 1, 1 otherwise.

set -u

MUTOS_CPP="${MUTOS_CPP:-mutos_cpp}"
DEFINES="-DM7100 -DASK -DIFSS -DV24 -DV30IDE"

if ! command -v "$MUTOS_CPP" >/dev/null 2>&1; then
    echo "error: '$MUTOS_CPP' not found (set \$MUTOS_CPP or add it to \$PATH)" >&2
    exit 1
fi

cd "$(dirname "$0")/c" || exit 1

ok=()
diff_fail=()
run_fail=()

for src in *.c; do
    name="${src%.c}"
    golden="${name}.i.golden"
    [ -f "$golden" ] || continue

    out="/tmp/${name}.mutos_cpp_test.i"
    if ! "$MUTOS_CPP" -P $DEFINES "$src" "$out" 2>/tmp/mutos_cpp_test_err.txt; then
        run_fail+=("$src")
        continue
    fi
    if diff -q "$out" "$golden" >/dev/null 2>&1; then
        ok+=("$src")
    else
        diff_fail+=("$src")
    fi
done

echo "=============================================="
echo "1. Error free preprocessor runs (${#ok[@]})"
for f in "${ok[@]}"; do echo "  $f"; done

echo
echo "2. Preprocessor runs with errors running diff (${#diff_fail[@]})"
if [ ${#diff_fail[@]} -eq 0 ]; then echo "  (none)"; fi
for f in "${diff_fail[@]}"; do echo "  $f"; done

echo
echo "3. Preprocessor runs with errors calling $MUTOS_CPP (${#run_fail[@]})"
if [ ${#run_fail[@]} -eq 0 ]; then echo "  (none)"; fi
for f in "${run_fail[@]}"; do echo "  $f"; done
echo "=============================================="

[ ${#diff_fail[@]} -eq 0 ] && [ ${#run_fail[@]} -eq 0 ]
