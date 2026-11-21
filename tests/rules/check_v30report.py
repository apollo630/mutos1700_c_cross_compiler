#!/usr/bin/env python3
"""check_v30report.py - the kernel built with -1 through its makefile, and
scripts/v30report.py on the build's log (docs/KERNEL_BUILD_V30.md).

In a scratch kernel tree (tests/mutos_ed/check_editors.py's: the kernel's
makefile, its headers, mch.c and fakeawk) with the four real kernel
sources of tests/mutos_cpp/c/ in their directories - main.c in mdep/,
sys.c, tty.c and v30ide.c in io/ -, mutos_make builds their objects with
CFLAGS="-c -O -K1024 -1" given on the command line, as the guide does,
and:

  1. every cc command of the log has -1 and is followed by its report of
     as; without the macro (the makefile's CFLAGS) the objects are 8086
     code and the log has no report;
  2. v30report.py sums the reports: 4 assemblies of 4 cc commands, the
     total the sum of the reports;
  3. v30report.py --forms: each source compiled again with -S has as many
     80186/V30 instructions by kind as its report counted, each a probed
     form; exit status 0, and no file left in the tree;
  4. --forms fails as it must: a report changed in the log (the count
     differs), a source missing (no such source), a log without -1;
  5. what --forms takes for a probed form: the forms v30kit.py probes
     are, others - a byte shift, a shift by 2 or 16, a rotate, imul of
     another register - are not.

Usage: check_v30report.py [ROOT]   (default: $MUTOS_ROOT, else <tree>/mutos_root)
Exit status 1 on any failure. Python 3, standard library only.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(os.path.dirname(HERE))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(REPO, 'tests', 'mutos_ed'))
sys.path.insert(0, os.path.join(REPO, 'scripts'))
import check_editors  # noqa: E402
import v30report  # noqa: E402

V30REPORT = os.path.join(REPO, 'scripts', 'v30report.py')
SOURCES = (('main.c', 'mdep'), ('sys.c', 'io'), ('tty.c', 'io'), ('v30ide.c', 'io'))
TARGETS = ['../mdep/main.o', '../io/sys.o', '../io/tty.o', '../io/v30ide.o']
CFLAGS = 'CFLAGS=-c -O -K1024 -1'

FAILED = []
CHECKS = 0


def check(ok, what):
    global CHECKS
    CHECKS += 1
    if not ok:
        FAILED.append(what)
        print('FAILED: ' + what)
    return ok


def tree(tmp):
    conf = check_editors.tree(tmp)
    for name, d in SOURCES:
        shutil.copy(os.path.join(REPO, 'tests', 'mutos_cpp', 'c', name),
                    os.path.join(conf, '..', d, name))
    return conf


def files(conf):
    out = []
    for d, _subdirs, names in os.walk(os.path.join(conf, '..')):
        out += [os.path.normpath(os.path.join(d, n)) for n in names]
    return sorted(out)


def build(conf, root, *macros):
    env = dict(os.environ, MUTOS_ROOT=root)
    for k in ('MUTOS_MODE', 'MUTOS_RULELOG'):
        env.pop(k, None)
    r = subprocess.run([check_editors.MAKE] + list(macros) + TARGETS, cwd=conf, env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return r.returncode, r.stdout.decode('latin-1')


def report(log, conf, root, *opts):
    with tempfile.NamedTemporaryFile('w', suffix='.log', delete=False) as f:
        f.write(log)
    try:
        env = dict(os.environ, MUTOS_ROOT=root)
        env.pop('MUTOS_RULELOG', None)
        r = subprocess.run([sys.executable, V30REPORT, f.name] + list(opts), cwd=conf, env=env,
                           capture_output=True)
        return r.returncode, r.stdout.decode('latin-1')
    finally:
        os.unlink(f.name)


def check_build(tmp, root):
    conf = tree(os.path.join(tmp, 'plain'))
    st, log = build(conf, root)
    check(st == 0, 'make without the macro: exit %d %r' % (st, log[-300:]))
    check('as: -1:' not in log and ' -1 ' not in log,
          'make without the macro: -1 in the log')
    st, out = report(log, conf, root)
    check(st == 1 and 'a build without -1' in out, 'v30report on a log without -1: %d %r'
          % (st, out))
    st, out = report(log, conf, root, '--forms')
    check(st == 1 and 'no "+ cc" line with -1' in out,
          'v30report --forms on a log without -1: %d %r' % (st, out))

    conf = tree(os.path.join(tmp, 'v30'))
    before = files(conf)
    st, log = build(conf, root, CFLAGS)
    check(st == 0, 'make %s: exit %d %r' % (CFLAGS, st, log[-300:]))
    lines = log.splitlines()
    cc = [i for i, ln in enumerate(lines) if ln.startswith('+ cc ')]
    check(len(cc) == 4, 'make %s: %d cc commands, not 4' % (CFLAGS, len(cc)))
    reports = []
    for i in cc:
        check(' -c -O -K1024 -1 ' in lines[i], 'not the CFLAGS given: %r' % lines[i])
        nxt = lines[i + 1] if i + 1 < len(lines) else ''
        m = v30report.REPORT.match(nxt)
        if check(m is not None, 'no report of as after %r' % lines[i]):
            reports.append(v30report.kinds_of(m))
    total = {}
    for k in reports:
        for w, n in k.items():
            total[w] = total.get(w, 0) + n
    check(sum(total.values()) > 0, 'the four sources: no 80186/V30 instruction')
    for t in TARGETS:
        check(os.path.isfile(os.path.join(conf, t)), 'make: %s missing' % t)

    st, out = report(log, conf, root)
    check(st == 0 and 'v30report: 4 assemblies with -1 (of 4 cc commands in the log)' in out
          and 'v30report: %d 80186/V30 instructions: %s' % (sum(total.values()),
                                                           v30report.show(total)) in out,
          'v30report: %d %r' % (st, out))
    objs = files(conf)
    st, out = report(log, conf, root, '--forms')
    check(st == 0 and ('v30report: 4 of 4 sources with -1 compiled again with -S: %d '
                       '80186/V30 instructions (%s), each a probed form'
                       % (sum(total.values()), v30report.show(total))) in out,
          'v30report --forms: %d %r' % (st, out))
    check(files(conf) == objs, 'v30report --forms: the tree changed: %r'
          % sorted(set(files(conf)) ^ set(objs)))
    check(set(objs) - set(before) == {os.path.normpath(os.path.join(conf, t)) for t in TARGETS},
          'make: other files than the four objects: %r' % sorted(set(objs) - set(before)))

    # a report changed: the count differs
    first = cc[0] + 1
    m = v30report.REPORT.match(lines[first])
    kinds = dict(v30report.kinds_of(m))
    kinds['push'] = kinds.get('push', 0) + 1
    changed = list(lines)
    changed[first] = 'as: -1: %d 80186/V30 instructions: %s' % (
        sum(kinds.values()), v30report.show(kinds))
    st, out = report('\n'.join(changed) + '\n', conf, root, '--forms')
    check(st == 1 and 'the build\'s report' in out,
          'v30report --forms with a changed report: %d %r' % (st, out))
    # a source missing
    gone = os.path.join(conf, '..', 'io', 'tty.c')
    os.rename(gone, gone + '.away')
    st, out = report(log, conf, root, '--forms')
    os.rename(gone + '.away', gone)
    check(st == 1 and 'tty.c: no such source' in out,
          'v30report --forms without tty.c: %d %r' % (st, out))
    st, out = report(log, conf, root, '--forms', '--conf', conf)
    check(st == 0, 'v30report --forms --conf: %d %r' % (st, out))


def check_forms():
    probed = ['push\t*-128.', 'push\t*127.', 'push\t#300.', 'push\t#_buf+4', 'push\t#L12',
              'sal\tax,*3.', 'sar\tsi,*15.', 'shr\tdi,*9.', 'imul\tax,*-5.', 'imul\tax,#300.']
    other = ['salb\tal,*3.', 'sal\tax,*2.', 'sal\tax,*16.', 'rol\tax,*3.', 'sal\tbp,*3.',
             'sal\t_x,*3.', 'imul\tbx,*3.', 'imul\tax,bx,*3.', 'imulb\tal,*3.']
    for insn in probed:
        check(v30report.unprobed(insn + '\n') == [] and
              len(list(v30report.V30_LINE.finditer(insn + '\n'))) == 1,
              'not taken for a probed form: %r' % insn)
    for insn in other:
        check(v30report.unprobed(insn + '\n') == [insn], 'taken for a probed form: %r' % insn)
    for insn in ('push\tax', 'sal\tax,cl', 'sal\tax,*1', 'imul\tcx', 'push\t*4.(bp)'):
        check(list(v30report.V30_LINE.finditer(insn + '\n')) == [],
              'an 8086 instruction taken for an 80186/V30 one: %r' % insn)
    check(v30report.forms_of('_f:push\t*3.\nsal\tax,*4.\nimul\tax,#300.\nsar\tdx,*5.\n') ==
          {'push': 1, 'shift': 2, 'imul': 1}, 'forms_of: the kinds')
    # v30kit.py probes what v30report.py takes for probed
    src = open(os.path.join(HERE, 'v30kit.py'), encoding='utf-8').read()
    check(re.search(r'^PROBED = v30report\.PROBED$', src, re.M) is not None and
          re.search(r'^V30_LINE = v30report\.V30_LINE$', src, re.M) is not None,
          'v30kit.py: V30_LINE and PROBED not v30report.py\'s')


def main(argv):
    root = argv[0] if argv else os.environ.get('MUTOS_ROOT') or os.path.join(REPO, 'mutos_root')
    root = os.path.abspath(root)
    for t in ('cc', 'make'):
        if not os.path.exists(os.path.join(root, 'bin', t)):
            print('FAILED: %s/bin/%s missing (run make first)' % (root, t))
            return 1
    with tempfile.TemporaryDirectory(prefix='check_v30report') as tmp:
        check_build(tmp, root)
    check_forms()
    print('check_v30report: %d checks, %d failed' % (CHECKS, len(FAILED)))
    return 1 if FAILED else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
