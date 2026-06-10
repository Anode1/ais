# Why plain text, and why it is fast

A recurring objection: "plain text is a relic; a real index needs a binary
database." This is the rebuttal, with measurements, not opinion.

## It is not the 1980s

The store is line-oriented plain text, read by streaming. On a modern CPU and
SSD that is not slow; it is grep/cat class, I/O-bound at hundreds of MB/s. The
numbers below are one desktop core over a 1,000,000-record store (85 MB). Full
table and reproduction steps: `../../performance.txt`.

    full scan (find, substring)       1.6 s     grep class
    dump every record                 1.8 s     cat class
    build the whole index (compact)   7.9 s     one streaming pass
    single put at n = 1M             0.05 s
    recall a rare key                  9 ms     via the id->offset index
    recall a hot key (270k hits)      2.2 s

Reads are O(matches) seeks through the id->offset index, or one streaming pass.
Nothing scans superlinearly at query time.

## The data is bounded by a human life, not by Moore's law

This is a *personal* memory, so estimate its ceiling. A person files on the
order of 100 to 1,000,000 things in a whole lifetime (a few dozen a day for ~70
years is about a million). So 1M records is not a waypoint we must scale past;
it is the upper bound, and AIS already answers it in milliseconds to a couple of
seconds. The key space is bounded too: human active vocabulary is ~10-20k words,
so distinct keys stay few (the 1M test had 4,000). That bound is also why the
index shards shallowly: keys live in `idx/<first-letter>/<key>`, navigable and
hash-free, and one fixed level keeps every bucket small enough to `ls`. Deeper,
adaptive splitting (re-shard a bucket by the next letter once it grows hot) is
available as an option, but at human scale it is not needed. (The early 2001
versions balanced the tree deeply because filesystems then, Windows on
first-generation Pentiums, slowed with only hundreds of files in one directory.
Modern filesystems handle thousands without trouble, so the shallow scheme is a
deliberate correction, not an oversight. Noted here to prevent re-introducing
balancing that the hardware no longer requires.) The "it won't scale" critique
assumes web scale; one person's memory is not web scale and never will be.

## Unix tools and hand-editing are the point, not a fallback

Because it is plain text, the whole toolbox already works on it:

    grep, sed, awk, sort, cat, less     query and slice the store directly
    rsync, git                          replicate, version, branch it
    any text editor                     repair it by hand

A binary index forfeits all of that and adds a parser you must trust. Plain text
also degrades gracefully: a damaged byte costs one line, not the database. The
format is at once the API, the backup, and the documentation (see the shipped
example index).

## What we gave up, honestly

Raw throughput at web scale, and sub-millisecond hot-key recall without warming
file handles. Neither matters for one person's memory. The hard limits and the
one known O(n^2) (bulk import) are in `../../limitations.txt`; the locking model
is in `LOCKING.md`.
