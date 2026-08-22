/* key.c -- key encoding and shard prefix. See key.h. Pure, no allocation.
 *
 * Deliberately locale-INDEPENDENT. ctype.h answers about bytes >= 0x80 according
 * to the current locale, and the two libcs disagree: glibc in the C locale calls
 * none of them control characters, while BSD (macOS) in a UTF-8 locale calls
 * 0x80-0x9F control, which is where UTF-8 continuation bytes live. So "ключ"
 * encoded to one name on Linux and another on macOS, the same key was filed
 * under two names across a mesh, and recall on macOS returned nothing. The rule
 * this file implements is ASCII, and now it says so in ASCII. */
#include <string.h>

#include "key.h"

int key_encode(const char *key, char *out, size_t outsz)
{
    size_t i = 0;
    const unsigned char *p = (const unsigned char *)key;

    if (key == NULL || outsz == 0)
        return -1;
    for (; *p != '\0' && i + 1 < outsz; p++) {
        unsigned char c = *p;
        /* Map to '_' anything unsafe: a key must be one store line-field (no
         * '|') AND one path component idx/<p>/<key> (no '/', '\\', space, ctrl). */
        if (c == ' ' || c == '|' || c == '/' || c == '\\' || c < 0x20 || c == 0x7F)
            out[i++] = '_';
        /* A leading dot only: the prefix is the first two encoded chars, so
         * "../../x" would encode to "..___x", take the prefix "..", and file
         * the posting one directory above idx/. Interior dots are common and
         * safe ("result.length"). */
        else if (c == '.' && i == 0)
            out[i++] = '_';
        else                          /* ASCII case-folding only: a non-ASCII key
                                       * must already be lowercase (LAYOUT.md) */
            out[i++] = (char)((c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c);
    }
    out[i] = '\0';
    return (i == 0) ? -1 : 0;
}

int key_prefix(const char *enc, char *out, size_t outsz)
{
    size_t n;

    if (enc == NULL || enc[0] == '\0' || outsz < 2)
        return -1;
    /* one char if the key is a single char, else the first two */
    n = (enc[1] == '\0' || outsz < 3) ? 1 : 2;
    memcpy(out, enc, n);
    out[n] = '\0';
    return 0;
}
