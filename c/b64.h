/* b64.h -- base64 (RFC 4648 standard alphabet, padded). Bounded, no heap.
 * Used to carry an encrypted file image as one printable "aisc:" store line. */
#ifndef AIS_B64_H
#define AIS_B64_H

#include <stddef.h>

/* Encoded buffer size (including the NUL) needed for N input bytes. */
#define AIS_B64_ENCLEN(n) (((((size_t)(n)) + 2u) / 3u) * 4u + 1u)

/* Encode SRC[n] into DST (size DSTSZ, >= AIS_B64_ENCLEN(n)); NUL-terminates.
 * Returns the encoded length (excluding the NUL), or -1 if DST is too small. */
long b64_encode(const unsigned char *src, size_t n, char *dst, size_t dstsz);

/* Decode the base64 in SRC (up to LEN chars or the first NUL; whitespace is
 * tolerated) into DST (size DSTSZ). Returns the number of decoded bytes, or -1
 * on an invalid character, truncated input, or DST too small. */
long b64_decode(const char *src, size_t len, unsigned char *dst, size_t dstsz);

#endif /* AIS_B64_H */
