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
URL); `ais --import <url> --token T` pulls and merges. Follow-ups: blob transfer, QR, GUI.

    device A                                            device B
      store ──feed_export──► A|ts|keys|value  (live records)
                             D|ts|hash         (tombstones)
                             │
                       aisc_seal(token)         XChaCha20-Poly1305,
                             │                  key = blake2b(token)
                       sync_serve ───TCP───► sync_pull
                       (bind LAN, ONE          (connect, send token,
                        client, token           read [len][sealed])
                        auth, [len][sealed])           │
                                               aisc_unseal(token)  ◄─ wrong token / tampering
                                                      │               rejected HERE, before
                                               feed_import_from ──┐    anything is merged
                                                                  ▼
                                                          store (merge): live records arrive,
                                                          deletions propagate, last-write-wins by ts

1. **Engine: tombstone-union merge** (`doc/dev/MERGE.md`) — **DONE**. `--import` is merge-aware;
   timestamped content-addressed tombstones; last-write-wins; record-level (v1), `ktomb`
   (key-level) deferred.
2. **Transport: `sync.c`** — **DONE**. `sync_export_sealed`/`sync_import_sealed` (seal / merge a
   stream), `sync_serve`/`sync_pull` (ephemeral single-client TCP, token auth, timeouts),
   `aisc_seal`/`aisc_unseal`/`aisc_token` (crypto). Forked-loopback test green.
3. **CLI surface** — **DONE**. `ais --export --serve [PORT]` (`sync_serve_lan`: token + pairing
   line, then `sync_serve`) and `ais --import <url> --token T` (`sync_pull_url`: parse host:port,
   `sync_pull`); `--token` flag added; `ais --export` to stdout unchanged.
4. **GUI: a "Sync" surface** — later. Phone (scan QR -> import) and the desktop GUIs.

Owner: the sync/engine track. All decisions locked (`ktomb` deferred).

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

    ais --export [--timeout 60]
        Serve to ONE authenticated peer over the LAN. Print the URL (ip:port), a one-time
        high-entropy TOKEN, and a QR encoding both. Exit on the first successful import, or
        at --timeout. (`--dump` is unchanged: print records to stdout, local.)

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
The original draft used HTTP/1.0 (for browser compatibility), but the client is `ais` itself,
not a browser, so the implementation is a smaller raw protocol:

    client connects, sends:   <token>\n
    server validates the token (length-checked compare). On a mismatch it serves nothing and
      closes -- no error body, so it does not reveal whether data exists.
    server replies:           [4-byte big-endian length][sealed blob]
       where the sealed blob = aisc_seal(token, the full A|/D| merge stream from feed_export).
    client reads the length + blob, aisc_unseal(token, ...), then feed_import_from the result.

The whole merge stream (live records + tombstones, including any secret `aisc:` markers) is
sealed as ONE blob, so the store/tomb/blobs split of the draft is unnecessary: the stream
already carries everything `--import` needs. Both sides set `SO_RCVTIMEO`/`SO_SNDTIMEO` so a
stalled peer cannot hang the transfer; the server is single-client and exits after one pull.

## Merge / convergence
There is NO index-to-index merge command today; `c/merge.c` is the query-time
posting-list merge. Reconcile is built from existing pieces:
- Pull fetches the peer's records (the `--dump` format) and feeds them through the
  `--import` path (`feed_import`). `put` is idempotent (dedup by store scan), so new
  records land and duplicates are suppressed. **Additions converge.**
- **Deletions do NOT propagate yet**: `--import` only adds, `--dump` skips tombstoned
  ids, and nothing unions the two `tomb` files. Propagating deletions needs a
  tombstone-union reconcile, which is the planned "seamless index merging" engine item
  (see ROADMAP).
- One-way by nature: after A imports B, A holds A+B and B is unchanged; full convergence =
  both sides import from the other.

DECISION (chosen: **full merge**). Build the tombstone-union reconcile first (the
seamless-merge engine item), then sync gets true convergence including deletions.
Additions-only was rejected as a temp solution. Conflict rule: **last-write-wins by the
store's `ts` column** (delete-after-add wins; re-add-after-delete wins), deterministic
for a single user across their own devices.

## Security model (end-to-end encrypted, from the start)
`--serve` binds 127.0.0.1 deliberately. `--export` MUST bind the LAN interface, so the
channel is encrypted end-to-end, not merely token-gated:
- the exporting device generates a **high-entropy one-time token** (>= 128-bit random,
  shown as text and a QR, never a short human PIN);
- that token is the **shared secret**: both sides derive a session key from it and the
  whole transfer body is sealed with **XChaCha20-Poly1305** (the ais_crypto AEAD), so a
  MITM on the LAN sees only ciphertext and tampering fails the Poly1305 tag;
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
1. **Blob files are not transferred yet.** `feed_export` emits records + tombstones; a doc /
   encrypted-doc record's *value* (the `aisc:@blobs/<ts>` reference) crosses, but the blob
   FILE does not, so the reference would dangle on the peer. Transferring blobs is a follow-up.
2. **QR**: emit a real QR (terminal unicode blocks + phone GUI), or just print URL+token for v1.
3. `--export` bind on multi-homed machines: all interfaces (current), or prompt which one?
4. Full-store merge is an O(store) scan per sync: fine at personal scale.

Resolved during implementation: identity = value (not keys+value); content hash is FNV-1a;
no tomb migration (v1/v2 coexist); the merge IS built (was "does --merge union tombstones?").
