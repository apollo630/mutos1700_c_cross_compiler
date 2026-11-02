#!/usr/bin/env python3
"""check_corpus.py - the makefiles of the construct corpus through
mutos_make, the port of the original make, and the ported tools (part of
"make test", no emulator).

Every tests/mutos_cc/*/Makefile.mutos (the thirteen, written for the real
make on the hardware) runs in a scratch copy of its directory - the sources
and the makefile only - with the tools of the MUTOS root:

    mutos_make -f Makefile.mutos -i CC=$MUTOS_ROOT/bin/cc \\
        CPP=$MUTOS_ROOT/lib/cpp C0=$MUTOS_ROOT/lib/c0

(the makefiles name /lib/cpp and /lib/c0 by absolute path, which no make can
redirect: they need the path as a macro, docs/TOOLCHAIN_PLAN.md, decision 10;
-i because the ten fltprobe sources of c1_errors.txt end with c1's messages
and exit status 1, as on the hardware). Then:

  * every .s, .i, .1 and .2 the makefile makes is the golden (.s.golden,
    .i.golden, .1.golden, .2.golden), byte for byte - but for the .s files
    of invalid_goldens.txt, which mutos_c1 refuses: there, the golden's
    first lines; that is the 62 + nine + fltprobe's sources of
    tests/mutos_cc/;
  * a second run does nothing (every target is up to date: no command is
    echoed);
  * "clean" leaves the scratch directory as it was before the first run.

Usage:
    check_corpus.py [--make PATH]

Python 3, standard library only. Exit status 1 on any difference.
"""
import concurrent.futures
import glob
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..', '..'))
MAKE = os.path.join(REPO, 'src', 'mutos_make', 'mutos_make')
CORPUS = os.path.join(REPO, 'tests', 'mutos_cc')


def invalid_goldens():
    """{category/name: lines} of tests/mutos_cc/invalid_goldens.txt."""
    out = {}
    with open(os.path.join(CORPUS, 'invalid_goldens.txt')) as f:
        for line in f:
            if line.startswith('#') or not line.strip():
                continue
            key, rest = (line.rstrip('\n').split(' ', 1) + [''])[:2]
            out[key] = int(rest.split()[0])
    return out


def read(path):
    try:
        with open(path, 'rb') as f:
            return f.read()
    except OSError:
        return None


def run(make, args, cwd, root):
    env = dict(os.environ, MUTOS_ROOT=root, TZ='UTC')
    r = subprocess.run([make] + args, cwd=cwd, env=env, capture_output=True,
                       timeout=900)
    return r.returncode, r.stdout, r.stderr


def one(job):
    make, d, root, invalid = job
    cat = os.path.basename(d)
    bad, counts = [], {'.s': 0, '.i': 0, '.1': 0, '.2': 0, 'refused': 0}
    macros = ['CC=%s/bin/cc' % root, 'CPP=%s/lib/cpp' % root,
              'C0=%s/lib/c0' % root]
    with tempfile.TemporaryDirectory(prefix='mkcorpus') as tmp:
        for f in os.listdir(d):
            if f.endswith(('.c', '.h')) or f == 'Makefile.mutos':
                shutil.copy(os.path.join(d, f), tmp)
        before = set(os.listdir(tmp))
        st, out, err = run(make, ['-f', 'Makefile.mutos', '-i'] + macros, tmp,
                           root)
        if err.rstrip().endswith(b'refused'):
            bad.append('%s: mutos_make refused: %s' % (cat, err[-200:]))
        made = sorted(set(os.listdir(tmp)) - before)
        if not made:
            bad.append('%s: nothing was made (exit %d, %r)' % (
                cat, st, (out + err)[:200]))
        for name in made:
            ext = os.path.splitext(name)[1]
            if ext not in counts:
                bad.append('%s: unexpected file %s' % (cat, name))
                continue
            gold = read(os.path.join(d, name + '.golden'))
            if gold is None:
                bad.append('%s: %s has no golden' % (cat, name))
                continue
            got = read(os.path.join(tmp, name))
            key = '%s/%s' % (cat, os.path.splitext(name)[0])
            if ext == '.s' and key in invalid:
                head = b''.join(gold.splitlines(True)[:invalid[key]])
                if got != head:
                    bad.append('%s/%s: not the first %d lines of the golden' %
                               (cat, name, invalid[key]))
                counts['refused'] += 1
            elif got != gold:
                bad.append('%s/%s: differs from its golden' % (cat, name))
            else:
                counts[ext] += 1
        # every source of the category has its .s and .1 (and so its .i, .2)
        want = sorted(f for f in os.listdir(tmp) if f.endswith('.c'))
        for f in want:
            for ext in ('.s', '.1', '.2', '.i'):
                if f[:-2] + ext not in made and \
                        os.path.exists(os.path.join(d, f[:-2] + ext + '.golden')):
                    bad.append('%s/%s: not made' % (cat, f[:-2] + ext))
        # a second run: everything is up to date, nothing is echoed
        st2, out2, err2 = run(make, ['-f', 'Makefile.mutos', '-i'] + macros, tmp,
                              root)
        if st2 != 0 or err2 or out2.strip():
            bad.append('%s: the second run did not rest: exit %d, %r' % (
                cat, st2, (out2 + err2)[:200]))
        # clean leaves what was there
        run(make, ['-f', 'Makefile.mutos', 'clean'], tmp, root)
        if set(os.listdir(tmp)) != before:
            bad.append('%s: clean left %s' % (cat, sorted(set(os.listdir(tmp)) ^ before)[:5]))
    return cat, counts, bad


def main(argv):
    make = MAKE
    if argv[:1] == ['--make'] and len(argv) == 2:
        make = os.path.abspath(argv[1])
    elif argv:
        sys.exit(__doc__.split('Usage:')[1].split('Python 3')[0])
    root = os.environ.get('MUTOS_ROOT') or os.path.join(REPO, 'mutos_root')
    if not os.access(make, os.X_OK):
        sys.exit('check_corpus.py: %s not built' % make)
    invalid = invalid_goldens()
    jobs = [(make, os.path.dirname(m), root, invalid) for m in
            sorted(glob.glob(os.path.join(CORPUS, '*', 'Makefile.mutos')))]
    total = {'.s': 0, '.i': 0, '.1': 0, '.2': 0, 'refused': 0}
    bad = []
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 1) as ex:
        for cat, counts, b in ex.map(one, jobs):
            for k, n in counts.items():
                total[k] += n
            bad.extend(b)
    left = sorted(f for f in os.listdir(os.path.join(root, 'tmp'))
                  if f.startswith('ctm'))
    if left:
        bad.append('%s/tmp: temporary files left: %s' % (root, left[:5]))
    print('mutos_make over the %d Makefile.mutos of tests/mutos_cc: %d .s, '
          '%d .i, %d .1 and %d .2 as the goldens, %d .s refused as '
          'invalid_goldens.txt says; second runs idle, clean clean'
          % (len(jobs), total['.s'], total['.i'], total['.1'], total['.2'],
             total['refused']))
    for b in bad:
        print('DIFFERENT:', b)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
