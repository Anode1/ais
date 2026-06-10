# Locking & concurrency

`INDEX/lock` coordinates writers. The rule: **reads take no lock; writers take
an exclusive flock for the duration of one mutating op.** A long-lived reader
(for example `ais --serve`) therefore never blocks the CLI or an agent.

## Model

- `store_open` opens the lock file but does **not** lock it. Any number of
  readers (`get`, `find`, `dump`, `keys`, `where`, `stats`) run at once.
- Each mutating op takes `store_wlock` (blocking `flock(LOCK_EX)`) and releases
  with `store_wunlock`: `put`, `add`, `del`, `del-key`, `compact` (and `import`
  and `doc`, which go through `put`). Writers serialize; a second writer waits
  rather than failing.
- `next_id` is **disk-authoritative**. A writer calls `store_load_next_id` under
  the lock before assigning an id, so two processes never hand out the same one.
  `store_close` does *not* save `next_id` (each write already did, under the
  lock); re-saving a stale in-memory value would clobber a concurrent writer.

## What a lock-free reader can observe

A read concurrent with a write may catch the store's last line half-appended.
That is the same graceful degradation the plain-text design accepts elsewhere:
one torn line, recoverable, never silent corruption. The `off` accelerator is
re-checked against the line's id on every lookup, so a stale `off` falls back to
a full scan instead of returning wrong data.

## Why reads are not locked

Unix convention: readers do not take exclusive locks (that is the Windows
mandatory-lock model). Concurrent reads are the common case for a personal
memory reached at once from the CLI, the GUI, and an agent.

## Files / API

- `INDEX/lock` is the flock target (an empty file).
- `store_wlock` / `store_wunlock` / `store_load_next_id` in `store.c`; see
  `LAYOUT.md` for the rest of the on-disk layout.
