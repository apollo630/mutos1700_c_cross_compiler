#!/bin/sh
# mutos_root.sh - make the MUTOS root, or bring it up to date.
#
# The MUTOS root (docs/TOOLCHAIN_PLAN.md, decisions 2 and 3) is one
# directory that mirrors the MUTOS file system: the cross tools look up
# every absolute path an original MUTOS 1700 tool names (/lib/crt0.o,
# /lib/c0, -lc's /lib/libc.a, /usr/include, /tmp) under it - $MUTOS_ROOT, by
# default the build tree's mutos_root/ (src/h/mutos_root.h). The
# top-level Makefile runs this script on every "make"; "make clean"
# removes the directory, with anything put into it.
#
#   lib/crt0.o     decoded from tests/mutos1700_crt0/crt0.o.base64.txt
#   lib/libc.a     decoded from tests/mutos1700_libc/libc.a.base64.txt as
#                  it is (not re-archived from its 167 objects), its
#                  modification time set to the time in its __.SYMDEF,
#                  1989-01-31 20:41:17 UTC, so that ld uses the table
#                  without "out of date"
#   lib/cpp, lib/c0, lib/c1, lib/c2, bin/as, bin/ld, bin/cc
#                  symbolic links to mutos_cpp, mutos_c0, mutos_c1,
#                  mutos_c2, mutos_as, mutos_ld, mutos_cc - the driver finds
#                  its passes through the root, under the original's
#                  paths; a link to a tool not built (yet) dangles, and
#                  running it fails as on a MUTOS system without the file
#   usr/lib/       empty: for the user's own libraries (ld -lX looks in
#                  lib/, then usr/lib/)
#   usr/include/   the original headers: mutos/mutos_include/usr/include/
#                  copied as it is, sys/ and local/ included (since
#                  2026-10-25) - a file the source tree does not have
#                  is left alone
#   tmp/           the temporary files: ld's /tmp/ldaXXXXXX (created and
#                  unlinked at once, opened), later cc's /tmp/ctm...
#
# The links are relative for the build tree's own mutos_root/ (they
# stay valid when the tree moves, with MUTOS_ROOT set to its new place)
# and absolute for a root made elsewhere.
#
# Files are rewritten only when they differ, so running it again changes
# nothing but libc.a's time if something reset it.
#
# Usage: scripts/mutos_root.sh [ROOT]     (default: <tree>/mutos_root)
# Needs: sh, base64 (-d), cmp, cp, find, ln, touch (-t, POSIX).

set -e

top=$(cd "$(dirname "$0")/.." && pwd)
root=${1:-$top/mutos_root}

mkdir -p "$root/lib" "$root/bin" "$root/usr/lib" "$root/usr/include" "$root/tmp"
root=$(cd "$root" && pwd)

# decode BASE64-FILE TARGET: write TARGET only if its bytes differ
decode() {
    tmp="$2.new.$$"
    base64 -d < "$1" > "$tmp"
    if [ -f "$2" ] && cmp -s "$tmp" "$2"; then
        rm -f "$tmp"
    else
        mv -f "$tmp" "$2"
        echo "mutos_root: $2"
    fi
}

decode "$top/tests/mutos1700_crt0/crt0.o.base64.txt" "$root/lib/crt0.o"
decode "$top/tests/mutos1700_libc/libc.a.base64.txt" "$root/lib/libc.a"
# libc.a's __.SYMDEF time (602282477), as POSIX touch takes it
TZ=UTC0 touch -t 198901312041.17 "$root/lib/libc.a"

# the original headers, file for file; one line for those copied
inc=$top/mutos/mutos_include/usr/include
headers=$(cd "$inc" && find . -type f | sort)
n=0
while read -r f; do
    f=${f#./}
    d=$(dirname "$root/usr/include/$f")
    [ -d "$d" ] || mkdir -p "$d"
    if ! cmp -s "$inc/$f" "$root/usr/include/$f"; then
        cp "$inc/$f" "$root/usr/include/$f"
        n=$((n + 1))
    fi
done <<HEADERS
$headers
HEADERS
if [ "$n" -gt 0 ]; then
    echo "mutos_root: $root/usr/include: $n header(s) from mutos/mutos_include/usr/include"
fi

# the passes and tools: MUTOS path, then the tool under src/
if [ "$root" = "$top/mutos_root" ]; then
    src=../../src            # from mutos_root/lib/ or mutos_root/bin/
else
    src=$top/src
fi
while read -r mpath tool; do
    want=$src/$tool
    if [ "$(readlink "$root/$mpath" 2>/dev/null || true)" != "$want" ]; then
        ln -sfn "$want" "$root/$mpath"
        echo "mutos_root: $root/$mpath -> $want"
    fi
done <<EOF
lib/cpp mutos_cpp/mutos_cpp
lib/c0 mutos_cc/mutos_c0
lib/c1 mutos_cc/mutos_c1
lib/c2 mutos_cc/mutos_c2
bin/as mutos_as/mutos_as
bin/ld mutos_ld/mutos_ld
bin/cc mutos_cc/mutos_cc
EOF
