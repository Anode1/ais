---
name: ais
description: Recall or store short references (links, paths, notes, facts, commands) in a local AIS plain-text index by keyword, using key intersection (AND) or union (OR). Use when the user wants to retrieve something they filed under their own keys, or to save something for later keyword recall. The keys are the user's own; do not assume any particular naming.
---

# AIS: recall and store by keyword

AIS is a local, plain-text associative index. You file references (a URL, a path, a version, a short note, a command) under one or more keys, and recall them by those keys with set algebra. Retrieval is a deterministic lookup and intersection on the user's own machine: exact, private, no cloud round-trip, and the model spends no tokens searching (it receives only the extracted slice).

## When to use
- The user wants to retrieve something they previously filed, named by keyword(s) rather than by location: a link, a path, a note, a fact, a command, anything in their index. Recall is by association, not by folder, and the keys are the user's own (and their project's), never a fixed namespace.
- The user wants to save a short reference for later keyword recall ("save this under ...", "keep this as ...").

Do not assume which keys exist. If unsure, list them first (see Conventions); the vocabulary belongs to the user, not to this skill.

## Prerequisites
- The `ais` binary on PATH (build from https://github.com/Anode1/ais).
- An index: AIS uses the nearest `.ais/` directory (git-style), else `~/.ais`. If none exists, `ais --init` creates one in the current directory.

## Recall
- AND (records under ALL keys): `ais <key> <key> ...`   e.g. `ais <keyA> <keyB>`
- OR (records under ANY key): `ais -o <key> <key> ...`
- Substring search of values and paths: `ais --find <text>`

Output is the matching records (id and value). Use the values directly; mention the id when the user might want to update or delete a record.

## Store
- `ais -v "<value>" <key> <key> ...`   e.g. `ais -v "https://example.org/page" <keyA> <keyB>`
- The value is a reference: a URL, a path, a short note, or a command. Keep values short. AIS indexes references, not documents.

## Update and delete (by id)
- Attach a key: `ais --update <id> <key>`
- Detach a key: `ais --update <id> -- -<key>`
- Delete a record: `ais --del <id>`

## Conventions
- Prefer keys that already exist. Before inventing a new key, run `ais --keys` (or `ais --tags` for usage counts) to see the keys in use, so the index stays consistent.
- Recall matches keys exactly. If a recall returns nothing, try `-o` (union), `--find` (substring), or a broader key.
- Do not dump large content into the index; store a path or URL to it instead.

## Notes
- Everything stays local and private; nothing leaves the machine.
- `ais --serve` opens the same index in a browser if the user prefers a GUI.
- `ais --help` lists every command.
