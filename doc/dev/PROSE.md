# How the documents are written

`STYLE.md` binds the C. This file binds the prose: the README, `PRIVACY.md`, the
`doc/` notes and the `WHY-*.md` arguments.

It exists because fifty-odd markdown files had settled into one voice, and a
reader who opens three of them in a row hears the same narrator each time. The
engine documents and the pitch should not sound alike.

## Bold a term, not a sentence

`doc/dev/LAYOUT.md` and `doc/dev/MERGE.md` state invariants as a bolded rule
followed by its explanation:

    **A VALUE NAMES ONE RECORD.** This is the engine's identity rule ...
    **A record may have NO keys.** `untag KEY` leaves one behind ...

That is a rule list and it stays. What had grown alongside it was an ordinary
topic sentence wearing the same bold:

    before   **It is never exported, and that is the whole point of keeping it separate.**
    after    It is never exported, and that is the whole point of keeping it separate.

If the bolded text is a named rule or a term being defined, keep it. If it is
just the first sentence of the paragraph, it is a sentence.

## Titles name

The `WHY-*.md` files had all converged on one shape:

    before   # Why C, and why it is safe enough
    before   # Why plain text, and why it is fast
    after    # Why C
    after    # Why plain text

Likewise `## The data is bounded by a human life`, not `..., not by Moore's
law`; `## What is new here`, not `..., and what is not`. The second clause is
the argument, and the argument goes in the body.

## Cut the contrast

`X, not Y` was the strongest habit in this tree. State the thing once. A
negation stays only where the boundary is the information the reader needs: a
Gatekeeper warning is a new-and-unsigned notice, not a malware finding; a wrong
key returns nothing rather than something plausible; a fingerprint is a fast
checksum, not a cryptographic one. Even then, prefer its own sentence over a
balanced tail. Cut every half that only flatters the first half: an index of
pointers *and not another silo to fill*, or `measured, not asserted` in front of
a table that is visibly a measurement, or a bolded rule name that carries its
rejected alternative when the body already explains it.

## One home per fact

The README's agent numbers are all reproduced by
`experiment/analyze.py --csv results_repeats_sanitized.csv`, and they match it
to the digit. That is the standard. A number that no command regenerates should
say where it came from and when, or not be there.

## Registers differ

The README sells. `doc/USING.txt` instructs. `doc/dev/LAYOUT.md`,
`MERGE.md` and `SYNC_PROTOCOL.md` are references and should be flatter and
duller than either. `PRIVACY.md` is a legal statement and wants no voice at all.

## Sections stop when the information stops

No closing maxim. If the last sentence of a section generalises rather than
informing, delete it.
