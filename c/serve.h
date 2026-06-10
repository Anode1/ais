/* serve.h -- `ais serve`: a tiny built-in localhost web GUI (no deps but libc).
 *
 * A minimal single-threaded HTTP/1.0 loop on 127.0.0.1 serving an embedded page
 * and two endpoints that call the engine directly:
 *   GET  /api/get?keys=k1+k2     -> id|value lines   (ais_get + ais_record)
 *   POST /api/put?keys=k1+k2     -> body = values, one per line (ais_put each)
 * A SKETCH: localhost only, one client at a time, minimal request parsing.
 * Returns -1 on setup failure; otherwise runs until the process is killed.
 */
#ifndef AIS_SERVE_H
#define AIS_SERVE_H

#include "ais.h"

int ais_serve(ais *a, int port);

#endif /* AIS_SERVE_H */
