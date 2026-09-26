#!/usr/bin/env python3
"""
dump_temp.py - human-readable dump of a mutos_cc temp1/temp2 file.

temp1/temp2 are the tagged intermediate-code byte streams mutos_c0
writes and mutos_c1 reads (see c0_outcode.h's wire format, and
src/mutos_cc/README.md's "The temp1/temp2 wire format" section). This
is a standalone reader, not a mode of mutos_c0 itself, so it works on
ANY .1/.2 file - one mutos_c0 just generated, or a real-hardware
*.1.golden/*.2.golden reference - which a dump mode built into mutos_c0
could not do (mutos_c0 never produced the golden files). This is the
"optional human-readable dump mode" CLAUDE.md's Milestone 4 section
flags as useful for checking front-end output "before any x86 code
generator exists", implemented as an external tool instead so it can
read goldens too.

Wire format (see c0_outcode.c/c0_outcode.h):
    B  - one opcode byte, followed by a fixed 0xFE marker byte (2 bytes)
    N  - one 16-bit little-endian value, sign-extended when printed (2 bytes)
    S  - a symbol: '_' + up to MCC_NCPS name bytes + a NUL terminator
         (present only when the name is non-empty; c0 always writes
         non-empty names here, so this reader always expects '_' first)
    0  - two raw zero bytes (OP_SWIT's table terminator - a single
         lone zero N-sized word, never a (0,0) pair - and the end of a
         BDATA run)
    1  - two bytes, value 1 low-byte-first (the "another byte follows"
         word of a BDATA run's (1, value) pairs)

temp2 holds only string literals (c0_parser.c's putstr()): LABEL <n>,
then one or more BDATA runs - BDATA, (1, byte)..., 0 - see OPCODES.

Each opcode's own argument shape (how many N/S fields follow the B)
is fixed and is transcribed below directly from every outcode() call
site in c0_parser.c/c0_main.c - see OPCODES. An opcode that never
actually appears in mutos_c0's own output (listed in ALL_OPCODE_NAMES
for display purposes only) has no known argument shape, so hitting one
stops the dump cleanly with a clear message rather than guessing and
silently desyncing the rest of the file - the same "explicit not yet
supported, never silently wrong" rule this project applies everywhere
else.

Usage:
    ./dump_temp.py FILE [FILE ...]
    ./dump_temp.py tests/mutos_cc/00_smoke/01_emptymain.1
    ./dump_temp.py tests/mutos_cc/03_ctrlflow/*.1.golden

Multiple files are dumped one after another, each under its own
"=== path ===" header. No arguments beyond a list of paths; nothing
about the file's byte layout is inferred from its name or extension.
"""

import sys

# ---------------------------------------------------------------------------
# Opcode numbers (mutos_cc.h) and argument shapes (c0_parser.c/c0_main.c's
# actual outcode() call sites - every opcode mutos_c0 ever writes).
#
# Each entry: name -> list of ('N', label, decoder) | ('S', label) fields
# that follow the opcode byte. `decoder` is an optional function applied
# to an N field's raw value for a friendlier rendering (e.g. resolving a
# TY_*/SC_* constant name); pass None for a plain signed-decimal N.
# ---------------------------------------------------------------------------

TY_BASE_NAMES = {
    0: "TY_INT", 1: "TY_CHAR", 2: "TY_FLOAT", 3: "TY_DOUBLE",
    4: "TY_STRUCT", 6: "TY_LONG", 7: "TY_UNSIGN",
}


TY_DEGREE_NAMES = {1: "PTR", 2: "FUNC", 3: "ARRAY"}


def decode_type(v):
    """Renders a TYPE field as its full derived-type chain, outermost
    degree first, then the base type - e.g. "PTR.TY_INT" (8),
    "FUNC.TY_INT" (16), "PTR.PTR.TY_INT" (40), "PTR.FUNC.TY_INT" (72),
    "PTR.ARRAY.TY_INT" (104) - read like C's own English: "pointer to
    array of int". This is v7/cc's incref() encoding (v7/cc/c0.h): the
    base type in the low 3 bits (TYPE), then one 2-bit degree tag per
    level (PTR=1, FUNC=2, ARRAY=3), the OUTERMOST in bits 3-4 (XTYPE)
    and each further one 2 bits higher - see c0_parser.c's
    ty_incref_tag()/ty_decref(). Note this bit layout is not fully
    unambiguous on its own: TY_UNION (a flat value of 8) and "pointer
    to int" decode identically here - mutos_c0 does not emit TY_UNION
    today, so this always resolves the more useful way for its actual
    output, but a hypothetical future union-typed value would print as
    a pointer instead."""
    name = TY_BASE_NAMES.get(v & 7, "TY_?%d" % (v & 7))
    tags = []
    t = (v & 0xFFFF) >> 3
    while t:
        tags.append(TY_DEGREE_NAMES.get(t & 3, "?"))
        t >>= 2
    return ".".join(tags + [name])


SC_NAMES = {
    9: "SC_TYPEDEF", 10: "SC_MOS", 11: "SC_AUTO", 12: "SC_EXTERN",
    13: "SC_STATIC", 14: "SC_REG", 15: "SC_STRTAG", 16: "SC_ARG",
    17: "SC_ARG1", 18: "SC_AREG", 20: "SC_DEFXTRN", 21: "SC_MOU",
    22: "SC_ENUMTAG", 24: "SC_ENUMCON",
}


def decode_sc(v):
    return SC_NAMES.get(v, "SC_?%d" % v)


N = lambda label, decoder=None: ("N", label, decoder)
S = lambda label: ("S", label)

OPCODES = {
    # -- segment/pseudo-op tags (c0's function/decl driver, not treeout()) --
    202: ("PROG", []),
    210: ("EVEN", []),
    207: ("SYMDEF", [S("name")]),
    208: ("SAVE", []),
    209: ("RETRN", [N("type", decode_type)]),
    213: ("SWIT", [N("deflab"), N("line")]),  # + variable trailing table -
                                               # handled specially, see below
    214: ("EXPR", [N("line")]),
    217: ("ANAME", [S("name"), N("offset")]),
    219: ("SETSTK", [N("bytes")]),
    105: ("SETREG", [N("regvar")]),
    111: ("BRANCH", [N("label")]),
    112: ("LABEL", [N("label")]),
    114: ("RLABEL", [S("name")]),
    0:   ("EOFC", []),

    # -- local STATIC variables (Milestone 4's "Function parameters and
    # calls" increment - v7/cc/c03.c's declist() STATIC case) - a local
    # static lives in its own dedicated BSS block, tagged with a fresh
    # intermediate-code label, rather than on the stack frame; confirmed
    # byte-for-byte against 04_funcs/05_staticvar.1.golden's "static int
    # n;" (BSS, LABEL(4), SSPACE(2), PROG, SNAME("_n", 4)) --
    204: ("BSS", []),                          # opens the block; the
                                                # LABEL/SSPACE that always
                                                # immediately follow carry
                                                # its own label/size
    206: ("SSPACE", [N("bytes")]),              # reserves N bytes in the
                                                 # block BSS/DATA just opened
    215: ("SNAME", [S("name"), N("label")]),    # declares `name` as this
                                                 # label's own static
                                                 # variable - same "BSN"
                                                 # shape as ANAME, but
                                                 # `label` is an internal
                                                 # LABEL NUMBER, not a
                                                 # bp-relative offset (see
                                                 # mutos_c1's own VK_STATIC)
    216: ("RNAME", [S("name"), N("regvar")]),   # declares `name` as a
                                                 # 'register'-class local -
                                                 # same "BSN" shape as ANAME/
                                                 # SNAME, but `regvar` is the
                                                 # register-allocator's own
                                                 # slot number (the SETREG
                                                 # value in effect right
                                                 # after this variable was
                                                 # claimed - see
                                                 # 04_funcs/06_regclass.
                                                 # 1.golden), not a
                                                 # bp-relative offset -
                                                 # mutos_c1 maps it to a
                                                 # physical register name
                                                 # (di/si) for every later
                                                 # NAME reference too (hclass
                                                 # SC_REG=14).

    # -- file-scope variables (07_scope - v7/cc/c02.c's extdef() for a
    # non-function declarator; c0_parser.c's parse_global_var()):
    # "int counter;" is CSPACE("_counter", 2); "static int hidden;" is
    # BSS, NLABEL("_hidden"), SSPACE(2) - no SYMDEF("") in front, unlike
    # v7 - confirmed byte-for-byte against 07_scope/01_globstat.1.golden
    # and 03_externdef.1.golden; "extern int total;" writes nothing --
    205: ("CSPACE", [S("name"), N("bytes")]),   # a common block of N bytes
    113: ("NLABEL", [S("name")]),               # a named label - here the
                                                 # static's own BSS block,
                                                 # which the SSPACE that
                                                 # follows reserves

    # -- expression-tree leaves/operators (treeout()) --
    20:  ("NAME", "special"),  # hclass, type, then EITHER a symbol name
                               # (hclass == SC_EXTERN - a called function's
                               # own name, a bare function name used as a
                               # value, per parse_call()/parse_primary(),
                               # or a file-scope variable - 07_scope)
                               # OR a numeric offset (hclass == SC_AUTO, a
                               # bp-relative stack offset, OR hclass ==
                               # SC_STATIC, an internal BSS label number -
                               # see SNAME above) - confirmed against
                               # v7/cc/c04.c's treeout() NAME case and
                               # against every 04_funcs .1.golden - see
                               # dump_name() below.
    21:  ("CON", [N("type", decode_type), N("value")]),
    25:  ("LCON", [N("type", decode_type), N("hi"), N("lo")]),
    13:  ("ITOP", [N("type", decode_type)]),
    58:  ("ITOL", [N("type", decode_type)]),
    59:  ("LTOI", [N("type", decode_type)]),
    107: ("CTOL", [N("type", decode_type)]),
    109: ("ITOC", [N("type", decode_type)]),
    35:  ("AMPER", [N("type", decode_type)]),
    36:  ("STAR", [N("type", decode_type)]),
    38:  ("COMPL", [N("type", decode_type)]),
    34:  ("EXCLA", [N("type", decode_type)]),
    30:  ("INCBEF", [N("type", decode_type)]),
    31:  ("DECBEF", [N("type", decode_type)]),
    32:  ("INCAFT", [N("type", decode_type)]),
    33:  ("DECAFT", [N("type", decode_type)]),
    40:  ("PLUS", [N("type", decode_type)]),
    41:  ("MINUS", [N("type", decode_type)]),
    42:  ("TIMES", [N("type", decode_type)]),
    43:  ("DIVIDE", [N("type", decode_type)]),
    44:  ("MOD", [N("type", decode_type)]),
    45:  ("RSHIFT", [N("type", decode_type)]),
    46:  ("LSHIFT", [N("type", decode_type)]),
    47:  ("AND", [N("type", decode_type)]),
    48:  ("OR", [N("type", decode_type)]),
    49:  ("EXOR", [N("type", decode_type)]),
    53:  ("LOGAND", [N("type", decode_type)]),
    54:  ("LOGOR", [N("type", decode_type)]),
    60:  ("EQUAL", [N("type", decode_type)]),
    61:  ("NEQUAL", [N("type", decode_type)]),
    62:  ("LESSEQ", [N("type", decode_type)]),
    63:  ("LESS", [N("type", decode_type)]),
    64:  ("GREATEQ", [N("type", decode_type)]),
    65:  ("GREAT", [N("type", decode_type)]),
    70:  ("ASPLUS", [N("type", decode_type)]),
    71:  ("ASMINUS", [N("type", decode_type)]),
    72:  ("ASTIMES", [N("type", decode_type)]),
    73:  ("ASDIV", [N("type", decode_type)]),
    74:  ("ASMOD", [N("type", decode_type)]),
    75:  ("ASRSH", [N("type", decode_type)]),
    76:  ("ASLSH", [N("type", decode_type)]),
    77:  ("ASSAND", [N("type", decode_type)]),
    78:  ("ASOR", [N("type", decode_type)]),
    79:  ("ASXOR", [N("type", decode_type)]),
    80:  ("ASSIGN", [N("type", decode_type)]),
    90:  ("QUEST", [N("type", decode_type)]),
    8:   ("COLON", [N("type", decode_type)]),
    97:  ("SEQNC", [N("type", decode_type)]),
    110: ("RFORCE", [N("type", decode_type)]),
    103: ("CBRANCH", [N("label"), N("cond"), N("line")]),

    # -- function calls (Milestone 4's "Function parameters and calls"
    # increment) - confirmed byte-for-byte against every 04_funcs
    # .1.golden with a call: --
    9:   ("COMMA", [N("type", decode_type)]),  # NOT the comma operator
                                                # (that's SEQNC=97 above) -
                                                # an OP_CALL argument-list
                                                # separator, chained
                                                # left-associatively for
                                                # 2+ arguments (a single
                                                # argument uses no COMMA at
                                                # all; zero arguments uses
                                                # NULLOP below instead)
    100: ("CALL", [N("type", decode_type)]),   # tr1 (already emitted) is
                                                # the callee - a NAME leaf
                                                # for a direct call, or a
                                                # STAR(TY_FUNC_INT) result
                                                # for an indirect call
                                                # through a function-
                                                # pointer variable; tr2
                                                # (already emitted) is the
                                                # argument tree (NULLOP,
                                                # a lone expression, or a
                                                # COMMA chain)
    # -- string literals (temp2 only - c0_parser.c's putstr(), v7/cc/
    # c00.c's function of the same name); confirmed byte-for-byte
    # against 05_arrptr/05_arrofptr.2.golden, 07_strlibc.2.golden and
    # 10_integ/04_strrev.2.golden: --
    200: ("BDATA", []),                        # followed by (1, byte)
                                                # pairs, then one lone 0
                                                # word - decoded by the
                                                # special case in dump();
                                                # a literal's run is split
                                                # ("0 BDATA") before every
                                                # 15th byte
    218: ("NULLOP", []),                       # v7/cc/c04.c's
                                                # treeout(NULL) shape - a
                                                # zero-argument call's
                                                # empty argument tree
}


# Every opcode name mutos_cc.h defines, for display purposes only when a
# byte matches a number this script has no confirmed argument shape for
# (see the "known but unhandled" case below) - never used to guess a shape.
ALL_OPCODE_NAMES = {
    0: "EOFC", 1: "SEMI", 2: "LBRACE", 3: "RBRACE", 4: "LBRACK", 5: "RBRACK",
    6: "LPARN", 7: "RPARN", 8: "COLON", 9: "COMMA", 10: "FSEL", 11: "CAST",
    12: "ETYPE", 13: "ITOP", 14: "PTOI", 15: "LTOP", 19: "KEYW", 20: "NAME",
    21: "CON", 22: "STRING", 23: "FCON", 24: "SFCON", 25: "LCON",
    26: "SLCON", 30: "INCBEF", 31: "DECBEF", 32: "INCAFT", 33: "DECAFT",
    34: "EXCLA", 35: "AMPER", 36: "STAR", 37: "NEG", 38: "COMPL", 39: "DOT",
    40: "PLUS", 41: "MINUS", 42: "TIMES", 43: "DIVIDE", 44: "MOD",
    45: "RSHIFT", 46: "LSHIFT", 47: "AND", 48: "OR", 49: "EXOR",
    50: "ARROW", 51: "ITOF", 52: "FTOI", 53: "LOGAND", 54: "LOGOR",
    56: "FTOL", 57: "LTOF", 58: "ITOL", 59: "LTOI", 60: "EQUAL",
    61: "NEQUAL", 62: "LESSEQ", 63: "LESS", 64: "GREATEQ", 65: "GREAT",
    66: "LESSEQP", 67: "LESSP", 68: "GREATQP", 69: "GREATP", 70: "ASPLUS",
    71: "ASMINUS", 72: "ASTIMES", 73: "ASDIV", 74: "ASMOD", 75: "ASRSH",
    76: "ASLSH", 77: "ASSAND", 78: "ASOR", 79: "ASXOR", 80: "ASSIGN",
    90: "QUEST", 91: "SIZEOF", 93: "MAX", 94: "MAXP", 95: "MIN",
    96: "MINP", 97: "SEQNC", 100: "CALL", 101: "MCALL", 102: "JUMP",
    103: "CBRANCH", 104: "INIT", 105: "SETREG", 107: "CTOL", 109: "ITOC",
    110: "RFORCE", 111: "BRANCH", 112: "LABEL", 113: "NLABEL",
    114: "RLABEL", 115: "STRASG", 200: "BDATA/SEOF", 201: "WDATA",
    202: "PROG", 203: "DATA", 204: "BSS", 205: "CSPACE", 206: "SSPACE",
    207: "SYMDEF", 208: "SAVE", 209: "RETRN", 210: "EVEN", 212: "PROFIL",
    213: "SWIT", 214: "EXPR", 215: "SNAME", 216: "RNAME", 217: "ANAME",
    218: "NULLOP", 219: "SETSTK", 220: "SINIT",
}

MARKER = 0xFE


class DecodeError(Exception):
    pass


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def eof(self):
        return self.pos >= len(self.data)

    def read_byte(self):
        if self.eof():
            raise DecodeError("unexpected end of file")
        b = self.data[self.pos]
        self.pos += 1
        return b

    def read_n(self):
        """Reads one 16-bit little-endian N field, sign-extended."""
        if self.pos + 2 > len(self.data):
            raise DecodeError("truncated N field at end of file")
        lo = self.data[self.pos]
        hi = self.data[self.pos + 1]
        self.pos += 2
        v = lo | (hi << 8)
        if v >= 0x8000:
            v -= 0x10000
        return v

    def read_s(self):
        """Reads one S field: '_' + up to MCC_NCPS bytes + NUL."""
        start = self.pos
        if self.eof():
            raise DecodeError("unexpected end of file reading a symbol")
        first = self.read_byte()
        if first != ord('_'):
            raise DecodeError(
                "expected a symbol ('_'-prefixed) at offset %d, found "
                "byte 0x%02x - c0 only ever writes non-empty names here"
                % (start, first))
        chars = []
        while True:
            if self.eof():
                raise DecodeError("unterminated symbol starting at offset %d" % start)
            b = self.read_byte()
            if b == 0:
                break
            chars.append(chr(b))
        return "_" + "".join(chars)


def dump(path):
    with open(path, "rb") as f:
        data = f.read()
    r = Reader(data)
    print("=== %s (%d bytes) ===" % (path, len(data)))
    while not r.eof():
        offset = r.pos
        op = r.read_byte()

        # An opcode byte is always immediately followed by the 0xFE
        # marker (c0_outcode.c's 'B' case). OP_SWIT's own trailing
        # table (a run of (label, value) N-pairs, terminated by one
        # lone zero N word - never an opcode-shaped byte pair) is
        # fully consumed by its own loop below, so this dispatch never
        # has to tell a bare N field apart from an opcode on its own.
        if r.pos < len(data) and data[r.pos] == MARKER:
            r.pos += 1  # consume the marker
            name = None
            spec = OPCODES.get(op)
            if spec is not None:
                name, fields = spec
            else:
                known_name = ALL_OPCODE_NAMES.get(op)
                print("[%6d] B  op=%d%s" % (
                    offset, op,
                    " (%s)" % known_name if known_name else ""))
                print(
                    "  -- no confirmed argument shape for this opcode "
                    "(mutos_c0 does not emit it) - stopping rather than "
                    "guessing how many bytes follow. Add it to OPCODES "
                    "in this script if you've confirmed its shape.")
                return

            if op == 20:  # OP_NAME: hclass, type, then EITHER a symbol
                          # name (hclass == SC_EXTERN, 12) or a numeric
                          # offset (any other hclass) - see OPCODES' own
                          # comment and v7/cc/c04.c's treeout().
                hclass = r.read_n()
                ty = r.read_n()
                parts = ["hclass=%s(%d)" % (decode_sc(hclass), hclass),
                          "type=%s(%d)" % (decode_type(ty), ty)]
                if hclass == 12:  # SC_EXTERN
                    parts.append('name="%s"' % r.read_s())
                else:
                    parts.append("offset=%d" % r.read_n())
                print("[%6d] B  %-8s %s" % (offset, name, "  ".join(parts)))
                continue

            parts = []
            for field in fields:
                if field[0] == "N":
                    _, label, decoder = field
                    v = r.read_n()
                    if decoder:
                        parts.append("%s=%s(%d)" % (label, decoder(v), v))
                    else:
                        parts.append("%s=%d" % (label, v))
                else:  # "S"
                    _, label = field
                    v = r.read_s()
                    parts.append('%s="%s"' % (label, v))
            print("[%6d] B  %-8s %s" % (offset, name, "  ".join(parts)))

            if op == 200:  # OP_BDATA: (1, value) pairs until a word that
                            # is not 1 (putstr()'s lone 0) - v7/cc/c11.c
                            # reads it the same way
                start = r.pos
                vals = []
                while True:
                    w = r.read_n()
                    if w != 1:
                        break
                    vals.append(r.read_n() & 0xFFFF)
                text = "".join(chr(v) if 32 <= v < 127 and v != 92
                               else "\\%o" % v for v in vals)
                print("[%6d] %d byte(s): %s  \"%s\"" % (
                    start, len(vals), " ".join("%02x" % v for v in vals),
                    text))
                if w != 0:
                    raise DecodeError("BDATA run at offset %d ended by %d, "
                                      "not 0" % (start, w))
                continue

            if op == 213:  # OP_SWIT: variable trailing (label,value) pairs
                            # terminated by one lone zero N word
                while True:
                    lab = r.read_n()
                    if lab == 0:
                        print("[%6d] N  (switch table terminator, 0)" % (r.pos - 2))
                        break
                    val = r.read_n()
                    print("[%6d] NN case  label=%d value=%d" % (r.pos - 4, lab, val))
        else:
            raise DecodeError(
                "byte 0x%02x at offset %d is not followed by the 0xFE "
                "opcode marker - malformed stream, or (less likely) a "
                "genuine 0x%02xFE-shaped N field landed where an opcode "
                "was expected; re-check the surrounding bytes with "
                "`od -A d -t u1`" % (op, offset, op))


def main(argv):
    if len(argv) < 2:
        sys.stderr.write(__doc__)
        return 1
    status = 0
    for i, path in enumerate(argv[1:]):
        if i:
            print()
        try:
            dump(path)
        except (DecodeError, OSError) as e:
            print("error: %s: %s" % (path, e), file=sys.stderr)
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv))
