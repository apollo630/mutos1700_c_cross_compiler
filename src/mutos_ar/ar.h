/*
 * ar.h - the MUTOS 1700 archiver, ported from the original binary.
 *
 * This is v7's ar (v7/ar/ar.c) as the original MUTOS 1700 binary has it
 * (mutos/mutos_bin/ar.golden.base64.txt, 0407, text 8,930 bytes, stripped
 * of its symbols). The port follows the method of the c0, c1, cpp and ld
 * ports (docs/C0_PORT_PLAN.md): the binary is the specification, v7's
 * source the skeleton. tests/realbin/armap.txt maps every function to its
 * text offset in the binary, and each function's comment in ar.c names
 * what the binary does differently from v7 - which is next to nothing:
 * the binary is v7's ar.c, compiled for the 8086 and linked with the
 * MUTOS libc.a, and the port is its host C.
 *
 * HOST REPRESENTATION. Unlike cpp, c0 and ld, ar needs no image of the
 * original's data segment: it keeps its state in globals and small
 * buffers it never runs past (the 512-byte copy buffer is read into
 * with at most 512 bytes, the 16-byte name holds the 14 bytes of an
 * archive's name and a zero, the flag table is indexed by a letter the
 * option switch has accepted). What the 16-bit original has that the
 * host's C does not is laid out here explicitly:
 *
 *   - the archive header, struct ar_hdr, is kept as the 26 bytes it
 *     is in the file, arbuf[]: name[14], then date, uid, gid, mode and
 *     size at the offsets below - the longs as the original keeps them in
 *     memory and in the archive, the high word first, each word
 *     little-endian ("PDP-11 middle-endian", CLAUDE.md, Core Requirement
 *     5);
 *   - the original's chars are signed (uid and gid print as negative
 *     numbers from 128 on, the option counters wrap at 128 and 256), its
 *     ints 16 bits (only where it matters: the copy loop's lengths are
 *     at most 512);
 *   - struct stat is v7's: the size and the times are longs of 32 bits,
 *     uid, gid and mode 16 bits - the host's wider values are cut to
 *     them, as the emulator's fstat() does and as the 8086 kernel's
 *     would give them.
 *
 * What the port does not model is refused (artrap()): an opened
 * directory - the original reads its entries, which the host's file
 * system does not have in the format the original sees.
 *
 * The temporary files "/tmp/vXXXXX" and the names derived from them are
 * paths of the MUTOS file system: they are looked up under the MUTOS root
 * ($MUTOS_ROOT, src/h/mutos_root.h; docs/TOOLCHAIN_PLAN.md, decision 2);
 * the names on the command line are the host's.
 */
#ifndef MUTOS_AR_AR_H
#define MUTOS_AR_AR_H

#include <stddef.h>
#include <stdint.h>

/* ---- the archive format (ar.h of MUTOS 1700, v7's) ------------------ */

#define	ARMAG		0177545		/* the magic number: an int, 65 ff */
#define	AR_HDR		26		/* sizeof(struct ar_hdr) */
#define	AR_NAME		0		/* char ar_name[14] */
#define	AR_DATE		14		/* long ar_date */
#define	AR_UID		18		/* char ar_uid */
#define	AR_GID		19		/* char ar_gid */
#define	AR_MODE		20		/* int ar_mode */
#define	AR_SIZE		22		/* long ar_size */

/* a v7 long as it lies in memory and in the file: high word first */
int32_t	ar_getlong(const uint8_t *p);
void	ar_putlong(uint8_t *p, int32_t v);

/* ---- arlib.c: what libc.a does for the original ---------------------- */

/* a refusal: the original depends on state the port does not model */
#if defined(__GNUC__)
#define	NORETURN __attribute__((noreturn))
#else
#define	NORETURN
#endif
NORETURN void	artrap(const char *what);

/* called by artrap() before it exits: ar removes its temporary files */
extern void	(*ar_trapcleanup)(void);

/* libc.a's mktemp(): the trailing X's become the process number's
 * digits, the first of them 'a', 'b', ... as long as the name exists;
 * "/" when even 'y' exists. as is a MUTOS path, changed in place. */
char	*ar_mktemp(char *as);

/* libc.a's perror(): "s: <message for errno>\n", four write()s to 2 */
void	ar_perror(const char *s, int err);

/* v7's stdio asks isatty(1) when the first character goes to stdout; libc.a's
 * isatty() is an ioctl() that fails with ENOTTY on a file or a pipe, which
 * leaves errno at 25 - the number wrerr()'s perror() prints after a write that
 * came back short. ar_out() does that once, before the first output to stdout */
void	ar_out(void);

/* libc.a's ctime() as printf(" %-12.12s %-4.4s ", cp+4, cp+20) uses it:
 * the month, day, hour and minute, then the year, of the time t in the
 * time zone the host has (UTC when TZ says so) */
void	ar_ctime(int32_t t, char *mon12, char *year4);

/* the host path of a MUTOS path (an absolute one under the MUTOS root) */
const char *ar_mpath(const char *mpath);

#endif /* MUTOS_AR_AR_H */
