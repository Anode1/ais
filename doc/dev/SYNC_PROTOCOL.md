# AIS LAN sync, protocol spec (DRAFT, for review, no code yet)

## Goal
One-shot, no-cloud, no-dependency transfer of an index between two devices on the same
network, then reconcile with the existing `ais --merge`. Reuses the `--serve` socket
pattern. This is the "push my phone to my laptop right now, nothing installed, same
Wi-Fi" path; for set-and-forget or cross-network sync, use Syncthing (see doc/SYNC.md).

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

    ais --offer [--timeout 60]
        Start an ephemeral HTTP server bound to the LAN. Print: the URL (ip:port), a
        one-time TOKEN, and a QR encoding both. Serve the endpoints below to ONE
        authenticated client, then exit (on first successful pull, or at --timeout).

    ais --pull URL --token TOK
        Fetch store+tomb+blobs from URL into a temp index, then merge it into the
        current index (the existing `ais --merge` path). Print a summary
        (added / deleted / unchanged) and delete the temp index.

Phone GUI: scan the QR to fill URL+token, one tap to pull.

## Wire protocol (HTTP/1.0, same loop as --serve)
Every request carries the token (`X-AIS-Token: <TOK>` header, or `?token=`). Missing or
wrong token returns 403; the server keeps waiting and does not reveal whether data exists.

    GET /sync/meta          -> version + record count (client checks compatibility first)
    GET /sync/store         -> the raw store file (text/plain)
    GET /sync/tomb          -> the raw tomb file (204 if none)
    GET /sync/blobs         -> blob filenames, one per line
    GET /sync/blob/<name>   -> one blob file (name validated: reject any '/' or '..')

Client order: meta (version gate) -> store -> tomb -> list blobs -> fetch missing blobs.

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
- One-way by nature: after A pulls B, A holds A+B and B is unchanged; full convergence =
  both sides pull.

DECISION (chosen: **full merge**). Build the tombstone-union reconcile first (the
seamless-merge engine item), then sync gets true convergence including deletions.
Additions-only was rejected as a temp solution. Conflict rule: **last-write-wins by the
store's `ts` column** (delete-after-add wins; re-add-after-delete wins), deterministic
for a single user across their own devices.

## Security model (end-to-end encrypted, from the start)
`--serve` binds 127.0.0.1 deliberately. `--offer` MUST bind the LAN interface, so the
channel is encrypted end-to-end, not merely token-gated:
- the offering device generates a **high-entropy one-time token** (>= 128-bit random,
  shown as text and a QR, never a short human PIN);
- that token is the **shared secret**: both sides derive a session key from it and the
  whole transfer body is sealed with **XChaCha20-Poly1305** (the ais_crypto AEAD), so a
  MITM on the LAN sees only ciphertext and tampering fails the Poly1305 tag;
- **ephemeral + single client**: the server runs only during the offer, serves one
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

## Implementation notes
- New file `sync.c` (+ `sync.h`). Reuse the accept/read/write helpers from `serve.c`; if
  clean, factor the shared bits into a tiny `httpd` helper, else duplicate the few lines.
  Do not restructure `serve.c` while the GUI redesign is in flight there.
- Token from the OS RNG already in `ais_crypto` (`rand_bytes`) / `/dev/urandom`.
- Rough size: ~250-400 lines, no new dependency.

## Open questions (resolve before coding)
1. Does `--merge` union tombstones today? If not, that is the first task.
2. Blob filename collisions across devices (same timestamp): keep ts-names or switch to a content hash?
3. v1 pairing: emit a real QR (terminal unicode blocks + phone GUI), or just print URL+token and have the phone type it?
4. `--offer` bind on multi-homed machines: all interfaces, or prompt which one?
5. Full-store merge is an O(store) scan per sync: fine for personal scale, revisit only for very large indexes.
