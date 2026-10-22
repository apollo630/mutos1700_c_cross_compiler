#!/usr/bin/env python3
"""lddis.py - annotated 8086 disassembly of the real MUTOS 1700 ld.

c1dis.py on mutos_bin/ld.golden.base64.txt with ldmap.txt: names the
functions and data from the map (the binary is stripped: the names are
v7's, see ldmap.txt), the libc.a functions linked in by matching libc.a's
objects against the text, and prints a listing with call targets, data
names and the strings an immediate points at. A switch's jump table in
the text (load1arg()'s at 0840, middle()'s at 0ddb, load2td()'s at 1512)
and main()'s hashed switch table (03e8) disassemble as noise;
ldtables.py --strings lists the strings.

Usage:
    lddis.py NAME               one function (a name from ldmap.txt)
    lddis.py START END          a text range (hex offsets)
    lddis.py --functions        the function list with sizes and status

Requires the Python module "capstone" (pip install capstone). Analysis
tool for the ld port; it is not part of any test.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c1dis  # noqa: E402

c1dis.use('ld')

if __name__ == '__main__':
    sys.exit(c1dis.main(sys.argv[1:]))
