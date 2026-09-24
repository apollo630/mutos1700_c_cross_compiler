# `tests/mutos_cc/fuzz/` - semantic and differential fuzzing

The golden corpus proves that `mutos_c0`/`mutos_c1` reproduce the real
compiler byte for byte on 62 programs. It cannot show that code outside
those programs *computes the right thing*: a whole construct can be
missing from the corpus (no corpus file has a constant left operand -
`mutos_c0` compiled `7 - x` as `x - 7` from its first commit,
2026-09-14, until 2026-09-24), and wrong code that
assembles looks just like right code. These two scripts check that
directly, on random programs:

- `fuzz_c.py` generates random K&R programs inside `mutos_c0`'s grammar,
  computes what each one must do under C semantics on a 16-bit `int`,
  runs it through the real pipeline (`mutos_cpp -P`, `mutos_c0`,
  `mutos_c1`, `mutos_as`) and executes the result.
- `x86sim.py` is the executor: an interpreter for the subset of 8086
  code `mutos_c1` emits for a call-free `main()`. It reports `main()`'s
  return value and every local's final value (found through `c1`'s own
  `| _name=-N.` frame comments). Anything outside its subset - a call, a
  byte operation, a branch on flags not set by a `cmp` - stops it with an
  error, never a guess.

`x86sim.py` is validated on real hardware-compiled code: every
`tests/mutos_cc` `.s.golden` it can execute (30 of the 62; the others
call functions or use byte operations) returns the value its C source
computes, including struct and bit-field programs `mutos_c1` cannot
produce yet.

Nothing here is part of the corpus: the scripts write only to a
temporary directory, and `run_goldens.sh`, `gen_mutos.sh` and the
corpus Makefiles only ever look at `*.c` files.

## Usage

```
make fuzz                                    # from the repo root: 500 programs, seed 1
make fuzz FUZZ_ARGS="-n 2000 -s 7"
tests/mutos_cc/fuzz/fuzz_c.py -n 1000 -s 3 --reasons
tests/mutos_cc/fuzz/fuzz_c.py --baseline /tmp/base   # compare with an earlier build
```

| Option | Meaning |
|---|---|
| `-n N`, `-s SEED` | number of programs, random seed (same seed = same programs) |
| `-j JOBS` | parallel jobs (default: CPU count) |
| `--baseline DIR` | also run the `mutos_c0`/`mutos_c1` found in `DIR` (an earlier build) and compare |
| `--no-arrays` | scalars only |
| `--reasons` | list every refusal reason with its count - a map of what `c1` does not support yet |
| `--keep DIR` | where to save problem programs (default: a new directory under `/tmp`) |
| `--side-effects-in-conditionals`, `--postfix-in-conditions` | re-enable constructs that hit two known `mutos_c1` bugs (below) |

The tools are taken from `src/` (build first, `make`), or from
`$MUTOS_CPP`/`$MUTOS_C0`/`$MUTOS_C1`/`$MUTOS_AS` - e.g. sanitizer builds.

## What counts as a problem

Each program ends in one of: **correct**; **refused** - `mutos_c0` or
`mutos_c1` stopped with an explicit "not yet supported" diagnostic, which
is the compiler working as designed; **WRONG** - it compiled, but some
variable at some point, or the return value, differs from C semantics;
or **BAD** - a crash, a sanitizer report or an `internal:` diagnostic,
output `mutos_as` rejects, or output `x86sim.py` cannot execute.

The check is not only on the final state: the generator puts a marker
statement `m = 101;`, `m = 102;`, ... before every top-level statement,
and `x86sim.py` snapshots all variables at each marker store, so a wrong
value that a later statement overwrites is still caught.

With `--baseline`, every program also runs through the earlier build,
and its outcome there is compared: "now correct (baseline wrong)" is a
fix, "regressed to refused/wrong" a regression. Exit status is 1 on any
WRONG, BAD or regression, 0 otherwise.

**Read a "regression" before believing it.** A baseline result can be
right by luck - most often a wrong value that is only truth-tested
(`if (7 - x)` is non-zero exactly when `x - 7` is) or that happens to
coincide (`0 - a` and `a - 0` when `a` is 0). When `mutos_c0` stopped
reversing constant left operands, 6000 programs showed 4 such
"regressions": three were exactly these masked wrong results, now
refused because `c1` has no confirmed shape for `const - expression`,
and one was a constant `?:` condition, which `c0` now emits as v7's real
`QUEST` tree and `c1` refuses (see `docs/DEVLOG.md`).

## What is generated, and why it is shaped this way

`main()` with up to three 2-D `int` arrays (2 or 4 by 2 or 4), sometimes
a 1-D one, and scalars: `i`, `j`, `k` (0 or 1, never reassigned - the only
subscripts, so every access is in bounds); `x`, `y`, `s`, `t` (ordinary);
`u` (written only by a comma item `(u = e1, e2)`); `n` (touched only by
`++`/`--`); `m` (the statement marker). Statements are assignments to a
scalar or an array element and `if`/`else`; expressions use `+ - * / % &
| ^ << >> < <= > >= == != && || ! ~ ?:`, comma lists, `++`/`--`, and
products of two 2-D elements (the evaluation-order case). At most one side
effect (`u` or `n`) per statement, and neither variable is read
otherwise, so no expected value depends on C's unspecified operand order.
A program that would divide by zero is dropped.

Some choices only raise the share of programs `c1` accepts (a single
refused statement discards the whole program): no constant in a truth
context (`if (4)`, `x && 3`), no constant divisor or constant multiplier
of 0 or a power of two, no `x = y;`, a right operand that is usually
simple (`c1` cannot spill yet - two compound operands mostly collide in
its registers), and frames of at most 80 bytes (81-127 is `c1`'s
unconfirmed `chkstk` gap). About 30% of programs compile with arrays,
about 60% without.

## Known `mutos_c1` bugs the default avoids

Both are recorded in `STATUS.md`'s open items, with repros:

1. **Side effects in an operand C does not evaluate are executed.**
   `z = x ? y++ : 4;` and `z = a && b++;` increment `y`/`b` even when that
   operand is skipped (a value-context `?:`/`&&`/`||` computes its operands
   before its branches). `--side-effects-in-conditionals` generates these.
2. **A postfix `++`/`--` in an `if`/`while` condition happens only on the
   true path.** `c1` defers a postfix increment to the statement's `EXPR`
   opcode, and a condition ends in `CBRANCH` instead, so the increment
   lands at the next `EXPR` - inside the branch taken when the condition
   holds (`while (n-- > 3)` loses the last decrement).
   `--postfix-in-conditions` generates these.

Once one is fixed, drop its avoidance here (search `fuzz_c.py` for
"known c1 bug") so the default run covers it.

## Typical use while changing the compiler

```
mkdir -p /tmp/base && cp src/mutos_cc/mutos_c0 src/mutos_cc/mutos_c1 /tmp/base/
# ... change the compiler, make ...
tests/mutos_cc/fuzz/fuzz_c.py -n 3000 -s 1 --baseline /tmp/base
```

Every difference is then classified by the semantic check - no
knowledge of the change is needed. `make test` stays the byte-exactness
check against real hardware; this is the complement for everything the
corpus does not contain.
