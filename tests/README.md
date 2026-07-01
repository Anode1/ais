# Tests

`make ut` runs everything, in two groups so a green core stays committable while a
GUI is still in progress. The layers also have their own targets (`codeut`, `cliut`,
`uiut`) for a fast, isolated run.

## Groups

**CORE -- the commit gate. Keep it green.**

| Layer | What it covers | Where |
|-------|----------------|-------|
| engine (codeut) | the `ais.h` API directly: key/store/get/add/del, compact, find, timeline, crypto + secret marker | `c/tests.c` (`make codeut`) |
| cli (cliut) | the real binary: streaming stdin (`-v -`), pipelines, argv, index discovery | `tests/cli.sh` (`make cliut`) |

**GUI -- the front-ends over the one engine. May lag; absent toolchain SKIPs.**

| Layer | What it covers | Where | Runs here? |
|-------|----------------|-------|-----------|
| web api (uiut) | `ais --serve` endpoints incl. the encrypt save + reveal round-trip | `tests/gui/serve.sh` | yes (needs curl + crypto) |
| web render (uiut) | the `--serve` page in headless Chrome: it loads and its controls exist by id (post-JS DOM) | `tests/gui/ui.sh` | yes (needs Chrome); else SKIP |
| native windows ui | that `win32/ais-gui.c` still compiles against the engine | `tests/gui/windows.sh` | only with MinGW-w64 (CI); else SKIP |
| flutter app | `dart analyze` of `app/flutter` (FFI binding + widgets); `flutter test` if a `test/` dir exists | `tests/gui/flutter.sh` | analyze if Dart present; else SKIP |

## Running

    make codeut   # engine tests only (fast inner loop)
    make cliut    # CLI black-box only (the binary through the shell)
    make uiut     # web GUI only: --serve HTTP api + page in headless Chrome (SKIPs absent)
    make ut       # EVERYTHING, every layer PASS / FAIL / SKIP, with a summary -- run before commit

Or a single GUI layer directly, e.g. `sh tests/gui/serve.sh ./c/ais`.

`make ut` prints two groups and a final line like:

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

- engine behaviour -> a case in `c/tests.c` (`make codeut`).
- binary / streaming / argv behaviour -> `tests/cli.sh` (`make cliut`).
- a `--serve` endpoint or page element -> `tests/gui/serve.sh` / `tests/gui/ui.sh` (`make uiut`).
- a whole new front-end layer -> a `tests/gui/<name>.sh` (exit 0/1/77) and one
  `layer fail_gui ...` line in `tests/run.sh`.

## Not here yet: click-and-assert UI

`uiut` covers the HTTP backend (`serve.sh`) and that the page renders with its
controls (`ui.sh`, headless Chrome `--dump-dom`), but not *interaction* -- typing a
query and asserting the result renders, or revealing a secret in-browser. Real
click-and-assert needs a small Chrome DevTools Protocol client driven from C under
`tests/`, not another runtime; see `tests/shot/README.md`. The screenshot helper
(`tests/shot/shot.sh`, shell only) lets an agent SEE the GUI in the meantime.
