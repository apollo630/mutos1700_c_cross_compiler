

GED(1)H                                 MUTOS 1700                           GED(1)H



G
NAME H
     ed - Texteditor
     em - Erweiterter Texteditor
G
U"BERSICHT H
     ed [ - ] [ -x ] [ name ]
     em [ - ] [ -x ] [ -s ] [ -l ] [ -e ] [ -r ] [name]
G
BESCHREIBUNG H
     Der Editor GedH ist der Standardeditor; GemH ist ein erweiterter Texteditor mit
     einigen    zusa"tzlichen    Kommandos.    Hervorzuheben   sind   dabei   der
     "recovering"-Mechanismus, das Editieren im Bildschirmmodus und  die  Unter-
     stu"tzung  von  8  Bit-Code  (z.B.  Umlaute).  Die folgende Liste beinhaltet
     sowohl die Kommandos des GedH als auch die des Gem.H Siehe dazu  ANHANG  1  fu"r
     den Editor GemH und ANHANG 2 fu"r seinen Bildschirmmodus.

     Wenn das Argument  _n_a_m_e  angegeben  wurde,  simuliert  der  Editor  GedH  ein
     GeH-Kommando  fu"r  das  bezeichnete  File. Das bedeutet, das File wird in den
     Editorpuffer gelesen und kann dort bearbeitet werden.  Wurde G-xH  angegeben,
     so  wird  ein  Kommando  GxH simuliert, um prima"r ein verschlu"sseltes File zu
     behandeln. Die Option G-H unterdru"ckt das Drucken der  Zeichenzahlangabe  bei
     GeH-, GrH-, und GzH-Kommandos.

     Der Editor GedH bewirkt das Editieren der Kopie eines beliebigen  Files;  die
     in der Kopie ausgefu"hrten A"nderungen haben keine Auswirkung, solange bezu"g-
     lich des Files kein Kommando GwH (write) gegeben wurde.  Die  Textkopie,  die
     editiert wird, befindet sich in einem tempora"ren File, dem _P_u_f_f_e_r.

     Die Kommandos fu"r GedH haben eine einfache und  feststehende  Struktur:  null
     oder  mehrere  _A_d_r_e_s_s_e_n, die durch ein einzelnes Zeichen (_K_o_m_m_a_n_d_o) ersetzt
     werden, eventuell durch  Parameter  fu"r  dieses  Kommando  erweitert.   Die
     Adressen  bestimmen  eine oder mehrere Zeilen im Puffer.  Fehlende Adressen
     werden durch Standardwerte ersetzt.  Es kann jeweils nur ein  Kommando  auf
     einer Zeile erscheinen.

     Verschiedene Kommandos erlauben eine Eingabe von Text in den Puffer.   Wenn
     GedH  Text  aufnimmt,  sagt man, der Editor ist im Eingabemodus (_i_n_p_u_t _m_o_d_e).
     In  diesem Modus werden Kommandos nicht als solche  erkannt;  alles  Einge-
     gebene  wird angefu"gt.  Der Eingabemodus wird mit einem einzelnen Punkt "."
     am Zeilenanfang verlassen.

     Der Editor GedH unterstu"tzt eine begrenzte Form  einer  regula"ren  Ausdrucks-
     notation.  Ein  regula"rer Ausdruck weist auf eine Gruppe von Zeichenfolgen.
     Ein Bestandteil dieser Gruppe von Folgen kann als  regula"rer  Ausdruck  von
     Folgen betrachtet werden.  In der folgenden Beschreibung der regula"ren Aus-
     dru"cke bedeutet das  Wort  "Zeichen"  ein  beliebiges  Zeichen,  aup8er  dem
     Newline-Zeichen.

     1.   Gewo"hnliche Zeichen, aup8er  Sonderzeichen,  entsprechen  sich  selbst.
          Sonderzeichen  sind  Begrenzer  fu"r regula"re Ausdru"cke sowie "\[." und
          manchmal  "^*$".

     2.   Ein "." entspricht einem beliebigen Zeichen.

     3.   Ein "\", dem durch ein beliebiges Zeichen,  aup8er  einer  Ziffer  oder
          "()" folgt, entspricht diesem Zeichen.



3/89                                     - G1H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



     4.   Eine nichtleere Folge _s, die in eckigen Klammern  eingeschlossen  ist,
          [_s] (oder [^_s]) entspricht irgendeinem Zeichen aus (oder nicht aus) _s.
          Innerhalb von _s hat "\" keine besondere  Bedeutung  und  kann  nur  an
          erster Stelle erscheinen.  Eine Teilfolge _a-_b, mit _a und _b in steigen-
          der ASCII-Ordnung, steht fu"r  die  eingeschlossene  Folge  von  ASCII-
          Zeichen.

     5.   Ein regula"rer Ausdruck gema"p8 Pkt. 1-4, dem ein "*"  folgt,  entspricht
          einer Folge von 0 oder Entsprechungen fu"r den regula"ren Ausdruck.

     6.   Ein regula"rer Ausdruck _x, gema"p8 Pkt. 1-8, der eingeklammert ist \(_x\),
          entspricht der Bedeutung von _x.

     7.   Ein "\", dem durch eine Ziffer _n folgt,  entspricht  der  Kopie  einer
          Folge  eines  eingeklammerten regula"ren Ausdrucks, der beim _n-ten Vor-
          kommen von "\(" beginnt.

     8.   Ein regula"rer Ausdruck _x gema"p8 Pkt. 1-8, dem ein regula"rer Ausdruck  _y
          gema"p8 Pkt. 1-7 folgt, entspricht der Existenz von _x, gefolgt durch die
          Existenz von _y, wobei die Existenz von _x so lang wie mo"glich  ist  und
          trotzdem die Existenz von _y zula"p8t.

     9.   Ein regula"rer Ausdruck gema"p8 Pkt. 1-8 mit  vorangestelltem  "^"  (oder
          nachgestelltem  "$")  wird gezwungen, am Beginn (oder am rechten Ende)
          einer Zeile zu stehen.

     10.  Ein regula"rer Ausdruck gema"p8 Pkt. 1-9 sucht den la"ngsten unter den  am
          weitesten links in einer Zeile vorkommenden.

     11.  Ein leerer regula"rer Ausdruck steht fu"r eine Kopie des letzten vorhan-
          denen regula"ren Ausdrucks.

     Regula"re Ausdru"cke werden  bei  der  Adressierung  benutzt,  um  Zeilen  zu
     bestimmen und in Substitutionskommandos, um den Teil einer Zeile zu finden,
     welcher zu ersetzen ist. Wenn man in einem regula"ren Ausdruck Sonderzeichen
     als  gewo"hnliche  Zeichen nutzen mo"chte, so kann dies durch Vorsetzen eines
     "\" erreicht werden. Dies bezieht sich auch auf das Zeichen fu"r die Begren-
     zung eines regula"ren Ausdrucks (im allgemeinen "/" ) und auf "\" selbst.

     Um die Adressierung bei GedH zu verstehen, ist es wichtig zu wissen,  dap8  es
     immer eine aktuelle Zeilenadresse gibt. Allgemein betrachtet man die letzte
     Zeile, die durch ein Kommando beeinflup8t wurde, als aktuelle Zeile;  trotz-
     dem  wird  die  genaue  Lage  der  aktuellen Zeile bei der Beschreibung des
     jeweiligen Kommandos erla"utert.  Die Adressen werden gema"p8 folgender Regeln
     erzeugt:

     1.   Das Zeichen "." (dot) adressiert die aktuelle (laufende) Zeile.

     2.   Das Zeichen "$" adressiert die letzte Zeile des Puffers.

     3.   Eine Dezimalzahl _n adressiert die _n-te Zeile im Puffer.

     4.   "'_x" adressiert die Zeile, die mit dem Namen _x markiert wurde, welcher
          ein  Kleinbuchstabe  sein  mup8.   Die Zeilen werden mit dem Kommando GkH
          markiert.

     5.   Ein regula"rer Ausdruck, der in Schra"gstrichen "/" eingeschlossen  ist,
          adressiert  die  Zeile,  die  durch  Vorwa"rtssuchen  von der aktuellen


3/89                                     - G2H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



          Zeilenadresse +1 bis zur ersten Zeile, die die entsprechende Folge des
          regula"ren  Ausdrucks  entha"lt,  gefunden  wurde.  Wenn no"tig, kann das
          Suchen u"ber den Pufferbeginn umlaufen.

     6.   Ein regula"rer Ausdruck, der in Fragezeichen  "?"  eingeschlossen  ist,
          adressiert die Zeile, die durch Ru"ckwa"rtssuchen von der aktuellen Zei-
          lenadresse -1 bis zur ersten Zeile, die die  entsprechende  Folge  des
          regula"ren  Ausdrucks  entha"lt,  gefunden  wurde.  Wenn no"tig, kann das
          Suchen u"ber das Pufferende umlaufen.

     7.   Eine Adresse, der ein Pluszeichen "+" oder Minuszeichen "-" folgt  und
          aup8erdem  eine   Dezimalzahl,  bedeutet: Adresse plus (bzw. minus) der
          angegebenen Zeilenzahl. Das Pluszeichen kann auch weggelassen werden.

     8.   Beginnt eine Adresse mit "+" oder "-", so wird die Addition oder  Sub-
          traktion  in  Bezug auf die aktuelle Zeile gemacht, d.h. "-5" bedeutet
          ".-5".

     9.   Endet eine Adresse mit "+" (oder "-"), so wird 1 addiert (bzw. subtra-
          hiert).   Als eine Folge dieser Regel und der Regel 8 bezieht sich "-"
          auf die Zeile vor der aktuellen. Ferner hat das Folgen von "+" und "-"
          Zeichen  eine additive Wirkung.  So bezieht sich "--" auf die aktuelle
          Zeile minus 2.

     10.  Um die Kompatibilita"t mit fru"heren Versionen des Editors zu  erhalten,
          ist das Zeichen "^" in Adressen a"quivalent mit "-" .

     Kommandos ko"nnen null, ein oder zwei Adressen  erfordern.   Kommandos,  die
     keine  Adressen  beno"tigen,  betrachten das Vorhandensein einer Adresse als
     einen Fehler.  Kommandos, welche ein oder zwei Adressen akzeptieren, nehmen
     Adressen an, wenn sie fehlen.  Wurden mehr Adressen angegeben, als ein ent-
     sprechendes Kommando akzeptiert, dann werden die letzte  oder  die  letzten
     beiden Adressen benutzt.

     Adressen werden durch Komma voneinander  getrennt.  Sie  ko"nnen  aber  auch
     durch  ein  Semikolon G";"H getrennt sein.  In diesem Falle wird die aktuelle
     Zeilenadresse G"."H auf die zuletzt ausgewertete  Zeile  gesetzt,  bevor  die
     na"chste  Adresse interpretiert wird.  Diese Eigenschaft kann zur Bestimmung
     der Anfangszeile fu"r Vorwa"rts- und Ru"ckwa"rtssuchen ("/" oder  "?")  benutzt
     werden.   Die  zweite  Adresse  irgendeiner  Zweiadressfolge mup8 eine Zeile
     bestimmen, die der zuerst adressierten folgt.

     In der nachstehenden Liste der GedH-Kommando  sind  die  Standardadressen  in
     runden Klammern angegeben.  Die Klammern sind nicht Teil der Adresse.

     Es ist im allgemeinen nicht zula"ssig, dap8 mehr als ein Kommando  auf  einer
     Zeile  erscheint.  Es kann jedoch an die meisten Kommandos ein "p" oder "l"
     angefu"gt werden, wodurch die aktuelle Zeile entweder gedruckt oder gelistet
     wird.

     (.)a
     <_t_e_x_t>
     .
          Das Anfu"gekommando (append) liest den vorliegenden Text und  fu"gt  ihn
          nach der adressierten Zeile an.  Die aktuelle Zeilenadresse wird, wenn
          Text eingegeben wurde, auf die letzte Eingabezeile  gelegt.   Anderen-
          falls  liegt  sie  auf der adressierten Zeile. Die Adresse "0" ist fu"r
          dieses Kommando gu"ltig; der Text wird in diesem  Fall  am  Anfang  des


3/89                                     - G3H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



          Puffers untergebracht.

     (.,.)c
     <_t_e_x_t>
     .
          Das A"nderungskommando (change) lo"scht die addresierten  Zeilen,  nimmt
          den  eingegebenen  Text  auf  und ersetzt damit die gelo"schten Zeilen.
          Die aktuelle Zeilenadresse liegt auf der letzten  Eingabezeile.   Wenn
          nichts eingegeben wurde, liegt diese auf der Zeile, die der gelo"schten
          vorangeht.

     (.,.)d
          Das Kommando "entferne Zeile" (delete) entfernt die addressierten Zei-
          len  aus  dem  Puffer. Die Zeile, die urspru"nglich nach der letzten zu
          entfernenden lag, wird die aktuelle Zeile; waren die entfernten Zeilen
          urspru"nglich am Ende, so wird die neue letzte Zeile die aktuelle.

     e _f_i_l_e_n_a_m_e
          Dieses Kommando ("editieren") veranlap8t, den vollsta"ndigen Inhalt  des
          Puffers  zu  entfernen und danach das angegebene File einzulesen.  Die
          aktuelle Zeilenadresse wird auf die letzte Zeile des Puffers  gesetzt.
          Es  wird  die  Anzahl  der gelesenen Zeichen ausgegeben.  Der _f_i_l_e_n_a_m_e
          wird fu"r eine mo"gliche Nutzung als Standard-File-Name fu"r ein nachfol-
          gendes  Kommando  GrH  oder  GwH  gemerkt.   Wenn _f_i_l_e_n_a_m_e fehlt, wird der
          gemerkte Name benutzt.

     E _f_i_l_e_n_a_m_e
          Dieses Kommando entspricht dem GeH-Kommando;  es  erfolgt  jedoch  keine
          Warnung, wenn nach der letzten Puffera"nderung kein GwH gegeben wird.

     f _f_i_l_e_n_a_m_e
          Dieses Kommando ("File-Name") gibt den  zuletzt  gemerkten  File-Namen
          aus.   Wenn ein _f_i_l_e_n_a_m_e angegeben wurde, so wird der zuletzt gemerkte
          aktuelle File-Name durch _f_i_l_e_n_a_m_e ersetzt.

     (1,$)g/_r_e_g_u_l_a"_r_e_r _A_u_s_d_r_u_c_k/_K_o_m_m_a_n_d_o_f_o_l_g_e
          Innerhalb des global-Kommandos ist der erste  Schritt  jede  Zeile  zu
          markieren,  welche den angegebenen regula"ren Ausdruck entha"lt.  Danach
          wird jede markierte Zeile  entsprechend  der  gegebenen  Kommandofolge
          bearbeitet  und  jedes Mal zur aktuellen Zeile gemacht.  Ein einzelnes
          Kommando oder das erste von mehreren Kommandos erscheint auf der glei-
          chen  Zeile  wie  das  global-Kommando. Alle Zeilen einer mehrzeiligen
          Kommandofolge, aup8er der letzten Zeile, mu"ssen mit  "\"  abgeschlossen
          werden.   Die  Kommandos  GAH-,  GiH-,  und  GcH und gemeinsame Eingabe sind
          erlaubt.  Der G"."H, der den Eingabemodus beendet, kann weggelassen wer-
          den,  wenn es die letzte Zeile der Kommandofolge ist.  Die Kommandos GgH
          und GvH sind in der Kommandofolge nicht erlaubt.

     (.)i
     <_t_e_x_t>
     .
          Dieses  Kommando  fu"gt  (insert)  den  vorliegenden   Text   vor   die
          adressierte  Zeile ein. Die aktuelle Zeilenadresse liegt auf der letz-
          ten Eingabezeile oder, wenn nichts eingegeben wurde,  auf  der  Zeile,
          die der adressierten vorangeht. Dieses Kommando unterscheidet sich von
          dem Kommando GaH nur durch die Plazierung des Textes.




3/89                                     - G4H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



     (.,.+1)j
          Dieses Kommando verbindet (join) die  adressierten  Zeilen  zur  einer
          Zeile; dazwischenliegende Newline-Zeichen verschwinden.  Die resultie-
          rende Zeile stellt die aktuelle Zeile dar.

     (.)k_x
          Das Markierungskommando GkH (mark) markiert die  adressierte  Zeile  mit
          dem Namen _x, welcher ein Kleinbuchstabe sein mup8.  Die Adressform "'_x"
          adressiert diese Zeile.

     (.,.)l
          Das List-Kommando druckt die adressierten Zeilen in einer unzweideuti-
          gen  Form:  Nichtgrafikzeichen  werden  zweistellig oktal gedruckt und
          lange Zeilen werden geteilt.  Das Kommando GlH  kann  auf  der  gleichen
          Zeile nach einem beliebigen Nicht-E/A-Kommando angefu"gt werden.

     (.,.)m_a
          Das Transportkommando (move) verlagert die adressierten Zeilen  hinter
          die  durch  _a adressierte Zeile.  Die letzte transportierte Zeile wird
          die aktuelle Zeile.

     (.,.)p
          Das  Druckkommando  (print)  druckt  die  adressierten  Zeilen.    Die
          aktuelle Zeilenadresse ist die der letzten gedruckten Zeile.  Das Kom-
          mando GpH kann auf der  gleichen  Zeile  nach  einem  Nicht-E/A-Kommando
          angefu"gt werden.

     (.,.)P
          Dieses Kommando ist identisch mit Gp.H

     q
          Das Quitkommando veranlap8t das Verlassen des Editors.  Es erfolgt kein
          automatisches Schreiben des Files.

     Q
          Dieses Kommando entspricht GqH; es erfolgt jedoch  keine  Warnung,  wenn
          nach der letzten Puffera"nderung kein GwH gegeben wurde.

     ($) r _f_i_l_e_n_a_m_e
          Das File-Lesekommando (read) liest  das  angegebene  File  hinter  die
          adressierte  Zeile.  Wenn  kein File-Name angegeben wurde, so wird der
          gemerkte File-Name benutzt (siehe Kommandos GeH- und GfH).  Der  File-Name
          wird  gemerkt,  wenn  es  noch  keinen  gemerkten File-Namen gibt. Die
          Adresse "0" ist fu"r GrH gu"ltig und bestimmt, dap8 das File an den  Anfang
          des  Puffers  gelesen  wird.  Wenn das Lesen erfolgreich war, wird die
          Anzahl der gelesenen Zeichen ausgegeben. Die letzte Zeile, die von dem
          File gelesen wurde, ist die Zeile mit der aktuellen Adresse.

     (.,.)s/_r_e_g_u_l_a"_r_e_r _A_u_s_d_r_u_c_k/_E_r_s_a_t_z/ oder
     (.,.)s/_r_e_g_u_l_a"_r_e_r _A_u_s_d_r_u_c_k/_E_r_s_a_t_z/g
          Das Substitutionskommando durchsucht jede adressierte Zeile nach  Vor-
          handensein  des angegebenen regula"ren Ausdrucks.  Auf jeder Zeile, auf
          der dieser gefunden wurde, werden  alle  entsprechenden  Zeichenfolgen
          durch  den  angegebenen Ersatz ausgetauscht, d.h. wenn das Kennzeichen
          fu"r eine globale Ersetzung GgH nach dem Kommando  erscheint.  Wurde  das
          global-Kennzeichen  nicht  gesetzt, so wird nur die jeweils erste Zei-
          chenfolge einer Zeile ausgetauscht. Es ist ein Fehler fu"r eine Substi-
          tution,  wenn  sie  auf  keiner  Zeile  ausgefu"hrt  werden  kann.  Als


3/89                                     - G5H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



          Begrenzung des regula"ren Ausdrucks und des Ersatzes kann an Stelle von
          "/"  jedes  beliebige  Zeichen, aup8er Leerzeichen und Newline-Zeichen,
          verwendet werden. Die aktuelle Zeilenadresse wird auf die letzte  sub-
          stituierte Zeile gelegt.

          Ein Ampersand "&", das im Ersatz erscheint, wird durch  eine  Zeichen-
          folge  ersetzt,  die dem regula"ren Ausdruck entspricht.  Die spezielle
          Bedeutung von "&" in disem Textteil kann durch  das  Vorstellen  eines
          "\" unterdru"ckt werden. Die Zeichen _n, wobei _n eine Ziffer ist, werden
          durch den Text ersetzt der dem _n-ten in "\(" und "\)" eingeschlossenen
          regula"ren Unterausdruck entspricht. Wenn die eingeklammerten Unteraus-
          dru"cke ineinandergeschachtelt sind, wird _n durch Za"hlen der  vorhande-
          nen "\(", von links beginnend, ermittelt.

          Die Zeilen ko"nnen auch durch  Einfu"gen  des  Newline-Zeichens  geteilt
          werden.  Dem  Newline-Zeichen mup8 in der herzustellenden Folge ein "\"
          vorangestellt werden.

     (.,.)ta
          Dieses Kommando wirkt wie das Kommando GmH, wobei jedoch die  Kopie  der
          adressierten Zeilen nach der  Adresse _a plaziert wird, die auch 0 sein
          kann. Die letzte Zeilenkopie wird die aktuelle Zeile.

     (.,.)u
          Das Kommando "ru"ckga"ngigmachen" (undo) gibt urspru"nglichen Inhalt  der
          aktuellen  Zeile  zuru"ck, wobei in dieser Zeile zuletzt eine Substitu-
          tion ausgefu"hrt worden sein mup8.

     (1,$)v/_r_e_g_u_l_a"_r_e_r _A_u_s_d_r_u_c_k/_K_o_m_m_a_n_d_o_f_o_l_g_e
          Dieses Kommando entspricht dem "global" Kommando GgH,  jedoch  wird  die
          Kommandofolge  fu"r  jede  aktuelle  Zeile,  die den regula"ren Ausdruck
          nicht entha"lt, ausgefu"hrt.

     (1,$)w _f_i_l_e_n_a_m_e
          Das File-Schreibkommando (write) schreibt die adressierten  Zeilen  in
          das angegebene File. Wenn das File nicht existiert, wird der Modus 666
          (lesbar und beschreibbar durch jeden) hergestellt. Der File-Name  wird
          gemerkt,  wenn  es  noch  keinen gemerkten File-Namen gibt.  Wenn kein
          File-Name angegeben  wurde,  so  wird  irgendein  gemerkter  File-Name
          benutzt  (siehe Kommandos GeH und GfH).  Die aktuelle Zeilenadresse bleibt
          unvera"ndert. Wenn das Kommando erfolgreich war, wird  die  Anzahl  der
          ru"ckgeschriebenen Zeichen angezeigt.

     (1,$)W _f_i_l_e_n_a_m_e
          Dieses Kommando entspricht GwH; die adressierten Zeilen werden jedoch an
          das File angefu"gt.

     x
          Es wird eine Verschlu"sselungsfolge von der  Standardeingabe  verlangt.
          Anschliep8end  verschlu"sseln  bzw.  entschlu"sseln die Kommandos G r-, e-H
          und GwH den Text mit diesem Schlu"ssel  durch  den  GcryptH(1)-Algorithmus.
          Ein spezieller leerer Schlu"ssel schaltet die Verschlu"sselung aus.

     ($)=
          Die Zeilennummer wird durch  dieses  Kommando  ausgegeben,  wobei  die
          aktuelle Zeilenadresse  nicht vera"ndert wird.




3/89                                     - G6H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



     !<_s_h_e_l_l _c_o_m_m_a_n_d>
          Das dem "!" auf dieser Zeile folgende wird nach GshH(1) gesendet, um  es
          als  ein  Kommando zu interpretieren.  Die aktuelle Zeilenadresse wird
          nicht vera"ndert.

     (.+1)<_n_e_w_l_i_n_e>
          Eine Adresse allein auf einer Zeile  veranlap8t,  dap8  die  adressierte
          Zeile  gedruckt  wird.  Eine Leerzeile ist a"quivalent mit ".+1". Damit
          ist ein schrittweises Anzeigen dieses Textes mo"glich.

     Wenn ein Interruptsignal CTRL-C gesendet wird, druckt GedH ein "?" und  kehrt
     zuru"ck zu seiner Kommandoebene.

     Einige Gro"p8en fu"r die Grenzen sind 512 Zeichen pro Zeile, 256  Zeichen  pro
     globaler  Kommandofolge,  64 Zeichen pro File-Name und 128K Zeichen  in dem
     tempora"ren File. Die Grenze der Zeilenzahl ist von der Gro"p8e des  Speichers
     abha"ngig: jede Zeile nimmt ein Wort in Anspruch.

     Wenn ein File gelesen wird, entfernt der Editor ASCII-NUL-Zeichen und  alle
     Zeichen  nach dem letzten Newline-Zeichen. Er liest keine Files, die Nicht-
     ASCII-Zeichen enthalten.
G
FILES H
     /tmp/e*
     ed.hup: das Ergebnis wird hier gerettet, wenn das Terminal abgeha"ngt wird.
G
SIEHE AUCH H
     sed(1)
G
DIAGNOSE H
     "?name" fu"r ein nichtauffindbares File; "?" fu"r ein Kommandofehler;  "?TMP"
     fu"r U"berlauf des tempora"ren Files; "?MEM" fu"r Hauptspeicherplatz-U"berlauf.

     Ein Kommando GqH- oder GeH war fehlerhaft, sofern nicht nach der letzten Puffe-
     ra"nderung ein GwH gegeben wurde. Ein zweites GqH oder GeH wird immer befolgt.
G
FEHLERQUELLEN H
     Das Kommando GlH vera"ndert DEL.
     Ein Kommando G!H kann einem Kommando GgH nicht unterworfen werden.
     Da 0 eine ungu"ltige Adresse fu"r ein Kommando GwH ist, ist es  nicht  mo"glich,
     ein leeres File mit GedH herzustellen.
G
ANHANG 1 H
     Dieser Anhang beschreibt die Eigenschaften des  GemH,  der  eine  Erweiterung
     des  oben  beschriebenen   Editors darstellt.  Der Editor GemH fu"hrt ein Pro-
     tokoll u"ber die Editor-Sitzung, so dap8, wenn irgendetwas  "Schlimmes"  pas-
     siert wa"hrend des Editierens (z.B.  System-Absturz), all die "harte" Arbeit
     nicht verloren geht.  Wenn der Editor mit G-rH und  dem Namen des  Protokoll-
     Files  (_n_i_c_h_t der  Name des  editierten Files) aufgerufen wird, so wird die
     zuletzt absolvierte Editor-Sitzung  nachvollzogen.  Protokoll-Files  werden
     bei  normalem  Editorausgang gelo"scht.  Wenn GemH aufgerufen wird, stellt man
     fest, dap8 er sich am Anfang mit "Editor" meldet. Er meldet  sich  ebenfalls
     mit  dem  Anforderungszeichen  ">".  Wenn GemH mit G-sH oder G-H aufgerufen wird,
     dann verha"lt er sich absolut still, d.h. er meldet sich nicht mit "Editor",
     hat kein Anforderungszeichen und schreibt keine Bytegro"p8e bei den Kommandos
     GeH-, GrH- und GwH aus.  Wird -e angegeben, so ist kein Kommando GeH- und  GwH  zuge-
     lassen und es werden keine Ausschriften wie beim G-sH ausgegeben. Mit G-lH kann
     der Aufbau des Editor-Protokolls unterdru"ckt werden. Das G-xH entspricht  der


3/89                                     - G7H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



     analogen  Option  des  Editors  GedH.  Wird G-qH angegeben, so kann die Editor-
     Sitzung mit dem Quit-Symbol CTRL-\ abgebrochen werden. Dabei wird ein "core
     dump"  erzeugt.   Das Anforderungszeichen kann auch wa"hrend des Editiervor-
     ganges zuru"ckgenommen werden. Die hinzugekommenen  Kommandos  des  GemH  sind
     folgende:

     b[+][-]
          Der Editor GemH entha"lt "line breaks". Das bedeutet, dap8 automatisch ein
          Newline-Zeichen eingefu"gt wird, wenn eine Eingabezeile la"nger als eine
          festgelegte La"nge ist. Normalerweise ist das "line  break"  abgeschal-
          ten, aup8er wenn der File-Name mit ".n" endet. Das Kommando Gb+H schaltet
          das "line break" ein und Gb-H schaltet es wieder ab.

     h[_f_i_l_e]
          GhH listet das File G/usr/lib/emhelp/commandsH,  welches  eine  Liste  der
          verfu"gbaren  Kommmandos  fu"r GemH entha"lt. Wird GhH unmittelbar durch eine
          Zeichenkette fortgesetzt, wobei _k_e_i_n Leerzeichen hinter GhH stehen darf,
          so  wird das File G/usr/lib/emhelp/<string>H gelistet. Die zur Zeit vor-
          handenen Files sind GhkeysH und GhscreeneditorH.  Mit  "hkeys"  wird  eine
          Kurzbeschreibung  der  Funktionstasten  und  mit  "hscreeneditor"  die
          Anwendungsbeschreibung des Bildschirmmodus ausgelistet.

     o[+][;][:][-]
          Wenn eines der Kommandos GoH, GoH+, GoH;, GoH: oder GoH- eingegeben  wird,  dann
          geht GemH in den Bildschirmmodus u"ber (siehe ANHANG 2)

     z/_r_e_g_u_l_a"_r_e_r _A_u_s_d_r_u_c_k/_E_r_s_a_t_z/
          Dies ist vergleichbar mit dem Kommando GsH, in welchem der regula"re Aus-
          druck  durch  den  Ersatz ersetzt wird. Aber es wird interaktiv ausge-
          fu"hrt, d.h. es stoppt bei jedem Auftreten  eines  regula"ren  Ausdrucks
          und  markiert.   Wenn nun ein "." eingeben wird, so wird die Substitu-
          tion ausgefu"hrt. Wenn jedoch ein <CR> eingeben wird, so wird die  Sub-
          stitution  unterdru"ckt.  Dies  ist sehr sinnvoll in Verbindung mit dem
          Kommando GgH (global),  wenn nur bei bestimmten, aber nicht allen  regu-
          la"ren Ausdru"cken in einem File eine A"nderung vorgenommen werden soll.

     "    Dies ist eine abku"rzende Schreibweise fu"r ".+1,.+22p",  um  den  Bild-
          schirm mit dem folgenden Text zu beschreiben.

     &    Abku"rzende Schreibweise fu"r ".-21,.p" - fu"r den vorhergehenden Text.

     %    Dieses Kommando listet von Zeile ".-9" bis Zeile ".+9" und  kennzeich-
          net  die aktuelle Zeile durch die Ausgabe horizontaler Markierungszei-
          len; dabei bleibt die aktuelle Zeilennummer unvera"ndert.

     (.,.)|<_c_o_m_m_a_n_d _l_i_s_t>
          Dieses Kommando gibt die Mo"glichkeit, die Ausgabe der Liste durch eine
          Pipe  an  ein  MUTOS-Kommando  zu  u"bergeben.  Zum  Beispiel  kann man
          "1,25|lpr" eingeben, um die Zeilen 1 bis 25 auf dem Print-Spooler aus-
          zugeben.

     !    Dieses Kommando wurde geschaffen, um die Arbeit zu erleichtern.   Wenn
          das Zeichen "%" irgendwo auf einer Shell-Kommandozeile erscheint, dann
          wird es durch den aktuellen File-Namen ersetzt.  Wenn das  erste  Zei-
          chen  nach dem "!" ein weiteres "!" ist, dann wird es durch das letzte
          Shell Kommando, welches ausgefu"hrt wurde, ersetzt. Zum Beispiel listet
          "!  pr  %"  das  aktuelle File auf dem Bildschirm aus. Wenn "!! | lpr"
          eingeben wird, dann wird  das  aktuelle  File  auf  dem  Print-Spooler


3/89                                     - G8H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



          ausgegeben.

     >    Nimmt das Anforderungszeichen zuru"ck.

     <    Schaltet das Anforderungszeichen ein.

     /    Das ist eine Kurzform fu"r "//" zum Suchen des aktuellen  Suchbegriffes
          vorwa"rts.

     ?    Kurzform fu"r "??" - sucht ru"ckwa"rts nach dem aktuellen Suchbegriff.
G
ANHANG 2 H
     Dieser Bildschirm-Editor dient zum Editieren bzw. Erstellen von  beliebigen
     ASCCI-Code-Texten  unter  dem  Betriebssystem  MUTOS  1700 mit dem Console-
     Terminal.  Fu"r mo"gliche Mehrnutzer-Terminals vom Typ K 8911 ist der  Editor
     em.8911 zu verwenden.

     Aufruf des Bildschirm-Editors im Gem:H

     SYNTAX:

                  {+/;}

        {(.,.)} o {-  } {/ Suchbegriff /}  ,mit { } wahlweise

                  {:  }

     mit:

     G+/;H  Anfu"gen                a (append)

     G-H    Einfu"gen     siehe     i (insert)   im Normalmodus

     G:H    U"berschreiben          c (change)

     MENUEFELD:

     F1    INS  ALT DEL   F4     F5      END     <-\     F3    <--'    DEL

     EXIT CMODE DELLINE DELEND DELSTART LINEEND NEXTLINE RESET APPEND DELCHAR

     F6

     MENUE

     bzw.

     F7      PGDN    PGUP      F10    F8     F9    F2   /|\  \|/    <--   -->

     SPLIT NEXTPAGE PREVPAGE SEARCH COMMAND GOTO  JOIN  UP  DOWN   LEFT RIGHT

     F6

     MENUE

     Das Menuefeld wird intensiv leuchtend auf  den  letzten  Zeilen  des  Bild-
     schirms  dargestellt. In der ersten Menuezeile wird die aktuelle Zeilennum-
     mer und,  falls  vorhanden,  der  aktuelle  File-Name,  ansonsten  "noname"


3/89                                     - G9H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



     ausgegeben.

     BEMERKUNGEN:

     G-H    Eine zu editierende Textzeile kann sich u"ber mehrere  Bildschirmzeilen
          erstrecken.  Die maximale La"nge einer Textzeile betra"gt 512 Einzelzei-
          chen, wobei Tabulatoren durch die no"tige Anzahl von  Leerzeichen  dar-
          gestellt werden, aber als Einzelzeichen za"hlen.

     G-H    Nichtdruckbare Zeichen (d.h. 0 <= ASCII-Code < 040 bzw.  0177)  werden
          durch  Leerzeichen dargestellt. Jede Schreib- bzw. Lo"schoperation wird
          bei dieser Position abgelehnt.

     G-H    Eine unerlaubte Verwendung einer Funktionstaste (siehe Menuefeld) bzw.
          die  Beta"tigung einer Taste, welche kein graphisches Zeichen darstellt
          (graphische Zeichen: 040 <= ASCII-Code <= 0176), fu"hrt zum Erto"nen der
          Glocke (Hupton).

     G-H    Innerhalb eines Tabulators (dargestellt durch Leerzeichen)  wird  jede
          Schreib-  bzw.  Lo"schoperation  abgelehnt,  ausgenommen  an der ersten
          Leerposition (d.h. Tabulatoren sind stets von links zu manipulieren).

     G-H    Falls ein Suchbegriff angegeben wird,  steht  der  Kursor  hinter  dem
          gefundenen Begriff (die Textzeile in der sich der Suchbegriff befindet
          ist die erste Textzeile des Bildschirms).

     FUNKTIONSTASTEN:

          -->                    }   Kursorbewegung innerhalb der
                                 }   aktuellen Textzeile
          <-- bzw. |<--|         }

           |                     }   Kursorbewegung auf die
          \|/                    }   na"chste bzw. vorhergehende
                                 }   Bildschirmzeile, wobei die
          /|\                    }   horizontale Kursorposition
           |                     }   erhalten bleibt.

     Die vertikale Kursorbewegung ist nur dann mo"glich,  wenn  sich  der  Kursor
     nach   der   Vertikalbewegung   an   einer  definierten  Position  befindet
     (definierte Positionen sind durch  Textsymbole  belegte  Bildschirmpositio-
     nen).

     EXIT
          Verlassen des Bildschirmmodus.

     CMODE
          U"berschreibmodus (Standard) bzw. durch Tastendruck umkehrbar.

     IMODE
          Einfu"gemodus

     DELLINE
          Lo"schen der aktuellen Textzeile (unabha"ngig von Kursorposition  inner-
          halb der Textzeile). In Abha"ngigkeit davon, ob eine folgende Textzeile
          existiert, steht der Kursor am Anfang der folgenden bzw.  vorhergehen-
          den  Textzeile.  Ist  die zu lo"schende Textzeile die einzige im ero"ff-
          neten Bereich, so wird der Bildschirmmodus verlassen.


3/89                                    - G10H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



     DELEND
          Lo"schen ab aktueller Kursorposition bis Textzeilenende  (Kursor  steht
          dann hinter dem letzten Zeichen der verbliebenen Textzeile).

     DELSTART
          Lo"schen ab aktueller Kursorposition bis Textzeilenanfang (verbleibende
          Textzeile  wird  dann  nach  links ausgerichtet und der Kursor auf den
          Zeilenanfang positioniert).

     LINEEND
          Kursor wird an das Ende der Textzeile positioniert (d.h. er steht hin-
          ter dem letzten Zeichen).

     NEXTLINE
          Der Kursor wird an den Anfang  der  folgenden  Textzeile  positioniert
          (unabha"ngig von aktueller Kursorposition).

     RESET
          Kursor wird an den Anfang der Textzeile positioniert.

     NEXTPAGE
          Der Bildschirm wird mit nachfolgenden  Textzeilen  beschrieben,  wobei
          die neue erste Textzeile die letzte des vorherigen Bildinhaltes ist.

     PREVPAGE
          Der Bildschirm wird mit vorhergehenden Textzeilen  beschrieben,  wobei
          die neue letzte Textzeile die erste des vorherigen Bildinhaltes ist.

     Der Kursor steht nach NEXTP bzw. PREVP auf dem ersten  Zeichen  der  ersten
     Textzeile des Bildschirms.

     CLEAR
          Wechsel zwischen den vorhandenen Menuefeldern.

     GOTO Der Kursor wird an den Anfang  der  ersten  Textzeile  des  geo"ffneten
          Bereiches positioniert.

     JOIN Zusammenfu"gen von aktueller und na"chst folgender Textzeile, wobei  der
          Kursor danach am Anfang der neuen Textzeile steht (unabha"ngig von Kur-
          sorposition)

     SEARCH
          Sucht einen bereits gemerkten Suchbegriff und positioniert sich dahin-
          ter.   Suchbegriff  kann  entweder im Normalmodus oder Bildschirmmodus
          eingegeben worden sein.

     COMMAND
          Nach Beta"tigen dieser Taste wird in der ersten Menuezeile die  Komman-
          doanforderung

                   COMMAND:

          ausgegeben und das aktuelle Menuefeld eingeblendet:







3/89                                    - G11H -                                  







GED(1)H                                 MUTOS 1700                           GED(1)H



       <N>ALT DEL    <N>F9      <STRING>F10       F8           DEL      F6

     DEL N LINES  GOTO LINE N  SEARCH STRING  EXIT CMD LINE  DELCHAR  CMD LINE
     mit:

     <N>ALT DEL
          Lo"schen von N Textzeilen, einschliep8lich aktueller Textzeile (fehlende
          Angabe von N entspricht N=1).

     <N>F9
          Der Kursor wird an den Anfang der N-ten Textzeile positioniert.  Falls
          der aktuelle Bildschirminhalt diese Zeile nicht entha"lt, wird gesuchte
          Textzeile zur ersten Bildschirmzeile (fehlende Angabe von N entspricht
          N=$, d.h. der letzten Textzeile des geo"ffneten Bereiches).

     <STRING>F10
          Der Kursor wird an das Ende des gesuchten STRING  positioniert,  wobei
          jeweils  nur  das  erste  Auftreten in einer Textzeile betrachtet wird
          (fehlende Angabe von STRING setzt einen bereits gemerkten  Suchbegriff
          voraus).

     F8   Abbruch der Kommandoeingabe.

     DEL  Lo"schen des zuletzt eingegebenen Zeichens bei der Kommandoeingabe.

     * Nichtdruckbare Zeichen (einschliep8lich Tabulator) werden bei der  Komman-
     doeingabe abgelehnt.

     * Falsche Kommandoeingaben fu"hren  zur  Fehlerausschriften  in  der  ersten
     Menuezeile.

     * Wird ein syntaktisch falscher regula"rer Ausdruck  als  Suchbegriff  ange-
     geben,  so wird der Bildschirmmodus verlassen. Danach ist der Bildschirmmo-
     dus erneut aufzurufen.

     * Die maximale La"nge der Kommandozeile betra"gt 25 Einzelzeichen.

     DELCHAR
          Lo"schen des Zeichens, unter dem sich der Kursor befindet.

     SPLIT
          Trennen der aktuellen Textzeile ab Kursorposition (Kursor  steht  dann
          auf dem ersten Zeichen der neuen Textzeile).

     APPEND
          Anfu"gen einer Leerzeile, Kursor steht dann auf der ersten Position der
          neuen  Textzeile (unabha"ngig  von Kursorposition innerhalb der aktuel-
          len Textzeile).

     Die Eingabe von Textzeichen (graphische Zeichen) erfolgt  im  jeweils  ein-
     gestellten  Modus  (CMODE  bzw. IMODE). Ist eine Bildschirmzeile voll, wird
     der Text auf der na"chsten Bildschirmzeile fortgesetzt.








3/89                                    - G12H -                                  





