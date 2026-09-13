/*
 * 10_integ/01_wordcount.c
 *
 * A small integration test: scan a fixed in-memory buffer (rather
 * than stdin, so this stays deterministic and self-contained) and
 * count characters, words and lines.  Exercises char arrays, nested
 * control flow and several local counters together.
 */
char text[] = "the quick brown fox\njumps over the lazy dog\n";

main()
{
	int i, nchar, nword, nline, inword;

	nchar = 0;
	nword = 0;
	nline = 0;
	inword = 0;

	for (i = 0; text[i] != '\0'; i = i + 1) {
		nchar = nchar + 1;
		if (text[i] == '\n')
			nline = nline + 1;
		if (text[i] == ' ' || text[i] == '\n') {
			inword = 0;
		} else if (inword == 0) {
			inword = 1;
			nword = nword + 1;
		}
	}
	return nchar + nword + nline;
}
