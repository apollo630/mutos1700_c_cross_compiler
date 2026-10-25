#!/usr/bin/env python3
"""c0dis.py - annotated 8086 disassembly of the real MUTOS 1700 c0.

c1dis.py on mutos/mutos_bin/c0.golden.base64.txt with c0map.txt: names the
functions and data from the map, the libc.a functions linked in by
matching libc.a's objects against the text, and prints a listing with
call targets, data names and the strings an immediate points at.

Usage:
    c0dis.py NAME               one function (a name from c0map.txt)
    c0dis.py START END          a text range (hex offsets)
    c0dis.py --functions        the function list with sizes and status

Requires the Python module "capstone" (pip install capstone). Analysis
tool for the c0 port (see docs/C0_PORT_PLAN.md); it is not part of any
test.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c1dis  # noqa: E402

c1dis.use('c0')

if __name__ == '__main__':
    sys.exit(c1dis.main(sys.argv[1:]))
