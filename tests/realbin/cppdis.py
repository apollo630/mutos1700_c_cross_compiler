#!/usr/bin/env python3
"""cppdis.py - annotated 8086 disassembly of the real MUTOS 1700 cpp.

c1dis.py on mutos/mutos_bin/cpp.golden.base64.txt with cppmap.txt: names the
functions and data from the map (the binary's own symbol table), the
libc.a functions linked in by matching libc.a's objects against the
text, and prints a listing with call targets, data names and the
strings an immediate points at. A switch's jump table in the text
(cotoken()'s at 0875, tobinary()'s at 2288, yyparse()'s at 250d and
2689) disassembles as noise; cpptables.py --symbols lists every symbol.

Usage:
    cppdis.py NAME               one function (a name from cppmap.txt)
    cppdis.py START END          a text range (hex offsets)
    cppdis.py --functions        the function list with sizes and status

Requires the Python module "capstone" (pip install capstone). Analysis
tool for the cpp port; it is not part of any test.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import c1dis  # noqa: E402

c1dis.use('cpp')

if __name__ == '__main__':
    sys.exit(c1dis.main(sys.argv[1:]))
