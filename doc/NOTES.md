# Notes an agent writes: plain text, bullets, one fact in one place

**A person files a note the way they type it. An agent files a note in the shape below, so that a person reads it as easily as a machine, and a machine as easily as a person.** Plain text under `ais --doc`, never a Word document, JSON or XML.

## The shape

- One file per subject, filed under the subject's keys (`ais --doc claude camper rv < camper-notes.md`). A new fact on the subject goes into that file, never into a second one.
- Markdown headings for the subject's parts, `-` bullets under them, two levels at most, nothing between bullets.
- One fact, one place. Say it once; a second mention is a pointer ("see 7-pin cord"), not a restatement.
- A claim carries its number, its date and its id: "500 A monitor, Amazon.ca B07RP5B5P7, 93.45 on 2026-10-21", not "the monitor is cheaper on Amazon now".
- A decision is stated as one, dated: "Decision 2026-10-21: keep the stock battery". What was rejected stays, in one clause, with why, so the next session does not re-open it.
- Units, codes and part numbers as printed on the label; page numbers of the manual they came from. These are the handles a later reader recalls by.
- A to-do list is its own section ("Open checks"), one bullet per check, the answer written into the bullet when it comes.
- No prose padding: no "it is worth noting", no restating the question, no sentence whose only job is to connect two bullets.

## The approach, when rewriting what exists

1. Back up the file.
2. Read all of it. Every fact, number, id and rejected alternative is an item; nothing is dropped because it looks minor.
3. Group by subject into headings. Within a heading, order by use: what is owned, what is decided, what is picked and its price, what was rejected, what is open.
4. Turn each sentence into a bullet that carries its fact without the sentence. Two facts in one sentence become two bullets or one bullet with a semicolon-free list.
5. Where a fact appears twice, keep the place where it is used and point from the other.
6. Re-file: `ais <keys>` for the id, `ais --del ID -y`, `ais --doc <keys> < file`. The index holds a snapshot; the file is the source.

## Why

One file rewritten this way on 2026-10-22, the same facts before and after:

| | prose | bullets |
|---|---|---|
| words | 4,818 | 3,897 |
| bytes | 27,370 | 22,582 |
| items lost | | 0 |

What went was the connective tissue a narrator adds and a reader skims. A model skims it too and pays for every token, on every read; a phone syncing the file carries it on every change. Bullets are also what a person edits on a phone without breaking anything, what `diff` and `grep` see line by line, and what any model reads in any year with nothing to parse.

JSON and XML fail from the other side: a person cannot edit them on a phone without breaking them, a field name is a schema that needs a document of its own, and a quoted string carries a fact no better than a line does. The bullet is the unit a human writes and a model reads; both ends already agree on it.

## What goes elsewhere

- A fact that only this session needs: nowhere.
- A secret: `ais KEY -e`, encrypted inline, never in the note.
- A document someone else wrote (a manual, a datasheet): keep the file where it is, put its path in the note with the page numbers that matter.
- A checklist for a bag or a kit: its own short file, item names only, no prices and no steps.
