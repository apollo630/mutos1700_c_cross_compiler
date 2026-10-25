

GLD(1)H                                    MUTOS                             GLD(1)H



G
NAME H
     ld - Lader
G
U"BERSICHT H
     ld [ options ] file ...
G
BESCHREIBUNG H
     Der Lader GldH verbindet  mehrere Objektprogramme  zu   einem,  lo"st  externe
     Referenzen  auf  und  durchsucht  Bibliotheken.  Im einfachsten Fall werden
     mehrere Objekt-_f_i_l_e_s angegeben und GldH verbindet sie zu  einem  Objektmodul.
     Dieser ist entweder ausfu"hrbar oder die Eingabe fu"r einen weiteren GldH-Lauf.
     Die Ausgabe des erzeugten  Objektmoduls  oder  ausfu"hrbaren  Files  erfolgt
     durch GldH auf das File Ga.outH (mit der Option G-oH a"nderbar).  Dieses File wird
     nur dann ausfu"hrbar gemacht, wenn keine Fehler wa"hrend des Ladens auftraten
     und alle externen Referenzen durch den Ladevorgang aufgelo"st wurden.

     Die als Argumente angegebenen Objekt-Files werden in der  angegebenen  Rei-
     henfolge  verknu"pft.  Der Eintrittspunkt fu"r den Objektmodul ist der Beginn
     der ersten Routine.

     Wenn ein Argument eine Bibliothek ist, so wird  sie  genau  einmal  zu  dem
     Zeitpunkt bearbeitet, an dem sie in der Argumentliste aufgetreten ist.  Nur
     die Bibliotheksbestandteile, die eine unaufgelo"ste externe  Referenz  defi-
     nieren,  werden geladen.  Ruft eine Bibliotheks-Routine eine andere Routine
     aus derselben Bibliothek auf, so mup8 die aufgerufene hinter der aufrufenden
     Routine stehen.  Dies gilt nur, wenn nicht das Kommando GranlibH(1) auf diese
     Bibliothek angewandt wurde.  Daher ist die Reihenfolge der Programme in den
     Bibliotheken von grop8er Bedeutung.  Ist der erste Bestandteil einer Biblio-
     thek mit "__.SYMDEF" benannt, dann ist dieser  das  Inhaltsverzeichnis  der
     Bibliothek.   Ein  solches  Verzeichnis  wird durch GranlibH erzeugt. Es wird
     iterativ durchsucht, um so viel Referenzen wie mo"glich aufzulo"sen.

     Die Symbole "_etext", "_edata" und "_end" ("etext", "edata" und "end" in C)
     sind  reserviert.  Wird auf sie Bezug genommen, werden sie entsprechend vor
     dem Programmtext, vor den initialisierten Daten, oder den allgemeinen Daten
     eingefu"gt.  Der Anwender darf diese Symbole nicht definieren.

     Alle Optionen aup8er G-lH sollten vor  dem  File-Namen  erscheinen.   Mo"gliche
     Optionen sind:

     -s   Reduziert den Objektmodul,  indem  die  Symboltabelle  gelo"scht  wird.
          (Dies  verschlechtert aber die Handhabung des Debuggers.) Diese Infor-
          mationen ko"nnen auch durch GstripH(1) gelo"scht werden.

     -u   Das folgende Argument wird als Symbol verwendet, das  als  undefiniert
          in die Symboltabelle eingetragen wird. Dies ist ausschliep8lich fu"r das
          Laden einer Bibliothek nu"tzlich.  Mit Hilfe der unaufgelo"sten Referenz
          wird das Laden der ersten Routine realisiert.

     -l_x  Diese Option ist eine Abku"rzung fu"r Glib/libH _x G.aH , wobei _x  eine  Zei-
          chenkette  darstellt.  Falls diese Bibliothek nicht existiert, verwen-
          det GldH G/usr/lib/libH _x G.aH .  Eine Bibliothek wird durchsucht, wenn  ihr
          Name auftritt. Daher ist die Stelle, an der G-lH auftritt, entscheidend.

     -x   Lo"scht  lokale  (nicht-".globl")  Symbole  in  der  Symboltabelle  des
          Objektmoduls.  Es  werden nur externe Symbole eingetragen. Durch diese
          Option wird Speicherplatz eingespart.


                                         - G1H -                                  







GLD(1)H                                    MUTOS                             GLD(1)H



     -X   Alle lokalen Symbole, die mit "L" beginnen,  werden  gelo"scht.   Diese
          Option wird von GccH(1) verwendet, um intern generierte Marken zu besei-
          tigen, wobei lokale Symbole der Routinen beibehalten werden.

     -n   Wurde aus Kompatibilita"tsgru"nden zu anderen MUTOS-Systemen eingefu"hrt.
          Die  Nutzung  dieser  Option  fu"hrt  zur  Fehlerausschrift.  Der Lader
          bricht jedoch seine Arbeit nicht ab, sondern  behandelt  diese  Option
          wie G-iH.

     -i   Wenn das Objekt-File ausgefu"hrt wird,  werden  die  Text-  und  Daten-
          Segmente des Programms in getrennten Adrep8ra"umen untergebracht.

     -o   Das Argument nach G-oH wird vom GldH als Ausgabedatei anstelle  von  Ga.outH
          verwendet.

     -e   Der folgende Name ist  der  Eintrittspunkt  des  geladenen  Programms.
          Standard ist 0.

     -O   Dies ist ein  U"berlagerungs-File,  wobei  nur  das  Textsegment  durch
          GexecH(2)  ersetzt  wird.  Geteilte Daten mu"ssen denselben strukturellen
          Aufbau haben wie in dem Programm, das u"berlagert wird.

     -D   Das folgende Argument ist eine Dezimalzahl, die die Gro"p8e  des  Daten-
          segmentes festlegt.
G
FILES H
     /lib/lib*.a      Bibliotheken
     /usr/lib/lib*.a  weitere Bibliotheken
     a.out            Ausgabe-File
G
SIEHE AUCH H
     as(1), ar(1), cc(1), ranlib(1)
G
FEHLERQUELLEN H
     Der Lader gestattet nur den Aufbau ausfu"hrbarer Files mit 64K  Byte  Adrep8-
     raum  (bzw. je 64K Byte fu"r Befehle und Daten bei Option G-iH).  Es wird kein
     Fehler gemeldet, wenn der Text und/oder die Daten 64K Byte u"berschreiten.























                                         - G2H -                                  





