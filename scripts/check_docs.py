#!/usr/bin/env python3
"""check_docs.py - consistency checker for this project's Markdown docs.

Catches the classes of documentation drift found during manual audits of
this repo (see CLAUDE.md's Session-Start Protocol / Workflow Guideline 6):

  1. Markdown link targets ("[text](path)") that point at a file that does
     not exist.
  2. Markdown anchor links ("[text](#anchor)") that don't match any real
     heading in that file (GitHub's heading-slug algorithm).
  3. Backtick-quoted file references (bare filename or path) whose basename
     doesn't exist anywhere in the repo but closely resembles one that does
     (e.g. `Assembler_as.pdf` when the real file is
     `MUTOS1700_Assembler_as.pdf`) - a likely stale/typo reference.
  4. "Facts" restated in more than one file that have drifted out of sync
     with each other (corpus pass count, STAUTO, NCPS, DIRSIZ, the chkstk
     threshold's (a,b] bound - see FACT_CHECKS below). New facts can be added there
     as one line each; the check needs no "expected value" to maintain,
     only that every occurrence of a fact agrees with every other one.
  5. A "Last updated: YYYY-MM-DD" line that doesn't match the file's actual
     last git-commit date - or, for a file with uncommitted changes, today's
     date (the date the commit about to be made will carry).

This does not replace human review - it only catches the mechanical,
easy-to-miss cases: a number bumped in one place but not another, a
renamed file whose old name is still quoted somewhere, a stale timestamp.

Usage:
    python3 scripts/check_docs.py

Exit status is 0 if clean, 1 if any check found something. Run from
anywhere inside the repo; it locates the repo root itself.
"""

import datetime
import difflib
import os
import re
import subprocess
import sys

ISSUES = 0


def report(msg):
    global ISSUES
    ISSUES += 1
    print(msg)


def repo_root():
    out = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        capture_output=True, text=True, check=True,
    )
    return out.stdout.strip()


def tracked_md_files(root):
    out = subprocess.run(
        ["git", "ls-files", "*.md"],
        capture_output=True, text=True, check=True, cwd=root,
    )
    return sorted(f for f in out.stdout.splitlines() if f)


def tracked_files(root):
    out = subprocess.run(
        ["git", "ls-files"],
        capture_output=True, text=True, check=True, cwd=root,
    )
    return [f for f in out.stdout.splitlines() if f]


def read_lines(root, relpath):
    with open(os.path.join(root, relpath), encoding="utf-8", errors="replace") as f:
        return f.readlines()


# ---------------------------------------------------------------------------
# Check 1 + 2: markdown-syntax links "[text](target)"
# ---------------------------------------------------------------------------

LINK_RE = re.compile(r"\]\(([^)\s]+)\)")


def github_slug(heading_text):
    """Approximate GitHub's heading -> anchor slug algorithm."""
    t = heading_text
    # drop inline formatting markers but keep their contents
    t = re.sub(r"`([^`]*)`", r"\1", t)
    t = re.sub(r"\*\*([^*]*)\*\*", r"\1", t)
    t = re.sub(r"\*([^*]*)\*", r"\1", t)
    t = t.lower()
    # remove anything that isn't a word char, hyphen, or space (Unicode-aware)
    t = re.sub(r"[^\w\- ]", "", t, flags=re.UNICODE)
    t = re.sub(r" ", "-", t)
    return t


def headings_and_slugs(lines):
    """Yield (slug) for every ATX heading, skipping fenced code blocks."""
    in_fence = False
    slugs = []
    for line in lines:
        if line.strip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        m = re.match(r"^(#{1,6})\s+(.*?)\s*$", line)
        if m:
            slugs.append(github_slug(m.group(2)))
    return slugs


def check_links(root, files):
    for relpath in files:
        lines = read_lines(root, relpath)
        text = "".join(lines)
        slugs = None  # computed lazily, only if the file has an anchor link
        for lineno, line in enumerate(lines, 1):
            for target in LINK_RE.findall(line):
                if target.startswith(("http://", "https://", "mailto:")):
                    continue
                if target.startswith("#"):
                    if slugs is None:
                        slugs = set(headings_and_slugs(lines))
                    anchor = target[1:]
                    if anchor not in slugs:
                        report(
                            f"{relpath}:{lineno}: broken anchor link '#{anchor}' "
                            f"(no heading in this file slugs to that)"
                        )
                    continue
                # plain relative path link, e.g. "./STATUS.md"
                candidate = os.path.normpath(os.path.join(root, os.path.dirname(relpath), target))
                if not os.path.exists(candidate):
                    candidate2 = os.path.normpath(os.path.join(root, target))
                    if not os.path.exists(candidate2):
                        report(f"{relpath}:{lineno}: link target does not exist: {target}")


# ---------------------------------------------------------------------------
# Check 3: backtick-quoted file references that don't match any real file
# ---------------------------------------------------------------------------
#
# Matched against basenames only (not full paths): this project's prose
# freely uses directory-relative shorthand ("01_expr/01_intarith.c" instead
# of the full "tests/mutos_cc/01_expr/01_intarith.c"), which is legitimate
# and not worth flagging. What *is* worth flagging is a basename that exists
# nowhere in the repo but closely resembles one that does - that's what
# actually happened with `Assembler_as.pdf` vs. the real
# `MUTOS1700_Assembler_as.pdf`.
#
# A few deliberate exclusions keep this check quiet on this project's own
# prose conventions rather than firing on them every run:
#   - a span starting with "." is a suffix pattern ("*.i.golden"), not a
#     filename
#   - a span under 12 chars is too short/generic to fuzzy-match reliably
#     (`a.out`, `foo.s`, `mch.o` all coincidentally resemble real files)
#   - an ALL-CAPS stem ("NAME.o.golden") is a Makefile-variable-style
#     placeholder, not a literal filename
#   - a bare `*.o` whose `*.s`/`*.c` sibling is a real, tracked file is a
#     legitimate (uncommitted) build artifact, not a broken reference

CODE_SPAN_RE = re.compile(r"`([^`]+)`")
PATHLIKE_RE = re.compile(r"^[\w.\-]+(/[\w.\-]+)+$")
BARE_FILENAME_RE = re.compile(r"^[\w.\-]+\.[A-Za-z0-9]{1,6}$")
MIN_CHECK_LEN = 12
BUILDABLE_SIBLING_EXTS = ("s", "c")


def check_file_references(root, all_files, md_files):
    basenames = sorted({os.path.basename(f) for f in all_files})
    basename_set = set(basenames)

    def is_valid(basename):
        if basename in basename_set:
            return True
        stem, ext = os.path.splitext(basename)
        if ext.lstrip(".") == "o":
            return any(f"{stem}.{alt}" in basename_set for alt in BUILDABLE_SIBLING_EXTS)
        return False

    for relpath in md_files:
        lines = read_lines(root, relpath)
        for lineno, line in enumerate(lines, 1):
            for span in CODE_SPAN_RE.findall(line):
                is_path = bool(PATHLIKE_RE.match(span))
                is_bare = bool(BARE_FILENAME_RE.match(span))
                if not (is_path or is_bare):
                    continue
                if span.startswith(("http:", "https:", "github.com")):
                    continue
                basename = os.path.basename(span) if is_path else span
                if "." not in basename or basename.startswith("."):
                    continue
                if len(basename) < MIN_CHECK_LEN:
                    continue
                stem = basename.split(".", 1)[0]
                if stem.isalpha() and stem.isupper():
                    continue
                if is_valid(basename):
                    continue
                close = difflib.get_close_matches(basename, basenames, n=1, cutoff=0.75)
                if close:
                    report(
                        f"{relpath}:{lineno}: `{span}` does not exist, but a "
                        f"similarly-named file does: `{close[0]}` - likely a stale "
                        f"reference to a renamed file"
                    )


# ---------------------------------------------------------------------------
# Check 4: facts restated in multiple places must agree
# ---------------------------------------------------------------------------

FACT_CHECKS = [
    ("mutos_cc corpus pass count (N/62)",
     re.compile(r"\b(?!62/62\b)(\d+)/62\b")),
    ("STAUTO value",
     re.compile(r"STAUTO\W{0,10}?(-?\d+)")),
    ("NCPS value",
     re.compile(r"NCPS\W{0,10}?(\d+)")),
    ("DIRSIZ value",
     re.compile(r"DIRSIZ\W{0,10}?(\d+)")),
    ("chkstk threshold bound (a,b]",
     re.compile(r"chkstk[\s\S]{0,80}?\(\s*(\d+)\s*,\s*(\d+)\s*\]|"
                 r"\(\s*(\d+)\s*,\s*(\d+)\s*\][\s\S]{0,80}?chkstk")),
]


def check_facts(root, files):
    for name, pattern in FACT_CHECKS:
        seen = {}
        for relpath in files:
            lines = read_lines(root, relpath)
            for lineno, line in enumerate(lines, 1):
                for m in pattern.finditer(line):
                    groups = tuple(g for g in m.groups() if g is not None)
                    seen.setdefault(groups, []).append((relpath, lineno, line.strip()))
        if len(seen) > 1:
            report(f"INCONSISTENT FACT: {name} has {len(seen)} different values across the repo:")
            for val, locs in seen.items():
                report(f"  value {val}:")
                for relpath, lineno, line in locs:
                    report(f"    {relpath}:{lineno}: {line}")


# ---------------------------------------------------------------------------
# Check 5: "Last updated" / "Stand" date stamps vs. actual last git commit
# ---------------------------------------------------------------------------

DATE_STAMP_RE = re.compile(r"(?:Last updated|Stand)[:,]?\s*(\d{4}-\d{2}-\d{2})")


def last_commit_date(root, relpath):
    out = subprocess.run(
        ["git", "log", "-1", "--format=%ad", "--date=short", "--", relpath],
        capture_output=True, text=True, check=True, cwd=root,
    )
    return out.stdout.strip() or None


def has_uncommitted_changes(root, relpath):
    """True if the working tree (staged or not) differs from HEAD for
    `relpath`. False if it does not - or if there is no HEAD yet."""
    r = subprocess.run(
        ["git", "diff", "--quiet", "HEAD", "--", relpath],
        capture_output=True, cwd=root,
    )
    return r.returncode == 1


def check_last_updated(root, files):
    # A file that is about to be committed must carry the date of THAT
    # commit, not of its previous one. Comparing only against `git log`
    # made the pre-commit hook reject every correctly bumped stamp on the
    # first commit of a new day (the log still showed the previous day's
    # commit) - and let through a commit on a later day that forgot to
    # bump the stamp at all (CI then failed after the push). For a file
    # with uncommitted changes the expected
    # date is therefore today's (local time, as git records the author
    # date of a new commit); for an unchanged file, e.g. in CI's clean
    # checkout, it is still its last commit's date.
    today = datetime.date.today().isoformat()
    for relpath in files:
        lines = read_lines(root, relpath)
        changed = None
        for lineno, line in enumerate(lines, 1):
            m = DATE_STAMP_RE.search(line)
            if not m:
                continue
            stated = m.group(1)
            if changed is None:
                changed = has_uncommitted_changes(root, relpath)
            if changed:
                if stated != today:
                    report(
                        f"{relpath}:{lineno}: says 'Last updated {stated}' but the "
                        f"file has uncommitted changes, so the commit carrying them "
                        f"will be dated {today} - bump the date as part of that change"
                    )
                continue
            actual = last_commit_date(root, relpath)
            if actual and actual != stated:
                report(
                    f"{relpath}:{lineno}: says 'Last updated {stated}' but the file's "
                    f"last commit is dated {actual} - bump the date as part of that change"
                )


# ---------------------------------------------------------------------------

def main():
    root = repo_root()
    md_files = tracked_md_files(root)
    all_files = tracked_files(root)

    check_links(root, md_files)
    check_file_references(root, all_files, md_files)
    check_facts(root, md_files)
    check_last_updated(root, md_files)

    if ISSUES:
        print(f"\n{ISSUES} issue(s) found.")
        return 1
    print("check_docs: no issues found.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
