# Tests

Two clearly separated groups, run by one suite so a green core stays committable
while a GUI is still in progress.

## Groups

**CORE -- the commit gate. Keep it green.**

| Layer | What it covers | Where |
|-------|----------------|-------|
| engine unit | the `ais.h` API directly: key/store/get/add/del, compact, find, timeline, crypto + secret marker | `c/tests.c` (`make ut`) |
| cli black-box | the real binary: streaming stdin (`-v -`), pipelines, argv, index discovery | `tests/cli.sh` |

**GUI -- the front-ends over the one engine. May lag; absent toolchain SKIPs.**

| Layer | What it covers | Where | Runs here? |
|-------|----------------|-------|-----------|
| serve http api | `ais --serve` endpoints incl. the encrypt save + reveal round-trip | `tests/gui/serve.sh` | yes (needs curl + crypto) |
| native windows ui | that `win32/ais-gui.c` still compiles against the engine | `tests/gui/windows.sh` | only with MinGW-w64 (CI); else SKIP |
| flutter app | `dart analyze` of `app/flutter` (FFI binding + widgets); `flutter test` if a `test/` dir exists | `tests/gui/flutter.sh` | analyze if Dart present; else SKIP |

## Running

    make ut       # engine unit tests only (fast inner loop)
    make check    # CORE: engine unit + cli black-box  -- the commit gate
    make suite    # CORE + GUI, every layer PASS / FAIL / SKIP, with a summary

Or a single GUI layer directly, e.g. `sh tests/gui/serve.sh ./c/ais`.

`make suite` prints two groups and a final line like:

    CORE: PASS    GUI: PASS    (skipped: 1)

It exits non-zero iff a **non-skipped** layer FAILS. The point of the split: a
green CORE with a red or skipped GUI is fine to commit -- the CLI/engine is the
contract; a GUI can be merged once its own layer goes green.

## Conventions

- **Throwaway index.** Tests run ais against a `mktemp -d` (or `/tmp`) index,
  never the repo's own `.ais` or a personal `~/.ais`.
- **Headless serve.** GUI/serve tests start the server with `AIS_NO_OPEN=1` so
  `--serve` does not pop a browser window.
- **SKIP = exit 77.** A layer whose toolchain is absent exits 77; the runner
  counts it as skipped, not failed. (Same convention as automake.)

## Adding a test

- engine behaviour -> a case in `c/tests.c` (`make ut`).
- binary / streaming / argv behaviour -> `tests/cli.sh`.
- a `--serve` endpoint -> `tests/gui/serve.sh`.
- a whole new front-end layer -> a `tests/gui/<name>.sh` (exit 0/1/77) and one
  `layer fail_gui ...` line in `tests/run.sh`.

## Not here yet: real GUI UI-driving

`serve.sh` exercises the HTTP backend, not clicks in a rendered page. For real
click-and-assert UI tests (or full-page screenshots), the home is **C under
`tests/`** -- a small Chrome DevTools Protocol client driven by the suite -- not
another runtime; see `tests/shot/README.md`. The viewport screenshot helper
(`tests/shot/shot.sh`, shell only) lets an agent SEE the GUI in the meantime.
