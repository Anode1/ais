/* secret.h -- the "aisc:" marker for encrypted values: detection, the recall-
 * time reveal policy, and encrypt/decrypt of a value into/out of that marker.
 *
 * The crypto lives in crypto/ (ais_crypto.*). This is the plaintext-side glue:
 * an encrypted value is SELF-IDENTIFYING -- "aisc:" + base64 of the AIS-CR1 file
 * image -- so detection is an exact prefix check, never a heuristic. Every
 * non-reveal path treats a marked value as opaque ordinary text, so dump / find
 * / import / sync carry it through unchanged; only an interactive recall at a
 * terminal triggers the decrypt dialog. When crypto/ is not vendored, the
 * encrypt/decrypt/prompt entry points return -1 (the feature is simply absent).
 */
#ifndef AIS_SECRET_H
#define AIS_SECRET_H

#include <stddef.h>

/* The text prefix a stored encrypted value carries. */
#define AIS_SECRET_PREFIX "aisc:"

/* 1 if VALUE is a marked (encrypted) value, else 0. NULL-safe. */
int secret_is_marked(const char *value);

/* 1 if a plain recall should reveal secrets interactively: stdout is a terminal
 * AND /dev/tty is usable. 0 for a pipe, redirect, cron, or no tty. */
int secret_reveal_context(void);

/* Encrypt PLAIN[plain_len] under PASSWORD into a marked value ("aisc:" + base64
 * of the encrypted file image) in OUT (size OUTSZ, NUL-terminated). Returns the
 * marked length, or -1 (crypto not built, encrypt error, or OUT too small). */
long secret_encrypt(const unsigned char *plain, size_t plain_len,
                    const unsigned char *password, size_t pw_len,
                    char *out, size_t outsz);

/* Decrypt a marked VALUE under PASSWORD into OUT (size OUTSZ). Returns the
 * plaintext length, or -1 (not marked, wrong password / tampered, crypto not
 * built, or OUT too small). */
long secret_decrypt(const char *value,
                    const unsigned char *password, size_t pw_len,
                    unsigned char *out, size_t outsz);

/* Prompt for one line on /dev/tty with echo OFF (CONFIRM != 0 asks twice and
 * requires a match). Thin wrapper over the crypto module's tty reader; returns
 * the length, or -1 (crypto not built, or error). The caller should secret_wipe BUF. */
int secret_prompt(const char *prompt, int confirm, char *buf, size_t buf_sz);

/* Reveal a marked secret interactively: prompt the passphrase, decrypt, and
 * write the cleartext to /dev/tty (never stdout). Returns 0, or -1. */
int secret_reveal(long id, const char *marked_value);

/* Volatile zero-wipe (not optimized away). Always available, no crypto needed. */
void secret_wipe(void *p, size_t n);

#endif /* AIS_SECRET_H */
