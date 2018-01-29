TARGETS = rom.bin

all : $(TARGETS)

clean :

distclean : clean
	-rm $(TARGETS)

rom.bin : rom.s 8x8font.inc
	xa -M -bt 57344 -o rom.bin rom.s

.PHONY : all clean distclean
