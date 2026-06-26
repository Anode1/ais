/* secret.h -- the "aisc:" marker for encrypted values: detection, the recall-
 * time reveal policy, and encrypt/decrypt of a value into/out of that marker.
 *
 * The crypto lives in crypto/ (ais_crypto.*). This is the plaintext-side glue.
 * An encrypted value is SELF-IDENTIFYING -- always the "aisc:" prefix -- in two
 * forms, told apart by the first byte after the prefix (an exact check, never a
 * heuristic; base64 never begins with '@'):
 *
 *     aisc:<base64>        INLINE  -- a short secret, the whole AIS-CR1 image is
 *                                     base64'd into the value (mode 1, -e).
 *     aisc:@<relpath>      BLOB    -- a big secret (a document), the image lives
 *                                     in <index>/<relpath> (mode 2, --doc -e).
 *
 * Both share the "aisc:" prefix, so every non-reveal path treats them as opaque
 * ordinary text and dump / find / import / sync carry them through unchanged;
 * only an interactive recall at a terminal triggers the decrypt dialog. When
 * crypto/ is not vendored, the encrypt/decrypt/prompt entry points return -1
 * (the feature is simply absent).
 */
#ifndef AIS_SECRET_H
#define AIS_SECRET_H

#include <stddef.h>

/* The text prefix a stored encrypted value carries. */
#define AIS_SECRET_PREFIX "aisc:"

/* 1 if VALUE is a marked (encrypted) value -- either form -- else 0. NULL-safe. */
int secret_is_marked(const char *value);

/* If VALUE is a BLOB reference (aisc:@<relpath>), return the relpath (a pointer
 * into VALUE); else NULL (an inline value or a non-secret). The '@' right after
 * "aisc:" is the exact discriminator -- base64 never starts with it. */
const char *secret_blob_relpath(const char *value);

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

/* Encrypt PLAIN[plain_len] under PASSWORD and write the raw AIS-CR1 image to
 * PATH (no base64, no marker -- the marker lives in the record value that
 * references this blob). Returns 0, or -1. For mode-2 (--doc -e) big secrets. */
int secret_encrypt_to_file(const unsigned char *plain, size_t plain_len,
                           const unsigned char *password, size_t pw_len,
                           const char *path);

/* Best-effort shred of an encrypted-blob payload: if VALUE is aisc:@<relpath>,
 * overwrite <index_dir>/<relpath> with zeros and unlink it; a no-op for inline
 * or non-secret values. Best-effort only -- on SSD/CoW/backed-up storage the
 * overwrite may not reach the physical bytes; the real protection is that the
 * blob is ciphertext. No crypto needed (plain file I/O), always available. */
void secret_shred_blob(const char *index_dir, const char *value);

/* Prompt for one line on /dev/tty with echo OFF (CONFIRM != 0 asks twice and
 * requires a match). Thin wrapper over the crypto module's tty reader; returns
 * the length, or -1 (crypto not built, or error). The caller should secret_wipe BUF. */
int secret_prompt(const char *prompt, int confirm, char *buf, size_t buf_sz);

/* Reveal a marked secret interactively: prompt the passphrase, decrypt, and
 * write the cleartext to /dev/tty (never stdout). For a BLOB value the image is
 * read from INDEX_DIR/<relpath>; INDEX_DIR is unused for inline values (pass the
 * index dir regardless). Returns 0, or -1. */
int secret_reveal(long id, const char *marked_value, const char *index_dir);

/* Volatile zero-wipe (not optimized away). Always available, no crypto needed. */
void secret_wipe(void *p, size_t n);

#endif /* AIS_SECRET_H */
