/* key.c -- key encoding and shard prefix. See key.h. Pure, no allocation. */
#include <ctype.h>
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
        if (c == ' ' || c == '|' || c == '/' || c == '\\' || iscntrl(c))
            out[i++] = '_';
        else
            out[i++] = (char)tolower(c);
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
