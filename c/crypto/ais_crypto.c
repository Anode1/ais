/* SPDX-License-Identifier: GPL-2.0-only
 *
 * ais_crypto - authenticated file encryption for the ais secret store.
 * Part of ais (GPLv2). Crypto primitives from vendored Monocypher (CC0/BSD).
 *
 * One auditable translation unit around two vetted Monocypher calls:
 *   crypto_argon2        -> Argon2id key derivation (the brute-force barrier)
 *   crypto_aead_lock/unlock -> XChaCha20-Poly1305 (confidentiality + integrity)
 *
 * File format (all integers little-endian):
 *   off  size  field
 *   0     8    magic "AIS-CR1\0"
 *   8     1    kdf id (0x02 = Argon2id)
 *   9     4    Argon2 memory, in 1 KiB blocks
 *   13    4    Argon2 passes
 *   17   16    salt        (random, not secret)
 *   33   24    nonce       (random, XChaCha20 tolerates random nonces)
 *   57    N    ciphertext  (same length as plaintext)
 *   57+N 16    Poly1305 tag
 * The 57-byte header is authenticated as associated data, so the KDF params,
 * salt and nonce cannot be altered without failing decryption.
 */
#include "ais_crypto.h"

/* Self-guard: compile to an inert object until Monocypher is vendored (run
 * crypto/vendor-monocypher.sh). This lets the generic engine Makefile pick up
 * this file with no special-casing -- present-but-inert before vendoring,
 * active after. GCC/Clang have supported __has_include for years. */
#if defined(__has_include)
#  if __has_include("monocypher.h")
#    define AISC_HAVE_MONOCYPHER 1
#  endif
#endif

#ifdef AISC_HAVE_MONOCYPHER

#include "monocypher.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/mman.h>

#define AISC_MAGIC      "AIS-CR1\0"
#define AISC_MAGIC_LEN  8
#define AISC_HDR_LEN    57          /* 8 + 1 + 4 + 4 + 16 + 24 */
#define AISC_SALT_LEN   16
#define AISC_NONCE_LEN  24
#define AISC_TAG_LEN    16
#define AISC_KDF_ARGON2ID 0x02

/* Refuse implausible KDF memory from an untrusted header (DoS guard): 2 GiB. */
#define AISC_MAX_BLOCKS (2u * 1024u * 1024u)

void aisc_wipe(void *p, size_t n) { crypto_wipe(p, n); }

aisc_kdf aisc_default_kdf(void) {
    aisc_kdf k;
    k.mem_blocks = 262144u;  /* 256 MiB */
    k.passes     = 3u;
    return k;
}

/* ----- little-endian helpers ------------------------------------------- */
static void put_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static uint32_t get_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ----- OS randomness (portable: /dev/urandom on Linux/BSD/macOS) -------- */
static int rand_bytes(uint8_t *p, size_t n) {
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return AISC_E_RANDOM;
    while (n) {
        ssize_t r = read(fd, p, n);
        if (r <= 0) { close(fd); return AISC_E_RANDOM; }
        p += r; n -= (size_t)r;
    }
    close(fd);
    return AISC_OK;
}

/* ----- key derivation -------------------------------------------------- */
/* Derives a 256-bit key from the password and (optional) keyfile. The keyfile
 * rides in Argon2's secret-key slot, so it acts as a true second factor that is
 * never stored in the file. work_area is large (mem_blocks KiB); it is wiped. */
static int derive_key(uint8_t key[32],
                      const uint8_t *pw, size_t pw_len,
                      const uint8_t *kf, size_t kf_len,
                      const uint8_t salt[AISC_SALT_LEN], aisc_kdf k) {
    if (k.mem_blocks < 8u || k.mem_blocks > AISC_MAX_BLOCKS || k.passes < 1u)
        return AISC_E_FORMAT;

    size_t work_len = (size_t)k.mem_blocks * 1024u;
    void *work = malloc(work_len);
    if (!work) return AISC_E_MEM;

    crypto_argon2_config cfg = {
        .algorithm = CRYPTO_ARGON2_ID,
        .nb_blocks = k.mem_blocks,
        .nb_passes = k.passes,
        .nb_lanes  = 1u
    };
    crypto_argon2_inputs in = {
        .pass = pw, .pass_size = (uint32_t)pw_len,
        .salt = salt, .salt_size = AISC_SALT_LEN
    };
    crypto_argon2_extras ex = {
        .key = kf, .key_size = (uint32_t)kf_len,   /* keyfile = second factor */
        .ad  = NULL, .ad_size = 0
    };

    crypto_argon2(key, 32, work, cfg, in, ex);
    crypto_wipe(work, work_len);
    free(work);
    return AISC_OK;
}

/* ----- encrypt --------------------------------------------------------- */
int aisc_encrypt(const uint8_t *plain, size_t plain_len,
                 const uint8_t *password, size_t pw_len,
                 const uint8_t *keyfile, size_t kf_len,
                 aisc_kdf kdf,
                 uint8_t **out, size_t *out_len) {
    if (!plain || !password || !out || !out_len) return AISC_E_ARG;

    uint8_t key[32];
    size_t file_len = AISC_HDR_LEN + plain_len + AISC_TAG_LEN;
    uint8_t *buf = malloc(file_len ? file_len : 1);
    if (!buf) return AISC_E_MEM;

    /* header */
    memcpy(buf, AISC_MAGIC, AISC_MAGIC_LEN);
    buf[8] = AISC_KDF_ARGON2ID;
    put_le32(buf + 9,  kdf.mem_blocks);
    put_le32(buf + 13, kdf.passes);
    if (rand_bytes(buf + 17, AISC_SALT_LEN) ||
        rand_bytes(buf + 33, AISC_NONCE_LEN)) { free(buf); return AISC_E_RANDOM; }

    int rc = derive_key(key, password, pw_len, keyfile, kf_len, buf + 17, kdf);
    if (rc != AISC_OK) { aisc_wipe(buf, file_len); free(buf); return rc; }

    uint8_t *ct  = buf + AISC_HDR_LEN;
    uint8_t *tag = ct + plain_len;
    crypto_aead_lock(ct, tag, key, buf + 33 /*nonce*/,
                     buf, AISC_HDR_LEN /*ad = header*/, plain, plain_len);

    aisc_wipe(key, sizeof key);
    *out = buf;
    *out_len = file_len;
    return AISC_OK;
}

/* ----- decrypt --------------------------------------------------------- */
int aisc_decrypt(const uint8_t *file, size_t file_len,
                 const uint8_t *password, size_t pw_len,
                 const uint8_t *keyfile, size_t kf_len,
                 uint8_t **out, size_t *out_len) {
    if (!file || !password || !out || !out_len) return AISC_E_ARG;
    if (file_len < AISC_HDR_LEN + AISC_TAG_LEN)  return AISC_E_FORMAT;
    if (memcmp(file, AISC_MAGIC, AISC_MAGIC_LEN) != 0) return AISC_E_FORMAT;
    if (file[8] != AISC_KDF_ARGON2ID)            return AISC_E_FORMAT;

    aisc_kdf kdf;
    kdf.mem_blocks = get_le32(file + 9);
    kdf.passes     = get_le32(file + 13);

    size_t ct_len = file_len - AISC_HDR_LEN - AISC_TAG_LEN;
    const uint8_t *ct  = file + AISC_HDR_LEN;
    const uint8_t *tag = ct + ct_len;

    uint8_t key[32];
    int rc = derive_key(key, password, pw_len, keyfile, kf_len, file + 17, kdf);
    if (rc != AISC_OK) return rc;

    uint8_t *pt = malloc(ct_len ? ct_len : 1);
    if (!pt) { aisc_wipe(key, sizeof key); return AISC_E_MEM; }

    int bad = crypto_aead_unlock(pt, tag, key, file + 33 /*nonce*/,
                                 file, AISC_HDR_LEN, ct, ct_len);
    aisc_wipe(key, sizeof key);
    if (bad) { aisc_wipe(pt, ct_len); free(pt); return AISC_E_AUTH; }

    *out = pt;
    *out_len = ct_len;
    return AISC_OK;
}

/* ----- passphrase prompt (echo off, /dev/tty only) --------------------- */
static int read_line_tty(int fd, char *buf, size_t buf_sz) {
    size_t n = 0;
    for (;;) {
        char c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0) return -1;
        if (c == '\n' || c == '\r') break;
        if (n + 1 < buf_sz) buf[n++] = c;   /* silently truncate at buf_sz-1 */
    }
    buf[n] = '\0';
    return (int)n;
}

/* best-effort write to the terminal; the count is intentionally ignored (these
 * are prompts, not data). Silences write()'s warn_unused_result under fortify. */
static void tty_write(int fd, const void *p, size_t n) {
    ssize_t r = write(fd, p, n);
    (void)r;
}

int aisc_prompt_passphrase(const char *prompt, int confirm,
                           char *buf, size_t buf_sz) {
    if (!buf || buf_sz < 2) return -1;
    int fd = open("/dev/tty", O_RDWR | O_NOCTTY);
    if (fd < 0) return -1;

    struct termios old, raw;
    if (tcgetattr(fd, &old) != 0) { close(fd); return -1; }
    raw = old;
    raw.c_lflag &= ~(tcflag_t)ECHO;
    tcsetattr(fd, TCSAFLUSH, &raw);

    int len = -1;
    if (prompt) tty_write(fd, prompt, strlen(prompt));
    len = read_line_tty(fd, buf, buf_sz);
    tty_write(fd, "\n", 1);              /* echo was off, so emit the newline */

    if (len >= 0 && confirm) {
        char again[1024];
        tty_write(fd, "Confirm: ", 9);
        int len2 = read_line_tty(fd, again, sizeof again);
        tty_write(fd, "\n", 1);
        if (len2 != len || memcmp(buf, again, (size_t)len) != 0) {
            tty_write(fd, "passphrases did not match\n", 26);
            len = -1;
        }
        aisc_wipe(again, sizeof again);
    }

    tcsetattr(fd, TCSAFLUSH, &old);
    close(fd);
    return len;
}

/* ----- optional standalone CLI ----------------------------------------- *
 * Build a self-test tool without touching the engine:
 *   cc -DAIS_CRYPTO_CLI -I. ais_crypto.c monocypher.c -o aiscrypt
 *   AIS_KEYFILE=~/key  ./aiscrypt enc secrets.txt secrets.aisc
 *   AIS_KEYFILE=~/key  ./aiscrypt dec secrets.aisc secrets.out
 */
#ifdef AIS_CRYPTO_CLI

static int read_file(const char *path, uint8_t **buf, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return AISC_E_IO;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return AISC_E_IO; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return AISC_E_IO; }
    rewind(f);
    uint8_t *b = malloc((size_t)sz ? (size_t)sz : 1);
    if (!b) { fclose(f); return AISC_E_MEM; }
    if (sz && fread(b, 1, (size_t)sz, f) != (size_t)sz) { free(b); fclose(f); return AISC_E_IO; }
    fclose(f);
    *buf = b; *len = (size_t)sz;
    return AISC_OK;
}

static int write_file(const char *path, const uint8_t *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return AISC_E_IO;
    int ok = (len == 0) || (fwrite(buf, 1, len, f) == len);
    if (fclose(f) != 0) ok = 0;
    return ok ? AISC_OK : AISC_E_IO;
}

int main(int argc, char **argv) {
    if (argc != 4 || (strcmp(argv[1], "enc") && strcmp(argv[1], "dec"))) {
        fprintf(stderr, "usage: %s enc|dec <in> <out>\n"
                        "       second factor (optional): $AIS_KEYFILE\n", argv[0]);
        return 2;
    }
    int enc = (strcmp(argv[1], "enc") == 0);

    /* optional keyfile = something you have */
    uint8_t *kf = NULL; size_t kf_len = 0;
    const char *kf_path = getenv("AIS_KEYFILE");
    if (kf_path && *kf_path && read_file(kf_path, &kf, &kf_len) != AISC_OK) {
        fprintf(stderr, "cannot read AIS_KEYFILE\n"); return 1;
    }

    char pw[1024];
    mlock(pw, sizeof pw);
    if (aisc_prompt_passphrase("Passphrase: ", enc /*confirm on encrypt*/, pw, sizeof pw) < 0) {
        aisc_wipe(pw, sizeof pw); munlock(pw, sizeof pw);
        if (kf) { aisc_wipe(kf, kf_len); free(kf); }
        return 1;
    }
    size_t pw_len = strlen(pw);

    uint8_t *in = NULL, *out = NULL; size_t in_len = 0, out_len = 0;
    int rc = read_file(argv[2], &in, &in_len);
    if (rc == AISC_OK) {
        rc = enc
           ? aisc_encrypt(in, in_len, (uint8_t *)pw, pw_len, kf, kf_len, aisc_default_kdf(), &out, &out_len)
           : aisc_decrypt(in, in_len, (uint8_t *)pw, pw_len, kf, kf_len, &out, &out_len);
    }

    aisc_wipe(pw, sizeof pw); munlock(pw, sizeof pw);
    if (kf) { aisc_wipe(kf, kf_len); free(kf); }
    if (in) { if (!enc) aisc_wipe(in, in_len); free(in); }

    if (rc != AISC_OK) {
        const char *m = rc == AISC_E_AUTH ? "wrong passphrase/keyfile or tampered file"
                      : rc == AISC_E_FORMAT ? "not an AIS-CR1 file"
                      : "I/O or memory error";
        fprintf(stderr, "%s: %s\n", argv[0], m);
        if (out) { aisc_wipe(out, out_len); free(out); }
        return 1;
    }

    rc = write_file(argv[3], out, out_len);
    if (out) { aisc_wipe(out, out_len); free(out); }
    if (rc != AISC_OK) { fprintf(stderr, "%s: cannot write %s\n", argv[0], argv[3]); return 1; }
    return 0;
}

#endif /* AIS_CRYPTO_CLI */

#else  /* !AISC_HAVE_MONOCYPHER */
/* Monocypher not vendored yet. ais_crypto.h still declares the API, so this
 * translation unit is not empty; the definitions appear once you run
 * crypto/vendor-monocypher.sh. */
#endif /* AISC_HAVE_MONOCYPHER */
