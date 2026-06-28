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

#ifndef O_NOCTTY             /* Windows/MinGW has no controlling-terminal flag */
#define O_NOCTTY 0
#endif

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

const char *secret_blob_relpath(const char *value)
{
    size_t pfx = sizeof AIS_SECRET_PREFIX - 1;

    if (!secret_is_marked(value) || value[pfx] != '@')
        return NULL;                             /* inline value, or not a secret */
    return value + pfx + 1;                       /* the relpath after "aisc:@" */
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

int secret_encrypt_to_file(const unsigned char *plain, size_t plain_len,
                           const unsigned char *password, size_t pw_len,
                           const char *path)
{
#ifdef SECRET_HAVE_CRYPTO
    uint8_t *file = NULL;
    size_t flen = 0;
    FILE *f;
    int ok;

    if (aisc_encrypt(plain, plain_len, password, pw_len, NULL, 0,
                     aisc_default_kdf(), &file, &flen) != AISC_OK)
        return -1;
    f = fopen(path, "wb");                        /* the raw AIS-CR1 image, no base64 */
    if (f == NULL) {
        aisc_wipe(file, flen);
        free(file);
        return -1;
    }
    ok = (fwrite(file, 1, flen, f) == flen);
    aisc_wipe(file, flen);
    free(file);
    if (fclose(f) != 0)
        ok = 0;
    return ok ? 0 : -1;
#else
    (void)plain; (void)plain_len; (void)password; (void)pw_len; (void)path;
    return -1;
#endif
}

void secret_shred_blob(const char *index_dir, const char *value)
{
    const char *rel = secret_blob_relpath(value);
    char path[AIS_PATH_MAX];
    FILE *f;

    if (rel == NULL || index_dir == NULL)
        return;                                  /* not an encrypted blob -> nothing to shred */
    if (snprintf(path, sizeof path, "%s/%s", index_dir, rel) >= (int)sizeof path)
        return;

    /* Best-effort overwrite in place, then unlink. The bytes are ciphertext, so
     * the encryption -- not this overwrite -- is the real protection; on flash /
     * CoW / backed-up storage the overwrite may not reach the physical cells. */
    f = fopen(path, "r+b");
    if (f != NULL) {
        if (fseek(f, 0, SEEK_END) == 0) {
            long n = ftell(f);
            if (n > 0 && fseek(f, 0, SEEK_SET) == 0) {
                static const unsigned char zeros[4096] = { 0 };
                long left = n;
                while (left > 0) {
                    size_t chunk = (left > (long)sizeof zeros) ? sizeof zeros : (size_t)left;
                    if (fwrite(zeros, 1, chunk, f) != chunk)
                        break;
                    left -= (long)chunk;
                }
                fflush(f);
            }
        }
        fclose(f);
    }
    remove(path);
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

#ifdef SECRET_HAVE_CRYPTO
/* Read PATH fully into a fresh malloc'd buffer (*len = size); NULL on any error.
 * The caller wipes and frees. Used to load a blob's AIS-CR1 image for reveal. */
static unsigned char *read_whole_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    long n;

    if (f == NULL)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

/* Write "<id> = <plaintext>\n" -- or an error line when N < 0 -- to the tty fd. */
static void reveal_write(int fd, long id, const unsigned char *pt, long n)
{
    ssize_t w;

    if (n < 0) {
        const char *m = "ais: cannot decrypt (wrong passphrase or tampered)\n";
        w = write(fd, m, strlen(m)); (void)w;
        return;
    }
    {
        char hdr[48];
        int hn = snprintf(hdr, sizeof hdr, "%ld = ", id);
        if (hn > 0) { w = write(fd, hdr, (size_t)hn); (void)w; }
    }
    w = write(fd, pt, (size_t)n); (void)w;
    w = write(fd, "\n", 1);       (void)w;
}
#endif

int secret_reveal(long id, const char *marked_value, const char *index_dir)
{
#ifdef SECRET_HAVE_CRYPTO
    char pw[1024];
    const char *rel = secret_blob_relpath(marked_value);
    int fd, rc = 0;

    if (secret_prompt("passphrase: ", 0, pw, sizeof pw) < 0) {
        secret_wipe(pw, sizeof pw);
        return -1;
    }
    fd = open("/dev/tty", O_WRONLY | O_NOCTTY);   /* reveal to the terminal, never stdout */
    if (fd < 0) {
        secret_wipe(pw, sizeof pw);
        return -1;
    }

    if (rel == NULL) {                            /* INLINE: bounded by the store line */
        static unsigned char pt[AIS_LINE_MAX];
        long n = secret_decrypt(marked_value, (const unsigned char *)pw, strlen(pw),
                                pt, sizeof pt);
        reveal_write(fd, id, pt, n);
        if (n > 0) secret_wipe(pt, (size_t)n);
        rc = (n < 0) ? -1 : 0;
    } else {                                      /* BLOB: image in <index_dir>/<rel> */
        char path[AIS_PATH_MAX];
        unsigned char *image, *pt = NULL;
        size_t ilen = 0, ptlen = 0;

        snprintf(path, sizeof path, "%s/%s", index_dir != NULL ? index_dir : ".", rel);
        image = read_whole_file(path, &ilen);
        if (image == NULL) {
            const char *m = "ais: encrypted blob is missing or unreadable\n";
            ssize_t w = write(fd, m, strlen(m)); (void)w;
            rc = -1;
        } else {
            if (aisc_decrypt(image, ilen, (const unsigned char *)pw, strlen(pw),
                             NULL, 0, &pt, &ptlen) == AISC_OK) {
                reveal_write(fd, id, pt, (long)ptlen);
                aisc_wipe(pt, ptlen);
                free(pt);
            } else {
                reveal_write(fd, id, NULL, -1);
                rc = -1;
            }
            aisc_wipe(image, ilen);
            free(image);
        }
    }
    secret_wipe(pw, sizeof pw);
    close(fd);
    return rc;
#else
    char msg[176];
    int fd, n;
    (void)marked_value; (void)index_dir;
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
