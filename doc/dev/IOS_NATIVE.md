# Building a native iOS client on the AIS engine

Handoff spec for a native (Swift/SwiftUI) iOS client. The point: you do NOT
reimplement the index, the crypto, the sync protocol, or the merge. All of that
is portable C in `libais` behind a stable C ABI (`c/embed.h`). You write the
Swift UI plus a thin layer of iOS glue over about twenty C functions.

The same `libais` backs the CLI, the desktop web GUI, the Flutter app, and a
wasm build. A native iOS app built on it interoperates with all of them
automatically, because they share one wire format and one on-disk format.

## 1. The ABI you call (`c/embed.h`)

Handles are opaque `void *` (one per open index). Returned strings are
heap-allocated C strings you must release with `ais_embed_free`.

    lifecycle   ais_embed_open(dir) -> void*        open/create, takes the single-writer lock
                ais_embed_close(h)                  release lock, flush, free
                ais_embed_locate(out,outsz)         resolve the same index the CLI would use
                ais_embed_default_set(dir)          persist "change store"

    data        ais_embed_recall(h,keys,or_mode)    -> "id|value\n" lines (0=AND, 1=OR)
                ais_embed_store(h,keys,value)        -> id>0
                ais_embed_store_encrypted(h,k,v,pw)  -> id>0 (inline "aisc:" secret)
                ais_embed_reveal(marked,pw)          decrypt an inline "aisc:" value
                ais_embed_del(h,id) / _update(h,id,keys)
                ais_embed_timeline(h,before,count,from,to) -> "id|ts|keys|value\n" lines
                ais_embed_tags(h)                    -> "count|key\n" lines
                ais_embed_free(buf)                  free any returned string

    sync        ais_embed_sync_pull(h,url,token)     join: connect, exchange, both converge
                ais_embed_sync_serve(h,port,token)   host: wait for one peer, both converge
                (ais_embed_pull / ais_embed_serve are the one-way variants; prefer the
                 sync_* pair for a unified "Sync" button)

Return codes (sync): `0` merged, `-1` bad args / malformed URL, `-2` no peer
completed (timeout, wrong token, connect failure), `-3` port busy (bind failed,
returned at once, not after the timeout). None of these functions print.

## 2. The rules that are NOT in the signatures (read this twice)

- **Never call on the main thread.** `sync_serve` blocks up to ~120 s waiting for
  a peer; `sync_pull` blocks for the transfer. Run every sync call on a
  background `Task` / `DispatchQueue`. The Flutter app runs them on a background
  isolate for exactly this reason.
- **One caller per handle at a time.** The handle is single-writer; concurrent
  calls on the same handle (say a `recall` during a `sync`) are a data race.
  Serialize all engine calls onto one queue.
- **One sync at a time.** A scanned deep link can arrive while a sync is already
  running. Guard it: the Flutter app keeps a `_syncBusy` flag and refuses a
  second sync until the first returns. Do the same.
- `ais_embed_open` holds the single-writer lock for the handle's lifetime. Keep
  one long-lived handle per index; do not open the same dir twice concurrently.
- SIGPIPE from a dropped socket is already ignored inside the embed layer, so a
  peer hangup mid-sync will not crash the process. There is no `fork()` on the
  embed path (the fork lives only in the CLI / web host), so it is iOS-safe.

## 3. Sync and pairing

Use the bidirectional pair (`sync_pull` to join, `sync_serve` to host); both
sides converge in one round because the merge is a CRDT (see
`doc/dev/MERGE.md`), so there is no fixed sender/receiver. Wire protocol and
security model: `doc/dev/SYNC_PROTOCOL.md`.

- **Token.** Generate a >=128-bit random hex token with `SecRandomCopyBytes`,
  show it to the user, and pass it to `sync_serve`. The joiner passes the same.
  The token never crosses the wire (the engine does challenge-response); a wrong
  token is rejected before anything merges.
- **Pairing link** (the contract shared with the Android app and the web GUI):

      ais://sync?host=<percent-encoded ip:port>&token=<hex>

  `host` is `ip:port` percent-encoded (":" becomes "%3A"). To join from a scanned
  link: decode `host`, build `url = "http://" + host`, call `sync_pull(url, token)`.
  To host, find this device's LAN IPv4 (prefer a private range), pick a port
  (the app uses 8766), and render that link as the QR.
- **Confirm before joining a scanned link.** A link can come from anywhere and a
  sync shares this device's records; show a confirm dialog naming the peer first.

## 4. iOS platform glue

- **Info.plist** already carries the reusable pieces (see `app/flutter/ios/Runner/Info.plist`):
  `NSLocalNetworkUsageDescription` (the Local Network prompt fires on first bind)
  and `CFBundleURLTypes` registering the `ais` scheme.
- **Deep links.** The `ais://` scheme routes to the scene under the UIScene
  lifecycle; handle it in `SceneDelegate.scene(_:openURLContexts:)`. The Flutter
  app's `app/flutter/ios/Runner/SceneDelegate.swift` is a working reference for
  the routing (it forwards the URL; a native app parses and joins directly).
- **QR.** Display with any renderer. SCAN natively with the Vision framework
  (`VNDetectBarcodesRequest`) or AVFoundation metadata output: no third-party
  dependency, unlike Android's ML Kit. This is an iOS advantage; in-app scanning
  is cheap, so you can offer it alongside the camera-app deep-link path.
- **Index location.** Use the app's Documents directory (or an App Group
  container to share the index with an extension), then `ais_embed_open(dir)`.

## 5. Building libais for iOS

The static-library target already exists (`make lib` -> `libais.a`, the engine
minus `main`, with sync and the vendored Monocypher crypto compiled in, no
external dependency). For iOS, build one slice per SDK/arch, then assemble an
xcframework. Run on macOS with Xcode; verify on first use.

    # build one static slice for a given SDK + arch into a slice dir
    # (usage: slice <sdk> <arch> <min-flag> <outdir>)
    slice() {
      SDK=$(xcrun --sdk "$1" --show-sdk-path)
      CC="$(xcrun -f clang) -isysroot $SDK -arch $2 $3"
      make -C c clean >/dev/null
      make -C c libais.a CC="$CC" AIS_STD="-std=c99"
      mkdir -p "$4" && cp c/libais.a "$4/"
    }

    # device (arm64) and simulator (arm64 + x86_64); merge the two sim arches
    slice iphoneos          arm64  -mios-version-min=13.0            build/ios-arm64
    slice iphonesimulator   arm64  -mios-simulator-version-min=13.0  build/sim-arm64
    slice iphonesimulator   x86_64 -mios-simulator-version-min=13.0  build/sim-x86_64
    lipo -create build/sim-arm64/libais.a build/sim-x86_64/libais.a -output build/sim/libais.a

    # wrap both platforms into one xcframework, carrying the public header
    xcodebuild -create-xcframework \
      -library build/ios-arm64/libais.a -headers c/embed.h \
      -library build/sim/libais.a       -headers c/embed.h \
      -output build/libais.xcframework

Then drag `libais.xcframework` into the app target, add `embed.h` to the
bridging header, and call the C functions directly from Swift.

## 6. The Flutter app as a behavioral reference

Not a dependency, just the reference for the non-obvious behavior to replicate:

    app/flutter/lib/main.dart          _genToken (128-bit hex), _lanIp (prefer private range),
                                       _handleLink (parse + confirm), _syncBusy (one at a time),
                                       background-isolate calls, the barrier dialog while a sync blocks
    app/flutter/lib/ais_ffi.dart       how the FFI seam marshals the same embed.h calls
    app/flutter/ios/Runner/            Info.plist (scheme + local network), SceneDelegate (deep link)

Read those for the exact success/failure messaging and the concurrency
discipline; everything else you get for free from `libais`.
