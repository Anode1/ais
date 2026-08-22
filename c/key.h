/* key.h -- key encoding and the navigable shard prefix.
 *
 * Encoding lowercases the key and maps every unsafe character to '_' -- space,
 * control chars, '|' (the store field delimiter), '/' and '\' (path
 * separators) -- so a key is one store field and one safe path component.
 * The shard prefix is the first one or two encoded characters, giving the
 * layout idx/<p>/<key>: `ls idx/a/` shows the keys beginning with 'a'. Nothing
 * is hashed.
 *
 * Pure functions: no allocation, bounded buffers supplied by the caller.
 */
#ifndef AIS_KEY_H
#define AIS_KEY_H

#include <stddef.h>

/* the longest shard name (4 bytes of one character) plus its NUL */
#define AIS_PREFIX_MAX 5

/* Encode KEY into OUT (size OUTSZ): lowercase ASCII; space, control, '|', '/'
 * and '\' all map to '_'. Truncates to fit OUT (always NUL-terminated).
 * Returns 0 on success, -1 if KEY encodes empty (nothing to file under). */
int key_encode(const char *key, char *out, size_t outsz);

/* Write the shard prefix of the already-encoded key ENC into OUT (size OUTSZ):
 * its first one or two characters. Returns 0, or -1 if ENC is empty. */
/* The shard directory name for an encoded key: two characters for ASCII, and
 * for anything else the whole FIRST character, 2 to 4 bytes. Never a partial
 * UTF-8 sequence -- APFS refuses a filename that is not valid UTF-8, so a
 * truncated prefix meant the posting could not be written at all on macOS and
 * iOS. OUT needs AIS_PREFIX_MAX bytes. */
int key_prefix(const char *enc, char *out, size_t outsz);

#endif /* AIS_KEY_H */
