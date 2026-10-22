/*
 * rootpath_test.c - src/h/mutos_root.h's lookup, for check_root.py.
 *
 * Usage: rootpath_test [-n SIZE] [MUTOS-PATH ...]
 *
 * Prints mutos_root() on the first line, then, for each argument, the
 * host path mutos_path() gives for it into a buffer of SIZE bytes
 * (default 4096) - or "(does not fit)" where it returns NULL, followed
 * by "(buffer not empty)" if it left anything in the buffer.
 * check_root.py compiles it with the -DMUTOS_ROOT_DEFAULT the Makefiles
 * use and runs it with MUTOS_ROOT unset, empty and set.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mutos_root.h"

int main(int argc, char **argv)
{
    size_t size = 4096;
    int i = 1;

    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        size = (size_t)strtoul(argv[2], NULL, 10);
        i = 3;
    }
    char *buf = malloc(size ? size : 1);
    if (buf == NULL) {
        perror("rootpath_test");
        return 1;
    }
    printf("%s\n", mutos_root());
    for (; i < argc; i++) {
        if (size > 0)
            memset(buf, 'x', size);
        if (mutos_path(buf, size, argv[i]) != NULL)
            printf("%s\n", buf);
        else if (size > 0 && buf[0] != '\0')
            printf("(does not fit)(buffer not empty)\n");
        else
            printf("(does not fit)\n");
    }
    free(buf);
    return 0;
}
