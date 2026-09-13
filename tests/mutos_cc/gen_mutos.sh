#!/bin/sh
#
# gen_mutos.sh
#
# THIS IS A SHELL SCRIPT, NOT A MAKEFILE. Run it with "sh gen_mutos.sh"
# (or "./gen_mutos.sh" once executable) -- never "make -f gen_mutos.sh".
# make will try to parse it as makefile rules and fail with something
# like "Must be a separator on rules line N. Stop." Use Makefile.mutos
# instead if you want the make-based version of this same workflow.
#
# Plain /bin/sh fallback for generating <name>.s / <name>.i / <name>.1 /
# <name>.2 next to every test case's <name>.c, for machines whose `make`
# can't run this directory's Makefile (real V7 `make` has no %,
# $(wildcard), or $(dir)/$(notdir) -- those are GNU Make extensions).
# Run from tests/mutos_cc/ on the real MUTOS 1700 hardware / emulator.
#
# Verified against the real MUTOS 1700 basename(1)/expr(1)/find(1)
# manpages:
# - `find . -name '*.c' -print`: the trailing -print is required, not
#   optional. Unlike GNU find, this find's predicates (-name included)
#   are pure tests with no implicit default action -- omitting -print
#   produces zero output and a silently empty loop, not an error.
# - `expr "$f" : '\(.*\)/'` for the directory part mirrors this system's
#   own documented idiom for the inverse (extracting the basename via
#   expr, shown directly in expr(1)'s EXAMPLES: `expr $a : '.*/\(.*\)'
#   '|' $a`) -- same leftmost-anchored/backtracking `.*` matching, just
#   capturing the other side of the last "/".
# - `basename "$f" .c` matches basename(1)'s own documented form
#   (`basename string [suffix]`) exactly.
#
# Uses only cc, cpp, /lib/c0, find, expr and basename. If your specific
# MUTOS 1700 installation is missing any of those under those exact
# names/paths, adjust below before running.
#
# c0 is fed cpp's OUTPUT (name.i), never the raw name.c -- see the
# Makefile's comment block for why that matters for byte-identical
# temp1/temp2. -P matches the flag every other golden in this project
# was generated with.

for f in `find . -name '*.c' -print`
do
	dir=`expr "$f" : '\(.*\)/'`
	base=`basename "$f" .c`
	echo "$dir/$base"
	(
		cd "$dir" || exit 1
		cpp -P "$base.c" > "$base.i" &&
		/lib/c0 "$base.i" "$base.1" "$base.2" &&
		cc -P -S "$base.c"
	) || echo "  FAILED: $dir/$base"
done
