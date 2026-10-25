/*
 *      x.out - Detail definitions of abs format
 */


/*      File format:
 *
 *      +---------------------+
 *      |   x.out header      |
 *      +---------------------+
 *      |   text              |
 *      +---------------------+
 *      |   data              |
 *      +---------------------+
 *      |   reloc table       |
 *      +---------------------+
 *      |   symbol table      |
 *      +---------------------+
 */



/*      relocation table entrys  */



struct srel86 {
	char    srtyp;          /* type of segment relocation  */
	char    srhib;          /* high byte of reloc address  */
	unsigned int srlow;     /* low  word of reloc address  */
};


#define R86_CTC 0               /* code reference to code */
#define R86_CTD 1               /* code reference to data */
#define R86_DTC 2               /* data reference to code */
#define R86_DTD 3               /* data reference to data */
