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
| web interact (uiut) | click-and-assert: type a query, press Enter, assert the seeded record renders -- driven by a C CDP client | `tests/gui/inter.sh` (+ `cdp.c`, `cdptest.c`) | yes (needs Chrome + cc); else SKIP |
| native windows ui | that `win32/ais-gui.c` still compiles against the engine | `tests/gui/windows.sh` | only with MinGW-w64 (CI); else SKIP |
| flutter app | `dart analyze` of `app/flutter` (FFI binding + widgets); `flutter test` if a `test/` dir exists | `tests/gui/flutter.sh` | analyze if Dart present; else SKIP |

## Running

    make codeut   # engine tests only (fast inner loop)
    make cliut    # CLI black-box only (the binary through the shell)
    make uiut     # web GUI: --serve HTTP api + rendered page + click-and-assert (SKIPs absent)
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
- a `--serve` *interaction* (type, press a key, assert the result) -> a case in
  `tests/gui/cdptest.c`, driven by the C CDP client (`make uiut`).
- a whole new front-end layer -> a `tests/gui/<name>.sh` (exit 0/1/77) and one
  `layer fail_gui ...` line in `tests/run.sh`.

## Click-and-assert: the C CDP client

`tests/gui/cdp.c` is a minimal Chrome DevTools Protocol client in C99 with no
dependencies: it speaks CDP straight to headless Chrome over a WebSocket (the
same wire protocol Puppeteer/Playwright use), with no chromedriver and no library
beyond libc + POSIX sockets. `cdptest.c` uses it to drive the live `--serve` page
(navigate, focus, type, press Enter) and assert the seeded record renders -- the
real input+fetch path, which `ui.sh` (static `--dump-dom`) cannot reach.
`inter.sh` compiles both with `cc`, starts the server and a headless Chrome with
`--remote-debugging-port`, and runs the driver; it SKIPs (77) if `cc` or Chrome
is absent. Set `AIS_CDP_DEBUG=1` to trace the CDP frames.

Not covered: visual/screenshot diffing and cross-browser (Firefox/Safari). For a
still image of the GUI, `tests/shot/shot.sh` (shell only) lets an agent SEE it.
