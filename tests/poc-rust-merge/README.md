# PoC: the ais merge in Rust, behind a C ABI

One-operation proof of concept for a Rust engine. It reimplements the core of
`c/merge.c` (the k-way intersection/union over sorted posting lists) in Rust,
exports it with a C ABI as `ais_merge_ids`, links it into a C harness, and checks
it against a C reference on the real index. The streaming plumbing (tombstones,
the emit callback, `post_stream` file heads) stays in C for now; this is the pure
compute core, the smallest honest slice to measure.

    make                      # build poc (C + Rust) and poc_c (C only)
    make check IDX=/tmp/ais_1m   # intersect the two busiest keys, both ways
    make size                 # byte sizes + the Rust delta

On the 1M index both agree exactly (21939 common ids), so the Rust drop-in
matches `merge.c`'s semantics.

## Your three questions

### Dependencies: none beyond libc

    $ ldd poc            # C + Rust merge
        libc.so.6
        ld-linux-x86-64.so.2
    $ ldd poc_c          # pure C
        libc.so.6
        ld-linux-x86-64.so.2

Identical. The Rust merge pulls in **no** extra shared library, not even
`libgcc`. That is because it is written `#![no_std]`: it uses only `core` (the
freestanding subset of Rust, no runtime, no allocator), no allocation, and **no
external crates at all**. So it links into a C program exactly like a `.o`, and
the C side keeps owning `main`, malloc, and libc.

### What is cargo / "cargo vendor", and why it does not apply here

- **cargo** is Rust's build-and-package manager (roughly `make` + a package
  fetcher in one). **crates** are third-party libraries it downloads from
  crates.io.
- **`cargo vendor`** copies those downloaded crates into a local `vendor/`
  directory so the build is offline and the dependencies are committed, the same
  idea as ais vendoring its jars under `java/lib/`.

This PoC uses **none of that**. No crates, so nothing to download, so nothing to
vendor, so no `cargo` needed. We call `rustc` directly on one file:

    rustc --edition 2021 -O -C panic=abort -C strip=symbols \
          --crate-type staticlib merge.rs -o libaismerge.a

and hand the resulting `.a` to `cc`. A dependency-free Rust component honours
ais's "no deps, vendored, no build step beyond the compiler" rule as cleanly as C
does. Cargo and vendoring would only enter if you chose to pull an outside crate
(for example a crypto crate), which for the engine you need not.

### Binary size: ~4 KB over pure C

| binary | bytes | note |
|---|---:|---|
| `poc_c` (pure C, stripped) | 14,464 | the harness + C merge |
| `poc` (C + Rust merge, stripped) | 18,640 | **+4 KB** for the Rust merge |
| `libaismerge.a` | 12 MB | intermediate archive, gitignored; only ~4 KB is linked in |

Two things matter here:

1. **A Rust `.a` linked into a C program is not the same as a standalone Rust
   binary.** A standalone Rust exe statically links the full `std` and floors at
   ~400 KB (see `tests/perf/LANG_COMPARISON.md`). A `no_std` staticlib linked
   into C adds only the code actually reached, ~4 KB here.
2. **You must let the linker garbage-collect.** Without it the archive drags in
   ~2.4 MB of unreferenced `core`. The Makefile builds with
   `-ffunction-sections -fdata-sections -Wl,--gc-sections` then `strip`, which
   drops the dead code and takes the delta from 2.4 MB to ~4 KB. Same machine
   code runs either way; the difference is purely dead-code removal.

## What this proves, and what it does not

Proves: the FFI drop-in is real (same `.a`-into-`cc` model as C), the algorithm
ports cleanly and matches on real data, and both the dependency and size costs
are negligible when written `no_std`.

Not yet done: the streaming `post_stream` path, tombstone suppression, the emit
callback, wiring it behind `merge.h`/`embed.h` in place of the C `merge_run`, and
running it under `make ut`. Those are the next increments if this goes further.
