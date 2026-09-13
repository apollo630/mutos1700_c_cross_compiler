/*
 * 09_abiprobe/01_argvmain.c
 *
 * main() declared with its real crt0-supplied parameters, per
 * MUTOS_C_ABI.md sect. 2.2/2.3 (argc, argv, envp are all pushed by
 * crt0 regardless of whether main's own signature mentions them).
 */
int strlen();

main(argc, argv)
int argc;
char *argv[];
{
	if (argc > 1)
		return strlen(argv[1]);
	return 0;
}
