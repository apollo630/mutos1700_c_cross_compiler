

GAS(1)H                                 MUTOS 1700                           GAS(1)H



G
NAME H
     as,asf - Assembler
G
U"BERSICHT H
     as [-LW] [-o output] file ...
     asf [-LW] [-o output] file ...
G
BESCHREIBUNG H
     Die Eingabe des zu assemblierenden  Quellprogramms  erfolgt  entweder  u"ber
     Standardeingabe,  oder  es  werden die miteinander verketteten _f_i_l_e_s als zu
     assemblierendes Quellprogramm eingegeben.

     Mit G-oH _o_u_t_p_u_t wird veranlap8t, dap8 die Ausgabe in das File  _o_u_t_p_u_t  erfolgt.
     Wird  diese  Option  nicht angegeben, erfolgt die Ausgabe in das File Ga.outH
     der aktuellen Directory.  Wenn es keine unaufgelo"sten Referenzen  nach  der
     Assemblierung  gibt  und  keine Fehler erkannt wurden, wird das ausgegebene
     File (_o_u_t_p_u_t oder Ga.outH) mit dem Attribut ausfu"hrbar (executable) markiert,
     anderenfalls  als  nicht  ausfu"hrbar.   Bestehen  noch unaufgelo"ste externe
     Referenzen, mup8 eine weitere Verarbeitung durch den Lader GldH(1) erfolgen.

     Wenn G-LH angegeben wird, werden Marken, die mit dem Buchstaben  GLH  beginnen,
     in  die  vom  Assembler ausgegebene Symboltabelle eingetragen. Wird kein G-LH
     angegeben, werden diese Marken nur intern vom Assembler  gefu"hrt,  und  sie
     erscheinen  nicht  in der Symboltabelle.  Die vom C-Compiler erzeugten, fu"r
     das File lokalen Charakter tragenden Marken beginnen alle mit GLH.

     Mit G-WH werden alle Fehlermeldungen des Assemblers ausgeschaltet.

     Fu"r die U"bersetzung von Programmen, die Befehle fu"r den Arithmetikprozessor
     K 1810 WM 87 enthalten, ist der Assembler GasfH aufzurufen.
     Die Erzeugung von lauffa"higen Programmen mittels GasfH ist  nur  bei  Vorhan-
     densein eines Arithmetikprozessors mo"glich.
G
FILES H
     /lib/as        Assembler
     /lib/asf       Assembler fu"r K 1810 WM 87-Befehle
     /tmp/tmp[1-3]  tempora"re Files
     a.out          Objekt-File
G
SIEHE AUCH H
     ld(1), nm(1), adb(1), a.out(5)
     PtB Teil 2, Sprachbeschreibung
G
DIAGNOSE H
     Die Fehlermeldungen sind selbsterkla"rend.  Fehler im 1. Pass verhindern die
     Abarbeitung des 2. Passes.













3/89                                     - G1H -                                  





