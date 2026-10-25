

GCC(1)H                                 MUTOS 1700                           GCC(1)H



G
NAME H
     cc  - C-Compiler
G
U"BERSICHT H
     cc [ options ] file ...
G
BESCHREIBUNG H
     Das Programm GccH steuert die U"bersetzung von in der Sprache C (oder  in  der
     Assemblersprache)  geschriebenen  Programmen.   Es  akzeptiert verschiedene
     Typen von Argumenten.

     Argumente, deren Namen mit ".c" enden, kennzeichnen C-Quellprogramme. Diese
     C-Quellprogramme  werden  compiliert. Das Objektprogramm wird in einem File
     abgelegt, dessen Name aus dem Namen der Quelle besteht, aber  anstatt  ".c"
     steht ".o". In vielen Fa"llen wird dieses ".o"-File gelo"scht, da ein einzel-
     nes C-Programm in einem Schritt compiliert und geladen wird.

     In der gleichen Weise wird von Argumenten,  deren  Namen  mit  ".s"  enden,
     angenommen,  dap8  es Assembler-Quell-Programme sind. Sie werden assembliert
     und ergeben ebenfalls ein File mit dem Suffix ".o".

     Die folgenden Optionen werden durch GccH  interpretiert  (siehe  auch  GldH(1),
     Load-Time-Optionen):

     -c      Unterdru"ckt die Lade-Phase bei  der  Compilation.  Ein  Objekt-File
             wird erzeugt.

     -p      Erzeugt Code, der za"hlt, wie  oft  eine  Routine  aufgerufen  wird.
             Wa"hrend  des  Ladens  wird  die  Standard-Start-Routine  durch eine
             andere ersetzt, die automatisch GmonitorH(3) aufruft und  arrangiert,
             dap8  ein  Gmon.outH-File  bei der Ausfu"hrung des Objektprogramms aus-
             geschrieben wird. Eine abarbeitbare Form kann  man  mit  Hilfe  von
             GprofH(1) generieren.

     -O      Der Objekt-Code-Optimierer wird wa"hrend der Compilation aufgerufen.

     -S      Das angegebene C-Programm wird compiliert und die Assembler-Sprach-
             Ausgabe einem entsprechenden File mit dem Suffix ".s" u"bergeben.

     -P      Nur der Macro-Pra"prozessor wird abgearbeitet, das Ergebnis befindet
             sich  in einem File, das anstatt mit ".c" mit ".i" endet. In diesem
             File gibt es keine Zeilen, die mit "#" beginnen.

     -E      Nur der Macro-Pra"prozessor wird abgearbeitet. Das Ergebnis wird der
             Standardausgabe u"bergeben. Diese Ausgabe ist fu"r Compiler-Pru"fungen
             vorgesehen und wird durch GccH nicht als Eingabe akzeptiert.

     -o _o_u_t_p_u_t
             Das Ausgabe-File wird _o_u_t_p_u_t genannt. Wenn diese  Option  verwendet
             wird,  bleibt das File Ga.outH erhalten. Wird _o_u_t_p_u_t nicht angegeben,
             wird ein File erzeugt, dessen Name aus dem ersten Teil  des  Quell-
             File-Namens ohne die Suffixe ".c", ".o" oder ".s" besteht.

     -D_n_a_m_e=_d_e_f
     -D_n_a_m_e  Definiert    _n_a_m_e    fu"r    den    Pra"prozessor,    analog    einer
             G#defineH-Anweisung.  Wenn keine Definition angegeben wird, dann wird
             _n_a_m_e als 1 definiert.


3/89                                     - G1H -                                  







GCC(1)H                                 MUTOS 1700                           GCC(1)H



     -U_n_a_m_e  Entfernt eine Initial-Definition von _n_a_m_e.

     -I_d_i_r   G#includeH-Files, deren Namen nicht mit "/"  beginnen,  werden  immer
             zuerst  in  der  Directory  des _f_i_l_e-Arguments gesucht, dann in den
             Directories, die in den Optionen G-IH mittels _d_i_r genannt werden  und
             schliep8lich in den Directories der Standard-Liste.

     -f      Dieser Schalter bewirkt, dap8 vom C-Compiler Assembler-Code  erzeugt
             wird, der den Befehlssatz des Arithmetikprozessors des K 1810 WM 87
             einschliep8t.  Bei Angabe dieser Option werden die Compilerpa"sse c0f
             und  c1f  aufgerufen,  danach  der  Assembler GasfH, der Code fu"r den
             K 1810 WM 87 erzeugt.  Schliep8lich wird die Bibliothek  G/lib/libf.aH
             vor  der C-Standardbibliothek geladen.  Ohne diesen Schalter werden
             fu"r alle Gleitkommaoperationen Simulationsroutinen eingebunden.

     -B_s_t_r_i_n_g
             Sucht entsprechende Compilerpa"sse in Files, die  durch  _s_t_r_i_n_g  mit
             den  Endungen  GcppH, Gc0H, Gc1H und Gc2H benannt werden.  Ist _s_t_r_i_n_g leer,
             so wird eine Standardversion verwendet.

     -t[p012]
             Findet nur die angegebenen Compilerpa"sse in den Files, deren  Namen
             durch  eine  Option  G-BH  konstruiert werden. Ist diese Option nicht
             angegeben, so wird fu"r _s_t_r_i_n_g "/usr/c/" verwendet.

     Von anderen Argumenten wird vorausgesetzt, dap8 es sich entweder  um  Lader-
     Optionen, um C-kompatible Objektprogramme, die durch einen fru"heren GccH-Lauf
     erzeugt wurden, oder um Bibliotheken von  C-kompatiblen  Routinen  handelt.
     Diese  Programme werden zusammen mit den Ergebnissen von jeder spezifizier-
     ten Compilation in der angegebenen Reihenfolge zu einem  ausfu"hrbaren  Pro-
     gramm geladen, welches den Namen Ga.outH erha"lt.

G
FILES H
     file.c        Eingabe-File
     file.o        Objekt-File
     a.out         geladener Objektmodul
     /tmp/ctm?     tempora"re Files
     /lib/cpp      Pra"prozessor
     /lib/c[01]    Compiler
     /lib/c[01]f   Compiler fu"r K 1810 WM 87-Befehle
     /usr/c/oc[012]Backup-Compiler
     /usr/c/ocpp   Backup-Pra"prozessor
     /lib/c2       Optimierer
     /lib/crt0.o   Laufzeitstartroutine
     /lib/mcrt0.o  Startroutine fu"r die Formatierung
     /lib/libc.a   Standardbibliothek, siehe GintroH(3)
     /lib/libf.a   Routinen zur Einbindung des K 1810 WM 87
     /usr/include  Standard-Directory fu"r #include-Files
G
SIEHE AUCH H
     monitor (3 ), prof (1 ), adb (1 ), ld (1 )
G
FEHLERQUELLEN H
     Die vom C-Compiler gelieferten Fehlermeldungen sind selbsterkla"rend.





3/89                                     - G2H -                                  





