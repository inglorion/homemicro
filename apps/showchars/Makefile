TARGETS = showchars.bin

all : $(TARGETS)

clean :

distclean : clean
	-rm $(TARGETS)

showchars.bin : showchars.s
	xa -M -bt 57344 -o showchars.bin showchars.s

.PHONY : all clean distclean
