TARGETS = testkeys.bin

all : $(TARGETS)

clean :

distclean : clean
	-rm $(TARGETS)

testkeys.bin : testkeys.s
	xa -M -bt 57344 -o testkeys.bin testkeys.s

.PHONY : all clean distclean
