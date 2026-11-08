#!/usr/bin/env python3
"""check_rules.py - the rules of the corrected mode and of -1 against their
register (docs/RULES.md) and their records.

docs/TOOLCHAIN_PLAN.md, section 7.2: each rule - the correction of one bug
of an original tool, or one 80186/V30 code rule - is a source file of its
own in its tool's rules directory, reached through a one-line hook in the
ported code, and listed in docs/RULES.md. Taking a rule out is deleting its
file, its hook and its row. This script checks that the three agree:

  1. every row of docs/RULES.md's register names a tool, a switch (-F or
     -1) and a state (diagnostic or active); its file exists in that tool's
     rules directory, is named after the rule (C1-03: c1_03.c, V30-PUSH:
     v30_push.c), defines rule_<name>() and logs under the rule's own name
     ("C1-03");
  2. rule_<name>( is called exactly once in the tool's other sources - the
     hook -, in the file the row names, on a line that names the rule
     ("C1-03" in its comment), or right below one that does;
  3. every rule file in a rules directory and every rule_...( call in the
     sources has its row;
  4. a correction's bug has its entry in docs/MUTOS_ORIGINAL_BUGS.md
     ("### C1-03 ..."), and that entry names docs/RULES.md; a V30 rule is
     named in docs/TOOLCHAIN_PLAN.md, section 7.4.

What a rule changes is checked by running it: tests/rules/check_accounting.py.

Usage:
    python3 scripts/check_rules.py

Exit status 0 if everything agrees, 1 otherwise. Standard library only.
"""

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, '..'))

# the tool's ported sources and its rules directory
TOOLS = {
    'c0': ('src/mutos_cc/c0port', 'src/mutos_cc/c0rules'),
    'c1': ('src/mutos_cc/c1port', 'src/mutos_cc/c1rules'),
    'c2': ('src/mutos_cc/c2port', 'src/mutos_cc/c2rules'),
    'as': ('src/mutos_as', 'src/mutos_as/asrules'),
    'ld': ('src/mutos_ld', 'src/mutos_ld/ldrules'),
}
SWITCHES = ('-F', '-1')
STATES = ('diagnostic', 'active')

ISSUES = []


def issue(msg):
    ISSUES.append(msg)


def read(rel):
    with open(os.path.join(REPO, rel), encoding='utf-8', errors='replace') as f:
        return f.read()


def slug(rule):
    """C1-03 -> c1_03, V30-PUSH -> v30_push."""
    return rule.lower().replace('-', '_')


def register():
    """The rows of docs/RULES.md's register: a list of dicts."""
    rows = []
    text = read('docs/RULES.md')
    lines = text.splitlines()
    start = None
    for i, line in enumerate(lines):
        if re.match(r'\|\s*Rule\s*\|\s*Tool\s*\|', line):
            start = i
            break
    if start is None:
        issue('docs/RULES.md: no register table (a header "| Rule | Tool | ...")')
        return rows
    head = [c.strip() for c in lines[start].strip().strip('|').split('|')]
    for line in lines[start + 2:]:
        if not line.startswith('|'):
            break
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if len(cells) != len(head):
            issue('docs/RULES.md: a register row with %d cells: %s' % (len(cells), line))
            continue
        row = dict(zip(head, cells))
        row = {k: v.replace('`', '') for k, v in row.items()}
        rows.append(row)
    return rows


def sources(directory):
    """The .c and .h files of a directory (relative to the repo), sorted."""
    d = os.path.join(REPO, directory)
    if not os.path.isdir(d):
        return []
    return sorted(os.path.join(directory, n) for n in os.listdir(d)
                  if n.endswith('.c') or n.endswith('.h'))


def rule_files():
    """{path: tool} of every rule file in the rules directories."""
    out = {}
    for tool, (_, rdir) in TOOLS.items():
        for p in sources(rdir):
            base = os.path.basename(p)
            if not base.endswith('.c') or base == tool + 'rules.c':
                continue
            out[p] = tool
    return out


def hooks():
    """{function: [(path, line number, the line before and the line)]} of
    the rule_...( calls in the ported sources (the rules directories left
    out)."""
    out = {}
    seen = set()
    for tool, (pdir, _) in TOOLS.items():
        for p in sources(pdir):
            if p in seen:
                continue
            seen.add(p)
            prev = ''
            for n, line in enumerate(read(p).splitlines(), 1):
                for m in re.finditer(r'\b(rule_[a-z0-9_]+)\s*\(', line):
                    out.setdefault(m.group(1), []).append((p, n, prev + '\n' + line))
                prev = line
    return out


def main():
    rows = register()
    files = rule_files()
    calls = hooks()
    bugs = read('docs/MUTOS_ORIGINAL_BUGS.md')
    plan = read('docs/TOOLCHAIN_PLAN.md')
    named = set()
    for row in rows:
        rule = row.get('Rule', '')
        tool = row.get('Tool', '')
        if not re.fullmatch(r'[A-Z0-9]+-[A-Z0-9]+', rule):
            issue('docs/RULES.md: a rule named %r' % rule)
            continue
        if rule in named:
            issue('docs/RULES.md: %s is listed twice' % rule)
        named.add(rule)
        if tool not in TOOLS:
            issue('%s: tool %r is none of %s' % (rule, tool, ', '.join(TOOLS)))
            continue
        if row.get('Switch') not in SWITCHES:
            issue('%s: switch %r is neither -F nor -1' % (rule, row.get('Switch')))
        if row.get('State') not in STATES:
            issue('%s: state %r is neither diagnostic nor active' % (rule, row.get('State')))
        fn = 'rule_' + slug(rule)
        want = os.path.join(TOOLS[tool][1], slug(rule) + '.c')
        if row.get('File') != want:
            issue('%s: the file is %r, it must be %s' % (rule, row.get('File'), want))
        if not os.path.isfile(os.path.join(REPO, want)):
            issue('%s: %s does not exist' % (rule, want))
        else:
            text = read(want)
            if not re.search(r'^\w[\w \t\*]*\b%s\s*\(' % fn, text, re.M) and \
               not re.search(r'^%s\s*\(' % fn, text, re.M):
                issue('%s: %s does not define %s()' % (rule, want, fn))
            if '"%s"' % rule not in text:
                issue('%s: %s does not log under "%s"' % (rule, want, rule))
        sites = calls.get(fn, [])
        if len(sites) != 1:
            issue('%s: %s() is called %d times in the ported sources, the hook must be '
                  'one call' % (rule, fn, len(sites)))
        else:
            p, n, line = sites[0]
            hookfile = row.get('Hook', '').split(',')[0].strip()
            if hookfile != p:
                issue('%s: the hook is in %s:%d, the register says %r' % (rule, p, n, hookfile))
            if rule not in line:
                issue('%s: the hook line %s:%d (or the line above it) does not name '
                      'the rule' % (rule, p, n))
        if row.get('Switch') == '-F':
            m = re.search(r'^### %s .*?(?=^### |^## |\Z)' % re.escape(rule), bugs, re.M | re.S)
            if m is None:
                issue('%s: no entry "### %s ..." in docs/MUTOS_ORIGINAL_BUGS.md' % (rule, rule))
            elif 'docs/RULES.md' not in m.group(0) and 'RULES.md' not in m.group(0):
                issue('%s: its entry in docs/MUTOS_ORIGINAL_BUGS.md does not name '
                      'docs/RULES.md' % rule)
        else:
            sec = re.search(r'^### 7\.4 .*?(?=^### |^## |\Z)', plan, re.M | re.S)
            if sec is None or rule not in sec.group(0):
                issue('%s: not named in docs/TOOLCHAIN_PLAN.md, section 7.4' % rule)
    listed_files = {os.path.join(TOOLS[r['Tool']][1], slug(r['Rule']) + '.c')
                    for r in rows if r.get('Tool') in TOOLS and r.get('Rule')}
    for p in sorted(files):
        if p not in listed_files:
            issue('%s: a rule file without a row in docs/RULES.md' % p)
    listed_fns = {'rule_' + slug(r['Rule']) for r in rows if r.get('Rule')}
    for fn, sites in sorted(calls.items()):
        if fn not in listed_fns:
            for p, n, _ in sites:
                issue('%s:%d: %s() is not in docs/RULES.md' % (p, n, fn))
    if ISSUES:
        for m in ISSUES:
            print('check_rules: ' + m)
        print('check_rules: %d problem(s)' % len(ISSUES))
        return 1
    print('check_rules: %d rules - files, hooks, register and records agree' % len(rows))
    return 0


if __name__ == '__main__':
    sys.exit(main())
