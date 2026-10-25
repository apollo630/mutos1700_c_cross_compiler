

GA.OUT(5)H                              MUTOS 1700                        GA.OUT(5)H



G
NAME H
     a.out - Assembler- und Lader-Ausgabe-File
G
U"BERSICHT H
     #include <a.out.h>
G
BESCHREIBUNG H
     Das File Ga.outH ist das Ausgabe-File des Assemblers  GasH(1)  und  des  Laders
     GldH(1).   Beide  Programme  machen das File Ga.outH ausfu"hrbar, falls es keine
     Fehler und unauflo"sbare externe Symbolbezugnahmen gibt.  Das Format ist  in
     dem entsprechenden Include-File definiert:

     struct  exec {   /* a.out header */
             int      a_magic;   /* magic number */
             unsigned a_text;    /* size of text segment */
             unsigned a_data;    /* size of initialized data */
             unsigned a_bss;     /* size of uninitialized data */
             unsigned a_syms;    /* size of symbol table */
             unsigned a_entry;   /* entry point */
             unsigned a_unused;  /* not used */
             unsigned a_flag;    /* relocation info stripped */
     };

     #define A_MAGIC1 0407       /* normal */
     #define A_MAGIC2 0410       /* read-only text */
     #define A_MAGIC3 0411       /* separated I&D */
     #define A_MAGIC4 0405       /* overlay */

     struct  nlist {  /* symbol table entry */
             char     n_name[8]; /* symbol name */
             int      n_type;    /* type flag */
             unsigned n_value;   /* value */
     };

                      /* values for type flag */
     #define N_UNDF   0          /* undefined */
     #define N_ABS    01         /* absolute */
     #define N_TEXT   02         /* text symbol */
     #define N_DATA   03         /* data symbol */
     #define N_BSS    04         /* bss symbol */
     #define N_TYPE   037
     #define N_REG    024        /* register name */
     #define N_FN     037        /* file name symbol */
     #define N_EXT    040        /* external bit, or'ed in */
     #define FORMAT   %04x"      /* to print a value */
     #define FWIDTH   4          /* width of FORMAT */

     Das File hat 4 Sektionen: Einen Kopf, Programm  und  Daten,  Verschieblich-
     keitsinformationen  und  eine  Symboltabelle  (in dieser Reihenfolge).  Die
     letzten 2 Sektionen sind leer, wenn beim  Laden  die  Option  G-sH  angegeben
     wurde,  oder  wenn  die Symbole und Verschieblichkeitsinformationen mit dem
     Kommando GstripH(1) gelo"scht wurden.

     Im Kopf wird die Gro"p8e jeder Sektion in Bytes angegeben (geradzahlig).  Die
     Gro"p8e des Kopfes ist in keiner der anderen Gro"p8enangaben enthalten.




3/89                                     - G1H -                                  







GA.OUT(5)H                              MUTOS 1700                        GA.OUT(5)H



     Wird ein File Ga.outH zur Ausfu"hrung in den Speicher geladen, werden  3  Seg-
     mente aufgebaut: Textsegment, Datensegment (mit nichtinitialisierten Daten,
     gefu"llt mit 0 fu"r nachfolgende Initialisierung) und ein Stack.   Das  Text-
     segment beginnt auf Platz 0 im Speicherabbild; der Kopf wird nicht geladen.

     Eine Magic-Nummer von  0407(8)  kennzeichnet,  dap8  das  Textsegment  nicht
     schreibgeschu"tzt   und   geteilt  ist  und  das  Datensegment  sich  sofort
     anschliep8t.  Ist die Magic-Nummer 0410 oder 0411, werden Text-  und  Daten-
     segment in getrennten Speicherbereichen angelegt. Das Textsegment ist daher
     schreibgeschu"tzt und kann von verschiedenen Prozessen geteilt genutzt  wer-
     den.  Sowohl das Text- als auch das Datensegment beginnen auf Speicherplatz
     0.  Ist die Magic-Nummer 0405, wird  das  Textsegment  einem  existierendem
     Textsegment  (0411  oder  0405) u"berlagert und das existierende Textsegment
     wird gerettet.

     Der Stack belegt, abwa"rts wachsend, die ho"chstmo"glichen Speicherpla"tze  des
     jeweils zur Verfu"gung stehenden Speicherraumes.  Der Stack wird automatisch
     erweitert, falls beno"tigt.  Das Datensegment kann mit dem Systemruf  GbrkH(2)
     erweitert werden.

     Das Textsegment beginnt im File Ga.outH auf Platz  020(8);  das  Datensegment
     auf  Platz  020+S9t8(die Gro"p8e des Textes).  Die Verschieblichkeitsinformati-
     onen beginnen auf Platz 020+S9t8+S9d8(die Gro"p8e der  Daten).   Der  Beginn  der
     Symboltabelle  ist  020+2(S9t8+S9d8),  wenn die Verschieblichkeitsinformationen
     vorhanden sind, anderenfalls 020+S9t8+S9d8 wenn nicht.

     Das Format der Symboltabellenpla"tze und die  prinzipiellen  Flag-Werte  der
     unterschiedlichen  Symboltypen  sind  im  Include-File  definiert.   Andere
     Flag-Werte ko"nnen auftreten, wenn  ein  Assemblerprogramm  Maschinenbefehle
     definiert.

     Ist der Typ eines Symbols undefiniert extern  und  sein  Wert  ungleich  0,
     interpretiert der Lader GldH(1) das Symbol als den Namen einer Common-Region,
     deren Gro"p8e sich aus dem Wert des Symbols ergibt.

     Der Wert eines Wortes in den Text-  oder  Datenportionen,  der  nicht  eine
     Bezugnahme  zu einem undefinierten externen Symbol ist, ist genau der Wert,
     der im Speicher bei Ausfu"hrung des Files erscheint.  Wenn ein Wort  in  der
     Text-  oder  Datenportion  eine  Bezugnahme zu einem undefinierten externen
     Symbol beinhaltet, die durch die Verschieblichkeitsinformation  fu"r  dieses
     Wort angezeigt wird, dann ist der Wert des Wortes ein Offset auf das bezug-
     genommene Symbol.  Wenn das File durch den Link-Editor verarbeitet wird und
     das  externe  Symbol seinen Wert erhalten hat, wird dieser Wert zu dem Wort
     in dem File addiert.

     Falls Verschieblichkeitsinformationen vorhanden sind, belaufen sie sich auf
     ein  Wort  pro Wort des Programmtextes oder initialisierter Daten.  Es gibt
     keine  Verschieblichkeitsinformationen,  wenn  das  Flag  "Relocation  info
     stripped" im Header gesetzt ist.

     Die Bits 3-1 eines Verschieblichkeitswortes kennzeichnen  das  Segment,  in
     dem  das  mit  dem  Verschieblichkeitswort  verbundene Text- oder Datenwort
     steht:

          000  Absolutwert
          002  Bezugnahme auf ein Textsegment
          004  Bezugnahme auf initialisierte Daten
          006  Bezugnahme auf nichtinitialisierte Daten (bss)


3/89                                     - G2H -                                  







GA.OUT(5)H                              MUTOS 1700                        GA.OUT(5)H



          010  Bezugnahme auf ein undefiniertes externes Symbol

     Das Bit des Verschieblichkeitswortes kennzeichnet, dap8 die Bezugnahme rela-
     tiv zum Befehlsza"hler (=1) bzw. auf ein aktuelles Symbol (=0) erfolgt.

     Der Rest des Verschieblichkeitswortes (Bits 15-4) beinhaltet im Falle einer
     externen  Bezugnahme  eine  Symbolnummer.  In  allen anderen Fa"llen bleiben
     diese Bits ungenutzt. Das 1. Symbol hat die Nummer 0, das zweite 1 usw..
G
SIEHE AUCH H
     as(1), ld)1), nm(19

















































3/89                                     - G3H -                                  





