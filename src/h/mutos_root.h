/*
 * mutos_root.h
 *
 * The MUTOS root: where the cross tools find the files an original
 * MUTOS 1700 tool names by an absolute path (docs/TOOLCHAIN_PLAN.md,
 * decisions 2 and 3).
 *
 * One directory mirrors the MUTOS file system, and every absolute MUTOS
 * path is looked up under it: cc's "/lib/crt0.o" is "<root>/lib/crt0.o",
 * the "/lib/libc.a" that ld makes of "-lc" is "<root>/lib/libc.a", cpp's
 * "/usr/include" is "<root>/usr/include", cc's "/lib/c0" is
 * "<root>/lib/c0" (in the build tree's root a symbolic link to
 * mutos_c0). The root is the environment variable MUTOS_ROOT when it is
 * set and not empty, otherwise MUTOS_ROOT_DEFAULT, compiled in at build
 * time: the absolute path of the build tree's mutos_root/, which the
 * Makefiles pass as -DMUTOS_ROOT_DEFAULT='"<path>"' (after moving the
 * tree: rebuild, or set MUTOS_ROOT). The host's own /lib and /usr/lib
 * are never used - on some hosts /usr/lib/libc.a is the host's C
 * library. There is no command-line option for it, so the tools keep
 * the originals' command lines.
 *
 * Header only (static inline functions), so that every tool can include
 * it without a library of its own.
 */

#ifndef MUTOS_ROOT_H
#define MUTOS_ROOT_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifndef MUTOS_ROOT_DEFAULT
#error "MUTOS_ROOT_DEFAULT is not defined - build with the Makefiles, which pass -DMUTOS_ROOT_DEFAULT='\"<build tree>/mutos_root\"'"
#endif

/* The environment variable that overrides the compiled-in default. */
#define MUTOS_ROOT_ENV "MUTOS_ROOT"

/*
 * mutos_root - the MUTOS root directory: $MUTOS_ROOT if it is set and
 * not empty, else the compiled-in MUTOS_ROOT_DEFAULT.
 */
static inline const char *mutos_root(void)
{
    const char *r = getenv(MUTOS_ROOT_ENV);
    return (r != NULL && r[0] != '\0') ? r : MUTOS_ROOT_DEFAULT;
}

/*
 * mutos_path - the host path of the MUTOS path mpath, written to buf
 * (n bytes):
 *   - an absolute MUTOS path is put under the root - "/lib/crt0.o"
 *     becomes "<root>/lib/crt0.o"; the root's trailing slashes are
 *     dropped, so a root of "/" maps every path to itself;
 *   - a relative path is copied unchanged: it names a file relative to
 *     the current directory, on MUTOS as on the host.
 * Returns buf, or NULL - with buf holding "" when n > 0 - if the result
 * does not fit into n bytes.
 */
static inline char *mutos_path(char *buf, size_t n, const char *mpath)
{
    const char *r = "";
    size_t rl = 0;
    size_t ml = strlen(mpath);

    if (mpath[0] == '/') {
        r = mutos_root();
        rl = strlen(r);
        while (rl > 0 && r[rl - 1] == '/')
            rl--;
    }
    if (n == 0)
        return NULL;
    if (rl + ml + 1 > n) {
        buf[0] = '\0';
        return NULL;
    }
    memcpy(buf, r, rl);
    memcpy(buf + rl, mpath, ml + 1);
    return buf;
}

#endif /* MUTOS_ROOT_H */
