# AIS: Coding Style and Ideology

How AIS is written, and why. Read this before contributing. The existing sources under `c/`
are the reference; when a rule here is unclear, read the code.

## Language and style

- **C99.** Style follows Arnold Robbins, *Linux Programming by Example: The Fundamentals*
  (K&R lineage): clear, conventional, no cleverness for its own sake.
- **POSIX for the plumbing:** `getopt` for arguments, standard streams, conventional exit codes.
- Build clean under `-std=c99 -Wall -Wextra`. A warning is a defect.

## Modularity: one file per concept

Each `.c`/`.h` pair is **one cohesive concept** -- a frame of knowledge or processing. This is the
module-as-class discipline (a class is just a struct plus the functions over it; that is where classes
came from). Decompose to keep complexity at bay: give a concept its own file when it is genuinely
isolatable (the help text, the store, the posting lists, the merge), not arbitrarily. Names are clear
and literal -- for files, types, and variables alike -- so a reader infers a file's responsibility
from its name and a function's from its signature.

## Memory: stack first, heap only when forced

- Process records **one at a time**, with **automatic (stack) variables and fixed-size buffers**.
- Avoid `malloc`/`calloc`/`realloc` unless an operation genuinely cannot be done in bounded memory.
- **Invariant: peak footprint is a function of the struct sizes, not the data size.** A 10 GB store
  and a 10 KB store run in the same memory. The largest footprint must be computable by hand from the
  structures held.
- When the heap is truly unavoidable, the allocation is bounded, documented, and freed on every path.

Why: it scales to any data size; it eliminates the leak / use-after-free / double-free class entirely;
and it yields small static binaries that drop into locked-down environments with no dependency surface.
This is how the author's earlier C tools ran unattended in hospitals and medical colleges, shipped as
source and precompiled binaries, with no compatibility trouble.

## Error handling and C idioms

- **Return codes, not exits, in the modules.** A function returns `0`/`-1` (or a value/`-1`); the
  caller decides. Only the CLI front-end (`main.c` and its helpers, e.g. `feed.c`) turns a fatal
  error into `die()`. No engine module calls `exit()` or prints an error: a single, visible exit
  path, per Power of Ten.
- **`die(fmt, ...)`** (`log.c`): print to stderr, exit non-zero. CLI-level, for unrecoverable user
  errors (cannot open INDEX, lock held).
- **`debug(fmt, ...)`** (`log.c`): gated on the runtime `-d` flag, prints to stderr; safe to sprinkle
  for tracing, off by default. No compile-time `#ifdef DEBUG`.
- **Functions over macros** -- type-checked, debuggable, greppable. The old `LOG`/`FATAL`/`PRINT`
  macro habit is retired.
- **Bounded strings only** -- `snprintf` always; never `strcpy`/`strcat`/`sprintf`. A buffer's size is
  always known (the `AIS_*_MAX` limits).
- **Single exit via `goto cleanup`** when a function holds a resource (open file, etc.): the
  Linux-kernel idiom -- one place to close and return, no leak on any path.
- **`static` for everything module-private**: a `.c` exposes only what its `.h` declares.
- **Declare at point of use, `const`-correct, `size_t` for sizes** -- the C99 cleanups, not
  K&R top-of-function.
- Build clean under `-std=c99 -Wall -Wextra`; run under AddressSanitizer/valgrind during development.
  No build framework: a plain `Makefile` and `cc`. (ant, not Maven.)

## Streaming

- Read forward, emit immediately. Hold only a bounded **frontier**, sized by the operation.
- Set operations (`AND`/`OR`) are **k-way merges of sorted posting lists**: keep only the current head
  of each list and advance. Memory is O(number of query keys), never O(corpus).
- If an algorithm needs context (a window), allocate **only that window**, predeclared and bounded
  (like a fixed-size read buffer), never the whole input.

The on-disk format that makes this possible (append-only plain-text store, monotonic ids,
per-key sharded posting lists, tombstones, compaction, "the store is the source of truth and the
index is rebuildable") is the subject of `LAYOUT.md`; this file is only about how the *code* is written.

## Robustness and portability (non-negotiable)

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
  standard, not a preference: NASA/JPL's *Power of Ten* (Holzmann, 2006), rule 3 (no heap after
  initialization), and MISRA C:2012 rule 21.3, which bans `malloc`/`free`. It is the discipline
  avionics and medical-device C is held to, which is why code written this way runs unattended.
- **Many small files over one blob** (per-key files, rsync-friendly, corruption-local) is the
  Maildir-over-mbox argument.
- **Plain text as the universal, durable interface** is the Unix philosophy (McIlroy; Raymond,
  *The Art of Unix Programming*, 2003).
- **Append-only log plus periodic compaction**, and key sharding, are the log-structured merge-tree
  (O'Neil et al., 1996) and the git object store.
- POSIX `argv`/`getopt` and robust I/O idioms follow Robbins, *Linux Programming by Example* (2004).

## Testing

- `make ut` runs the unit tests. `tests/INDEX/store` is both fixture and a worked example of the format.

---

Reference: Arnold Robbins, *Linux Programming by Example: The Fundamentals* (Prentice Hall, 2004):
POSIX `argv`/`getopt`, I/O idioms, and the C99 style this project follows.
