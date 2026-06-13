# WASM PWA -- AIS in the browser, on every device

Goal: a **self-contained** AIS that installs from a URL on iPhone, Android, and
any desktop browser -- no app stores, no Apple Developer account, no
`cygwin1.dll`, no GPL/App-Store conflict. The index lives in the browser's own
storage on the device, which fits the project ethos exactly: *your memory, yours
to keep -- no server.*

Today's `app/` PWA is only a thin client to a local `ais --serve` (`/api/...`),
so it is a desktop convenience, not a standalone phone app. This track makes it
stand alone by running the C engine **in the browser as WebAssembly** and keeping
the store in browser storage.

## Architecture

```
  app/index.html  (UI, unchanged look)
        |
        |  (no fetch /api in standalone mode)
        v
  app/engine/ais.js + ais.wasm   <-- emcc build of the FFI seam (embed.c) + engine
        |  ais_embed_open/store/recall/timeline/tags  (same API as native FFI)
        v
  emscripten FS  ->  IDBFS (IndexedDB)   <-- the store/idx/blobs persist on-device
```

The same `embed.h` API the Flutter app uses over FFI is what the browser calls
over WASM -- one engine, three front-ends (CLI, native FFI, WASM).

## Curated WASM sources

Engine core + FFI seam only; **exclude** `main.c` (CLI), `serve.c` (sockets),
`feed.c` (nftw/tty -- not needed for store/recall), `help.c`, `tests.c`, `win.c`:

    embed.c ais.c store.c post.c key.c merge.c compact.c stats.c find.c doc.c locate.c log.c

This avoids every hard-to-port POSIX call (sockets, nftw, realpath, /dev/tty).
The one shim likely needed is `flock` -> no-op (a browser origin is
single-threaded, so the writer lock is moot); confirm on the first emcc run.

## Milestones

1. **Engine -> WASM (build only).** `make wasm` (emcc) emits `app/engine/ais.js`
   + `ais.wasm` exporting the `ais_embed_*` functions. CI: `wasm-pwa.yml`
   (emsdk + `make wasm`), dispatch-only, uploads the artifact. *(scaffolded now)*
2. **Persistent storage.** Mount IDBFS at the index dir; `FS.syncfs` after each
   write so the store survives reloads. (OPFS is the faster future option.)
3. **JS data layer.** In `app/`, switch the data calls: if the WASM module is
   present, call `ccall('ais_embed_recall', ...)` etc.; else fall back to the
   existing `fetch('/api/...')` (so the same page works both standalone and
   against a local `ais --serve`). UI unchanged.
4. **Deploy.** GitHub Pages publishes `app/` (with the built engine). The Pages
   URL is the install point: open on the phone -> "Add to Home Screen".

Milestones 2-4 build on 1; do them in order, verifying each on CI / a real phone.

## Why this over native apps

| | WASM PWA | Android native | iOS native |
|---|---|---|---|
| iPhone | yes (Safari install) | -- | yes |
| Android | yes | yes (.apk/F-Droid) | -- |
| Desktop browsers | yes | -- | -- |
| App store / account | none | optional | required ($99/yr) |
| GPL friction | none | none | App-Store conflict |

The native Flutter apps remain a parallel option (richer OS integration, native
voice); the WASM PWA is the broadest, lowest-friction reach and the best brand fit.
