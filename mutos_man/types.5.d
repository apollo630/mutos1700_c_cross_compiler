

GTYPES(5)H                                 MUTOS                          GTYPES(5)H



G
NAME H
     types - Einfache Systemdatentypen
G
U"BERSICHT H
     #include <sys/types.h>
G
BESCHREIBUNG H
     Die Datentypen, die in dem Include-File definiert sind,  werden  im  MUTOS-
     System-Code benutzt.  Einige dieser Datentypen sind dem Nutzer zuga"nglich:

     typedef   long           daddr_t;       /* disk address */
     typedef   char *         caddr_t;       /* core address */
     typedef   unsigned int   ino_t;         /* i-node number */
     typedef   long           time_t;        /* a time */
     typedef   int            label_t[6];    /* program status */
     typedef   int            dev_t;         /* device code */
     typedef   long           off_t;         /* offset in file */
          /* selectors and constructor for device code */
     #define   major(x)       (int)(((unsigned)x>>8))
     #define   minor(x)       (int)(x&0377)
     #define   makedev(x,y)   (dev_t)((x)<<8|(y))

     Das Format Gdaddr_tH wird im allgemeinen fu"r Plattenspeicheradressen benutzt.
     Eine  Ausnahme bildet die Inode (siehe GfilsysH(5)).  Zeiten werden in Sekun-
     den entschlu"sselt, ab 1. Januar 1970, 00:00:00 Uhr  Weltzeit.   Der  Major-
     Teil  des  Gera"tecodes  spezifiziert  die  Art und der Minor-Teil die Unit-
     Nummer des Gera"tes  und  ist  installationsabha"ngig.   Offsets  werden  vom
     Beginn  eines  Files  aus in Bytes gemessen.  Die Variablen vom Typ Glabel_tH
     werden zum Retten des Prozessor-Status benutzt.
G
SIEHE AUCH H
     filsys(5), time(2), lseek(2), adb(1)



























                                         - G1H -                                  





