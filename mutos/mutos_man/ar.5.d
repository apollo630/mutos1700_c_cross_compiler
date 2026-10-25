

GAR(5)H                                    MUTOS                             GAR(5)H



G
NAME H
     ar - Archiv- (Bibliotheks-) File-Format
G
U"BERSICHT H
     #include <ar.h>
G
BESCHREIBUNG H
     Das Archiv-Kommando GarH(1) wird benutzt, um verschiedene  Files  miteinander
     zu  kombinieren.  Archive werden hauptsa"chlich als Bibliotheken zur Weiter-
     verwendung durch den Lader GldH(1) verwendet.

     Ein mit dem Kommando GarH hergestelltes File beginnt mit einer  Magic-Nummer,
     gefolgt  von  den enthaltenen Files.  Jedem Einzel-File ist ein Header vor-
     angestellt.  Die Magic-Nummer und das Header-Format sind im  entsprechenden
     Include-File definiert:

          #define ARMAG 0177545
          struct  ar_hdr {
                  char  ar_name[14];
                  long  ar_date;
                  char  ar_uid;
                  char  ar_gid;
                  int   ar_mode;
                  long  ar_size;
          };

     Der Name ist eine mit 0 beendete Zeichenkette; das Datum hat  das  von  dem
     Systemruf  GtimeH(2)  bereitgestellte  Format.  Nutzer-ID und Gruppen-ID sind
     Zahlen; der Modus ist ein Bitmuster entsprechend  dem  Systemruf  GchmodH(2);
     die Gro"p8e (size) wird in Bytes angegeben.

     Jedes File beginnt auf einer Wortgrenze.  Ein 0-Byte wird, falls  erforder-
     lich,  zwischen  den  Files  eingefu"gt.   Die Gro"p8e des Files wird ohne dem
     eventuellen Auffu"llungszeichen angegeben.
G
SIEHE AUCH H
     ar(1), ld(1), nm(1)
G
FEHLERQUELLEN H
     Die Kodierung der Nutzer- und Gruppen-ID mit Zeichen fu"hrt zu Fehlern.



















                                         - G1H -                                  





