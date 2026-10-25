/* machine dependent stuff for core files */

#define TXTRNDSIZ 4096L
#define stacktop(siz) (0x10000L)
#define stackbas(siz) (0x10000L-siz)
