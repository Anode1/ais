# The `--serve` HTTP API

`ais --serve [PORT]` puts the engine behind a small HTTP API on loopback and
serves a page that drives it. **Two** front ends use this API and must stay in
step (see `GUI.md`): the page embedded in `c/serve.c` as the `PAGE[]` string
literal, and `app/index.html` (the installable PWA), served when `$AIS_WEB`
points at the `app/` directory.

Everything is loopback-only, single user, no accounts. A `POST` to `/api/*` from
a cross-site context is refused; see the `cross_site` check at the top of
`handle()` in `c/serve.c`.

Bodies and replies are plain text, never JSON: one record per line, the same
shapes the CLI prints. A GUI that can be debugged with `curl` is the point.

## Retrieval

| Method | Path | Answers |
| --- | --- | --- |
| GET | `/api/get?keys=A+B[&or=1][&count=N][&after=ID]` | `id\|value` per line, one line per LINK |
| GET | `/api/timeline?count=N[&before=ID][&from=D][&to=D]` | `id\|ts\|keys\|value`, newest first |
| GET | `/api/tags[?count=N&afterc=C&afterk=K]` | `count\|key`, busiest first |
| GET | `/api/keys?id=N` | the visible tags of one record |
| GET | `/api/stats` | `records:` / `keys:` / `deleted:`, as `ais --stats` |
| GET | `/api/where` | the index directory |
| GET | `/api/version` | `engine:` and on-disk `format:`; see `VERSIONING.md` |

`/api/get` emits **one line per link**, and a record may hold several. A GUI that
counts lines counts links, not records — group by the id before showing a count,
especially in anything destructive. Both front ends learned this the hard way.

## Writing

| Method | Path | Does |
| --- | --- | --- |
| POST | `/api/put?keys=A+B[&enc=1]` | save; body is the value, or `passphrase\nvalue` when `enc=1` |
| POST | `/api/update?id=N&keys=A+-B` | attach/detach tags (a `-` prefix detaches) |
| POST | `/api/setvalue?id=N` | replace one value in place; body is `oldvalue\nnewvalue` |
| POST | `/api/del?id=N` | delete one record |
| POST | `/api/reveal` | body is `passphrase\nmarked-value`; replies with the cleartext, or empty |

There is deliberately no `/api/add`: the CLI's `--add ID -v VALUE` (a second link
on one record) has no HTTP equivalent, so a multi-link record can be READ from a
GUI but never created or extended there. Acceptance testing flagged this as a real
gap, not a design choice — noted here so it is not mistaken for one.

A save that stores nothing answers **500**, not 200 with a count of zero. An
encrypted save on a build with no crypto module used to reply `saved 0
record(s)` with a 200, so both pages closed the sheet as though the secret had
been stored — the one case where believing a success message loses exactly the
thing you were protecting.

## Tag-level operations

| Method | Path | Does |
| --- | --- | --- |
| POST | `/api/untag?keys=KEY` | remove the tag, KEEP every record |
| POST | `/api/del-under?keys=KEY` | DELETE every record filed under the tag |

Both take **one** tag, not the whitespace-separated list every other endpoint
takes, and refuse a value containing whitespace with a 400. Folding `a b` to
`a_b` and answering 200 for a tag that cannot exist is worse than saying no.

`/api/del-under` shreds encrypted blobs before tombstoning, the same pre-pass
`/api/del` and the CLI run.

These two are one keystroke apart and opposite in consequence, so the UI
obligations are not optional: the safe one names the TAG, the destructive one
names the RECORDS and their count, and the destructive one previews what it will
destroy and offers the safe one as a way out. `GUI.md` has the wording.

## Maintenance

| Method | Path | Does |
| --- | --- | --- |
| POST | `/api/compact[?forget=1]` | reclaim deleted records; `forget=1` also drops the delete facts |

This exists because **a phone has no CLI**. Without it the store grows forever
with deleted bodies, the tags of deleted records linger in `idx/`, and the
privacy note's advice (`ais --compact --forget-deleted`) is unreachable for
exactly the people who most need it.

`forget=1` maps to `ais_compact_purge`: each tombstone keeps its id, so the
record stays suppressed here, but loses its digest, so the deletion stops
travelling to peers and stops being testable against a guess. The caller must
say the price out loud before asking — a device that has not synced since can
push those records back. Both pages ask it as a **separate** question from
"reclaim the space", because it is the one choice here another device can undo.

## Sync

`/api/sync/host`, `/api/sync/status`, `/api/sync/join`, `/api/sync-folder`,
`/api/export-bundle`, `/api/import-bundle`, `/api/store`. See `SYNC.md`.

`/api/store` switches the active index and persists the choice via
`ais_default_set`, which writes the developer's REAL `~/.ais/config`. Any test
touching it must snapshot and restore that file — `tests/gui/serve.sh` does, and
did not for a long time, which is how a plain `make ut` silently repointed a
developer's saved default at a temp directory it then deleted.

## Static files

`GET /` serves `$AIS_WEB/index.html` if that directory has one, else the
embedded `PAGE`. Any other path is served from `$AIS_WEB` if present. The name
must be one safe filename — the per-character filter rejects `/` and `..`, so
traversal is not possible — but note that every file in that directory is
web-readable, so do not point `$AIS_WEB` at a directory holding private files.

`$AIS_WEB` was documented in `app/README.md` and `doc/android-install.md` for a
long time before it was ever read: `serve_asset` hardcoded `gui/web`, so the
documented `AIS_WEB=app ./ais --serve` silently fell back to the embedded page
and `app/` was unreachable.

## Testing

- `tests/gui/serve.sh` — the API itself, over `curl`.
- `tests/gui/ui.sh` — both pages rendered in headless Chrome, asserting on the
  post-JS DOM.
- `tests/gui/cdptest.c` — one CDP driver, run against **both** pages by
  `tests/gui/inter.sh`. The element ids and function names are deliberately
  identical across the two front ends so this works; keep them that way.
