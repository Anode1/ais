/* secret.c -- see secret.h. Detection, reveal policy, and value encrypt/decrypt
 * around the "aisc:" marker. The crypto-dependent parts compile inert (return
 * -1) until Monocypher is vendored; everything else builds and tests regardless. */
#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"          /* AIS_LINE_MAX */
#include "secret.h"
#include "b64.h"

#if defined(__has_include) && __has_include("crypto/monocypher.h")
#  define SECRET_HAVE_CRYPTO 1
#  include "crypto/ais_crypto.h"
#endif

void secret_wipe(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) *v++ = 0;
}

int secret_is_marked(const char *value)
{
    return value != NULL
        && strncmp(value, AIS_SECRET_PREFIX, sizeof AIS_SECRET_PREFIX - 1) == 0;
}

int secret_reveal_context(void)
{
    int fd;

    if (!isatty(STDOUT_FILENO))                  /* piped / redirected -> opaque */
        return 0;
    fd = open("/dev/tty", O_RDWR | O_NOCTTY);     /* must be able to prompt+reveal */
    if (fd < 0)
        return 0;
    close(fd);
    return 1;
}

long secret_encrypt(const unsigned char *plain, size_t plain_len,
                    const unsigned char *password, size_t pw_len,
                    char *out, size_t outsz)
{
#ifdef SECRET_HAVE_CRYPTO
    size_t pfx = sizeof AIS_SECRET_PREFIX - 1;
    uint8_t *file = NULL;
    size_t flen = 0;
    long n;

    if (out == NULL || outsz <= pfx)
        return -1;
    if (aisc_encrypt(plain, plain_len, password, pw_len, NULL, 0,
                     aisc_default_kdf(), &file, &flen) != AISC_OK)
        return -1;
    memcpy(out, AIS_SECRET_PREFIX, pfx);
    n = b64_encode(file, flen, out + pfx, outsz - pfx);   /* "aisc:" + base64(image) */
    aisc_wipe(file, flen);
    free(file);
    return (n < 0) ? -1 : (long)(pfx + (size_t)n);
#else
    (void)plain; (void)plain_len; (void)password; (void)pw_len; (void)out; (void)outsz;
    return -1;
#endif
}

long secret_decrypt(const char *value,
                    const unsigned char *password, size_t pw_len,
                    unsigned char *out, size_t outsz)
{
#ifdef SECRET_HAVE_CRYPTO
    size_t pfx = sizeof AIS_SECRET_PREFIX - 1;
    static unsigned char img[AIS_LINE_MAX];      /* base64-decoded file image (BSS) */
    long ilen;
    uint8_t *pt = NULL;
    size_t ptlen = 0;
    long ret = -1;

    if (!secret_is_marked(value))
        return -1;
    ilen = b64_decode(value + pfx, strlen(value + pfx), img, sizeof img);
    if (ilen < 0)
        return -1;
    if (aisc_decrypt(img, (size_t)ilen, password, pw_len, NULL, 0, &pt, &ptlen) == AISC_OK) {
        if (ptlen <= outsz) {
            memcpy(out, pt, ptlen);
            ret = (long)ptlen;
        }
        aisc_wipe(pt, ptlen);
        free(pt);
    }
    aisc_wipe(img, (size_t)ilen);
    return ret;
#else
    (void)value; (void)password; (void)pw_len; (void)out; (void)outsz;
    return -1;
#endif
}

int secret_prompt(const char *prompt, int confirm, char *buf, size_t buf_sz)
{
#ifdef SECRET_HAVE_CRYPTO
    return aisc_prompt_passphrase(prompt, confirm, buf, buf_sz);
#else
    (void)prompt; (void)confirm; (void)buf; (void)buf_sz;
    return -1;
#endif
}

int secret_reveal(long id, const char *marked_value)
{
#ifdef SECRET_HAVE_CRYPTO
    char pw[1024];
    static unsigned char pt[AIS_LINE_MAX];       /* decrypted secret (BSS) */
    long n;
    int fd;
    ssize_t w;

    if (secret_prompt("passphrase: ", 0, pw, sizeof pw) < 0) {
        secret_wipe(pw, sizeof pw);
        return -1;
    }
    n = secret_decrypt(marked_value, (const unsigned char *)pw, strlen(pw), pt, sizeof pt);
    secret_wipe(pw, sizeof pw);

    fd = open("/dev/tty", O_WRONLY | O_NOCTTY);   /* reveal to the terminal, never stdout */
    if (fd < 0) {
        if (n > 0) secret_wipe(pt, (size_t)n);
        return -1;
    }
    if (n < 0) {
        const char *m = "ais: cannot decrypt (wrong passphrase or tampered)\n";
        w = write(fd, m, strlen(m)); (void)w;
    } else {
        char hdr[48];
        int hn = snprintf(hdr, sizeof hdr, "%ld = ", id);
        if (hn > 0) { w = write(fd, hdr, (size_t)hn); (void)w; }
        w = write(fd, pt, (size_t)n); (void)w;
        w = write(fd, "\n", 1);      (void)w;
        secret_wipe(pt, (size_t)n);
    }
    close(fd);
    return 0;
#else
    char msg[176];
    int fd, n;
    (void)marked_value;
    fd = open("/dev/tty", O_WRONLY | O_NOCTTY);
    if (fd < 0)
        return -1;
    n = snprintf(msg, sizeof msg,
                 "ais: record %ld is an encrypted secret; reveal needs the crypto "
                 "module (run crypto/vendor-monocypher.sh, then make).\n", id);
    if (n > 0) {
        ssize_t w = write(fd, msg, (size_t)n);
        (void)w;
    }
    close(fd);
    return 0;
#endif
}
