TARGETS = emutest.bin

all : $(TARGETS)

clean :

distclean : clean
	-rm $(TARGETS)

emutest.bin : emutest.s
	xa -M -bt 57344 -o emutest.bin emutest.s

.PHONY : all clean distclean
