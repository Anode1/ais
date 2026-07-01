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
- **`c/ais.h`** -- the public API. The engine implements it; the tests test it.

These four are the contract. Do not change behavior without changing them first.

## Build and test

    make        # build ./c/ais            (run from repo root; delegates to c/)
    make codeut # engine tests (c/tests.c, in-process) -- the fast inner loop
    make cliut  # CLI black-box (tests/cli.sh: the binary through the shell)
    make uiut   # web GUI (tests/gui: --serve HTTP api + page in headless Chrome) -- SKIPs absent
    make ut     # EVERYTHING: codeut + cliut + uiut + wrappers, each PASS/FAIL/SKIP -- run before commit
    make codeut-asan / codeut-ubsan   # the engine tests under AddressSanitizer / UBSan
    make hooks  # enable the pre-push hook (runs codeut-asan + codeut-ubsan before a push)
    make clean

`make ut` runs two groups: CORE (codeut + cliut -- keep green, the commit gate)
and GUI (uiut + the wrapper build-checks; a layer whose toolchain is absent SKIPs).
A green CORE with a red or skipped GUI is fine to commit. Full layout in `tests/README.md`.

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

The text store is the source of truth; the index (`idx/`, `tomb`, `next_id`) is
rebuildable from it and disposable.

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
