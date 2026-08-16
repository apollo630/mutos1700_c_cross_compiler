#!/usr/bin/env python3
"""
Erzeugt eine minimale, handgefertigte MUTOS/V7-Objektdatei (.o) zum
Testen des portierten Linkers, da noch kein `as`/`cc` existiert.

Layout (siehe ld.c Kommentar):
  Header(16) + Text(tsize) + Data(dsize) + RelocText(tsize) + RelocData(dsize) + Symtab(ssize)

Testfall: ein Objekt mit
  - 4 Byte Text: "mov ax, <undefined_symbol>" simuliert durch ein Wort,
    das per externer Relokation (REXT) auf ein undefiniertes Symbol "_foo" verweist,
    und ein zweites Wort ohne Relokation (RABS).
  - 2 Byte Data: ein Wort, initialisiert mit 0x1234, RABS (keine Relokation).
  - kein BSS.
  - Symboltabelle: ein globales Textsymbol "_main" (Wert 0, EXTERN+TEXT),
    ein globales undefiniertes Symbol "_foo" (EXTERN+UNDEF, Wert 0).
"""
import struct
import sys

def u16(x):
    return struct.pack('<H', x & 0xFFFF)

def sym(name, type_, value):
    n = name.encode('ascii')[:8].ljust(8, b'\0')
    return n + bytes([type_, 0]) + u16(value)

N_EXT = 0x20
N_TEXT = 0x02
N_UNDF = 0x00

FMAGIC = 0o407

def build(path, entrypoint_sym=None):
    text = u16(0x0000) + u16(0x00AA)   # 2 Textwoerter (4 Byte)
    data = u16(0x1234)                  # 1 Datenwort (2 Byte)

    # Relokation: erstes Textwort -> externe Referenz auf Symbol-Index 0 (_foo)
    # r-Format: (r&01)=0 (kein Vorzeichenwechsel) + (symno<<4) + REXT(010)
    REXT = 0o10
    reloc_text = u16((0 << 4) | REXT) + u16(0x0000)  # zweites Wort: RABS, keine Relokation
    reloc_data = u16(0x0000)  # RABS

    symtab = b''
    symtab += sym('_foo', N_EXT | N_UNDF, 0)
    symtab += sym('_main', N_EXT | N_TEXT, 0)

    tsize = len(text)
    dsize = len(data)
    bsize = 0
    ssize = len(symtab)

    header = u16(FMAGIC) + u16(tsize) + u16(dsize) + u16(bsize) + u16(ssize) + u16(0) + u16(0) + u16(0)

    with open(path, 'wb') as f:
        f.write(header)
        f.write(text)
        f.write(data)
        f.write(reloc_text)
        f.write(reloc_data)
        f.write(symtab)

N_DATA = 0x03

def build_b(path):
    # Diese Datei hat 2 Byte Text (1 Wort, keine Relokation) und
    # 2 Byte Data (1 Wort, keine Relokation). Sie definiert _foo als
    # EXTERN+DATA-Symbol, dessen roher gespeicherter Wert bereits die
    # V7-Konvention "thisfile_tsize + lokaler_data_offset" beruecksichtigt:
    # tsize(2) + offset(0) = 2.
    text = u16(0x9090)          # 1 Textwort, Fuellwert
    data = u16(0x0000)          # 1 Datenwort, Wert von _foo selbst (0)
    reloc_text = u16(0x0000)    # RABS
    reloc_data = u16(0x0000)    # RABS

    symtab = sym('_foo', N_EXT | N_DATA, 2)  # 2 = thisfile_tsize(2) + local_offset(0)

    tsize = len(text)
    dsize = len(data)
    bsize = 0
    ssize = len(symtab)

    header = u16(FMAGIC) + u16(tsize) + u16(dsize) + u16(bsize) + u16(ssize) + u16(0) + u16(0) + u16(0)

    with open(path, 'wb') as f:
        f.write(header)
        f.write(text)
        f.write(data)
        f.write(reloc_text)
        f.write(reloc_data)
        f.write(symtab)

if __name__ == '__main__':
    build(sys.argv[1])
    build_b(sys.argv[2])
    print("geschrieben:", sys.argv[1], sys.argv[2])
