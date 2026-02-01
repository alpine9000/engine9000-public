AMIGA_GCC=/opt/amiga/bin/m68k-amigaos-gcc
CFLAGS=-fomit-frame-pointer -noixemul -Wall -Werror

all: load9000 example

load9000: main.c Makefile
	$(AMIGA_GCC) $(CFLAGS) $< -o $@ -lamiga

example: example.c Makefile
	$(AMIGA_GCC) $(CFLAGS) -g $< -o $@

clean:
	rm load9000 example
