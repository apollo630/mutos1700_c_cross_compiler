

GLORDER(1)H                                MUTOS                         GLORDER(1)H



G
NAME H
     lorder - Finden von Ordnungsbeziehungen fu"r eine Objekt-Bibliothek
G
U"BERSICHT H
     lorder file ...
G
BESCHREIBUNG H
     Die Eingabe sind ein oder mehrere _f_i_l_e_s des Objekt- oder  Bibliotheksarchi-
     ves  (siehe  GarH(1)).   Die  Standardausgabe  ist  eine Liste von Paaren von
     Objekt-File-Namen.  Deren Bedeutung ist, dap8 das erste File externe Identi-
     fikatoren  entha"lt,  die in dem zweiten File definiert werden.  Die Ausgabe
     von GlorderH kann durch GtsortH(1) abgearbeitet werden, um die Bibliothek so zu
     ordnen, dap8 GldH(1) zu ihr in einem einzigen Durchlauf zugreifen kann.

     Die folgende Kommandozeile baut z.B. aus existierenden ".o"-Files eine neue
     Bibliothek auf.

          ar cr library 'lorder *.o | tsort`
G
FILES H
     *symref, *symdef
     nm(1), sed(1), sort(1), join(1)
G
SIEHE AUCH H
     tsort(1), ld(1), ar(1)
G
FEHLERQUELLEN H
     Die Namen von Objekt-Files mu"ssen, innerhalb und aup8erhalb  von  Bibliothe-
     ken, mit ".o" enden. Andernfalls entsteht Unsinn.






























                                         - G1H -                                  





