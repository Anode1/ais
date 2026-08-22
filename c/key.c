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
    unsigned char c0;
    size_t n;

    if (enc == NULL || enc[0] == '\0' || outsz < 2)
        return -1;
    c0 = (unsigned char)enc[0];
    if (c0 < 0x80) {
        /* ASCII: one char if the key is a single char, else the first two. */
        n = (enc[1] == '\0' || outsz < 3) ? 1 : 2;
    } else {
        /* Non-ASCII: the whole FIRST CHARACTER, however many bytes it takes.
         *
         * Two bytes is the first character only up to U+07FF. For a three-byte
         * character (CJK, and most of what is not Latin or Cyrillic) two bytes
         * is a TRUNCATED sequence, so the shard directory's name was not valid
         * UTF-8 -- which Linux accepts and APFS refuses. On macOS and iOS the
         * mkdir failed, the posting was never written, and the key recalled
         * nothing: an index full of Japanese keys was readable on one of the
         * user's devices and silently empty on another.
         *
         * ASCII keeps its two-character prefix, and so does every two-byte
         * character, so no index that works today changes shape. An index with
         * three-byte keys made before this needs one --compact, which rebuilds
         * idx/ from the store. */
        n = (c0 >= 0xF0) ? 4 : (c0 >= 0xE0) ? 3 : 2;
        while (n > 1 && (size_t)n >= outsz)
            n--;                              /* never overrun the caller's buffer */
        {   /* a malformed sequence: keep only the bytes that are actually there */
            size_t k;
            for (k = 1; k < n; k++)
                if (((unsigned char)enc[k] & 0xC0) != 0x80) { n = k; break; }
        }
    }
    memcpy(out, enc, n);
    out[n] = '\0';
    return 0;
}
