#!/usr/bin/env bash
#
# check_floatdat.sh - checks mutos_as's ".float" encoding against the
# floating constants the REAL MUTOS 1700 toolchain wrote into libc.a.
#
# Assembles floatdat.s (four constants, "%.17e" as mutos_c1 writes them)
# and compares each value's 4 bytes in the resulting data segment with
# the bytes of the same constant in a real libc.a object's data segment
# (tests/mutos1700_libc/atof.o and ecvt.o) - read from those objects at
# test time, not copied into this directory. See README.md.
#
# Since 2026-09-27 the real objects under tests/mutos1700_libc/ are stored
# base64-encoded as "<name>.o.base64.txt" (the raw ".o" binaries were
# deleted - see CLAUDE.md's ".o"-ambiguity caution); each referenced object
# is base64-decoded into a scratch file before its data segment is read.
#
# Usage (from this directory):
#   ./check_floatdat.sh                              # "mutos_as" from $PATH
#   MUTOS_AS=../../../src/mutos_as/mutos_as ./check_floatdat.sh
#
# Exit status: 0 if every comparison matches, 1 otherwise.

set -u

MUTOS_AS="${MUTOS_AS:-mutos_as}"
HERE="$(cd "$(dirname "$0")" && pwd)"
LIBC="$HERE/../../mutos1700_libc"

if ! command -v "$MUTOS_AS" >/dev/null 2>&1; then
    echo "error: '$MUTOS_AS' not found (set \$MUTOS_AS or add it to \$PATH)" >&2
    exit 1
fi

obj="$(mktemp)"
log="$(mktemp)"
decode_dir="$(mktemp -d)"
trap 'rm -f "$obj" "$log"; rm -rf "$decode_dir"' EXIT

if ! "$MUTOS_AS" -o "$obj" "$HERE/floatdat.s" >"$log" 2>&1; then
    echo "error: mutos_as failed on floatdat.s:" >&2
    cat "$log" >&2
    exit 1
fi

# data_bytes FILE OFFSET: the 4 bytes at OFFSET in FILE's data segment,
# as hex. The a.out header's a_text (bytes 2-3) is little-endian; it is
# read byte by byte so the host's own byte order does not matter.
data_bytes() {
    local lo hi
    read -r lo hi < <(od -An -tu1 -j2 -N2 "$1")
    od -An -tx1 -j $((16 + lo + 256 * hi + $2)) -N4 "$1" | tr -s ' \n' ' ' | sed 's/^ //; s/ $//'
}

# decoded_path NAME: base64-decodes "$LIBC/NAME.base64.txt" into
# "$decode_dir/NAME" (once per NAME) and prints that scratch path.
decoded_path() {
    local name="$1" out="$decode_dir/$1"
    if [ ! -e "$out" ]; then
        if ! base64 -d "$LIBC/$name.base64.txt" >"$out" 2>/dev/null; then
            echo "error: failed to base64-decode '$LIBC/$name.base64.txt'" >&2
            exit 1
        fi
    fi
    printf '%s' "$out"
}

fail=0
n=0
# floatdat.s offset | real object | its data offset | value
while read -r ours real roff what; do
    n=$((n + 1))
    want="$(data_bytes "$(decoded_path "$real")" "$roff")"
    got="$(data_bytes "$obj" "$ours")"
    if [ "$got" = "$want" ]; then
        printf '  ok    %-6s %s  = %s data+%d\n' "$what" "$got" "$real" "$roff"
    else
        printf '  FAIL  %-6s %s != %s data+%d (%s)\n' "$what" "$got" "$real" "$roff" "$want"
        fail=$((fail + 1))
    fi
done <<'EOF'
0 atof.o 0 2**56
4 atof.o 8 10.0
4 ecvt.o 12 10.0
8 atof.o 20 1.0
8 ecvt.o 32 1.0
12 atof.o 24 5.0
EOF

echo "floatdat.s: $((n - fail))/$n real libc.a constants byte-identical"
[ "$fail" -eq 0 ]
