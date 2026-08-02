# tests/fleet -- running more than one agent without opening gaps

The premise: a fleet does not fail by being too slow, it fails by *believing it
covered something*. So the shared state here is a matrix generated from the code,
where a gap is an empty cell you can count, rather than a chat log where a gap is
invisible.

Nothing here is a framework. It is one generator, one lock, and three prompts.

## The loop

    sh tests/fleet/matrix.sh > tests/fleet/matrix.tsv     # cells from the code
    sh tests/fleet/claim.sh stat tests/fleet/matrix.tsv   # where you stand

Then run N agents on `templates/auditor.md`, each pulling cells with
`claim.sh next`. Then N on `templates/adversary.md`, HIGH risk first. You triage
`findings.tsv`. Only then do fixers run, one finding each, serialized per module.

## Why the phases are separate

The find phases are **read-only**. An agent that cannot write cannot regress the
release. That is the whole reason the split exists, and it is not a style
preference: the week before a closed test is the worst possible moment to let
nine agents edit C and Dart in parallel.

## Where the cells come from

    cli      the getopt_long table in c/main.c        (the parser, not the help text)
    web      the strcmp(path, "/api/...") chain in c/serve.c
    android  every visible label and keyboard shortcut in app/flutter/lib/*.dart
    doc      verbs help.c promises that the parser does not accept

The partition is mechanical on purpose. If a manager (human or agent) invents the
split, the split will look plausible and have holes in it. Creativity belongs
inside a cell, in how you attack it, never in deciding which cells exist.

`COVERED?` from the generator means only that a test file *names* the cell. A
mention is not a test; that is what the auditor is for.

## Termination

Not a clock. Stop when `claim.sh stat` shows no open HIGH cells, and two
consecutive adversary rounds add nothing new to `findings.tsv`. Keep a token cap
as a runaway backstop, not as the goal.

## Status values

    GAP? COVERED?   the generator's guess, unverified
    GAP COVERED     an auditor verified it, with a file:line or a missing assertion
    DRIFT           the docs and the parser disagree
    CLAIMED         someone holds it (the OWNER column)
