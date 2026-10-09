/* serve.h -- `ais --serve`: a built-in localhost web GUI (no deps but libc).
 * Single-threaded HTTP/1.0 on 127.0.0.1 serving an embedded page and 24
 * /api/ routes that call the engine directly (recall, put, edit, delete, tags,
 * timeline, sync, bundle import and export; the list is in serve.c's request
 * handler). Headers and body are read in loops; an import-bundle body may be up
 * to 64 MiB. Localhost only, one request at a time; a sync Host runs beside the
 * loop. Returns -1 on setup failure; otherwise runs until the process is killed.
 */
#ifndef AIS_SERVE_H
#define AIS_SERVE_H

#include "ais.h"

int ais_serve(ais *a, int port);

#endif /* AIS_SERVE_H */
