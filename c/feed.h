/* feed.h -- bulk feeding values into the index (the CLI put -/-R aspect),
 * kept out of main.c so the dispatcher stays linear. */
#ifndef AIS_FEED_H
#define AIS_FEED_H

#include "ais.h"

/* File each non-empty stdin line, verbatim, as a value under KEYS. */
void feed_stdin(ais *a, const char *keys);

/* File every regular file under DIR (recursive) as a value under KEYS. */
void feed_dir(ais *a, const char *dir, const char *keys);

/* Interactive feed: read each value from stdin and prompt the terminal for keys
 * per value (added to BASE, which may be empty), then put it. Keys are read from
 * /dev/tty -- or from $AIS_TTY (a file) if set, for scripting and testing. */
void feed_interactive(ais *a, const char *base);

#endif /* AIS_FEED_H */
