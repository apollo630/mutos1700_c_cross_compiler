# Make a mutos.  Options:


# CONFIG may be one or more of the following:
# KDEBUG MSDOS LOAD86
CONFIG = 

# DRIVER may be one or more of the following:
# 7100/7150:	LP IFSS IFSP V24 RAM AMX DABA V30IDE
# 1834:		LP ASK(IFSS+V24) RAM DABA

# FLAGS may be one or more of the following:
# -DINSTRM -DDISKMON -DSYSACCT -DSYSLOCK
FLAGS = 

### 7100 only:
TYP = M7100
HTYP = AC
STYP = NOBIOS
DRIVER = V30IDE V24 IFSS 

### 7150 only:
# TYP = M7150
# HTYP = AC
# STYP = BIOS
# DRIVER = LP IFSS V24

### 1834 only:
# TYP = M1834
# HTYP = EC
# STYP = BIOS
# DRIVER = LP ASK 

SETOPT = @for confopt in $(TYP) $(HTYP) $(STYP) $(CONFIG) $(DRIVER); \
	 do eval $$confopt=;done; \
	 eval $${IFSS+'ASP=;ASK='}; eval $${V24+'ASP=;ASK='}; \
	 eval $${IFSP+'ASP='}; \
	 set -x

CONFOPT = -D$(TYP) $${LP+-DLP} $${AMX+-DAMX} $${ASK+-DASK} $${BUS+-DBUS} \
	  $${IFSP+-DIFSP} $${IFSS+-DIFSS} $${V24+-DV24} \
	  $${RAM+-DRAM} $${MSDOS+-DMSDOS} $${KDEBUG+-DKDEBUG} \
	  $${LOAD86+-DLOAD86} $${DABA+-DDABA} $${V30IDE+-DV30IDE}

CFLAGS = -c -O -K1024
LFLAGS = -i

H	=	../h
CFG	=	../cfg
IO	=	../io
MDEP	=	../mdep
SYS	=	../sys


SYSOBJ=	$(SYS)/acct.o $(SYS)/alloc.o $(SYS)/bio.o $(SYS)/clocal.o \
	$(SYS)/fio.o $(SYS)/iget.o $(SYS)/clock.o \
	$(SYS)/malloc.o $(SYS)/nami.o $(SYS)/pipe.o \
	$(SYS)/prf.o $(SYS)/prim.o $(SYS)/rdwri.o $(SYS)/sig.o $(SYS)/slp.o \
	$(SYS)/sys1.o $(SYS)/sys2.o $(SYS)/sys3.o $(SYS)/sys4.o \
	$(SYS)/sysent.o $(SYS)/sysx.o $(SYS)/subr.o


IOOBJ =		$(IOOBJ_7100) $(IOOBJ_7150) $(IOOBJ_1834) $(IOOBJ_COMM)

IOOBJ_AC =	$(IO)/intr_AC.o $(IO)/lp_AC.o $(IO)/bus.o \
		$(IO)/amx.o $(IO)/k5170.o $(IO)/ifss.o $(IO)/v24.o \
		$(IO)/ifsp.o $(IO)/v30ide.o

IOOBJ_7100 =	$(IOOBJ_AC) $(IO)/cons_M7100.o

IOOBJ_7150 =	$(IOOBJ_AC) $(IO)/cons_M7150.o

IOOBJ_1834 = 	$(IO)/intr_EC.o $(IO)/lp_EC.o $(IO)/ask.o \
		$(IO)/cons_M1834.o $(IO)/hd.o $(IO)/floppy.o

IOOBJ_COMM = 	$(IO)/dsort.o $(IO)/mem.o $(IO)/sys.o $(IO)/tty.o \
		$(IO)/delay.o $(IO)/ramfl.o $(IO)/dlock.o

MDEPOBJ=	$(MDEP)/debug.o $(MDEP)/machdep.o $(MDEP)/main.o \
		$(MDEP)/physio.o $(MDEP)/sbreak.o $(MDEP)/text.o \
		$(MDEP)/trap.o $(MDEP)/dosent.o $(MDEP)/load86.o \
		$(MDEP)/msdos.o $(MDEP)/kdebug.o

CFGOBJ =	$(CFG)/cram.o $(CFG)/ccons.o

SPEC_7100 = 	$(MDEP)/genio_M7100.o $(MDEP)/gcpc_NOBIOS.o \
		c_M7100.o symtab_M7100.o \
		$(CFG)/c5170.o $(CFG)/casp.o $(CFG)/camx.o $(CFG)/clp_AC.o \
		$(MDEP)/mch_M7100.o

SPEC_7150 =	$(MDEP)/genio_M7150.o $(MDEP)/gcpc_BIOS.o \
		c_M7150.o symtab_M7150.o \
		$(CFG)/c5170.o $(CFG)/casp.o $(CFG)/camx.o $(CFG)/clp_AC.o \
		$(MDEP)/mch_M7150.o

SPEC_1834 = 	$(MDEP)/genio_M1834.o $(MDEP)/gcpc_BIOS.o \
		c_M1834.o symtab_M1834.o \
		$(CFG)/cwf.o $(CFG)/cask.o $(CFG)/clp_EC.o \
		$(MDEP)/mch_M1834.o


  mutos:	$(IOOBJ_7100) $(SPEC_7100)			### 7100 only
# mutos:	$(IOOBJ_7150) $(SPEC_7150)			### 7150 only
# mutos:	$(IOOBJ_1834) $(SPEC_1834)			### 1834 only
  mutos:	$(CFGOBJ) $(MDEPOBJ) $(SYSOBJ) $(IOOBJ_COMM) arith.a
	$(SETOPT);\
	ld $(LFLAGS) -o mutos \
		symtab_$(TYP).o $(MDEP)/mch_$(TYP).o \
		c_$(TYP).o $(MDEP)/gcpc_$(STYP).o \
		$(MDEP)/debug.o $(MDEP)/main.o $(MDEP)/genio_$(TYP).o \
		$(MDEP)/trap.o $(MDEP)/machdep.o \
		$(MDEP)/sbreak.o $(MDEP)/physio.o $(MDEP)/text.o \
		$(IO)/mem.o $(IO)/sys.o $(IO)/tty.o \
		$(IO)/cons_$(TYP).o $(IO)/intr_$(HTYP).o \
		$${LP+$(IO)/lp_$(HTYP).o} \
		$${AC+$(IO)/k5170.o} $${EC+$(IO)/hd.o} $${EC+$(IO)/floppy.o} \
		$(IO)/delay.o $(IO)/dsort.o \
		$${IFSS+$${AC+$(IO)/ifss.o}} $${V24+$${AC+$(IO)/v24.o}} \
		$${V30IDE+$${AC+$(IO)/v30ide.o}} \
		$${ASK+$${EC+$(IO)/ask.o}} $${IFSP+$${AC+$(IO)/ifsp.o}} \
		$${BUS+$${AC+$(IO)/bus.o}} $${AMX+$${AC+$(IO)/amx.o}} \
		$${RAM+$(IO)/ramfl.o} $${DABA+$(IO)/dlock.o} \
		$(SYS)/clock.o $(SYS)/prim.o $(SYS)/sysent.o $(SYS)/sys2.o \
		$(SYS)/sys3.o $(SYS)/sys4.o $(SYS)/sysx.o $(SYS)/sys1.o \
		$(SYS)/acct.o $(SYS)/malloc.o $(SYS)/fio.o $(SYS)/slp.o \
		$(SYS)/sig.o $(SYS)/nami.o $(SYS)/alloc.o $(SYS)/iget.o \
		$(SYS)/rdwri.o $(SYS)/subr.o $(SYS)/bio.o $(SYS)/prf.o \
		$(SYS)/pipe.o $(SYS)/clocal.o \
		$${MSDOS+$(MDEP)/dosent.o} $${MSDOS+$(MDEP)/msdos.o} \
		$${LOAD86+$(MDEP)/load86.o} \
		$${AC+$(CFG)/c5170.o} $${EC+$(CFG)/cwf.o} \
		$${LP+$(CFG)/clp_$(HTYP).o} $${RAM+$(CFG)/cram.o} \
		$${ASP+$${AC+$(CFG)/casp.o}} $${ASK+$${EC+$(CFG)/cask.o}} \
		$(CFG)/ccons.o $${KDEBUG+$${BIOS+$(MDEP)/kdebug.o}} \
		arith.a
	-size mutos;sync

$(IO)/intr_$(HTYP).o:	$(IO)/intr.c
	$(SETOPT); \
	cd $(IO); \
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) intr.c; mv intr.o intr_$(HTYP).o

$(IO)/bus.o $(IO)/amx.o $(IO)/k5170.o $(IO)/ifss.o $(IO)/v24.o $(IO)/ifsp.o \
$(IO)/ask.o $(IO)/hd.o $(IO)/floppy.o $(IO)/lp_$(HTYP).o $(IO)/v30ide.o:
	$(SETOPT); \
	cd $(IO); \
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) $*.c

$(IO)/cons_$(TYP).o:	$(IO)/cons_$(STYP).c
	$(SETOPT); \
	cd $(IO); \
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) $(IO)/cons_$(STYP).c; \
	mv $(IO)/cons_$(STYP).o $(IO)/cons_$(TYP).o

$(MDEP)/genio_$(TYP).o:	$(MDEP)/genio.c
	$(SETOPT); \
	cd $(MDEP); \
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) genio.c; mv genio.o genio_$(TYP).o

$(MDEP)/gcpc_$(STYP).o:	$(MDEP)/gcpc.c
	$(SETOPT); \
	cd $(MDEP); \
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) gcpc.c; mv gcpc.o gcpc_$(STYP).o

$(CFG)/c5170.o $(CFG)/casp.o $(CFG)/camx.o $(CFG)/cwf.o $(CFG)/cask.o: 
	$(SETOPT); \
	cd $(CFG); \
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) $*.c

$(CFG)/clp_$(HTYP).o:	$(CFG)/clp.c
	$(SETOPT); \
	cd $(CFG); \
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) clp.c; mv clp.o clp_$(HTYP).o

c_$(TYP).o:    c.c
	$(SETOPT);\
	cc -c $(CONFOPT) c.c; mv c.o c_$(TYP).o

$(MDEP)/mch_$(TYP).o:	$(MDEP)/mch.c
	$(SETOPT);\
	cd $(MDEP);\
	cc -P $(CONFOPT) $(MDEP)/mch.c;\
	em mch.i <fakeawk >/dev/null;\
	mv mch.i mch.s;\
	cc -c mch.s;\
	mv mch.o mch_$(TYP).o;\
	rm mch.[is]


symtab_$(TYP).o: symtab.c
	$(SETOPT);\
	cc -c $(CONFOPT) symtab.c; mv symtab.o $@

$(SYSOBJ):
	$(SETOPT);\
	cd $(SYS);\
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) $*.c

$(MDEPOBJ):
	$(SETOPT);\
	cd $(MDEP);\
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) $*.c

$(IOOBJ_COMM):
	$(SETOPT);\
	cd $(IO);\
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) $*.c

$(CFGOBJ):
	$(SETOPT);\
	cd $(CFG) ;\
	cc $(CFLAGS) $(CONFOPT) $(FLAGS) $*.c

show:
	@echo "Typ: $(TYP)  Options: $(DRIVER) $(CONFIG) $(FLAGS)"

save:
	@echo -n "Please, insert floppy into drive 1 and hit return:"; \
	read xxx;cd ..;tar cv */*.[ch] conf/Makefile conf/arith.a mdep/fakeawk

7100 7150 1834:
	@(echo "g/^### [17][18][035][04] only:$$/+,+4s/^[^#]/# &/"; \
	echo "g/### [17][18][035][04] only$$/s/^  /# /"; \
	echo "g/^### $@ only:$$/+,+4s/^# //"; \
	echo "g/### $@ only$$/s/^# /  /"; echo w) >eddep; \
	cp Makefile Makefile.bak
	@ed - Makefile <eddep
	@rm eddep
	@touch arith.a

AMX noAMX ASK noASK BUS noBUS IFSP noIFSP IFSS noIFSS V24 noV24 LP noLP \
RAM noRAM DABA noDABA KDEBUG noKDEBUG MSDOS noMSDOS LOAD86 noLOAD86 \
INSTRM noINSTRM DISKMON noDISKMON SYSACCT noSYSACCT SYSLOCK noSYSLOCK \
V30IDE noV30IDE:
	@case $@ in\
		LP)		o=LP;		a=Y;	s=DRIVER;;\
		noLP)		o=LP;		a=;	s=DRIVER;;\
		AMX)		o=AMX;		a=Y;	s=DRIVER;;\
		noAMX)		o=AMX;		a=;	s=DRIVER;;\
		ASK)		o=ASK;		a=Y;	s=DRIVER;;\
		noASK)		o=ASK;		a=;	s=DRIVER;;\
		BUS)		o=BUS;		a=Y;	s=DRIVER;;\
		noBUS)		o=BUS;		a=;	s=DRIVER;;\
		IFSP)		o=IFSP;		a=Y;	s=DRIVER;;\
		noIFSP)		o=IFSP;		a=;	s=DRIVER;;\
		IFSS)		o=IFSS;		a=Y;	s=DRIVER;;\
		noIFSS)		o=IFSS;		a=;	s=DRIVER;;\
		V24)		o=V24;		a=Y;	s=DRIVER;;\
		noV24)		o=V24;		a=;	s=DRIVER;;\
		RAM)		o=RAM;		a=Y;	s=DRIVER;;\
		noRAM)		o=RAM;		a=;	s=DRIVER;;\
		DABA)		o=DABA;		a=Y;	s=DRIVER;;\
		noDABA)		o=DABA;		a=;	s=DRIVER;;\
		V30IDE)		o=V30IDE;	a=Y;	s=DRIVER;;\
		noV30IDE)	o=V30IDE;	a=;	s=DRIVER;;\
		MSDOS)		o=MSDOS;	a=Y;	s=CONFIG;;\
		noMSDOS)	o=MSDOS;	a=;	s=CONFIG;;\
		KDEBUG)		o=KDEBUG;	a=Y;	s=CONFIG;;\
		noKDEBUG)	o=KDEBUG;	a=;	s=CONFIG;;\
		LOAD86)		o=LOAD86;	a=Y;	s=CONFIG;;\
		noLOAD86)	o=LOAD86;	a=;	s=CONFIG;;\
		INSTRM)		o=INSTRM;	a=Y;	s=FLAGS;;\
		noINSTRM)	o=INSTRM;	a=;	s=FLAGS;;\
		DISKMON)	o=DISKMON;	a=Y;	s=FLAGS;;\
		noDISKMON)	o=DISKMON;	a=;	s=FLAGS;;\
		SYSACCT)	o=SYSACCT;	a=Y;	s=FLAGS;;\
		noSYSACCT)	o=SYSACCT;	a=;	s=FLAGS;;\
		SYSLOCK)	o=SYSLOCK;	a=Y;	s=FLAGS;;\
		noSYSLOCK)	o=SYSLOCK;	a=;	s=FLAGS;;\
	esac;\
	case $$s in\
		FLAGS)	echo "g/^$$s =/s/-D$$o *//g" >eddep;\
			case $$a in\
				Y)	echo "s/ *\$$/ -D$$o/" >>eddep;;\
			esac;;\
		*)	echo "g/^$$s =/s/$$o *//g" >eddep;\
			case $$a in\
				Y)	echo "s/ *\$$/ $$o/" >>eddep;;\
			esac;;\
	esac;\
	echo "w" >>eddep;\
	touch `grep -l "#[ 	]*ifn*def.*$$o" \
	*.c $(SYS)/*.c $(MDEP)/*.c $(IO)/*.c $(CFG)/*.c $(H)/*.h`
	@cp Makefile Makefile.bak;\
	ed - Makefile <eddep;\
	rm eddep

depend:
	@-for i in *.c $(SYS)/*.c $(MDEP)/*.c $(IO)/*.c $(CFG)/*.c; do \
		(if /bin/grep '^#[ 	]*include' /dev/null $$i | sed \
			-e 's/:[^"]*"\([^"]*\)".*/: \1/'; \
			then echo '	touch $$@'; \
		else true; \
		fi); \
	done >makedep
	@(echo '/^# DO NOT DELETE THIS LINE/+2,$$d'; \
	echo '$$r makedep'; echo 'w') >eddep
	@cp Makefile Makefile.bak
	@ed - Makefile < eddep
	@rm eddep makedep
	@(echo '# DEPENDENCIES MUST END AT END OF FILE'; \
	echo '# IF YOU PUT STUFF HERE IT WILL GO AWAY'; \
	echo '# see make depend above') >> Makefile

# DO NOT DELETE THIS LINE -- make depend uses it
# DEPENDENCIES MUST END AT END OF FILE

