

GMAKE(1)H                                  MUTOS                           GMAKE(1)H



G
NAME H
     make - Verwaltung von Programmgruppen
G
U"BERSICHT H
     make [ -f makefile ] [ options ] ...  file ...
G
BESCHREIBUNG H
     Das Kommando GmakeH fu"hrt Kommandos aus, die in einem _m_a_k_e_f_i_l_e stehen, um ein
     oder  mehrere _n_a_m_e_n auf den neuesten Stand zu bringen.  Ein _n_a_m_e ist norma-
     lerweise ein Programm.  Wenn die Option  G-fH  nicht  angegeben  ist,  werden
     _m_a_k_e_f_i_l_e und _M_a_k_e_f_i_l_e nacheinander abgearbeitet.  Wird als _m_a_k_e_f_i_l_e ein "-"
     angegeben, so wird die Standardeingabe genommen.  Es ko"nnen mehrere  Optio-
     nen G-fH auftreten.

     Durch GmakeH wird ein File hergestellt, falls seit seiner letzten  Erstellung
     die Files, von denen es abha"ngt, modifiziert wurden.

     Das _m_a_k_e_f_i_l_e entha"lt eine Folge von Eintragungen, die  Abha"ngigkeiten  zwi-
     schen  Files  spezifizieren.   Die erste Zeile dieser Eintragungen ist eine
     durch Leerzeichen getrennte Liste von Zielen, hinter denen ein  Doppelpunkt
     und eine Liste der dafu"r erforderlichen Files steht.  Text, dem ein Semiko-
     lon folgt und alle folgenden Zeilen, die mit einem Tabulator beginnen, sind
     Shell-Kommandos,  die  abgearbeitet  werden,  um  das Ziel auf den neuesten
     Stand zu bringen.

     Zwischen Nummernzeichen "#" und Newline <NL> stehen Kommentare.

     Die folgenden Zeilen seien in einem _m_a_k_e_f_i_l_e enthalten. Sie beinhalten  die
     Informationen,  dap8  pgm  von  zwei  Files, Ga.oH und Gb.oH, abha"ngt, und diese
     ihrerseits von den entsprechenden G.cH-Files und einem gemeinsamen File GinclH.

          pgm: a.o b.o
               cc a.o b.o -lm -o pgm
          a.o: incl a.c
               cc -c a.c
          b.o: incl b.c
               cc -c b.c

     Zeilen der Form

          string1 = string2

     sind Makro-Definitionen,  die  so  im  _m_a_k_e_f_i_l_e  angegeben  werden  ko"nnen.
     Erscheint  im Text nach der Definition die Zeichenfolge $(_s_t_r_i_n_g_1), so wird
     diese durch _s_t_r_i_n_g_2 ersetzt.  Wenn _s_t_r_i_n_g_1 ein einzelnes Zeichen ist,  ko"n-
     nen die Klammern weggelassen werden.

     Von GmakeH werden selbst Vorbedingungen fu"r solche  Files  abgeleitet,  deren
     Konstruktionskommandos  im  _m_a_k_e_f_i_l_e nicht angegeben sind.  Fu"r ein G.oH-File
     kann man z.B. das entsprechende G.cH-File voraussetzen, da man durch die Com-
     pilierung des G.cH-Files das G.oH-File erha"lt.  Dadurch verku"rzt sich das obige
     Beispiel:

          pgm: a.o b.o
               cc a.o b.o -lm -o pgm
          a.o b.o: incl



                                         - G1H -                                  







GMAKE(1)H                                  MUTOS                           GMAKE(1)H



     Die Vorbedingungen werden anhand entsprechender  File-Suffixe  hergeleitet.
     Die  Suffixe  sind  unter dem besonderen Namen .SUFFIXES aufgelistet.  Sind
     mehrere solche Listen vorhanden, werden sie aneinandergereiht.  Eine  leere
     Liste  lo"scht das, was bisher in solchen Suffix-Listen angegeben wurde.  In
     diesen Listen ist die Reihenfolge der Suffixe entscheidend. Der erste  mo"g-
     liche  Name,  fu"r den sowohl ein File als auch eine Regel (wie weiter unten
     beschrieben) existiert, wird abgeleitet.  Die Standardliste ist:

          .SUFFIXES: .out .o .c .e .r .f .y .l .s

     Um ein File mit dem Suffix _s_2 herzustellen, welches von einem File mit  dem
     Suffix  _s_1  abha"ngt, wird eine Eintragung fu"r das "Ziel" _s_1_s_2 spezifiziert.
     In solchen Eintragungen ko"nnen Sondermakros verwendet werden.   Diese  Son-
     dermakros  sind  G$*H fu"r den Zielnamen ohne Suffix, G$@H fu"r den vollsta"ndigen
     Zielnamen, G$<H fu"r die komplette Liste der Vorbedingungen  und  G$?H  fu"r  die
     Liste  der  veralteten Vorbedingungen.  Zum Beispiel ist eine Regel fu"r die
     Herstellung optimierter ".o"-Files aus ".c"-Files

          .c.o: ; cc -c -O -o $@ $*.c

     Fu"r bestimmte U"bersetzerprogramme gibt es Makros, um standardma"p8ig Optionen
     an  diese  U"bersetzer  zu u"bergeben.  Insbesondere werden die Makros CFLAGS
     fu"r GcH(1)-Optionen, LFLAGS und YFLAGS fu"r GlexH- und GyaccH(1)-Optionen  bereit-
     gestellt.

     Kommandozeilen werden einzeln abgearbeitet, jede durch ihre  eigene  Shell.
     Eine  Zeile  wird  ausgegeben,  wenn  sie  ausgefu"hrt wird, falls nicht das
     besondere Ziel .SILENT im _m_a_k_e_f_i_l_e steht, oder "@" das  erste  Zeichen  des
     Kommandos ist.

     Kommandos, die einen Ru"ckkehrstatus ungleich Null (siehe  GintroH(1))  haben,
     veranlassen GmakeH , die Abarbeitung zu beenden. Der Abbruch geschieht nicht,
     wenn im _m_a_k_e_f_i_l_e der spezielle Name oder  das  entsprechende  Kommando  mit
     <HT>"-" beginnt.

     Ein Interrupt und ein Quit-Signal veranlassen, dap8 das Ziel gelo"scht  wird,
     sofern das Ziel nicht von dem besonderen Namen .PRECIOUS abha"ngt.

     Weitere Optionen:

     -i   Entspricht der besonderen Eintragung .IGNORE:.

     -k   Hat ein Kommando einen Ru"ckkehrwert ungleich Null, so wird die  Arbeit
          nach  dem aktuellen Eintrittspunkt abgebrochen.  Die Zweige, die nicht
          von diesem Eintrittspunkt abha"ngen, werden weiterbearbeitet.

     -n   Die Kommandozeilen, die fu"r die Erneuerung des Ziels beno"tigt  werden,
          werden auf dem Terminal ausgeschrieben, aber nicht abgearbeitet.

     -t   Erneuert das Datum der angegeben Ziel-Files, ohne irgendwelche Komman-
          dos auszufu"hren.

     -r   Entspricht dem Eintrittspunkt .SUFFIXES: ohne Liste.

     -s   Entspricht dem besonderen Eintrittspunkt .SILENT:.
G
FILES H
     makefile, Makefile


                                         - G2H -                                  







GMAKE(1)H                                  MUTOS                           GMAKE(1)H



G
SIEHE AUCH H
     sh(1), touch(1)
G
FEHLERQUELLEN H
     Einige Kommandos ko"nnen einen Ru"ckkehrwert ungleich Null  haben.   Um  ent-
     sprechenden Schwierigkeiten aus dem Wege zu gehen, sollte man die Option G-iH
     verwenden.

     Kommandos, die direkt durch Shell ausgefu"hrt werden (bemerkenswert  hierbei
     ist GcdH(1)), u"bergehen die Newlines in Gmake.H

















































                                         - G3H -                                  





