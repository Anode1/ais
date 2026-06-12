#
# Copyright (C) 2001 Vasili Gavrilov
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or any later version.
#
# AIS re-engineering (2026): ANSI C / C99, plain-text sharded store (no binary backend).
#
# Honors the standard variables, so packagers and other compilers just work:
#   CC CFLAGS CPPFLAGS LDFLAGS LDLIBS  -- project flags are APPENDED, never
#   override yours (distros inject hardening; macOS/BSD supply clang as cc).
# Profiles:  make | make release | make debug | make pedantic | make static
#   `static` is Linux-only: macOS/BSD ship no static libc, and there the plain
#   `make` binary links only the always-present system libc -- equally portable.
#

SHELL = /bin/sh

BIN     = ais
TESTBIN = ais_ut

UNAME := $(shell uname)

# -- toolchain (overridable on the command line or by the packager) --------
CC       ?= cc
CFLAGS   ?= -O2
CPPFLAGS ?=
LDFLAGS  ?=
LDLIBS   ?=

# -- project-required flags (always applied, before yours) -----------------
NOCYGWIN =
ifeq ($(UNAME), CYGWIN32/95)
	NOCYGWIN = -mno-cygwin
endif
ifeq ($(UNAME), CYGWIN32/NT)
	NOCYGWIN = -mno-cygwin
endif

# macOS/clang hide BSD+POSIX extras (flock, LOCK_*, INADDR_LOOPBACK, ...) under
# -std=c99 (__STRICT_ANSI__). _DARWIN_C_SOURCE re-exposes the full system API.
# glibc exposes them by default, so this is Darwin-only.
AIS_PORT =
ifeq ($(UNAME), Darwin)
	AIS_PORT = -D_DARWIN_C_SOURCE
endif

AIS_STD    = -std=c99
AIS_WARN   = -W -Wall
AIS_CFLAGS = $(NOCYGWIN) $(AIS_PORT) $(AIS_STD) $(AIS_WARN)

# all *.c are sources (no editing this file when a new .c is added)
SOURCES.c := $(wildcard *.c)
OBJS       = $(SOURCES.c:.c=.o)

# Library form (for embedders / the Flutter FFI seam): the engine MINUS main().
# A library must not carry main(); the host app (or the FFI loader) provides it.
LIBOBJS = $(filter-out main.o,$(OBJS))
LIBSRC  = $(filter-out main.c,$(SOURCES.c))

# -- profiles tweak only the standard knobs --------------------------------
debug    : CFLAGS   = -g -O0
debug    : AIS_WARN += -Wundef
pedantic : AIS_STD   = -std=c99 -pedantic
pedantic : AIS_WARN += -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations
release  : CFLAGS    = -O2
static   : LDFLAGS  += -static

.SUFFIXES:
.SUFFIXES: .d .o .h .c
.c.o: ; $(CC) $(AIS_CFLAGS) $(CPPFLAGS) $(CFLAGS) -MMD -c $*.c

.PHONY: clean ut check static static-guard lib

all release debug pedantic: $(BIN)

# static: a fully self-contained binary -- Linux only (macOS has no static libc).
static: static-guard $(BIN)
static-guard:
ifeq ($(UNAME),Darwin)
	@echo "ais: 'make static' is unsupported on macOS (no static libSystem)." >&2
	@echo "     Use plain 'make' -- the dynamic binary links only libSystem," >&2
	@echo "     present on every macOS, so it carries no add-on libraries." >&2
	@false
endif

$(BIN): $(OBJS)
	$(CC) $(AIS_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $(BIN) $(OBJS) $(LDLIBS)

# Test binary: all sources with -DUNIT_TEST; rebuilt only when a source changes.
# `ut`/`check` are .PHONY, so they ALWAYS run (rebuilding first if needed).
$(TESTBIN): $(SOURCES.c)
	$(CC) $(AIS_CFLAGS) -g -DUNIT_TEST $(CPPFLAGS) $(SOURCES.c) -o $(TESTBIN) $(LDLIBS)

ut check: $(TESTBIN)
	./$(TESTBIN)

# -- libais: the engine as a library, for embedders (Flutter dart:ffi, native
# plugins). libais.a = static (linked into the app, e.g. iOS); libais.so =
# shared, for dlopen-style FFI (Linux desktop, Android). Both exclude main().
lib: libais.a libais.so
libais.a: $(LIBOBJS)
	ar rcs $@ $(LIBOBJS)
libais.so: $(LIBSRC)
	$(CC) $(AIS_CFLAGS) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -o $@ $(LIBSRC) $(LDLIBS)

clean:
	-rm -f $(BIN) $(TESTBIN) $(OBJS) libais.a libais.so *.d

-include $(OBJS:.o=.d)
