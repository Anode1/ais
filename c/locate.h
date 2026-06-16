/* locate.h -- resolve which index directory to use (front-end policy).
 *
 * ais_locate writes the chosen index path into OUT (size OUTSZ), by precedence
 * (no env vars; OPT/-f is the only explicit override):
 *   1. OPT (the -f argument) if non-NULL and non-empty
 *   2. the nearest ".ais/" directory at or above the current dir (git-style)
 *   3. the saved default index from ~/.ais/config (an `index = PATH` line)
 *   4. the built-in default ~/.ais (Windows: %USERPROFILE%\.ais), falling back
 *      to the pre-1.0 location if ~/.ais does not exist yet but that one does.
 * Returns 0 on success, -1 on error (path too long, or no home dir for #4).
 * A local index is created explicitly with `ais --init` (a fresh ".ais" here).
 */
#ifndef AIS_LOCATE_H
#define AIS_LOCATE_H

#include <stddef.h>

int ais_locate(const char *opt, char *out, size_t outsz);

/* The saved default index, persisted in ~/.ais/config (used by `ais --default`
 * and the GUIs' "change store"). get: 1 if set (-> OUT), 0 if none, -1 error.
 * set: PATH non-empty saves it, NULL/empty clears it; 0/-1. */
int ais_default_get(char *out, size_t outsz);
int ais_default_set(const char *path);

#endif /* AIS_LOCATE_H */
