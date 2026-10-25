#!/usr/bin/env python3
"""c2cmp.py - mutos_c2, the port of the real c2, against the original.

The original MUTOS 1700 c2 (mutos/mutos_bin/c2.golden.base64.txt) runs under
mutosemu.py; mutos_c2 (src/mutos_cc/c2port/) runs on the host. Both get
the same command line in two copies of the same directory - the input,
and an output file named second (as cc runs it: "c2 tmp5 tmp3"), or
stdin and stdout -; their output, messages and exit status must be the
same, byte for byte.

A refusal of the port (exit status 2 and a last message ending in
"refused") counts apart: it must have written a prefix of what the
original wrote - the output blocks and the messages before its own -,
also where the original does not finish (a CPU fault, an endless loop),
and there the port must have refused. Where both are stopped by the
limits (an endless loop), their outputs must agree as far as the
shorter goes.

Usage:
    c2cmp.py goldens [-j N] [--port PATH] [--show N] [--keep DIR]
        every .s golden of tests/mutos_cc (the real c1's output: the
        construct corpus, 11_kernel, fltprobe) through both - "c2 in out",
        "c2 - in out" (the statistics on stderr) and "c2 < in > out" -;
        the nine 11_kernel files' outputs also against
        tests/mutos_as/kernel_opt/, the real hardware's optimized kernel
        sources
    c2cmp.py cases [same options]
        hand-written cases: the command lines ("-", "+", none, stdin and
        stdout, a missing input, an output that cannot be created), the
        input's form (empty, no last linefeed, empty lines, lines at and
        past the line buffer's end, odd bytes, long tokens and operands
        over the fixed buffers, many chunks, a large function, the heap's
        end), label numbers, and the original's bugs C2-01 ..
    c2cmp.py files [-] FILE... [same options]
        the given files, each as "c2 [-] FILE out"

As a module: compare(src, cwd, ...) -> (kind, detail) with kind one of
'same', 'refused', 'orig-fails' (the original does not finish, the port
refused), 'endless' (both stopped by the limits), 'DIFFERENT',
'BAD-REFUSAL'.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import glob
import io
import os
import resource
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_c2')
MAX_INSNS = 2_000_000_000   # 09_amx (3,791 lines) takes some 20 s
CASE_INSNS = 300_000_000    # the cases: an endless original stops here
EOF_LIMIT = 1000
OUT_LIMIT = 1 << 20
PORT_CPU = 60               # seconds
KERNEL_OPT = {'01_delay': 'delay', '02_prim': 'prim', '03_mem': 'mem',
              '04_pipe': 'pipe', '05_nami': 'nami', '06_fio': 'fio',
              '07_v24': 'v24', '08_tty': 'tty', '09_amx': 'amx'}


class _Limit(Exception):
    pass


class _Capped(io.BytesIO):
    """stdout or stderr of the emulated original, stopped at OUT_LIMIT."""
    def write(self, b):
        n = super().write(b)
        if self.tell() > OUT_LIMIT:
            raise _Limit()
        return n


def run_orig(args, cwd, stdin=b'', outname=None, max_insns=None):
    """(status or None, (stdout, output file or None), messages, why it did
    not finish); why starts with 'endless' for a run stopped by a limit."""
    out, err = _Capped(), _Capped()
    emu = mutosemu.Emu(mutosemu.load_image('c2'), ['c2'] + list(args),
                       cwd=cwd, stdin=io.BytesIO(stdin), stdout=out,
                       stderr=err, eof_limit=EOF_LIMIT)
    why = ''
    st = None
    try:
        st = emu.run(max_insns or MAX_INSNS)
    except _Limit:
        why = 'endless: output past %d bytes' % OUT_LIMIT
    except RuntimeError as e:
        why = str(e)
        if 'stopped without exit' in why or 'past the end of a file' in why:
            why = 'endless: ' + why
    return st, (out.getvalue(), _outfile(cwd, outname)), err.getvalue(), why


def _outfile(cwd, outname):
    if not outname:
        return None
    p = os.path.join(cwd, outname)
    return open(p, 'rb').read()[:OUT_LIMIT] if os.path.isfile(p) else None


def run_port(args, cwd, port=PORT, stdin=b'', outname=None, cpu=None):
    """(status, (stdout, output file or None), messages); status None for a
    run a limit stopped."""
    def limits():
        c = cpu or PORT_CPU
        resource.setrlimit(resource.RLIMIT_CPU, (c, c))
        resource.setrlimit(resource.RLIMIT_FSIZE, (OUT_LIMIT, OUT_LIMIT))
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe:
        r = subprocess.run([port] + list(args), cwd=cwd, input=stdin,
                           stdout=fo, stderr=fe, preexec_fn=limits)
        fo.seek(0)
        fe.seek(0)
        out, err = fo.read(), fe.read()
    st = r.returncode
    if st < 0:          # SIGXCPU, SIGXFSZ: stopped by the limits
        st = None
    return st, (out, _outfile(cwd, outname)), err


def diff_at(a, b):
    a, b = a or b'', b or b''
    n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
             min(len(a), len(b)))
    return 'differs at byte %d (%d vs %d bytes)' % (n, len(a), len(b))


def _prefix(a, b):
    """Is output a (stdout, file) a prefix of b, part by part?"""
    return all((y or b'').startswith(x or b'') for x, y in zip(a, b))


def compare_run(files, args, stdin=b'', outname=None, port=PORT,
                max_insns=None, cpu=None):
    """The port against the original: the same command line in two copies
    of one directory holding files (name -> bytes, None for a
    directory); their stdout, the
    output file named outname, the messages and the exit status compared.
    Returns (kind, detail, the original's output: (stdout, file))."""
    with tempfile.TemporaryDirectory() as tmp:
        od, pd = os.path.join(tmp, 'o'), os.path.join(tmp, 'p')
        for d in (od, pd):
            os.mkdir(d)
            for name, data in files.items():
                if data is None:            # a directory
                    os.mkdir(os.path.join(d, name))
                else:
                    open(os.path.join(d, name), 'wb').write(data)
        ost, oout, oerr, why = run_orig(args, od, stdin, outname, max_insns)
        pst, pout, perr = run_port(args, pd, port, stdin, outname, cpu)
    perr_s = perr.decode('latin1')
    lines = perr_s.rstrip('\n').split('\n')
    refused = (pst == 2 and lines[-1].startswith('c2: ')
               and lines[-1].endswith('refused'))
    if ost is None and pst is None and why.startswith('endless'):
        def agree(a, b):
            a, b = a or b'', b or b''
            n = min(len(a), len(b))
            return a[:n] == b[:n]
        if all(agree(x, y) for x, y in zip(pout, oout)) and agree(perr, oerr):
            return 'endless', why, oout
        return 'DIFFERENT', 'both endless (%s), but the outputs differ: %s' % (
            why, diff_at(pout[1] or pout[0], oout[1] or oout[0])), oout
    if pst is None:
        return 'DIFFERENT', 'the port was stopped by the limits; the original ' \
            '%s' % ('exit %d' % ost if ost is not None else why), oout
    if refused:
        before = perr_s[:len(perr_s) - len(lines[-1]) - 1].encode('latin1')
        ok = _prefix(pout, oout) and oerr.startswith(before)
    if ost is None:
        if refused and ok:
            return 'orig-fails', 'the original does not finish (%s); the port ' \
                'refused: %s' % (why, lines[-1]), oout
        if refused:
            return 'BAD-REFUSAL', 'the original does not finish (%s); the ' \
                'port refused, its output not a prefix of the original\'s: ' \
                '%s' % (why, lines[-1]), oout
        return 'DIFFERENT', 'the original does not finish (%s); the port ' \
            'exit %s' % (why, pst), oout
    if refused:
        return ('refused' if ok else 'BAD-REFUSAL'), lines[-1], oout
    if (pst, pout, perr) == (ost, oout, oerr):
        return 'same', '', oout
    parts = []
    if pst != ost:
        parts.append('exit %s, original %d' % (pst, ost))
    for what, x, y in (('stdout', pout[0], oout[0]),
                       ('the output file', pout[1], oout[1])):
        if x != y:
            parts.append('%s %s' % (what, 'missing in one' if (x is None) !=
                                    (y is None) else diff_at(x, y)))
    if perr != oerr:
        parts.append('messages %r, original %r' % (perr[:300], oerr[:300]))
    return 'DIFFERENT', '; '.join(parts), oout


def compare(src, cwd, port=PORT, mode='file', flags=(), max_insns=None,
            cpu=None):
    """Compare the port with the original on the file src (in cwd). mode
    'file': "c2 [flags] src out" in two copies of cwd; 'stdio': "c2
    [flags] < src > out". Returns (kind, detail, original's output - the
    output file or stdout)."""
    data = open(os.path.join(cwd, src), 'rb').read()
    name = os.path.basename(src)
    if mode == 'file':
        kind, detail, o = compare_run({name: data}, list(flags) + [name, 'out.s'],
                                      outname='out.s', port=port,
                                      max_insns=max_insns, cpu=cpu)
        return kind, detail, o[1]
    kind, detail, o = compare_run({}, list(flags), stdin=data, port=port,
                                  max_insns=max_insns, cpu=cpu)
    return kind, detail, o[0]


# ---------------------------------------------------------------------
# Hand-written cases: the command lines, the messages, the limits, the
# original's bugs (docs/MUTOS_ORIGINAL_BUGS.md, C2-01 ..)
# ---------------------------------------------------------------------

FUNC = (b'.globl\t_f\n.text\n_f:\npush\tbp\nmov\tbp,sp\npush\tdi\npush\tsi\n'
        b'|NREG\t3\njmp\tL1\nL2:mov\tax,*4.(bp)\nmov\tdx,ax\npush\tdx\n'
        b'call\t_g\nmov\tsp,bp\nor\tax,ax\nbeq\tL3\nmov\tax,*0.\nL3:|RTYP\t0\n'
        b'jmp\tcret\nL1:sub\tsp,*2.\njmp\tL2\n.data\n')


def _line(n, nl=True):
    return b'mov\tax,' + b'x' * (n - 7) + (b'\n' if nl else b'')


def _bigfunc(n):
    out = [b'.globl\t_big\n.text\n_big:\npush\tbp\nmov\tbp,sp\n']
    for i in range(n):
        out.append(b'mov\tax,*%d.(bp)\nadd\tax,dx\nbeq\tL%d\nL%d:inc\tsi\n'
                   % (i % 100, i + 10, i + 10))
    out.append(b'jmp\tcret\n')
    return b''.join(out)


def cases():
    """(name, files, args, stdin, outname)"""
    c = []

    def add(name, data, args=('in.s', 'out.s'), stdin=b'', outname='out.s',
            extra=None):
        files = {'in.s': data} if data is not None else {}
        files.update(extra or {})
        c.append((name, files, list(args), stdin, outname))

    # command lines
    add('in out', FUNC)
    add('- in out', FUNC, ('-', 'in.s', 'out.s'))
    add('-x in out', FUNC, ('-x', 'in.s', 'out.s'))
    add('+ in out', FUNC, ('+', 'in.s', 'out.s'))
    add('+ - in out', FUNC, ('+', '-', 'in.s', 'out.s'))
    add('- + in out: "+" the input', FUNC, ('-', '+', 'in.s'), outname='in.s')
    add('in', FUNC, ('in.s',), outname=None)
    add('none: stdin, stdout', None, (), stdin=FUNC, outname=None)
    add('- alone', None, ('-',), stdin=FUNC, outname=None)
    add('in out extra', FUNC, ('in.s', 'out.s', 'x'))
    add('input missing', None, ('nosuch.s', 'out.s'))
    add('- input missing', None, ('-', 'nosuch.s', 'out.s'))
    add('output not creatable', FUNC, ('in.s', 'nodir/out.s'),
        outname='nodir/out.s')
    add('input a directory', None, ('d', 'out.s'), extra={'d': None})
    # the input's form
    add('empty input', b'')
    add('no last linefeed', FUNC[:-1])
    add('only a linefeed', b'\n')
    add('empty lines', b'\n\nmov\tax,bx\n\n\n\nL1:\n\n\njmp\tL1\n\n')
    for n in (509, 510, 511, 512):
        add('a line of %d characters' % n, _line(n))
        add('a last line of %d characters, no linefeed' % n, _line(n, False))
    add('a label of 510 characters', b'x' * 510 + b':\n')
    add('a label of 511 characters', b'x' * 511 + b':\n')
    add('tabs and blanks first', b' \t mov\tax,bx\n\t\tjmp\tL1\n  L1:\n')
    add('a colon in an operand', b'mov\tax,a:b\n.byte\t1,2\n')
    add('odd bytes', b'\xffmov\tax,bx\n\x80\x81:\nmov\t\xe4x,bx\n')
    for n in (30, 31, 32, 33, 40):
        add('a first token of %d characters' % n,
            FUNC + b'y' * n + b'\tax,bx\nmov\tax,bx\n')
    for n in (31, 32, 33, 49, 50, 51, 52, 60, 100):
        add('an operand of %d characters' % n,
            FUNC + b'mov\tax,_' + b'z' * (n - 1) + b'\nmov\tbx,ax\n'
            + b'mov\tcx,_' + b'z' * (n - 1) + b'\nadd\tdx,cx\n')
    add('many chunks', FUNC * 40)
    add('a large function', _bigfunc(600))
    add('a large function: "-" (the core)', _bigfunc(600), ('-', 'in.s', 'out.s'))
    add('a function past the heap', _bigfunc(2500))
    # label numbers
    add('L0, L65535, L40000', b'beq\tL0\nL0:\njmp\tL65535\nL65535:\n'
        b'jmp\tL40000\nL40000:inc\tax\nbr\tL40000\n')
    add('Lx labels', b'Lx1:\njmp\tLx1\nL7x:\njmp\tL7x\nL5\nL\n')
    add('a label used twice', b'L1:\ninc\tax\nL1:\ninc\tbx\nbeq\tL1\n')
    # the original's bugs
    add('C2-01: a destination equal to a register\'s constant',
        b'mov\tax,*5.\nmov\t*5.,bx\n')
    add('C2-01: an instruction without operands', b'inc\tax\nmov\n')
    add('C2-02: mov/push not counted', FUNC, ('-', 'in.s', 'out.s'))
    add('C2-04: test without a comma', b'mov\tax,bx\ntest\tax\nbeq\tL1\n'
        b'inc\tax\nL1:\n')
    add('C2-05: lea into memory', b'mov\tax,*1.\njmp\tL1\nL3:inc\tbx\n'
        b'L1:cmp\tax,*1.\nbeq\tL9\nlea\t_x,(si)\nL9:inc\tcx\njmp\tL3\n',
        ('-', 'in.s', 'out.s'))
    add('C2-05: lea into a register', b'mov\tax,*1.\njmp\tL1\nL3:inc\tbx\n'
        b'L1:cmp\tax,*1.\nbeq\tL9\nlea\tbx,(si)\nL9:inc\tcx\njmp\tL3\n',
        ('-', 'in.s', 'out.s'))
    add('C2-03: a source operand of 32 characters (C2-01)',
        b'mov\tax,_' + b'z' * 31 + b'\nmov\tbx,ax\n')
    add('C2-06: a jump to an undefined label', b'br\tL5\ninc\tax\nL5:br\tL99\n'
        b'.globl\t_g\nbr\tL5\ninc\tax\nL5:\nbr\tL8\nL8:jmp\tL9\n')
    add('C2-06: an empty line between', b'br\tL5\nL5:\nbr\tL99\n'
        b'beq\tL7\ninc\tax\nL7:\n')
    add('C2-06: a branch to code', b'beq\t_foo\njmp\tL1\ninc\tax\nL1:\n')
    add('C2-07: an empty line taken for the head', b'L13:\njmp\tL13\n')
    add('C2-07: endless', b'L10:\nL13:\njmp\tL13\n')
    add('|NREG without an operand', b'|NREG\nmov\tax,bx\npush\tax\n')
    add('|NREG -1, 0', b'|NREG\t-1\n|NREG\t0\nmov\tax,bx\npush\tax\n'
        b'call\t_g\n')
    return c


def _case_one(job):
    (name, files, args, stdin, outname), port = job
    kind, detail, _ = compare_run(files, args, stdin, outname, port,
                                  max_insns=CASE_INSNS)
    return name, [('', kind, detail)]


def golden_jobs():
    return sorted(glob.glob(os.path.join(ROOT, 'tests', 'mutos_cc', '**',
                                         '*.s.golden'), recursive=True))


def _golden_one(job):
    golden, port, keep = job
    out = []
    d, name = os.path.split(golden)
    for label, mode, flags in (('c2 in out', 'file', ()),
                               ('c2 - in out', 'file', ('-',)),
                               ('c2 <in >out', 'stdio', ())):
        kind, detail, oout = compare(name, d, port, mode, flags)
        stem = name[:-len('.s.golden')]
        if mode == 'file' and not flags and kind == 'same' \
                and stem in KERNEL_OPT:
            ref = os.path.join(ROOT, 'tests', 'mutos_as', 'kernel_opt',
                               KERNEL_OPT[stem] + '.s')
            if oout != open(ref, 'rb').read():
                kind, detail = 'DIFFERENT', 'both differ from kernel_opt/'
            else:
                label += ' = kernel_opt'
        out.append((label, kind, detail))
        if keep and kind not in ('same', 'refused'):
            os.makedirs(keep, exist_ok=True)
            shutil.copy(golden, keep)
    return golden, out


def _files_one(job):
    f, flags, port = job
    cwd = os.path.dirname(os.path.abspath(f)) or '.'
    kind, detail, _ = compare(os.path.basename(f), cwd, port, 'file', flags)
    return f, [('', kind, detail)]


def main(argv):
    jobs, port, show, keep = os.cpu_count() or 1, PORT, 0, None
    args = []
    i = 0
    while i < len(argv):
        a = argv[i]
        if a == '-j':
            jobs = int(argv[i + 1]); i += 2
        elif a == '--port':
            port = os.path.abspath(argv[i + 1]); i += 2
        elif a == '--show':
            show = int(argv[i + 1]); i += 2
        elif a == '--keep':
            keep = os.path.abspath(argv[i + 1]); i += 2
        else:
            args.append(a); i += 1
    if not args or args[0] not in ('goldens', 'cases', 'files'):
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    if args[0] == 'goldens':
        work = [(g, port, keep) for g in golden_jobs()]
        fn = _golden_one
    elif args[0] == 'cases':
        work = [(c, port) for c in cases()]
        fn = _case_one
    else:
        flags = tuple(a for a in args[1:] if a.startswith('-'))
        work = [(f, flags, port) for f in args[1:] if not f.startswith('-')]
        fn = _files_one
    counts = {}
    bad = []
    n = 0
    kopt = 0
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for name, results in ex.map(fn, work):
            for mode, kind, detail in results:
                n += 1
                counts[kind] = counts.get(kind, 0) + 1
                kopt += mode.endswith('= kernel_opt')
                label = '%s%s' % (name if args[0] == 'cases' else
                                  os.path.relpath(name, ROOT),
                                  ' [%s]' % mode if mode else '')
                if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                    bad.append('%s: %s: %s' % (label, kind, detail))
                elif kind != 'same' and show:
                    print('%s: %s: %s' % (label, kind, detail))
    for b in bad[:show or 20]:
        print(b)
    print('c2 port against the original: %d runs, %s'
          % (n, ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    if args[0] == 'goldens':
        print('the original and the port give kernel_opt/: %d/%d'
              % (kopt, len(KERNEL_OPT)))
    return 1 if bad or (args[0] == 'goldens' and kopt != len(KERNEL_OPT)) \
        else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
