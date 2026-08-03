# AGENTS.md -- how to develop AIS (for humans and AI agents)

AIS is a plain-text associative index in C99. This is the operating manual for
working on it. Read it, then `doc/dev/STYLE.md` and `doc/dev/LAYOUT.md`.

## The contract (read first)

- **`doc/dev/STYLE.md`** -- coding ideology: stack/streaming (avoid the heap), one
  concept per `.c/.h`, error handling, idioms, lineage. Non-negotiable. Before adding
  any `malloc` to the core, check it against STYLE.md's sanctioned-heap list ("The only
  heap the core sanctions"); the record path allocates nothing.
- **`doc/dev/LAYOUT.md`** -- on-disk format, module map, algorithms, CLI, build order.
- **`doc/dev/LOCKING.md`** -- reader/writer lock model and `next_id` correctness.
- **`doc/dev/FORMAT_V2.md`** -- the decided `--dump`/`--import` grammar
  (`KEY... -v VALUE`), why ids leave that surface and stay everywhere else, and
  the order of work. Read before touching feed.c's parsing or ais_dump.
- **`c/ais.h`** -- the public API. The engine implements it; the tests test it.

These four are the contract. Do not change behavior without changing them first.

Not the contract, but read it before writing any GUI test: **`doc/dev/GUI_TESTING.md`**
-- how to drive a front-end automatically (headless browser, Flutter by deep link
and keyboard, a real APK on an emulator). Every trap in it was paid for once
already.

## Build and test

    make        # build ./c/ais            (run from repo root; delegates to c/)
    make codeut # engine tests (c/tests.c, in-process) -- the fast inner loop
    make cliut  # CLI black-box (tests/cli.sh: the binary through the shell)
    make uiut   # web GUI (tests/gui: --serve HTTP api + page in headless Chrome) -- SKIPs absent
    make ut     # EVERYTHING: codeut + cliut + uiut + wrappers, each PASS/FAIL/SKIP -- run before commit
    make codeut-asan / codeut-ubsan   # the engine tests under AddressSanitizer / UBSan
    make hooks  # enable the pre-push hook (runs codeut-asan + codeut-ubsan before a push)
    make clean

`make ut` runs two groups: CORE (codeut + cliut + the FFI stack budget -- keep
green, the commit gate) and GUI (uiut + the wrapper build-checks + the native
Flutter sync UI; a layer whose toolchain is absent SKIPs).
A green CORE with a red or skipped GUI is fine to commit. Full layout in `tests/README.md`.

Two layers exist because something broke without anything noticing:

- **`tests/stack/`** measures how much STACK the engine needs at the FFI seam,
  which is a Dart isolate thread of about 512 KB, not a process's 8 MB. A change
  once doubled the primary save path's frame and every test stayed green; on a
  phone that is the app dying when the user saves a note. Do not measure this
  with `ulimit -s` on the CLI -- `main()`'s own frame is ~141 KB the app never
  pays, so it overstates the need by a third.
- **`tests/gui/flutter-sync.sh`** drives the real Host/Join UI against a CLI
  peer. The harness existed but was wired into nothing, so when the Sync control
  moved into the overflow menu it silently stopped testing sync at all, for
  weeks, while the merge code underneath was being rewritten. It opens the sheet
  by keyboard (Ctrl+Shift+S) precisely so a layout change cannot quietly
  disconnect it again. It needs the Linux desktop toolchain (clang, ninja,
  libgtk-3-dev), which cannot be installed on Pop!_OS without downgrading the
  running desktop's Wayland libraries -- so there it SKIPs, and the Android
  layer below is the one that actually runs.
- **`tests/gui/flutter-sync-android.sh`** does the same job against the SHIPPED
  artifact: it builds the debug APK for the device's ABI, hands the app an
  `ais://` pairing link (the scan-to-pair path), taps through the prefilled Join
  dialog, and asserts records cross in BOTH directions against a CLI peer on the
  host (the emulator reaches it at 10.0.2.2). Assertions read the app's private
  index back with `run-as`. It SKIPs unless a device is attached; set
  `AIS_ANDROID_BOOT=1` to have it boot an AVD headlessly, and
  `AIS_ANDROID_CLEAR=1` for a clean-room run (that DELETES the emulator's index,
  hence opt-in). Match the APK's `--target-platform` to the device ABI or the app
  dies on launch with a missing `libflutter.so`, which reads like a product bug.

Before tagging a release, run `make codeut-asan` and `make codeut-ubsan`: they
rebuild the engine tests with the compiler's sanitizers so memory errors (overflow,
use-after-free) and undefined behavior abort with a file:line report instead of
passing silently under `-O2`. You do not have to remember: `.github/workflows/
sanitizers.yml` runs both on Linux and macOS on every push, and `make hooks`
installs a pre-push hook that runs them locally first (bypass once with
`git push --no-verify`). Keep them out of the default build -- they are ~2-3x
slower and not universally available, so `make` / `make ut` stay portable.

To SEE the web GUI (a C string `PAGE[]` in `c/serve.c`), screenshot it rather
than guess at layout (`AIS_NO_OPEN=1` keeps `--serve` from opening a browser):

    c/ais -f /tmp/x --init && AIS_NO_OPEN=1 c/ais -f /tmp/x --serve 8080 &
    tests/shot/shot.sh http://127.0.0.1:8080/ /tmp/gui.png   # then open the PNG

Always run ais against a `/tmp` or personal `~/.ais` index, never the repo's own.
See `tests/shot/README.md`.

**Never open a window on the real display.** Anything that can show a window --
the Flutter desktop build, the win32 GUI under wine, a browser -- runs on a
virtual X server. A shell here inherits `DISPLAY=:0` and `WAYLAND_DISPLAY`, each
command is a fresh shell so exports do not persist, and GTK prefers Wayland, so
overriding `DISPLAY` alone still lands on the developer's screen. Neutralise both,
inline, every time:

    env -u WAYLAND_DISPLAY -u XDG_SESSION_TYPE GDK_BACKEND=x11 DISPLAY=:99 \
        xvfb-run -a flutter run -d linux

Browsers take `--headless=new --ozone-platform=headless` instead, and the Android
emulator takes `-no-window` (no X server at all); `make uiut` already does this.
If a check cannot run headless, say so rather than falling back to a real
display. Recipes for all three are in `doc/dev/GUI_TESTING.md`.

The text store is the source of truth; the index (`idx/`, `tomb`, `next_id`) is
rebuildable from it and disposable. That is not a slogan: `tests/cli.sh` deletes
`idx/` and `off` outright, compacts, and asserts every record still recalls.

Two identity rules the whole engine rests on, both now pinned by tests:

- **A value names ONE record.** Every write path enforces it -- `put` resolves an
  existing record by value, `ais_merge_addval` by content hash, `ais_set_value`
  and `ais_add` refuse a value another record holds. Two records sharing a value
  make a peer collapse them, and a later delete of either takes both.
- **An id never crosses a device boundary.** It is a local ordinal: the sort key
  that makes posting lists mergeable and the recency order. Cross-device identity
  is `content_hash` over the value, and the export stream carries no id field at
  all. `tests/cli.sh` asserts that structurally, so adding one goes red.

## The development loop (test-driven)

Tests are the objective gate. Never trust output you have not verified.

1. **Lock the contract.** If the change needs new behavior, update
   `ais.h` / `doc/dev/LAYOUT.md` / `doc/dev/STYLE.md` first, so there is one agreed spec.
2. **Implement** against the contract, in `STYLE.md`'s idiom (one concept per
   file, modules return codes, only the CLI `die()`s; the rationale is there).
3. **Test.** Add or extend tests in `c/tests.c` -- linear, inline, ONE comment per
   test saying what it checks. Cover the new behavior and its edges.
4. **Verify.** `make codeut` green; no warnings under `-std=c99 -Wall -Wextra` (a
   warning is a defect, per `STYLE.md`).

Red -> green -> refactor. Every change keeps the whole suite green (regression).

## Working with AI agents (native orchestration, no plugins)

Developed with Claude Code's built-in orchestration -- nothing to install:

- Spawn focused **subagents** for independent work (explore the legacy code,
  implement a module, write tests). A separate **tester** agent with a fresh
  context writing the tests is preferred: independent eyes catch the
  implementer's assumptions.
- The **integrator** (the main session) locks the contract and runs `make ut`.
- For a large structured job, a deterministic multi-agent workflow can fan out;
  for ordinary work, one subagent plus the test gate is enough.
- A `PostToolUse` hook auto-runs `make codeut` after edits to `c/` (see `.claude/`).

Keep orchestration minimal: the model + native subagents + the test gate. Resist
building agent infrastructure that itself needs maintaining.

## Layout

    c/         the engine (C99): key store post merge compact ais embed secret
               doc find stats locate serve + main.c/feed.c (CLI) + tests.c;
               off/multi/tomb/version are store files, not modules
    c/crypto/  the secret-store encryption module (ais_crypto + WHY/README)
    c/attic/   the pre-rewrite v0 prototype -- reference only, not built
    doc/       about.txt, OVERVIEW.md, foundation.md, ROADMAP.md, migration.txt,
               performance.txt, limitations.txt, USING.txt                       (public)
    doc/dev/   STYLE, LAYOUT, BNF, LOCKING, WHY-PLAIN-TEXT, WHY-C, DISTRIBUTION, SIGNING,
               GUI, README, and the sync docs (SYNC, SYNC_PROTOCOL, MERGE) (developers)
    tests/     the committed fixture (tests/INDEX/store)
    tests/shot/ screenshot the --serve GUI to a PNG so an agent can see its frontend change
    gui/       desktop launchers (ais-web.{desktop,command,bat}) that start --serve
    win32/     the native Windows GUI (ais-gui.c)
    app/       the Flutter mobile app and the PWA front-end (over the embed FFI seam)
    legacy/    the 2005 shell + 2009 Java originals
