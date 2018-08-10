AVR_AS ?= avr-as
AVR_LD ?= avr-ld
AVR_OBJCOPY ?= avr-objcopy

TARGETS = ps2kbd-m328.hex
OBJECTS = ps2kbd-m328.elf ps2kbd-m328.o

all : $(TARGETS)

clean :
	-rm $(OBJECTS)

distclean :
	-rm $(OBJECTS) $(TARGETS)

.o.elf :
	$(AVR_LD) $< -o $@

.elf.hex :
	$(AVR_OBJCOPY) -O ihex $< $@

.s.o :
	$(AVR_AS) -mmcu=atmega328 $< -o $@

.SUFFIXES : .elf .hex .o .s

.PHONY : all clean distclean
