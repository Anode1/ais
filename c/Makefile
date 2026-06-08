#
# Copyright (C) 2001 Vasili Gavrilov
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or any later version.
#
# AIS re-engineering (2026): ANSI C / C99, SQLite-ready pluggable backend.
#

SHELL=/bin/sh

BIN=ais
TESTBIN=ais_ut

ARCH := `uname`

NOCYGWIN=

ifeq ($(ARCH), CYGWIN32/95)
	NOCYGWIN= -mno-cygwin
endif
ifeq ($(ARCH), CYGWIN32/NT)
	NOCYGWIN= -mno-cygwin
endif


# consider all *.c as sources (no editing this file when adding new .c)
SOURCES.c := $(wildcard *.c)


CFLAGS= $(NOCYGWIN) -std=c99 -W -Wall
CPPFLAGS=
CC=gcc
SLIBS=
INCLUDES=
OBJS=$(SOURCES.c:.c=.o)
LINK=gcc $(CFLAGS)
LFLAGS=-lm

debug    : CFLAGS = $(NOCYGWIN) -std=c99 -W -Wall -g -Wundef
pedantic : CFLAGS = $(NOCYGWIN) -std=c99 -pedantic -W -Wall -g -Wundef -Wstrict-prototypes -Wmissing-prototypes -Wmissing-declarations
release  : CFLAGS = $(NOCYGWIN) -std=c99 -W -Wall -O2
.SUFFIXES:
.SUFFIXES: .d .o .h .c
.c.o: ; $(CC) $(INCLUDES) $(CFLAGS) -MMD -c $*.c

.PHONY: clean ut

all release debug pedantic: $(BIN)

$(BIN): $(OBJS)
	$(LINK) $(FLAGS) -o $(BIN) $(OBJS) $(LFLAGS)

# ut: build a SEPARATE test binary straight from sources (its own flags) and
# run it. Standalone + .PHONY, so it ALWAYS executes -- never the shared-$(BIN)
# "Nothing to be done" staleness. Mirrors kul's `ut` (compile + run); exit is
# non-zero if any check fails.
ut:
	$(CC) $(NOCYGWIN) -std=c99 -W -Wall -g -DUNIT_TEST $(INCLUDES) $(SOURCES.c) -o $(TESTBIN) $(LFLAGS)
	./$(TESTBIN)

clean:
	-rm -f $(BIN) $(TESTBIN) $(OBJS) *.d

-include $(OBJS:.o=.d)
