# C vs Java vs Python: the two core operations

The two operations `performance.txt` measures for the engine (a full store scan,
and the intersection of posting lists), written in all three languages with the
SAME algorithm, on the same warm 1M-record / 88 MB index. The question: how much
of ais's speed is C, and how much is just the algorithm?

Run it: `sh lang_bench.sh` (regenerates the index if absent, ~2 min first time).

Machine: one desktop core, /tmp, warm page cache. `cc -O2`, OpenJDK 21,
CPython 3.12. First-order numbers; they move with the machine.

## Same hand-written algorithm (the loop, as written in C and Java)

| Operation                          |  C -O2 |   Java warm | Python loop |
|------------------------------------|-------:|------------:|------------:|
| scan 88 MB store + count           |  ~90ms |  ~100-150ms |      ~300ms |
| intersect posting lists (merge)    |  1.3ms |       1.3ms |       ~72ms |
| process startup (before any work)  |   ~1ms |       ~30ms |       ~10ms |

Apples to apples, Python is ~3x slower scanning and ~55x slower on the merge: a
tight two-pointer integer loop is exactly what the CPython interpreter is worst
at (bytecode dispatch and boxed ints per step, not a register op). Java reaches
C speed once the JIT compiles the hot loop a few iterations in; cold it is
~3-4x slower (the first merge above was 5 ms before warming to 1.3 ms).

## But idiomatic Python delegates the loop to C builtins

| Operation                          | Python idiomatic |
|------------------------------------|-----------------:|
| scan  `data.count(b'...')`         | ~67ms (beats the hand-written C loop) |
| intersect  `len(setA & setB)`      | ~6ms |

`bytes.count` is CPython's optimized C substring search, and `set & set` is a C
hash-join, so competent Python is within a few x of C HERE, as long as the hot
work stays inside CPython's C internals and not in Python bytecode.

## What it means

Python's speed is binary: C-fast when the loop is a builtin, ~50x slower the
moment the loop is yours. Java is C-class warm, but pays JVM startup plus JIT
warmup on every invocation, which a short-lived CLI never amortizes.

The catch for ais: its intersection is a STREAMING k-way merge of sorted posting
lists, memory O(number of keys), holding only each list's head. There is no
Python builtin for "merge N sorted streams". The choices are the hand-written
loop (the ~72 ms, ~55x) or `set &`, which is a hash join that loads every id of
every key into hash tables and abandons the bounded-memory streaming that lets
ais answer a 270k-hit key in ~2 s without holding it all in RAM. The very design
that makes ais scale on bounded memory is the design Python can only run slowly,
or only by becoming a different, memory-heavier program.

So: for batch, vectorizable, builtin-shaped work Python is fine and Java matches
C. For a short-lived CLI running hand-written streaming loops on every command,
the order is C, then warm Java, then Python-with-builtins, then hand-written
Python far behind. ais is the second category, which is why it is C.
