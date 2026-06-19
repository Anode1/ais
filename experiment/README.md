# experiment: index vs. file search, measured

A small, self-contained harness behind the paper *Compress the Access, Not the
Store*. It runs an agent twice on the same questions, once with an **AIS index**
(recall by exact keys), once with **file search** (grep + read), and records the
tokens each spends.

The paper's headline numbers come from a separate **private ~100k-line code
project**, which cannot be shipped. This bundle reproduces the *method* on a
relatable, public corpus instead: a small **photo library**.

## Why a photo library

Photos are the textbook case for keys over folders. A picture is *Mary* **and**
*Mother* **and** *Hawaii* **and** *2019* at once; no folder tree can hold that
without picking one parent and losing the rest. You ask by any combination of
facets and get back the items under *all* of them.

In AIS, listing several keys defaults to AND (intersection): `mary mother photo`
recalls every item filed under all three, zero, one, or many, and nothing else.
Media type is just another facet, and *photo* and *video* are mutually exclusive,
so `peter video` returns only Peter's videos, no photos and no documents, the way
anyone would expect. People already know how to combine keys.

Each "photo" here is a one-line caption file in `library/` so the file-search arm
has real text to grep (grep cannot read image pixels). The contrast:

```
index   mary mother photo  -> the photos with both Mary and Mother (AND is the
                              default): zero to many, and never a video.
grep    "Mary"             -> ADDS a video and a cake ("Birthday cake for Mary",
                              no Mary in frame) and MISSES a photo of Mary
                              captioned only "Beach day".
```

Exact keys are precise and self-verifying (a wrong key returns the empty set);
content search guesses, and both over- and under-shoots.

## Files

- `library/` -- the corpus: one caption file per photo/video (what `grep` searches).
- `index/` -- the AIS index over the same items, filed under faceted keys.
- `questions.json` -- natural-language questions and their expected items.
- `run.py` -- the harness (agent loop; needs an Anthropic API key, spends tokens).
- `analyze.py` -- summary, per-question table (`--latex`), sanitized CSV export.
- `server.py` -- the MCP plugin (exposes the index's recall/keys as tools).
- `results_repeats_sanitized.csv` -- the paper's deposited run (8 questions x 5
  repeats), with the free-text answer column removed.

## Run it

Build `ais` first (`make` in the repo root). Then, from this directory:

```sh
export ANTHROPIC_API_KEY=sk-ant-...

python3 run.py --ais ../ais --index index --search-root library \
  --questions questions.json --ais-no-read --repeats 5 --out results.csv

python3 analyze.py --csv results.csv --out chart.png \
  --latex table.tex --sanitized-csv results_sanitized.csv
```

Regenerate the paper's table from the deposited data, no API key needed:

```sh
python3 analyze.py --csv results_repeats_sanitized.csv
```

## Inspect the index directly (no model, no tokens)

```sh
ais -f index --keys                 # the facet vocabulary
ais -f index mary mother photo      # the intersection
ais -f index peter video            # videos of Peter
```
