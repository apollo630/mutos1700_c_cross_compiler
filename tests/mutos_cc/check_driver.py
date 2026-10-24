#!/usr/bin/env python3
"""check_driver.py - mutos_cc, the port of the original cc, with the
ported passes, against the real-hardware goldens (part of "make test").

mutos_cc runs the passes it finds through the MUTOS root - its compiled-in
default, the build tree's mutos_root/, unless MUTOS_ROOT is set: there
lib/cpp, lib/c0, lib/c1, bin/as and bin/ld are mutos_cpp, mutos_c0,
mutos_c1, mutos_as and mutos_ld. In a scratch copy of each tests/mutos_cc
directory, as the goldens were made on the hardware:

  * "cc -S x.c" for every source with an .s golden: the .s is the
    golden, byte for byte; the messages (stdout and stderr together) are
    c1_errors.txt's for that file, the exit status 1 where there are
    messages and 0 elsewhere. A file listed in invalid_goldens.txt is the
    real compiler's invalid output: mutos_c1 refuses it - the .s is the
    golden's first lines, the last message ends in "refused", exit
    status 1;
  * "cc -P x.c" for every source with an .i golden: the .i is the golden,
    exit status 0, nothing written to stdout or stderr;
  * "cc -o x x.c" for every program of the construct corpus (00_smoke ..
    10_integ): exit status 0, x a 0407 or 0411 a.out, x.o removed as the
    original cc removes a single source's object, nothing else left in
    the directory.

Afterwards the root's tmp/ must hold no temporary file of cc's
(/tmp/ctm...).

The comparison with the original cc itself (traces, fuzzing, the original
passes) needs the emulator: tests/realbin/cccmp.py and ccfuzz.py ("make
check-realbin").

Usage:
    check_driver.py [--cc PATH]

Python 3, standard library only. Exit status 1 on any difference.
"""
import concurrent.futures
import glob
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
CC = os.path.join(REPO, 'src', 'mutos_cc', 'mutos_cc')


def listed(name):
    """invalid_goldens.txt or c1_errors.txt: {category/name: [fields]}."""
    out = {}
    with open(os.path.join(HERE, name)) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            key, rest = (line.rstrip('\n').split(' ', 1) + [''])[:2]
            out.setdefault(key, []).append(rest)
    return out


def run(cc, args, cwd):
    r = subprocess.run([cc] + args, cwd=cwd, capture_output=True)
    lines = [ln for ln in (r.stdout + r.stderr).decode('latin1').split('\n')
             if ln]
    return r.returncode, lines


def read(path):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError:
        return None


def one(job):
    cc, d, base, invalid, messages, link = job
    label = '%s/%s' % (os.path.basename(d), base)
    bad = []
    counts = {}
    with tempfile.TemporaryDirectory(prefix='ccdrv') as tmp:
        for f in os.listdir(d):
            if f.endswith(('.c', '.h')):
                shutil.copy(os.path.join(d, f), tmp)
        src = base + '.c'
        gold = os.path.join(d, base + '.s.golden')
        if os.path.exists(gold):
            st, msgs = run(cc, ['-S', src], tmp)
            want = read(gold)
            got = read(os.path.join(tmp, base + '.s'))
            why = ''
            if invalid is not None:
                head = b''.join(want.splitlines(True)[:invalid])
                if got != head or st != 1 or not msgs \
                        or not msgs[-1].endswith('refused'):
                    why = 'not refused after %d lines (exit %d, %r)' % (
                        invalid, st, msgs[-1:])
            elif got != want:
                why = 'no .s' if got is None else '.s differs'
            elif msgs != messages:
                why = 'messages %r, not %r' % (msgs[:2], messages[:2])
            elif st != (1 if messages else 0):
                why = 'exit status %d' % st
            kind = 'cc -S (refused)' if invalid is not None else 'cc -S'
            counts[kind] = counts.get(kind, 0) + 1
            if why:
                bad.append('cc -S %s: %s' % (label, why))
            os.unlink(os.path.join(tmp, base + '.s')) if got is not None \
                else None
        gold = os.path.join(d, base + '.i.golden')
        if os.path.exists(gold):
            st, msgs = run(cc, ['-P', src], tmp)
            got = read(os.path.join(tmp, base + '.i'))
            why = ''
            if got != read(gold):
                why = 'no .i' if got is None else '.i differs'
            elif st != 0 or msgs:
                why = 'exit status %d, messages %r' % (st, msgs[:2])
            counts['cc -P'] = counts.get('cc -P', 0) + 1
            if why:
                bad.append('cc -P %s: %s' % (label, why))
            if got is not None:
                os.unlink(os.path.join(tmp, base + '.i'))
        if link:
            before = set(os.listdir(tmp))
            st, msgs = run(cc, ['-o', base, src], tmp)
            after = set(os.listdir(tmp))
            why = ''
            img = read(os.path.join(tmp, base))
            if st != 0 or msgs:
                why = 'exit status %d, messages %r' % (st, msgs[:2])
            elif img is None or len(img) < 16 or \
                    struct.unpack_from('<H', img)[0] not in (0o407, 0o411):
                why = 'no a.out %s' % base
            elif after - before != {base}:
                why = 'files left: %s' % sorted(after - before)
            counts['cc -o'] = counts.get('cc -o', 0) + 1
            if why:
                bad.append('cc -o %s: %s' % (label, why))
    return counts, bad


def main(argv):
    cc = CC
    if argv[:1] == ['--cc'] and len(argv) == 2:
        cc = os.path.abspath(argv[1])
    elif argv:
        sys.exit(__doc__.split('Usage:')[1].split('Python 3')[0])
    if not os.access(cc, os.X_OK):
        sys.exit('check_driver.py: %s not built' % cc)
    invalid = {k: int(v[0].split()[0]) for k, v in
               listed('invalid_goldens.txt').items()}
    errors = listed('c1_errors.txt')
    jobs = []
    for d in sorted(glob.glob(os.path.join(HERE, '*', ''))):
        d = d.rstrip(os.sep)
        cat = os.path.basename(d)
        if cat == 'fuzz':
            continue
        for f in sorted(os.listdir(d)):
            if not f.endswith('.c'):
                continue
            base = f[:-2]
            key = '%s/%s' % (cat, base)
            jobs.append((cc, d, base, invalid.get(key), errors.get(key, []),
                         cat[:2].isdigit() and cat != '11_kernel'))
    counts, bad = {}, []
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 1) as ex:
        for c, b in ex.map(one, jobs):
            for k, n in c.items():
                counts[k] = counts.get(k, 0) + n
            bad.extend(b)
    root = os.environ.get('MUTOS_ROOT') or os.path.join(REPO, 'mutos_root')
    left = sorted(f for f in os.listdir(os.path.join(root, 'tmp'))
                  if f.startswith('ctm'))
    if left:
        bad.append('%s/tmp: temporary files left: %s' % (root, left[:5]))
    nbad = {k: sum(1 for b in bad if b.startswith(k + ' ')) for k in
            ('cc -S', 'cc -P', 'cc -o')}
    print('mutos_cc -S x.c: %d/%d as the .s goldens (c1\'s messages and exit '
          'status included), %d refused as invalid_goldens.txt says'
          % (counts.get('cc -S', 0) - nbad['cc -S'], counts.get('cc -S', 0),
             counts.get('cc -S (refused)', 0)))
    print('mutos_cc -P x.c: %d/%d as the .i goldens'
          % (counts.get('cc -P', 0) - nbad['cc -P'], counts.get('cc -P', 0)))
    print('mutos_cc -o x x.c: %d/%d linked (the object removed, nothing else '
          'left)' % (counts.get('cc -o', 0) - nbad['cc -o'],
                     counts.get('cc -o', 0)))
    for b in bad:
        print('  ' + b)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
