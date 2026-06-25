/* secret.h -- the "aisc:" marker for encrypted values, and the recall-time
 * reveal policy. The crypto itself lives in crypto/ (ais_crypto.*); this is the
 * plaintext-side glue: detect a marked value, decide whether to reveal it
 * interactively, and route the reveal.
 *
 * Encrypted values are SELF-IDENTIFYING -- value = "aisc:" + base64 of the
 * AIS-CR1 file image -- so detection is an exact prefix check, never a
 * heuristic. Every non-reveal path treats a marked value as an opaque ordinary
 * value, so dump / find / import / sync carry it through unchanged; only an
 * interactive recall at a terminal triggers a decrypt dialog.
 */
#ifndef AIS_SECRET_H
#define AIS_SECRET_H

/* The text prefix a stored encrypted value carries. */
#define AIS_SECRET_PREFIX "aisc:"

/* 1 if VALUE is a marked (encrypted) value, else 0. NULL-safe. */
int secret_is_marked(const char *value);

/* 1 if a plain recall should reveal secrets interactively: stdout is a terminal
 * AND /dev/tty is usable (so we can prompt and reveal there). 0 otherwise -- a
 * pipe, redirect, cron job, or no tty -- where secrets stay opaque. The caller
 * has already established this is a recall, not --dump/--find/--serve/etc. */
int secret_reveal_context(void);

/* Reveal a marked secret to the user (PROTOTYPE STUB): writes to /dev/tty only,
 * never stdout. The real version will read a passphrase
 * (crypto/aisc_prompt_passphrase) and decrypt (aisc_decrypt). Returns 0, or -1
 * if /dev/tty cannot be opened. */
int secret_reveal(long id, const char *marked_value);

#endif /* AIS_SECRET_H */
