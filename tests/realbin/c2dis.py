#!/usr/bin/env python3
"""c2dis.py - annotated 8086 disassembly of the real MUTOS 1700 c2.

c1dis.py on mutos_bin/c2.golden.base64.txt with c2map.txt: names the
functions and data from the map (the binary is stripped: the names are
v7's where there is a counterpart in v7/cc/c20.c and c21.c, see
c2map.txt), the libc.a functions linked in by matching libc.a's objects
against the text, and prints a listing with call targets, data names and
the strings an immediate points at. The switches' jump tables in the
text disassemble as noise (c2map.txt lists them); c2tables.py --strings
lists the strings.

Usage:
    c2dis.py NAME               one function (a name from c2map.txt)
    c2dis.py START END          a text range (hex offsets)
    c2dis.py --functions        the function list with sizes and status

Requires the Python module "capstone" (pip install capstone). Analysis
tool for the c2 port; it is not part of any test.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c1dis  # noqa: E402

c1dis.use('c2')

if __name__ == '__main__':
    sys.exit(c1dis.main(sys.argv[1:]))
