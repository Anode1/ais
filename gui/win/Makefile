# gui/win/Makefile -- ais-gui.exe, the Windows launcher (ais-gui.c says what it
# does). Cross-compiled with MinGW-w64, as c/ is:
#   make -C gui/win                              (CC defaults to the mingw gcc)
#   make -C gui/win CC=x86_64-w64-mingw32-gcc AIS_VERSION=0.3.40
# scripts/dist.sh win and native-windows.yml run it; tests/gui/windows.sh
# checks it still builds.
ifeq ($(origin CC),default)
CC = x86_64-w64-mingw32-gcc
endif
WINDRES ?= $(patsubst %-gcc,%-windres,$(CC))
CFLAGS  ?= -O2

# Version: the tag, as c/Makefile derives it; the .rc also wants four numbers.
# Both reach windres through version.h: its -D mangles a quoted string.
AIS_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null | sed 's/^v//')
ifeq ($(strip $(AIS_VERSION)),)
AIS_VERSION := 0.0.0-dev
endif
AIS_VER_NUM := $(shell echo '$(AIS_VERSION)' | sed -n -E 's/^([0-9]+)\.([0-9]+)\.([0-9]+).*/\1,\2,\3,0/p')
ifeq ($(strip $(AIS_VER_NUM)),)
AIS_VER_NUM := 0,0,0,0
endif

all: ais-gui.exe

# -mwindows: the GUI subsystem, so no console opens; -static: no libgcc DLL.
ais-gui.exe: ais-gui.c ais-gui.res
	$(CC) -std=gnu99 -W -Wall $(CFLAGS) -mwindows -static -o $@ ais-gui.c ais-gui.res -lws2_32 -lshell32

ais-gui.res: ais-gui.rc version.h ../../icons/ais.ico
	$(WINDRES) -O coff -o $@ ais-gui.rc

version.h: FORCE
	@printf '#define AIS_VERSION "%s"\n#define AIS_VER_NUM %s\n' '$(AIS_VERSION)' '$(AIS_VER_NUM)' \
	    | cmp -s - $@ 2>/dev/null || \
	printf '#define AIS_VERSION "%s"\n#define AIS_VER_NUM %s\n' '$(AIS_VERSION)' '$(AIS_VER_NUM)' > $@

clean:
	rm -f ais-gui.exe ais-gui.res version.h

.PHONY: all clean FORCE
FORCE:
