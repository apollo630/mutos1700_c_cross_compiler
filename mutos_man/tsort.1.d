

GTSORT(1)H                                 MUTOS                          GTSORT(1)H



G
NAME H
     tsort - Topologisches Sortieren
G
U"BERSICHT H
     tsort [ file ]
G
BESCHREIBUNG H
     Ausgehend von der im Eingabe-File _f_i_l_e vorgegebenen partiellen Ordnung  von
     Items  erzeugt GtsortH eine vollsta"ndig geordnetet Liste dieser Items.  Falls
     _f_i_l_e  nicht  angegeben  ist,  wird  die  Standardeingabe  als  Eingabe-File
     benutzt.

     Die Eingabe fu"r GtsortH besteht aus  Paaren  nichtleerer  Zeichenketten  (den
     Items), die durch Leerzeichen getrennt sind.  Paare aus verschiedenen Items
     legen eine Ordnung zwischen diesen Items fest.  Paare aus identischen Items
     kennzeichnen nur das Vorhandensein dieses Items.
G
SIEHE AUCH H
     lorder(1)
G
DIAGNOSE H
     Besteht das Eingabe-File aus einer ungeraden Anzahl von Zeichenketten, wird
     "odd data" ausgegeben.

     Eventuell auftretende Zyklen werden gekennzeichnet.
G
FEHLERQUELLEN H
     Als einziger Anwendungsfall ist das Herstellen  der  fu"r  ein  Bibliotheks-
     archiv notwendigen Ordnung gedacht.






























                                         - G1H -                                  





