#!/usr/bin/env python3
"""ccdis.py - annotated 8086 disassembly of the real MUTOS 1700 cc.

c1dis.py on mutos/mutos_bin/cc.golden.base64.txt with ccmap.txt: names the
functions and data from the map (the binary is stripped: the names are
v7's, see ccmap.txt), the libc.a functions linked in by matching libc.a's
objects against the text, and prints a listing with call targets, data
names and the strings an immediate points at. cc is 0407 - one segment,
the data after the text -, so a data address is an address in that
segment (c1dis.py indexes the data by address for a 0407 binary). main()'s
hashed switch table (02ff) disassembles as noise; cctables.py --strings
lists the strings.

Usage:
    ccdis.py NAME               one function (a name from ccmap.txt)
    ccdis.py START END          a text range (hex offsets)
    ccdis.py --functions        the function list with sizes and status

Requires the Python module "capstone" (pip install capstone). Analysis
tool for the cc port; it is not part of any test.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c1dis  # noqa: E402

c1dis.use('cc')

if __name__ == '__main__':
    sys.exit(c1dis.main(sys.argv[1:]))
