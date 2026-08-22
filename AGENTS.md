# AGENTS.md -- how to develop AIS (for humans and AI agents)

AIS is a plain-text associative index in C99. This is the operating manual for
working on it. Read it, then `doc/dev/STYLE.md` and `doc/dev/LAYOUT.md`.

**Picking up work?** `doc/ROADMAP.md`'s "Known gaps" is the current list: what is
unfinished in the last release, what has never been verified on real hardware,
which test cannot see, and which defects are knowingly unfixed and why.

## The contract (read first)

- **`doc/dev/STYLE.md`** -- coding ideology: stack/streaming (avoid the heap), one
  concept per `.c/.h`, error handling, idioms, lineage. Non-negotiable. Before adding
  any `malloc` to the core, check it against STYLE.md's sanctioned-heap list ("The only
  heap the core sanctions"); the record path allocates nothing.
- **`doc/dev/LAYOUT.md`** -- on-disk format, module map, algorithms, CLI, build order.
- **`doc/dev/PROSE.md`** -- how the documents are written: what the bold means, what
  the titles may say, and which of the `X, not Y` contrasts are load-bearing.
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

`make ut` runs two groups: CORE (codeut + cliut + the FFI stack budget, the
commit gate) and GUI (uiut + the wrapper build-checks + the native Flutter sync
UI; a layer whose toolchain is absent SKIPs). A green CORE with a red or skipped
GUI is fine to commit. Every layer, what it covers and how to run it alone:
`tests/README.md`.

Two of those layers exist because something broke without anything noticing, and
both lessons generalise:

- **`tests/stack/`** measures how much STACK the engine needs at the FFI seam,
  which is a Dart isolate thread of about 512 KB, not a process's 8 MB. A change
  once doubled the primary save path's frame and every test stayed green; on a
  phone that is the app dying when the user saves a note. Do not measure this
  with `ulimit -s` on the CLI: `main()`'s own frame is ~141 KB the app never
  pays, so it overstates the need by a third.
- **`tests/gui/flutter-sync.sh`** drives the real Host/Join UI. The harness
  existed but was wired into nothing, so when the Sync control moved into the
  overflow menu it silently stopped testing sync at all, for weeks, while the
  merge code underneath was being rewritten. Wire a new layer into
  `tests/run.sh` the day you write it.

Before tagging a release, run `make codeut-asan` and `make codeut-ubsan`: they
rebuild the engine tests under the compiler's sanitizers, so memory errors and
undefined behavior abort with a file:line report instead of passing silently
under `-O2`. `sanitizers.yml` runs both on every push and `make hooks` installs a
pre-push hook that runs them first (bypass once with `git push --no-verify`).
They stay out of the default build: 2-3x slower and not universally available, so
`make` and `make ut` stay portable. The rest of the release procedure is in
`doc/dev/VERSIONING.md`.

**Never open a window on the real display.** Anything that can show one -- the
Flutter desktop build, the win32 GUI under wine, a browser -- runs on a virtual X
server. A shell here inherits `DISPLAY=:0` and `WAYLAND_DISPLAY`, each command is
a fresh shell so exports do not persist, and GTK prefers Wayland, so overriding
`DISPLAY` alone still lands on the developer's screen. Neutralise both, inline,
every time:

    env -u WAYLAND_DISPLAY -u XDG_SESSION_TYPE GDK_BACKEND=x11 DISPLAY=:99 \
        xvfb-run -a flutter run -d linux

Browsers take `--headless=new --ozone-platform=headless` instead, and the Android
emulator takes `-no-window` (no X server at all); `make uiut` already does this.
If a check cannot run headless, say so rather than falling back to a real
display.

The web GUI is a C string (`PAGE[]` in `c/serve.c`), so an agent that has not
looked at it is guessing: render it to a PNG with `tests/shot/shot.sh` and open
the file. That recipe and every other way to drive a front-end are in
`doc/dev/GUI_TESTING.md`. Always run ais against a `/tmp` or personal `~/.ais`
index, never the repo's own.

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

## A claim is not only prose

**Makefile comments, header comments, source comments and the usage text the
binary prints are CLAIMS, and they go stale exactly like a README.** When you
change behaviour, they move with the code; when you audit, they are in scope.

This is not a style note, it is the single highest-yield instruction we have
measured. A defect where the Makefile promised to honour your `CFLAGS` and had
stopped doing so was found by 3 of 3 agents told this sentence and 0 of 19 agents
not told it (Fisher p = 0.0006). It also beat four-agent fleets that lacked the
sentence, at a quarter of the cost, so it is worth more than any team arrangement
tried alongside it. The full comparison is in `hsearch/probes/0010`.

Two real examples from this repository, both live at the time:

- `Makefile` said "Honors the standard variables (CC CFLAGS CPPFLAGS ...)" while
  `CFLAGS =` had replaced `CFLAGS ?=`, so an exported value was silently dropped.
- `man/ais.1` said `-k` forces "a key that begins with '-'" months after such a
  key started being refused outright. `help.c` had been corrected; the man page
  had not.

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

    c/         the engine (C99), one concept per file; the module map and the
               on-disk format are in doc/dev/LAYOUT.md
    c/crypto/  the secret-store encryption module (ais_crypto + WHY/README)
    c/attic/   the pre-rewrite v0 prototype -- reference only, not built
    doc/       the public docs; README.md's "Learn more" table is their index
    doc/dev/   the developer notes; doc/dev/README.md lists them and says which
               to read first
    tests/     the suite and the committed fixture (tests/INDEX/store); layers
               and conventions in tests/README.md
    gui/       the double-click launchers that start the web GUI
    win32/     the native Windows GUI (ais-gui.c)
    app/       the Flutter mobile app and the PWA front-end (over the embed FFI seam)
    legacy/    the 2005 shell + 2009 Java originals
