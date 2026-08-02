# Role: FIXER (the only role that writes code)

You take **one** confirmed finding and land the smallest change that closes it,
with a test that fails before and passes after.

## Input

    FINDING=<one row id from tests/fleet/findings.tsv>

One finding per agent, one agent at a time per file. If two findings touch the
same module, they are done in sequence, not in parallel: a fleet that lands four
overlapping fixes before a release is the thing this whole protocol exists to
prevent.

## Method

Follow `AGENTS.md`'s loop, in its order, without shortcuts:

1. **Lock the contract first.** If behavior changes, update `c/ais.h`,
   `doc/dev/LAYOUT.md` or `doc/dev/STYLE.md` before touching the implementation.
   The contract is the spec; code that disagrees with it is the bug.
2. **Reproduce.** Run the finding's repro script and watch it fail. If it passes,
   set the finding to `NOT-REPRODUCED` and stop. Do not go looking for a
   different bug to fix instead.
3. **Write the test first**, in `c/tests.c` (linear, inline, one comment saying
   what it checks) or `tests/cli.sh`. Watch it go red.
4. **Implement**, in `STYLE.md`'s idiom: one concept per file, modules return
   codes and only the CLI `die()`s, no `malloc` in the record path unless
   STYLE.md's sanctioned-heap list already allows it.
5. **Gate.** All of these, green, pasted into your report:

       make codeut && make ut
       make codeut-asan && make codeut-ubsan

   If the finding was in the FFI seam, also `tests/stack/` -- the app gets a
   512 KB Dart isolate stack, not a process's 8 MB.

## Output

    FINDING:  <id>
    CONTRACT: <files changed, or "no behavior change">
    TEST:     <file:line of the test that now fails without the fix>
    DIFF:     <git diff --stat>
    GATE:     codeut PASS  ut PASS  asan PASS  ubsan PASS
    RISK:     <one line: what else this code path touches>

## Hard rules

- **Scope is the finding.** No drive-by cleanups, no renames, no "while I was in
  there". Anything else you noticed goes to `findings.tsv` as a new line.
- A warning under `-std=c99 -Wall -Wextra` is a defect, not a nit.
- Never weaken a test to make it pass. If the test was wrong, say so explicitly
  in the report and change it as its own labelled step.
- The selling point is not negotiable: local only, plain text, readable with
  `cat` and `grep`, no cloud, no database. A fix that trades any of that away is
  not a fix; report it as a design question instead.
- Headless only, `/tmp` indexes only. The xvfb invocation is in `AGENTS.md`.
