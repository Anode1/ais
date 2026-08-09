# Five languages, two core operations, one algorithm

The two operations `performance.txt` measures for the engine (a full store scan,
and the intersection of posting lists), written in all five (C, Ada, Rust, Java,
Python) with the SAME algorithm, on the same warm 1M-record / 80.9 MB index. The
question: how much of ais's speed is C, and how much is just the algorithm?

Run it: `sh lang_bench.sh` (regenerates the index if absent, ~2 min first time).
Add `OPT=3` to build the native benches at `-O3` instead of `-O2`.
SPARK proof: `sh spark_prove.sh` (needs `gnatprove`; proves the merge has no
runtime errors).

Machine: Intel Core i7-1165G7 (4 cores / 8 threads, 12 MB L3), 64 GB RAM, index
in /tmp, warm page cache. `cc -O2`, GNAT 13.3, rustc 1.75, OpenJDK 21, CPython
3.12. Every figure below is a **median of 10 process launches x 6 warm
in-process iterations (70 samples)**, each launch pinned to one core with
`taskset` and all builds interleaved within a repeat, so drift hits them
equally. First-order numbers; they move with the machine.

The dataset: 1,000,000 records, an 80.9 MB store, and the two largest posting
lists holding 270,011 and 132,637 ids with 21,923 in common. `gen.py` is seeded,
so the data is reproducible; every build below agrees on both answers
(379,090 matching records, 21,923 common ids), which is the first check that
they are running the same algorithm.

## What is being measured, exactly

The clock covers **one pass of the loop over data already in memory**, nothing
else. Each bench reads the file (and, for the intersection, parses the ids into
an array) *before* the first `clock_gettime`, then runs the same loop 7 times and
prints each iteration; the tables take the median of the warm ones. `lang_bench.sh`
`cat`s the store and both posting lists to /dev/null first, so the page cache is
warm and the numbers are CPU, not disk.

The two operations are the engine's two hot paths, and nothing else about ais is
in these numbers:

- **scan**: walk 80.9 MB of store bytes, count the lines containing a substring.
  This is what `--find` and `--dump` do.
- **intersect**: two-pointer merge of the two largest posting lists (sorted id
  arrays), counting the ids present in both. This is what a multi-key `get`
  does, and it is the operation that makes AND the default.

What the numbers deliberately exclude: file reading and parsing, memory
allocation, the index build, and process startup (the third row is measured
separately, as the time for a do-nothing program in each language to start and
exit). Also excluded, and this is the important one, the engine's actual
**streaming** behaviour: the timed merge runs on two arrays already in RAM, so it
measures the loop's CPU cost, not the bounded-memory k-way streaming that lets
ais answer a 270k-hit key without holding it all. That is measured separately in
[`../../doc/performance.txt`](../../doc/performance.txt).

So the question this answers is narrow and deliberately so: **given the identical
algorithm, how much does the language itself cost on the two loops ais runs most?**
It is not a claim about the languages in general, and not a benchmark of five
implementations of ais.

## Same hand-written algorithm (the identical loop in every language)

All native builds at `-O2`. Ada appears twice because its default is different
from everyone else's: checks on is what you get, checks off is what the SPARK
proof licenses (see below).

| Operation                          |  C -O2 | Ada, checks on | Ada `-gnatp` | Rust -O | Java warm | Python loop |
|------------------------------------|-------:|---------------:|-------------:|--------:|----------:|------------:|
| scan 80.9 MB store + count         | 50.3ms |         73.2ms |       50.9ms |  57.7ms |     107ms |       239ms |
| intersect posting lists (merge)    | 1.17ms |         1.32ms |       1.25ms |  1.16ms |    1.30ms |       66ms  |
| process startup (before any work)  |  0.2ms |          0.9ms |        0.9ms |   0.4ms |    21.3ms |       9.5ms |

Apples to apples, Python is ~4.7x slower scanning and ~57x slower on the merge: a
tight two-pointer integer loop is exactly what the CPython interpreter is worst
at (bytecode dispatch and boxed ints per step, not a register op).

Java reaches C-class speed on the **merge** once the JIT compiles the hot loop a
few iterations in — 6.9 ms on the first pass, 1.3 ms warm. On the **scan** it
does not, and it does something we did not expect: the first pass is the fastest
one (74 ms), and the steady state after JIT compilation settles ~40% *slower*
(~107 ms, 2.1x C). The effect is stable across all 10 launches, and the native
builds show flat per-iteration times on the same data, so it is not thermal
drift. We have not chased down which C2 decision costs it; the honest reading is
that "the JIT reaches C speed" holds for the merge here and not for the scan.

## But idiomatic Python delegates the loop to C builtins

| Operation                          | Python idiomatic |
|------------------------------------|-----------------:|
| scan  `data.count(b'...')`         | 46.5ms (beats the hand-written C loop) |
| intersect  `len(setA & setB)`      | 4.95ms |

`bytes.count` is CPython's optimized C substring search, and `set & set` is a C
hash-join, so competent Python is within a few x of C HERE, as long as the hot
work stays inside CPython's C internals and not in Python bytecode.

## Ada: native like C, and the checks are not free

GNAT is a GCC front end, so Ada compiles to the same native code and starts
instantly like C. With checks suppressed it is a dead heat with C on the scan
(50.9 vs 50.3 ms) and within 7% on the merge, far past Python's hand loop.

The interesting part is the safety knob. Ada runs with bounds and overflow
checks ON by default, and on these loops that is not free:

| Cost of leaving the checks on | scan | merge |
|-------------------------------|-----:|------:|
| at `-O2`                      | +44% | +5%   |
| at `-O3`                      | +16% | +8%   |

The scan figure is solid: +44% here, +43% and +46% in two earlier sweeps. The
merge figure is small enough to sit near the run-to-run noise of a 1.2 ms
measurement — repeated sweeps put it anywhere from +5% to +20%, so read it as
"single-digit-to-modest percent", not as a precise number. Either way, it is not
zero, and the binaries say why: the checks-on build carries 35 check-raise call
sites (`__gnat_rcheck_*`), the `-gnatp` build carries none.

So the honest version of the Ada story is not "safety was free". It is: **Ada
gives you C's speed and C's startup with the checks off, and the checks cost
real time when they are on — which is exactly what makes proving them
removable worth something.** That is the next section.

## The -O3 column

`-O3` is worth a look because the C convention is to stay at `-O2` — partly
because its extra loop transformations can amplify the damage from undefined
behaviour, which is far less of a concern in Ada. On these two loops:

| build                     | scan   | vs -O2 | merge  | vs -O2 |
|---------------------------|-------:|-------:|-------:|-------:|
| C -O2                     | 50.3ms |        | 1.17ms |        |
| C -O3                     | 48.9ms |  -2.6% | 1.18ms |  +0.9% |
| Rust opt-level=2          | 57.7ms |        | 1.16ms |        |
| Rust opt-level=3          | 61.1ms |  +5.8% | 1.24ms |  +6.7% |
| Ada -O2, checks on        | 73.2ms |        | 1.32ms |        |
| Ada -O3, checks on        | 66.2ms |  -9.6% | 1.29ms |  -2.1% |
| Ada -O2 `-gnatp`          | 50.9ms |        | 1.25ms |        |
| Ada -O3 `-gnatp`          | 57.1ms | +12.2% | 1.20ms |  -4.2% |

**For C, `-O3` buys nothing here** — both operations land inside the run-to-run
spread, for 17% more `.text` (4289 -> 5029 bytes). Declining the extra UB
exposure costs zero performance on this code. Rust at opt-level=3 is likewise a
small loss.

**The one build `-O3` clearly helps is Ada with checks on**, ~10% on the scan,
reproducible across sweeps. The extra inlining and loop unswitching give GNAT
more context to hoist or fold range checks, so `-O3` recovers roughly a third of
what the checks cost. On the `-gnatp` build there is no check overhead left to
absorb, and the scan measures ~12% slower.

**That ~12% is code placement, not a transformation cost**, which took some
digging to establish. Three findings, all pointing the same way. First,
`-fopt-info-loop-optimized` reports no `-O3` loop pass firing on the scan loop at
all: the only added transformations are at `bench.adb:58` and `:63`, both inside
`Read_Postings`, which is the merge path. Second, the scan's hot path is
instruction-for-instruction identical at both levels, six instructions either
way, differing only in that `-O3` leaves a `+0x10` displacement in the addressing
mode where `-O2` pre-biases the pointer. Third, and decisive, sweeping
`-falign-loops` and `-falign-functions` over 16/32/64 moves `-O2` between 49.3
and 57.1 ms and `-O3` between 48.7 and 53.7 ms, best-of-9 runs each. The two
ranges overlap almost completely and the fastest build of the eighteen is an
`-O3` one. Bisecting the added passes corroborates it: `-fno-tree-loop-vectorize`,
`-fno-tree-slp-vectorize` and `-fno-unswitch-loops` each independently recover
the whole loss, and two of those touch only `Read_Postings`. They help by
shrinking code ahead of the scan and moving it, not by removing a cost from it.

So the `-O3` column above is worth roughly what the alignment spread is worth,
which is to say the differences under about 10% on the scan are not measuring the
optimizer. That caveat applies to the checks-on row too.

The exact difference between the two levels, from `gcc -Q --help=optimizers`
(GCC 13.3): 137 passes enabled at `-O2`, 150 at `-O3`. The thirteen added are
`-fgcse-after-reload`, `-fipa-cp-clone`, `-floop-interchange`,
`-floop-unroll-and-jam`, `-fpeel-loops`, `-fpredictive-commoning`,
`-fsplit-loops`, `-fsplit-paths`, `-ftree-loop-distribution`,
`-ftree-partial-pre`, `-funroll-completely-grow-size`, `-funswitch-loops`,
`-fversion-loops-for-strides`, plus `-fvect-cost-model` going from `very-cheap`
to `dynamic`. Every one of them trades code size for loop throughput. Both hot
loops are byte-at-a-time state machines with a loop-carried dependence, so there
is nothing in them to interchange, distribute or vectorize, and the added passes
leave them alone. What they do find is the line-counting loop in
`Read_Postings`, which `-O3` vectorizes and unrolls, growing that routine from
306 to 1007 bytes; that is the plausible source of the merge's ~4% gain, and of
the layout shift that costs the scan its 12%. GNAT is a GCC front end, so that
same list is what `gnatmake -O3` turns on.

## Rust: also native, C-class, safe at compile time

Rust matched C on the merge (1.16 vs 1.17 ms) and came in 15% behind on the scan
(57.7 vs 50.3 ms): same LLVM-class native code, and the bounds checks in the
guarded loops were largely elided by the optimizer. So like Ada it delivers
near-C speed with safety, but the safety is the borrow checker (memory and
data-race safety proven at compile time) rather than runtime checks.

## Safety: what each language guarantees

Speed is one axis; what the language guarantees about that hot loop is another.

| Language       | the hot loop's safety, and when it is enforced |
|----------------|------------------------------------------------|
| C              | none: overflow and out-of-bounds are undefined behavior |
| Java / Python  | checked at RUN TIME (exception / panic), inside a heavy runtime |
| Ada (default)  | checked at run time, native; costs ~44% on the scan, a few percent on the merge |
| Rust           | memory + data-race safety PROVEN at compile time (borrow checker); bounds checked at run time, mostly elided |
| SPARK          | absence of ALL runtime errors PROVEN at compile time (`gnatprove`: no overflow, no out-of-range, no div-by-zero), so the checks can be switched off and the build is safe by proof, not by luck |

Rust and SPARK are the compile-time-safety tier and prove different things: Rust
proves memory and aliasing safety; SPARK proves the absence of every runtime
error (and, with contracts, functional correctness) via an SMT solver. SPARK is
the strongest static assurance of the set, which is why avionics and crypto use
it.

This is not hypothetical here. `spark/` holds a SPARK version of the merge;
`sh spark_prove.sh` (needs `gnatprove`) proves it, and the result is clean:

    SPARK Analysis results        Total      Flow      Provers   Justified   Unproved
    Run-time Checks                  15         .    15 (CVC5)           .          .
    Assertions                        6         .     6 (CVC5)           .          .
    Termination                       2         1     1 (CVC5)           .          .
    Total                            23    1 (4%)     22 (96%)           .          .

    Success: all checks proved (23 checks).

Every array index proved in range, every `+` proved non-overflowing, the loop
invariants discharged and the loop proved to terminate: zero runtime-error checks
left to chance.

What that is worth, measured. Timing the proved unit itself (`spark/merge.adb`
behind a driver, same harness):

| build of the proved unit  | merge  |
|---------------------------|-------:|
| `-O2`, checks on          | 1.38ms |
| `-O2 -gnatp`              | 1.15ms |
| `-O3 -gnatp`              | 1.14ms |

The proved unit with checks off is the fastest merge in this whole document —
marginally ahead of C's 1.17 ms, i.e. parity. So the proof is not decoration:
it is what licenses the build that matches C, and on the scan loop the same
move is worth 44%. **The claim is not that Ada's checks are free. It is that
SPARK makes removing them safe.**

Scope, so the proof is not oversold: `merge.ads` states a `Pre` but no `Post`.
What is proved is absence of runtime errors and termination — *not* that the
result equals the true intersection count. A version that always returned 0
would prove just as clean. Functional correctness would need a postcondition;
that is a further step SPARK supports and this unit does not take.

## What it means

Python's speed is binary: C-fast when the loop is a builtin, ~57x slower the
moment the loop is yours. Java pays JVM startup (21 ms) plus JIT warmup on every
invocation, which a short-lived CLI never amortizes — and on the scan the JIT
never reaches C speed at all here.

The catch for ais: its intersection is a STREAMING k-way merge of sorted posting
lists, memory O(number of keys), holding only each list's head. (The timed merge
above runs on two arrays already loaded, so it measures this loop's CPU, not the
bounded-memory streaming, which is the engine's design, measured in
`performance.txt`.) There is no Python builtin for "merge N sorted streams". The choices are the hand-written
loop (the 66 ms, ~57x) or `set &`, which is a hash join that loads every id of
every key into hash tables and abandons the bounded-memory streaming that lets
ais answer a 270k-hit key in ~2 s without holding it all in RAM. The very design
that makes ais scale on bounded memory is the design Python can only run slowly,
or only by becoming a different, memory-heavier program.

So: for batch, vectorizable, builtin-shaped work Python is fine and Java is
C-class on the merge. For a short-lived CLI running hand-written streaming loops
on every command, the order is C, Ada with the checks proved off, and Rust (all
native, instant start; Rust and SPARK-Ada safe too), then Ada with checks on,
then warm Java, then Python-with-builtins, then hand-written Python far behind.
ais is the second category, which is why it is C (Rust or SPARK-Ada would be the
safe-systems alternative at the same speed).
