/* key.h -- key encoding and the navigable shard prefix.
 *
 * A key is a human word. Encoding lowercases it and turns whitespace into '_'
 * so it is a safe single path component. The shard prefix is the first one or
 * two encoded characters, giving the navigable layout idx/<p>/<key>:
 * `ls idx/a/` shows the keys beginning with 'a'. Nothing is hashed.
 *
 * Pure functions: no allocation, bounded buffers supplied by the caller.
 */
#ifndef AIS_KEY_H
#define AIS_KEY_H

#include <stddef.h>

/* Encode KEY into OUT (size OUTSZ): lowercase ASCII, whitespace -> '_'.
 * Truncates to fit OUT (always NUL-terminated). Returns 0 on success,
 * -1 if KEY encodes empty (nothing to file under). */
int key_encode(const char *key, char *out, size_t outsz);

/* Write the shard prefix of the already-encoded key ENC into OUT (size OUTSZ):
 * its first one or two characters. Returns 0, or -1 if ENC is empty. */
int key_prefix(const char *enc, char *out, size_t outsz);

#endif /* AIS_KEY_H */
