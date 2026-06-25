/* feed.h -- bulk feeding values into the index (the CLI put aspect),
 * kept out of main.c so the dispatcher stays linear. */
#ifndef AIS_FEED_H
#define AIS_FEED_H

#include "ais.h"

/* File each non-empty stdin line, verbatim, as a value under KEYS. */
void feed_stdin(ais *a, const char *keys);

/* Interactive feed: read each value from stdin and prompt the terminal for keys
 * per value (added to BASE, which may be empty), then put it. Keys are read from
 * /dev/tty -- or from $AIS_TTY (a file) if set, for scripting and testing. */
void feed_interactive(ais *a, const char *base);

/* Import "keys|value" lines from stdin, putting each (the inverse of dump).
 * Blank lines and lines starting with '#' are skipped; idempotent via ais_put. */
void feed_import(ais *a);

/* Like feed_import, but confirm each record first: show "keys | value" and read
 * a [y/N] answer from the terminal (/dev/tty, or $AIS_TTY for scripting/tests).
 * Only y/Y takes the record; N -- the default, including a bare Enter -- skips.
 * Records arrive on stdin, answers on the tty, so the two streams stay separate.
 * For adopting selected bits of another person's shared index/keys-file. */
void feed_import_interactive(ais *a);

/* doc: read a (possibly large, multi-line) document from stdin, save it as a
 * blob file <index>/blobs/<timestamp>.txt, and put that relative path as a value under
 * KEYS. The engine stores only the path; the bytes live in the file. */
void feed_doc(ais *a, const char *keys);

#endif /* AIS_FEED_H */
