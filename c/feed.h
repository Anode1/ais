/* feed.h -- bulk feeding values into the index (the CLI put -/-R aspect),
 * kept out of main.c so the dispatcher stays linear. */
#ifndef AIS_FEED_H
#define AIS_FEED_H

#include "ais.h"

/* File each non-empty stdin line, verbatim, as a value under KEYS. */
void feed_stdin(ais *a, const char *keys);

/* File every regular file under DIR (recursive) as a value under KEYS. */
void feed_dir(ais *a, const char *dir, const char *keys);

#endif /* AIS_FEED_H */
