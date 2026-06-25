/* locate.h -- resolve which index directory to use (front-end policy).
 *
 * ais_locate writes the chosen index path into OUT (size OUTSZ), by precedence
 * (no env vars; OPT/-f is the only explicit override):
 *   1. OPT (the -f argument) if non-NULL and non-empty
 *   2. the nearest ".ais/" directory at or above the current dir (git-style)
 *   3. the CURRENT named index from ~/.ais/config (`current = NAME` ->
 *      `index.NAME = PATH`); if no `current`, the legacy `index = PATH` line
 *   4. the built-in default ~/.ais (the "home" index; Windows: %USERPROFILE%\.ais),
 *      falling back to the pre-1.0 location if ~/.ais does not exist yet.
 * Returns 0 on success, -1 on error (path too long, or no home dir for #4).
 * A local index is created explicitly with `ais --init` (a fresh ".ais" here).
 *
 * Multiple indexes work like git branches: a registry of named indexes plus a
 * "current" pointer, all in ~/.ais/config. The built-in name "home" is ~/.ais
 * and is always available (never stored). `--switch` moves current; `--indexes`
 * lists them. Switching just repoints "current" -- indexes are separate stores,
 * so there is no merge of history (move records with --import / --import-interactively).
 */
#ifndef AIS_LOCATE_H
#define AIS_LOCATE_H

#include <stddef.h>

int ais_locate(const char *opt, char *out, size_t outsz);

/* The built-in "home" index path (~/.ais). 0/-1. */
int ais_home_path(char *out, size_t outsz);

/* Override the home dir used for ~/.ais and ~/.ais/config (NULL/"" restores the
 * OS account home). A test seam, and a hook for embedders wanting a custom
 * config home; affects every function here. */
void ais_home_override(const char *dir);

/* The CURRENT index name (the `current = NAME` line that --switch sets).
 * 1 if set (-> OUT), 0 if none (means the built-in home), -1 on error. */
int ais_current_get(char *out, size_t outsz);
/* Set the current index to NAME. NAME NULL/""/"home" clears it (back to home). 0/-1. */
int ais_current_set(const char *name);

/* Resolve a registered index NAME to its PATH. "home" (and ""/NULL) -> ~/.ais.
 * 1 -> OUT, 0 if NAME is not registered, -1 on error. */
int ais_index_path(const char *name, char *out, size_t outsz);
/* A sensible default directory for a NEW named index: ~/.ais-NAME. 0/-1. */
int ais_index_default_dir(const char *name, char *out, size_t outsz);
/* Register NAME -> PATH (overwrites). "home" is reserved (returns -1). 0/-1. */
int ais_index_add(const char *name, const char *path);
/* Forget the registry entry for NAME (the data dir is left untouched).
 * 0 on success (also if NAME was absent), -1 on error / reserved name. */
int ais_index_remove(const char *name);

/* Enumerate registered indexes (NOT the built-in home) as CB(name, path, ctx).
 * CB returns 0 to continue, non-0 to stop. Returns 0, the stop code, or -1. */
typedef int (*ais_index_cb)(const char *name, const char *path, void *ctx);
int ais_index_list(ais_index_cb cb, void *ctx);

/* DEPRECATED (back-compat): the legacy single saved default, the `index = PATH`
 * line. Superseded by the named registry + `current`. get: 1/0/-1; set: 0/-1.
 * Kept so `ais --default` and pre-registry configs keep working for one release. */
int ais_default_get(char *out, size_t outsz);
int ais_default_set(const char *path);

#endif /* AIS_LOCATE_H */
