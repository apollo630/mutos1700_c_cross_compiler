#!/usr/bin/env python3
"""fuzz_c.py - semantic and differential fuzzing for mutos_c0/mutos_c1.

Generates random K&R programs inside mutos_c0's grammar, runs each one
through the real pipeline (mutos_cpp -P | mutos_c0 | mutos_c1 |
mutos_as), executes the resulting assembly with x86sim.py, and compares
the final value of EVERY variable - scalars and every array element -
and main()'s return value with what the program must compute under C
semantics on a 16-bit int (evaluated by this script from the program's
own syntax tree). This catches what the golden corpus cannot: code that
assembles and looks plausible but computes the wrong thing, and whole
construct classes the corpus never contains.

With --baseline DIR (a directory holding the mutos_c0 and mutos_c1 of
an earlier build) every program also runs through the baseline
pipeline, and each program's outcome - correct, wrong, refused - is
compared: a program the baseline compiled correctly that the new build
now refuses or miscompiles is a regression. No knowledge of the change
under test is needed; the semantic check classifies every difference.

Exit status 1 if anything is BAD: wrong code, output that does not
assemble, a crash / sanitizer report / "internal:" diagnostic, code
x86sim.py cannot execute, or (with --baseline) a regression. An
explicit "not yet supported" refusal is NOT bad - it is the compiler
declining a shape it has no golden for; refusals are tallied by reason.

Usage:
    fuzz_c.py [-n COUNT] [-s SEED] [-j JOBS] [--baseline DIR]
              [--no-arrays] [--scope] [--keep DIR] [--reasons]

See README.md in this directory for what is generated and why.
"""
import argparse
import os
import random
import re
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.dont_write_bytecode = True  # no __pycache__/ in the test tree
sys.path.insert(0, str(Path(__file__).resolve().parent))
import x86sim  # noqa: E402

ROOT = Path(__file__).resolve().parents[3]
TOOLS = {
    "cpp": os.environ.get("MUTOS_CPP", str(ROOT / "src/mutos_cpp/mutos_cpp")),
    "c0": os.environ.get("MUTOS_C0", str(ROOT / "src/mutos_cc/mutos_c0")),
    "c1": os.environ.get("MUTOS_C1", str(ROOT / "src/mutos_cc/mutos_c1")),
    "as": os.environ.get("MUTOS_AS", str(ROOT / "src/mutos_as/mutos_as")),
}


def w(v):
    """C int arithmetic on this target: 16 bits, two's complement."""
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Skip(Exception):
    """The program would divide by zero / overflow a division: dropped."""


# ---------------------------------------------------------------------
# Syntax tree, rendering, evaluation.
#
# Nodes are tuples: ("con", v) ("var", name) ("el2", arr, i, j)
# ("el1", arr, i) ("bin", op, l, r) ("un", op, e) ("cond", c, t, f)
# ("comma", e1, e2) - "(u = e1, e2)" - and ("inc", form, name).
# ---------------------------------------------------------------------

def render(n):
    k = n[0]
    if k == "con":
        return str(n[1])
    if k == "var":
        return n[1]
    if k == "el2":
        return f"{n[1]}[{n[2]}][{n[3]}]"
    if k == "el1":
        return f"{n[1]}[{n[2]}]"
    if k == "bin":
        return f"({render(n[2])} {n[1]} {render(n[3])})"
    if k == "un":
        return f"{n[1]}{render(n[2])}"
    if k == "cond":
        return f"({render(n[1])} ? {render(n[2])} : {render(n[3])})"
    if k == "comma":
        return f"(u = {render(n[1])}, {render(n[2])})"
    if k == "inc":
        return {"post++": f"{n[2]}++", "pre++": f"++{n[2]}",
                "post--": f"{n[2]}--", "pre--": f"--{n[2]}"}[n[1]]
    raise ValueError(k)


def evaluate(n, env):
    """Value of `n` under C semantics, applying its side effects to env
    - only for operands C actually evaluates (&&, ||, ?: short-circuit)."""
    k = n[0]
    if k == "con":
        return n[1]
    if k == "var":
        return env[n[1]]
    if k == "el2":
        return env[(n[1], env[n[2]], env[n[3]])]
    if k == "el1":
        return env[(n[1], env[n[2]])]
    if k == "un":
        v = evaluate(n[2], env)
        return w(~v) if n[1] == "~" else int(v == 0)
    if k == "cond":
        return evaluate(n[2], env) if evaluate(n[1], env) else evaluate(n[3], env)
    if k == "comma":
        env["u"] = evaluate(n[1], env)
        return evaluate(n[2], env)
    if k == "inc":
        old = env[n[2]]
        new = w(old + (1 if "++" in n[1] else -1))
        env[n[2]] = new
        return old if n[1].startswith("post") else new
    op = n[1]
    lv = evaluate(n[2], env)
    if op == "&&":
        return int(bool(lv) and bool(evaluate(n[3], env)))
    if op == "||":
        return int(bool(lv) or bool(evaluate(n[3], env)))
    rv = evaluate(n[3], env)
    if op in ("/", "%"):
        if rv == 0 or (lv == -0x8000 and rv == -1):
            raise Skip()
        q = abs(lv) // abs(rv) * (1 if (lv < 0) == (rv < 0) else -1)
        return w(q) if op == "/" else w(lv - q * rv)
    if op == "<<":
        return w(lv << rv)
    if op == ">>":
        return lv >> rv                    # sar: arithmetic
    return {"+": lambda: w(lv + rv), "-": lambda: w(lv - rv),
            "*": lambda: w(lv * rv), "&": lambda: w(lv & rv),
            "|": lambda: w(lv | rv), "^": lambda: w(lv ^ rv),
            "<": lambda: int(lv < rv), "<=": lambda: int(lv <= rv),
            ">": lambda: int(lv > rv), ">=": lambda: int(lv >= rv),
            "==": lambda: int(lv == rv), "!=": lambda: int(lv != rv)}[op]()


# ---------------------------------------------------------------------
# Program generator.
#
# Variables: i, j, k - subscripts, 0 or 1, never reassigned (every
# subscript stays in bounds); x, y, s, t - ordinary scalars; u - written
# only by a comma item "(u = e1, e2)"; n - touched only through ++/--.
# u and n are never read otherwise and at most one of those side
# effects occurs per statement, so no statement's value depends on C's
# unspecified operand evaluation order.
#
# With --scope, some of x, y, s, t are file-scope variables instead
# ("int y;", "static int y;", or "extern int y;" before main() and "int
# y;" after it - the 07_scope shapes), and a statement may be a nested
# block that declares a local of a global's name, shadowing it for the
# block's own statements ("{ int y; y = 3; x = y + 1; }"). The globals'
# final and per-statement values are checked at their fixed addresses.
# ---------------------------------------------------------------------

BINOPS = ["+", "-", "*", "/", "%", "&", "|", "^", "<<", ">>",
          "<", "<=", ">", ">=", "==", "!=", "&&", "||"]
SCALARS = ["x", "y", "s", "t"]
INDEXES = ["i", "j", "k"]


GLOBAL_KINDS = ["plain", "static", "extern"]


class Gen:
    def __init__(self, rnd, arrays, scope=False):
        self.r = rnd
        self.a2 = {}
        self.a1 = {}
        # name -> "plain" | "static" | "extern" (see the comment above)
        self.globals = {}
        if scope:
            names = [n for n in SCALARS if rnd.random() < 0.5] or [rnd.choice(SCALARS)]
            self.globals = {n: rnd.choice(GLOBAL_KINDS) for n in names}
        if arrays:
            # Locals beyond the scalars' 20 bytes: at most 60, so the frame
            # stays within the confirmed "sub sp,N" range (N <= 80) - N in
            # 81..127 is an explicit c1 refusal (see c1_gen.c's SETSTK).
            while True:
                self.a2 = {name: (rnd.choice([2, 4]), rnd.choice([2, 4]))
                           for name in ("a", "b", "c")[: rnd.randint(1, 3)]}
                self.a1 = {"v": 4} if rnd.random() < 0.5 else {}
                size = sum(2 * r * c for r, c in self.a2.values()) + 8 * len(self.a1)
                if size <= 60:
                    break
        self.side_effect_used = False

    def leaf(self, allow_const=True):
        r = self.r.random()
        if r < 0.25 and allow_const:
            return ("con", self.r.randint(0, 20))
        if r < 0.55 or not (self.a2 or self.a1):
            return ("var", self.r.choice(SCALARS + INDEXES[:1]))
        if self.a1 and r < 0.65:
            return ("el1", "v", self.r.choice(INDEXES))
        if not self.a2:
            return ("var", self.r.choice(SCALARS))
        name = self.r.choice(list(self.a2))
        return ("el2", name, self.r.choice(INDEXES), self.r.choice(INDEXES))

    def side_effect(self, depth, cond_ctx):
        """`cond_ctx`: the operand is conditionally evaluated (a ?: arm, an
        &&/|| right operand) - where C may skip the side effect."""
        if self.side_effect_used:
            return None
        if self.r.random() >= 0.08:
            return None
        self.side_effect_used = True
        if self.r.random() < 0.5:
            forms = ["post++", "pre++", "post--", "pre--"]
            return ("inc", self.r.choice(forms), "n")
        e1 = self.expr(max(depth - 1, 0), cond_ctx)
        while e1[0] in ("var", "el1", "el2"):
            e1 = self.expr(max(depth - 1, 0), cond_ctx)
        return ("comma", e1, self.expr(max(depth - 1, 0), cond_ctx))

    def expr(self, depth, cond_ctx=False, truth=False):
        """`truth`: the value is tested for zero (an operand of !, && or
        ||, a ?: or if condition) - never a bare constant there: c1
        refuses one, and real code does not write "if (4)"."""
        se = self.side_effect(depth, cond_ctx)
        if se:
            return se
        if depth <= 0 or self.r.random() < 0.25:
            return self.leaf(allow_const=not truth)
        r = self.r.random()
        if self.a2 and r < 0.12:
            # a product of two 2-D elements - the evaluation-order case
            return ("bin", "*", self.leaf_el2(), self.leaf_el2())
        if r < 0.20:
            op = self.r.choice("~!")
            return ("un", op, self.expr(depth - 1, cond_ctx, truth=op == "!"))
        if r < 0.28:
            return ("cond", self.expr(depth - 1, cond_ctx, truth=True),
                    self.expr(depth - 1, True), self.expr(depth - 1, True))
        op = self.r.choice(BINOPS)
        logical = op in ("&&", "||")
        left = self.expr(depth - 1, cond_ctx, truth=logical)
        if op in ("<<", ">>"):
            return ("bin", op, left, ("con", self.r.randint(1, 4)))
        # c1 has no spilling yet: two compound operands mostly collide in
        # its working registers and are refused, so keep the right one
        # simple most of the time (both still happen).
        rdepth = depth - 1 if self.r.random() < 0.35 else 0
        # A constant divisor, or a constant multiplier of 0 or a power of
        # two, is an explicit c1 refusal (no golden shows its shape).
        right = self.expr(rdepth, cond_ctx or logical,
                          truth=logical or op in ("/", "%"))
        if op == "*" and right[0] == "con" and right[1] & (right[1] - 1) == 0:
            right = ("con", self.r.choice([3, 5, 6, 7, 9, 10, 11, 12]))
        return ("bin", op, left, right)

    def leaf_el2(self):
        name = self.r.choice(list(self.a2))
        return ("el2", name, self.r.choice(INDEXES), self.r.choice(INDEXES))

    def target(self):
        if self.a2 and self.r.random() < 0.2:
            return self.leaf_el2()
        if self.a1 and self.r.random() < 0.1:
            return ("el1", "v", self.r.choice(INDEXES))
        return ("var", self.r.choice(SCALARS))

    def rhs(self):
        depth = self.r.choice([1, 1, 2, 2, 3])
        e = self.expr(depth)
        while e[0] in ("var", "el1", "el2"):
            e = self.expr(depth)   # "x = y;" and the like are refused
        return e

    def statement(self, nested=False):
        self.side_effect_used = False
        if self.globals and not nested and self.r.random() < 0.2:
            # A block shadowing one global with a local of its name.
            var = self.r.choice(list(self.globals))
            inner = [self.statement(nested=True)
                     for _ in range(self.r.randint(1, 2))]
            return ("block", var, self.r.randint(-5, 9), inner)
        if self.r.random() < 0.2:
            cond = self.expr(self.r.randint(1, 2), truth=True)
            then = ("assign", self.target(), self.rhs_in_cond())
            other = None
            if self.r.random() < 0.4:
                other = ("assign", self.target(), self.rhs_in_cond())
            return ("if", cond, then, other)
        return ("assign", self.target(), self.rhs())

    def rhs_in_cond(self):
        # A branch of an if is conditionally executed as a whole - which
        # C and c1 agree on - so side effects are fine there.
        return self.rhs()

    def program(self):
        env = {}
        decl, init = [], []
        for name, (rows, cols) in self.a2.items():
            decl.append(f"\tint {name}[{rows}][{cols}];")
            for p in range(rows):
                for q in range(cols):
                    env[(name, p, q)] = self.r.randint(-9, 20)
                    init.append(f"\t{name}[{p}][{q}] = {env[(name, p, q)]};")
        for name, size in self.a1.items():
            decl.append(f"\tint {name}[{size}];")
            for p in range(size):
                env[(name, p)] = self.r.randint(-9, 20)
                init.append(f"\t{name}[{p}] = {env[(name, p)]};")
        # m: the statement marker - "m = k;" before the k-th statement,
        # so every intermediate state is checked, not only the final one
        # (a wrong value a later statement overwrites is still caught).
        names = INDEXES + SCALARS + ["u", "n", "m"]
        decl.append("\tint " + ", ".join(n for n in names if n not in self.globals) + ";")
        for name in names[:-1]:            # not m: its first store is a marker
            env[name] = self.r.randint(0, 1) if name in INDEXES else self.r.randint(-5, 9)
            init.append(f"\t{name} = {env[name]};")
        body = []
        self.snaps = []
        self.stmts = []
        nstmt = self.r.randint(1, 4)
        for k in range(1, nstmt + 2):
            env["m"] = 100 + k
            body.append(f"\tm = {100 + k};")
            self.snaps.append(dict(env))
            if k <= nstmt:
                st = self.statement()
                self.stmts.append(st)
                self.execute(st, env)
                body.append(self.render_stmt(st))
        self.side_effect_used = False
        ret = self.expr(self.r.randint(0, 2)) if self.r.random() < 0.3 else ("var", "s")
        self.ret_node = ret
        retval = evaluate(ret, env)
        body.append(f"\treturn {render(ret)};")
        before = "".join({"plain": f"int {n};\n", "static": f"static int {n};\n",
                          "extern": f"extern int {n};\n"}[k]
                         for n, k in self.globals.items())
        after = "".join(f"int {n};\n" for n, k in self.globals.items() if k == "extern")
        src = (before + "main()\n{\n" + "\n".join(decl) + "\n\n" +
               "\n".join(init + body) + "\n}\n" + after)
        return src, env, retval

    def store(self, tgt, val, env):
        if tgt[0] == "var":
            env[tgt[1]] = val
        elif tgt[0] == "el2":
            env[(tgt[1], env[tgt[2]], env[tgt[3]])] = val
        else:
            env[(tgt[1], env[tgt[2]])] = val

    def execute(self, st, env):
        if st[0] == "assign":
            self.store(st[1], evaluate(st[2], env), env)
        elif st[0] == "block":
            # The inner local hides the global for the block's duration;
            # the global's own value is untouched by the block.
            saved = env[st[1]]
            env[st[1]] = st[2]
            for inner in st[3]:
                self.execute(inner, env)
            env[st[1]] = saved
        elif evaluate(st[1], env):
            self.execute(st[2], env)
        elif st[3]:
            self.execute(st[3], env)

    def render_stmt(self, st):
        if st[0] == "assign":
            return f"\t{render(st[1])} = {render(st[2])};"
        if st[0] == "block":
            inner = "\n".join("\t" + line for line in
                               "\n".join(self.render_stmt(x) for x in st[3]).split("\n"))
            return (f"\t{{\n\t\tint {st[1]};\n\n\t\t{st[1]} = {st[2]};\n"
                    f"{inner}\n\t}}")
        s = f"\tif ({render(st[1])})\n\t\t{self.render_stmt(st[2]).strip()}"
        if st[3]:
            s += f"\n\telse\n\t\t{self.render_stmt(st[3]).strip()}"
        return s


def generate(rnd, arrays, scope=False):
    while True:
        g = Gen(rnd, arrays, scope)
        try:
            src, env, ret = g.program()
        except Skip:
            continue
        return src, env, ret, g


# ---------------------------------------------------------------------
# Running one program through one build.
# ---------------------------------------------------------------------

BUG_MARKERS = ("Sanitizer", "runtime error", "internal:")


def reason_of(stderr):
    line = stderr.strip().splitlines()[-1] if stderr.strip() else "(no message)"
    line = re.sub(r"^\S+:\d+: error: ", "", line)
    line = re.sub(r"^mutos_c[01]: ", "", line)
    return re.sub(r"-?\d+", "N", line)[:100]


def run_build(wd, tag, c0, c1, env, ret, g):
    """Outcome of one build: ('ok'|'wrong'|'refused'|'bad', detail, .s)"""
    i_file, one, two, s_file = wd / "p.i", wd / f"{tag}.1", wd / f"{tag}.2", wd / f"{tag}.s"
    p = subprocess.run([c0, str(i_file), str(one), str(two)], capture_output=True, text=True)
    if p.returncode not in (0, 1) or any(m in p.stderr for m in BUG_MARKERS):
        return "bad", f"mutos_c0 crashed or reported an internal error: {p.stderr.strip()[-300:]}", ""
    if p.returncode:
        return "refused", "c0: " + reason_of(p.stderr), ""
    p = subprocess.run([c1, str(one), str(two), str(s_file)], capture_output=True, text=True)
    if p.returncode not in (0, 1) or any(m in p.stderr.replace("internal limit", "") for m in BUG_MARKERS):
        return "bad", f"mutos_c1 crashed or reported an internal error: {p.stderr.strip()[-300:]}", ""
    if p.returncode:
        return "refused", "c1: " + reason_of(p.stderr), ""
    text = s_file.read_text()
    p = subprocess.run([TOOLS["as"], "-o", str(wd / f"{tag}.o"), str(s_file)],
                       capture_output=True, text=True)
    if p.returncode:
        return "bad", f"output does not assemble: {p.stderr.strip()[-300:]}", text
    try:
        r = x86sim.run(text, watch="m")
    except x86sim.SimError as e:
        return "bad", f"x86sim cannot execute the output: {e}", text
    diffs = []
    marks = [v for v, _ in r.snapshots]
    want = [e["m"] for e in g.snaps]
    if marks != want:
        diffs.append(f"statement markers {marks}, expected {want}")
    else:
        for (mark, snap), expect in zip(r.snapshots, g.snaps):
            d = state_diffs(snap, expect, g)
            if d:
                diffs.append(f"before statement {mark - 100}: " + "; ".join(d[:4]))
                break
    if r.ret != ret:
        diffs.append(f"return value {r.ret}, expected {ret}")
    diffs += state_diffs(r, env, g)
    if diffs:
        return "wrong", "; ".join(diffs[:6]), text
    return "ok", "", text


def state_diffs(r, env, g):
    """Every variable whose value in `r` differs from `env`."""
    diffs = []
    for key, val in env.items():
        if isinstance(key, tuple):
            name = key[0]
            if len(key) == 3:
                off = r.locals[name] + 2 * (key[1] * g.a2[name][1] + key[2])
            else:
                off = r.locals[name] + 2 * key[1]
            label = f"{name}{''.join(f'[{x}]' for x in key[1:])}"
        elif key in g.globals:
            # A file-scope variable: its fixed address, not a frame slot
            # (a block's shadowing local of the same name registers
            # "| _y=-N." too, so r.locals[key] is not it).
            got = r.word_at(r.data["_" + key])
            if got != val:
                diffs.append(f"{key} (global) = {got}, expected {val}")
            continue
        else:
            off, label = r.locals[key], key
        if r.word(off) != val:
            diffs.append(f"{label} = {r.word(off)}, expected {val}")
    return diffs


def run_one(job):
    idx, src, env, ret, g, args, workroot = job
    wd = Path(workroot) / f"p{idx}"
    wd.mkdir()
    (wd / "p.c").write_text(src)
    p = subprocess.run([TOOLS["cpp"], "-P", str(wd / "p.c"), str(wd / "p.i")],
                       capture_output=True, text=True)
    if p.returncode:
        return idx, ("bad", "mutos_cpp failed: " + p.stderr.strip()[-200:], ""), None
    new = run_build(wd, "new", TOOLS["c0"], TOOLS["c1"], env, ret, g)
    old = None
    if args.baseline:
        old = run_build(wd, "old", str(Path(args.baseline) / "mutos_c0"),
                        str(Path(args.baseline) / "mutos_c1"), env, ret, g)
    return idx, new, old


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("-n", "--count", type=int, default=500)
    ap.add_argument("-s", "--seed", type=int, default=1)
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--baseline", metavar="DIR",
                    help="directory with an earlier build's mutos_c0 and mutos_c1")
    ap.add_argument("--no-arrays", action="store_true",
                    help="scalars only (no 1-D/2-D arrays)")
    ap.add_argument("--scope", action="store_true",
                    help="file-scope variables and shadowing nested blocks")
    ap.add_argument("--keep", metavar="DIR", help="where to save BAD programs "
                    "(default: a new directory under /tmp)")
    ap.add_argument("--reasons", action="store_true",
                    help="list every refusal reason with its count")
    args = ap.parse_args()

    for name, path in TOOLS.items():
        if not os.access(path, os.X_OK):
            sys.exit(f"fuzz_c: {path} not found - build first (make) or set "
                     f"MUTOS_{name.upper()}")
    rnd = random.Random(args.seed)
    progs = [generate(rnd, not args.no_arrays, args.scope) for _ in range(args.count)]

    counts, reasons, bad, cmp = {}, {}, [], {}
    with tempfile.TemporaryDirectory(prefix="fuzz_c.") as workroot:
        jobs = [(i, *progs[i], args, workroot) for i in range(args.count)]
        with ThreadPoolExecutor(max_workers=args.jobs) as ex:
            results = sorted(ex.map(run_one, jobs), key=lambda r: r[0])
    for idx, new, old in results:
        src = progs[idx][0]
        counts[new[0]] = counts.get(new[0], 0) + 1
        if new[0] == "refused":
            reasons[new[1]] = reasons.get(new[1], 0) + 1
        if new[0] in ("bad", "wrong"):
            bad.append((idx, "new build: " + new[0], new[1], src))
        if old:
            if old[0] == "ok" and new[0] != "ok":
                bad.append((idx, "REGRESSION", f"baseline correct, now {new[0]}: {new[1]}", src))
            if old[0] == new[0] and old[2] == new[2]:
                key = "identical"
            elif old[0] == "ok" and new[0] == "ok":
                key = "changed, still correct"
            elif new[0] == "ok":
                key = f"now correct (baseline {old[0]})"
            elif old[0] == "ok":
                key = f"regressed to {new[0]}"
            else:
                key = f"baseline {old[0]} -> {new[0]}"
            cmp[key] = cmp.get(key, 0) + 1

    n = args.count
    print(f"fuzz_c: {n} programs, seed {args.seed}"
          f"{', scalars only' if args.no_arrays else ''}"
          f"{', file scope' if args.scope else ''}")
    print(f"  correct {counts.get('ok', 0)}, refused {counts.get('refused', 0)}, "
          f"WRONG {counts.get('wrong', 0)}, BAD {counts.get('bad', 0)}")
    if args.baseline:
        print("  vs baseline: " + ", ".join(f"{v} {k}" for k, v in sorted(cmp.items())))
    if args.reasons:
        for k, v in sorted(reasons.items(), key=lambda kv: -kv[1]):
            print(f"  {v:6d}  {k}")
    if bad:
        keep = Path(args.keep) if args.keep else Path(tempfile.mkdtemp(prefix="fuzz_c.bad."))
        keep.mkdir(parents=True, exist_ok=True)
        for idx, kind, detail, src in bad:
            (keep / f"p{idx}.c").write_text(src)
            (keep / f"p{idx}.txt").write_text(f"{kind}\n{detail}\n")
        print(f"  {len(bad)} problem(s) - programs saved in {keep}:")
        for idx, kind, detail, _ in bad[:10]:
            print(f"    p{idx}: {kind}: {detail[:150]}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
