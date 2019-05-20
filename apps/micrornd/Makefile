TARGETS = micrornd.bin

all : $(TARGETS)

clean :

distclean : clean
	-rm $(TARGETS)

micrornd.bin : micrornd.s
	xa -M -bt 57344 -o micrornd.bin micrornd.s

.PHONY : all clean distclean
