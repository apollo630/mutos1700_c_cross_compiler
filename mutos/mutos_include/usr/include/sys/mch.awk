BEGIN		{ incl = 0; }
/^# 1 /		{ print "|" $0; incl++; }
		{ if (incl == 1 && NR > 1) print; }
/^# [^1]/	{ incl--; }
/^# 1[^ ]/	{ incl--; }
