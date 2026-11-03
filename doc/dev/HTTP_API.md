# The `--serve` HTTP API

`ais --serve [PORT]` puts the engine behind a small HTTP API on loopback and
serves a page that drives it. **Two** front ends use this API and must stay in
step (see `GUI.md`): the page embedded in `c/serve.c` as the `PAGE[]` string
literal, and `app/index.html` (the installable PWA), served when `$AIS_WEB`
points at the `app/` directory.

Everything is loopback-only, single user, no accounts. A request of any method
to `/api/*` from a cross-site context is refused (403); see the `cross_site` check at the top of
`handle()` in `c/serve.c`.

Bodies and replies are plain text, never JSON: one record per line, the same
shapes the CLI prints. A GUI that can be debugged with `curl` is the point.

## Retrieval

| Method | Path | Answers |
| --- | --- | --- |
| GET | `/api/get?keys=A+B[&or=1][&count=N][&after=ID][&meta=1]` | `id\|value` per line, one line per LINK; `meta=1` emits `id\|keys\|value` (the record's visible tags, space-separated, possibly empty) |
| GET | `/api/find?q=TEXT[&key=TAG]` | `id\|value` per line: live records whose value contains TEXT as a case-insensitive (ASCII) substring, as `--find` prints; `key=` keeps only the records filed under TAG (the project). Values come verbatim from the store, so a document row is its `blobs/` path: the search covers stored values, not document bodies. Both pages append these after the tag matches, deduped by id, under one "Found in the text" separator |
| GET | `/api/timeline?count=N[&before=ID][&from=D][&to=D][&key=TAG]` | `id\|ts\|keys\|value`, newest first; `key=` restricts the page to one tag (the project) |
| GET | `/api/tags[?count=N&afterc=C&afterk=K]` | `count\|key`, busiest first |
| GET | `/api/due` | `id\|ts\|keys\|value`, id ascending: every record under a `due:YYYY[-MM[-DD]]` key whose day has come, judged by the server's local day; empty when nothing is due |
| GET | `/api/keys?id=N` | the visible tags of one record |
| GET | `/api/doc?v=blobs/...` | the full text of one document, named by its stored `blobs/` value (the first line inside every `aisdoc:` base64, see below). List views carry only a bounded preview, so an editor starts from this. 404 when the file is not here, is not editable text (NUL / non-UTF-8), or exceeds what one `setvalue` request could save back (~32 KB) |
| GET | `/api/stats` | `records:` / `keys:` / `deleted:`, as `ais --stats` |
| GET | `/api/where` | the index directory |
| GET | `/api/version` | `engine:` and on-disk `format:`; see `VERSIONING.md` |

`/api/get` emits **one line per link**, and a record may hold several. A GUI that
counts lines counts links, not records: group by the id before showing a count,
especially in anything destructive. Both front ends learned this the hard way.

A document value reaches the client as `aisdoc:<base64 of "path\ncontent">`: the
first line is the stored `blobs/` path (the handle for `/api/doc` and the old
value for `setvalue`, carried per row because an id alone cannot name one value
of a multi-link record) and the rest is the bounded preview. An absent document
(file not on this device) arrives as its raw `blobs/` path instead, and is not
editable there.

## Writing

| Method | Path | Does |
| --- | --- | --- |
| POST | `/api/put?keys=A+B[&enc=1]` | save; body is the value, or `passphrase\nvalue` when `enc=1` |
| POST | `/api/update?id=N&keys=A+-B` | attach/detach tags (a `-` prefix detaches) |
| POST | `/api/setvalue?id=N` | replace one value in place; body is `oldvalue\nnewtext`. The old value is one line (a document's is its `blobs/` path, from the row's `aisdoc:` header line); the new text may span lines, and the engine stores it inline or as a fresh document blob, as saving does |
| POST | `/api/del?id=N` | delete one record; 404 with the body `no record N` when no live record has that id, 400 when `id` is missing |
| POST | `/api/reveal` | body is `passphrase\nmarked-value`; replies with the cleartext, or empty |

There is deliberately no `/api/add`: the CLI's `--add ID -v VALUE` (a second link
on one record) has no HTTP equivalent, so a multi-link record can be READ from a
GUI but never created or extended there. Acceptance testing flagged this as a real
gap, not a design choice; noted here so it is not mistaken for one.

A save that stores nothing answers **500**, not 200 with a count of zero. An
encrypted save on a build with no crypto module used to reply `saved 0
record(s)` with a 200, so both pages closed the sheet as though the secret had
been stored: the one case where believing a success message loses exactly the
thing you were protecting.

## Tag-level operations

| Method | Path | Does |
| --- | --- | --- |
| POST | `/api/untag?keys=KEY` | remove the tag, KEEP every record |
| POST | `/api/del-under?keys=KEY` | DELETE every record filed under the tag |

Both take **one** tag, not the whitespace-separated list every other endpoint
takes, and refuse a value containing whitespace with a 400. Folding `a b` to
`a_b` and answering 200 for a tag that cannot exist is worse than saying no.

`/api/del-under` discards each record's document or encrypted blob
(`ais_doc_discard`) before tombstoning, the same pre-pass the CLI's `--del-under`
runs, because `ais_del_key` never calls the discard callback. `/api/del` runs no
pre-pass: `ais_del` deletes first and the discard callback takes the blob after.

The UI obligations for the pair, and the locked wording, are in `GUI.md`,
"Untag vs delete-under".

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
say the price out loud before asking: a device that has not synced since can
push those records back. Both pages ask it as a **separate** question from
"reclaim the space", because it is the one choice here another device can undo.

## Importing a file

| Method | Path | Does |
| --- | --- | --- |
| POST | `/api/import-links` | body is a browser's bookmarks HTML or a links CSV (Pocket, Instapaper, Raindrop, a spreadsheet), at most 64 MiB; replies `imported N skipped M new K under MARKER`, or 400 with the reason (`bad upload`, `no url column: ...`, `UTF-16 text: save it as UTF-8 first`, `a zip: open it, then pick the .csv inside`, `import failed`). The same importer as `--import-bookmarks` (`import.h`) |

## Sync

`/api/sync/host`, `/api/sync/status`, `/api/sync/join`, `/api/sync-folder`,
`/api/export-bundle`, `/api/import-bundle`, `/api/store`. See `SYNC_DESIGN.md`.
The host, status, join, export-bundle, import-bundle and `POST /api/sync-folder`
routes exist only in a build with the crypto module (`SERVE_HAVE_SYNC`) and are
a 404 without it; `GET /api/sync-folder` and `/api/store` always exist.

`GET /api/sync-folder` answers the folder this index syncs with (kept in
`<index>/syncfolder`), or an empty body when none is set.

`POST /api/sync-folder` takes the folder path as the body and answers `synced`, or
**400 with the reason as the body**: `no such folder`, `not a folder`, `cannot read
that folder`, `folder empty` (synced here before, no device bundles in it now), or
`cannot write`. Each has a different remedy, so a front end must show which one it
got; a flat "sync failed" is what let a broken folder sync pass for a working
backup. `?force=1` accepts the `folder empty` case, and is the web equivalent of
the CLI's `-y`. Nothing else creates the folder or bypasses a check.

`/api/store` switches the active index and persists the choice in the
developer's REAL `~/.ais/config`: `current` set by name when the path is a
registered index, `current` and the legacy line both cleared for home, otherwise
`current` cleared and the legacy `index = PATH` line written (`ais_default_set`). Any test
touching it must snapshot and restore that file: `tests/gui/serve.sh` does, and
did not for a long time, which is how a plain `make ut` silently repointed a
developer's saved default at a temp directory it then deleted.

## Static files

`GET /` serves `$AIS_WEB/index.html` if that directory has one, else the
embedded `PAGE`. Any other path is served from `$AIS_WEB`, or from `gui/web`
under the working directory when `$AIS_WEB` is unset (the dev default). The name
must be one safe filename: the per-character filter allows only `[0-9A-Za-z._-]`,
so a `/` is refused and traversal is not possible (`..` passes the filter and
opens a directory, which fails). Every file in that directory is
web-readable, so do not point `$AIS_WEB` at a directory holding private files.

`$AIS_WEB` was documented in `app/README.md` and `doc/android-install.md` for a
long time before it was ever read: `serve_asset` used `gui/web` only, so the
documented `AIS_WEB=app ./ais --serve` silently fell back to the embedded page
and `app/` was unreachable.

## Testing

- `tests/gui/serve.sh`: the API itself, over `curl`.
- `tests/gui/ui.sh`: both pages rendered in headless Chrome, asserting on the
  post-JS DOM.
- `tests/gui/cdptest.c`: one CDP driver, run against **both** pages by
  `tests/gui/inter.sh` (`GUI.md` says why one driver fits both).
