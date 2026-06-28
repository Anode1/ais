# Why another password manager

The one-line reason: the operating system's own keystores answer the question
**"is this the logged-in user?"**, not **"should this particular program have this
secret right now?"** On Windows and Linux, that means any code running as you can
read your stored passwords in cleartext. ais_crypto changes the question the
secret is gated on.

## What the OS keystores actually do

**Windows (DPAPI / Credential Manager).** Saved secrets are wrapped with
`CryptProtectData`, whose key chain derives from your **login password** (the
DPAPI master key in your profile, unlocked at logon). The key is scoped to the
**user, not the application**. Once you are logged in, that key is live in your
session, so any process running as you can call `CryptUnprotectData` and get the
plaintext. This is exactly what infostealer malware and tools like mimikatz do.
DPAPI protects against a different user and against an offline copy of the file.
It does not protect against same-user code.

**Linux (Secret Service: GNOME Keyring / KWallet).** The keyring is encrypted
with a key derived from your login password and is **auto-unlocked at login by
PAM**. It is exposed over the D-Bus session bus with **no real per-application
gating**: any process in your session can ask and receive the secret. And if no
keyring is running (headless, or a browser configured with `--password-store=basic`),
the browser falls back to a **hardcoded key**, which is obfuscation, not
encryption.

So the honest answer to "can a malicious process running as me get the
plaintext?" on Windows and Linux is **yes**. The access decision is "are you this
user", and you already are.

## Why macOS is stronger, and Linux/Windows are not

Apple's Keychain adds three things the others lack:

1. **Per-item ACLs tied to code signatures.** Each item lists which signed apps
   may read it. A different process is denied or triggers a user prompt. The check
   is "is this the authorized app", not just "is this the user."
2. **Code signing + Gatekeeper** give those ACLs teeth: identity is cryptographic.
3. **Secure Enclave** (Apple Silicon / T2): keys live in hardware that even kernel
   code cannot extract, gated by Touch ID / Face ID.

On Linux and Windows the boundary is your **login session** (any code as you
wins). On macOS it is the **app's signed identity plus hardware-isolated keys**.
That is the gap: strong on Apple, weak on the other two, and you do not control
the algorithm on any of them.

## What ais_crypto changes

It gates secrets behind **something you know** (a master passphrase) plus
optionally **something you have** (a keyfile), neither of which is ever handed to
the OS keystore or tied to your login session:

- **Encrypted even while you are logged in.** The vault key comes from your
  passphrase via a memory-hard KDF (Argon2id), not from your login. A broad
  infostealer that scrapes DPAPI or the Secret Service gets nothing usable.
- **Raises the cost of offline guessing.** Argon2id makes each guess expensive
  even with the file in hand, so a stolen vault is not a stolen password list.
  This buys cost, not certainty: a weak passphrase still falls. The real strength
  is your passphrase entropy times the Argon2id cost.
- **Minimal plaintext window.** Decrypt one entry on demand, wipe immediately,
  never write plaintext to a temp file. Exposure shrinks to the instant of use.
- **Auditable algorithm, not opaque OS trust.** Open source, one small reviewable
  module: security by the algorithm (Kerckhoffs), so you verify it instead of
  trusting a closed service keyed to your login.
- **Strength on par with AES-256.** The default cipher, XChaCha20-Poly1305, is a
  modern AEAD of strength comparable to AES-256 and is constant-time in software
  (no cache-timing side channel). AES-256-GCM is a one-line swap for a project
  that needs that specific cipher. The vault's strength rests on the passphrase
  and the Argon2id cost, which is where it should rest.
- **Portable and uniform.** The same guarantees on Linux, Windows, and macOS,
  instead of whatever the local OS keystore happens to enforce.

## Agents make this urgent, not obsolete

An AI agent runs *as you*, with your shell privileges, so it is a new and now
*everyday* instance of "a process running as you", the exact thing the OS and
browser keystores cannot gate against. The threat shifts from "malware could read
my keystore" (rare; you had to be infected) to "the tool I run all day inherits my
login and can read my keystore, browser passwords, SSH keys, and cloud tokens." It
does not even take a malicious agent: one that reads untrusted input (a web page, a
file, an email) can be steered by prompt injection into reading and exfiltrating
any credential it can reach.

This does not make password managers pointless; it makes the distinction decisive.
A store that *auto-unlocks at login* (the OS keystore, browser-saved passwords) is
now routinely exposed, because the agent is logged in as you. A vault gated by a
*passphrase the agent does not hold* removes that free win: with no auto-unlock,
the agent can no longer simply *read* your secrets at rest. It does not make you
invulnerable, on one machine there is no true out-of-band channel, so an agent that
is actively present can still keylog the passphrase as you type it and read the one
entry you decrypt for use (see "Honest limits"). The shift is real but bounded:
from "passively scraped in bulk, any time" to "must actively capture you at the
moment of use." The rule for the agent era: keep anything an agent must never touch
(banking, personal logins) in a passphrase-gated store, never in the OS keystore,
and never auto-unlocked in the agent's environment.

The same logic favors *any* passphrase-gated manager, not this one: a locked
1Password or KeePass clears the bar too. ais_crypto's niche is not a security edge
over them, it is *integration and ownership*. The encrypted secret lives inline in
your own plain-text context index, not in a separate cloud vault, so you carry your
whole memory store as files you own and sync however you already do (a file-sync
tool, a USB stick, your own drive, no proprietary cloud), with the same small
auditable engine on PC, Mac, Linux, and phone.

## Honest limits (so this is reasoning, not marketing)

macOS is not magic either: malware can inject into the authorized app, root can
bypass, and users get trained to click "Always Allow." And no software manager
removes the in-memory exposure: while a secret is decrypted for use, code running
as you can read it, and a live keylogger can capture your passphrase as you type
it. ais_crypto does not beat an active rootkit.

## The claim, stated honestly

ais_crypto raises the bar from **"trivial for any process running as you"** (the
Windows and Linux keystores) to **"requires actively capturing your passphrase at
the moment you type it."** That is a large, real improvement, plus an auditable
algorithm and uniform behavior across operating systems. It is not an absolute
shield, and it does not pretend to be.
