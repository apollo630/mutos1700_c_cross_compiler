#!/usr/bin/env python3
"""hwkit.py - the hardware check of step F3b: the corrected copy of
libc.a linked by the original ld on the MUTOS 1700 hardware.

docs/TOOLCHAIN_PLAN.md, F3b: "on the hardware, a program linked by hand
with ld against the copy before it replaces anything". This tool makes
the kit for that check and predicts, file for file, what the hardware
must make and give:

  - the trigger programs (triggers/*.c, one per correction of the copy);
  - fixlibc.a, the copy (src/libc_fixed/libc.a);
  - hwcheck.sh, a script for the hardware's sh (v7's Bourne shell): each
    program compiled by the hardware's "cc -c", linked by its ld twice -
    as cc links it ("ld -X /lib/crt0.o -o N.H [-i] N.o -lc", the
    original /lib/libc.a) and with the copy alone in the place of -lc
    (N.F) -, both run, their exit statuses written to the file "status",
    every N.o, N.H and N.F compared with cmp against expect/;
  - expect/: N.o, N.H, N.F and status, what the originals make and give;
  - README, how to run it.

The prediction runs the originals under tests/realbin/mutosemu.py: the
whole chain of the original cc (cctrace.py's --run: cpp, c0, c1, as) for
N.o, the original ld for N.H and N.F, then both images. It must agree
with the cross toolchain - mutos_cc -c gives the same N.o, mutos_ld the
same images, mutos_cc -F (the corrected mode, ld's rule LIBC-F) the same
N.F -, the original ld must link the same N.F with fixlibc.a's table of
contents out of date (as after a copy to the hardware: it warns), and
each image must end as its source's "expect: original X, corrected Y"
says (X for N.H, Y for N.F; SIGFPE is the status 136 the shell gives a
program it kills). A disagreement is reported and no kit is written.

Usage:
    hwkit.py [--out DIR] [--tar FILE]

--out: the kit's directory (default "hwkit"; it must not exist or be
empty). --tar: also a tar file of the kit - regular files only, under
the directory's name, no directory entries (v7's tar makes them). Exit
status 0 if the prediction holds, 1 otherwise. Requires the Python
module "unicorn" and a built tree ("make").
"""

import io
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ROOT, 'tests', 'realbin'))

try:
    import mutosemu  # noqa: E402
    import cctrace   # noqa: E402  (the original cc and its passes)
    import ldcmp     # noqa: E402  (the original ld, the port's run)
except ImportError:
    sys.exit('hwkit.py: needs the Python module "unicorn" (pip install unicorn)')

CC = os.path.join(ROOT, 'src', 'mutos_cc', 'mutos_cc')
LD = os.path.join(ROOT, 'src', 'mutos_ld', 'mutos_ld')
MUTOS_ROOT = os.path.join(ROOT, 'mutos_root')
COPY = os.path.join(ROOT, 'src', 'libc_fixed', 'libc.a')
TRIGGERS = os.path.join(HERE, 'triggers')
SIGFPE = 8
MAX_INSNS = 20000000
LD_OPTIONS = ('-i',)     # "cc: ..." options of a trigger cc hands to ld

SCRIPT = '''\
: hwcheck.sh - step F3b of the cross toolchain, the hardware check of
: the corrected copy of libc.a, fixlibc.a. For each trigger program N:
: the original cc compiles N.c, the original ld links N.o once as cc
: links it, with /lib/libc.a, into N.H and once with fixlibc.a alone
: into N.F, both run. Their exit statuses go into the file status, and
: N.o, N.H and N.F are compared with expect/. Nothing outside this
: directory is changed. Run it here with sh hwcheck.sh, see README.
rm -f status
bad=
for n in %(names)s
do
	case $n in
%(cases)s	*)	i= ;;
	esac
	rm -f $n.o $n.H $n.F core
	cc -c $n.c
	ld -X /lib/crt0.o -o $n.H $i $n.o -lc
	ld -X /lib/crt0.o -o $n.F $i $n.o fixlibc.a
	./$n.H
	h=$?
	./$n.F
	f=$?
	rm -f core
	echo $n $h $f >>status
	echo $n: with libc.a $h, with fixlibc.a $f
	for x in o H F
	do
		if cmp -s $n.$x expect/$n.$x
		then :
		else
			echo $n.$x: DIFFERENT from expect/$n.$x
			bad=yes
		fi
	done
done
if cmp -s status expect/status
then
	echo statuses: as expected
else
	echo statuses: NOT as expected, expected are
	cat expect/status
fi
case $bad in
yes)	echo files: DIFFERENT, see above ;;
*)	echo files: every .o, .H and .F as expected ;;
esac
'''

README = '''\
The hardware check of step F3b of the MUTOS 1700 cross toolchain
================================================================

Made by tests/libc_runtime/hwkit.py. The corrected copy of libc.a -
fixlibc.a here - linked by the ORIGINAL ld of MUTOS 1700 and run on the
hardware, before the copy replaces anything. Nothing outside this
directory is changed: /lib/libc.a stays as it is.

On MUTOS 1700, in this directory (with its subdirectory expect/):

    sh hwcheck.sh >log 2>&1
    cat log

For each program N (one per correction of the copy) the script runs

    cc -c N.c
    ld -X /lib/crt0.o -o N.H [-i] N.o -lc          (as cc links: /lib/libc.a)
    ld -X /lib/crt0.o -o N.F [-i] N.o fixlibc.a    (the copy alone)
    ./N.H; ./N.F

writes "N <status of N.H> <status of N.F>" into the file "status" and
compares N.o, N.H and N.F with cmp against expect/. Only %(i_names)s is
linked with -i.

Expected - the originals run under an 8086 emulator; the cross
toolchain makes the same files, and the V30 computes as the 8086 here:

%(table)s

136 is SIGFPE: the status the v7 shell gives a program it killed with
it (128 + 8); the shell prints "Floating exception" (and "- core
dumped"), and the script removes the core files. Should this shell give
another status for a killed program, the four SIGFPE lines show it -
the files are the check that decides.%(l03)s

What the log must end with:

    statuses: as expected
    files: every .o, .H and .F as expected

Harmless, and expected if the copy's file time is newer than its table
of contents (after most ways of copying it), once per N.F:

    ld:fixlibc.a(__.SYMDEF): out of date (warning)

The original ld then reads the archive member by member and links the
same N.F (checked under the emulator).

If anything differs: bring back the log, the file status and the N.o,
N.H and N.F the script left.
'''


def expectations(text):
    """(original, corrected) exit statuses of a trigger's "expect:" line."""
    m = re.search(r'expect: original (\w+), corrected (\w+)', text)
    if m is None:
        return None

    def status(word):
        return 128 + SIGFPE if word == 'SIGFPE' else int(word)
    return status(m.group(1)), status(m.group(2))


def triggers():
    """[(name without .c, source text, expected (H, F), ld options)]."""
    out = []
    for f in sorted(os.listdir(TRIGGERS)):
        if not f.endswith('.c'):
            continue
        with open(os.path.join(TRIGGERS, f), encoding='utf-8') as fh:
            text = fh.read()
        exp = expectations(text)
        m = re.search(r'^ \* cc: (.*)$', text, re.M)
        opts = m.group(1).split() if m else []
        if exp is None or any(o not in LD_OPTIONS for o in opts):
            sys.exit('hwkit.py: %s: no "expect:" line, or a "cc:" option other than %s'
                     % (f, ', '.join(LD_OPTIONS)))
        if len(f) > 14:
            sys.exit('hwkit.py: %s: the name is too long for MUTOS with .H/.F (14 '
                     'characters)' % f)
        out.append((f[:-2], text, exp, opts))
    return out


def run_image(image, cwd):
    """The way an image ends under the emulator: ('exit', status, stdout,
    stderr) or ('stopped', why)."""
    o, e = io.BytesIO(), io.BytesIO()
    try:
        emu = mutosemu.Emu(image, ['a.out'], cwd=cwd, stdin=io.BytesIO(b''),
                           stdout=o, stderr=e, eof_limit=1000)
        return ('exit', emu.run(MAX_INSNS), o.getvalue(), e.getvalue())
    except RuntimeError as x:
        return ('stopped', str(x))


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def table_time(archive):
    """The date of an archive's first member header (__.SYMDEF): the
    v7 long, high word first, at bytes 16..19."""
    hi = archive[16] | archive[17] << 8
    lo = archive[18] | archive[19] << 8
    return hi << 16 | lo


def predict(trigs, work):
    """Run the originals and the cross toolchain on every trigger in work.
    [(problem)] and {name: (N.o, N.H, N.F, (status H, status F))}."""
    bad, files = [], {}
    oroot = cctrace.oracle_root(os.path.join(work, 'oracle'))
    od, pd = os.path.join(work, 'orig'), os.path.join(work, 'port')
    os.makedirs(od)
    os.makedirs(pd)
    copy = read(COPY)
    t = table_time(copy)
    for d in (od, pd):
        with open(os.path.join(d, 'fixlibc.a'), 'wb') as f:
            f.write(copy)
        os.utime(os.path.join(d, 'fixlibc.a'), (t, t))
    env = dict(os.environ, MUTOS_ROOT=MUTOS_ROOT)
    for k in ('MUTOS_MODE', 'MUTOS_RULELOG'):
        env.pop(k, None)
    for name, text, exp, opts in trigs:
        src = name + '.c'
        for d in (od, pd):
            with open(os.path.join(d, src), 'w', encoding='utf-8') as f:
                f.write(text)
        # N.o: the original cc's chain, and mutos_cc
        tr = cctrace.run_cc(['-c', src], root=oroot, cwd=od, run=True)
        if tr.status != 0 or tr.output(1) or tr.output(2) or \
                not os.path.exists(os.path.join(od, name + '.o')):
            bad.append('%s: the original cc -c: exit %d %r' % (name, tr.status,
                                                               tr.output(1) + tr.output(2)))
            continue
        obj = read(os.path.join(od, name + '.o'))
        r = subprocess.run([CC, '-c', src], cwd=pd, env=env, capture_output=True)
        if r.returncode != 0 or r.stdout or r.stderr or \
                read(os.path.join(pd, name + '.o')) != obj:
            bad.append('%s: mutos_cc -c does not give the original\'s %s.o' % (name, name))
        # N.H and N.F: the original ld, and mutos_ld
        images = {}
        for tag, lib in (('H', '-lc'), ('F', 'fixlibc.a')):
            args = ['-X', '/lib/crt0.o', '-o', name + '.' + tag] + opts + [name + '.o', lib]
            st, out, err, why = ldcmp.run_orig(args, od, oroot)
            path = os.path.join(od, name + '.' + tag)
            if st != 0 or out or err or why or not os.path.exists(path):
                bad.append('%s: the original ld %s: %r %r %r %r' % (name, ' '.join(args),
                                                                   st, out, err, why))
                continue
            images[tag] = read(path)
            # a name on mutos_ld's command line is the host's (decision 4)
            pargs = [os.path.join(oroot, 'lib', 'crt0.o') if a == '/lib/crt0.o' else a
                     for a in args]
            pst, pout, perr = ldcmp.run_port(pargs, pd, oroot, LD)
            ppath = os.path.join(pd, name + '.' + tag)
            if pst != 0 or pout or perr or not os.path.exists(ppath) or \
                    read(ppath) != images[tag]:
                bad.append('%s: mutos_ld %s does not give the original\'s image'
                           % (name, ' '.join(args)))
        if len(images) != 2:
            continue
        # the copy's table of contents out of date: a warning, the same N.F
        os.utime(os.path.join(od, 'fixlibc.a'), (t + 3600, t + 3600))
        args = ['-X', '/lib/crt0.o', '-o', name + '.F2'] + opts + [name + '.o', 'fixlibc.a']
        st, out, err, why = ldcmp.run_orig(args, od, oroot)
        os.utime(os.path.join(od, 'fixlibc.a'), (t, t))
        if st != 0 or out != b'ld:fixlibc.a(__.SYMDEF): out of date (warning)\n' or err or \
                why or read(os.path.join(od, name + '.F2')) != images['F']:
            bad.append('%s: the original ld, table out of date: %r %r %r %r, %s'
                       % (name, st, out, err, why, 'the same image'
                          if os.path.exists(os.path.join(od, name + '.F2')) and
                          read(os.path.join(od, name + '.F2')) == images['F']
                          else 'another image'))
        # mutos_cc -F: the corrected mode makes the same N.F
        r = subprocess.run([CC, '-F'] + opts + ['-o', name + '.cF', src], cwd=pd, env=env,
                           capture_output=True)
        if r.returncode != 0 or r.stdout or r.stderr or \
                read(os.path.join(pd, name + '.cF')) != images['F']:
            bad.append('%s: mutos_cc -F does not make %s.F' % (name, name))
        # both run
        got = []
        for tag, want in (('H', exp[0]), ('F', exp[1])):
            how = run_image(images[tag], od)
            if how[0] != 'exit' or how[1] != want or how[2] or how[3]:
                bad.append('%s.%s: %r, not exit %d' % (name, tag, how, want))
            got.append(how[1] if how[0] == 'exit' else None)
        files[name] = (obj, images['H'], images['F'], tuple(got))
    return bad, files


def table(trigs, files):
    rows = ['    %-10s %-14s %-14s %s' % ('N', 'N.H (libc.a)', 'N.F (copy)', 'bug')]
    for name, text, exp, opts in trigs:
        h, f = files[name][3]
        bug = name.split('_')[0].upper().replace('L', 'L-', 1)
        rows.append('    %-10s %-14s %-14s %s' % (name, h, f, bug))
    return '\n'.join(rows)


def write_kit(out, trigs, files):
    os.makedirs(os.path.join(out, 'expect'))
    shutil.copy2(COPY, os.path.join(out, 'fixlibc.a'))
    status = ''
    for name, text, exp, opts in trigs:
        with open(os.path.join(out, name + '.c'), 'w', encoding='utf-8') as f:
            f.write(text)
        obj, h, fimg, (sh, sf) = files[name]
        for ext, data in (('o', obj), ('H', h), ('F', fimg)):
            with open(os.path.join(out, 'expect', name + '.' + ext), 'wb') as f:
                f.write(data)
        status += '%s %d %d\n' % (name, sh, sf)
    with open(os.path.join(out, 'expect', 'status'), 'w') as f:
        f.write(status)
    names = [t[0] for t in trigs]
    cases = ''.join('\t%s)\ti=%s ;;\n' % (t[0], ' '.join(t[3])) for t in trigs if t[3])
    with open(os.path.join(out, 'hwcheck.sh'), 'w') as f:
        f.write(SCRIPT % {'names': ' '.join(names), 'cases': cases})
    i_names = ', '.join(t[0] for t in trigs if '-i' in t[3]) or 'no program'
    l03 = ''
    if 'l03_ftoi' in names:
        l03 = ('\n\nl03_ftoi.H compares with the word at address 8000 of its data\n'
               'segment (L-03): SIGFPE unless that word happens to be 8000 hex.')
    with open(os.path.join(out, 'README'), 'w') as f:
        f.write(README % {'table': table(trigs, files), 'i_names': i_names, 'l03': l03})


def write_tar(out, tarname):
    base = os.path.basename(os.path.normpath(out))
    paths = []
    for d, _subs, fs in os.walk(out):
        for f in fs:
            paths.append(os.path.relpath(os.path.join(d, f), out))
    with tarfile.open(tarname, 'w', format=tarfile.USTAR_FORMAT) as tar:
        for rel in sorted(paths):
            info = tar.gettarinfo(os.path.join(out, rel), arcname=base + '/' + rel)
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mode = 0o644
            with open(os.path.join(out, rel), 'rb') as f:
                tar.addfile(info, f)


def main(argv):
    out, tarname = 'hwkit', None
    args = list(argv)
    while args:
        a = args.pop(0)
        if a == '--out' and args:
            out = args.pop(0)
        elif a == '--tar' and args:
            tarname = args.pop(0)
        else:
            print(__doc__)
            return 2
    for tool in (CC, LD):
        if not os.access(tool, os.X_OK):
            sys.exit('hwkit.py: %s is not built - run "make" first' % tool)
    for f in (COPY, os.path.join(MUTOS_ROOT, 'lib', 'fixed', 'libc.a')):
        if not os.path.isfile(f):
            sys.exit('hwkit.py: %s is missing - run "make" first' % f)
    if os.path.exists(out) and (not os.path.isdir(out) or os.listdir(out)):
        sys.exit('hwkit.py: %s exists and is not an empty directory' % out)
    trigs = triggers()
    with tempfile.TemporaryDirectory(prefix='hwkit.') as work:
        bad, files = predict(trigs, work)
    if bad:
        for b in bad:
            print('  WRONG ' + b)
        print('hwkit: the prediction does not hold - no kit written')
        return 1
    write_kit(out, trigs, files)
    if tarname:
        write_tar(out, tarname)
    print('hwkit: %d trigger programs: the original cc and ld under the emulator make '
          'what mutos_cc -c, mutos_ld and mutos_cc -F make; the copy\'s table out of '
          'date links the same; each image ends as its "expect:" line says'
          % len(trigs))
    print(table(trigs, files))
    print('hwkit: the kit is in %s%s' % (out, ' and %s' % tarname if tarname else ''))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
