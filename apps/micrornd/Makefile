TARGETS = micrornd.bin test test.bin

all : $(TARGETS)

clean :

distclean : clean
	-rm $(TARGETS)

test : test.o test.bin
	$(CC) $(CFLAGS) test.o -o test

micrornd.bin : micrornd.s micrornd_code.inc micrornd_data.inc
	xa -M -bt 57344 -o micrornd.bin micrornd.s

test.bin : test.s micrornd_code.inc micrornd_data.inc
	xa -M -bt 1024 -o test.bin test.s

test.o : test.c ../../emulator/hm1000.h ../../emulator/hm1000.c
	$(CC) $(CFLAGS) -I../../emulator -c test.c

.PHONY : all clean distclean
