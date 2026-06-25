/* b64.c -- base64 (RFC 4648 standard alphabet, padded). See b64.h.
 * Bounded buffers, no heap; the caller sizes DST via AIS_B64_ENCLEN. */
#include "b64.h"

static const char ENC[64] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

long b64_encode(const unsigned char *src, size_t n, char *dst, size_t dstsz)
{
    size_t i, o = 0;

    if (src == NULL || dst == NULL || dstsz < AIS_B64_ENCLEN(n))
        return -1;
    for (i = 0; i + 3 <= n; i += 3) {
        unsigned v = ((unsigned)src[i] << 16)
                   | ((unsigned)src[i + 1] << 8) | src[i + 2];
        dst[o++] = ENC[(v >> 18) & 63];
        dst[o++] = ENC[(v >> 12) & 63];
        dst[o++] = ENC[(v >> 6) & 63];
        dst[o++] = ENC[v & 63];
    }
    if (i < n) {                              /* 1 or 2 trailing bytes */
        int two = (i + 1 < n);
        unsigned v = (unsigned)src[i] << 16;
        if (two) v |= (unsigned)src[i + 1] << 8;
        dst[o++] = ENC[(v >> 18) & 63];
        dst[o++] = ENC[(v >> 12) & 63];
        dst[o++] = two ? ENC[(v >> 6) & 63] : '=';
        dst[o++] = '=';
    }
    dst[o] = '\0';
    return (long)o;
}

/* One base64 char -> 0..63, -2 for '=', -1 for invalid. */
static int dec1(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    return -1;
}

long b64_decode(const char *src, size_t len, unsigned char *dst, size_t dstsz)
{
    size_t i = 0, o = 0;
    int quad[4], qn = 0;

    if (src == NULL || dst == NULL)
        return -1;
    while (i < len && src[i] != '\0') {
        char c = src[i++];
        int d;
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t')
            continue;                         /* tolerate whitespace */
        d = dec1(c);
        if (d == -1)
            return -1;                        /* invalid character */
        quad[qn++] = d;
        if (qn == 4) {
            int pad = (quad[2] == -2) + (quad[3] == -2);
            unsigned v;
            if (quad[0] == -2 || quad[1] == -2) return -1;   /* '=' too early */
            if (quad[2] == -2 && quad[3] != -2) return -1;   /* "x=y" illegal  */
            v = ((unsigned)quad[0] << 18) | ((unsigned)quad[1] << 12)
              | ((unsigned)(quad[2] == -2 ? 0 : quad[2]) << 6)
              | (unsigned)(quad[3] == -2 ? 0 : quad[3]);
            if (o + (size_t)(3 - pad) > dstsz) return -1;
            dst[o++] = (unsigned char)(v >> 16);
            if (pad < 2) dst[o++] = (unsigned char)(v >> 8);
            if (pad < 1) dst[o++] = (unsigned char)v;
            qn = 0;
            if (pad) break;                   /* padding terminates the stream */
        }
    }
    if (qn != 0)
        return -1;                            /* truncated (not a multiple of 4) */
    return (long)o;
}
