#!/usr/bin/env python3
"""tsortdis.py - annotated 8086 disassembly of the real MUTOS 1700 tsort.

c1dis.py on mutos/mutos_bin/tsort.golden.base64.txt with tsortmap.txt: names the
functions and data from the map (the binary is stripped: the names are
v7's, see tsortmap.txt), the libc.a functions linked in by matching libc.a's
objects against the text, and prints a listing with call targets, data
names and the strings an immediate points at. A switch's jump table in
the text disassembles as noise; tsorttables.py (ar: artables.py) lists
the data.

Usage:
    tsortdis.py NAME               one function (a name from tsortmap.txt)
    tsortdis.py START END          a text range (hex offsets)
    tsortdis.py --functions        the function list with sizes and status

Requires the Python module "capstone" (pip install capstone). Analysis
tool for the tsort port; it is not part of any test.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c1dis  # noqa: E402

c1dis.use('tsort')

if __name__ == '__main__':
    sys.exit(c1dis.main(sys.argv[1:]))
