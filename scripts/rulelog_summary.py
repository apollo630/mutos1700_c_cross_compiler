#!/usr/bin/env python3
"""rulelog_summary.py - what a rule log says, by rule (docs/RULES.md).

The tools append a line to the file MUTOS_RULELOG names for each rule hook
they pass (src/h/mutos_mode.h):

    c1 C1-03 met src=09_amx.c line=1344 out=41935-42035 op=++

This prints, per rule and kind ("met": the original's path taken,
"applied": the rule changed the output), how often and where - the
source and line for c1, the rule's own keys for the others. To see which
of the hooked bugs a whole build meets - the MUTOS kernel's, for one:

    rm -f /tmp/rules.log
    MUTOS_RULELOG=/tmp/rules.log mutos_root/bin/make     (in the kernel's conf/)
    python3 scripts/rulelog_summary.py /tmp/rules.log

Usage:
    rulelog_summary.py LOG [--all]

--all lists every place (without it, the first twenty of each rule).
Python 3, standard library only.
"""
import sys


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    every = '--all' in sys.argv[1:]
    if len(args) != 1:
        print(__doc__.strip().split('\n\n')[-2], file=sys.stderr)
        return 2
    rules = {}
    with open(args[0], encoding='latin-1') as f:
        for line in f:
            parts = line.split()
            if len(parts) < 3:
                continue
            tool, rule, what = parts[:3]
            keys = dict(p.split('=', 1) for p in parts[3:] if '=' in p)
            if 'src' in keys or 'line' in keys:
                where = '%s:%s' % (keys.get('src', '?'), keys.get('line', '?'))
            else:
                where = ' '.join(p for p in parts[3:]) or '-'
            extra = ' '.join('%s=%s' % (k, v) for k, v in keys.items()
                             if k not in ('src', 'line', 'out'))
            rules.setdefault((rule, tool, what), []).append(
                where + (' (%s)' % extra if extra and ('src' in keys or 'line' in keys) else ''))
    if not rules:
        print('no entries')
        return 0
    for (rule, tool, what), places in sorted(rules.items()):
        files = sorted({p.split(':')[0] for p in places})
        print('%-8s %-3s %-7s %5d time(s), %d source(s)' % (rule, tool, what,
                                                           len(places), len(files)))
        shown = places if every else places[:20]
        for p in shown:
            print('    ' + p)
        if len(shown) < len(places):
            print('    ... (%d more; --all lists them)' % (len(places) - len(shown)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
