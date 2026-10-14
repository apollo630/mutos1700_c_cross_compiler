/*
 * c1tab.c - the real MUTOS 1700 c1's tables, read from the original binary
 * (mutos_bin/c1.golden.base64.txt) by tests/realbin/c1tables.py --c.
 * GENERATED - do not edit; "make check-realbin" checks it is up to date.
 *
 * The code tables (regtab, efftab, cctab, sptab) are v7's struct table
 * lists of struct optab rows, as v7's cvopt compiled them from a table
 * source in v7's notation with MUTOS assembler text - readable in
 * tests/realbin/c1_tables.txt. Every template string is the binary's,
 * byte for byte (a 0200 bit asks cexpr() for a tab). Each list and
 * string is named by its data-segment offset in the binary.
 */
#include "c1.h"

char maprel[] = {60, 61, 64, 65, 62, 63, 68, 69, 66, 67};
char notrel[] = {61, 60, 65, 64, 63, 62, 69, 68, 67, 66};
char *regname[8] = {"ax", "dx", "si", "di", "cx", "bx", "bp", "sp"};

/* xlongrel()'s branches for a long relation (v7's lrtab) */
char lrtab[2][3][6] = {
	{{0, 61, 63, 63, 65, 65}, {61, 0, 65, 65, 63, 63}, {60, 61, 66, 67, 68, 69}},
	{{0, 61, 63, 63, 64, 65}, {61, 0, 65, 0, 0, 63}, {60, 61, 60, 0, 0, 61}},
};

int16_t opdope[124] = {
	0000000,	/*   0 op 0 */
	0000000,	/*   1 op 1 */
	0000000,	/*   2 op 2 */
	0000000,	/*   3 op 3 */
	0036000,	/*   4 op 4 */
	0002000,	/*   5 op 5 */
	0036000,	/*   6 op 6 */
	0002000,	/*   7 op 7 */
	0014201,	/*   8 : */
	0007001,	/*   9 , */
	0000000,	/*  10 field select */
	0000000,	/*  11 op 11 */
	0000001,	/*  12 op 12 */
	0000001,	/*  13 int->ptr */
	0000001,	/*  14 ptr->int */
	0000001,	/*  15 long->ptr */
	0000001,	/*  16 field assign */
	0000001,	/*  17 >> */
	0000001,	/*  18 >> */
	0000000,	/*  19 op 19 */
	0000400,	/*  20 name */
	0000400,	/*  21 short constant */
	0000400,	/*  22 string */
	0000400,	/*  23 float */
	0000400,	/*  24 double */
	0000400,	/*  25 long constant */
	0000400,	/*  26 long constant */
	0000400,	/*  27 *r++ */
	0000400,	/*  28 *--r */
	0000001,	/*  29 op 29 */
	0034203,	/*  30 ++pre */
	0034203,	/*  31 --pre */
	0034203,	/*  32 ++post */
	0034203,	/*  33 --post */
	0034220,	/*  34 !un */
	0034202,	/*  35 & */
	0034220,	/*  36 indirect */
	0034200,	/*  37 - */
	0034220,	/*  38 ~ */
	0036001,	/*  39 . */
	0030101,	/*  40 + */
	0030001,	/*  41 - */
	0032101,	/*  42 * */
	0032001,	/*  43 / */
	0032001,	/*  44 % */
	0026061,	/*  45 >> */
	0026061,	/*  46 << */
	0020161,	/*  47 & */
	0016161,	/*  48 | */
	0016161,	/*  49 ^ */
	0036001,	/*  50 -> */
	0000000,	/*  51 int->double */
	0000000,	/*  52 double->int */
	0000001,	/*  53 && */
	0000001,	/*  54 || */
	0030001,	/*  55 &~ */
	0000000,	/*  56 double->long */
	0000000,	/*  57 long->double */
	0000000,	/*  58 integer->long */
	0000000,	/*  59 long->integer */
	0022005,	/*  60 == */
	0022005,	/*  61 != */
	0024005,	/*  62 <= */
	0024005,	/*  63 < */
	0024005,	/*  64 >= */
	0024005,	/*  65 > */
	0024005,	/*  66 <p */
	0024005,	/*  67 <=p */
	0024005,	/*  68 >p */
	0024005,	/*  69 >=p */
	0012213,	/*  70 =+ */
	0012213,	/*  71 =- */
	0012213,	/*  72 =* */
	0012213,	/*  73 =/ */
	0012213,	/*  74 =% */
	0012253,	/*  75 =>> */
	0012253,	/*  76 =<< */
	0012253,	/*  77 =& */
	0012253,	/*  78 =| */
	0012253,	/*  79 =^ */
	0012213,	/*  80 = */
	0030001,	/*  81 & for tests */
	0032001,	/*  82 * */
	0032001,	/*  83 / */
	0032001,	/*  84 % */
	0012253,	/*  85 =& ~ */
	0012213,	/*  86 =* */
	0012213,	/*  87 =/ */
	0012213,	/*  88 =% */
	0000001,	/*  89 *(x+c) */
	0014201,	/*  90 ? */
	0026061,	/*  91 << */
	0012253,	/*  92 long =<< */
	0000101,	/*  93 \/ */
	0000101,	/*  94 \/ */
	0000101,	/*  95 /\ */
	0000101,	/*  96 /\ */
	0000001,	/*  97 , */
	0036001,	/*  98 call * */
	0000000,	/*  99 call2 */
	0036001,	/* 100 call */
	0036000,	/* 101 op 101 */
	0000000,	/* 102 goto */
	0000000,	/* 103 jump cond */
	0000000,	/* 104 init */
	0000400,	/* 105 set nregs */
	0000000,	/* 106 load value */
	0000000,	/* 107 char->long */
	0000000,	/* 108 long->char */
	0000000,	/* 109 int<->char */
	0000000,	/* 110 force register */
	0000000,	/* 111  */
	0000000,	/* 112  */
	0000000,	/* 113  */
	0000000,	/* 114  */
	0000000,	/* 115 = structure */
	0000001,	/* 116  (struct setup) */
	0032001,	/* 117 udiv */
	0032001,	/* 118 =udiv */
	0032001,	/* 119 umod */
	0032001,	/* 120 =umod */
	0000000,	/* 121 char->int */
	0000001,	/* 122 (table lookup only) */
	0034220,	/* 123 (table lookup only) */
};

char *opntab[122] = {
	"op 0",	/*   0 */
	"op 1",	/*   1 */
	"op 2",	/*   2 */
	"op 3",	/*   3 */
	"op 4",	/*   4 */
	"op 5",	/*   5 */
	"op 6",	/*   6 */
	"op 7",	/*   7 */
	":",	/*   8 */
	",",	/*   9 */
	"field select",	/*  10 */
	"op 11",	/*  11 */
	"op 12",	/*  12 */
	"int->ptr",	/*  13 */
	"ptr->int",	/*  14 */
	"long->ptr",	/*  15 */
	"field assign",	/*  16 */
	">>",	/*  17 */
	">>",	/*  18 */
	"op 19",	/*  19 */
	"name",	/*  20 */
	"short constant",	/*  21 */
	"string",	/*  22 */
	"float",	/*  23 */
	"double",	/*  24 */
	"long constant",	/*  25 */
	"long constant",	/*  26 */
	"*r++",	/*  27 */
	"*--r",	/*  28 */
	"op 29",	/*  29 */
	"++pre",	/*  30 */
	"--pre",	/*  31 */
	"++post",	/*  32 */
	"--post",	/*  33 */
	"!un",	/*  34 */
	"&",	/*  35 */
	"indirect",	/*  36 */
	"-",	/*  37 */
	"~",	/*  38 */
	".",	/*  39 */
	"+",	/*  40 */
	"-",	/*  41 */
	"*",	/*  42 */
	"/",	/*  43 */
	"%",	/*  44 */
	">>",	/*  45 */
	"<<",	/*  46 */
	"&",	/*  47 */
	"|",	/*  48 */
	"^",	/*  49 */
	"->",	/*  50 */
	"int->double",	/*  51 */
	"double->int",	/*  52 */
	"&&",	/*  53 */
	"||",	/*  54 */
	"&~",	/*  55 */
	"double->long",	/*  56 */
	"long->double",	/*  57 */
	"integer->long",	/*  58 */
	"long->integer",	/*  59 */
	"==",	/*  60 */
	"!=",	/*  61 */
	"<=",	/*  62 */
	"<",	/*  63 */
	">=",	/*  64 */
	">",	/*  65 */
	"<p",	/*  66 */
	"<=p",	/*  67 */
	">p",	/*  68 */
	">=p",	/*  69 */
	"=+",	/*  70 */
	"=-",	/*  71 */
	"=*",	/*  72 */
	"=/",	/*  73 */
	"=%",	/*  74 */
	"=>>",	/*  75 */
	"=<<",	/*  76 */
	"=&",	/*  77 */
	"=|",	/*  78 */
	"=^",	/*  79 */
	"=",	/*  80 */
	"& for tests",	/*  81 */
	"*",	/*  82 */
	"/",	/*  83 */
	"%",	/*  84 */
	"=& ~",	/*  85 */
	"=*",	/*  86 */
	"=/",	/*  87 */
	"=%",	/*  88 */
	"*(x+c)",	/*  89 */
	"\?",	/*  90 */
	"<<",	/*  91 */
	"long =<<",	/*  92 */
	"\\/",	/*  93 */
	"\\/",	/*  94 */
	"/\\",	/*  95 */
	"/\\",	/*  96 */
	",",	/*  97 */
	"call *",	/*  98 */
	"call2",	/*  99 */
	"call",	/* 100 */
	"op 101",	/* 101 */
	"goto",	/* 102 */
	"jump cond",	/* 103 */
	"init",	/* 104 */
	"set nregs",	/* 105 */
	"load value",	/* 106 */
	"char->long",	/* 107 */
	"long->char",	/* 108 */
	"int<->char",	/* 109 */
	"force register",	/* 110 */
	"",	/* 111 */
	"",	/* 112 */
	"",	/* 113 */
	"",	/* 114 */
	"= structure",	/* 115 */
	" (struct setup)",	/* 116 */
	"udiv",	/* 117 */
	"=udiv",	/* 118 */
	"umod",	/* 119 */
	"=umod",	/* 120 */
	"char->int",	/* 121 */
};

struct instab instab[] = {
	{106, "mov", "\?\?\?"},
	{ 80, "mov", "\?\?\?"},
	{ 60, "cmp", "\?\?\?"},
	{ 61, "cmp", "\?\?\?"},
	{ 62, "cmp", "\?\?\?"},
	{ 63, "cmp", "\?\?\?"},
	{ 64, "cmp", "\?\?\?"},
	{ 65, "cmp", "\?\?\?"},
	{ 66, "cmp", "\?\?\?"},
	{ 67, "cmp", "\?\?\?"},
	{ 68, "cmp", "\?\?\?"},
	{ 69, "cmp", "\?\?\?"},
	{ 40, "add", "inc"},
	{ 70, "add", "inc"},
	{ 41, "sub", "dec"},
	{ 71, "sub", "dec"},
	{ 30, "add", "inc"},
	{ 31, "sub", "dec"},
	{ 32, "add", "inc"},
	{ 33, "sub", "dec"},
	{ 42, "imul", "\?\?\?"},
	{ 72, "imul", "\?\?\?"},
	{ 43, "idiv", "\?\?\?"},
	{ 73, "idiv", "\?\?\?"},
	{117, "div", "\?\?\?"},
	{118, "div", "\?\?\?"},
	{ 44, "idiv", "\?\?\?"},
	{ 74, "idiv", "\?\?\?"},
	{119, "div", "\?\?\?"},
	{120, "div", "\?\?\?"},
	{ 14, "idiv", "\?\?\?"},
	{ 45, "sar", "rcr"},
	{ 75, "sar", "rcr"},
	{ 46, "sal", "rcl"},
	{ 76, "sal", "rcl"},
	{ 47, "and", "\?\?\?"},
	{ 77, "and", "\?\?\?"},
	{ 48, "or", "\?\?\?"},
	{ 78, "or", "\?\?\?"},
	{ 49, "xor", "\?\?\?"},
	{ 79, "xor", "\?\?\?"},
	{ 37, "neg", "\?\?\?"},
	{ 38, "not", "\?\?\?"},
	{ 82, "lmul", "lmul"},
	{ 83, "ldiv", "ldiv"},
	{ 84, "lrem", "lrem"},
	{ 86, "almul", "almul"},
	{ 87, "aldiv", "aldiv"},
	{ 88, "alrem", "alrem"},
	{ 17, "shr", "rcr"},
	{ 18, "shr", "rcr"},
	{0, 0, 0}
};

struct instab branchtab[] = {
	{ 60, "beq", "bne"},
	{ 61, "bne", "beq"},
	{ 62, "ble", "bgt"},
	{ 63, "blt", "bge"},
	{ 64, "bge", "blt"},
	{ 65, "bgt", "ble"},
	{ 66, "blos", "bhi"},
	{ 67, "blo", "bhis"},
	{ 68, "bhis", "blo"},
	{ 69, "bhi", "blos"},
	{260, "beq", "bne"},
	{261, "bne", "beq"},
	{262, "ble", "bgt"},
	{263, "blt", "bge"},
	{264, "bge", "blt"},
	{265, "bgt", "ble"},
	{266, "beq", "bne"},
	{267, "|nop", "br"},
	{268, "br", "|nop"},
	{269, "bne", "beq"},
	{0, 0, 0}
};

/* template strings */
static char s11a4[] = "jmp\301\n";
static char s11aa[] = "GBjmp\300IA\n";
static char s11b4[] = "GBjmp\300#(IB)\n";
static char s11c1[] = "call\301\n";
static char s11c8[] = "call\301\n";
static char s11cf[] = "call\300A\n";
static char s11d7[] = "GBcall\300#(IB)\n";
static char s11e5[] = "GAcall\311A\n";
static char s11ef[] = "GAV\311A+,B +2(IB)\nmovJ\311A,B(IB)\n";
static char s120d[] = "GBmov\311B,#(IB)\nV\311A+,B +2(IB)\nmovJ\311A,B(IB)\n";
static char s1237[] = "GAV\nlea\341x,B(IB)\ncall\346ldJU\n";
static char s1252[] = "GAV\311A+,*2(IB)\nmovJ\311A,(IB)\n";
static char s126d[] = "GBmov\311B,#(IB)\nV\311A+,*2(IB)\nmovJ\311A,(IB)\n";
static char s1294[] = "GAV\nlea\341x,(IB)\ncall\346ldJU\n";
static char s12ae[] = "movC\311A,A\n";
static char s12b8[] = "GBmovC\311A,#(IB)\n";
static char s12c8[] = "mov\311A+,A+\nmov\311A,A\n";
static char s12db[] = "GBmov\311A+,#+2(IB)\nmov\311A,#(IB)\n";
static char s12f9[] = "lea\341x,A\ncall\346ldCU\n";
static char s130c[] = "GBlea\341x,#(IB)\ncall\346ldCU\n";
static char s1325[] = "M'C\301\nmovC\311A,A\n";
static char s1334[] = "MC\301,B\nmov\311A,A\n";
static char s1343[] = "GJM'C\243(IJ)\nmovC\311A,#(IJ)\n";
static char s135c[] = "GBM'C\243(IB)\nmovC\311A,#(IB)\n";
static char s1375[] = "GJMC\243(IJ),B\nmovC\311A,#(IJ)\n";
static char s138f[] = "GBMC\243(IB),B\nmovC\311A,#(IB)\n";
static char s13a9[] = "M\301+,*1\nV\301,*0\nGA";
static char s13b9[] = "GJM\243+2(IJ),*1\nV\243(IJ),*0\nmov\311A+,#+2(IJ)\nmov\311A,#(IJ)\n";
static char s13ed[] = "GBM\243+2(IB),*1\nV\243(IB),*0\nmov\311A+,#+2(IB)\nmov\311A,#(IB)\n";
static char s1421[] = "lea\341x,A\ncall\346ldCU\nlea\341x,B\ncall\346MD\nlea\341x,A\ncall\346stCR\n";
static char s1456[] = "GBlea\341x,#(IB)\n|\npush\341x\ncall\346ldCU\nlea\341x,B\ncall\346MD\npop\341x\ncall\346stCR\n";
static char s1498[] = "movC\311A,A\nM'C\301\n";
static char s14a7[] = "mov\311A,A\nMC\301,B\n";
static char s14b6[] = "GJmovC\311A,#(IJ)\nM'C\243(IJ)\n";
static char s14cf[] = "GBpush\243(IB)\nM'C\243(IB)\npop\311A\n";
static char s14eb[] = "GJmovC\311A,#(IJ)\nMC\243(IJ),B\n";
static char s1505[] = "GBpush\243(IB)\nMC\243(IB),B\npop\311A\n";
static char s1522[] = "GAM\301+,*1\nV\301,*0\n";
static char s1532[] = "GJmov\311A+,#+2(IJ)\nmov\311A,#(IJ)\nM\243+2(IJ),*1\nV\243(IJ),*0\n";
static char s1566[] = "GBpush\243+2(IB)\npush\243(IB)\nM\243+2(IB),*1\nV\243(IB),*0\npop\311A\npop\311A+\n";
static char s15a2[] = "lea\341x,A\ncall\346ldCU\ncall\346dupU\nlea\341x,B\ncall\346MD\nlea\341x,A\ncall\346stCpQ\n";
static char s15e2[] = "GBlea\341x,#(IB)\n|\npush\341x\ncall\346ldCU\ncall\346dupU\nlea\341x,B\ncall\346MD\npop\341x\ncall\346stCpQ\n";
static char s162f[] = "GAMC\311A\n";
static char s1637[] = "GAM\311A\nM\311A+\nV\311A,*0\n";
static char s164a[] = "GAcall\346M\n";
static char s1654[] = "KAmovC\301,IA\n";
static char s1660[] = "GBmovC\243(IB),B\nmovC\311A,B\n";
static char s1678[] = "KA!GJmovC\243(IJ),IA\n";
static char s168b[] = "GDKApop\342x\nmovC\243(bx),IA\n";
static char s16a3[] = "KAmov\301+,IA+\nmov\301,IA\n";
static char s16b8[] = "KA!GJmov\243+2(IJ),IA+\nmov\243(IJ),IA\n";
static char s16d9[] = "GDKApop\342x\nmov\243(bx),IA\nmov\243+2(bx),IA+\n";
static char s16ff[] = "KAlea\341x,A\ncall\346stCR\n";
static char s1714[] = "KAGBlea\341x,#(IB)\ncall\346stCR\n";
static char s172f[] = "KAandC\301,Z\norC\301,IA\n";
static char s1742[] = "KCGBandC\243(IB),Z\npop\342x\norC\243(IB),bx\nmov\311A,bx\n";
static char s176e[] = "GA";
static char s1771[] = "GAM'J\311A\n";
static char s177a[] = "GAMD\311A,B\n";
static char s1784[] = "GAMD\311A,B\n";
static char s178e[] = "GAKJMD\311A,\"(IJ)\n";
static char s179e[] = "GAKIM\311A,II\n";
static char s17aa[] = "KDGApop\342x\nMD\311A,\"(bx)\n";
static char s17c0[] = "KCGApop\342x\nM\311A,bx\n";
static char s17d2[] = "GAM\311A+,B\nV\311A,*0\n";
static char s17e3[] = "GAKIM\311A+,II\nV\311A,*0\n";
static char s17f7[] = "GAM\311A+,B+\nV\311A,B\n";
static char s1808[] = "GAKIM\311A+,II+\nV\311A,II\n";
static char s181d[] = "KCGApop\342x\npop cx\nM\311A+,cx\nV\311A,bx\n";
static char s183e[] = "GAlea\341x,B\ncall\346MD\n";
static char s1851[] = "GAKBlea\341x,\"(IR)\ncall\346MD\n";
static char s186a[] = "GAKAcall\346MQ\n";
static char s1877[] = "GA| shift of 0 removed\n";
static char s188f[] = "GAMC\311A,*1\n";
static char s189a[] = "GAmovD\343x,B\nMC\311A,cl\n";
static char s18ae[] = "GAKImovD\343x,II\nMC\311A,cl\n";
static char s18c5[] = "GAKJmovD\343x,\"(IJ)\nMC\311A,cl\n";
static char s18df[] = "KCGApop\343x\nMC\311A,cl\n";
static char s18f2[] = "GA| shift\260 of long removed\n";
static char s190e[] = "GAM\311A,*1\nM'\311A+,*1\n";
static char s1921[] = "GAmov\343x,B\nM\311A,*1\nM'\311A+,*1\nloop\256-4\n";
static char s1944[] = "GAmov\343x,B\nor\343x,cx\njz\256+8\nM\311A,*1\nM'\311A+,*1\nloop\256-4\n";
static char s1975[] = "GAKImov\343x,II\nor\343x,cx\njz\256+8\nM\311A,*1\nM'\311A+,*1\nloop\256-4\n";
static char s19a9[] = "GAKJmov\343x,\"(IJ)\nor\343x,cx\njz\256+8\nM\311A,*1\nM'\311A+,*1\nloop\256-4\n";
static char s19e0[] = "KCGApop\343x\nor\343x,cx\njz\256+8\nM\311A,*1\nM'\311A+,*1\nloop\256-4\n";
static char s1a11[] = "KCGApop\343x\nor\343x,cx\njz\256+8.\nM\311A,*1\nM'\311A+,*1\nloop\256-4\npop\343x\n";
static char s1a49[] = "GA| shift\260 of long removed\n";
static char s1a65[] = "GAM\311A+,*1\nM'\311A,*1\n";
static char s1a78[] = "GAmov\343x,B\nM\311A+,*1\nM'\311A,*1\nloop\256-4\n";
static char s1a9b[] = "GAmov\343x,B\nor\343x,cx\njz\256+8\nM\311A+,*1\nM'\311A,*1\nloop\256-4\n";
static char s1acc[] = "GAKImov\343x,II\nor\343x,cx\njz\256+8\nM\311A+,*1\nM'\311A,*1\nloop\256-4\n";
static char s1b00[] = "GAKJmov\343x,\"(IJ)\nor\343x,cx\njz\256+8\nM\311A+,*1\nM'\311A,*1\nloop\256-4\n";
static char s1b37[] = "KCGApop\343x\nor\343x,cx\njz\256+8\nM\311A+,*1\nM'\311A,*1\nloop\256-4\n";
static char s1b68[] = "KCGApop\343x\nor\343x,cx\njz\256+8.\nM\311A+,*1\nM'\311A,*1\nloop\256-4\npop\343x\n";
static char s1ba0[] = "movC\341x,A\nmovD\343x,B\nML\343x\n";
static char s1bb8[] = "GBmovC\341x,#(IB)\nmovD\343x,B\nML\343x\n";
static char s1bd6[] = "GAmovC\341x,IA\nmovD\343x,B\nML\343x\n";
static char s1bf1[] = "movL\341x,A\nML\302\n";
static char s1bff[] = "GBmovL\341x,#(IB)\nML\302\n";
static char s1c13[] = "GAmovL\341x,IA\nML\302\n";
static char s1c24[] = "GAKJmovL\341x,IA\nML\242(IJ)\n";
static char s1c3b[] = "KCGAmovL\341x,IA\npop\343x\nML\343x\n";
static char s1c55[] = "GAlea\341x,B\ncall\346mulD\n";
static char s1c6a[] = "GAKBlea\341x,\"(IR)\ncall\346mulD\n";
static char s1c85[] = "GAKAcall\346mulQ\n";
static char s1c94[] = "mov\341x,A\nTmov\343x,B\nM\343x\n";
static char s1caa[] = "GBmov\341x,#(IB)\nTmov\343x,B\nM\343x\n";
static char s1cc6[] = "GAmov\341x,IA\nTmov\343x,B\nM\343x\n";
static char s1cdf[] = "mov\341x,A\nTM\302\n";
static char s1cec[] = "GBmov\341x,#(IB)\nTM\302\n";
static char s1cff[] = "GAmov\341x,IA\nTM\302\n";
static char s1d0f[] = "GAKJmov\341x,IA\nTM\242(IJ)\n";
static char s1d25[] = "KCGAmov\341x,IA\nTpop\343x\nM\343x\n";
static char s1d3e[] = "GAlea\341x,B\ncall\346divD\n";
static char s1d53[] = "GAKBlea\341x,\"(IR)\ncall\346divD\n";
static char s1d6e[] = "GAKAcall\346divQ\n";
static char s1d7d[] = "MC\301,B\n";
static char s1d84[] = "ML\301,B\nmovL\311A,A\n";
static char s1d94[] = "movC\311A,A\nML\311A,B\nmovC\301,IA\n";
static char s1dae[] = "KAML\301,IA\nmovL\311A,A\n";
static char s1dc1[] = "ML\243(IB),B\nmovL\311A,#(IB)\n";
static char s1dd9[] = "GBmovL\341x,B\nML\243(IB),ax\nmovL\311A,#(IB)\n";
static char s1dfd[] = "KA!GJML\243(IJ),IA\nmovL\311A,#(IJ)\n";
static char s1e1b[] = "KCGBpop\341x\nML\243(IB),ax\nmovL\311A,#(IB)\n";
static char s1e3e[] = "M\301,B\nV\301+,*0.\n";
static char s1e4c[] = "GBM\243+2(IB),B\nV\243(IB),*0.\n";
static char s1e65[] = "KCGJmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\npop\343x\npop\341x\nM\311A+,ax\nV\311A,cx\nmov\243(IJ),IA\nmov\243+2(IJ),IA+\n";
static char s1ebb[] = "KAlea\341x,A\ncall\346MC\nlea\341x,A\ncall\346stCR\n";
static char s1ee0[] = "KAGBlea\341x,#(IB)\n|\npush\341x\ncall\346MC\npop\341x\ncall\346stCR\n";
static char s1f12[] = "MC\301,B\n";
static char s1f19[] = "M\301,B\nV\301+,*0.\n";
static char s1f27[] = "GBM\243+2(IB),B\nV\243(IB),*0.\n";
static char s1f40[] = "GAlea\341x,B\ncall\346MD\nlea\341x,A\ncall\346stCR\n";
static char s1f65[] = "GAKBlea\341x,\"(IR)\ncall\346MD\nlea\341x,A\ncall\346stCR\n";
static char s1f90[] = "GAKAcall\346M\nlea\341x,A\ncall\346stCR\n";
static char s1fae[] = "GBlea\341x,#(IB)\n|\npush\341x\ncall\346ldCU\nKAcall\346MQ\npop\341x\ncall\346stCR\n";
static char s1fea[] = "mov\341x,A\nmov\343x,B\nM\343x\nmov\301,ax\n";
static char s2007[] = "GBmov\341x,#(IB)\nmov\343x,B\nM\343x\nmov\243(IB),ax\n";
static char s202e[] = "mov\341x,A\nM\302\nmov\301,ax\n";
static char s2042[] = "KAmov\341x,IA\nM\301\nmov\301,ax\n";
static char s2059[] = "GBmov\341x,#(IB)\nM\302\nmov\243(IB),ax\n";
static char s2077[] = "KCGBpop\341x\nM\243(IB)\nmov\243(IB),ax\n";
static char s2095[] = "KAlea\341x,A\ncall\346mulC\nlea\341x,A\ncall\346stCR\n";
static char s20bc[] = "KAGBlea\341x,#(IB)\n|\npush\341x\ncall\346mulC\npop\341x\ncall\346stCR\n";
static char s20f0[] = "mov\341x,A\nTmov\343x,B\nM\343x\nmov\301,ax\n";
static char s210e[] = "GBmov\341x,#(IB)\nTmov\343x,B\nM\343x\nmov\243(IB),ax\n";
static char s2136[] = "mov\341x,A\nTM\302\nmov\301,ax\n";
static char s214b[] = "KAmov\343x,IA\nmov\341x,A\nTM\343x\nmov\301,ax\n";
static char s216c[] = "GBmov\341x,#(IB)\nTM\302\nmov\243(IB),ax\n";
static char s218b[] = "KCGBmov\341x,#(IB)\nTpop\343x\nM\343x\nmov\243(IB),ax\n";
static char s21b3[] = "GAlea\341x,B\ncall\346divD\nlea\341x,A\ncall\346stCR\n";
static char s21da[] = "GAKBlea\341x,\"(IR)\ncall\346divD\nlea\341x,A\ncall\346stCR\n";
static char s2207[] = "GAKAcall\346div\nlea\341x,A\ncall\346stCR\n";
static char s2227[] = "GBlea\341x,#(IB)\n|\npush\341x\ncall\346ldCU\nKAcall\346divQ\npop\341x\ncall\346stCR\n";
static char s2265[] = "mov\341x,A\nTmov\343x,B\nM\343x\nmov\301,dx\n";
static char s2283[] = "GBmov\341x,#(IB)\nTmov\343x,B\nM\343x\nmov\243(IB),dx\n";
static char s22ab[] = "mov\341x,A\nTM\302\nmov\301,dx\n";
static char s22c0[] = "KAmov\343x,IA\nmov\341x,A\nTM\343x\nmov\301,dx\n";
static char s22e1[] = "GBmov\341x,#(IB)\nTM\302\nmov\243(IB),dx\n";
static char s2300[] = "KCGBmov\341x,#(IB)\nTpop\343x\nM\343x\nmov\243(IB),dx\n";
static char s2328[] = "|\363hift of 0 missing\nmovC\311A,A\n";
static char s2346[] = "GB|\363hift of 0 missing\nmovC\311A,#(IB)\n";
static char s236a[] = "MC\301,*1\nmovC\311A,A\n";
static char s237b[] = "GBMC\243(IB),*1\nmovC\311A,#(IB)\n";
static char s2396[] = "movD\343x,B\nMC\301,cl\nmovC\311A,A\n";
static char s23b0[] = "KBmovD\343x,\"(IB)\nMC\301,cl\nmovC\311A,A\n";
static char s23d0[] = "KAmovD\343x,IA\nMC\301,cl\nmovC\311A,A\n";
static char s23ed[] = "GBmovD\343x,B\nMC\243(IB),cl\nmovC\311A,#(IB)\n";
static char s2411[] = "GBKJmovD\343x,\"(IJ)\nMC\243(IB),cl\nmovC\311A,#(IB)\n";
static char s243b[] = "GBKAmovD\343x,II\nMC\243(IB),cl\nmovC\311A,#(IB)\n";
static char s2462[] = "KCGBpop\343x\nMC\243(IB),cl\nmovC\311A,#(IB)\n";
static char s2485[] = "| shift 0 of long removed\n";
static char s24a0[] = "GB| shift\260 of long missing\n";
static char s24bc[] = "GB| shift\260 of long missing\n";
static char s24d8[] = "M\301+,*1\nM'\301,*1\nmov\311A,A\nmov\311A+,A+\n";
static char s24f9[] = "GBM\243+2(IB),*1\nM'\243(IB),*1\nmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\n";
static char s252e[] = "GAmov\343x,B\nM\311A+,*1\nM'\311A,*1\nloop\256-4\nmov\301,IA\nmov\301+,IA+\n";
static char s2563[] = "GAmov\343x,B\nor\343x,cx\njz\256+16\nM\311A+,*1\nM'\311A,*1\nloop\256-4\nmov\301,IA\nmov\301+,IA+\n";
static char s25a7[] = "KCGApop\343x\nor\343x,cx\njz\256+16\nM\311A+,*1\nM'\311A,*1\nloop\256-4\nmov\301,IA\nmov\301+,IA+\n";
static char s25eb[] = "GJmov\311A,#+2(IJ)\nmov\311A+,#(IJ)\nmov\343x,B\nM\311A+,*1\nM'\311A,*1\nloop\256-4\nmov\243(IJ),IA\nmov\243+2(IJ),IA+\n";
static char s2644[] = "GJmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\nmov\343x,B\nor\343x,cx\njz\256+16.\nM\311A+,*1\nM'\311A,*1\nloop\256-4\nmov\243(IJ),IA\nmov\243+2(IJ),IA+\n";
static char s26ad[] = "KCGJmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\npop\343x\nor\343x,cx\njz\256+16.\nM\311A+,*1\nM'\311A,*1\nloop\256-4\nmov\243(IJ),IA\nmov\243+2(IJ),IA+\n";
static char s2716[] = "KCmov\311A,A\nmov\311A+,A+\npop\343x\nor\343x,cx\njz\256+16.\nM\311A+,*1\nM'\311A,*1\nloop\256-4\nmov\301,IA\nmov\301+,IA+\npop\343x\n";
static char s2771[] = "KCGJmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\npop\343x\nor\343x,cx\njz\256+16.\nM\311A+,*1\nM'\311A,*1\nloop\256-4\nmov\243(IJ),IA\nmov\243+2(IJ),IA+\npop\343x\n";
static char s27e0[] = "|\363hift of 0 removed\nmovC\311A,A\n";
static char s27fe[] = "GB|\363hift of 0 removed\nmovC\311A,#(IB)\n";
static char s2822[] = "MC\301,*1\nmovC\311A,A\n";
static char s2833[] = "GBMC\243(IB),*1\nmovC\311A,#(IB)\n";
static char s284e[] = "movD\343x,B\nMC\301,cl\nmovC\311A,A\n";
static char s2868[] = "KBmovD\343x,\"(IB)\nMC\301,cl\nmovC\311A,A\n";
static char s2888[] = "KAmovD\343x,IA\nMC\301,cl\nmovC\311A,A\n";
static char s28a5[] = "GBmovD\343x,B\nMC\243(IB),cl\nmovC\311A,#(IB)\n";
static char s28c9[] = "GBKJmovD\343x,\"(IJ)\nMC\243(IB),cl\nmovC\311A,#(IB)\n";
static char s28f3[] = "GBKAmovD\343x,II\nMC\243(IB),cl\nmovC\311A,#(IB)\n";
static char s291a[] = "KCGBpop\343x\nMC\243(IB),cl\nmovC\311A,#(IB)\n";
static char s293d[] = "| shift\260 of long missing\n";
static char s2957[] = "GB| shift\260 of long missing\n";
static char s2973[] = "M\301,*1\nM'\301+,*1\nmov\311A,A\nmov\311A+,A+\n";
static char s2994[] = "GBM\243(IB),*1\nM'\243+2(IB),*1\nmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\n";
static char s29c9[] = "GAmov\343x,B\nM\311A,*1\nM'\311A+,*1\nloop\256-4\nmov\301,IA\nmov\301+,IA+\n";
static char s29fe[] = "GAmov\343x,B\nor\343x,cx\njz\256+16\nM\311A,*1\nM'\311A+,*1\nloop\256-4\nmov\301,IA\nmov\301+,IA+\n";
static char s2a42[] = "KCGApop\343x\nor\343x,cx\njz\256+16\nM\311A,*1\nM'\311A+,*1\nloop\256-4\nmov\301,IA\nmov\301+,IA+\n";
static char s2a86[] = "GJmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\nmov\343x,B\nM\311A,*1\nM'\311A+,*1\nloop\256-4\nmov\243(IJ),IA\nmov\243+2(IJ),IA+\n";
static char s2adf[] = "GJmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\nmov\343x,B\nor\343x,cx\njz\256+16.\nM\311A,*1\nM'\311A+,*1\nloop\256-4\nmov\243(IJ),IA\nmov\243+2(IJ),IA+\n";
static char s2b48[] = "KCGJmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\npop\343x\nor\343x,cx\njz\256+16.\nM\311A,*1\nM'\311A+,*1\nloop\256-4\nmov\243(IJ),IA\nmov\243+2(IJ),IA+\n";
static char s2bb1[] = "KCmov\311A,A\nmov\311A+,A+\npop\343x\nor\343x,cx\njz\256+16.\nM\311A,*1\nM'\311A+,*1\nloop\256-4\nmov\301,IA\nmov\301+,IA+\npop\343x\n";
static char s2c0c[] = "KCGJmov\311A,#(IJ)\nmov\311A+,#+2(IJ)\npop\343x\nor\343x,cx\njz\256+16.\nM\311A,*1\nM'\311A+,*1\nloop\256-4\nmov\243(IJ),IA\nmov\243+2(IJ),IA+\npop\343x\n";
static char s2c7b[] = "GATcall\351tofU\n";
static char s2c89[] = "GAcall\346toiQ\n";
static char s2c96[] = "GAcall\346tolQ\n";
static char s2ca3[] = "GCcall\354tofU\nadd\363p,*4\n";
static char s2cb9[] = "GI!sub\311A,IA\n";
static char s2cc6[] = "GIdcwd\n";
static char s2cce[] = "mov\311A,A+\n";
static char s2cd8[] = "GBmov\311A,#+2(IB)\n";
static char s2ce9[] = "KCGCcall\315d\nadd\363p,*8.\n";
static char s2cff[] = "KCGCcall\315d\nadd\363p,*6.\n";
static char s2d15[] = "movb\341x,A\ncbw\ncwd\n";
static char s2d27[] = "GBmovb\341x,#(IB)\ncbw\ncwd\n";
static char s2d3f[] = "GAmov\341x,IA\ncbw\ncwd\n";
static char s2d53[] = "mov\341x,A+\ncbw\n";
static char s2d61[] = "GBmov\341x,#+2(IB)\ncbw\n";
static char s2d76[] = "GAmov\341x,IA+\ncbw\n";
static char s2d87[] = "mov\341x,A\nT";
static char s2d91[] = "movb\341x,A\ncbw\n";
static char s2d9f[] = "GBmovb\341x,#(IB)\ncbw\n";
static char s2db3[] = "GAmov\341x,IA\ncbw\n";
static char s2dc3[] = "MC\301,B\n";
static char s2dca[] = "KBMC\301,\"(IB)\n";
static char s2dd7[] = "GBMC\243(IB),B\n";
static char s2de4[] = "KAMC\301,IA\n";
static char s2dee[] = "KA!GJMC\243(IJ),IA\n";
static char s2dff[] = "GDKApop\342x\nMC\243(bx),IA\n";
static char s2e15[] = "mov\311A,B\nM\301,IA\nM\301+,IA\n";
static char s2e2b[] = "M\301+,B\nV\301,*0\n";
static char s2e38[] = "GBM\243(IB),B\nM\243+2(IB),*0\n";
static char s2e50[] = "KBM\301+,\"(IB)\nM\301,*0\n";
static char s2e63[] = "KAM\301+,IA\nM\301,*0\n";
static char s2e73[] = "KAM\301+,IA+\nV\301,IA\n";
static char s2e84[] = "KA!GJM\243+2(IJ),IA+\nV\243(IJ),IA\n";
static char s2ea1[] = "KCGBmov\342x,IA\npop\311A\npop\311A+\nM\243+2(bx),IA+\nV\243(bx),IA\n";
static char s2ed3[] = "M'C\301\n";
static char s2ed9[] = "GBM'C\243(IB)\n";
static char s2ee5[] = "lea\341x,A\ncall\346ldCU\nlea\341x,B\ncall\346MD\nlea\341x,A\ncall\346stCpQ\n";
static char s2f1b[] = "GBlea\341x,#(IB)\n|\npush\341x\ncall\346ldCU\nlea\341x,B\ncall\346MD\npop\341x\ncall\346stCpQ\n";
static char s2f5e[] = "GBlea\341x,#(IB)\n|\npush\341x\ncall\346ldCU\nKAcall\346MQ\npop\341x\ncall\346stCpQ\n";
static char s2f9b[] = "| shift by 0 removed\n";
static char s2fb1[] = "GB| shift by 0 removed\n";
static char s2fc9[] = "MC\301,*1\n";
static char s2fd1[] = "GBMC\243(IB),*1\n";
static char s2fdf[] = "movD\343x,B\nMC\301,cl\n";
static char s2ff0[] = "KBmovD\343x,\"(IB)\nMC\301,cl\n";
static char s3007[] = "KAmovD\343x,IA\nMC\301,cl\n";
static char s301b[] = "GBmovD\343x,B\nMC\243(IB),cl\n";
static char s3032[] = "GBKJmovD\343x,\"(IJ)\nMC\243(IB),cl\n";
static char s304f[] = "GBKAmovD\343x,II\nMC\243(IB),cl\n";
static char s3069[] = "KCGBpop\343x\nMC\243(IB),cl\n";
static char s307f[] = "andC\301,Z\norC\301,B\n";
static char s308f[] = "KAandC\301,Z\norC\301,IA\n";
static char s30a2[] = "GBandC\243(IB),Z\norC\243(IB),B\n";
static char s30bc[] = "KAGJandC\243(IJ),Z\norC\243(IJ),IA\n";
static char s30d9[] = "GBKIandC\243(IB),Z\norC\243(IB),II\n";
static char s30f6[] = "KCGBandC\243(IB),Z\npop\341x\norC\243(IB),ax\n";
static char s3119[] = "movC\311A,A\ntestC\311A,IA\n";
static char s312e[] = "cmpC\301,*0\n";
static char s3138[] = "GBcmpC\243(IB),*0\n";
static char s3148[] = "GAtestC\311A,IA\n";
static char s3156[] = "GBcmp\243(IB),*0\nX0cmp\243+2(IB),*0\nX1";
static char s3177[] = "GAcmp\311A,*0\nX0cmp\311A+,*0\nX1";
static char s3191[] = "GAcall\346testQ\nsahf\n";
static char s31a4[] = "testC\301,B\n";
static char s31ae[] = "GBtestC\243(IB),B\n";
static char s31be[] = "KBtestD\242(IB),A\n";
static char s31ce[] = "M\301,*0\nX0M\301+,B\nX1";
static char s31df[] = "GBM\243(IB),*0\nX0M\243+2(IB),*0\nX1";
static char s31fc[] = "GBM\243(IB),*0\nX0M\243+2(IB),B\nX1";
static char s3218[] = "GAM\311A,*0\nX0M\311A+,*0\nX1";
static char s322e[] = "GAM\311A,*0\nX0M\311A+,B\nX1";
static char s3243[] = "GAM\311A,B\nX0M\311A+,B+\nX1";
static char s3258[] = "GAKJM\311A,\"(IJ)\nX0M\311A+,\"+2(IJ)\nX1";
static char s3278[] = "KCGApop\343x\npop\342x\nM\311A,cx\nX0M\311A+,bx\nX1";
static char s329c[] = "GAKAcall\346cmpQQ\nsahf\n";
static char s32b1[] = "M'C\301\n";
static char s32b7[] = "GBM'C\243(IB)\n";
static char s32c3[] = "HA";
static char s32c6[] = "mov\311A,A\npush\311A\n";
static char s32d6[] = "mov\311A,A+\npush\311A\nmov\311A,A\npush\311A\n";
static char s32f6[] = "push\301\n";
static char s32fd[] = "GBpush\243(IB)\n";
static char s330a[] = "GBpush\243+2(IB)\npush\243(IB)\n";
static char s3323[] = "GAsub\363p,*8\nmov\341x,sp\ncall\346stdpQ\n";
static char s3343[] = "GCKC";

static struct optab l34a0[] = {
	{020, 0, 077, 0, s12ae},	/* %a,n */
	{0177, 0, 077, 0, s12b8},	/* %n*,n */
	{020, 8, 077, 0, s12c8},	/* %al,n */
	{0177, 8, 077, 0, s12db},	/* %nl*,n */
	{020, 4, 077, 0, s12f9},	/* %af,n */
	{020, 5, 077, 0, s12f9},	/* %ad,n */
	{0177, 4, 077, 0, s130c},	/* %nf*,n */
	{0177, 5, 077, 0, s130c},	/* %nd*,n */
	{0}
};
static struct optab l3498[] = {
	{077, 0, 077, 0, s1294},	/* %n,n */
	{0}
};
static struct optab l348a[] = {
	{077, 0, 077, 0, s1252},	/* %n,n */
	{0177, 0, 077, 0, s126d},	/* %n*,n */
	{0}
};
static struct optab l34d2[] = {
	{020, 0, 05, 0, s1325},	/* %a,1 */
	{020, 0, 010, 0, s1334},	/* %a,c */
	{0124, 0, 05, 0, s1343},	/* %e*,1 */
	{0177, 0, 05, 0, s135c},	/* %n*,1 */
	{0124, 0, 010, 0, s1375},	/* %e*,c */
	{0177, 0, 010, 0, s138f},	/* %n*,c */
	{020, 8, 05, 0, s13a9},	/* %al,1 */
	{0124, 8, 05, 0, s13b9},	/* %el*,1 */
	{0177, 8, 05, 0, s13ed},	/* %nl*,1 */
	{020, 4, 020, 4, s1421},	/* %af,af */
	{020, 5, 020, 5, s1421},	/* %ad,ad */
	{0177, 4, 020, 4, s1456},	/* %nf*,af */
	{0177, 5, 020, 5, s1456},	/* %nd*,ad */
	{0}
};
static struct optab l3522[] = {
	{020, 0, 05, 0, s1498},	/* %a,1 */
	{020, 0, 010, 0, s14a7},	/* %a,c */
	{0124, 0, 05, 0, s14b6},	/* %e*,1 */
	{0177, 0, 05, 0, s14cf},	/* %n*,1 */
	{0124, 0, 010, 0, s14eb},	/* %e*,c */
	{0177, 0, 010, 0, s1505},	/* %n*,c */
	{020, 8, 05, 0, s1522},	/* %al,1 */
	{0124, 8, 05, 0, s1532},	/* %el*,1 */
	{0177, 8, 05, 0, s1566},	/* %nl*,1 */
	{020, 4, 020, 4, s15a2},	/* %af,af */
	{020, 5, 020, 5, s15a2},	/* %ad,ad */
	{0177, 4, 020, 4, s15e2},	/* %nf*,af */
	{0177, 5, 020, 5, s15e2},	/* %nd*,ad */
	{0}
};
static struct optab l3572[] = {
	{077, 0, 077, 0, s162f},	/* %n,n */
	{077, 8, 077, 0, s1637},	/* %nl,n */
	{077, 4, 077, 0, s164a},	/* %nf,n */
	{077, 5, 077, 0, s164a},	/* %nd,n */
	{0}
};
static struct optab l345a[] = {
	{011, 0, 077, 0, s11c8},	/* %r,n */
	{020, 0, 077, 0, s11cf},	/* %a,n */
	{0177, 0, 077, 0, s11d7},	/* %n*,n */
	{077, 0, 077, 0, s11e5},	/* %n,n */
	{0}
};
static struct optab l3452[] = {
	{020, 0, 077, 0, s11c1},	/* %a,n */
	{0}
};
static struct optab l358c[] = {
	{020, 0, 077, 0, s1654},	/* %a,n */
	{0177, 1, 011, 1, s1660},	/* %nw*,rw */
	{0177, 3, 011, 3, s1660},	/* %nb*,rb */
	{0124, 1, 077, 1, s1678},	/* %ew*,nw */
	{0124, 3, 077, 3, s1678},	/* %eb*,nb */
	{0177, 1, 077, 1, s168b},	/* %nw*,nw */
	{0177, 3, 077, 3, s168b},	/* %nb*,nb */
	{0177, 3, 077, 1, s168b},	/* %nb*,nw */
	{020, 8, 077, 8, s16a3},	/* %al,nl */
	{0124, 8, 077, 8, s16b8},	/* %el*,nl */
	{0177, 8, 077, 8, s16d9},	/* %nl*,nl */
	{020, 4, 077, 4, s16ff},	/* %af,nf */
	{020, 5, 077, 5, s16ff},	/* %ad,nd */
	{0177, 4, 077, 4, s1714},	/* %nf*,nf */
	{0177, 5, 077, 5, s1714},	/* %nd*,nd */
	{0}
};
static struct optab l35fc[] = {
	{077, 0, 04, 0, s176e},	/* %n,z */
	{077, 0, 05, 0, s1771},	/* %n,1 */
	{077, 1, 010, 0, s177a},	/* %nw,c */
	{077, 3, 010, 0, s177a},	/* %nb,c */
	{077, 1, 020, 1, s1784},	/* %nw,aw */
	{077, 3, 020, 3, s1784},	/* %nb,ab */
	{077, 1, 0124, 1, s178e},	/* %nw,ew* */
	{077, 3, 0124, 3, s178e},	/* %nb,eb* */
	{077, 1, 024, 1, s179e},	/* %nw,ew */
	{077, 3, 024, 3, s179e},	/* %nb,eb */
	{077, 1, 0177, 1, s17aa},	/* %nw,nw* */
	{077, 3, 0177, 3, s17aa},	/* %nb,nb* */
	{077, 1, 077, 1, s17c0},	/* %nw,nw */
	{077, 3, 077, 3, s17c0},	/* %nb,nb */
	{077, 8, 020, 9, s17d2},	/* %nl,au */
	{077, 8, 024, 9, s17e3},	/* %nl,eu */
	{077, 8, 020, 8, s17f7},	/* %nl,al */
	{077, 8, 024, 8, s1808},	/* %nl,el */
	{077, 8, 077, 8, s181d},	/* %nl,nl */
	{077, 4, 020, 4, s183e},	/* %nf,af */
	{077, 5, 020, 5, s183e},	/* %nd,ad */
	{077, 4, 0177, 4, s1851},	/* %nf,nf* */
	{077, 5, 0177, 5, s1851},	/* %nd,nd* */
	{077, 4, 077, 4, s186a},	/* %nf,nf */
	{077, 5, 077, 5, s186a},	/* %nd,nd */
	{0}
};
static struct optab l379c[] = {
	{020, 1, 011, 1, s1ba0},	/* %aw,rw */
	{020, 3, 011, 3, s1ba0},	/* %ab,rb */
	{0177, 1, 011, 1, s1bb8},	/* %nw*,rw */
	{0177, 3, 011, 3, s1bb8},	/* %nb*,rb */
	{077, 1, 011, 1, s1bd6},	/* %nw,rw */
	{077, 3, 011, 3, s1bd6},	/* %nb,rb */
	{020, 1, 020, 1, s1bf1},	/* %aw,aw */
	{020, 3, 020, 3, s1bf1},	/* %ab,ab */
	{0177, 1, 020, 1, s1bff},	/* %nw*,aw */
	{0177, 3, 020, 3, s1bff},	/* %nb*,ab */
	{077, 1, 020, 1, s1c13},	/* %nw,aw */
	{077, 3, 020, 3, s1c13},	/* %nb,ab */
	{077, 1, 0124, 1, s1c24},	/* %nw,ew* */
	{077, 3, 0124, 3, s1c24},	/* %nb,eb* */
	{077, 1, 077, 1, s1c3b},	/* %nw,nw */
	{077, 3, 077, 3, s1c3b},	/* %nb,nb */
	{077, 4, 020, 4, s1c55},	/* %nf,af */
	{077, 5, 020, 5, s1c55},	/* %nd,ad */
	{077, 4, 0177, 4, s1c6a},	/* %nf,nf* */
	{077, 5, 0177, 5, s1c6a},	/* %nd,nd* */
	{077, 4, 077, 4, s1c85},	/* %nf,nf */
	{077, 5, 077, 5, s1c85},	/* %nd,nd */
	{0}
};
static struct optab l3822[] = {
	{020, 1, 011, 1, s1c94},	/* %aw,rw */
	{0177, 1, 011, 1, s1caa},	/* %nw*,rw */
	{077, 1, 011, 1, s1cc6},	/* %nw,rw */
	{020, 1, 020, 1, s1cdf},	/* %aw,aw */
	{0177, 1, 020, 1, s1cec},	/* %nw*,aw */
	{077, 1, 020, 1, s1cff},	/* %nw,aw */
	{077, 1, 0124, 1, s1d0f},	/* %nw,ew* */
	{077, 1, 077, 1, s1d25},	/* %nw,nw */
	{077, 4, 020, 4, s1d3e},	/* %nf,af */
	{077, 5, 020, 5, s1d3e},	/* %nd,ad */
	{077, 4, 0177, 4, s1d53},	/* %nf,nf* */
	{077, 5, 0177, 5, s1d53},	/* %nd,nd* */
	{077, 4, 077, 4, s1d6e},	/* %nf,nf */
	{077, 5, 077, 5, s1d6e},	/* %nd,nd */
	{0}
};
static struct optab l36f0[] = {
	{077, 0, 04, 0, s1877},	/* %n,z */
	{077, 0, 05, 0, s188f},	/* %n,1 */
	{077, 0, 020, 0, s189a},	/* %n,a */
	{077, 0, 024, 0, s18ae},	/* %n,e */
	{077, 0, 0124, 1, s18c5},	/* %n,ew* */
	{077, 0, 077, 0, s18df},	/* %n,n */
	{077, 8, 04, 0, s18f2},	/* %nl,z */
	{077, 8, 05, 0, s190e},	/* %nl,1 */
	{077, 8, 010, 0, s1921},	/* %nl,c */
	{077, 8, 020, 0, s1944},	/* %nl,a */
	{077, 8, 024, 0, s1975},	/* %nl,e */
	{077, 8, 0124, 1, s19a9},	/* %nl,ew* */
	{077, 8, 077, 0, s19e0},	/* %nl,n */
	{077, 8, 077, 8, s1a11},	/* %nl,nl */
	{0}
};
static struct optab l3746[] = {
	{077, 0, 04, 0, s1877},	/* %n,z */
	{077, 0, 05, 0, s188f},	/* %n,1 */
	{077, 0, 020, 0, s189a},	/* %n,a */
	{077, 0, 024, 0, s18ae},	/* %n,e */
	{077, 0, 0124, 0, s18c5},	/* %n,e* */
	{077, 0, 077, 0, s18df},	/* %n,n */
	{077, 8, 04, 0, s1a49},	/* %nl,z */
	{077, 8, 05, 0, s1a65},	/* %nl,1 */
	{077, 8, 010, 0, s1a78},	/* %nl,c */
	{077, 8, 020, 0, s1a9b},	/* %nl,a */
	{077, 8, 024, 0, s1acc},	/* %nl,e */
	{077, 8, 0124, 1, s1b00},	/* %nl,ew* */
	{077, 8, 077, 0, s1b37},	/* %nl,n */
	{077, 8, 077, 8, s1b68},	/* %nl,nl */
	{0}
};
static struct optab l3694[] = {
	{077, 1, 010, 0, s177a},	/* %nw,c */
	{077, 3, 010, 0, s177a},	/* %nb,c */
	{077, 1, 020, 1, s1784},	/* %nw,aw */
	{077, 3, 020, 3, s1784},	/* %nb,ab */
	{077, 1, 0124, 1, s178e},	/* %nw,ew* */
	{077, 3, 0124, 3, s178e},	/* %nb,eb* */
	{077, 1, 024, 1, s179e},	/* %nw,ew */
	{077, 3, 024, 3, s179e},	/* %nb,eb */
	{077, 1, 0177, 1, s17aa},	/* %nw,nw* */
	{077, 3, 0177, 3, s17aa},	/* %nb,nb* */
	{077, 1, 077, 0, s17c0},	/* %nw,n */
	{077, 3, 077, 0, s17c0},	/* %nb,n */
	{077, 8, 020, 8, s17f7},	/* %nl,al */
	{077, 8, 024, 8, s1808},	/* %nl,el */
	{077, 8, 077, 8, s181d},	/* %nl,nl */
	{0}
};
static struct optab l3878[] = {
	{020, 1, 010, 0, s1d7d},	/* %aw,c */
	{020, 3, 010, 0, s1d7d},	/* %ab,c */
	{020, 1, 011, 1, s1d84},	/* %aw,rw */
	{020, 1, 011, 3, s1d84},	/* %aw,rb */
	{020, 3, 011, 3, s1d84},	/* %ab,rb */
	{020, 3, 011, 1, s1d84},	/* %ab,rw */
	{020, 1, 020, 1, s1d94},	/* %aw,aw */
	{020, 3, 020, 3, s1d94},	/* %ab,ab */
	{020, 1, 020, 3, s1d94},	/* %aw,ab */
	{020, 3, 020, 1, s1d94},	/* %ab,aw */
	{020, 1, 077, 1, s1dae},	/* %aw,nw */
	{020, 3, 077, 3, s1dae},	/* %ab,nb */
	{020, 1, 077, 3, s1dae},	/* %aw,nb */
	{020, 3, 077, 1, s1dae},	/* %ab,nw */
	{0177, 1, 010, 0, s1dc1},	/* %nw*,c */
	{0177, 3, 010, 0, s1dc1},	/* %nb*,c */
	{0177, 1, 020, 1, s1dd9},	/* %nw*,aw */
	{0177, 3, 020, 3, s1dd9},	/* %nb*,ab */
	{0177, 1, 020, 3, s1dd9},	/* %nw*,ab */
	{0177, 3, 020, 1, s1dd9},	/* %nb*,aw */
	{0124, 1, 077, 1, s1dfd},	/* %ew*,nw */
	{0124, 3, 077, 3, s1dfd},	/* %eb*,nb */
	{0124, 1, 077, 3, s1dfd},	/* %ew*,nb */
	{0124, 3, 077, 1, s1dfd},	/* %eb*,nw */
	{0177, 1, 077, 1, s1e1b},	/* %nw*,nw */
	{0177, 3, 077, 3, s1e1b},	/* %nb*,nb */
	{0177, 1, 077, 3, s1e1b},	/* %nw*,nb */
	{0177, 3, 077, 1, s1e1b},	/* %nb*,nw */
	{020, 8, 010, 0, s1e3e},	/* %al,c */
	{0177, 8, 010, 0, s1e4c},	/* %nl*,c */
	{0177, 8, 077, 8, s1e65},	/* %nl*,nl */
	{020, 4, 077, 4, s1ebb},	/* %af,nf */
	{020, 5, 077, 5, s1ebb},	/* %ad,nd */
	{0177, 4, 077, 4, s1ee0},	/* %nf*,nf */
	{0177, 5, 077, 5, s1ee0},	/* %nd*,nd */
	{0}
};
static struct optab l394c[] = {
	{020, 1, 010, 0, s1f12},	/* %aw,c */
	{020, 3, 010, 0, s1f12},	/* %ab,c */
	{020, 1, 011, 1, s1d84},	/* %aw,rw */
	{020, 1, 011, 3, s1d84},	/* %aw,rb */
	{020, 3, 011, 3, s1d84},	/* %ab,rb */
	{020, 3, 011, 1, s1d84},	/* %ab,rw */
	{020, 1, 020, 1, s1d94},	/* %aw,aw */
	{020, 3, 020, 3, s1d94},	/* %ab,ab */
	{020, 1, 020, 3, s1d94},	/* %aw,ab */
	{020, 3, 020, 1, s1d94},	/* %ab,aw */
	{020, 1, 077, 1, s1dae},	/* %aw,nw */
	{020, 3, 077, 3, s1dae},	/* %ab,nb */
	{020, 1, 077, 3, s1dae},	/* %aw,nb */
	{020, 3, 077, 1, s1dae},	/* %ab,nw */
	{0177, 1, 020, 1, s1dc1},	/* %nw*,aw */
	{0177, 3, 020, 3, s1dc1},	/* %nb*,ab */
	{0177, 1, 020, 3, s1dc1},	/* %nw*,ab */
	{0177, 3, 020, 1, s1dc1},	/* %nb*,aw */
	{0124, 1, 077, 1, s1dfd},	/* %ew*,nw */
	{0124, 3, 077, 3, s1dfd},	/* %eb*,nb */
	{0124, 1, 077, 3, s1dfd},	/* %ew*,nb */
	{0124, 3, 077, 1, s1dfd},	/* %eb*,nw */
	{0177, 1, 077, 1, s1e1b},	/* %nw*,nw */
	{0177, 3, 077, 3, s1e1b},	/* %nb*,nb */
	{0177, 1, 077, 3, s1e1b},	/* %nw*,nb */
	{0177, 3, 077, 1, s1e1b},	/* %nb*,nw */
	{020, 8, 010, 0, s1f19},	/* %al,c */
	{0177, 8, 010, 0, s1f27},	/* %nl*,c */
	{0177, 8, 077, 8, s1e65},	/* %nl*,nl */
	{020, 4, 020, 4, s1f40},	/* %af,af */
	{020, 5, 020, 5, s1f40},	/* %ad,ad */
	{020, 4, 0177, 4, s1f65},	/* %af,nf* */
	{020, 5, 0177, 5, s1f65},	/* %ad,nd* */
	{020, 4, 077, 4, s1f90},	/* %af,nf */
	{020, 5, 077, 5, s1f90},	/* %ad,nd */
	{0177, 4, 077, 4, s1fae},	/* %nf*,nf */
	{0177, 5, 077, 5, s1fae},	/* %nd*,nd */
	{0}
};
static struct optab l3a2c[] = {
	{020, 1, 011, 1, s1fea},	/* %aw,rw */
	{0177, 1, 011, 1, s2007},	/* %nw*,rw */
	{020, 1, 020, 1, s202e},	/* %aw,aw */
	{020, 1, 077, 1, s2042},	/* %aw,nw */
	{0177, 1, 020, 1, s2059},	/* %nw*,aw */
	{0177, 1, 077, 1, s2077},	/* %nw*,nw */
	{020, 4, 077, 4, s2095},	/* %af,nf */
	{020, 5, 077, 5, s2095},	/* %ad,nd */
	{0177, 4, 077, 4, s20bc},	/* %nf*,nf */
	{0177, 5, 077, 5, s20bc},	/* %nd*,nd */
	{0}
};
static struct optab l3a6a[] = {
	{020, 1, 011, 1, s20f0},	/* %aw,rw */
	{0177, 1, 011, 1, s210e},	/* %nw*,rw */
	{020, 1, 020, 1, s2136},	/* %aw,aw */
	{020, 1, 077, 1, s214b},	/* %aw,nw */
	{0177, 1, 020, 1, s216c},	/* %nw*,aw */
	{0177, 1, 077, 1, s218b},	/* %nw*,nw */
	{020, 4, 020, 4, s21b3},	/* %af,af */
	{020, 5, 020, 5, s21b3},	/* %ad,ad */
	{020, 4, 0177, 4, s21da},	/* %af,nf* */
	{020, 5, 0177, 5, s21da},	/* %ad,nd* */
	{020, 4, 077, 4, s2207},	/* %af,nf */
	{020, 5, 077, 5, s2207},	/* %ad,nd */
	{0177, 4, 077, 4, s2227},	/* %nf*,nf */
	{0177, 5, 077, 5, s2227},	/* %nd*,nd */
	{0}
};
static struct optab l3ac0[] = {
	{020, 1, 011, 1, s2265},	/* %aw,rw */
	{0177, 1, 011, 1, s2283},	/* %nw*,rw */
	{020, 1, 020, 1, s22ab},	/* %aw,aw */
	{020, 1, 077, 1, s22c0},	/* %aw,nw */
	{0177, 1, 020, 1, s22e1},	/* %nw*,aw */
	{0177, 1, 077, 1, s2300},	/* %nw*,nw */
	{0}
};
static struct optab l3b78[] = {
	{020, 0, 04, 0, s27e0},	/* %a,z */
	{0177, 0, 04, 0, s27fe},	/* %n*,z */
	{020, 0, 05, 0, s2822},	/* %a,1 */
	{0177, 0, 05, 0, s2833},	/* %n*,1 */
	{020, 0, 020, 0, s284e},	/* %a,a */
	{020, 0, 0177, 0, s2868},	/* %a,n* */
	{020, 0, 077, 0, s2888},	/* %a,n */
	{0177, 0, 020, 0, s28a5},	/* %n*,a */
	{0177, 0, 0124, 0, s28c9},	/* %n*,e* */
	{0177, 0, 024, 0, s28f3},	/* %n*,e */
	{0177, 0, 077, 0, s291a},	/* %n*,n */
	{020, 8, 04, 0, s293d},	/* %al,z */
	{0177, 8, 04, 0, s2957},	/* %nl*,z */
	{020, 8, 05, 0, s2973},	/* %al,1 */
	{0177, 8, 05, 0, s2994},	/* %nl*,1 */
	{020, 8, 010, 0, s29c9},	/* %al,c */
	{020, 8, 020, 0, s29fe},	/* %al,a */
	{020, 8, 077, 0, s2a42},	/* %al,n */
	{0177, 8, 010, 0, s2a86},	/* %nl*,c */
	{0177, 8, 020, 0, s2adf},	/* %nl*,a */
	{0177, 8, 077, 0, s2b48},	/* %nl*,n */
	{020, 8, 077, 8, s2bb1},	/* %al,nl */
	{0177, 8, 077, 8, s2c0c},	/* %nl*,nl */
	{0}
};
static struct optab l3ae6[] = {
	{020, 0, 04, 0, s2328},	/* %a,z */
	{0177, 0, 04, 0, s2346},	/* %n*,z */
	{020, 0, 05, 0, s236a},	/* %a,1 */
	{0177, 0, 05, 0, s237b},	/* %n*,1 */
	{020, 0, 020, 0, s2396},	/* %a,a */
	{020, 0, 0177, 0, s23b0},	/* %a,n* */
	{020, 0, 077, 0, s23d0},	/* %a,n */
	{0177, 0, 020, 0, s23ed},	/* %n*,a */
	{0177, 0, 0124, 0, s2411},	/* %n*,e* */
	{0177, 0, 024, 0, s243b},	/* %n*,e */
	{0177, 0, 077, 0, s2462},	/* %n*,n */
	{020, 8, 04, 0, s2485},	/* %al,z */
	{077, 8, 04, 0, s24a0},	/* %nl,z */
	{0177, 8, 04, 0, s24bc},	/* %nl*,z */
	{020, 8, 05, 0, s24d8},	/* %al,1 */
	{0177, 8, 05, 0, s24f9},	/* %nl*,1 */
	{020, 8, 010, 0, s252e},	/* %al,c */
	{020, 8, 020, 0, s2563},	/* %al,a */
	{020, 8, 077, 0, s25a7},	/* %al,n */
	{0177, 8, 010, 0, s25eb},	/* %nl*,c */
	{0177, 8, 020, 0, s2644},	/* %nl*,a */
	{0177, 8, 077, 0, s26ad},	/* %nl*,n */
	{020, 8, 077, 8, s2716},	/* %al,nl */
	{0177, 8, 077, 8, s2771},	/* %nl*,nl */
	{0}
};
static struct optab l343e[] = {
	{020, 0, 077, 0, s11a4},	/* %a,n */
	{077, 0, 077, 0, s11aa},	/* %n,n */
	{0177, 0, 077, 0, s11b4},	/* %n*,n */
	{0}
};
static struct optab l3c04[] = {
	{077, 0, 077, 0, s2c7b},	/* %n,n */
	{0}
};
static struct optab l3c0c[] = {
	{077, 4, 077, 0, s2c89},	/* %nf,n */
	{077, 5, 077, 0, s2c89},	/* %nd,n */
	{0}
};
static struct optab l3c1a[] = {
	{077, 4, 077, 0, s2c96},	/* %nf,n */
	{077, 5, 077, 0, s2c96},	/* %nd,n */
	{0}
};
static struct optab l3c28[] = {
	{077, 8, 077, 0, s2ca3},	/* %nl,n */
	{0}
};
static struct optab l3c30[] = {
	{077, 9, 077, 0, s2cb9},	/* %nu,n */
	{077, 0, 077, 0, s2cc6},	/* %n,n */
	{0}
};
static struct optab l3c3e[] = {
	{020, 8, 077, 0, s2cce},	/* %al,n */
	{0177, 8, 077, 0, s2cd8},	/* %nl*,n */
	{0}
};
static struct optab l3c4c[] = {
	{077, 8, 077, 8, s2ce9},	/* %nl,nl */
	{0}
};
static struct optab l3c54[] = {
	{077, 0, 077, 8, s2cff},	/* %n,nl */
	{0}
};
static struct optab l35e8[] = {
	{020, 0, 077, 0, s172f},	/* %a,n */
	{0124, 0, 077, 0, s30bc},	/* %e*,n */
	{0177, 0, 077, 0, s1742},	/* %n*,n */
	{0}
};
static struct optab l3c5c[] = {
	{020, 0, 077, 0, s2d15},	/* %a,n */
	{0177, 0, 077, 0, s2d27},	/* %n*,n */
	{077, 0, 077, 0, s2d3f},	/* %n,n */
	{0}
};
static struct optab l3c70[] = {
	{020, 8, 077, 0, s2d53},	/* %al,n */
	{0177, 8, 077, 0, s2d61},	/* %nl*,n */
	{077, 8, 077, 0, s2d76},	/* %nl,n */
	{0}
};
static struct optab l3c84[] = {
	{011, 0, 077, 0, s2d87},	/* %r,n */
	{020, 0, 077, 0, s2d91},	/* %a,n */
	{0177, 0, 077, 0, s2d9f},	/* %n*,n */
	{077, 0, 077, 0, s2db3},	/* %n,n */
	{0}
};
static struct optab l3474[] = {
	{077, 0, 010, 0, s11ef},	/* %n,c */
	{0177, 0, 010, 0, s120d},	/* %n*,c */
	{0}
};
static struct optab l3482[] = {
	{077, 0, 010, 0, s1237},	/* %n,c */
	{0}
};
static struct optab l3d5e[] = {
	{020, 0, 05, 0, s2ed3},	/* %a,1 */
	{0177, 0, 05, 0, s2ed9},	/* %n*,1 */
	{011, 1, 020, 1, s2dc3},	/* %rw,aw */
	{011, 3, 020, 0, s2dc3},	/* %rb,a */
	{020, 1, 011, 1, s2dc3},	/* %aw,rw */
	{020, 3, 011, 3, s2dc3},	/* %ab,rb */
	{011, 1, 0177, 1, s2dca},	/* %rw,nw* */
	{011, 3, 0177, 0, s2dca},	/* %rb,n* */
	{0177, 1, 011, 1, s2dd7},	/* %nw*,rw */
	{0177, 3, 011, 0, s2dd7},	/* %nb*,r */
	{020, 1, 077, 1, s2de4},	/* %aw,nw */
	{020, 3, 077, 3, s2de4},	/* %ab,nb */
	{0124, 1, 077, 1, s2dee},	/* %ew*,nw */
	{0124, 3, 077, 0, s2dee},	/* %eb*,n */
	{0177, 1, 077, 1, s2dff},	/* %nw*,nw */
	{0177, 3, 077, 0, s2dff},	/* %nb*,n */
	{020, 8, 010, 0, s2e2b},	/* %al,c */
	{020, 8, 077, 8, s2e73},	/* %al,nl */
	{0124, 8, 077, 8, s2e84},	/* %el*,nl */
	{0177, 8, 077, 8, s2ea1},	/* %nl*,nl */
	{020, 4, 020, 4, s2ee5},	/* %af,af */
	{020, 5, 020, 5, s2ee5},	/* %ad,ad */
	{0177, 4, 020, 4, s2f1b},	/* %nf*,af */
	{0177, 5, 020, 5, s2f1b},	/* %nd*,ad */
	{0177, 4, 077, 4, s2f5e},	/* %nf*,nf */
	{0177, 5, 077, 5, s2f5e},	/* %nd*,nd */
	{0}
};
static struct optab l3cde[] = {
	{011, 1, 020, 1, s2dc3},	/* %rw,aw */
	{020, 1, 011, 1, s2dc3},	/* %aw,rw */
	{020, 3, 011, 0, s2dc3},	/* %ab,r */
	{011, 1, 0177, 1, s2dca},	/* %rw,nw* */
	{011, 3, 0177, 0, s2dca},	/* %rb,n* */
	{0177, 1, 011, 1, s2dd7},	/* %nw*,rw */
	{0177, 3, 011, 0, s2dd7},	/* %nb*,r */
	{020, 1, 077, 1, s2de4},	/* %aw,nw */
	{020, 3, 077, 0, s2de4},	/* %ab,n */
	{0124, 1, 077, 1, s2dee},	/* %ew*,nw */
	{0124, 3, 077, 0, s2dee},	/* %eb*,n */
	{0177, 1, 077, 1, s2dff},	/* %nw*,nw */
	{0177, 3, 077, 0, s2dff},	/* %nb*,n */
	{020, 8, 04, 0, s2e15},	/* %al,z */
	{020, 8, 010, 0, s2e2b},	/* %al,c */
	{0177, 8, 010, 0, s2e38},	/* %nl*,c */
	{020, 8, 0177, 9, s2e50},	/* %al,nu* */
	{020, 8, 077, 9, s2e63},	/* %al,nu */
	{020, 8, 077, 8, s2e73},	/* %al,nl */
	{0124, 8, 077, 8, s2e84},	/* %el*,nl */
	{0177, 8, 077, 8, s2ea1},	/* %nl*,nl */
	{0}
};
static struct optab l3dfc[] = {
	{020, 0, 04, 0, s2f9b},	/* %a,z */
	{0177, 0, 04, 0, s2fb1},	/* %n*,z */
	{020, 0, 05, 0, s2fc9},	/* %a,1 */
	{0177, 0, 05, 0, s2fd1},	/* %n*,1 */
	{020, 0, 020, 0, s2fdf},	/* %a,a */
	{020, 0, 0177, 0, s2ff0},	/* %a,n* */
	{020, 0, 077, 0, s3007},	/* %a,n */
	{0177, 0, 020, 0, s301b},	/* %n*,a */
	{0177, 0, 0124, 0, s3032},	/* %n*,e* */
	{0177, 0, 024, 0, s304f},	/* %n*,e */
	{0177, 0, 077, 0, s3069},	/* %n*,n */
	{0}
};
static struct optab l3e40[] = {
	{020, 0, 011, 0, s307f},	/* %a,r */
	{020, 0, 077, 0, s308f},	/* %a,n */
	{0177, 0, 011, 0, s30a2},	/* %n*,r */
	{0124, 0, 077, 0, s30bc},	/* %e*,n */
	{0177, 0, 024, 0, s30d9},	/* %n*,e */
	{0177, 0, 077, 0, s30f6},	/* %n*,n */
	{0}
};
static struct optab l40fe[] = {
	{077, 0, 077, 0, s3343},	/* %n,n */
	{0}
};
static struct optab l3ee6[] = {
	{010, 0, 077, 0, s3119},	/* %c,n */
	{020, 0, 077, 0, s312e},	/* %a,n */
	{0177, 0, 077, 0, s3138},	/* %n*,n */
	{077, 0, 077, 0, s3148},	/* %n,n */
	{0177, 8, 077, 0, s3156},	/* %nl*,n */
	{077, 8, 077, 0, s3177},	/* %nl,n */
	{077, 4, 077, 0, s3191},	/* %nf,n */
	{077, 5, 077, 0, s3191},	/* %nd,n */
	{0}
};
static struct optab l4060[] = {
	{020, 0, 05, 0, s32b1},	/* %a,1 */
	{0177, 0, 05, 0, s32b7},	/* %n*,1 */
	{011, 1, 020, 1, s2dc3},	/* %rw,aw */
	{011, 3, 020, 0, s2dc3},	/* %rb,a */
	{020, 1, 011, 1, s2dc3},	/* %aw,rw */
	{020, 3, 011, 3, s2dc3},	/* %ab,rb */
	{011, 1, 0177, 1, s2dca},	/* %rw,nw* */
	{011, 3, 0177, 0, s2dca},	/* %rb,n* */
	{0177, 1, 011, 1, s2dd7},	/* %nw*,rw */
	{0177, 3, 011, 0, s2dd7},	/* %nb*,r */
	{020, 1, 077, 1, s2de4},	/* %aw,nw */
	{020, 3, 077, 3, s2de4},	/* %ab,nb */
	{0124, 1, 077, 1, s2dee},	/* %ew*,nw */
	{0124, 3, 077, 0, s2dee},	/* %eb*,n */
	{0177, 1, 077, 1, s2dff},	/* %nw*,nw */
	{0177, 3, 077, 0, s2dff},	/* %nb*,n */
	{0}
};
static struct optab l40c2[] = {
	{077, 0, 077, 0, s32c3},	/* %n,n */
	{0}
};
static struct optab l3f18[] = {
	{011, 1, 010, 0, s31a4},	/* %rw,c */
	{011, 3, 010, 0, s31a4},	/* %rb,c */
	{020, 1, 010, 0, s31a4},	/* %aw,c */
	{020, 3, 010, 0, s31a4},	/* %ab,c */
	{0177, 0, 010, 0, s31ae},	/* %n*,c */
	{0177, 3, 010, 0, s31ae},	/* %nb*,c */
	{010, 0, 0177, 0, s31be},	/* %c,n* */
	{010, 0, 0177, 3, s31be},	/* %c,nb* */
	{077, 1, 020, 1, s1784},	/* %nw,aw */
	{077, 3, 020, 3, s1784},	/* %nb,ab */
	{077, 1, 0124, 1, s178e},	/* %nw,ew* */
	{077, 3, 0124, 3, s178e},	/* %nb,eb* */
	{077, 1, 024, 1, s179e},	/* %nw,ew */
	{077, 3, 024, 3, s179e},	/* %nb,eb */
	{077, 1, 0177, 1, s17aa},	/* %nw,nw* */
	{077, 3, 0177, 3, s17aa},	/* %nb,nb* */
	{077, 1, 077, 0, s17c0},	/* %nw,n */
	{077, 3, 077, 0, s17c0},	/* %nb,n */
	{0}
};
static struct optab l3f86[] = {
	{011, 1, 020, 1, s2dc3},	/* %rw,aw */
	{020, 1, 011, 1, s2dc3},	/* %aw,rw */
	{011, 3, 020, 3, s2dc3},	/* %rb,ab */
	{020, 3, 011, 3, s2dc3},	/* %ab,rb */
	{011, 0, 0177, 0, s2dca},	/* %r,n* */
	{020, 0, 0177, 0, s2dca},	/* %a,n* */
	{0177, 1, 011, 1, s2dd7},	/* %nw*,rw */
	{0177, 3, 011, 3, s2dd7},	/* %nb*,rb */
	{020, 1, 077, 1, s2de4},	/* %aw,nw */
	{020, 3, 077, 0, s2de4},	/* %ab,n */
	{011, 1, 077, 1, s2de4},	/* %rw,nw */
	{011, 3, 077, 0, s2de4},	/* %rb,n */
	{077, 1, 020, 0, s1784},	/* %nw,a */
	{077, 3, 020, 0, s1784},	/* %nb,a */
	{077, 1, 011, 0, s1784},	/* %nw,r */
	{077, 3, 011, 0, s1784},	/* %nb,r */
	{077, 1, 0124, 1, s178e},	/* %nw,ew* */
	{077, 3, 0124, 3, s178e},	/* %nb,eb* */
	{077, 1, 024, 1, s179e},	/* %nw,ew */
	{077, 3, 024, 3, s179e},	/* %nb,eb */
	{0177, 1, 077, 1, s2dff},	/* %nw*,nw */
	{0177, 3, 077, 3, s2dff},	/* %nb*,nb */
	{077, 1, 077, 1, s17c0},	/* %nw,nw */
	{077, 3, 077, 3, s17c0},	/* %nb,nb */
	{020, 8, 010, 0, s31ce},	/* %al,c */
	{0177, 8, 04, 0, s31df},	/* %nl*,z */
	{0177, 8, 010, 0, s31fc},	/* %nl*,c */
	{0177, 8, 020, 9, s31fc},	/* %nl*,au */
	{077, 8, 04, 0, s3218},	/* %nl,z */
	{077, 8, 010, 0, s322e},	/* %nl,c */
	{077, 8, 020, 9, s322e},	/* %nl,au */
	{077, 8, 020, 8, s3243},	/* %nl,al */
	{077, 8, 0124, 8, s3258},	/* %nl,el* */
	{077, 8, 077, 8, s3278},	/* %nl,nl */
	{077, 4, 077, 4, s329c},	/* %nf,nf */
	{077, 5, 077, 5, s329c},	/* %nd,nd */
	{0}
};
static struct optab l40d2[] = {
	{010, 0, 077, 0, s32c6},	/* %c,n */
	{020, 8, 077, 0, s32d6},	/* %al,n */
	{020, 1, 077, 0, s32f6},	/* %aw,n */
	{0177, 1, 077, 0, s32fd},	/* %nw*,n */
	{0177, 8, 077, 0, s330a},	/* %nl*,n */
	{077, 4, 077, 0, s3323},	/* %nf,n */
	{077, 5, 077, 0, s3323},	/* %nd,n */
	{0}
};

struct table regtab[] = {
	{106, l34a0},	/* load value */
	{123, l3498},	/* op 123 */
	{ 36, l348a},	/* indirect */
	{ 30, l34d2},	/* ++pre */
	{ 31, l34d2},	/* --pre */
	{ 32, l3522},	/* ++post */
	{ 33, l3522},	/* --post */
	{ 37, l3572},	/* - */
	{ 38, l3572},	/* ~ */
	{ 98, l345a},	/* call * */
	{ 99, l3452},	/* call2 */
	{ 80, l358c},	/* = */
	{ 40, l35fc},	/* + */
	{ 41, l35fc},	/* - */
	{ 42, l379c},	/* * */
	{ 43, l3822},	/* / */
	{ 14, l3822},	/* ptr->int */
	{ 44, l3822},	/* % */
	{ 45, l36f0},	/* >> */
	{ 46, l3746},	/* << */
	{ 47, l3694},	/* & */
	{ 48, l3694},	/* | */
	{ 49, l3694},	/* ^ */
	{ 70, l3878},	/* =+ */
	{ 71, l394c},	/* =- */
	{ 72, l3a2c},	/* =* */
	{ 73, l3a6a},	/* =/ */
	{ 74, l3ac0},	/* =% */
	{ 75, l3b78},	/* =>> */
	{ 76, l3ae6},	/* =<< */
	{ 77, l3878},	/* =& */
	{ 78, l3878},	/* =| */
	{ 79, l3878},	/* =^ */
	{102, l343e},	/* goto */
	{ 51, l3c04},	/* int->double */
	{ 52, l3c0c},	/* double->int */
	{ 56, l3c1a},	/* double->long */
	{ 57, l3c28},	/* long->double */
	{ 58, l3c30},	/* integer->long */
	{ 59, l3c3e},	/* long->integer */
	{ 91, l36f0},	/* << */
	{ 82, l3c4c},	/* * */
	{ 83, l3c4c},	/* / */
	{ 84, l3c4c},	/* % */
	{ 86, l3c54},	/* =* */
	{ 87, l3c54},	/* =/ */
	{ 88, l3c54},	/* =% */
	{ 16, l35e8},	/* field assign */
	{ 92, l3ae6},	/* long =<< */
	{ 17, l36f0},	/* >> */
	{ 18, l3b78},	/* >> */
	{107, l3c5c},	/* char->long */
	{108, l3c70},	/* long->char */
	{109, l3c84},	/* int<->char */
	{ 89, l3474},	/* *(x+c) */
	{122, l3482},	/* op 122 */
	{117, l3822},	/* udiv */
	{118, l3a6a},	/* =udiv */
	{119, l3822},	/* umod */
	{120, l3ac0},	/* =umod */
	{0, 0}
};

struct table efftab[] = {
	{ 30, l3d5e},	/* ++pre */
	{ 31, l3d5e},	/* --pre */
	{ 32, l3d5e},	/* ++post */
	{ 33, l3d5e},	/* --post */
	{ 80, l3cde},	/* = */
	{ 70, l3d5e},	/* =+ */
	{ 71, l3d5e},	/* =- */
	{ 77, l3cde},	/* =& */
	{ 78, l3cde},	/* =| */
	{ 79, l3cde},	/* =^ */
	{ 75, l3dfc},	/* =>> */
	{ 76, l3dfc},	/* =<< */
	{ 18, l3dfc},	/* >> */
	{ 16, l3e40},	/* field assign */
	{116, l40fe},	/*  (struct setup) */
	{0, 0}
};

struct table cctab[] = {
	{106, l3ee6},	/* load value */
	{ 30, l4060},	/* ++pre */
	{ 31, l4060},	/* --pre */
	{ 34, l40c2},	/* !un */
	{ 37, l40c2},	/* - */
	{ 38, l40c2},	/* ~ */
	{ 40, l40c2},	/* + */
	{ 41, l40c2},	/* - */
	{ 45, l40c2},	/* >> */
	{ 46, l40c2},	/* << */
	{ 47, l3f18},	/* & */
	{ 48, l40c2},	/* | */
	{ 49, l40c2},	/* ^ */
	{ 60, l3f86},	/* == */
	{ 61, l3f86},	/* != */
	{ 62, l3f86},	/* <= */
	{ 63, l3f86},	/* < */
	{ 64, l3f86},	/* >= */
	{ 65, l3f86},	/* > */
	{ 66, l3f86},	/* <p */
	{ 67, l3f86},	/* <=p */
	{ 68, l3f86},	/* >p */
	{ 69, l3f86},	/* >=p */
	{ 70, l4060},	/* =+ */
	{ 71, l4060},	/* =- */
	{ 75, l3dfc},	/* =>> */
	{ 76, l3dfc},	/* =<< */
	{ 18, l3dfc},	/* >> */
	{ 77, l3cde},	/* =& */
	{ 78, l3cde},	/* =| */
	{ 79, l3cde},	/* =^ */
	{0, 0}
};

struct table sptab[] = {
	{106, l40d2},	/* load value */
	{0, 0}
};
