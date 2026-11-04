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

/* NTFS's reserved bytes, all below 0x40, so U+F000 + c is EF 80 (0x80 | c). */
static int key_fs_reserved(unsigned char c)
{
    return c == ':' || c == '<' || c == '>' || c == '"' || c == '?' || c == '*';
}

int key_fsname_win(const char *enc, char *out, size_t outsz)
{
    const unsigned char *p = (const unsigned char *)enc;
    size_t i = 0;

    if (enc == NULL || outsz == 0)
        return -1;
    for (; *p != '\0'; p++) {
        if (key_fs_reserved(*p)) {
            if (i + 4 > outsz)
                return -1;
            out[i++] = (char)0xEF;
            out[i++] = (char)0x80;
            out[i++] = (char)(0x80 | *p);
        } else {
            if (i + 2 > outsz)
                return -1;
            out[i++] = (char)*p;
        }
    }
    out[i] = '\0';
    return 0;
}

int key_fsdecode_win(const char *name, char *out, size_t outsz)
{
    const unsigned char *p = (const unsigned char *)name;
    size_t i = 0;

    if (name == NULL || outsz == 0)
        return -1;
    for (; *p != '\0'; p++) {
        if (i + 2 > outsz)
            return -1;
        if (p[0] == 0xEF && p[1] == 0x80 && key_fs_reserved((unsigned char)(p[2] & 0x3F))
            && (p[2] & 0xC0) == 0x80) {
            out[i++] = (char)(p[2] & 0x3F);
            p += 2;
        } else {
            out[i++] = (char)*p;
        }
    }
    out[i] = '\0';
    return 0;
}

#ifdef _WIN32
int key_fsname(const char *enc, char *out, size_t outsz)
{
    return key_fsname_win(enc, out, outsz);
}

int key_fsdecode(const char *name, char *out, size_t outsz)
{
    return key_fsdecode_win(name, out, outsz);
}
#else
int key_fsname(const char *enc, char *out, size_t outsz)
{
    if (enc == NULL || outsz == 0 || strlen(enc) + 1 > outsz)
        return -1;
    memcpy(out, enc, strlen(enc) + 1);
    return 0;
}

int key_fsdecode(const char *name, char *out, size_t outsz)
{
    return key_fsname(name, out, outsz);
}
#endif

/* ---- case folding for matching -------------------------------------------- */

unsigned long key_utf8_cp(const char *s, int *len)
{
    const unsigned char *u = (const unsigned char *)s;
    unsigned long c;
    int n, i;

    if (u[0] < 0x80)       { *len = 1; return u[0]; }
    else if ((u[0] & 0xE0) == 0xC0) { n = 2; c = u[0] & 0x1F; }
    else if ((u[0] & 0xF0) == 0xE0) { n = 3; c = u[0] & 0x0F; }
    else if ((u[0] & 0xF8) == 0xF0) { n = 4; c = u[0] & 0x07; }
    else                   { *len = 1; return u[0]; }   /* a stray byte: itself */
    for (i = 1; i < n; i++) {
        if ((u[i] & 0xC0) != 0x80) { *len = 1; return u[0]; }  /* truncated */
        c = (c << 6) | (u[i] & 0x3F);
    }
    *len = n;
    return c;
}

int key_utf8_put(unsigned long c, char *out)
{
    if (c < 0x80)      { out[0] = (char)c; return 1; }
    if (c < 0x800)     { out[0] = (char)(0xC0 | (c >> 6));
                         out[1] = (char)(0x80 | (c & 0x3F)); return 2; }
    if (c < 0x10000)   { out[0] = (char)(0xE0 | (c >> 12));
                         out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
                         out[2] = (char)(0x80 | (c & 0x3F)); return 3; }
    out[0] = (char)(0xF0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
    out[3] = (char)(0x80 | (c & 0x3F));
    return 4;
}

/* Latin Extended-A pairs upper/lower by adjacent code points, even/odd in
 * U+0100-0137 and U+014A-0177, odd/even in U+0139-0148 and U+0179-017E. The
 * singletons (U+0130 I-dot, U+0131, U+0138, U+0149, U+0178, U+017F) fold to
 * themselves. */
unsigned long key_fold_cp(unsigned long c)
{
    if (c < 0x80)                       return (c >= 'A' && c <= 'Z') ? c + 32 : c;
    if (c >= 0xC0 && c <= 0xDE)         return (c == 0xD7) ? c : c + 32;   /* Latin-1 */
    if (c >= 0x100 && c <= 0x137)       return (c % 2 == 0) ? c + 1 : c;   /* Latin Ext-A */
    if (c >= 0x139 && c <= 0x148)       return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x14A && c <= 0x177)       return (c % 2 == 0) ? c + 1 : c;
    if (c >= 0x179 && c <= 0x17E)       return (c % 2 == 1) ? c + 1 : c;
    if (c >= 0x391 && c <= 0x3A9)       return (c == 0x3A2) ? c : c + 32;  /* Greek */
    if (c >= 0x400 && c <= 0x40F)       return c + 80;                     /* Cyrillic Ѐ-Џ */
    if (c >= 0x410 && c <= 0x42F)       return c + 32;                     /* Cyrillic А-Я */
    return c;
}

unsigned long key_upper_cp(unsigned long c)
{
    if (c < 0x80)                       return (c >= 'a' && c <= 'z') ? c - 32 : c;
    if (c >= 0xE0 && c <= 0xFE)         return (c == 0xF7) ? c : c - 32;
    if (c >= 0x101 && c <= 0x138)       return (c % 2 == 1) ? c - 1 : c;
    if (c >= 0x13A && c <= 0x149)       return (c % 2 == 0) ? c - 1 : c;
    if (c >= 0x14B && c <= 0x178)       return (c % 2 == 1) ? c - 1 : c;
    if (c >= 0x17A && c <= 0x17F)       return (c % 2 == 0) ? c - 1 : c;
    if (c >= 0x3B1 && c <= 0x3C9)       return (c == 0x3C2) ? c : c - 32;
    if (c >= 0x450 && c <= 0x45F)       return c - 80;
    if (c >= 0x430 && c <= 0x44F)       return c - 32;
    return c;
}

int key_fold_prefix(const char *key, const char *prefix)
{
    while (*prefix != '\0') {
        int lk, lp;
        unsigned long ck, cp;
        if (*key == '\0')
            return 0;
        ck = key_utf8_cp(key, &lk);
        cp = key_utf8_cp(prefix, &lp);
        if (key_fold_cp(ck) != key_fold_cp(cp))
            return 0;
        key += lk;
        prefix += lp;
    }
    return 1;
}
