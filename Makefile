MINGW_CC=x86_64-w64-mingw32-gcc 
JOBS=-j12

all:
	$(MAKE) $(JOBS) -C ami9000 platform=osx
	$(MAKE) $(JOBS) -C geo9000/libretro
	$(MAKE) $(JOBS) -C e9k-debugger

w64:
	$(MAKE) $(JOBS) -C ami9000 platform=win CC=$(MINGW_CC)
	$(MAKE) $(JOBS) -C geo9000/libretro platform=win64 CC=$(MINGW_CC)
	$(MAKE) $(JOBS) -C e9k-debugger w64

clean:
	$(MAKE) $(JOBS) -C ami9000 platform=win CC=$(MINGW_CC) clean
	$(MAKE) $(JOBS) -C geo9000/libretro platform=win64 CC=$(MINGW_CC) clean
	$(MAKE) $(JOBS) -C e9k-debugger clean
