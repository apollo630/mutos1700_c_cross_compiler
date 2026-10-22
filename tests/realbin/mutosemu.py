#!/usr/bin/env python3
"""mutosemu.py - run the real MUTOS 1700 binaries under an 8086 emulator.

The original toolchain binaries live in ../../mutos_bin/ as base64 text
(<name>.golden.base64.txt: as, c0, c1, c2, cc, cpp, ld - stripped 0411 or
0407 a.out files, see that directory). This module loads one of them -
or any other MUTOS a.out - into Unicorn and runs it the way the MUTOS
kernel would, far enough for the compiler passes and the assembler:

  * 0411 (separate I&D): text in its own 64K code segment, data+bss in a
    second segment that is also the stack segment; 0407: one segment.
  * crt0's view of the initial stack: argc, argv[], NULL, envp NULL, the
    strings above them at the top of the data segment (see
    docs/MUTOS_C_ABI.md, section 2.2).
  * The two far-call trampolines crt0 sets up (data words Isgadr/Iscadr,
    offsets 4 and 2 in the code segment - docs/MUTOS_C_ABI.md 2.2):
    CS:0002 is the system call (number in AX, arguments in BX, CX, SI,
    DI as sys123.o/lseek.o load them, result in AX and BX, carry set on
    error with the error number in AX), CS:0004 grows the stack (chkstk:
    the request is granted as asked).

Unicorn executes x86 code as an 80386; where that differs from the 8086
in what these binaries compute - a shift or rotate by CL of 32 or more,
a signed division whose quotient is -32768 - the instructions get hooks
that do what the 8086 (and the V30) does (Emu._hook_8086()).

System calls served: exit, fork, read, write, open, close, wait, creat,
link, unlink, exece (and exec), chmod, break, stat, lseek, getpid,
fstat, access, kill, signal, ioctl (always "not a tty"), umask.
Signals: signal() records the handler (libc.a's per-signal trampoline);
kill() of the process itself delivers the signal as v7's kernel does -
a caught one through an interrupt frame into its trampoline, the handler
then reset to the default; a default one kills the process (run()
returns 128 + the signal number, emu.killed holds the signal); an
ignored one does nothing.
Anything else is answered with EINVAL and a warning on stderr.

fork, exece and wait are faked, for "cc" (and later "make"): fork()
saves the process - registers, its segments, its open files - and
continues as the child (fork() returns 0 there). The child's exece()
asks the emulator's on_exec(emu, path, argv, envp, pid) what the
program would do: None if there is no such file (exece() fails with
ENOENT and the child goes on, as on MUTOS), -errno for another failure
(ENOEXEC: not an a.out), else the child's wait() status word (exit
status << 8, or a signal number). The default only
checks that the file exists and answers "exited 0"; cctrace.py's runs
the program itself. Then, or at the child's exit(), the saved process
is restored and its fork() returns the child's process number; wait()
returns the children in the order they ended. Every exece() is listed
in emu.execs. Only one process runs at a time, so a child's exece()
returns to its parent before the parent goes on - as a parent that
waits at once sees it.

stat/fstat fill v7's 30-byte struct stat (sys/stat.h: the longs high
word first, docs/MUTOS_C_ABI.md 1.6) from the host's; chmod, link and
creat act on the host's files; umask is the emulated process's own,
starting from the host's, and applies to the mode creat gives a new
file. The errors of these calls are the host's error numbers, which are
v7's up to 34 (EINVAL beyond).

Validation: run over every golden in tests/mutos_cc, the real c1 gives
each .s.golden byte for byte (c1_errors.txt's messages and the round
logs' included) and the real c0 each .1/.2 golden - see check_real.py
and README.md.

Usage:
    mutosemu.py [--cwd DIR] [--root DIR] [--max-insns N] [--pid N] TOOL [ARG...]
        TOOL is a name in mutos_bin/ (c1, c0, cpp, as, ...) or a path to
        an a.out file. Relative file names the program opens are resolved
        against --cwd (default: the current directory), absolute ones
        (/usr/include/stdio.h) under --root if given, else as they are.
        getpid() is the host's (or --pid's), so the real as's scratch
        file /tmp/AS<pid> differs between runs side by side. The program's
        stdout/stderr go to ours; the exit status is the program's - 125
        (and a "mutosemu:" message) if it stopped without an exit, ran
        past --max-insns or made the emulator fail.

As a module:
    emu = Emu(image_bytes, ['c1', 't1', 't2', 'out.s'], cwd=dir)
    emu.hook_text(0x0492, fn)      # fn(emu) before that text offset runs
    status = emu.run()

Requires the Python module "unicorn" (pip install unicorn).
"""
import base64
import io
import os
import struct
import sys

from unicorn import Uc, UcError, UC_ARCH_X86, UC_MODE_16, UC_HOOK_CODE
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX,
                               UC_X86_REG_DX, UC_X86_REG_SI, UC_X86_REG_DI,
                               UC_X86_REG_SP, UC_X86_REG_BP, UC_X86_REG_IP,
                               UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES,
                               UC_X86_REG_SS, UC_X86_REG_EFLAGS)

HERE = os.path.dirname(os.path.abspath(__file__))
BIN_DIR = os.path.normpath(os.path.join(HERE, '..', '..', 'mutos_bin'))

CSEG = 0x1000          # paragraph of the code segment
DSEG = 0x2000          # paragraph of the data/stack segment
CARRY = 0x0001
SYSCALL_OFF = 0x0002   # Iscadr's offset (crt0.o data word 6)
STKGROW_OFF = 0x0004   # Isgadr's offset (crt0.o data word 2)

# V7 error numbers used below
ENOENT, EBADF, ECHILD, EACCES, EINVAL, ESPIPE, ENOTTY = 2, 9, 10, 13, 22, 29, 25
# v7's error numbers 1..34 are the host's (Linux) 1..34 - EPERM .. ERANGE
V7_ERRNO_MAX = 34


def v7_errno(e):
    """The v7 error number for a host OSError."""
    return e.errno if e.errno and e.errno <= V7_ERRNO_MAX else EINVAL


def host_umask():
    """The host process's umask (read without changing it)."""
    m = os.umask(0o022)
    os.umask(m)
    return m

REG = {'ax': UC_X86_REG_AX, 'bx': UC_X86_REG_BX, 'cx': UC_X86_REG_CX,
       'dx': UC_X86_REG_DX, 'si': UC_X86_REG_SI, 'di': UC_X86_REG_DI,
       'sp': UC_X86_REG_SP, 'bp': UC_X86_REG_BP, 'ip': UC_X86_REG_IP,
       'cs': UC_X86_REG_CS, 'ds': UC_X86_REG_DS, 'ss': UC_X86_REG_SS}


def load_image(tool):
    """Return the a.out bytes of a mutos_bin tool name or of a file path."""
    if os.sep not in tool and not os.path.exists(tool):
        path = os.path.join(BIN_DIR, tool + '.golden.base64.txt')
        with open(path, 'rb') as f:
            return base64.b64decode(f.read())
    with open(tool, 'rb') as f:
        return f.read()


class _Exit(Exception):
    pass


def default_on_exec(emu, path, argv, envp, pid):
    """A faked exece(): the program is not run. ENOENT (None) if there is
    no such file, as MUTOS's exece; else the child "exits 0"."""
    if not os.path.isfile(emu._path(path)):
        return None
    return 0


class Emu:
    """One process: a loaded a.out image, its arguments and open files."""

    def __init__(self, image, args, cwd='.', stdin=None, stdout=None,
                 stderr=None, root=None, eof_limit=None, pid=None, fds=None,
                 on_exec=None, umask=None, envp=()):
        magic, tsize, dsize, bsize = struct.unpack_from('<4H', image, 0)
        if magic not in (0o407, 0o411):
            raise ValueError('not a 0407/0411 a.out (magic %o)' % magic)
        self.magic = magic
        self.tsize = tsize
        self.text = image[16:16 + tsize]
        self.data = image[16 + tsize:16 + tsize + dsize]
        self.cwd = cwd
        self.root = root
        # eof_limit: stop the run after that many reads in a row that
        # return nothing (a program reading past the end of a file for
        # ever - c0 at some ends of file; each read is a system call
        # here, so --max-insns would take minutes to stop it)
        self.eof_limit = eof_limit
        self.eof_reads = 0
        self.status = None
        self.insns = 0
        uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, 0x100000)
        if magic == 0o411:
            self.cbase, self.dbase = CSEG << 4, DSEG << 4
            uc.mem_write(self.cbase, self.text)
            uc.mem_write(self.dbase, self.data)
            dseg = DSEG
            self.brk = dsize + bsize
        else:
            self.cbase = self.dbase = CSEG << 4
            uc.mem_write(self.cbase, self.text + self.data)
            dseg = CSEG
            self.brk = tsize + dsize + bsize
        self.uc = uc
        uc.reg_write(UC_X86_REG_CS, CSEG)
        for r in (UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS):
            uc.reg_write(r, dseg)
        # argument and environment strings at the top of the data
        # segment, then the words crt0 reads: argc, argv[0..], NULL,
        # envp[0..], NULL
        top = 0xFFF0
        ptrs = []
        for a in list(args) + list(envp):
            s = a.encode('latin1') + b'\0'
            top -= len(s)
            uc.mem_write(self.dbase + top, s)
            ptrs.append(top)
        top &= ~1
        words = ([len(args)] + ptrs[:len(args)] + [0]
                 + ptrs[len(args):] + [0])
        sp = top - 2 * len(words)
        uc.mem_write(self.dbase + sp, struct.pack('<%dH' % len(words), *words))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_IP, 0)
        if fds is not None:
            # inherited open files (a faked exece): used, never closed
            self.fds = dict(fds)
        else:
            self.fds = {0: stdin if stdin is not None else sys.stdin.buffer,
                        1: stdout if stdout is not None else sys.stdout.buffer,
                        2: stderr if stderr is not None else sys.stderr.buffer}
        self.owned = set()
        self.text_hooks = {}
        self.handlers = {}
        self.killed = None
        self.pid = pid if pid is not None else (os.getpid() & 0x7FFF) or 1
        self.umask = host_umask() if umask is None else umask
        # fork/exece/wait (faked, see the module's docstring)
        self.on_exec = on_exec if on_exec is not None else default_on_exec
        self.forks = []         # saved parents, innermost last
        self.zombies = []       # (pid, wait status) of ended children
        self.execs = []         # every exece: (pid, path, argv, envp)
        self.next_pid = self.pid
        uc.hook_add(UC_HOOK_CODE, self._trampoline,
                    begin=self.cbase + SYSCALL_OFF,
                    end=self.cbase + STKGROW_OFF)
        self._hook_8086()

    # -- register and memory access, offsets within the data segment ----
    def reg(self, name):
        return self.uc.reg_read(REG[name]) & 0xFFFF

    def set_reg(self, name, value):
        self.uc.reg_write(REG[name], value & 0xFFFF)

    def word(self, off):
        return struct.unpack('<H', self.uc.mem_read(self.dbase + (off & 0xFFFF), 2))[0]

    def byte(self, off):
        return self.uc.mem_read(self.dbase + (off & 0xFFFF), 1)[0]

    def cstr(self, off):
        out = bytearray()
        while True:
            c = self.byte(off + len(out))
            if c == 0:
                return out.decode('latin1')
            out.append(c)

    def hook_text(self, off, fn):
        """Call fn(emu) each time the instruction at text offset off is
        about to run (analysis hooks: function entries, return points)."""
        self.uc.hook_add(UC_HOOK_CODE, lambda uc, a, s, u: fn(self),
                         begin=self.cbase + off, end=self.cbase + off)

    # -- the 8086 where Unicorn's 80386 differs ---------------------------
    def _hook_8086(self):
        """Unicorn executes x86 code as an 80386 does. Two of its
        differences from the 8086 (and its clone K1810WM86,
        docs/MUTOS1700_Assembler_as.pdf) and the V30 change what the
        MUTOS binaries compute, so the instructions concerned get code
        hooks that do what the 8086 does (a hook on an opcode byte inside
        another instruction never fires - it never starts an instruction):

        * a shift or rotate by CL (D2/D3) uses CL's low five bits on the
          80386 (and the 80186), all of CL on the 8086 and the V30 ("temp
          <- CL, while temp != 0" in the V20/V30 manual; the AP-186 note:
          "the 8086 does not do this") - 1 << 40 is 256 under Unicorn, 0
          on the hardware. _shift_cl() does nothing for CL < 32 and does
          the 8086's shift for a register operand; a memory operand with
          CL >= 32 is not modelled and stops the run;
        * a signed division (F6/F7 /7) whose quotient is -32768 (-128 for a
          byte) is a divide error on the 8086 (AP-186: "whenever the
          absolute value of the quotient is greater than 7FFFH") and the
          V30 (its manual: the most negative quotient is -32,767), not on
          the 80186 and the 80386 - -32768 / 1. _idiv() stops the run with
          a divide error there, as Unicorn does for the other faults.

        Most such instructions cannot reach those cases and run often (c0
        and c1 run some 20,000 shifts per kernel file): a shift right
        after "mov cx,<n>" or "mov cl,<n>" with n < 32, and "cwd / mov
        cx,<n> / idiv cx" with |n| >= 2, get no hook - unless something
        could jump straight to the instruction: a relative jump or call in
        the text with it as target, or a word in the text or data equal to
        its offset (a jump table). The test may keep a hook needlessly,
        never drop one wrongly."""
        t = self.text
        shifts = [i for i in range(len(t) - 1)
                  if t[i] in (0xD2, 0xD3) and (t[i + 1] >> 3) & 7 != 6]
        idivs = [i for i in range(len(t) - 1)
                 if t[i] in (0xF6, 0xF7) and (t[i + 1] >> 3) & 7 == 7]
        if not shifts and not idivs:
            return
        targets = set()
        for i, b in enumerate(t):
            if (0x70 <= b <= 0x7F or 0xE0 <= b <= 0xE3 or b == 0xEB) \
                    and i + 1 < len(t):
                targets.add((i + 2 + struct.unpack_from('<b', t, i + 1)[0])
                            & 0xFFFF)
            elif b in (0xE8, 0xE9) and i + 2 < len(t):
                targets.add((i + 3 + struct.unpack_from('<H', t, i + 1)[0])
                            & 0xFFFF)
        for blob in (t, self.data):
            for i in range(len(blob) - 1):
                targets.add(struct.unpack_from('<H', blob, i)[0])
        for i in shifts:
            safe = ((i >= 3 and t[i - 3] == 0xB9
                     and struct.unpack_from('<H', t, i - 2)[0] < 32)
                    or (i >= 2 and t[i - 2] == 0xB1 and t[i - 1] < 32))
            if not (safe and i not in targets):
                self.uc.hook_add(UC_HOOK_CODE, self._shift_cl,
                                 begin=self.cbase + i, end=self.cbase + i)
        for i in idivs:
            safe = (i >= 4 and t[i - 4] == 0x99 and t[i - 3] == 0xB9
                    and t[i] == 0xF7 and t[i + 1] == 0xF9
                    and abs(struct.unpack_from('<h', t, i - 2)[0]) >= 2
                    and i - 3 not in targets)
            if not (safe and i not in targets):
                self.uc.hook_add(UC_HOOK_CODE, self._idiv,
                                 begin=self.cbase + i, end=self.cbase + i)

    def _ea(self, off):
        """The data-segment offset of the memory operand of the instruction
        whose ModRM byte is at text offset off + 1 (16-bit addressing; DS,
        SS and ES are one segment in these binaries)."""
        modrm = self.text[off + 1]
        mod, rm = modrm >> 6, modrm & 7
        if mod == 0 and rm == 6:
            return struct.unpack_from('<H', self.text, off + 2)[0]
        r = self.reg
        base = (r('bx') + r('si'), r('bx') + r('di'), r('bp') + r('si'),
                r('bp') + r('di'), r('si'), r('di'), r('bp'), r('bx'))[rm]
        disp = (0 if mod == 0 else
                struct.unpack_from('<b', self.text, off + 2)[0] if mod == 1
                else struct.unpack_from('<H', self.text, off + 2)[0])
        return (base + disp) & 0xFFFF

    def _idiv(self, uc, addr, size, user):
        off = addr - self.cbase
        op, modrm = self.text[off], self.text[off + 1]
        rm = modrm & 7
        word = op & 1
        if modrm >> 6 == 3:
            if word:
                d = self.reg(self._REG16[rm])
            else:
                w = self.reg(self._REG16[rm & 3])
                d = (w >> 8) & 0xFF if rm & 4 else w & 0xFF
        else:
            a = self._ea(off)
            d = self.word(a) if word else self.byte(a)
        if word:
            divisor = d - 0x10000 if d & 0x8000 else d
            n = (self.reg('dx') << 16) | self.reg('ax')
            dividend = n - (1 << 32) if n & 0x80000000 else n
            least = -32768
        else:
            divisor = d - 0x100 if d & 0x80 else d
            n = self.reg('ax')
            dividend = n - 0x10000 if n & 0x8000 else n
            least = -128
        if divisor == 0:
            return                          # Unicorn faults itself
        q = abs(dividend) // abs(divisor)
        if (dividend < 0) != (divisor < 0):
            q = -q
        if q == least:
            uc.emu_stop()
            raise RuntimeError('emulation stopped: an 8086 divide error at '
                               '%04x:%04x (quotient %d - the 80386 Unicorn '
                               'emulates does not fault there)'
                               % (self.reg('cs'), off, q))

    _REG16 = ('ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di')

    def _shift_cl(self, uc, addr, size, user):
        n = self.reg('cx') & 0xFF
        if n < 32:
            return
        off = addr - self.cbase
        op, modrm = self.text[off], self.text[off + 1]
        kind, rm = (modrm >> 3) & 7, modrm & 7
        if modrm >> 6 != 3:
            uc.emu_stop()
            raise RuntimeError('a shift of memory by CL = %d at %04x (the '
                               '8086 uses all of CL, the emulator its low '
                               'five bits; not modelled)' % (n, off))
        bits = 16 if op & 1 else 8
        if bits == 16:
            name = self._REG16[rm]
            v = self.reg(name)
        else:
            name = self._REG16[rm & 3]
            w = self.reg(name)
            v = (w >> 8) & 0xFF if rm & 4 else w & 0xFF
        mask, top = (1 << bits) - 1, 1 << (bits - 1)
        fl = uc.reg_read(UC_X86_REG_EFLAGS)
        cf = fl & 1
        for _ in range(n):                  # one bit at a time, as the 8086
            last = v
            if kind in (4, 0, 2):           # shl, rol, rcl
                out = 1 if v & top else 0
                v = ((v << 1) & mask) | (cf if kind == 2 else
                                          out if kind == 0 else 0)
            else:                           # shr, sar, ror, rcr
                out = v & 1
                v = (v >> 1) | (top if (kind == 7 and v & top)
                                or (kind == 1 and out)
                                or (kind == 3 and cf) else 0)
            cf = out
        if kind in (4, 0, 2):
            of = (1 if v & top else 0) ^ cf
        elif kind == 5:
            of = 1 if last & top else 0
        elif kind == 7:
            of = 0
        else:
            of = (1 if v & top else 0) ^ (1 if v & (top >> 1) else 0)
        fl = (fl & ~0x0801) | cf | (of << 11)
        if kind >= 4:                       # shifts set SF, ZF, PF
            fl &= ~0x00C4
            fl |= (0x80 if v & top else 0) | (0x40 if v == 0 else 0)
            fl |= 0x04 if bin(v & 0xFF).count('1') % 2 == 0 else 0
        uc.reg_write(UC_X86_REG_EFLAGS, fl)
        if bits == 16:
            self.set_reg(name, v)
        else:
            w = self.reg(name)
            self.set_reg(name, (w & 0x00FF) | (v << 8) if rm & 4
                         else (w & 0xFF00) | v)
        self.set_reg('ip', off + 2)

    # -- the kernel side ----------------------------------------------
    def _return_far(self, ax, bx=None, error=False):
        self.set_reg('ax', ax)
        if bx is not None:
            self.set_reg('bx', bx)
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS)
        self.uc.reg_write(UC_X86_REG_EFLAGS,
                          (fl | CARRY) if error else (fl & ~CARRY))
        sp = self.reg('sp')
        ss = self.reg('ss') << 4
        ip, cs = struct.unpack('<HH', self.uc.mem_read(ss + sp, 4))
        self.set_reg('sp', sp + 4)
        self.uc.reg_write(UC_X86_REG_CS, cs)
        self.uc.reg_write(UC_X86_REG_IP, ip)

    def _trampoline(self, uc, addr, size, user):
        off = addr - self.cbase
        if off == SYSCALL_OFF:
            self._syscall()
        elif off == STKGROW_OFF:
            self._return_far(self.reg('bx'))

    def _deliver(self, sig):
        """kill() of the process itself: v7's delivery of signal sig."""
        h = self.handlers.get(sig, 0)
        if h == 1:                                   # ignored
            return self._return_far(0)
        if h == 0:                                   # default: killed
            if self.forks:                           # a child: ends
                return self._child_end(sig)
            self.killed = sig
            self.status = 128 + sig
            self.uc.emu_stop()
            raise _Exit()
        self._return_far(0)
        if sig not in (4, 5):                        # SIGILL, SIGTRAP stay
            self.handlers[sig] = 0
        sp = self.reg('sp') - 6
        fl = self.uc.reg_read(UC_X86_REG_EFLAGS) & 0xFFFF
        self.uc.mem_write((self.reg('ss') << 4) + sp,
                          struct.pack('<3H', self.reg('ip'), self.reg('cs'), fl))
        self.set_reg('sp', sp)
        self.uc.reg_write(UC_X86_REG_IP, h)

    def _path(self, name):
        if os.path.isabs(name):
            # an absolute MUTOS path (/usr/include/...) lives under root
            if self.root:
                return os.path.join(self.root, name.lstrip('/'))
            return name
        return os.path.join(self.cwd, name)

    def _newfd(self, f):
        fd = 0
        while fd in self.fds:
            fd += 1
        self.fds[fd] = f
        self.owned.add(fd)
        return fd

    # -- fork, exece, wait (faked) -------------------------------------
    def _segments(self):
        """(address, size) of the memory a process may change: the data
        segment for 0411; for 0407 the one segment above the text (the
        text itself is compared, not rewritten - see _child_end())."""
        if self.magic == 0o411:
            return self.dbase, 0x10000
        return self.cbase + self.tsize, 0x10000 - self.tsize

    def _fork(self):
        """fork(): save the parent, continue as the child."""
        addr, size = self._segments()
        self.next_pid += 1
        child = self.next_pid
        self.forks.append({
            'context': self.uc.context_save(),
            'memory': bytes(self.uc.mem_read(addr, size)),
            'text': (bytes(self.uc.mem_read(self.cbase, self.tsize))
                     if self.magic == 0o407 else None),
            'fds': dict(self.fds), 'owned': set(self.owned),
            'brk': self.brk, 'handlers': dict(self.handlers),
            'umask': self.umask, 'eof_reads': self.eof_reads,
            'pid': self.pid, 'child': child})
        parent = self.pid
        self.pid = child
        # the child shares the parent's open files: it may close them
        # (only its own entry goes), never the host file under them
        self.owned = set()
        return self._return_far(parent, 0)          # BX = 0: the child

    def _child_end(self, status):
        """The child ended (exit, a fatal signal, or a faked exece) with
        wait() status word status: restore the parent, whose fork()
        returns the child's number."""
        frame = self.forks.pop()
        for fd in self.owned:                       # files the child opened
            try:
                self.fds[fd].close()
            except (OSError, ValueError, KeyError):
                pass
        for fd in frame['fds']:
            try:
                frame['fds'][fd].flush()
            except (OSError, ValueError, AttributeError):
                pass
        self.uc.context_restore(frame['context'])
        addr, _ = self._segments()
        self.uc.mem_write(addr, frame['memory'])
        if frame['text'] is not None and \
                bytes(self.uc.mem_read(self.cbase, self.tsize)) != frame['text']:
            self.uc.mem_write(self.cbase, frame['text'])
        self.fds, self.owned = frame['fds'], frame['owned']
        self.brk, self.handlers = frame['brk'], frame['handlers']
        self.umask, self.eof_reads = frame['umask'], frame['eof_reads']
        self.pid = frame['pid']
        self.zombies.append((frame['child'], status & 0xFFFF))
        return self._return_far(frame['child'], 1)  # BX != 0: the parent

    def _strings(self, off):
        """The strings of a NULL-terminated pointer array at off."""
        out = []
        while off:
            p = self.word(off)
            if p == 0:
                return out
            out.append(self.cstr(p))
            off += 2
        return out

    def _exece(self, bx, cx, si):
        path = self.cstr(bx)
        argv = self._strings(cx)
        envp = self._strings(si)
        self.execs.append((self.pid, path, argv, envp))
        status = self.on_exec(self, path, argv, envp, self.pid)
        if status is None:
            return self._return_far(ENOENT, error=True)
        if status < 0:                               # -errno: exece fails
            return self._return_far(-status, error=True)
        if not self.forks:
            # exece in the first process: it is replaced - end the run
            # with the program's status, as a shell would see it
            self.status = (status >> 8) & 0xFF if status & 0xFF == 0 \
                else 128 + (status & 0x7F)
            self.uc.emu_stop()
            raise _Exit()
        return self._child_end(status)

    def _wait(self):
        if not self.zombies:
            return self._return_far(ECHILD, error=True)
        pid, status = self.zombies.pop(0)
        return self._return_far(pid, status)

    # -- stat ---------------------------------------------------------
    def _stat_bytes(self, st):
        """v7's struct stat (30 bytes) for a host os.stat_result; the
        longs (st_size and the times) high word first."""
        def lng(v):
            v = int(v) & 0xFFFFFFFF
            return struct.pack('<HH', v >> 16, v & 0xFFFF)
        return (struct.pack('<7H', st.st_dev & 0xFFFF, st.st_ino & 0xFFFF,
                            st.st_mode & 0xFFFF, min(st.st_nlink, 0x7FFF),
                            st.st_uid & 0xFFFF, st.st_gid & 0xFFFF,
                            getattr(st, 'st_rdev', 0) & 0xFFFF)
                + lng(st.st_size) + lng(st.st_atime) + lng(st.st_mtime)
                + lng(st.st_ctime))

    def _fstat_of(self, f):
        """The host stat of an open file - for an in-memory one (a test's
        captured stdout), a regular file of its present size, mode 0, the
        way v7 shows a pipe."""
        try:
            return os.fstat(f.fileno())
        except (AttributeError, OSError, ValueError, io.UnsupportedOperation):
            size = len(f.getvalue()) if hasattr(f, 'getvalue') else 0
            return os.stat_result((0o100000, 0, 0, 1, 0, 0, size, 0, 0, 0))

    def _syscall(self):
        n = self.reg('ax')
        bx, cx, si, di = (self.reg('bx'), self.reg('cx'), self.reg('si'),
                          self.reg('di'))
        ret = self._return_far
        if n == 1:                                   # exit(status)
            if self.forks:                           # a child: ends
                return self._child_end((bx & 0xFF) << 8)
            self.status = bx & 0xFF
            self.uc.emu_stop()
            raise _Exit()
        if n == 2:                                   # fork()
            return self._fork()
        if n == 7:                                   # wait()
            return self._wait()
        if n in (11, 59):                            # exec(name, argv),
            return self._exece(bx, cx, si if n == 59 else 0)  # exece(.., envp)
        if n == 9:                                   # link(name1, name2)
            try:
                os.link(self._path(self.cstr(bx)), self._path(self.cstr(cx)))
            except OSError as e:
                return ret(v7_errno(e), error=True)
            return ret(0)
        if n == 15:                                  # chmod(name, mode)
            try:
                os.chmod(self._path(self.cstr(bx)), cx & 0o7777)
            except OSError as e:
                return ret(v7_errno(e), error=True)
            return ret(0)
        if n == 18:                                  # stat(name, buf)
            try:
                st = os.stat(self._path(self.cstr(bx)))
            except OSError as e:
                return ret(v7_errno(e), error=True)
            self.uc.mem_write(self.dbase + cx, self._stat_bytes(st))
            return ret(0)
        if n == 28:                                  # fstat(fd, buf)
            f = self.fds.get(bx)
            if f is None:
                return ret(EBADF, error=True)
            try:
                f.flush()
            except (OSError, ValueError, AttributeError):
                pass
            self.uc.mem_write(self.dbase + cx,
                              self._stat_bytes(self._fstat_of(f)))
            return ret(0)
        if n == 60:                                  # umask(mask)
            old, self.umask = self.umask, bx & 0o777
            return ret(old)
        if n == 3:                                   # read(fd, buf, n)
            f = self.fds.get(bx)
            if f is None:
                return ret(EBADF, error=True)
            data = f.read(si) or b''
            self.uc.mem_write(self.dbase + cx, data)
            if data:
                self.eof_reads = 0
            else:
                self.eof_reads += 1
                if self.eof_limit and self.eof_reads > self.eof_limit:
                    self.status = None
                    self.eof_stop = True
                    self.uc.emu_stop()
                    raise _Exit()
            return ret(len(data))
        if n == 4:                                   # write(fd, buf, n)
            f = self.fds.get(bx)
            if f is None:
                return ret(EBADF, error=True)
            f.write(bytes(self.uc.mem_read(self.dbase + cx, si)))
            return ret(si)
        if n == 5:                                   # open(name, mode)
            try:
                f = open(self._path(self.cstr(bx)), 'rb' if cx == 0 else 'r+b')
            except OSError:
                return ret(ENOENT, error=True)
            return ret(self._newfd(f))
        if n == 6:                                   # close(fd)
            f = self.fds.pop(bx, None)
            if f is None:
                return ret(EBADF, error=True)
            if bx in self.owned:
                self.owned.discard(bx)
                f.close()
            else:
                f.flush()
            return ret(0)
        if n == 8:                                   # creat(name, mode)
            path = self._path(self.cstr(bx))
            new = not os.path.exists(path)
            try:
                f = open(path, 'w+b')
            except OSError:
                return ret(EACCES, error=True)
            if new:                                  # a new file: the mode
                try:                                 # asked, less the umask
                    os.chmod(path, cx & 0o7777 & ~self.umask)
                except OSError:
                    pass
            return ret(self._newfd(f))
        if n == 10:                                  # unlink(name)
            try:
                os.unlink(self._path(self.cstr(bx)))
            except OSError:
                return ret(ENOENT, error=True)
            return ret(0)
        if n == 17:                                  # break(addr)
            self.brk = bx
            return ret(0)
        if n == 19:                                  # lseek(fd, hi:lo, whence)
            f = self.fds.get(bx)
            if f is None:
                return ret(EBADF, error=True)
            off = (cx << 16) | si
            if off & 0x80000000:
                off -= 1 << 32
            try:
                pos = f.seek(off, di)
            except (OSError, ValueError, io.UnsupportedOperation):
                return ret(ESPIPE, error=True)
            return ret((pos >> 16) & 0xFFFF, pos & 0xFFFF)
        if n == 20:                                  # getpid: the host's,
            # so that scratch names built from it (the real as's
            # /tmp/AS<pid>) differ between runs side by side
            return ret(self.pid)
        if n == 33:                                  # access(name, mode)
            if os.path.exists(self._path(self.cstr(bx))):
                return ret(0)
            return ret(ENOENT, error=True)
        if n == 48:                                  # signal(sig, func)
            old = self.handlers.get(bx, 0)
            self.handlers[bx] = cx
            return ret(old)
        if n == 37:                                  # kill(pid, sig)
            if cx and bx in (0, self.pid):
                return self._deliver(cx)
            return ret(0)
        if n == 54:                                  # ioctl: not a tty
            return ret(ENOTTY, error=True)
        sys.stderr.write('mutosemu: system call %d not served\n' % n)
        return ret(EINVAL, error=True)

    def run(self, max_insns=0):
        """Run to exit(); return the exit status."""
        try:
            try:
                self.uc.emu_start(self.cbase, self.cbase + 0x10000,
                                  count=max_insns)
            except _Exit:
                pass
            except UcError as e:
                raise RuntimeError('emulation stopped: %s at %04x:%04x'
                                   % (e, self.reg('cs'), self.reg('ip')))
        finally:
            # what the program write()s is written, as by the kernel, also
            # when a fault or a limit stops it: the host files are
            # buffered, so they are closed (or flushed) in any case
            for fd in list(self.fds):
                try:
                    if fd in self.owned:
                        self.fds[fd].close()
                    else:
                        self.fds[fd].flush()
                except (OSError, ValueError):
                    pass
        if self.status is None:
            if getattr(self, 'eof_stop', False):
                raise RuntimeError('program reads past the end of a file for '
                                   'ever (%d reads of nothing)' % self.eof_limit)
            raise RuntimeError('program stopped without exit at %04x'
                               % self.reg('ip'))
        return self.status


def run_tool(tool, args, cwd='.', stdin=b'', max_insns=0, root=None,
             eof_limit=None, pid=None, on_exec=None):
    """Run a tool with captured output: (status, stdout, stderr)."""
    out, err = io.BytesIO(), io.BytesIO()
    emu = Emu(load_image(tool), [os.path.basename(tool)] + list(args),
              cwd=cwd, stdin=io.BytesIO(stdin), stdout=out, stderr=err,
              root=root, eof_limit=eof_limit, pid=pid, on_exec=on_exec)
    st = emu.run(max_insns)
    return st, out.getvalue(), err.getvalue()


def main(argv):
    cwd = '.'
    root = None
    max_insns = 0
    pid = None
    while argv and argv[0].startswith('--'):
        if argv[0] == '--cwd':
            cwd = argv[1]
            argv = argv[2:]
        elif argv[0] == '--root':
            root = argv[1]
            argv = argv[2:]
        elif argv[0] == '--max-insns':
            max_insns = int(argv[1])
            argv = argv[2:]
        elif argv[0] == '--pid':
            pid = int(argv[1])
            argv = argv[2:]
        else:
            sys.exit('mutosemu: unknown option %s' % argv[0])
    if not argv:
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    tool = argv[0]
    emu = Emu(load_image(tool), [os.path.basename(tool)] + argv[1:], cwd=cwd,
              root=root, pid=pid)
    try:
        status = emu.run(max_insns)
    except RuntimeError as e:
        sys.stdout.flush()
        sys.stderr.write('mutosemu: %s\n' % e)
        return 125
    sys.stdout.flush()
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
