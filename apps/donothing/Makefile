TARGETS = donothing.bin

all : $(TARGETS)

clean :

distclean : clean
	-rm $(TARGETS)

donothing.bin : donothing.s
	xa -M -bt 768 -o donothing.bin donothing.s

.PHONY : all clean distclean
