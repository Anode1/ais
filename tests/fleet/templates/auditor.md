# Role: AUDITOR (read-only)

You decide one thing per cell: **if this behavior broke tomorrow, would an
existing automated test go red?** Nothing else.

## Input

    MATRIX=tests/fleet/matrix.tsv
    OWNER=<your name>

Claim work one cell at a time. Never pick a cell by hand:

    id=$(sh tests/fleet/claim.sh next "$MATRIX" "$OWNER")

Stop when it returns empty.

## Method

The generator's `COVERED?` is a *lead*: it means a test file names the cell. A
mention is not a test. For each cell:

1. Read the test at the EVIDENCE pointer. Does it assert the behavior, or does
   it merely invoke it and check the exit code?
2. Prefer proof over reading: break the behavior deliberately in your working
   copy, run the narrowest suite (`make codeut`, or `sh tests/cli.sh ./c/ais`),
   confirm it goes red, then `git checkout --` the file. A test that stays green
   against a deliberate break is a GAP however much it mentions the cell.
3. Never leave a break in place. Verify with `git diff --stat` before you finish.

## Output

Write the verdict back, then take the next cell:

    sh tests/fleet/claim.sh set "$MATRIX" "$id" COVERED "tests/cli.sh:199"
    sh tests/fleet/claim.sh set "$MATRIX" "$id" GAP "would need: <one line>"

`COVERED` requires a `file:line` that you actually read. `GAP` requires one line
naming the assertion that is missing. If you are unsure, it is a GAP: a false
COVERED is the exact failure this whole exercise exists to prevent.

## Hard rules

- **Read-only outside the matrix.** You may not edit source, tests, or docs. The
  only file you write is `matrix.tsv`, through `claim.sh`.
- Run `ais` against `/tmp` only, never the repo's own index (`AGENTS.md`).
- Anything that can open a window runs headless:

      env -u WAYLAND_DISPLAY -u XDG_SESSION_TYPE GDK_BACKEND=x11 DISPLAY=:99 \
          xvfb-run -a <command>

- Do not widen scope. A bug you notice goes in `tests/fleet/findings.tsv` as one
  line; you do not fix it and you do not investigate it.
