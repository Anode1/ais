/* SPDX-License-Identifier: GPL-2.0-only
 *
 * ais_crypto - authenticated file encryption for the ais secret store.
 *
 * Part of ais (GPLv2). Built on vendored Monocypher (CC0-1.0 / BSD-2-Clause,
 * permissive and GPL-compatible; see README.md and LICENSE.monocypher).
 *
 * Design (see README.md for the full rationale and threat model):
 *   key   = Argon2id(password [+ keyfile as secret], random salt)   // 256-bit
 *   file  = header || ciphertext || tag                             // AEAD
 *   AEAD  = XChaCha20-Poly1305 (the header is authenticated as associated data)
 *
 * No hardcoded salt: the salt is random per file and stored in the header (a
 * salt is not secret). The optional second factor is a keyfile (something you
 * have), fed to the KDF as a secret key. The password is read from /dev/tty
 * with echo off, never from argv/env/history.
 */
#ifndef AIS_CRYPTO_H
#define AIS_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

/* Return codes. */
enum {
    AISC_OK       =  0,
    AISC_E_IO     = -1,   /* open/read/write failure                          */
    AISC_E_FORMAT = -2,   /* bad magic, truncated, or implausible KDF params  */
    AISC_E_AUTH   = -3,   /* wrong password/keyfile, or the file was tampered */
    AISC_E_MEM    = -4,   /* allocation failure                               */
    AISC_E_RANDOM = -5,   /* OS RNG failure                                   */
    AISC_E_ARG    = -6    /* invalid argument                                 */
};

/* Argon2id cost. Higher = slower to brute force AND slower for you. */
typedef struct {
    uint32_t mem_blocks;  /* memory, in 1 KiB blocks (65536 = 64 MiB default)  */
    uint32_t passes;      /* iterations, >= 1                                 */
} aisc_kdf;

/* The default cost (see ais_crypto.c). */
aisc_kdf aisc_default_kdf(void);

/* Encrypt `plain` into a self-contained file image (header+ciphertext+tag),
 * returned in *out (malloc'd; the caller frees). `keyfile` is the optional
 * second factor and may be NULL. Returns AISC_OK or an AISC_E_* code. */
int aisc_encrypt(const uint8_t *plain,    size_t plain_len,
                 const uint8_t *password, size_t pw_len,
                 const uint8_t *keyfile,  size_t kf_len,
                 aisc_kdf kdf,
                 uint8_t **out, size_t *out_len);

/* Decrypt a file image into *out (malloc'd; the caller frees, then wipes it
 * with aisc_wipe). Authentication failure returns AISC_E_AUTH and writes
 * nothing. The KDF cost is read from the file header. */
int aisc_decrypt(const uint8_t *file,     size_t file_len,
                 const uint8_t *password, size_t pw_len,
                 const uint8_t *keyfile,  size_t kf_len,
                 uint8_t **out, size_t *out_len);

/* Read a passphrase from /dev/tty with echo OFF (not stdin, so it cannot be
 * piped from shell history; not argv/env, so it never hits ps or /proc).
 * `confirm` != 0 asks twice and requires a match. Returns the length, or -1.
 * Caller should mlock+wipe `buf`. */
int aisc_prompt_passphrase(const char *prompt, int confirm,
                           char *buf, size_t buf_sz);

/* Constant-time wipe (compiler cannot optimize it away). */
void aisc_wipe(void *p, size_t n);

#endif /* AIS_CRYPTO_H */
