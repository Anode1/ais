# win32/Makefile -- native Win32 GUI (ais-gui.exe), an ISOLATED wrapper.
#
# It compiles the GUI front-end plus the engine core (via the public FFI seam)
# into one self-contained exe. The pure-C core in ../c is never modified or
# rebuilt by this Makefile -- the wrapper only depends on embed.h + locate.h.
#
# Cross-compiled from Linux CI with MinGW-w64 (see
# ../.github/workflows/native-windows.yml):
#   make -C win32 CC=x86_64-w64-mingw32-gcc
# A native Windows toolchain works too if CC points at a MinGW gcc.

CC      ?= x86_64-w64-mingw32-gcc
WINDRES ?= x86_64-w64-mingw32-windres
CFLAGS  ?= -O2 -std=gnu99 -W -Wall
CORE     = ../c

# engine core + FFI seam (embed) + index policy (locate) + Windows shims (win);
# deliberately NOT main/serve/feed/help/tests -- the GUI needs none of them.
ENGINE = $(addprefix $(CORE)/, embed.c ais.c store.c post.c key.c merge.c \
           compact.c stats.c find.c doc.c locate.c log.c win.c)

# only ever-present system DLLs -- no .NET, no Cygwin, no extra runtime.
# shell32: ShellExecute (open URLs) + SHGetFolderPath (the local app-data dir).
# ws2_32: Winsock, referenced by win.c (ais_net_init); always present on Windows.
WINLIBS = -lcomctl32 -lgdi32 -luser32 -lshell32 -lws2_32

.PHONY: all clean
all: ais-gui.exe

# ais.res.o embeds the application manifest (visual styles + asInvoker).
ais-gui.exe: ais-gui.c $(ENGINE) ais.res.o
	$(CC) $(CFLAGS) -I$(CORE) -mwindows -static -o $@ ais-gui.c $(ENGINE) ais.res.o $(WINLIBS)

ais.res.o: ais.rc ais.manifest
	$(WINDRES) ais.rc -O coff -o ais.res.o

clean:
	-rm -f ais-gui.exe ais.res.o
