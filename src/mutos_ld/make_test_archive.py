#!/usr/bin/env python3
"""
Builds small synthetic MUTOS/V7 archives (.a) to test ld's new archive
support: a plain archive (no __.SYMDEF) and a ranlib-style archive with
an up-to-date table of contents, using PDP-11 middle-endian encoding
for the archdr/tab `long` fields, as verified against a real libc.a.
"""
import struct
import sys
import os
import time

def u16(x):
    return struct.pack('<H', x & 0xFFFF)

def pdp11_long(x):
    x &= 0xFFFFFFFF
    hi = (x >> 16) & 0xFFFF
    lo = x & 0xFFFF
    return struct.pack('<HH', hi, lo)

N_EXT = 0x20
N_TEXT = 0x02
N_UNDF = 0x00
FMAGIC = 0o407

def sym(name, type_, value):
    n = name.encode('ascii')[:8].ljust(8, b'\0')
    return n + bytes([type_, 0]) + u16(value)

def make_obj(text_words, data_words, syms, reloc_text=None, reloc_data=None):
    """syms: list of (name, type, value); text_words/data_words: list of ints"""
    text = b''.join(u16(w) for w in text_words)
    data = b''.join(u16(w) for w in data_words)
    if reloc_text is None:
        reloc_text = b''.join(u16(0) for _ in text_words)
    if reloc_data is None:
        reloc_data = b''.join(u16(0) for _ in data_words)
    symtab = b''.join(sym(n, t, v) for n, t, v in syms)
    tsize, dsize, bsize, ssize = len(text), len(data), 0, len(symtab)
    header = u16(FMAGIC) + u16(tsize) + u16(dsize) + u16(bsize) + u16(ssize) + u16(0) + u16(0) + u16(0)
    return header + text + data + reloc_text + reloc_data + symtab

def ar_member(name, content):
    aname = name.encode('ascii')[:14].ljust(14, b'\0')
    atime = pdp11_long(int(time.time()))
    auid = bytes([0])
    agid = bytes([0])
    amode = u16(0o100644)
    asize = pdp11_long(len(content))
    hdr = aname + atime + auid + agid + amode + asize
    body = content
    if len(body) & 1:
        body += b'\0'
    return hdr + body

ARCMAGIC = 0o177545

def build_plain_archive(path):
    """Archive with no __.SYMDEF - forces the linear-scan (case 1) path.
    Member 'libfoo.o' defines external symbol '_foo' (a text symbol)."""
    obj = make_obj(
        text_words=[0x9090, 0x9090],  # 2 filler words (4 bytes)
        data_words=[],
        syms=[('_foo', N_EXT | N_TEXT, 0)],
    )
    data = u16(ARCMAGIC) + ar_member('foo.o', obj)
    with open(path, 'wb') as f:
        f.write(data)

def build_toc_archive(path):
    """Archive WITH a ranlib-style __.SYMDEF table of contents - exercises
    the ldrand()/TOC fast path (case 2), as long as the archive file's
    own mtime is not newer than the embedded timestamp (we set both to
    'now' and then also touch the timestamp forward slightly so the
    embedded one is >= the file mtime after write)."""
    obj1 = make_obj(
        text_words=[0x9191],
        data_words=[],
        syms=[('_bar', N_EXT | N_TEXT, 0)],
    )
    obj2 = make_obj(
        text_words=[0x9292, 0x9292],
        data_words=[0x0001],
        syms=[('_baz', N_EXT | N_TEXT, 0)],
    )
    m1 = ar_member('bar.o', obj1)
    m2 = ar_member('baz.o', obj2)

    # byte offsets of each member's archdr, relative to file start:
    # ARCMAGIC(2) + symdef_hdr(26) + symdef_body(padded) + ...
    tab_entries = [('_bar', None), ('_baz', None)]  # cloc filled in below

    # First pass: figure out sizes to compute offsets.
    def padded(n):
        return n + (n & 1)

    tab_size = len(tab_entries) * 12
    symdef_body_size = padded(tab_size)
    off_symdef_hdr = 2
    off_m1 = off_symdef_hdr + 26 + symdef_body_size
    off_m2 = off_m1 + len(m1)

    tab_bytes = b''
    tab_bytes += ('_bar'.encode('ascii')[:8].ljust(8, b'\0')) + pdp11_long(off_m1)
    tab_bytes += ('_baz'.encode('ascii')[:8].ljust(8, b'\0')) + pdp11_long(off_m2)
    if len(tab_bytes) & 1:
        tab_bytes += b'\0'

    symdef_aname = '__.SYMDEF'.encode('ascii').ljust(14, b'\0')
    symdef_atime = pdp11_long(int(time.time()) + 3600)  # 1h in the "future" so it's never stale
    symdef_hdr = symdef_aname + symdef_atime + bytes([0]) + bytes([0]) + u16(0o100644) + pdp11_long(len(tab_bytes))

    data = u16(ARCMAGIC) + symdef_hdr + tab_bytes + m1 + m2
    with open(path, 'wb') as f:
        f.write(data)
    # Ensure the archive file's mtime is safely in the past relative to
    # the embedded (future) __.SYMDEF timestamp.
    past = time.time() - 10
    os.utime(path, (past, past))

if __name__ == '__main__':
    build_plain_archive('libplain.a')
    build_toc_archive('libtoc.a')
    print('written: libplain.a, libtoc.a')
