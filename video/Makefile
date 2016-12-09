AVR_AS ?= avr-as
AVR_LD ?= avr-ld
AVR_OBJCOPY ?= avr-objcopy

TARGETS = vga.hex vga-m328.hex vga-m328-ntsc.hex
OBJECTS = vga.elf vga.o vga-m328.elf vga-m328.o vga-m328-ntsc.elf vga-m328-ntsc.o

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
