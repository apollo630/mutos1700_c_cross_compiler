# MUTOS 1700 C Calling Convention & Program Startup/Cleanup

**Status:** Research reference for Milestone 4 (`mutos_cc`/`mutos_c0`/`mutos_c1`).
Not yet implemented anywhere — this document records what the *real* MUTOS 1700
toolchain's compiled code and C runtime actually do, reverse-engineered from real
hardware-linked object files, so `mutos_c1`'s code generator can reproduce it
byte-compatibly. Everything below is either **directly confirmed** by disassembling
real `.o` files (own subsections say so explicitly) or **directly quoted** from real
`.s` source retained by the kernel build. Nothing here is guessed from generic 8086
C-compiler folklore — every rule has a cited example.

## Evidence sources

- `tests/mutos1700_crt0/crt0.o` — the real linked C runtime startup object.
- `tests/mutos1700_libc/*.o` (167 files) — the real linked library, containing both
  **compiler-generated** functions (the vast majority — anything with a leading `_`
  and a `push bp / mov bp,sp / push di / push si` opening) and a smaller set of
  **hand-written assembly** leaf/syscall stubs (see "What is *not* part of this
  convention" below) — the two are easy to tell apart and must not be confused.
- `tests/mutos_as/kernel_opt/mch.s` — real, `c2`-optimized kernel `.s` source
  (retained via the modified `conf/Makefile`'s `-S` flag), which — uniquely among the
  evidence used here — contains the **literal compiler-generated source text**, not
  just its disassembly. This is where `cret`'s own definition, and two real
  C-function prologues (`_co`, `_ci`), come from directly.

Disassembly in this document was produced with
`objdump -D -b binary -m i386 -M intel,i8086` against the raw `text` segment
extracted from each `.o`'s real MUTOS `a.out` header (16-byte header, then `text`,
`data`, `trel`, `drel`, `symtab` — see `src/h/mutos_aout.h`). Relocation entries were
decoded per the project's already-established rules (bit15 = 1-byte shift flag, bits
4–14 = symbol index, low 4 bits = relocation type) to identify call/jump targets by
symbol name.

---

## 1. Function calling convention

### 1.1 Argument passing: stack only, right-to-left, caller cleans up

**There is no register-based argument passing in this ABI.** Every call passes every
argument on the stack, regardless of type. Registers are used *inside* a function
purely as an implementation detail (loading a parameter out of the stack frame into a
register to work with it) — that is never visible to the caller.

- Arguments are pushed **right-to-left** (the last-declared parameter is pushed
  first), so the **first-declared parameter ends up at the lowest stack offset**,
  closest to the return address.
- The **caller** removes the arguments after the call (classic cdecl-style
  caller-cleanup), via `add sp,N` where `N = 2 × (number of argument words)`.
  Confirmed at every single call site examined, with no exceptions:
  - `crt0.o`: `call _main` / `add sp,0x6` (3-word argc/argv/envp call).
  - `sprintf.o`: `call __doprnt` / `add sp,0x6` (3-word call), and
    `call __flsbuf` / `add sp,0x4` (2-word call) in the same function.
  - `chkstk.o`'s overflow path: `push bx / push ax / call _kill` (2-word call, though
    this particular call site never returns so cleanup is moot there).
- Confirmed multi-word example (`sprintf.o`, real disassembly):
  ```
  lea   di,[bp-0xc]      ; &charcount local
  push  di
  lea   di,[bp+0x8]      ; &fmt (2nd sprintf parameter, taken by address)
  push  di
  push  WORD PTR [bp+0x6]; s (1st sprintf parameter, re-pushed by value)
  call  __doprnt
  add   sp,0x6            ; caller pops all 3 words
  ```
- Taking the address of a local or of a parameter for a call (e.g. to pass a `char *`
  into a var-args-style helper) is done with a plain `lea reg,[bp±N]` followed by
  `push reg` — nothing implicit or hidden here.

### 1.2 Standard function prologue/epilogue (confirmed, unconditional, fixed)

Every real compiler-generated C function examined — from the tiniest one-liner
(`_abs`, `_ci`) to the largest (`execvp.o`'s 646-byte-frame function) — opens with
the **exact same four instructions**, in this exact order, and closes with a tail
jump into one shared epilogue routine:

```
push  bp
mov   bp,sp
push  di
push  si
    ...
    jmp   cret        ; epilogue (tail jump, NOT call+ret)
```

`cret` itself (quoted verbatim from `tests/mutos_as/kernel_opt/mch.s`, and confirmed
byte-identical in every `cret.o`/inline disassembly checked):

```
|***
|	cret -- exit from C-funciton.
|***
	.globl	cret
cret:
	lea	sp,#-4(bp)
	pop	si
	pop	di
	pop	bp
	ret
```

i.e. in `mutos_as` intermediate/x86 terms: `lea sp,[bp-4]` / `pop si` / `pop di` /
`pop bp` / `ret`. Because `lea sp,[bp-4]` computes the new `sp` **relative to `bp`**
rather than incrementally, it discards *any* amount of local-variable space
(`sub sp,N` for any `N`) in one instruction — this is exactly why `cret` can be a
single shared routine reachable by a plain `jmp` from every function regardless of
how many locals it declared.

**This prologue/epilogue is emitted completely unconditionally — regardless of
whether the function actually uses `di`/`si`, has any locals, or has any
parameters.** Confirmed directly: `_ci` (`mch.s`, zero parameters, uses neither `di`
nor `si` internally) still opens with `push bp / mov bp,sp / push di / push si` and
closes with `jmp cret`; `_abs`, `_strlen` likewise. **`mutos_c1`'s non-optimizing
code generator should replicate this unconditionally, with no
leaf-function/register-usage-based elision** — there is no evidence anywhere in the
corpus (including in `kernel_opt`, i.e. `c2`-optimized kernel code) that the
optimizer ever removes an unused saved register from this fixed frame. That kind of
elision, if wanted at all, is out of scope for Milestone 4 and would belong to
Milestone 5's `c2` work — and even there, no real corpus evidence currently supports
it being a real `c2` behavior.

### 1.3 Parameter stack offsets

With the standard prologue above, `bp+0` = saved caller's `bp`, `bp+2` = return
address (**2 bytes — this is a `near` call/return convention; every C-to-C call
examined uses `E8`/`C3`, never a far call**), so:

| Offset | Contents |
|---|---|
| `bp+0` | saved caller `bp` |
| `bp+2` | return address (near, 2 bytes) |
| `bp+4` | 1st parameter |
| `bp+6` | 2nd parameter |
| `bp+8` | 3rd parameter |
| `bp+0xa` | 4th parameter |
| … | +2 per subsequent word-sized parameter |

Confirmed directly and repeatedly:
- `_abs(n)`: `n` at `bp+4` — `cmp WORD PTR [bp+0x4],0x0`.
- `_strcmp(s1,s2)`: `s1` at `bp+4` → `di`, `s2` at `bp+6` → `si`.
- `_strcpy(dst,src)`: `dst` at `bp+4`, `src` at `bp+6`.
- `_lseek(fd, offset, whence)`: `fd` at `bp+4`; `offset` (a `long`) occupies **two**
  words at `bp+6` and `bp+8`; `whence` at `bp+0xa`. This directly confirms a `long`
  parameter consumes exactly two consecutive parameter-slot words, with later
  parameters shifted accordingly (see §1.5 for word order within the pair).
- `_co(c)` (`mch.s`): `push *4.(bp)` — `c` at `bp+4`.

A parameter of any scalar type (`char`, `int`, pointer) occupies exactly one 16-bit
word in this scheme. No sub-word (1-byte) push was observed anywhere in the corpus —
consistent with (and presumably a direct consequence of) K&R default argument
promotion: `char`/`short` arguments are always passed as a full word.

### 1.4 Local variable offsets — fixed frame layout, not usage-dependent

Local variables live **below** `bp`, and — like the saved registers above — their
offsets follow a **fixed layout that reserves the first two local-sized slots for the
saved `di`/`si`, regardless of whether that particular function actually has any
locals there**:

| Offset | Contents |
|---|---|
| `bp-2` | saved `di` |
| `bp-4` | saved `si` |
| `bp-6` | 1st local word |
| `bp-8` | 2nd local word |
| `bp-0xa` | 3rd local word |
| … | −2 per subsequent local word |

Locals are reserved with `sub sp,N` immediately after the fixed
`push bp/mov bp,sp/push di/push si` prologue, where `N` is the total byte size of all
locals (`N` is a multiple of 2 in every example seen; a single local `char` still
occupies a full word — e.g. `sprintf.o`'s `mov BYTE PTR [bp-0x6],0x42` inside an
8-byte, i.e. 4-word, local block).

Confirmed via two independent multi-local examples:
- `atol.o`: `sub sp,4` (2 locals), used as `[bp-0x6]` and `[bp-0x8]` — the running
  `long` accumulator (see §1.5 for word order).
- `sprintf.o`: `sub sp,8` (4 locals), touching `[bp-0x6]` (a byte), `[bp-0xa]`
  (`0x7fff`, sprintf's internal "unlimited count"), and `[bp-0xc]` (a pointer) — i.e.
  offsets `-6, -8, -10, -12`, exactly the expected `-6, -8, ...` sequence for a
  4-word block.
- `strcpy.o`: `sub sp,2` (1 local, `[bp-0x6]`) used to save the original `dst`
  pointer so it can be returned unmodified at the end.

### 1.5 Return values

- **16-bit scalar (`int`, pointer, `char`)**: returned in `AX`. Confirmed in every
  non-`long`-returning function examined (`_abs`, `_strlen`, `_strcpy`, `_sprintf`,
  …) — the last thing before `jmp cret` is always a `mov ax,<value>` (or the value is
  already correctly in `ax`).
- **32-bit `long`**: returned in **`DX:AX`, with `DX` = high 16 bits, `AX` = low 16
  bits** — the same register pairing the 8086 `MUL`/`IMUL`/`DIV` instructions
  themselves use, and the same pairing used everywhere else in this ABI for a
  register-resident `long` (see §1.6). Directly confirmed in `_atol` (parses a
  decimal string into a `long`):
  ```
  mov   ax,WORD PTR [bp-0x6]   ; low word of the long accumulator
  mov   dx,WORD PTR [bp-0x8]   ; high word
  jmp   cret
  ```
  Also confirmed in `aldiv`/`ldiv` (compiler-internal, see §1.6): both end with
  `mov dx,<hi>` / `mov ax,<lo>` immediately before their own custom epilogue.

### 1.6 `long` (32-bit) layout and splitting

**Confirmed: a `long` is always represented as two consecutive 16-bit words, with the
HIGH-order word at the LOWER address/offset and the LOW-order word at the HIGHER
address/offset** — i.e. exactly the same PDP-11 "middle-endian" word order already
established project-wide for `ar` archive `long` fields, now directly confirmed to
also apply to compiled C `long` values (locals, register-pairs, and by-reference
arguments alike). This resolves `CLAUDE.md`'s open note that this is "not yet
relevant" until `mutos_cc` — it is now confirmed to be exactly the same rule.

Confirmed three independent ways:
1. **`long` locals** (`atol.o`): `[bp-0x6]` = low word, `[bp-0x8]` = high word — i.e.
   the lower address (`bp-8`) holds the high word, the higher address (`bp-6`) holds
   the low word.
2. **`long` in memory pointed to by a register** (`almul.o`/`aldiv.o`, operating on
   `*bx`): `[bx]` = high word, `[bx+2]` = low word.
3. **`long` parameter passed by value** (`_lseek`'s `offset`, at `bp+6`/`bp+8`):
   `bp+6` (lower offset, closer to return address ⇒ pushed *last*) is treated as the
   high word, `bp+8` (pushed *first*) as the low word — i.e. pushing a `long`
   argument is exactly equivalent to treating it as two ordinary sub-arguments in
   `(high, low)` declaration order and applying the ordinary right-to-left push rule
   from §1.1 to them: **push the low word first, then the high word**, which
   naturally reproduces the same "high word at the lower address" layout as
   everywhere else.

### 1.7 Register roles: caller-saved vs. callee-saved

| Register | Role |
|---|---|
| `AX`, `CX`, `DX` | Always caller-saved/scratch. Never preserved by the standard prologue; freely clobbered by every function body examined. |
| `BP`, `DI`, `SI` | Always callee-saved. Unconditionally pushed/popped by the standard prologue (§1.2), regardless of actual use. |
| `BX` | **Conditionally** callee-saved. In the overwhelming majority of functions `BX` is used as ordinary scratch, exactly like `AX`/`CX`/`DX`, with no save/restore (e.g. `_strcmp`, `_strlen`, `_sprintf` all clobber `bx` freely). A small number of functions — observed only in the compiler's own internal long-arithmetic runtime helpers, see §1.8 — use an *extended* prologue that also preserves `BX`. **From a caller's point of view, `BX` must be treated as not guaranteed to survive a call**, exactly like `AX`/`CX`/`DX`, unless the specific callee is known to use the extended form. |

### 1.8 A *different*, internal-only ABI: the long-arithmetic runtime helpers

`almul`/`aldiv`/`alrem` (long `*`, `/`, `%`) and their integer counterparts
`lmul`/`ldiv`/`lrem` (all **without** a leading `_` — i.e. these are
compiler/runtime-internal symbols, never user-callable C functions, and K&R-mangled
C names would never collide with them since a user C identifier always gets exactly
one leading `_`) use a **different, extended prologue** that is *not* the general
public C-function ABI from §1.2–1.7 — it is a private contract between `mutos_c1`'s
own code generator and its own runtime-support routines:

```
push  bp
push  bx        ; <- extra, BEFORE mov bp,sp
mov   bp,sp
push  di
push  si
    ...
    lea   sp,[bp-4]
    pop   si
    pop   di
    pop   bx      ; <- extra, matching the extra push bx
    pop   bp
    ret            ; NOT "jmp cret" -- cret doesn't know about the extra bx
```

Because `bx` is pushed *before* `mov bp,sp`, the frame is shifted by one word
relative to §1.3: `bp+0` = saved `bx`, `bp+2` = saved caller `bp`, `bp+4` = return
address, **`bp+6`** = 1st parameter (confirmed in both `almul.o` and `aldiv.o`/
`ldiv.o`/`lrem.o`).

Observed calling shape for these helpers — confirmed via `almul`/`aldiv`:
`helper(long *lvalue_and_result, <operand2, passed as two words per §1.6>)`
— i.e. the first parameter is a pointer to a `long` that serves as **both** one input
operand (read via `[bx]`/`[bx+2]`) **and** the destination the result is written back
into (in place), while the second operand is passed by value as an ordinary
two-word `long` per §1.6. This is presumably how `mutos_c1`'s expression evaluator
implements a `long`-typed intermediate/lvalue operation: materialize the left operand
at a known stack address, call the helper with `&lvalue` plus the right operand, and
read the (now-updated) `long` back out — or read the DX:AX result directly per §1.5,
both patterns are present (`aldiv`/`ldiv` end by loading `DX:AX` from the working
registers right before their custom epilogue, in addition to having written the
result back through the pointer).

**This convention is out of scope for how `mutos_cc` calls ordinary user C
functions** — it only matters for the small, fixed set of built-in helpers the code
generator itself must emit calls to for `long` multiply/divide/modulo (the 8086 has
no 32×32 hardware multiply/divide, so this software fallback is presumably needed
regardless of target CPU tier, including `-mv30`).

### 1.9 Large stack frames: the `chkstk` guard

When a function's local-variable frame is large, the code generator replaces the
plain `sub sp,N` from §1.4 with a call to a shared stack-overflow-checking helper
instead:

```
push  bp
mov   bp,sp
push  di
push  si
mov   ax,<framesize>     ; framesize in BYTES, as an immediate
call  chkstk
    ... (locals now addressable exactly as if "sub sp,framesize" had run)
```

`chkstk` (`chkstk.o`, real disassembly) computes `bx = sp - ax`, compares `bx`
against a local `Stkbot` limit; if the new `sp` would stay above the limit, it
**pops its own return address into `ax`, sets `sp = bx` (i.e. performs the frame
allocation itself), then does `jmp ax`** — this is why `call chkstk` behaves exactly
like an inline `sub sp,framesize` to the code that follows it, just with an added
guard. If the check fails, `chkstk` attempts to grow the stack via a far call through
a runtime-initialized function-pointer variable (`Isgadr`, see §2.3), retrying up to
`PROBESLO` (`256`) bytes at a time; if growth genuinely fails it calls
`_kill(_getpid(), SIGSEG)` then `__exit` — i.e. a real stack overflow terminates the
process with `SIGSEGV`(-equivalent), consistent with modern Unix behavior.

Empirically, in the real `libc.a` corpus: the **largest frame using plain
inline `sub sp,N`** is `N=76` bytes (`ttyname.o`); the **smallest frame observed
using `call chkstk`** is `N=256` bytes (`fstab.o`). No example in the corpus falls
between 76 and 256 bytes, so the exact cutoff the real `cc`/`c1` used is bounded to
that range but not pinned down further by this corpus. A conservative,
easy-to-justify choice for `mutos_c1` would be a round threshold inside that gap
(e.g. 128 bytes) pending a real example that narrows it further.

### 1.10 What is *not* part of this convention

Not every function-shaped `.o` in `libc.a` was produced by the compiler. A
substantial minority — mostly direct 1:1 syscall wrappers (`access.o`, `alarm.o`,
`chdir.o`, `chmod.o`, …) — are hand-written assembly, easily recognized because they
do **not** use the `push bp/mov bp,sp/push di/push si …/jmp cret` shape at all.
Two patterns seen:

- **Trivial syscall stubs**, e.g. `access.o` in full:
  ```
  mov  ax,0x21      ; syscall number (from a local ABS constant, ".access = 33")
  jmp  sys2a         ; shared N-argument syscall trap dispatcher
  ```
  A small family of shared dispatchers exists, apparently named by argument count —
  `sys1a` (confirmed, used by `_exit`'s raw-syscall form in `exit.o`) and `sys2a`
  (confirmed, used by `_access`) were both directly observed; the naming pattern
  suggests further members (`sys0a`, `sys3a`, …) but none beyond these two were
  directly confirmed in the corpus examined.
- **Flat-frame stubs that skip `bp` entirely**, e.g. `_lseek` (`lseek.o`): opens with
  `push bp / mov bp,sp / push si / push di` (note: **`si` before `di`** — the
  opposite order from §1.2!) and closes with its own inline `pop di / pop si / pop bp`
  — internally self-consistent, but deliberately *not* calling the shared `cret`
  (which would corrupt the stack if the push order didn't match `cret`'s fixed pop
  order). This is further confirmation that `cret`'s pop order permanently fixes the
  compiler's push order for any code that *does* use `cret` — hand-written code that
  doesn't call `cret` is free to differ, and evidently sometimes does.

None of this affects what `mutos_c1`'s code generator itself needs to emit — it only
matters when validating full `libc.a`-linked golden binaries, so that a hand-written
stub's differing internal register-save order isn't mistaken for a second valid
compiler convention.

---

## 2. Program startup (`crt0`) and cleanup

### 2.1 Text-segment layout

`crt0.o`'s `text` segment (182 bytes) is laid out exactly like every other linked
MUTOS executable's text segment start (already documented for `mutos_as`/`mutos_ld`):
a **fixed 128-byte reserved vector block**, then the real startup code.

```
offset 0   (start, EXT):  eb 7e         jmp   start0        ; jump over the vector block
offset 2:                 eb fe ×8      jmp   $  (×8)        ; trap vectors, self-loop
offset 0x12 .. 0x7f:       00 00 ...     (zero fill)
offset 0x80 (start0):     8b dc          mov   bx,sp
```

### 2.2 `start`/`start0`: building `main`'s argument list

Full annotated disassembly (symbols `start0`=0x80, `s1`=0x8c, `s2`=0xb0 from the real
symbol table):

```
start0:
    mov   bx,sp          ; bx = S, the SP value the kernel handed this process:
                          ;   [S]   = argc
                          ;   [S+2] = argv[0], argv[1], ..., argv[argc-1], NULL, envp[0], ...
    sub   sp,0x4          ; reserve 2 words for the 3rd/4th call-argument slots below
    add   bx,0x2           ; bx = S+2 = &argv[0]
    push  bx                ; push &argv[0]           -> stack: [.. , argv-ptr]
    push  WORD PTR [bx-2]   ; push [S] = argc          -> stack: [argc, argv-ptr, ..]
s1:
    add   bx,0x2
    test  WORD PTR [bx],0xffff   ; ZF set iff *bx == 0 (an all-ones AND-mask, functionally
                                   ; identical to "cmp WORD PTR [bx],0"; NOT a 0xFFFF sentinel)
    jne   s1                       ; loop until the argv[] NULL terminator is found
    add   bx,0x2                    ; bx now = &envp[0] (just past the NULL)
    mov   si,sp
    mov   WORD PTR [si+0x4],bx       ; fill the 3rd call-argument slot with envp
    mov   WORD PTR [0x0],bx           ; _environ = envp   (external, relocated)
    mov   ax,cs
    mov   WORD PTR [0x4],ax            ; crt0-local: high/segment half of Isgadr := cs
    mov   WORD PTR [0x8],ax            ; crt0-local: high/segment half of Iscadr := cs
    call  _main                         ; main(argc, argv, envp)
    add   sp,0x6                         ; caller cleans up all 3 argument words (§1.1)
s2:
    push  ax                              ; push main()'s return value
    call  _exit                            ; exit(status)  -- NOT the raw syscall, see §2.4
    jmp   s2                                ; safety net: retry forever if _exit ever returns
```

Notes:

- `argv[]` **is** conventionally NULL-terminated (a first read of the raw
  `f7 07 ff ff` / `test WORD PTR [bx],0xffff` bytes can look like a `0xFFFF`
  sentinel, but `TEST` performs a bitwise AND and sets `ZF` from the result — ANDing
  with an all-ones mask is a no-op, so this is exactly a zero-test, not a
  compare-against-`0xFFFF`. Verified by tracing the full loop and its exit
  condition.).
- `main` is called with **exactly the argument-passing convention of §1.1**: 3 words
  pushed (built here via an interleaved `sub sp,4`/`push`/`push`/write-back sequence
  rather than three literal `push` instructions, but the net stack shape handed to
  `_main` is identical to an ordinary 3-argument call), and the caller (`start0`)
  cleans up with `add sp,0x6` afterward exactly as in any other 3-argument call.
- The global `_environ` (`char **environ`) is set directly from the crt0-computed
  `envp` pointer, via an ordinary relocated absolute store — no separate library
  initialization call is involved.
- `Isgadr`/`Iscadr` are two module-local 4-byte (far-pointer-shaped) variables
  defined in `crt0.o`'s own data segment; `crt0` initializes only their
  **segment/high half** to `cs` (the process's single code=data=stack segment — every
  example in this ABI is a near/flat/tiny-model call, consistent with this). Their
  **offset/low half** is presumably fixed at build/load time. Several `libc.a`
  routines that must trap into the kernel (`chkstk`'s stack-growth path, `_lseek`,
  `_exit`'s raw form) reach it via an indirect **far** call through one of these two
  pointers (`FF 1E` ModRM, `CALL FAR [mem]`) — this is the process's syscall
  trampoline mechanism. Not part of the C-to-C ABI itself, included here only because
  it's visible directly in `crt0.o` and is otherwise easy to misread.
- `crt0.o`'s symbol table also contains an unreferenced local `ABS` symbol literally
  named `exit` with value `1` — confirmed (byte-for-byte symbol name check) **not**
  the same symbol as `exit.o`'s own local `.exit` (`ABS`, value `1`, dot-prefixed).
  It is not used by any relocation in `crt0.o` and appears to be a harmless unused
  leftover; not investigated further as it has no bearing on startup/cleanup
  behavior.

### 2.3 `main`'s signature and the near-call/tiny-memory-model assumption

Every call observed anywhere in this ABI — `call _main`, `call _exit`, every libc
inter-function call — is a plain near `E8 rel16` / `C3`/`jmp` pair. There is no
evidence anywhere in the corpus of a far call being used for an ordinary C function
call (far calls are reserved for the OS-trampoline mechanism in §2.2 and for the
hand-written `co`/`ci` firmware-monitor forwarding routines in `mch.s`, both
deliberately outside the C ABI). `mutos_c1` should therefore generate near calls
exclusively for ordinary C function calls, consistent with a single flat
code+data+stack segment (tiny/small memory model) for normal user processes.

### 2.4 Cleanup: `exit()` vs. `_exit()`, and the `_cleanup()` hook

MUTOS 1700's C library implements the classic two-tier Unix exit design, confirmed
directly by disassembling both halves:

- **`exit(status)`** — K&R-mangled asm symbol `_exit` (exactly what `crt0` calls
  after `main` returns, §2.2) — defined in `cuexit.o`:
  ```
  call  __cleanu     ; call _cleanup()  (mangled + 8-char-truncated, see below)
  mov   ax,0x1        ; syscall number 1 = exit
  jmp   sys1a           ; tail-jump into the shared 1-argument syscall trap dispatcher
  ```
  i.e. `exit()` **first runs a cleanup hook, then performs the raw exit syscall** —
  it never returns.
- **`_exit(status)`** (the raw, no-cleanup syscall) — K&R-mangled asm symbol
  `__exit` (double underscore: one from mangling any C name, one already present in
  the C source name `_exit`) — defined separately in `exit.o`:
  ```
  mov   bx,WORD PTR [sp+2]   ; status (no bp frame at all -- a hand-written leaf stub)
  mov   ax,0x1
  call  DWORD PTR [Iscadr]    ; far call through the syscall trampoline (§2.2)
  jmp   $                      ; safety net, should never be reached
  ```
- **`_cleanup()`** — K&R-mangled asm symbol `__cleanu` (from C source name
  `_cleanup`; `__cleanup` is 9 characters and gets **truncated to 8**, i.e.
  `__cleanu`, by the same K&R 8-character external-symbol-truncation rule already
  documented for `mutos_as`). Two competing definitions exist in the archive, a
  classic "avoid pulling in stdio for programs that don't use it" linker trick:
  - `fakcu.o` provides a trivial stub: `__cleanu: ret` (does nothing).
  - `flsbuf.o` (the real stdio buffer-flush implementation file) *also* defines a
    real `__cleanu` internally, which — being bundled in the same object as
    `_flsbuf`/friends — is only pulled into the link if the program already uses
    stdio for some other reason. Whichever definition the linker's archive scan
    resolves first (per this project's already-documented `getfile()`/archive
    linear-scan behavior) is the one that ends up satisfying `cuexit.o`'s `__cleanu`
    reference; a program using no stdio at all gets `fakcu.o`'s no-op, a program
    using any stdio function gets the real flush routine "for free" via the same
    archive member it needed anyway.

For `mutos_cc`/Milestone 4 purposes, the actionable takeaway is: **`crt0` always
calls `exit()` (the cleanup-performing symbol `_exit`), never the raw
`_exit()`/`__exit`, after `main` returns** — this is what `mutos_cc`'s own crt0
(if the project ships one, vs. linking against the real `crt0.o`) needs to match, and
it's also the reason a MUTOS C program's `main()` returning normally correctly
flushes stdio even without explicit `atexit`/`fclose` calls, exactly like classic
Unix `exit()`.

---

## 3. Summary checklist for `mutos_c1` code generation

- [ ] Every function: `push bp / mov bp,sp / push di / push si` prologue,
      unconditionally (§1.2).
- [ ] Parameters at `bp+4, bp+6, bp+8, ...`; locals at `bp-6, bp-8, bp-10, ...`
      (§1.3–1.4).
- [ ] Arguments pushed right-to-left; caller cleans up with `add sp,N` after every
      call (§1.1).
- [ ] Return: `AX` for scalars, `DX:AX` (`DX`=high) for `long` (§1.5).
- [ ] `long` = 2 words, high word at the lower address/offset, everywhere (locals,
      by-reference, by-value parameters) (§1.6).
- [ ] Epilogue: `jmp cret` (shared routine: `lea sp,[bp-4] / pop si / pop di /
      pop bp / ret`) (§1.2).
- [ ] Large frames: `mov ax,framesize / call chkstk` instead of inline `sub sp,N`
      above some threshold in `(76, 256]` bytes, exact cutoff not yet pinned down
      (§1.9).
- [ ] Long multiply/divide/modulo: emit calls to `almul`/`aldiv`/`alrem` using their
      *own* extended, pointer-first calling convention (§1.8) — not the general ABI.
- [ ] `crt0` (own object, real or reimplemented) must: read `argc`/`argv` off the
      initial `sp`, scan for the NULL `argv` terminator, set `_environ`, call `_main`
      with `(argc, argv, envp)` per the standard ABI, then call `exit()` (not raw
      `_exit()`) with `main`'s return value, with an infinite-loop safety net after
      the call (§2.2, §2.4).
