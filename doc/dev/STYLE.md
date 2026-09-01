# AIS: Coding Style and Ideology

How AIS is written, and why. Read this before contributing. The existing sources under `c/`
are the reference; when a rule here is unclear, read the code.

**Scope: the engine core.** These rules bind the C engine (the `c/` sources behind `ais.h` and
`embed.h`) and are non-negotiable there. GUI wrappers, language bindings, and plugins sit outside
the core and follow their host's conventions; they call the engine, they do not relax it. Do not let
a peripheral's needs leak inward and dilute the core to ordinary C. The point of writing this down is
that the strictness is the asset: it should not be distilled away by a well-meaning refactor.

## Language and style

- **C99.** Style follows Arnold Robbins, *Linux Programming by Example: The Fundamentals*
  (K&R lineage): clear, conventional, no cleverness for its own sake.
- **POSIX for the plumbing:** `getopt` for arguments, standard streams, conventional exit codes.
- Build clean under `-std=c99 -Wall -Wextra`. A warning is a defect.

## Modularity: one file per concept

Each `.c`/`.h` pair is **one cohesive concept**: a frame of knowledge or processing. This is the
module-as-class discipline (a class is just a struct plus the functions over it; that is where classes
came from). Decompose to keep complexity at bay: give a concept its own file when it is genuinely
isolatable (the help text, the store, the posting lists, the merge). Names are clear
and literal, for files, types, and variables alike, so a reader infers a file's responsibility
from its name and a function's from its signature.

Two functions do not meet this bar and are known debts, not precedents: `main()` in `main.c` (~630
lines) and `handle()` in `serve.c` (~520). Both are flat option/route dispatchers where each case is
short and calls out immediately, which is why they have been tolerated, but each is long enough that
a new case is easy to misplace. Add work to them by adding a case that delegates, never by growing one
in place; splitting the dispatch out is welcome. No other function in the tree may reach this size.

## Memory: stack first, heap only when forced

- Process records **one at a time**, with **automatic (stack) variables and fixed-size buffers**.
- Avoid `malloc`/`calloc`/`realloc` unless an operation genuinely cannot be done in bounded memory.
- **Invariant: peak footprint is a function of the struct sizes alone.** A 10 GB store
  and a 10 KB store run in the same memory. The largest footprint must be computable by hand from the
  structures held.
- When the heap is truly unavoidable, the allocation is bounded, documented, and freed on every path.
- **The only heap the core sanctions today.** Every other `malloc` is a defect to justify in review
  or remove. The record path itself (get, find, set, merge, compact) is strictly stack-and-stream and
  allocates nothing. The four exceptions are all bounded by something other than the corpus:
  1. *FFI return strings and the handle* (`embed.c`): a variable-length result handed across the seam
     to a GUI or binding, owned by the caller and released with `ais_embed_free`. Bounded by one
     result, not the store.
  2. *Bounded aggregate collectors* (`ais.c`: `ais_keys`, `ais_tags`, `tl_scan`): a set sized by key
     cardinality or a fixed top-N count, never by record count, and freed before return.
  3. *Crypto buffers* (`secret.c`): an AEAD document is authenticated as a whole, so the blob and its
     plaintext are held once, bounded by the document, wiped and freed on every path.
  4. *Sync transport* (`sync.c`): the sealed merge stream is buffered whole to seal or verify it as one
     AEAD document (it cannot be authenticated incrementally), then wiped and freed on every path.
     Bounded by the export and capped on the receive side (64 MiB); the send side needs the same cap,
     and a streaming chunked AEAD is the planned follow-up that returns this to stack-and-stream.
  5. *Encrypted document intake* (`feed.c`, `feed_encrypt_doc`): stdin is read into one buffer capped
     at a compile-time `DOC_MAX` (4 MiB), because the AEAD seals the document as a whole, as in (3).
     Bounded by the constant, never by the store; input past the cap is refused rather than grown into,
     and the buffer is wiped and freed on every path, including each `die()`.
  6. *Bundle upload* (`serve.c`, the `/api/import-bundle` handler): a POST body is buffered whole to be
     merged as one bundle, capped at 64 MiB, the same cap the sync receive side uses. Bounded by the
     upload, never by the store, and freed on every path. Front-end code, not the record path.

  Both (5) and (6) hold one document for one operation and are refused past a fixed cap. Neither may be
  imitated to hold *records*: the record path stays stack-and-stream and allocates nothing.

Why this discipline pays, four ways:

- **Safety.** It eliminates the leak / use-after-free / double-free class by construction: the bulk
  of C's CVEs simply cannot occur where nothing is allocated. This is the avionics and medical-device
  standard (Power of Ten, MISRA C:2012 rule 21.3), which is why the author's earlier C tools ran
  unattended in hospitals and medical colleges, shipped as source and precompiled binaries with no
  compatibility trouble.
- **Low entropy.** The whole memory model fits in your head. Peak footprint is computable by hand from
  the structs held: no allocator state, no ownership graph, no lifetimes to track, nothing hidden to
  reason about. Less to know is less to get wrong, and a reviewer can verify the bound by reading.
- **Fast execution.** No allocator on the record path means no `malloc` latency and no fragmentation,
  and reading forward over fixed buffers is cache-friendly and predictable. A 10 GB store and a 10 KB
  store run in the same small, constant memory.
- **Portability.** Small static binaries with no dependency surface drop into locked-down environments
  unchanged. A stock C99 toolchain is the only requirement.

## Error handling and C idioms

- **Return codes in the modules.** A function returns `0`/`-1` (or a value/`-1`); the
  caller decides. Only the CLI front-end (`main.c` and its helpers, e.g. `feed.c`) turns a fatal
  error into `die()`. No engine module calls `exit()` or prints an error: a single, visible exit
  path, per Power of Ten.
- **`die(fmt, ...)`** (`log.c`): print to stderr, exit non-zero. CLI-level, for unrecoverable user
  errors (cannot open INDEX, lock held).
- **`debug(fmt, ...)`** (`log.c`): gated on the runtime `-d` flag, prints to stderr; safe to sprinkle
  for tracing, off by default. No compile-time `#ifdef DEBUG`.
- **Functions over macros**: type-checked, debuggable, greppable. The old `LOG`/`FATAL`/`PRINT`
  macro habit is retired.
- **Bounded strings only**: `snprintf` always; never `strcpy`/`strcat`/`sprintf`. A buffer's size is
  always known (the `AIS_*_MAX` limits).
  The one sanctioned `strcpy` is the *checked copy into a fixed buffer*, where the guard sits on the
  line above it and returns rather than truncating:

      char kbuf[AIS_LINE_MAX];
      if (strlen(key) >= sizeof kbuf)
          return 0;                    /* skip an absurdly long key */
      strcpy(kbuf, key);               /* ais_get tokenizes in place */

  It exists because `ais_get` tokenizes its keys in place and so needs a mutable copy, and `snprintf`
  would silently truncate where these callers must skip the input instead. Three sites use it
  (`embed.c`: `ais_embed_get` and `keys_tag`; `serve.c`: `keysof_tag`). Anywhere the guard is not
  immediately visible above the call, it is a defect. `strcat` and `sprintf` have no sanctioned use.
- **Single exit via `goto cleanup`** when a function holds a resource (open file, etc.): the
  Linux-kernel idiom: one place to close and return, no leak on any path.
- **`static` for everything module-private**: a `.c` exposes only what its `.h` declares.
- **Declare at point of use, `const`-correct, `size_t` for sizes**: the C99 cleanups, not
  K&R top-of-function.
- Build clean under `-std=c99 -Wall -Wextra`; the suite runs under AddressSanitizer/UBSan on every
  push and in a pre-push hook (`make codeut-asan`), which is how the memory-safety class is caught
  rather than by switching language (the rationale is `WHY-C.md`). No build framework: a plain
  `Makefile` and `cc`. (ant, not Maven.)

## Streaming

- Read forward, emit immediately. Hold only a bounded **frontier**, sized by the operation.
- Set operations (`AND`/`OR`) are **k-way merges of sorted posting lists**: keep only the current head
  of each list and advance. Memory is O(number of query keys), never O(corpus).
- If an algorithm needs context (a window), allocate **only that window**, predeclared and bounded
  (like a fixed-size read buffer), never the whole input.

The on-disk format that makes this possible (append-only plain-text store, monotonic ids,
per-key sharded posting lists, tombstones, compaction, "the store is the source of truth and the
index is rebuildable") is the subject of `LAYOUT.md`; this file is only about how the *code* is written.

## Damage tolerance and portability (non-negotiable)

- Plain text outlives its tools. Prefer it to binary formats that fail catastrophically on a single
  corrupt byte.
- Corruption must stay local and recoverable, never take down the whole store.
- No exotic dependencies. A stock C99 toolchain builds it; the binary ships anywhere.

## Restraint

- No framework, dependency, feature, or abstraction without proven worth against its maintenance cost.
- No in-memory structure whose size tracks the data.
- Keep the core simple and self-evident. The file format is the contract.

## Lineage

None of this is invented here; the parts are canonical, only the assembly is ours:

- **Stack-first, no dynamic allocation, footprint bounded by struct sizes** is the safety-critical
  standard: NASA/JPL's *Power of Ten* (Holzmann, 2006), rule 3 (no heap after
  initialization), and MISRA C:2012 rule 21.3, which bans `malloc`/`free`. It is the discipline
  avionics and medical-device C is held to, which is why code written this way runs unattended.
- **Many small files over one blob** (per-key files, rsync-friendly, corruption-local) is the
  Maildir-over-mbox argument.
- **Plain text as the universal, durable interface** is the Unix philosophy (McIlroy; Raymond,
  *The Art of Unix Programming*, 2003).
- **Append-only log plus periodic compaction**, and key sharding, are the log-structured merge-tree
  (O'Neil et al., 1996) and the git object store.
- POSIX `argv`/`getopt` and the I/O idioms follow Robbins, *Linux Programming by Example* (2004).

## Testing

- `make ut` runs the tests (the whole suite; `make codeut` runs just the C engine unit tests). `tests/INDEX/store` is both fixture and a worked example of the format.

---

Reference: Arnold Robbins, *Linux Programming by Example: The Fundamentals* (Prentice Hall, 2004):
POSIX `argv`/`getopt`, I/O idioms, and the C99 style this project follows.
