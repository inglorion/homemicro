GPIO = gpio.o
TARGETS = $(GPIO) \
	gpios_low \
	memory/readmem memory/writemem \
	memory/writerom

all : $(TARGETS)

clean :

distclean :
	-rm $(TARGETS)

.PHONY : all clean distclean

gpio.o : gpio.c gpio.h

gpios_low : $(GPIO) gpios_low.c

memory/readmem : $(GPIO) memory/readmem.c

memory/writemem : $(GPIO) memory/writemem.c

memory/writerom : $(GPIO) memory/writerom.c
