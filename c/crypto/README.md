# ais_crypto

A small, auditable file-encryption module for the ais secret store. One
translation unit (`ais_crypto.c`) around two vetted primitives, so the whole
security-critical surface fits on a couple of screens and can be reviewed in
isolation. Security rests on the algorithm and your passphrase, not on hiding
the code (Kerckhoffs): the source and file format are public; only the
passphrase and optional keyfile are secret.

> **Why this exists** rather than the OS keystore (Windows DPAPI, Linux Secret
> Service) or a browser's built-in manager: see [WHY.md](WHY.md). Short version:
> those gate secrets on "are you the logged-in user", so any process running as
> you can read them; this gates on a passphrase that never touches the OS.

## What it does

```
key   = Argon2id(passphrase [+ keyfile], random salt)      # 256-bit, memory-hard
file  = header || ciphertext || tag                        # authenticated
cipher = XChaCha20-Poly1305                                 # AEAD
```

- **Argon2id** turns your passphrase into the key. This is the actual barrier
  against guessing: a strong cipher with a weak KDF is weak. Default cost is
  64 MiB / 3 passes (tune in `aisc_default_kdf`); kept openable on low-end phones,
  since the cost is stored per file and re-allocated to decrypt.
- **XChaCha20-Poly1305** (AEAD) gives confidentiality and integrity together, so
  any tampering fails loudly instead of decrypting to garbage. Constant-time in
  pure software (no AES-NI dependency, no cache-timing side channel). It is
  "AES-256 or better" in strength; if you specifically need AES-256-GCM, swap the
  two `crypto_aead_*` calls for libsodium's `crypto_aead_aes256gcm_*`.

## File format

All integers little-endian. The 57-byte header is authenticated as associated
data, so the KDF params, salt, and nonce cannot be altered without failing
decryption.

```
off  size  field
0     8    magic "AIS-CR1\0"
8     1    kdf id (0x02 = Argon2id)
9     4    Argon2 memory (1 KiB blocks)
13    4    Argon2 passes
17   16    salt   (random, NOT secret)
33   24    nonce  (random)
57    N    ciphertext (== plaintext length)
57+N 16    Poly1305 tag
```

## Two design points worth understanding

**No hardcoded salt.** A salt is not secret; its only job is to be unique per
file so an attacker cannot precompute. Hardcoding it (especially in open-source
code) makes it public and fixed, which is the worst case. So the salt is random
per file and stored in the header in cleartext. That costs nothing and is
strictly stronger.

**The real second factor is a keyfile, not a baked-in secret.** If you want
"two parts," use a keyfile: something you have, in addition to the passphrase you
know. It is fed into Argon2's secret-key slot, so it changes the derived key and
is never written into the file. Lose either factor and the data is unrecoverable;
an attacker needs both.

```
# encrypt/decrypt with a keyfile as the second factor
AIS_KEYFILE=/media/usb/ais.key ./aiscrypt enc secrets.txt secrets.aisc
```

**Passphrase hygiene.** The passphrase is read from `/dev/tty` with terminal echo
off. It never comes from `argv` or an environment variable (which leak to `ps`
and `/proc`) and never from stdin (which could be piped from shell history). Key
and passphrase buffers are wiped after use; the CLI also `mlock`s the passphrase
buffer so it is not swapped to disk.

## Build

```
# 1) vendor the crypto primitives once (pins + checksums Monocypher)
sh vendor-monocypher.sh        # writes monocypher.c, monocypher.h, LICENSE.monocypher

# 2) build ais as usual; the engine Makefile now compiles this dir in
make            # from the repo root (or: make -C c)
```

The Makefile wiring is guarded: until `monocypher.c` is vendored, the engine
builds exactly as before, so a fresh checkout is never broken by a missing file.

```
# build the standalone self-test/CLI tool without touching the engine
cc -DAIS_CRYPTO_CLI -I. ais_crypto.c monocypher.c -o aiscrypt
```

## Usage from C

```c
#include "ais_crypto.h"
uint8_t *out; size_t out_len;
int rc = aisc_encrypt(plain, plain_len, pw, pw_len, keyfile, kf_len,
                      aisc_default_kdf(), &out, &out_len);
/* ... write out ... */
aisc_wipe(out, out_len); free(out);
```

## Licensing

- `ais_crypto.{c,h}` are part of ais and are **GPL-2.0-only** (see `../../COPYING`).
- **Monocypher** is dual-licensed **CC0-1.0 / BSD-2-Clause**: permissive, and
  GPL-compatible. We vendor `monocypher.c` / `monocypher.h` unchanged and keep its
  license as `LICENSE.monocypher`. The combined binary ships under ais's GPLv2;
  Monocypher's terms add at most attribution.

Note: for a bundled dependency you *want* a permissive license (CC0/BSD/MIT), not
GPL. A GPL dependency is the awkward case (copyleft obligations); a permissive one
drops into any project, GPL included.

## The residual risk (read this)

Encryption protects the file at rest. While ais has decrypted a secret to show or
use it, the cleartext exists in process memory, and anything running as your user
during that window can read it. We shrink the window (derive on demand, wipe key
and plaintext immediately, never write plaintext to a temp file) but cannot remove
it. The cipher is not the weak point; a decrypted live session is.
