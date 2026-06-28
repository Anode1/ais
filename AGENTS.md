# AGENTS.md -- how to develop AIS (for humans and AI agents)

AIS is a plain-text associative index in C99. This is the operating manual for
working on it. Read it, then `doc/dev/STYLE.md` and `doc/dev/LAYOUT.md`.

## The contract (read first)

- **`doc/dev/STYLE.md`** -- coding ideology: stack/streaming (avoid the heap), one
  concept per `.c/.h`, error handling, idioms, lineage. Non-negotiable.
- **`doc/dev/LAYOUT.md`** -- on-disk format, module map, algorithms, CLI, build order.
- **`doc/dev/LOCKING.md`** -- reader/writer lock model and `next_id` correctness.
- **`c/ais.h`** -- the public API. The engine implements it; the tests test it.

These three are the contract. Do not change behavior without changing them first.

## Build and test

    make        # build ./c/ais            (run from repo root; delegates to c/)
    make ut     # engine unit tests (c/tests.c) -- the fast inner loop
    make check  # CORE: engine unit + CLI black-box (tests/cli.sh) -- the commit gate
    make suite  # CORE + GUI (--serve, native Windows, Flutter), each PASS/FAIL/SKIP
    make clean

Tests are two groups: CORE (engine + CLI -- keep green, it is the commit gate)
and GUI (the front-ends; a layer whose toolchain is absent SKIPs). A green CORE
with a red or skipped GUI is fine to commit. Full layout in `tests/README.md`.

To SEE the web GUI (it is a C string, `PAGE[]`, in `c/serve.c`), screenshot it
and look -- do not guess at layout. Start it headless on a throwaway index, then
capture (`AIS_NO_OPEN=1` is the documented switch that stops `--serve` opening a
browser window -- it exists for agents, screenshots, and CI):

    c/ais -f /tmp/x --init && AIS_NO_OPEN=1 c/ais -f /tmp/x --serve 8080 &
    tests/shot/shot.sh http://127.0.0.1:8080/ /tmp/gui.png   # then open the PNG

Always run ais against a `/tmp` index (or a personal `~/.ais`), never the repo's
own `.ais`. The screenshot helper is shell-only; if real CDP / click-and-assert
UI tests are ever wanted they go in C under `tests/` (a `make uiut`), not another
runtime. See `tests/shot/README.md`.

The text store is the source of truth; the index (`idx/`, `tomb`, `next_id`) is
rebuildable from it and disposable.

## The development loop (test-driven)

Tests are the objective gate. Never trust output you have not verified.

1. **Lock the contract.** If the change needs new behavior, update
   `ais.h` / `doc/dev/LAYOUT.md` / `doc/dev/STYLE.md` first, so there is one agreed spec.
2. **Implement** against the contract. One concept per file (see the module map).
   Engine modules return codes; only the CLI front-end (`main.c`, `feed.c`) calls
   `die()`.
3. **Test.** Add or extend tests in `c/tests.c` -- linear, inline, ONE comment per
   test saying what it checks. Cover the new behavior and its edges.
4. **Verify.** `make ut` green; no warnings under `-std=c99 -Wall -Wextra`.

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
- A `PostToolUse` hook auto-runs `make ut` after edits to `c/` (see `.claude/`).

Keep orchestration minimal: the model + native subagents + the test gate. Resist
building agent infrastructure that itself needs maintaining.

## Layout

    c/         the engine (C99): key store post merge compact ais log help feed
               + main.c (CLI) + tests.c
    c/attic/   the pre-rewrite v0 prototype -- reference only, not built
    doc/       about.txt, OVERVIEW.md, foundation.md, migration.txt              (public)
    doc/history/ first-interaction log + screenshot (priority record = Zenodo DOI, not committed)
    doc/dev/   STYLE.md, LAYOUT.md, LOCKING.md                                     (developers)
    tests/     the committed fixture (tests/INDEX/store)
    tests/shot/ screenshot the --serve GUI to a PNG so an agent can see its frontend change
    gui/       future reference GUI wrappers (thin callers of the CLI)
    legacy/    the 2005 shell + 2009 Java originals
