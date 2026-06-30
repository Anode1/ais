# util/Makefile -- standalone archive-maintenance helpers.
#
# These are NOT part of the engine: they are small tools the index keeper runs
# to groom a photo/file archive (extract dates, geotags, and so on). They may
# stay dependency-free C, like exif here, or shell out to heavier tools that
# only the maintainer needs installed. The `ais` binary never depends on them.

CC     ?= cc
CFLAGS ?= -O2 -std=c99 -W -Wall

.PHONY: all clean check
all: exif

exif: exif.c
	$(CC) $(CFLAGS) -o $@ exif.c

mktest: mktest.c
	$(CC) $(CFLAGS) -o $@ mktest.c

check: exif mktest
	@sh test.sh

clean:
	-rm -f exif mktest
