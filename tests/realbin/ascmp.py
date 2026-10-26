#!/usr/bin/env python3
"""ascmp.py - mutos_as against the original as (plan step 5: a measurement).

The original MUTOS 1700 as (mutos/mutos_bin/as.golden.base64.txt) runs under
mutosemu.py; mutos_as (src/mutos_as/) runs on the host. Unlike mutos_cpp,
mutos_ld, mutos_cc and mutos_c2, mutos_as is not a port of the original
(docs/TOOLCHAIN_PLAN.md, decision 9: not ported now; a differential fuzzer
measures the differences, a port is decided after that). So this tool does
not demand "the same": it puts every run into a class and counts them.

Both get the same command line in two copies of the same directory. The
classes, from the most to the least serious:

    OBJ-DIFF       both accept (exit status 0), the objects differ
    PORT-ACCEPTS   the original rejects (status != 0), the port accepts
    PORT-REJECTS   the original accepts, the port rejects
    both-reject    both reject; flags say what else differs
    obj-same       both accept, the objects are the same, but something else
                   differs (flags)
    same           exit status, stdout, stderr, every file's bytes and mode
                   and the set of files are the same

and the flags of a run that is not "same": status (the exit statuses), stdout,
stderr (the messages), files (the set of files left), bytes (a file both
have, different bytes), mode (a file both have, different permissions).

The port may be given extra options the original does not know (--port-args,
e.g. -1 for the 80186 instructions, plan section 4); they are added to its
command line only.

Usage:
    ascmp.py goldens [-j N] [--port PATH] [--port-args ARG] [--show N]
                     [--keep DIR]
        every source with a real-hardware golden object (tests/mutos_as:
        kernel_opt, kernel_nonopt, libc_recon, float_coverage) and every
        compiler output (tests/mutos_cc/**/*.s.golden), assembled by both
        with a matrix of command lines (cc's -gC, plain); where a real
        golden object exists, both are checked against it too
    ascmp.py cases [same options]
        hand-written command lines and inputs: the output name, stdin,
        several inputs, a missing input, options (also the ones the
        manual names), syntax errors, the original's limits
    both take --write FILE (save the class of every run that is not "same"
    - one line per run, without the details) and --check FILE (compare with
    such a file, exit status 1 on any difference either way: a new
    difference, or one that is gone and must be taken out of the file and
    out of the documentation); tests/realbin/ascmp_goldens.txt and
    ascmp_cases.txt are the files make check-realbin checks against
    ascmp.py options [-j N] [--show N]
        the original against itself: what -g, -C, -l and -e change on
        every source of "goldens" (the plan's open question)
    ascmp.py files [ARG...]
        one command line, in a copy of the current directory; prints
        both runs in full

As a module: compare(args, srcdir, port=PORT, port_args=(), keep=None)
-> Result; classify(result) -> (kind, flags).

Requires the Python module "unicorn" (pip install unicorn).
"""
import base64
import concurrent.futures
import glob
import io
import os
import resource
import shutil
import stat
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'src', 'mutos_as', 'mutos_as')
AS_TESTS = os.path.join(ROOT, 'tests', 'mutos_as')
CC_TESTS = os.path.join(ROOT, 'tests', 'mutos_cc')
MAX_INSNS = 3_000_000_000   # the biggest kernel source takes some 10 s
OUT_LIMIT = 1 << 20         # output past this: stopped
PORT_CPU = 60               # seconds
WALL = 0                    # seconds of wall clock for the original, 0: none
                            # (asfuzz.py sets one: a case it does not finish
                            # is tried a hundred times by the reduction)

# What the sources of "goldens" are run with: cc's own call (as -gC -o x.o
# tmp), the same without the options, and the original's -e.
MATRIX = [['-gC', '-o', 'x.o', 'x.s'],
          ['-o', 'x.o', 'x.s']]
KINDS = ['same', 'obj-same', 'both-reject', 'PORT-REJECTS', 'PORT-ACCEPTS',
         'OBJ-DIFF']
SERIOUS = ('OBJ-DIFF', 'PORT-ACCEPTS', 'PORT-REJECTS')


class Result:
    """What one command line did in the original and in the port."""
    def __init__(self, args, port_args):
        self.args = list(args)
        self.port_args = list(port_args)
        self.ost = self.pst = None       # exit statuses (None: no finish)
        self.oout = self.pout = b''      # stdout
        self.oerr = self.perr = b''      # stderr
        self.ofiles = self.pfiles = {}   # {name: (bytes, mode)}
        self.why = ''                    # why the original did not finish


class _Limit(Exception):
    pass


class _Capped(io.BytesIO):
    def write(self, b):
        n = super().write(b)
        if self.tell() > OUT_LIMIT:
            raise _Limit()
        return n


def snapshot(d):
    """{name: (bytes, mode)} of a directory's files."""
    out = {}
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        st = os.lstat(p)
        if stat.S_ISDIR(st.st_mode):
            out[name + '/'] = (b'', st.st_mode & 0o777)
        else:
            with open(p, 'rb') as f:
                out[name] = (f.read(), st.st_mode & 0o777)
    return out


def run_orig(args, cwd, stdin=b'', max_insns=None):
    """(status or None, stdout, stderr, why it did not finish)."""
    if max_insns is None:
        max_insns = MAX_INSNS       # looked up now: asfuzz.py sets a lower one
    out, err = _Capped(), _Capped()
    emu = mutosemu.Emu(mutosemu.load_image('as'), ['as'] + list(args),
                       cwd=cwd, stdin=io.BytesIO(stdin), stdout=out,
                       stderr=err, eof_limit=1000)
    why = ''
    st = None
    if WALL:
        # a wall-clock limit (Unicorn's timeout, in microseconds): a stop on
        # it is a run that did not finish, like the instruction limit
        real_start = emu.uc.emu_start
        emu.uc.emu_start = lambda begin, until, timeout=0, count=0: real_start(
            begin, until, timeout=WALL * 1000000, count=count)
    try:
        st = emu.run(max_insns)
    except _Limit:
        why = 'endless: messages past %d bytes' % OUT_LIMIT
    except RuntimeError as e:
        why = str(e)
        if 'stopped without exit' in why or 'past the end of a file' in why:
            why = 'endless: ' + why
    return st, out.getvalue(), err.getvalue(), why


def _limits():
    resource.setrlimit(resource.RLIMIT_CPU, (PORT_CPU, PORT_CPU))
    resource.setrlimit(resource.RLIMIT_FSIZE, (16 << 20, 16 << 20))


def run_port(args, cwd, stdin=b'', port=PORT):
    """(status, stdout, stderr); status None for a run a limit stopped."""
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe:
        r = subprocess.run([port] + list(args), cwd=cwd, input=stdin,
                           stdout=fo, stderr=fe, preexec_fn=_limits)
        fo.seek(0)
        fe.seek(0)
        out, err = fo.read()[:OUT_LIMIT + 1], fe.read()
    st = r.returncode
    if st < 0:
        st = None
    return st, out, err


def diff_at(a, b):
    n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
             min(len(a), len(b)))
    return 'differs at byte %d (%d vs %d bytes)' % (n, len(a), len(b))


def compare(args, srcdir, port=PORT, port_args=(), stdin=b'', keep=None):
    """The port against the original: the same arguments (the port's with
    port_args first) in copies of srcdir. keep: a directory to leave both
    copies in on a serious difference. Returns a Result."""
    res = Result(args, port_args)
    with tempfile.TemporaryDirectory(prefix='ascmp') as tmp:
        do, dp = os.path.join(tmp, 'orig'), os.path.join(tmp, 'port')
        shutil.copytree(srcdir, do, symlinks=True)
        shutil.copytree(srcdir, dp, symlinks=True)
        res.ost, res.oout, res.oerr, res.why = run_orig(args, do, stdin)
        res.pst, res.pout, res.perr = run_port(list(port_args) + list(args),
                                               dp, stdin, port)
        res.ofiles, res.pfiles = snapshot(do), snapshot(dp)
        kind, _ = classify(res)
        if keep and kind in SERIOUS:
            os.makedirs(keep, exist_ok=True)
            k = tempfile.mkdtemp(dir=keep, prefix='case')
            shutil.copytree(do, os.path.join(k, 'orig'), symlinks=True)
            shutil.copytree(dp, os.path.join(k, 'port'), symlinks=True)
            shutil.copytree(srcdir, os.path.join(k, 'src'), symlinks=True)
            with open(os.path.join(k, 'args'), 'w') as f:
                f.write(' '.join(list(port_args) + list(args)) + '\n'
                        + kind + ': ' + detail(res) + '\n')
    return res


def classify(r):
    """(kind, flags) of a Result; flags is a sorted list."""
    flags = set()
    if r.ost != r.pst:
        flags.add('status')
    if r.oout != r.pout:
        flags.add('stdout')
    if r.oerr != r.perr:
        flags.add('stderr')
    if set(r.ofiles) != set(r.pfiles):
        flags.add('files')
    for n in set(r.ofiles) & set(r.pfiles):
        ob, om = r.ofiles[n]
        pb, pm = r.pfiles[n]
        if ob != pb:
            flags.add('bytes')
        if om != pm:
            flags.add('mode')
    if not flags:
        return 'same', []
    o_ok = r.ost == 0
    p_ok = r.pst == 0
    if o_ok and p_ok:
        return ('OBJ-DIFF' if 'bytes' in flags else 'obj-same'), sorted(flags)
    if p_ok:
        return 'PORT-ACCEPTS', sorted(flags)
    if o_ok:
        return 'PORT-REJECTS', sorted(flags)
    return 'both-reject', sorted(flags)


def _first(b):
    line = b.decode('latin-1').split('\n', 1)[0]
    return line[:70]


def detail(r):
    """One line on what differs."""
    kind, flags = classify(r)
    parts = []
    if 'status' in flags:
        parts.append('status %s vs %s' % (r.ost, r.pst))
    if 'bytes' in flags:
        for n in sorted(set(r.ofiles) & set(r.pfiles)):
            if r.ofiles[n][0] != r.pfiles[n][0]:
                parts.append('%s %s' % (n, diff_at(r.ofiles[n][0],
                                                   r.pfiles[n][0])))
                break
    if 'files' in flags:
        parts.append('files %s vs %s' % (sorted(set(r.ofiles) - set(r.pfiles)),
                                         sorted(set(r.pfiles) - set(r.ofiles))))
    if 'mode' in flags:
        for n in sorted(set(r.ofiles) & set(r.pfiles)):
            if r.ofiles[n][1] != r.pfiles[n][1]:
                parts.append('%s mode %o vs %o' % (n, r.ofiles[n][1],
                                                   r.pfiles[n][1]))
                break
    if 'stderr' in flags:
        parts.append('stderr "%s" vs "%s"' % (_first(r.oerr), _first(r.perr)))
    if 'stdout' in flags:
        parts.append('stdout "%s" vs "%s"' % (_first(r.oout), _first(r.pout)))
    if r.why:
        parts.append('original: ' + r.why)
    return '; '.join(parts)


# ---- the sources -------------------------------------------------------

def _b64(path):
    return base64.b64decode(open(path, 'rb').read())


def golden_sources():
    """[(label, source path, golden object bytes or None)]."""
    out = []
    for d in ('kernel_opt', 'kernel_nonopt', 'libc_recon', 'float_coverage'):
        for s in sorted(glob.glob(os.path.join(AS_TESTS, d, '*.s'))):
            g = s[:-2] + '.o.golden_base64.txt'
            out.append((os.path.relpath(s, ROOT), s,
                        _b64(g) if os.path.exists(g) else None))
    for s in sorted(glob.glob(os.path.join(CC_TESTS, '**', '*.s.golden'),
                              recursive=True)):
        out.append((os.path.relpath(s, ROOT), s, None))
    for s in sorted(glob.glob(os.path.join(AS_TESTS, 'v30_speculative',
                                           '*.s'))):
        out.append((os.path.relpath(s, ROOT), s, None))
    return out


def _golden_one(job):
    label, src, golden, port, port_args, keep = job
    results = []
    with tempfile.TemporaryDirectory(prefix='ascmpsrc') as d:
        shutil.copyfile(src, os.path.join(d, 'x.s'))
        for args in MATRIX:
            r = compare(args, d, port, port_args, keep=keep)
            extra = ''
            # the real-hardware goldens were made by a plain "as -o x.o x.s"
            # (no -C: they keep the compiler's L labels, see STATUS.md)
            if golden is not None and args == ['-o', 'x.o', 'x.s']:
                og = r.ofiles.get('x.o', (None,))[0] == golden
                pg = r.pfiles.get('x.o', (None,))[0] == golden
                extra = 'golden:orig=%s,port=%s' % ('Y' if og else 'N',
                                                    'Y' if pg else 'N')
            results.append((' '.join(args[:-3]) or '-', r, extra))
    return label, results


# ---- hand-written cases ------------------------------------------------

OK = b'.text\n_f:\n\tnop\n\tret\n'


def cases():
    """[(name, args, files {name: bytes}, stdin)]."""
    cs = []

    def case(name, args, files=None, stdin=b''):
        cs.append((name, list(args), dict(files or {}), stdin))

    ok = {'x.s': OK}
    # the output name
    case('default name', ['x.s'], ok)
    case('-o name', ['-o', 'y.o', 'x.s'], ok)
    case('-oNAME attached', ['-oy.o', 'x.s'], ok)
    case('-o last, no name', ['x.s', '-o'], ok)
    case('-o twice', ['-o', 'y.o', '-o', 'z.o', 'x.s'], ok)
    case('-o before and after', ['-o', 'y.o', 'x.s', '-o', 'z.o'], ok)
    case('input without .s', ['-o', 'y.o', 'x.c'], {'x.c': OK})
    case('input without .s, no -o', ['x.c'], {'x.c': OK})
    case('input a.out-like name', ['prog'], {'prog': OK})
    case('input with a directory', ['d/x.s'], {'d/x.s': OK})
    case('input in a dir, -o', ['-o', 'y.o', 'd/x.s'], {'d/x.s': OK})
    case('input, dot only', ['.s'], {'.s': OK})
    case('long names', ['a' * 40 + '.s'], {'a' * 40 + '.s': OK})
    # the inputs
    case('stdin', [], {}, OK)
    case('stdin and -o', ['-o', 'y.o'], {}, OK)
    case('no input, empty stdin', [], {}, b'')
    case('two inputs', ['-o', 'y.o', 'a.s', 'b.s'],
         {'a.s': b'.text\n_a:\n\tnop\n', 'b.s': b'_b:\n\tret\n'})
    case('three inputs', ['a.s', 'b.s', 'c.s'],
         {'a.s': OK, 'b.s': OK, 'c.s': OK})
    case('missing input', ['-o', 'y.o', 'nofile.s'], {})
    case('input is a directory', ['-o', 'y.o', 'd.s'], {'d.s/x': b''})
    case('empty input', ['-o', 'y.o', 'x.s'], {'x.s': b''})
    case('only comments', ['-o', 'y.o', 'x.s'], {'x.s': b'| nothing\n'})
    case('no last linefeed', ['-o', 'y.o', 'x.s'], {'x.s': b'.text\n\tnop'})
    # the options
    for o in ('-g', '-C', '-gC', '-Cg', '-e', '-l', '-gCel', '-L', '-W', '-D',
              '-T', '-X', '-v', '-1', '-u', '-gx', '-', '--'):
        case('option %s' % o, [o, '-o', 'y.o', 'x.s'], ok)
    case('options split', ['-g', '-C', '-o', 'y.o', 'x.s'], ok)
    case('options after the input', ['-o', 'y.o', 'x.s', '-g'], ok)
    case('-o is an option letter', ['-eo', 'y.o', 'x.s'], ok)
    case('-oy.o -e', ['-oy.o', '-e', 'x.s'], ok)
    # what -e leaves out
    loc = (b'.globl _f\n.text\n_f:\nL1:\n\tnop\n1:\n\tjmp 1b\n.data\n_d:\n'
           b'\t.word _f, L1\n.comm _c, 10\n')
    case('symbols, no -e', ['-o', 'y.o', 'x.s'], {'x.s': loc})
    case('symbols, -e', ['-e', '-o', 'y.o', 'x.s'], {'x.s': loc})
    # errors
    case('syntax error', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n\tmov ax,\n'})
    case('two errors', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n\tmov ax,\n\tadd ,\n'})
    case('redefined symbol', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\nx:\nx:\n\tnop\n'})
    case('undefined symbol', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n\tjmp nowhere\n'})
    case('bad operand count', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n\tmov ax,bx,cx\n'})
    case('unknown mnemonic, bare', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n\tfoo\n\tret\n'})
    case('unknown mnemonic, operand', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n\tfoo ax\n\tret\n'})
    case('unknown mnemonic, two operands', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n\tfoo ax,bx\n\tret\n'})
    case('bad character', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n\tmov ax,\x01\n'})
    case('byte 0200', ['-o', 'y.o', 'x.s'], {'x.s': b'.text\n\tnop\n\x80\n'})
    case('NUL byte', ['-o', 'y.o', 'x.s'], {'x.s': b'.text\n\tnop\n\x00\n'})
    # AS-14: bytes of 0200 and up in code are read in a way that depends on the value
    # (0200 loses the code of the file, 0351 sends the original into unmapped memory)
    case('byte 0351, the original crashes', ['-o', 'y.o', 'x.s'], {'x.s': b'.text\n\tnop\n\xe9\n'})
    case('byte 0247, a syntax error', ['-o', 'y.o', 'x.s'], {'x.s': b'.text\n\tnop\n\xa7\n'})
    case('CR LF', ['-o', 'y.o', 'x.s'], {'x.s': b'.text\r\n\tnop\r\n'})
    # what the 80186 instructions do without -1 (plan section 4)
    for name, line in (('pusha', b'\tpusha\n'), ('popa', b'\tpopa\n'),
                       ('leave', b'\tleave\n'), ('insb', b'\tinsb\n'),
                       ('insw', b'\tinsw\n'), ('outsb', b'\toutsb\n'),
                       ('outsw', b'\toutsw\n'),
                       ('enter', b'\tenter *8.,*0.\n'),
                       ('bound', b'\tbound ax,(bx)\n'),
                       ('push imm word', b'\tpush #1000.\n'),
                       ('push imm byte', b'\tpush *5.\n'),
                       ('imul imm', b'\timul ax,#10.\n'),
                       ('imul 3 operands', b'\timul cx,ax,*10.\n'),
                       ('shl imm', b'\tshl ax,*3.\n'),
                       ('shr imm', b'\tshr bx,*4.\n'),
                       ('rol imm', b'\trol ax,*2.\n'),
                       ('shl mem imm', b'\tshl (bx),*3.\n')):
        case('186: ' + name, ['-o', 'y.o', 'x.s'],
             {'x.s': b'.text\n_f:\n' + line + b'\tret\n'})
    # found by asfuzz.py (2026-10-25) and fixed in mutos_as: kept here
    for name, body in (
            ('shift of a global, cl', b'\tsarb 2.+_gs,cl\n\tshl _gs,cl\n\trorb _gs,*1\n'),
            ('test register, address', b'\ttest cx,_d\n\ttestb al,_d\n\ttest cx,#_d\n'),
            ('xchg byte registers', b'\txchg al,bl\n\txchg bl,ah\n\txchg cx,dx\n'),
            ('int 3 is one byte', b'\tint *3.\n\tint *1+2.\n\tint *13.\n\tnop\n'),
            ('ret and reti with a count', b'\tret *6.\n\treti #4.\n\tret\n\treti\n'),
            ('xchg with memory', b'\txchg ax,_gs\n\txchg *4.(bx),cx\n\txchgb cl,*4.(bx)\n\txchg al,_gs\n'),
            ('test memory, register', b'\ttest _gs,cx\n\ttestb *4.(bx),dl\n\ttest (si),ax\n'),
            ('pop memory', b'\tpop _gs\n\tpop *4.(bx)\n\tpop (si)\n'),
            ('lds, les with an address', b'\tlds ax,_gs\n\tles bx,10.\n\tlds si,_gs+2\n'),
            ('incb, decb of a register', b'\tincb cl\n\tdecb bx\n\tincb al\n'),
            ('displacement taken as 16 bits', b'\tmov ax,65535.(bx)\n\tmov ax,/ff80(bx)\n\tmov ax,65536.(bx)\n\tmov ax,32768.(bx)\n'),
            ('dot in a displacement', b'\tnop\n\tmov ax,.-1.(bx)\n\tmov .+2.(bx),*5.\n'),
            ('eight labels in front', b'L132:L131:L129:L127:L124:L115:L90:L111:cmp\tdi,#128.\n\tbne\tL90\n'),
            ('twenty labels in front', b'\n'.join([b'']) + b''.join(b'La%02d:' % i for i in range(20)) + b'nop\n\tjmp\tLa00\n'),
            ('.asciz escapes', b'.data\n.asciz *a\\nb\\tc\\\\d\\*e\\012f\\377g\\400h*\n'),
            ('.asciz odd digits', b'.data\n.asciz *a\\19b\\129c\\378d\\108e\\1089f\\8g\\x41h*\n'),
            ('.asciz not closed', b'.data\n.asciz *abc\n'),
            ('.asciz text after', b'.data\n.asciz *abc*def\n'),
            ('.asciz comment after', b'.data\n.asciz *abc* | c\n'),
            # the directives (asfuzz.py --words, 2026-10-25): the original has
            # .blkw, .ascii, .end, .list, .nlist, .insrt, .dlptr, .targref and
            # .textref besides the ones mutos_as had; any other .name is a symbol
            ('.blkw in each segment', b'.text\n.blkw 5\n.data\n.blkw 3\n.bss\nb: .blkw 4\n.blkb 1\nc: .blkw 2\n'),
            ('.ascii', b'.data\n.ascii *abc*\n.ascii *d\\n*\n.byte 7\n'),
            ('.end stops the source', b'\tnop\n.end\n\tnop\n.garbage 5 5\n'),
            ('label on .end', b'\tnop\nfoo: .end\n\tnop\n'),
            ('.list and .nlist', b'\tnop\n.list\n\tnop\n.nlist\n\tnop\n'),
            ('.targref and .textref alone', b'\t.targref\n.textref\n\tnop\n'),
            ('odd bss size is made even', b'.bss\nb: .blkb 3.\n'),
            ('.name alone is a symbol', b'\tnop\n.foo\n.data\n.evn\n'),
            ('.name = value', b'\t.baz = 3\n\t.baz2 = .baz\n'),
            ('.name in an expression', b'\tmov ax,#.baz\n\t.word .baz\n'),
            ('.name with an operand', b'\t.foo 1\n'),
            ('.dlptr', b'.text\n.dlptr 5\n'),
            ('.insrt without quotes', b'.text\n.insrt y.s\n'),
            # the size of a displacement (asfuzz.py, 2026-10-25): a byte for an
            # absolute value of symbols defined by then, a word for a label,
            # an external one or a symbol defined further down
            ('displacement from an assigned constant', b'K = 5\n\tmov ax,K(bx)\n\tmov K(bx),#1\n\tmov ax,K+300.(bx)\n\tmov ax,-K(bp)\n\tmov ax,F(bx)\nF = 2\n\tmov ax,F(bx)\n'),
            ('displacement from label differences', b'a: nop\nb: nop\n\tmov ax,b-a(bx)\n\tmov ax,a-b(bx)\n\tmov ax,a-a(di)\n\tmov ax,a(bx)\n\tmov ax,b-a+300.(bx)\nL2:\nL101: subb cl,L2-L2(di)\n\tsubb cl,L3-L3(di)\nL3:\n'),
            ('displacement from a difference, other segments', b'.data\nd1: .byte 1\nd2: .byte 2\n.text\n\tmov ax,d2-d1(bx)\n\tmov ax,d2(bx)\n'),
            # the indirect forms of call and jmp (asfuzz.py, 2026-10-25)
            ('call and jmp through memory', b'\tcall @(bx)\n\tjmp @(di)\n\tcall @(bp)\n\tcall @(bx)(si)\n\tcall @(bp)(di)\n\tcall @10(bp)(di)\n\tcall @_gs\n\tcall @_gs+2\n\tjmp @_gs(bx)\n\tcall @*4.(bx)\n'),
            ('calli and jmpi through memory', b'\tcalli @(bx)\n\tcalli @_gs\n\tjmpi @(si)\n\tjmpi @_gs\n\tjmpi @4.(bx)\n\tcalli @bx\n\tjmpi @ax\n'),
            ('call and jmp of a register', b'\tcall ax\n\tjmp bx\n\tcall al\n\tcall @cl\n\tjmpi ax\n\tcalli sp\n\tbr si\n\tbr @(si)\n'),
            # kept: a conditional branch to an indirect address (the original puts EA
            # where the jmp needs FF, AS-11) and jmpi / calli of a plain address (the
            # original: the opcode, the word and four zero bytes, 7 bytes, AS-12);
            # mutos_as refuses them
            ('conditional branch to a register', b'\tble @cx\n\tnop\n'),
            ('conditional branch to memory', b'\tbeq @(bx)\n\tbgt @_gs\n\tblo @10(bx)\n\tnop\n'),
            # kept: forms and words the original takes and mutos_as does not (STATUS.md)
            ('pop cs', b'\tpop cs\n'),
            ('a name with a dot alone (mov.b)', b'\tmov.b\n\tnop\n'),
            ('push of a byte register', b'\tpush bl\n\tnop\n'),
            ('pop of a byte register', b'\tpop al\n\tnop\n'),
            ('lea into a byte register', b'\tlea al,*4.(bx)\n\tnop\n'),
            ('movb without an operand', b'\tmovb\n\tnop\n'),
            ('cmpb without an operand', b'\tcmpb\n\tnop\n'),
            ('bc and bnc', b'L1:\tbc L1\n\tbnc L1\n'),
            ('scab and scaw', b'\tscab\n\tscaw\n\tnop\n'),
            ('a symbol plus a number alone', b'\t_x+2\n\tnop\n'),
            ('jmpi of an address', b'\tjmpi 5\n\tnop\n'),
            ('calli of an address', b'\tcalli _gs\n\tnop\n'),
            ('call (reg) without @', b'\tcall (bx)\n'),
            ('call disp(reg) without @', b'\tcall 10(bx)\n'),
            ('jmp *disp(reg) without @', b'\tjmp *10(bx)\n'),
            ('@ on a data instruction', b'\tmov ax,@bx\n'),
            ('@(reg) on a data instruction', b'\tpush @(bx)\n'),
            ('@ with a marker and a direct address', b'\tcall @*_gs\n'),
            # the symbols are numbered in the order they are written (asfuzz.py, 2026-10-25)
            ('symbols in operand order', b'\tsbb _tab,#_buf2\n\tmov _p,#_q\n\tadd _u+4.,#_v\n\tcmp _w,#_w\n'),
            ('a number alone in .bss', b'.bss\n/0\n5\nab: 7\n.text\nnop\n'),
            ('sums of many terms', b'.data\n' + b'+'.join([b'1'] * 9) + b'\n' + b'+'.join([b'2'] * 30) + b'\n.word ' + b'+'.join([b'3'] * 31) + b'\n'),
            # kept: mutos_as refuses an operand of more than 64 tokens, the original takes 127 characters
            ('a sum of 40 terms', b'.data\n.word ' + b'+'.join([b'3'] * 40) + b'\n'),
            # a line of 128 characters or more is cut by the original after the 127th, and
            # the rest is read as a line of its own - a comment's tail too (AS-13, kept:
            # here the tail "T" is a symbol in the original's object)
            ('comment line of 127 characters', b'\tnop\n|' + b'c' * 125 + b'T\n\tnop\n'),
            ('comment line of 128 characters', b'\tnop\n|' + b'c' * 126 + b'T\n\tnop\n'),
            ('statement and comment of 128 characters', b'\tnop\t|' + b'c' * 121 + b'T\n\tnop\n'),
            # character constants (asfuzz.py, 2026-10-25): 'a', 'ab', \n \t \b \f \r, any other \c is c
            ('character constants', b".data\n.byte 'a','b'\n.word 'ab','\\n','\\t','\\\\','\\'','\\0','|',',','*',' ','\\q'\n.byte 'a'-/20\nK = 'z'\n.word K, -'a', 'a'*2\n.text\n\tmov al,#'a'\n\tcmpb (bx),*'\\n'\n\tmov ax,#'A'+1\n\tmov ax,'a'(bx)\n"),
            ('character constant of three', b".data\n.word 'abc'\n"),
            ('character constant not closed', b".data\n.word 'a\n"),
            ('character constant, empty', b".data\n.word ''\n"),
            ('two numbers in one operand', b".data\n.word 5 6\n"),
            ('parentheses in an expression', b".data\n.word 1+(2)\n"),
            # kept: the original puts out a broken object here (AS-10)
            ('(bp) after a displacement', b'\tmov ax,10(bx)\n\tsub (bp),#16.\n\tsub (bp),#17.\n'),
            # kept: the original puts out a broken object here (AS-07)
            ('odd .data then .bss', b'.data\n.byte 1\n.bss\nb: .blkb 4.\n'),
            ('odd .data, .bss, then more .data', b'.data\n.byte 1\n.bss\nb: .blkb 4.\n.data\n.byte 5\n')):
        case(name, ['-o', 'y.o', 'x.s'],
             {'x.s': (b'.globl _gs\n.text\n' if body.startswith(b'\t') else b'.text\n') + body})
    # kept: under -C the original writes an "L" name once more for each .globl / .comm of it
    case('.globl L twice, -C', ['-C', '-o', 'y.o', 'x.s'],
         {'x.s': b'.globl L1\n.globl L1\n.text\n\tnop\n'})
    # .insrt "file" (the original: ten files deep, the eleventh is an error)
    case('.insrt', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n\tnop\n.insrt "y.s"\n\tret\n', 'y.s': b'\tnop\n\tnop\n'})
    case('.insrt nested', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n.insrt "z.s"\n\tret\n', 'z.s': b'.insrt "y.s"\n\tint *4.\n',
          'y.s': b'\tnop\n'})
    case('.insrt missing file', ['-o', 'y.o', 'x.s'],
         {'x.s': b'.text\n.insrt "nofile.s"\n'})
    for depth in (10, 11):
        files = {'x.s': b'.text\n.insrt "f1.s"\n'}
        for i in range(1, depth + 1):
            files['f%d.s' % i] = (b'.insrt "f%d.s"\n' % (i + 1)) if i < depth else b'\tnop\n'
        case('.insrt %d deep' % depth, ['-o', 'y.o', 'x.s'], files)
    return cs


def _case_one(job):
    (name, args, files, stdin), port, port_args, keep = job
    with tempfile.TemporaryDirectory(prefix='ascmpsrc') as d:
        for n, t in files.items():
            p = os.path.join(d, n)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as f:
                f.write(t)
        return name, args, compare(args, d, port, port_args, stdin, keep)


# ---- the options' effect (the original against itself) ----------------

def _options_one(job):
    label, src = job
    out = {}
    with tempfile.TemporaryDirectory(prefix='ascmpsrc') as d:
        shutil.copyfile(src, os.path.join(d, 'x.s'))
        base = None
        for opt in ([], ['-g'], ['-C'], ['-l'], ['-e'], ['-gCl']):
            dd = tempfile.mkdtemp(dir=d)
            shutil.copyfile(src, os.path.join(dd, 'x.s'))
            st, so, se, why = run_orig(opt + ['-o', 'x.o', 'x.s'], dd)
            snap = snapshot(dd)
            key = (st, so, se, snap.get('x.o', (None, None)))
            if base is None:
                base = key
            out[' '.join(opt) or '(none)'] = (key == base, key[3][0])
    return label, out


def main(argv):
    jobs, port, show, keep, port_args = os.cpu_count() or 1, PORT, 0, None, []
    check = write = None
    args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ('-j', '--port', '--show', '--keep', '--port-args', '--check',
                 '--write') and (not args or args[0] != 'files'):
            v = argv[i + 1]
            if a == '-j':
                jobs = int(v)
            elif a == '--check':
                check = v
            elif a == '--write':
                write = v
            elif a == '--port':
                port = os.path.abspath(v)
            elif a == '--show':
                show = int(v)
            elif a == '--keep':
                keep = os.path.abspath(v)
            else:
                port_args.append(v)
            i += 2
        else:
            args.append(a)
            i += 1
    if not args or args[0] not in ('goldens', 'cases', 'options', 'files'):
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])

    if args[0] == 'files':
        with tempfile.TemporaryDirectory(prefix='ascmpsrc') as d:
            shutil.copytree('.', d, dirs_exist_ok=True, symlinks=True)
            r = compare(args[1:], d, port, port_args)
        kind, flags = classify(r)
        print('%s %s' % (kind, ','.join(flags)))
        for who, st, out, err, files in (
                ('original', r.ost, r.oout, r.oerr, r.ofiles),
                ('port', r.pst, r.pout, r.perr, r.pfiles)):
            print('--- %s: status %s' % (who, st))
            print('stdout: %r' % out)
            print('stderr: %r' % err)
            print('files:  %s' % {n: (len(b), '%o' % m)
                                  for n, (b, m) in files.items()})
        if r.why:
            print('original did not finish: ' + r.why)
        return 0

    if args[0] == 'options':
        work = [(l, s) for l, s, _ in golden_sources()]
        changed = {}
        n = 0
        with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
            for label, out in ex.map(_options_one, work):
                n += 1
                for opt, (same, _) in out.items():
                    if not same:
                        changed.setdefault(opt, []).append(label)
        print('the original against itself on %d sources (-o x.o x.s):' % n)
        for opt in ('-g', '-C', '-l', '-e', '-gCl'):
            ls = changed.get(opt, [])
            print('  %-5s changes the run on %d of %d%s'
                  % (opt, len(ls), n, (': ' + ', '.join(ls[:show])) if show
                     and ls else ''))
        return 0

    counts = {}
    flagcounts = {}
    gold = {'orig': [0, 0], 'port': [0, 0]}
    serious = []
    notsame = []
    runs = 0

    def tally(label, mode, r, extra=''):
        nonlocal runs
        kind, flags = classify(r)
        runs += 1
        if kind != 'same':
            notsame.append('%s [%s]: %s %s' % (label, mode, kind, ','.join(flags)))
        counts[kind] = counts.get(kind, 0) + 1
        for f in flags:
            flagcounts.setdefault(kind, {})
            flagcounts[kind][f] = flagcounts[kind].get(f, 0) + 1
        if extra.startswith('golden:'):
            og, pg = extra[7:].split(',')
            gold['orig'][0] += 1
            gold['port'][0] += 1
            gold['orig'][1] += og.endswith('Y')
            gold['port'][1] += pg.endswith('Y')
        line = '%s [%s]: %s %s: %s' % (label, mode, kind, ','.join(flags),
                                       detail(r))
        if kind in SERIOUS:
            serious.append(line)
        elif show and kind != 'same':
            print(line)

    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        if args[0] == 'goldens':
            work = [(l, s, g, port, port_args, keep)
                    for l, s, g in golden_sources()]
            for label, results in ex.map(_golden_one, work):
                for mode, r, extra in results:
                    tally(label, mode, r, extra)
        else:
            work = [(c, port, port_args, keep) for c in cases()]
            for name, cargs, r in ex.map(_case_one, work):
                tally(name, ' '.join(cargs), r)
    for s in serious[:show or 25]:
        print(s)
    if len(serious) > (show or 25):
        print('... and %d more' % (len(serious) - (show or 25)))
    print('as port against the original (%s): %d runs, %s'
          % (args[0], runs, ', '.join('%d %s' % (counts[k], k)
                                      for k in KINDS if k in counts)))
    for k in KINDS:
        if k in flagcounts:
            print('  %-13s %s' % (k, ', '.join(
                '%s %d' % (f, c) for f, c in sorted(flagcounts[k].items()))))
    if gold['orig'][0]:
        print('  against the real-hardware golden objects: original %d/%d, '
              'port %d/%d' % (gold['orig'][1], gold['orig'][0],
                              gold['port'][1], gold['port'][0]))
    notsame.sort()
    if gold['orig'][0]:
        notsame.append('real-hardware golden objects: original %d/%d, port %d/%d'
                       % (gold['orig'][1], gold['orig'][0],
                          gold['port'][1], gold['port'][0]))
    if write:
        with open(write, 'w') as f:
            f.write(''.join(l + '\n' for l in notsame))
        print('wrote %d lines to %s' % (len(notsame), write))
    if check:
        want = [l.rstrip('\n') for l in open(check)]
        new = sorted(set(notsame) - set(want))
        gone = sorted(set(want) - set(notsame))
        for l in new:
            print('NEW      ' + l)
        for l in gone:
            print('GONE     ' + l)
        if new or gone or len(want) != len(notsame):
            print('ascmp: the runs differ from %s (%d new, %d gone); '
                  'if that is the change, take it over with --write and '
                  'document it' % (check, len(new), len(gone)))
            return 1
        print('ascmp: the runs agree with %s (%d lines)' % (check, len(want)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
