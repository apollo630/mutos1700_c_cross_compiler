#!/usr/bin/env python3
"""c0cmp.py - mutos_c0, the port of the real c0, against the original.

The original MUTOS 1700 c0 (mutos/mutos_bin/c0.golden.base64.txt) runs under
mutosemu.py; mutos_c0 (src/mutos_cc/c0port/, docs/C0_PORT_PLAN.md) runs
on the host. Both get the same .i file; their temp1 (.1), temp2 (.2),
messages and exit status must be the same, byte for byte.

A refusal of the port (exit status 2, "c0: ... refused") counts apart:
it must have written a prefix of what the original wrote - in each temp
file and in the messages before its own -, and where the original does
not finish (it loops for ever at some end-of-file cases), the port must
have refused (it loops for ever at some ends of file, and a CPU fault
stops it).

Usage:
    c0cmp.py goldens [-j N] [--port PATH] [--show N] [--keep DIR]
        every .i.golden of tests/mutos_cc (the 62-file corpus, fltprobe,
        11_kernel) and of tests/mutos_cpp/c
    c0cmp.py files FILE.i... [same options]

As a module: compare(path_to_i, port) -> (kind, detail) with kind one of
'same', 'refused', 'DIFFERENT', 'BAD-REFUSAL', 'orig-fails'.

Requires the Python module "unicorn" (pip install unicorn).
"""
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import mutosemu       # noqa: E402

ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
PORT = os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_c0')
MAX_INSNS = 60_000_000      # the largest kernel file takes about 10M


def run_orig(src, tmp):
    t1, t2 = os.path.join(tmp, 'o.1'), os.path.join(tmp, 'o.2')
    try:
        st, out, err = mutosemu.run_tool('c0', [src, t1, t2], cwd=tmp,
                                         max_insns=MAX_INSNS, eof_limit=1000)
    except RuntimeError as e:
        return None, b'', b'', str(e)
    read = lambda p: open(p, 'rb').read() if os.path.exists(p) else b''
    return st, read(t1), read(t2), err.decode('latin1')


def run_port(src, tmp, port):
    t1, t2 = os.path.join(tmp, 'p.1'), os.path.join(tmp, 'p.2')
    r = subprocess.run([port, src, t1, t2], capture_output=True, cwd=tmp,
                       timeout=120)
    read = lambda p: open(p, 'rb').read() if os.path.exists(p) else b''
    return r.returncode, read(t1), read(t2), r.stderr.decode('latin1')


def compare(src, port=PORT, keep=None):
    """Compare the port with the original on one .i file."""
    src = os.path.abspath(src)
    with tempfile.TemporaryDirectory() as tmp:
        ost, o1, o2, oerr = run_orig(src, tmp)
        pst, p1, p2, perr = run_port(src, tmp, port)
        refused = pst == 2 and 'refused' in perr
        if ost is None:
            kind = 'orig-fails' if refused else 'DIFFERENT'
            detail = 'the original does not finish (%s); the port %s' % (
                oerr, 'refused: ' + perr.rstrip('\n').split('\n')[-1]
                if refused else 'exit %d' % pst)
        elif refused:
            # split at newlines only (a message can hold a vertical tab)
            lines = perr.rstrip('\n').split('\n')
            before = '\n'.join(lines[:-1])
            if 'stdio buffer' in lines[-1]:
                # the original's own buffer was overwritten: only what it
                # had written out before - whole 512-byte blocks - is its
                p1 = p1[:len(p1) // 512 * 512]
                p2 = p2[:len(p2) // 512 * 512]
            ok = (o1.startswith(p1) and o2.startswith(p2)
                  and oerr.startswith(before))
            kind = 'refused' if ok else 'BAD-REFUSAL'
            detail = lines[-1]
        elif (pst, p1, p2, perr) == (ost, o1, o2, oerr):
            kind, detail = 'same', ''
        else:
            parts = []
            if pst != ost:
                parts.append('exit %d, original %d' % (pst, ost))
            for name, a, b in (('.1', p1, o1), ('.2', p2, o2)):
                if a != b:
                    n = next((i for i in range(min(len(a), len(b)))
                              if a[i] != b[i]), min(len(a), len(b)))
                    parts.append('%s differs at byte %d (%d vs %d bytes)'
                                 % (name, n, len(a), len(b)))
            if perr != oerr:
                parts.append('messages: %r, original %r'
                             % (perr[:300], oerr[:300]))
            kind, detail = 'DIFFERENT', '; '.join(parts)
        if keep and kind not in ('same', 'refused'):
            d = os.path.join(keep, os.path.basename(src))
            os.makedirs(d, exist_ok=True)
            shutil.copy(src, d)
            for f in os.listdir(tmp):
                shutil.copy(os.path.join(tmp, f), d)
            with open(os.path.join(d, 'messages'), 'w') as f:
                f.write('port:\n%s\noriginal:\n%s\n' % (perr, oerr))
    return kind, detail


def golden_inputs():
    tests = os.path.join(ROOT, 'tests')
    out = sorted(glob.glob(os.path.join(tests, 'mutos_cc', '**', '*.i.golden'),
                           recursive=True))
    out += sorted(glob.glob(os.path.join(tests, 'mutos_cpp', 'c', '*.i.golden')))
    return out


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
    inputs = golden_inputs() if args[0] == 'goldens' else args[1:]
    counts = {}
    bad = []
    with concurrent.futures.ThreadPoolExecutor(jobs) as ex:
        for src, (kind, detail) in zip(inputs, ex.map(
                lambda s: compare(s, port, keep), inputs)):
            counts[kind] = counts.get(kind, 0) + 1
            name = os.path.relpath(src, ROOT)
            if kind in ('DIFFERENT', 'BAD-REFUSAL'):
                bad.append('%s: %s: %s' % (name, kind, detail))
            elif kind != 'same' and show:
                print('%s: %s: %s' % (name, kind, detail))
    for b in bad[:show or 20]:
        print(b)
    print('c0 port against the original: %d inputs, %s'
          % (len(inputs), ', '.join('%d %s' % (n, k)
                                    for k, n in sorted(counts.items()))))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
