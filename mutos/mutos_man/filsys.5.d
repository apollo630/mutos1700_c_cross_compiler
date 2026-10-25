

GFILSYS(5)H                                MUTOS                         GFILSYS(5)H



G
NAME H
     filsys, flblk, ino - Format der File-System-Speicherung
G
U"BERSICHT H
     #include <sys/types.h>
     #include <sys/flbk.h>
     #include <sys/filsys.h>
     #include <sys/ino.h>
G
BESCHREIBUNG H
     Jedes File-System, das auf einem  Datentra"ger  gespeichert  wird,  hat  ein
     gemeinsames  Format  fu"r bestimmte wichtige Informationen.  Jedes abgespei-
     cherte System ist in eine bestimmte Anzahl von 512-Byte-Blo"cken unterteilt.
     Block  0 ist unbenutzt und verfu"gbar, um ein Bootstrap-Programm oder andere
     Informationen aufzunehmen.

     Block 1 ist der Super-Block.  Das Format des Super-Blocks  im  Include-File
     <Gsys/filsys.hH> ist wie folgt definiert:

     /*
      * Structure of the super-block
      */
     struct filsys {
            unsigned short s_isize;    /* size in blocks of
                                          i-list */
            daddr_t  s_fsize;          /* size in blocks of
                                          entire volume */
            short    s_nfree;          /* number of addresses
                                          in s_free */
            daddr_t  s_free[NICFREE];  /* free block list */
            short    s_ninode;         /* number of i-nodes
                                          in s_inode */
            ino_t    s_inode[NICINOD];  /* free i-node list */
            char     s_flock;          /* lock during free
                                          list manipulation */
            char     s_ilock;          /* lock during i-list
                                          manipulation */
            char     s_fmod;           /* super block modified
                                          flag */
            char     s_ronly;          /* mounted read-only
                                          flag */
            time_t   s_time;           /* last super block
                                          update */
            daddr_t  s_tfree;          /* total free blocks*/
            ino_t    s_tinode;         /* total free inodes */
            /* remainder not maintained by this version of
               the system */
            short    s_m;              /* interleave factor */
            short    s_n;              /* " " */
            char     s_fname[6];       /* file system name */
            char     s_fpack[6];       /* file system pack
                                          name */
            /* reminander is maintained for MUTOS */
            char     s_clean;          /* S_CLEAN if structure
                                          is properly closed */
     };



                                         - G1H -                                  







GFILSYS(5)H                                MUTOS                         GFILSYS(5)H



     #define S_CLEAN 0106              /* arbitrary magic
                                          value */

     Die Variable Gs_isizeH entha"lt die Adresse des 1. Blocks  nach  der  i-Liste,
     die  unmittelbar nach dem Super-Block im Block 2 beginnt.  Folglich ist die
     i-Liste Gs_isizeH-2 Blo"cke lang.  In Gs_fsizeH ist die Adresse des nicht poten-
     tiell vefu"gbaren Zuweisungsbereiches fu"r ein File enthalten.  Diese Adresse
     wird durch das System benutzt, um fehlerhafte Blockadressen zu testen; wird
     eine "unmo"gliche" Blockadresse von der Frei-Liste (Liste der freien Blo"cke)
     zugewiesen oder freigemacht, wird eine Fehlerdiagnose auf der Systemkonsole
     ausgegeben.   Aup8erdem wird das Frei-Feld gelo"scht, um weiteren Zuweisungen
     von der vermutlich zersto"rten Frei-Liste vorzubeugen.

     Die Frei-Liste ist fu"r jeden Datentra"ger  wie  folgt  aufgebaut:  Das  Feld
     Gs_freeH  entha"lt  in Gs_free[1], ... , s_free[s_nfree-1]H bis zu NICFREE freie
     Blocknummern.  NICFREE ist eine Konfigurationskonstante.  In Gs_free[0]H  ist
     die Blockadresse des Kopfblockes einer Reihe von Blo"cken enthalten, die die
     Frei-Liste  bilden.   Das  Format  jedes  Blockes  der  Frei-Liste  ist  im
     Include-File <Gsys/fblk.hH> wie folgt definiert:

     struct fblk
     {
            int      df_nfree;
            daddr_t  df_free[NICFREE];
     };

     Die Variable Gdf_nfreeH und das Feld Gdf_freeH in einem Frei-Block  entsprechen
     genau  der  Bedeutung von Gs_nfreeH und Gs_freeH im Super-Block.  Die Zuweisung
     eines Blockes im File-System geschieht wie folgt: Der Za"hler Gs_nfree,H  wird
     dekrementiert.   Die  neue Blocknummer wird Gs_free[s_nfree]H.  Wenn die neue
     Blockadresse 0 ist, gibt es keine weiteren verfu"gbaren Blo"cke und ein  Feh-
     ler  wird  angezeigt.   Wenn  Gs_nfreeH  0 wurdd, ist ein neuer Block von der
     Frei-Liste in Gs_nfreeH und Gs_freeH einzulesen.  Soll  ein  Block  freigemacht
     werden, wird zuna"chst gepru"ft, ob Gs_nfreeH gleich NICFREE ist. Wenn ja, wird
     der gefu"llte Frei-Block auf  die  Platte  geschrieben  und  Gs_nfreeH  auf  0
     gesetzt.   Der  freizumachende Block wird der na"chste Block der Frei-Liste,
     seine Adresse wird nach Gs_free[s_nfree]H gebracht. Anschliep8end wird Gs_nfreeH
     inkrementiert.

     Die Variable Gs_ninodeH ist die Anzahl der freien i-Nummern im Feld  Gs_inodeH.
     Zur Zuweisung einer Inode: Ist die Variable Gs_ninodeH gro"p8er als 0, wird sie
     dekrementiert und Gs_inode[s_ninode]H ist die Nummer der  neuen  Inode.   War
     Gs_ninodeH  gleich  0,  wird die i-Liste gelesen und die Nummern aller freien
     Inodes (bis zu NICINOD) in das Feld Gs_inode,H  eingetragen.  Danach  erfolgt
     die  Zuweisung  wie  anfangs  beschrieben.  Wird eine Inode freigemacht, so
     wird ihre i-Nummer in Gs_inode[s_ninode]H eingetragen und Gs_ninodeH  inkremen-
     tiert.   Falls Gs_ninodeH bereits NICINODE ist, ist es kein Verlust, wenn die
     Nummer der freien I-Node zuna"chst nicht eingetragen werden kann,  da  diese
     Liste nur zur Beschleunigung des Zuweisungsungsprozesses dient. Die eigent-
     liche Information, dap8 die Inode frei ist, wird in ihr selbst aufbewahrt.

     Die Variablen Gs_flockH und Gs_ilockH  sind  Flags  in  der  Speicherkopie  des
     File-Systems,  solange  es  in das System eingegliedert ist, ihre Werte auf
     der Platte sind uninteressant.  Der Wert von  Gs_fmodH  auf  der  Platte  ist
     ebenfalls  unwichtig.   Er  wird als Flag zum Anzeigen, dap8 der Super-Block
     gewechselt worden ist, verwendet und bewirkt, dap8 der  Super-Block  wa"hrend
     der  na"chsten  periodischen  U"bertragung der File-Systeminformation auf die
     Platte zu u"bertragen ist.  Die  Variable  Gs_ronlyH  ist  der  Schreibschutz-


                                         - G2H -                                  







GFILSYS(5)H                                MUTOS                         GFILSYS(5)H



     Indikator,  der  ebenfalls  nur  wa"hrend  der Zeit der System-Eingliederung
     Bedeutung hat.

     Die Variable Gs_timeH ist die Zeit, bei der der Super-Block des  File-Systems
     das  letzte  Mal  gewechselt  wurde.   Wa"hrend  eines Reboot-Vorganges wird
     Gs_timeH des Super-Blocks fu"r das Root-File-System benutzt, um die  Ausgangs-
     zeit zu setzen.

     Die Variablen Gs_tfreeH, Gs_tinodeH, Gs_fnameH und Gs_fpackH werden  nicht  laufend
     aktualisiert.

     Die erste i-Nummer ist 1. Die Speicherung von Inodes beginnt  im  Block  2.
     Die  La"nge  der  Inodes betra"gt 64 Bytes, so dap8 8 von ihnen in einen Block
     passen.  Die Inode 2  ist  reserviert  fu"r  die  Root-Directory  des  File-
     Systems,  sonst  hat  keine  andere  i-Nummer eine Standardbedeutung.  Jede
     Inode repra"sentiert ein File.  Das Format einer Inode, ist im  Include-File
     <Gsys/ino.hH> definiert:

     /*
      * Inode structure as it appears on
      * a disk block.
      */
     struct dinode
     {
             unsigned short       di_mode;     /* mode and type
                                     of file */
             short   di_nlink;    /* number of links to file */
             short   di_uid;      /* owner's user id */
             short   di_gid;      /* owner's group id */
             off_t   di_size;     /* number of bytes in file */
             char    di_addr[40]; /* disk block addresses */
             time_t  di_atime;    /* time last accessed */
             time_t  di_mtime;    /* time last modified */
             time_t  di_ctime;    /* time created */
     };
     #define INOPB   8            /* 8 inodes per block */
     /*
      * the 40 address bytes:
      *      39 used; 13 addresses
      *      of 3 bytes each.
      */

     Die Variable Gdi_modeH kennzeichnet die Art des Files, sie wird  entsprechend
     der  Variablen Gst_modeH des Systemrufes GstatH(2) entschlu"sselt.  Die Variable
     Gdi_nlinkH ist die Anzahl von Directory-Eintragungen (Links), die  zu  dieser
     Inode zugreifen.  Die Variablen Gdi_uidH und Gdi_gidH enthalten die Nutzer- und
     Gruppen-ID des Eigentu"mers.  Die Variable Gdi_sizeH entha"lt  die  Anzahl  der
     Bytes  im  File.   Die  Variablen Gdi_atimeH und Gdi_mtimeH sind die Zeiten des
     letzten Zugriffes bzw. der  letzten  Modifikation  zum  File-Inhalt  (siehe
     GtimesH(2)).  Die Variable Gdi_ctimeH bewahrt die Zeit der letzten Modifikation
     bezogen auf die Inode bzw. auf das File und wird fu"r die  Entscheidung,  ob
     sie zur Platte zu u"bertragen sind, benutzt.

     Spezial-Files werden an ihrem Modus und nicht an  ihrer  i-Nummer  erkannt.
     Ein  Spezial-File  vom  Typ "blockorientiert" kann potentiell wie ein File-
     System eingegliedert werden.  Ein Spezial-File vom Typ  "zeichenorientiert"
     kann  dies  nicht,  obwohl es nicht notwendigerweise zeichenorientiert sein
     mup8.  Bei Spezial-Files ist das Feld  Gdi_addrH  mit  dem  Gera"tecode  (siehe


                                         - G3H -







GFILSYS(5)H                                MUTOS                         GFILSYS(5)H



     GtypesH(5))  belegt.  Die Gera"tecodes von blockorientierten und zeichenorien-
     tierten Files u"berlappen sich.

     Blockadressen von normalen Files und Directories werden in dem Feld Gdi_addrH
     aufbewahrt,  gepackt  in 3 Bytes.  Die ersten 10 Adressen spezifizieren den
     Gera"teblock direkt.  Die letzten 3  Adressen  sind  einfach,  zweifach  und
     dreifach  indirekt und zeigen auf einen Block von 128 Blockzeigern.  Zeiger
     in Indirektblo"cken haben den Typ Gdaddr_tH (siehe GtypesH(5)).

     Fu"r das Vorhandensein von Block _b in einem File ist es nicht notwendig, dap8
     alle  Blo"cke  kleiner als _b vorhanden sind.  Eine Blocknummer 0 irgendwo in
     den Adrep8worten der Inodes oder in einem  Indirektblock  kennzeichnet,  dap8
     der  korrespondierende Block noch niemals zugewiesen wurde.  Solch ein feh-
     lerhafter Block liest sich so, als wu"rde er nur leere Worte enthalten.
G
SIEHE AUCH H
     icheck(1), dcheck(1), dir(5), mount(1), stat(2), types(5)











































                                         - G4H -





