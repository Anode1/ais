# AIS LAN sync, protocol spec (IMPLEMENTED)

## Goal
One-shot, no-cloud, no-dependency transfer of an index between two devices on the same
network, then reconcile via the merge-aware `--import` (see `doc/dev/MERGE.md`). Reuses the
`--serve` socket pattern. This is the "push my phone to my laptop right now, nothing
installed, same Wi-Fi" path; for set-and-forget or cross-network sync, use Syncthing
(see `doc/SYNC.md`).

## Status (as built)
Decisions LOCKED: full tombstone-union merge (decision B), end-to-end encrypted from the
start, CLI = `--export` / `--import <url>` (no `--remote`), `--import` merge-aware for all
sources, plain `--dump` unchanged. **Built, wired, and tested** (257 unit tests incl. a
forked-loopback socket test): `ais --export --serve [PORT]` serves one peer (prints token +
URL); `ais --import <url> --token T` pulls and merges. Blob transfer is done (below); remaining follow-ups: GUI polish.

    device A                                            device B
      store ──feed_export──► A|ts|keys|value  (live records)
                             D|ts|hash         (record tombstones)
                             K|ts|hash|key     (key-detaches)
                             │
                       aisc_seal_key(k_seal)    k_seal = subkey(token,"seal");
                             │                  the token itself is NEVER sent
                       sync_serve ──challenge─► sync_pull
                             ◄────── proof ──── (answer = keyed proof of
                       (verify; [len][sealed])   token+challenge)
                                                      │
                                               aisc_unseal_key(k_seal) ◄─ wrong token / tampering
                                                      │                   rejected HERE, before
                                               feed_import_from ──┐       anything is merged
                                                                  ▼
                                                          store (merge): live records arrive,
                                                          deletions propagate, last-write-wins by ts

1. **Engine: tombstone-union merge** (`doc/dev/MERGE.md`) — **DONE**. `--import` is merge-aware;
   timestamped content-addressed tombstones; last-write-wins; BOTH record-level (`tomb`) and
   key-level (`ktomb`) -- a key-detach travels as a `K|<ts>|<hash>|<key>` line (`ais_merge_detach`)
   and stays removed after sync.
2. **Transport: `sync.c`** — **DONE**. `sync_export_sealed`/`sync_import_sealed` (seal / merge a
   stream), `sync_serve`/`sync_pull` (ephemeral single-client TCP, token auth, timeouts),
   `aisc_seal`/`aisc_unseal`/`aisc_token` (crypto). Forked-loopback test green.
3. **CLI surface** — **DONE**. `ais --export --serve [PORT]` (`sync_serve_lan`: token + pairing
   line, then `sync_serve`) and `ais --import <url> --token T` (`sync_pull_url`: parse host:port,
   `sync_pull`); `--token` flag added; `ais --export` to stdout unchanged.
4. **GUI: a "Sync" surface** — later. Phone (scan QR -> import) and the desktop GUIs.

Owner: the sync/engine track. All decisions locked; `ktomb` key-detach shipped (merges via the `K|` line).

## Non-goals (this is where the weight lives, kept out on purpose)
- No auto-discovery (no mDNS/multicast). Pairing is manual: IP:port + token, shown as text and a QR.
- No cross-internet, relays, or NAT traversal. Same LAN only.
- No continuous/background sync. One run, one transfer, the server exits.
- No TLS/cert machinery. Security is a one-time token plus an ephemeral, single-use server (below).

## What moves
Only the real, non-rebuildable data:

    store      append-only records
    tomb       deletions
    blobs/     documents saved by `doc`

NOT `idx/`, `off`, `next_id`, `version`, `lock` (rebuilt or local-only).

The **full store** is sent, not an id-delta: device id spaces are independent, so
"records after id N" is meaningless across devices. `put`/`merge` are idempotent and
dedup by content, so re-sending the whole store is safe and only genuinely-new records
land. Stores are small plain text; optimize to a ts/hash delta later only if size warrants.

## CLI
Reuses the existing dump/import vocabulary. The only new verb is `--export`; there is no
`--remote` flag (a URL operand is self-evidently remote, otherwise it is stdin/file).

    ais --export --serve [PORT] [--timeout 60]
        Serve to ONE authenticated peer over the LAN. Print the URL (ip:port), a one-time
        high-entropy TOKEN, and a QR encoding both. Exit on the first successful import, or
        at --timeout. (Bare `ais --export`, without `--serve`, writes the A|/D| merge stream
        to stdout; `--dump` prints plain records, local.)

    ais --import <url> --token TOK
        Remote: fetch the peer's export stream, decrypt with TOKEN, and MERGE into the
        current index (last-write-wins; deletions included). Print a summary
        (added / deleted / unchanged).

    ais --import [< file]
        Local: stdin or a file, unchanged surface, same merge path as the remote case.

`--import` is merge-aware for EVERY source (stdin / file / URL); the operand's form selects
it (a `http(s)://` URL is remote, otherwise local). The only remote-only flag is `--token`
(auth, not source). Phone GUI: scan the QR to fill url+token, one tap to import.

## Wire protocol (as built: a raw length-framed exchange, not HTTP)
The client is `ais` itself, not a browser, so the protocol is a small raw exchange. The token
is NEVER sent; the client proves knowledge by answering a fresh challenge:

    server -> client:   challenge   (24 random bytes, fresh per session)
    client -> server:   proof       (32 bytes = BLAKE2b-keyed(token, "ais-sync-auth-v1" || challenge))
    server verifies the proof in constant time; on a mismatch it serves nothing and closes
      (no error body, so it does not reveal whether data exists).
    server -> client:   [4-byte big-endian length][sealed blob]
       sealed = aisc_seal_key(k_seal, the full A|/D|/K| stream), k_seal = BLAKE2b-keyed(token,
       "ais-sync-seal-v1") -- a DIFFERENT subkey than the proof.
    client reads length + blob, aisc_unseal_key(k_seal, ...), then feed_import_from.

The token never crosses the wire and the seal key is a domain-separated subkey, so a passive
sniffer who captures the whole handshake learns neither the token nor `k_seal` and cannot
decrypt the stream. The sealed blob is `version(1) | nonce(24) | ciphertext | tag(16)` (the
version byte authenticated as AD, for algorithm agility). The whole merge stream (records +
tombstones, incl. any `aisc:` markers) is one blob, capped at 64 MiB on BOTH ends. Both sides
set `SO_RCVTIMEO`/`SO_SNDTIMEO` so a stalled peer cannot hang it; the server is single-client
and exits after one pull.

## Sealed payload format (records + blob files)
Inside the sealed blob, the *plaintext* is versioned and self-describing:

    byte 0        AIS_SYNC_PROTO (=1), the payload-format version. This is distinct from the
                  AEAD-frame version byte above (line ~103): that one guards the crypto
                  envelope, this one guards the plaintext layout. On a mismatch the importer
                  merges NOTHING and returns loud (-2), surfaced as "the other device runs a
                  different AIS version -- update both", so a format skew can never silently
                  half-apply.
    blob section  zero or more  B|blobs/<name>|<size>\n  headers, each followed by <size> raw
                  bytes: the doc blob FILES themselves. The section ends at the first line
                  that does not start with "B|".
    record stream the A|/D|/K| feed_export text (records + tombstones + key-detaches), from there to end of
                  payload -- unchanged from the one-way format, so this byte-0 gate is the
                  only thing an older peer would trip on.

Blob merge is by NAME + CONTENT (blobs are immutable and timestamp-named, so never by mtime):
same name + identical bytes = skip (dedup); same name + different bytes = keep BOTH (the
incoming file lands as `blobs/<stem>-<seq><ext>`) and the incoming record's value is repointed,
covering both the plain `blobs/X` and the encrypted `aisc:@blobs/X` value forms. The relpath is
validated to stay inside `blobs/` (no `/`, no `..`); each blob and the whole payload are capped
(`AIS_SYNC_MAX_BLOB` / 64 MiB) on both ends.

## Two-way in one round (`--sync`, bidir)
`sync_serve`/`sync_pull` take a `bidir` flag; with it set, one connection carries the sealed
payload BOTH ways (server sends then receives+merges, client the reverse), so both devices
converge in a single round with no fixed sender/receiver -- safe because the merge is a CRDT
(`MERGE.md`). CLI verb `ais --sync --serve [PORT]` / `ais --sync <url> --token T`; the mobile
app's one "Sync" button (Host / Join) runs the same exchange. One-way `--export`/`--import`
(`bidir=0`) stays for a deliberate one-directional copy.

## Merge / convergence
Reconcile is the content-keyed, ts-resolved tombstone-union merge (see `MERGE.md`).
`--export` emits adds (`A|ts|keys|value`), record tombstones (`D|ts|hash`), and per-key
detaches (`K|ts|hash|key`); `--import` replays them through `feed_import_from`, where `put`
is idempotent (dedup by content), `ais_merge_del` unions the record tombstones, and
`ais_merge_detach` unions the key-detaches (so a removed tag stays removed after sync).
**Additions and deletions both converge**,
last-write-wins by the store's `ts` column (delete-after-add wins; re-add-after-delete
wins), deterministic for one user across their own devices. One-way by nature: after A
imports B, A holds A+B and B is unchanged; full convergence is both sides importing from
the other.

## Security model (end-to-end encrypted, from the start)
The local web `--serve` binds 127.0.0.1 deliberately; `--export --serve` (and `--sync
--serve`) bind all interfaces so a LAN peer can reach them, and the channel is encrypted
end-to-end, not merely token-gated:
- the exporting device generates a **high-entropy one-time token** (>= 128-bit random,
  shown as text and a QR, never a short human PIN);
- the token is the shared secret but **never travels**: the client proves knowledge by
  answering a fresh challenge with `BLAKE2b-keyed(token, "...-auth-v1" || challenge)`, so a
  sniffer who sees the proof cannot recover the token or replay it on a later session;
- the body is sealed with **XChaCha20-Poly1305** under a SEPARATE subkey
  `BLAKE2b-keyed(token, "...-seal-v1")`, domain-separated from the auth proof, so observing
  the handshake never yields the seal key; a MITM sees only ciphertext and tampering fails
  the Poly1305 tag (and a version byte allows future algorithm changes);
- **ephemeral + single client**: the server runs only during the export, serves one
  authenticated peer, and exits on first success or at `--timeout`.

Cost is negligible. Because the token is already high-entropy random, we do NOT use the
memory-hard Argon2id (that exists to slow brute-force of weak passphrases). A fast KDF
over the token (Monocypher `crypto_blake2b`, HKDF-style) yields the session key in
microseconds, and XChaCha20-Poly1305 runs at GB/s, so a personal store encrypts
instantly. No Argon2 cost, no PAKE, no TLS/cert machinery.

Why a long token and not a short code: a short human code would need a PAKE (SPAKE2, as
in Magic Wormhole) to resist offline guessing. We sidestep that by showing a long token
via QR/paste, so direct key derivation is safe and simple.

Covered: a passive snoop AND an active MITM on a hostile LAN get only ciphertext without
the token, which is infeasible to brute-force. Still out of scope: cross-internet reach
(no relay), which remains Syncthing's job.

## Implementation notes (as built)
- New file `sync.c` (+ `sync.h`), gated POSIX + crypto; Windows / no-crypto builds get inert
  stubs that return -1. It does NOT reuse `serve.c` (that loop is HTTP-on-127.0.0.1 for the
  browser GUI); `sync.c` has its own small raw socket code that binds the LAN.
- Token from `aisc_token` (OS RNG via `ais_crypto`'s `rand_bytes`).
- The seal derives the key as `blake2b(token)` and uses XChaCha20-Poly1305 (`aisc_seal` /
  `aisc_unseal`), no Argon2.
- Tested by `test_sync_sealed` (sealed-stream round-trip) and `test_sync_socket` (a forked
  client/server over loopback) in `c/tests.c`.

## Remaining work & follow-ups
1. **Blob files** — **DONE** (see "Sealed payload format" above). The doc blob FILES now travel
   in the sealed payload ahead of the record stream, with keep-both-on-collision and value
   repointing, so a synced doc reference no longer dangles.
2. **QR / scan-to-join** — pairing link defined: `ais://sync?host=<ip:port>&token=<hex>`. The
   phone registers the `ais://` scheme (Android intent-filter + iOS `CFBundleURLTypes`) and a
   thin native `MethodChannel` (`ais/deeplink`, MainActivity / SceneDelegate) hands a scanned
   link to Dart, which CONFIRMS then joins -- so the phone's OWN camera scans (no bundled QR
   scanner / no ML Kit). Android is the tested path; iOS is best-effort (deferred to TestFlight).
   Still to do: RENDER that link as a QR on a host device -- the desktop web Sync surface
   (`serve.c`, a small vendored single-file JS generator), and/or a phone-host QR via the
   pure-Dart `qr_flutter`. The link is confirmed before any sync because it can arrive from
   anywhere.
3. `--export` bind on multi-homed machines: all interfaces (current), or prompt which one?
4. Full-store merge is an O(store) scan per sync: fine at personal scale.

Resolved during implementation: identity = value (not keys+value); content hash is FNV-1a;
no tomb migration (v1/v2 coexist); the merge IS built (was "does --merge union tombstones?").
