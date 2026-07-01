# Why C, and why it is safe enough

A recurring objection: "C is unsafe; a memory-safe language like Rust would be
the responsible choice." This is the rebuttal. The short version: speed does not
decide it, the one real gain (memory safety) is recovered by tooling without a
rewrite, and the deciding factor is that a personal tool holding your secrets
should be code you can read and verify yourself.

## Speed is not the argument, either way

The two core operations (a full store scan, a posting-list intersection) were
written in five languages on identical warm 1M-record data. C, Rust, Ada, and
warm Java all land in the same class; Python is C-fast only when the hot loop is
a builtin, and about 50x slower when it is not. So no language here is
meaningfully faster than C for this work: speed is a reason neither to switch nor
to stay. Full table and reproduction: `../../tests/perf/LANG_COMPARISON.md`.

## What a rewrite would actually buy

Honestly, one thing: memory safety, meaning no buffer overflow, use-after-free,
or undefined behavior. It would not buy speed (identical), startup (identical,
both native), or mobile reach: the C engine already cross-compiles to Android and
iOS through the same LLVM/Clang the NDK and Xcode use. The entire case reduces to
the memory-safety class of bug.

## How that safety is obtained without a rewrite

That class is caught here, not ignored:

- Development is **test-driven**: the regression suite (engine unit, CLI
  black-box, and browser UI render) is written test-first and gates every commit
  and the deployment procedure, so a regression cannot ship silently. Behavior is
  the objective gate; see the test-driven loop in `../../AGENTS.md`.
- That same suite runs under AddressSanitizer and UndefinedBehaviorSanitizer on
  every push, on Linux and macOS, and again in a local pre-push hook, so
  overflow, use-after-free, and UB abort with a file:line report instead of
  passing silently under `-O2`. Sanitizers catch only what the tests exercise,
  which is why a broad, growing suite matters. See `make codeut-asan` / `make
  codeut-ubsan`, the hook in `scripts/hooks/`, and `.github/workflows/sanitizers.yml`.
- The code builds clean under `-std=c99 -Wall -Wextra`; a warning is a defect.
- The style is stack-and-stream: the heap is rare and sanctioned (`STYLE.md`),
  and strings are bounded (`snprintf`, never `strcpy`). A small, disciplined heap
  surface is a small bug surface.

Together these close most of the gap a rewrite would close, in the language we
already read.

## The deciding reason: you can read it

A safety guarantee is worth the most to someone who cannot audit the code, and
the least to an author who reads every line. AIS is a personal index that holds
passwords and private pointers, so the code guarding those secrets should be code
its owner can inspect, byte for byte, in a language that is universally legible.
A rewrite would replace "I verified it" with "the compiler says so", and the
`unsafe` blocks an engine still needs (for mmap, raw I/O, and the FFI seam) are
harder to audit than the equivalent C, not easier. For a tool you own and trust
with secrets, staying legible beats a promise you cannot read.

## When to revisit

This is a decision, not dogma. If AIS ever takes untrusted network input at
scale, or a security posture demands provable memory safety, revisit it. The port
would be clean: the engine is a C-ABI library behind a stable seam (`embed.h`)
and the on-disk formats are contracts, so a memory-safe engine would be a library
swap, not a rewrite of the whole app. A proof of concept lives in
`../../tests/poc-rust-merge/`. Until that need is real, C plus sanitizers is the
better trade.
