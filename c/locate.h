/* locate.h -- resolve which index directory to use (front-end policy).
 *
 * ais_locate writes the chosen index path into OUT (size OUTSZ), by precedence:
 *   1. OPT (the -f argument) if non-NULL and non-empty
 *   2. $AIS_INDEX if set
 *   3. the nearest ".ais/" directory at or above the current dir (git-style)
 *   4. the per-user index: $XDG_DATA_HOME/ais, else $HOME/.local/share/ais
 *      (its parent directories are created). There is NO implicit ./INDEX.
 * Returns 0 on success, -1 on error (path too long, or no HOME/XDG for #4).
 * A local index is created explicitly with `ais init` (a fresh ".ais" here).
 */
#ifndef AIS_LOCATE_H
#define AIS_LOCATE_H

#include <stddef.h>

int ais_locate(const char *opt, char *out, size_t outsz);

#endif /* AIS_LOCATE_H */
