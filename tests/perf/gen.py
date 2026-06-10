#!/usr/bin/env python3
"""gen.py -- synthetic dataset generator for the AIS associative index.

Writes ONLY the three files AIS can rebuild an index from:
    <DIR>/store    append-only records, one per line: "id|keys|value"
    <DIR>/next_id  a single line, the next id to assign (= N+1)
    <DIR>/meta     one line per record: "id|epoch_seconds" (not read by ais)

It does NOT write idx/ -- run `ais -f <DIR> -y compact` afterwards, which
rebuilds the posting lists from the store with AIS's own logic.

Usage:   gen.py DIR N
Example: gen.py /tmp/ais_1m 1000000

Determinism: a fixed RNG seed (SEED below) makes the whole dataset
reproducible. Re-running with the same DIR/N yields byte-identical files.

------------------------------------------------------------------ data model
VALUES  File paths found by walking KUL_ROOT (read-only), taken RELATIVE to
        KUL_ROOT, '.git' pruned. Sampled with replacement. '|' is stripped
        from a path (AIS forbids '|' in a value); paths never contain newlines.
        If the pool is empty we fall back to a synthetic "value/<i>" path.

KEYS    1 or 2 keys per record (P(2 keys) = 0.8). Vocabulary: lowercase-ASCII
        words from /usr/share/dict/words (filtered to [a-z]+, deduped, capped
        at VOCAB_MAX). If the dict is absent, words are mined from KUL path
        components instead. Lowercase ASCII keeps AIS's key_encode an identity
        (tolower + space->'_'), so keys map 1:1 to idx/<prefix>/<key> files.

        Keys are drawn from a SKEWED (Zipf) distribution: word ranked r
        (1-based) has probability proportional to 1 / r**ZIPF_S. With
        ZIPF_S = 1.1 over a few thousand words, the top handful of words land
        in a large fraction of records (huge posting lists) while the long
        tail is rare -- the realistic shape that stresses get/merge.

META    "id|epoch" with epoch MONOTONICALLY NON-DECREASING in id. Timestamps
        start at META_START (1996-01-01) and advance by a small random gap per
        record so the whole run spans ~META_YEARS years ending near 2026, at a
        few-to-dozens of records per day. meta is therefore time-sorted and an
        awk date-range scan over it is a clean monotone slice.

The script prints a summary to stderr: pool sizes, the chosen hot/rare keys,
and the realised posting-size of the hottest key (so the benchmark knows which
key to query).
"""
import os
import sys
import random
import re

# ------------------------------------------------------------------- constants
KUL_ROOT   = "/home/vas/kul"
DICT_PATH  = "/usr/share/dict/words"
SEED       = 20260606          # fixed: reproducible dataset
VOCAB_MAX  = 4000              # distinct key words (a few thousand)
ZIPF_S     = 1.1               # power-law exponent; >1 -> a few very hot keys
P_TWO_KEYS = 0.8               # most records carry 2 keys
META_START = 820454400         # 1996-01-01 00:00:00 UTC, in epoch seconds
META_YEARS = 30                # span ~1996..2026


def collect_values(root):
    """Relative file paths under ROOT (read-only walk), '.git' pruned."""
    vals = []
    rootlen = len(root.rstrip("/")) + 1
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for fn in filenames:
            rel = os.path.join(dirpath, fn)[rootlen:]
            rel = rel.replace("|", "")        # AIS forbids '|' in a value
            if rel:
                vals.append(rel)
    return vals


def load_vocab(values):
    """Lowercase-ASCII key words: from the dict if present, else mined from
    KUL path components. Deduped, sorted (stable rank), capped at VOCAB_MAX."""
    words = []
    if os.path.exists(DICT_PATH):
        with open(DICT_PATH, encoding="utf-8", errors="ignore") as f:
            for line in f:
                w = line.strip()
                if re.fullmatch(r"[a-z]+", w):
                    words.append(w)
    if not words:                              # fallback: mine from paths
        seen = set()
        for v in values:
            for tok in re.split(r"[^a-z]+", v.lower()):
                if tok and tok not in seen:
                    seen.add(tok)
                    words.append(tok)
    words = sorted(set(words))
    if len(words) > VOCAB_MAX:
        # deterministic subsample preserving order, so ranks are stable
        step = len(words) / VOCAB_MAX
        words = [words[int(i * step)] for i in range(VOCAB_MAX)]
    return words


def zipf_weights(n, s):
    """Unnormalised Zipf weights for ranks 1..n: w[r-1] = 1 / r**s."""
    return [1.0 / (r ** s) for r in range(1, n + 1)]


def main():
    if len(sys.argv) != 3:
        sys.exit("usage: gen.py DIR N")
    out_dir = sys.argv[1]
    n = int(sys.argv[2])
    os.makedirs(out_dir, exist_ok=True)

    rng = random.Random(SEED)

    values = collect_values(KUL_ROOT)
    if not values:
        values = ["value/%d" % i for i in range(1000)]   # safety fallback
    vocab = load_vocab(values)
    weights = zipf_weights(len(vocab), ZIPF_S)

    # Track posting sizes so we can report a hot and a rare key afterwards.
    key_count = [0] * len(vocab)

    # Time advances by a small per-record gap; total span ~= META_YEARS.
    span = META_YEARS * 365 * 24 * 3600
    mean_gap = span / n                       # seconds/record on average

    store_path = os.path.join(out_dir, "store")
    meta_path  = os.path.join(out_dir, "meta")

    epoch = META_START
    with open(store_path, "w", encoding="utf-8") as sf, \
         open(meta_path, "w", encoding="utf-8") as mf:
        for i in range(n):
            rid = i + 1
            # --- keys: 1 or 2, Zipf-drawn, distinct within the record -------
            k = 2 if rng.random() < P_TWO_KEYS else 1
            idxs = []
            while len(idxs) < k:
                j = rng.choices(range(len(vocab)), weights=weights, k=1)[0]
                if j not in idxs:
                    idxs.append(j)
            for j in idxs:
                key_count[j] += 1
            keys = " ".join(vocab[j] for j in idxs)
            # --- value: a sampled relative path -----------------------------
            val = values[rng.randrange(len(values))]
            sf.write("%d|%s|%s\n" % (rid, keys, val))
            # --- meta: monotonic epoch, small random forward gap ------------
            # exponential-ish gap (a few to dozens/day); never decreases.
            epoch += int(rng.expovariate(1.0 / mean_gap))
            mf.write("%d|%d\n" % (rid, epoch))

    with open(os.path.join(out_dir, "next_id"), "w", encoding="utf-8") as nf:
        nf.write("%d\n" % (n + 1))

    # --------------------------------------------------------- report to stderr
    ranked = sorted(range(len(vocab)), key=lambda j: key_count[j], reverse=True)
    hot = ranked[0]
    # a "rare" key that still appears at least once, near the tail
    rare = next((j for j in reversed(ranked) if key_count[j] > 0), ranked[-1])
    nonzero = [c for c in key_count if c > 0]
    nonzero.sort()
    med = nonzero[len(nonzero) // 2] if nonzero else 0
    over100k = sum(1 for c in key_count if c > 100000)
    sys.stderr.write(
        "GEN done: N=%d  values_pool=%d  vocab=%d  distinct_keys_used=%d\n"
        "  hot_key=%r count=%d   rare_key=%r count=%d\n"
        "  posting median=%d  keys>100k=%d  epoch_first=%d epoch_last=%d\n"
        % (n, len(values), len(vocab), len(nonzero),
           vocab[hot], key_count[hot], vocab[rare], key_count[rare],
           med, over100k, META_START, epoch))


if __name__ == "__main__":
    main()
