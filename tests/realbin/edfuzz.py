#!/usr/bin/env python3
"""edfuzz.py - differential fuzzing of mutos_ed and mutos_em against the
emulated original ed and em (step 9).

Every case is a random file and a random script of editor commands -
addresses of every form (numbers that wrap, '.', '$', +/- offsets, marks,
searches forwards and backwards, ',' and ';' pairs), the commands of ed (and
em's own: '"', '&', '%', "s/../../n", 'b', 'h', '<', '>'), random regular
expressions (classes, stars, \\( \\), \\1, anchors, empty ones) and
replacements (&, \\1, an escaped newline), text with tabs, control
characters and (for em) bytes of 0200 and above, file names to write, read
and edit - run by edcmp.compare(): the standard output and error, the exit
status and the trees of the original under the emulator and of the port must
be the same, or the port refuses with a prefix of the original's output.
Most scripts come through a pipe, which the editors' seek to the end of the
input after an error cannot skip, so that a script goes on after an error.
'!', '|', 'x', 'o' and 'z' are left out (the emulator has no fork(), pipe()
or terminal; 'o' and the encryption are refused by the ports).

Usage:
    edfuzz.py [-n N] [-s SEED] [-j JOBS] [--tool ed|em] [--keep DIR] [--show N]

Exit status 1 if any case differs. Requires the Python module "unicorn".
"""
import concurrent.futures
import os
import random
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import edcmp          # noqa: E402

LETTERS = 'abcxyz'
SPECIAL = ' \t.*[]^$/\\-'


def rword(r, eight):
    n = r.randrange(0, 12)
    s = []
    for _ in range(n):
        k = r.random()
        if k < 0.7:
            s.append(r.choice(LETTERS))
        elif k < 0.9:
            s.append(r.choice(SPECIAL))
        elif k < 0.95:
            s.append(chr(r.choice((1, 8, 0x1b, 0x7f))))
        elif eight:
            s.append(chr(r.randrange(0x80, 0xff)))
        else:
            s.append(r.choice(LETTERS))
    return ''.join(s)


def rfile(r, eight):
    n = r.choice((0, 1, 2, 5, 10, 20, 30))
    lines = [rword(r, eight) for _ in range(n)]
    text = ''.join(x + '\n' for x in lines)
    if text and r.random() < 0.1:
        text = text[:-1]            # no newline at the end
    return text.encode('latin1')


def rre(r, delim):
    """A random regular expression (without the delimiters)."""
    if r.random() < 0.1:
        return ''
    out = []
    if r.random() < 0.2:
        out.append('^')
    nb = 0
    for _ in range(r.randrange(1, 6)):
        k = r.random()
        if k < 0.45:
            out.append(r.choice(LETTERS))
        elif k < 0.55:
            out.append('.')
        elif k < 0.65:
            body = ''.join(r.choice(LETTERS + '-') for _ in range(r.randrange(1, 4)))
            out.append('[' + ('^' if r.random() < 0.3 else '') + body + ']')
        elif k < 0.72 and nb < 3:
            out.append('\\(' + r.choice(LETTERS) + ('*' if r.random() < 0.3 else '') + '\\)')
            nb += 1
        elif k < 0.76 and nb:
            out.append('\\' + str(r.randrange(1, nb + 1)))
        elif k < 0.82:
            out.append('\\' + r.choice('.*[$/'))
        elif k < 0.86:
            out.append(' ')
        else:
            out.append(r.choice(LETTERS))
        if r.random() < 0.25:
            out.append('*')
    if r.random() < 0.15:
        out.append('$')
    s = ''.join(out)
    return s.replace(delim, '\\' + delim) if delim not in '\\' else s


def rrhs(r, delim):
    out = []
    for _ in range(r.randrange(0, 5)):
        k = r.random()
        if k < 0.5:
            out.append(r.choice(LETTERS + 'XYZ '))
        elif k < 0.65:
            out.append('&')
        elif k < 0.75:
            out.append('\\' + r.choice('123'))
        elif k < 0.8:
            out.append('\\&')
        elif k < 0.84:
            out.append('\\\n')
        else:
            out.append(r.choice(LETTERS))
    return ''.join(out).replace(delim, '\\' + delim)


def raddr1(r):
    k = r.random()
    if k < 0.25:
        return str(r.choice((0, 1, 2, 3, 5, 9, 20, 40, 65536, 65537, 32768)))
    if k < 0.35:
        return '.'
    if k < 0.45:
        return '$'
    if k < 0.55:
        return r.choice(('.', '$', '')) + r.choice('+-^') + r.choice(('', '1', '2', '7'))
    if k < 0.6:
        return "'" + r.choice('abc')
    if k < 0.75:
        d = r.choice('/?')
        return d + rre(r, d) + d
    return r.choice(('1', '2', '3'))


def raddr(r):
    k = r.random()
    if k < 0.35:
        return ''
    if k < 0.75:
        return raddr1(r)
    return raddr1(r) + r.choice((',', ';')) + raddr1(r)


def rtext(r, eight):
    return ''.join(rword(r, eight) + '\n' for _ in range(r.randrange(0, 4))) + '.\n'


def rcmd(r, tool, eight, depth=0):
    a = raddr(r)
    k = r.random()
    pf = r.choice(('', '', '', 'p', 'l'))
    if k < 0.14:
        return a + r.choice('ppl=') + '\n'
    if k < 0.2:
        return a + '\n'
    if k < 0.27:
        return a + 'd' + pf + '\n'
    if k < 0.35:
        return a + r.choice('aic') + '\n' + rtext(r, eight)
    if k < 0.5:
        d = r.choice('/' * 6 + '-,x')
        flags = r.choice(('', '', 'g', 'p', 'gp'))
        if tool == 'em' and r.random() < 0.2:
            flags = r.choice(('n', 'gn', 'gnp'))
        return a + 's' + d + rre(r, d) + d + rrhs(r, d) + d + flags + '\n'
    if k < 0.58 and depth == 0:
        d = r.choice('/' * 4 + '-')
        sub = rcmd(r, tool, eight, 1).rstrip('\n').replace('\n', '\\\n')
        return a + r.choice('gv') + d + rre(r, d) + d + sub + '\n'
    if k < 0.65:
        return a + r.choice('mt') + raddr1(r) + pf + '\n'
    if k < 0.7:
        return a + 'j' + pf + '\n'
    if k < 0.74:
        return a + 'k' + r.choice('abc') + '\n'
    if k < 0.77:
        return a + 'u' + pf + '\n'
    if k < 0.82 and depth == 0:
        return a + r.choice(('w', 'w', 'W')) + ' ' + r.choice(('f2', 'f3', 't')) + '\n'
    if k < 0.85 and depth == 0:
        return a + 'r ' + r.choice(('f2', 't', 'nofile')) + '\n'
    if k < 0.87 and depth == 0:
        return r.choice(('e f2\n', 'E t\n', 'f\n', 'f f3\n', 'e\n', 'w\n'))
    if tool == 'em' and k < 0.93:
        return a + r.choice(('"', '&', '%', 'b+', 'b-', 'h', 'hkeys')) + '\n'
    if tool == 'em' and k < 0.95 and depth == 0:
        return r.choice(('<', '>')) + '\n'
    if k < 0.97:
        return a + r.choice('yYZ#@,;') + '\n'
    return a + r.choice('pl') + '\n'


def rcase(seed, tool=None):
    r = random.Random(seed)
    tool = tool or r.choice(edcmp.TOOLS)
    eight = tool == 'em'
    files = {'t': rfile(r, eight)}
    if r.random() < 0.3:
        files['f2'] = rfile(r, eight)
    cmds = [rcmd(r, tool, eight) for _ in range(r.randrange(1, 18))]
    end = r.choice(('w\nq\n', 'Q\n', 'q\n', '', 'w f3\nQ\n'))
    script = ''.join(cmds) + end
    args = r.choice((['t'], ['t'], ['-', 't'], [], ['nofile']))
    if tool == 'em' and r.random() < 0.15:
        args = ['-s'] + args
    # most scripts through a pipe: an error does not end them there
    return edcmp.Case('fuzz%d' % seed, tool, args, script.encode('latin1'), files,
                      r.random() < 0.8)


def run_one(a):
    seed, tool, keep = a
    c = rcase(seed, tool)
    return seed, c.tool, edcmp.compare(c, keep, max_insns=60_000_000)


def main(argv):
    n, seed, jobs, tool, keep, show = 200, 1, 1, None, None, 20
    a = list(argv)
    while a:
        if a[0] == '-n':
            n, a = int(a[1]), a[2:]
        elif a[0] == '-s':
            seed, a = int(a[1]), a[2:]
        elif a[0] == '-j':
            jobs, a = int(a[1]), a[2:]
        elif a[0] == '--tool':
            tool, a = a[1], a[2:]
        elif a[0] == '--keep':
            keep, a = a[1], a[2:]
        elif a[0] == '--show':
            show, a = int(a[1]), a[2:]
        else:
            sys.exit(__doc__.split('Usage:')[1].split('Exit status')[0])
    t0 = time.time()
    work = [(seed + i, tool, keep) for i in range(n)]
    if jobs > 1:
        with concurrent.futures.ProcessPoolExecutor(jobs) as ex:
            res = list(ex.map(run_one, work, chunksize=4))
    else:
        res = [run_one(w) for w in work]
    from collections import Counter
    cnt = Counter()
    why = Counter()
    bad = []
    for s, t, (kind, detail) in res:
        cnt[(t, kind)] += 1
        if kind.startswith('refused'):
            why[(t, kind, detail[:80])] += 1
        if kind in ('DIFFERENT', 'BAD-REFUSAL'):
            bad.append((s, t, kind, detail))
    print('%d cases, seeds %d..%d' % (n, seed, seed + n - 1))
    for (t, k), v in sorted(cnt.items()):
        print('  %s %-16s %d' % (t, k, v))
    for (t, k, d), v in sorted(why.items()):
        print('    %s %-16s %4d  %s' % (t, k, v, d))
    for s, t, kind, detail in bad[:show]:
        print('seed %d (%s) %s: %s' % (s, t, kind, detail[:500]))
    print('%.0f s' % (time.time() - t0))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
