/* key.h -- key encoding and the navigable shard prefix.
 *
 * Encoding lowercases the key and maps every unsafe character to '_' -- space,
 * control chars and 0x7F, '|' (the store field delimiter), '/' and '\' (path
 * separators), and a leading '.' -- so a key is one store field and one safe path
 * component. The shard prefix is the first two ASCII characters or the whole
 * first non-ASCII character (key_prefix), giving the
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

/* Encode KEY into OUT (size OUTSZ): lowercase ASCII; space, control, 0x7F, '|',
 * '/', '\' and a leading '.' all map to '_'. Truncates to fit OUT (always
 * NUL-terminated). Returns 0 on success, -1 if KEY encodes empty (nothing to file under). */
int key_encode(const char *key, char *out, size_t outsz);

/* The shard directory name for an encoded key: two characters for ASCII, and
 * for anything else the whole FIRST character, 2 to 4 bytes. Never a partial
 * UTF-8 sequence -- APFS refuses a filename that is not valid UTF-8, so a
 * truncated prefix meant the posting could not be written at all on macOS and
 * iOS. OUT needs AIS_PREFIX_MAX bytes. */
int key_prefix(const char *enc, char *out, size_t outsz);

/* The file name a posting takes on THIS platform for an encoded key, and the
 * encoded key back from a file name read out of a shard. POSIX: the identity.
 * Windows: NTFS refuses the six bytes : < > " ? * in a name, and a colon is
 * worse than refused, "due:2020" opens a stream called 2020 of a file called
 * due, which no directory listing shows. Each of the six becomes the
 * private-use character U+F000 + byte (three bytes, EF 80 80+byte), the
 * mapping Cygwin and Samba use, and comes back on read. OUT needs up to three
 * bytes per input byte; a name that would not fit returns -1. The shard
 * prefix is taken from the file name, so it is a legal directory name too. */
int key_fsname(const char *enc, char *out, size_t outsz);
int key_fsdecode(const char *name, char *out, size_t outsz);
/* The Windows mapping itself, on every platform, so the tests can see it. */
int key_fsname_win(const char *enc, char *out, size_t outsz);
int key_fsdecode_win(const char *name, char *out, size_t outsz);

#endif /* AIS_KEY_H */
