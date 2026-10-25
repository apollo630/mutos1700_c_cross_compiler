

GRANLIB(1)H                                MUTOS                         GRANLIB(1)H



G
NAME H
     ranlib - Herstellung von Bibliotheken mit direktem Zugriff
G
U"BERSICHT H
     ranlib  archive ...
G
BESCHREIBUNG H
     Das Kommando GranlibH konvertiert Bibliotheks-Files in  eine  Form,  die  vom
     Lader  wesentlich  schneller  abgearbeitet  werden  kann.  Der Grundgedanke
     besteht darin, der Bibliothek ein Inhaltsverzeichnis _.SYMDEF  zu  stellen,
     das den Platz des Files in dieser Bibliothek genau angibt.

     Das Kommando GranlibH benutzt GarH(1) zur Rekonstruktion  der  Bibliothek.  Das
     bedeutet,  es mup8 im File-System, in dem sich die aktuelle Directory befin-
     det, genug tempora"rer Speicherplatz vorhanden sein.
G
SIEHE AUCH H
     ld(1), ar(1)
G
FEHLERQUELLEN H
     Da die Generierung einer Bibliothek  mit  Gar(1)H  und  die  Herstellung  des
     Direktzugriffes  mit  GranlibH  getrennte  Prozesse sind, ko"nnen Phasenfehler
     auftreten. Der Lader GldH(1) gibt eine Warnung aus, wenn  das  Modifikations-
     datum  einer Bibliothek ju"nger ist als das Datum der Erzeugung der entspre-
     chenden Directory.  Allerdings wird diese Warnung selbst  dann  ausgegeben,
     wenn die Bibliothek nur kopiert wurde.

































                                         - G1H -                                  





