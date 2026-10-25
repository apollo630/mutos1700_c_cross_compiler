#!/usr/bin/env python3
"""cppcmp.py - mutos_cpp, the port of the real cpp, against the original.

The original MUTOS 1700 cpp (mutos/mutos_bin/cpp.golden.base64.txt) runs under
mutosemu.py; mutos_cpp (src/mutos_cpp/) runs on the host. Both get the
same command line in the same directory; their output (stdout, or the
output file named second), messages and exit status must be the same,
byte for byte.

A refusal of the port (exit status 2 and a last message ending in
"refused") counts apart: it must have written a prefix of what the
original wrote - the output and the messages before its own -, also
where the original does not finish (a CPU fault, an endless loop), and
there the port must have refused. Where both are stopped by the limits
(an endless expansion), their outputs must agree as far as the shorter
goes.

Usage:
    cppcmp.py goldens [-j N] [--port PATH] [--show N] [--keep DIR]
        every .i.golden of tests/mutos_cc and tests/mutos_cpp/c: its .c
        through both, with -P (the golden's own command line; the
        original's output is also checked against the golden), without
        -P (the # N "file" lines, as cc runs cpp), and into an output
        file named on the command line
    cppcmp.py files [OPTIONS...] FILE... [same options]
        the given files, each with the given cpp options (e.g. -P -DX)

As a module: compare(args, cwd, ...) -> (kind, detail) with kind one of
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
PORT = os.path.join(ROOT, 'src', 'mutos_cpp', 'mutos_cpp')
MAX_INSNS = 40_000_000      # the largest golden (mch.c) takes 2M to 4M
EOF_LIMIT = 1000
OUT_LIMIT = 1 << 20         # an endless expansion is stopped here
PORT_CPU = 5                # seconds
KERNEL_DEFINES = ['-DM7100', '-DASK', '-DIFSS', '-DV24', '-DV30IDE']


class _Limit(Exception):
    pass


class _Capped(io.BytesIO):
    """stdout or stderr of the emulated original, stopped at OUT_LIMIT."""
    def write(self, b):
        n = super().write(b)
        if self.tell() > OUT_LIMIT:
            raise _Limit()
        return n


def run_orig(args, cwd, root=None, stdin=b'', outname=None, max_insns=None):
    """(status or None, output, messages, why it did not finish); why
    starts with 'endless' for a run stopped by the output or instruction
    limit."""
    a = list(args) + ([outname] if outname else [])
    out, err = _Capped(), _Capped()
    emu = mutosemu.Emu(mutosemu.load_image('cpp'), ['cpp'] + a, cwd=cwd,
                       stdin=io.BytesIO(stdin), stdout=out, stderr=err,
                       root=root, eof_limit=EOF_LIMIT)
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
    o, e = out.getvalue(), err.getvalue()
    if outname:
        p = os.path.join(cwd, outname)
        o = open(p, 'rb').read()[:OUT_LIMIT] if os.path.exists(p) else b''
    return st, o, e, why


def _limits():
    resource.setrlimit(resource.RLIMIT_CPU, (PORT_CPU, PORT_CPU))
    resource.setrlimit(resource.RLIMIT_FSIZE, (OUT_LIMIT, OUT_LIMIT))


def run_port(args, cwd, port=PORT, root=None, stdin=b'', outname=None):
    """(status, output, messages); status None for a run the CPU-time or
    output-size limit stopped (an endless expansion)."""
    a = [port] + list(args) + ([outname] if outname else [])
    env = dict(os.environ)
    if root:
        env['MUTOS_ROOT'] = root
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe:
        r = subprocess.run(a, cwd=cwd, input=stdin, stdout=fo, stderr=fe,
                           env=env, preexec_fn=_limits)
        fo.seek(0)
        fe.seek(0)
        out, err = fo.read(), fe.read()
    st = r.returncode
    if st < 0:          # SIGXCPU, SIGXFSZ: stopped by the limits
        st = None
    if outname:
        p = os.path.join(cwd, outname)
        out = open(p, 'rb').read() if os.path.exists(p) else b''
    return st, out, err


def diff_at(a, b):
    n = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
             min(len(a), len(b)))
    return 'differs at byte %d (%d vs %d bytes)' % (n, len(a), len(b))


def compare(args, cwd, port=PORT, root=None, stdin=b'', outfile=False):
    """Compare the port with the original: the same arguments in the same
    directory. With outfile, the output goes to a file named last on the
    command line (o.i for the original, p.i for the port)."""
    ost, oout, oerr, why = run_orig(args, cwd, root, stdin,
                                    'o.i' if outfile else None)
    pst, pout, perr = run_port(args, cwd, port, root, stdin,
                               'p.i' if outfile else None)
    if ost is None and pst is not None and why.startswith('endless: program'):
        # the port finished: the original may just be slow (deep, long
        # expansions), so it gets more time
        ost, oout, oerr, why = run_orig(args, cwd, root, stdin,
                                        'o.i' if outfile else None,
                                        MAX_INSNS * 25)
    perr_s = perr.decode('latin1')
    lines = perr_s.rstrip('\n').split('\n')
    refused = (pst == 2 and lines[-1].startswith('cpp: ')
               and lines[-1].endswith('refused'))
    if ost is None and pst is None and why.startswith('endless'):
        # both stopped by the limits: the shorter output a prefix of the
        # longer, the same for the messages
        def agree(a, b):
            n = min(len(a), len(b))
            return a[:n] == b[:n]
        if agree(pout, oout) and agree(perr, oerr):
            return 'endless', why
        return 'DIFFERENT', 'both endless (%s), but the outputs differ: %s' % (
            why, diff_at(pout, oout))
    if pst is None:
        return 'DIFFERENT', 'the port was stopped by the limits; the original ' \
            '%s' % ('exit %d' % ost if ost is not None else why)
    # a refusal: what the port wrote before it - its output blocks, its
    # messages - must be a prefix of what the original wrote, whether the
    # original then finishes or not
    if refused:
        before = perr_s[:len(perr_s) - len(lines[-1]) - 1].encode('latin1')
        ok = oout.startswith(pout) and oerr.startswith(before)
    if ost is None:
        if refused and ok:
            return 'orig-fails', 'the original does not finish (%s); the port ' \
                'refused: %s' % (why, lines[-1])
        if refused:
            return 'BAD-REFUSAL', 'the original does not finish (%s); the ' \
                'port refused, its output not a prefix of the original\'s: ' \
                '%s' % (why, lines[-1])
        return 'DIFFERENT', 'the original does not finish (%s); the port ' \
            'exit %s' % (why, pst)
    if refused:
        return ('refused' if ok else 'BAD-REFUSAL'), lines[-1]
    if (pst, pout, perr) == (ost, oout, oerr):
        return 'same', ''
    parts = []
    if pst != ost:
        parts.append('exit %s, original %d' % (pst, ost))
    if pout != oout:
        parts.append('output ' + diff_at(pout, oout))
    if perr != oerr:
        parts.append('messages %r, original %r' % (perr[:300], oerr[:300]))
    return 'DIFFERENT', '; '.join(parts)


def golden_jobs():
    tests = os.path.join(ROOT, 'tests')
    jobs = []
    for g in sorted(glob.glob(os.path.join(tests, 'mutos_cc', '**', '*.i.golden'),
                              recursive=True)):
        jobs.append((g, []))
    for g in sorted(glob.glob(os.path.join(tests, 'mutos_cpp', 'c', '*.i.golden'))):
        jobs.append((g, KERNEL_DEFINES))
    return jobs


def _golden_one(job):
    """One golden: -P (also against the golden), no -P, an output file."""
    golden, defines, port, keep = job
    base = golden[:-len('.i.golden')]
    src = os.path.basename(base) + '.c'
    d = os.path.dirname(golden)
    out = []
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        # a copy of the directory, so that the output files go there; the
        # headers the corpus includes by relative names come along
        work = os.path.join(tmp, 'd')
        shutil.copytree(d, work, ignore=shutil.ignore_patterns(
            '*.golden', '*.base64.txt', '*.s', '*.o', '*.log'))
        parent = os.path.dirname(d)
        for sub in ('h',):      # tests/mutos_cpp's ../h
            if os.path.isdir(os.path.join(parent, sub)) and \
                    not os.path.exists(os.path.join(tmp, sub)):
                shutil.copytree(os.path.join(parent, sub), os.path.join(tmp, sub))
        for name, args, outfile in (('-P', ['-P'] + defines + [src], False),
                                    ('', defines + [src], False),
                                    ('-P file', ['-P'] + defines + [src], True)):
            kind, detail = compare(args, work, port, outfile=outfile)
            if name == '-P' and kind == 'same':
                st, o, _, _ = run_orig(args, work)
                if o != open(golden, 'rb').read():
                    kind, detail = 'DIFFERENT', 'both differ from the golden'
            out.append((name, kind, detail))
            if keep and kind not in ('same', 'refused'):
                k = os.path.join(keep, os.path.relpath(base, ROOT).replace('/', '_')
                                 + (name.replace(' ', '_') or '_noP'))
                shutil.copytree(work, k, dirs_exist_ok=True)
    return golden, out


def _files_one(job):
    f, opts, port = job
    cwd = os.path.dirname(os.path.abspath(f)) or '.'
    return f, [('', ) + compare(opts + [os.path.basename(f)], cwd, port)]


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
    if not args or args[0] not in ('goldens', 'files'):
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    if args[0] == 'goldens':
        work = [(g, d, port, keep) for g, d in golden_jobs()]
        fn = _golden_one
    else:
        opts = [a for a in args[1:] if a.startswith('-')]
        files = [a for a in args[1:] if not a.startswith('-')]
        work = [(f, opts, port) for f in files]
        fn = _files_one
    counts = {}
    bad = []
    n = 0
    with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
        for name, results in ex.map(fn, work):
            for mode, kind, detail in results:
                n += 1
                counts[kind] = counts.get(kind, 0) + 1
                label = '%s%s' % (os.path.relpath(name, ROOT),
                                  ' [%s]' % mode if mode else ' [no -P]'
                                  if args[0] == 'goldens' else '')
                if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                    bad.append('%s: %s: %s' % (label, kind, detail))
                elif kind != 'same' and show:
                    print('%s: %s: %s' % (label, kind, detail))
    for b in bad[:show or 20]:
        print(b)
    print('cpp port against the original: %d runs, %s'
          % (n, ', '.join('%d %s' % (c, k) for k, c in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
