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

System calls served: exit, read, write, open, close, creat, unlink,
break, lseek, getpid, access, kill, signal, ioctl (always "not a tty").
Anything else is answered with EINVAL and a warning on stderr. There is
no fork/exec, so "cc" itself cannot run; its passes (cpp, c0, c1, as)
are run one by one instead, as cc would run them.

Validation: run over every golden in tests/mutos_cc, the real c1 gives
each .s.golden byte for byte (c1_errors.txt's messages and the round
logs' included) and the real c0 each .1/.2 golden - see check_real.py
and README.md.

Usage:
    mutosemu.py [--cwd DIR] [--root DIR] [--max-insns N] TOOL [ARG...]
        TOOL is a name in mutos_bin/ (c1, c0, cpp, as, ...) or a path to
        an a.out file. Relative file names the program opens are resolved
        against --cwd (default: the current directory), absolute ones
        (/usr/include/stdio.h) under --root if given, else as they are.
        getpid() is the host's, so the real as's scratch file /tmp/AS<pid>
        differs between runs side by side. The program's
        stdout/stderr go to ours; the exit status is the program's.

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
ENOENT, EBADF, EACCES, EINVAL, ESPIPE, ENOTTY = 2, 9, 13, 22, 29, 25

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


class Emu:
    """One process: a loaded a.out image, its arguments and open files."""

    def __init__(self, image, args, cwd='.', stdin=None, stdout=None,
                 stderr=None, root=None):
        magic, tsize, dsize, bsize = struct.unpack_from('<4H', image, 0)
        if magic not in (0o407, 0o411):
            raise ValueError('not a 0407/0411 a.out (magic %o)' % magic)
        self.text = image[16:16 + tsize]
        self.data = image[16 + tsize:16 + tsize + dsize]
        self.cwd = cwd
        self.root = root
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
        # argument strings at the top of the data segment, then the
        # words crt0 reads: argc, argv[0..], NULL, envp NULL
        top = 0xFFF0
        ptrs = []
        for a in args:
            s = a.encode('latin1') + b'\0'
            top -= len(s)
            uc.mem_write(self.dbase + top, s)
            ptrs.append(top)
        top &= ~1
        words = [len(args)] + ptrs + [0, 0]
        sp = top - 2 * len(words)
        uc.mem_write(self.dbase + sp, struct.pack('<%dH' % len(words), *words))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_IP, 0)
        self.fds = {0: stdin if stdin is not None else sys.stdin.buffer,
                    1: stdout if stdout is not None else sys.stdout.buffer,
                    2: stderr if stderr is not None else sys.stderr.buffer}
        self.owned = set()
        self.text_hooks = {}
        uc.hook_add(UC_HOOK_CODE, self._trampoline,
                    begin=self.cbase + SYSCALL_OFF,
                    end=self.cbase + STKGROW_OFF)

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

    def _syscall(self):
        n = self.reg('ax')
        bx, cx, si, di = (self.reg('bx'), self.reg('cx'), self.reg('si'),
                          self.reg('di'))
        ret = self._return_far
        if n == 1:                                   # exit(status)
            self.status = bx & 0xFF
            self.uc.emu_stop()
            raise _Exit()
        if n == 3:                                   # read(fd, buf, n)
            f = self.fds.get(bx)
            if f is None:
                return ret(EBADF, error=True)
            data = f.read(si) or b''
            self.uc.mem_write(self.dbase + cx, data)
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
            try:
                f = open(self._path(self.cstr(bx)), 'w+b')
            except OSError:
                return ret(EACCES, error=True)
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
            return ret((os.getpid() & 0x7FFF) or 1)
        if n == 33:                                  # access(name, mode)
            if os.path.exists(self._path(self.cstr(bx))):
                return ret(0)
            return ret(ENOENT, error=True)
        if n in (37, 48):                            # kill, signal
            return ret(0)
        if n == 54:                                  # ioctl: not a tty
            return ret(ENOTTY, error=True)
        sys.stderr.write('mutosemu: system call %d not served\n' % n)
        return ret(EINVAL, error=True)

    def run(self, max_insns=0):
        """Run to exit(); return the exit status."""
        try:
            self.uc.emu_start(self.cbase, self.cbase + 0x10000,
                              count=max_insns)
        except _Exit:
            pass
        except UcError as e:
            raise RuntimeError('emulation stopped: %s at %04x:%04x'
                               % (e, self.reg('cs'), self.reg('ip')))
        for fd in list(self.fds):
            try:
                if fd in self.owned:
                    self.fds[fd].close()
                else:
                    self.fds[fd].flush()
            except (OSError, ValueError):
                pass
        if self.status is None:
            raise RuntimeError('program stopped without exit at %04x'
                               % self.reg('ip'))
        return self.status


def run_tool(tool, args, cwd='.', stdin=b'', max_insns=0, root=None):
    """Run a tool with captured output: (status, stdout, stderr)."""
    out, err = io.BytesIO(), io.BytesIO()
    emu = Emu(load_image(tool), [os.path.basename(tool)] + list(args),
              cwd=cwd, stdin=io.BytesIO(stdin), stdout=out, stderr=err,
              root=root)
    st = emu.run(max_insns)
    return st, out.getvalue(), err.getvalue()


def main(argv):
    cwd = '.'
    root = None
    max_insns = 0
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
        else:
            sys.exit('mutosemu: unknown option %s' % argv[0])
    if not argv:
        sys.exit(__doc__.split('Usage:')[1].split('As a module')[0])
    tool = argv[0]
    emu = Emu(load_image(tool), [os.path.basename(tool)] + argv[1:], cwd=cwd,
              root=root)
    status = emu.run(max_insns)
    sys.stdout.flush()
    return status


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
