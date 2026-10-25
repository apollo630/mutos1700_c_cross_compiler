

GAR(1)H                                    MUTOS                             GAR(1)H



G
NAME H
     ar - Archiv- und Bibliothekspflege
G
U"BERSICHT H
     ar key [ posname ] afile name ...
G
BESCHREIBUNG H
     Das Kommando GarH behandelt Gruppen von Files, die zu einem Archiv-File _a_f_i_l_e
     zusammengefap8t  sind.  Es wird hauptsa"chlich zur Erstellung und Aktualisie-
     rung von Bibliotheken verwendet, die vom Lader  genutzt  werden.   Es  kann
     natu"rlich auch fu"r andere, a"hnliche Aufgaben verwendet werden.

     Das Schlu"sselzeichen _k_e_y ist ein Zeichen aus der Menge GdrqtpmxH,  mo"glicher-
     weise  in Verbindung mit einem oder mehreren Zeichen aus der Menge GvuaibclH.
     Mit _n_a_m_e werden im Archiv-File enthaltene oder hinzuzufu"gende  Files  ange-
     geben.

     Die Schlu"sselzeichen _k_e_y haben folgende Bedeutung:

     d    Lo"schen des genannten Files aus dem Archiv-File.

     GrH    Ersetzen des genannten Files im Archiv-File.
          Wird r zusammen mit GuH verwendet, werden nur die Files  ersetzt,  deren
          Daten  nach  der  Erstellung des Archiv-Files noch modifiziert wurden.
          Wird eines der Positionszeichen GabiH verwendet, mup8 das  _p_o_s_n_a_m_e  Argu-
          ment  vorhanden  sein.  Es gibt an, dap8 neue Files hinter (GaH) oder vor
          (GbH oder GiH) _p_o_s_n_a_m_e zu plazieren sind. Anderenfalls  werden  die  neuen
          Files am Ende angefu"gt.

     q    Rasches Anfu"gen der genannten Files  an  das  Ende  des  Archiv-Files.
          Eventuelle  Positionszeichen  sind ungu"ltig. Das Kommando pru"ft nicht,
          ob sich die anzufu"genden Files schon im Archiv-File befinden.   Aller-
          dings  ist es sinnvoll, diesen Mehraufwand zu vermeiden, indem man ein
          Archiv schrittweise erstellt.

     t    Ausgeben des Inhaltsverzeichnisses eines Archiv-Files.
          Sind keine _n_a_m_e_n genannt, werden alle Files des  Archivs  aufgelistet,
          sind _n_a_m_e_n angegeben, werden nur diese Files aufgelistet.

     p    Der Inhalt der genannten Files wird ausgegeben (Standardausgabe).

     m    Die genannten Files werden an das Archivende  gebracht.   Falls  Posi-
          tionsparameter angegeben sind, mup8 das _p_o_s_n_a_m_e Argument vorhanden sein
          und wie bei GrH gibt es an, wohin die Files bewegt werden.  (Umsortieren
          des Archiv-Files)

     x    Kopieren des genannten Files in eine  Directory.   Falls  keine  _n_a_m_e_n
          angegeben  sind,  werden alle Files kopiert.  (Sie stehen dann einzeln
          in der aktuellen Directory und ko"nnen dort  beispielsweise  korrigiert
          werden).  Das Archiv-File wird durch x nicht vera"ndert.

     v    Diese Option wird nur in Verbindung mit den  Schlu"sselparametern  ver-
          wendet.   Sie  gibt  noch einmal explizit aus, welche Files in welcher
          Form von der aktuellen Archiv-Option betroffen sind (z.B. "d - file1",
          falls file1 gelo"scht wurde ).  Wird sie zusammen mit GtH verwendet, lie-
          fert sie eine ausfu"hrliche Liste  sa"mtlicher  File-Informationen,  wie
          Erstellungsdatum,  Zugriffsrecht  u.s.w.   Wird  sie  zusammen  mit  GpH


                                         - G1H -                                  







GAR(1)H                                    MUTOS                             GAR(1)H



          verwendet, wird bei der Ausgabe jedem File sein Name vorangestellt.

     c    Normalerweise ero"ffnet GarH selbst ein Archiv-File, wenn es  noch  nicht
          vorhanden ist und beno"tigt wird.  Daru"ber erfolgt eine Information (cr
          -afile ).  Wird nun Option GcH verwendet, wird  diese  Nachricht  unter-
          dru"ckt.

     l    Normalerweise bringt GarH seine tempora"ren Files in die Directory  G/tmpH.
          Diese Option ermo"glicht es, sie in die lokale Directory zu bringen.
G
FILES H
     /tmp/v*   tempora"re Files
G
SIEHE AUCH H
     ld(1), ar(5), lorder(1)
G
FEHLERQUELLEN H
     Wenn das gleiche File mehrfach in der Argumentliste auftritt, kann es  pas-
     sieren, dap8 es mehrfach in das Archiv-File gebracht wird.









































                                         - G2H -                                  





