

GNM(1)H                                    MUTOS                             GNM(1)H



G
NAME H
     nm - Ausgabe der Symboltabelle
G
U"BERSICHT H
     nm [ -gnopru ] [ file ... ]
G
BESCHREIBUNG H
     GNmH druckt die Symboltabelle von jedem Objekt-File  _f_i_l_e  in  der  Argument-
     liste.   Ist  ein Argument ein Archiv, so wird eine Liste fu"r jedes Objekt-
     File erzeugt.  Wird kein _f_i_l_e angegeben,  so  wird  die  Symboltabelle  von
     Ga.outH ausgegeben.

     Jeder Symbolname wird durch seinen Wert  (Leerzeichen,  falls  undefiniert)
     und  einen  der Buchstaben GUH (undefiniert), GAH (absolut), GTH (Textsegmentsym-
     bol), GDH (Datensegmentsymbol), GBH (Bss-Segmentsymbol), oder GCH (Common Symbol)
     dargestellt.   Wenn  das  Symbol lokal (nicht extern) ist, wird ein kleiner
     Buchstabe ausgegeben.  Die Ausgabe wird alphabetisch sortiert.

     Die Optionen sind:

     -g   Gibt nur globale (externe) Symbole aus.

     -n   Sortiert numerisch anstatt alphabetisch.

     -o   Schreibt vor jede Ausgabezeile den File- oder Archivelement-Namen.

     -p   Es wird nicht sortiert, sondern  in  Symbol-Tabellen-Reihenfolge  aus-
          gegeben.

     -r   Sortiert in umgekehrter Reihenfolge.

     -u   Gibt nur undefinierte Symbole aus.
G
SIEHE AUCH H
     ar(1), ar(5), a.out(5)
























                                         - G1H -                                  





