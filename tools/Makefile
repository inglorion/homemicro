GPIO = gpio.o
TARGETS = $(GPIO) \
	cartridge/readcart \
	cartridge/writecart \
	gpios_low \
	memory/readmem memory/writemem \
	memory/writerom

all : $(TARGETS)

clean :

distclean :
	-rm $(TARGETS)

.PHONY : all clean distclean

cartridge/readcart : $(GPIO) cartridge/readcart.c

cartridge/writecart : $(GPIO) cartridge/writecart.c

gpio.o : gpio.c gpio.h

gpios_low : $(GPIO) gpios_low.c

memory/readmem : $(GPIO) memory/readmem.c

memory/writemem : $(GPIO) memory/writemem.c

memory/writerom : $(GPIO) memory/writerom.c
