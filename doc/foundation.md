# Chaos Makes Many, Compression Keeps Few

*A plain account of where innovation comes from, and the human part.*

Vasili Gavrilov. Markdown of the essay; the typeset PDF lives in
`articles/innovation_compression/`. This is the conceptual foundation note for the AIS project.

> **Status:** a synthesis. The parts below are established and named; the contribution is the
> assembly, not the parts. Closest neighbours to credit up front: **Sperber** (culture as contagion
> with attractors) and **Schmidhuber** (interest as compression).

## The picture in one breath
Something generates a huge number of candidate patterns. Almost all are junk. A few are kept. The
ones kept are short to describe and fit a structure many people already share, so they catch on and
spread. The making is cheap and can be mechanised. The keeping needs a *prior* — a sense of which few
are worth it — and that sense is the scarce part. The frame throughout: intelligence is the discovery
of compressors, things that describe a lot with a little (Gavrilov, *Intelligence Is the Discovery of
Compressors*).

## 1. Many are made
A simple rule run over and over produces patterns of unlimited variety; this is what chaos and
nonlinear dynamics do (Gavrilov, *Emergence Does Not Care About Substrate*). Ideas also combine: an
innovation is usually old pieces in a new arrangement, and the number of combinations grows
exponentially (Weitzman 1998, *Recombinant Growth*); each new one opens the door to more, the
"adjacent possible" (Kauffman 2000). The supply of candidates is effectively endless. That is the
easy part.

## 2. Few are kept
Out of that endless supply, only a tiny, countable handful of forms turn out to matter, and they can
often be *enumerated*:

- For how smooth systems jump, Thom showed exactly **seven** basic shapes (the elementary
  catastrophes) for up to four controlling factors (Thom 1975); Arnold extended the classification to
  caustics (Arnold 1984).
- Wolfram ran the simplest programs by the thousands: they fall into **four classes**, and only a
  couple are rich — Rule 110 (universal) and Rule 30 (a randomness source) (Wolfram 2002).
- In physics, very different materials near a critical point behave identically: a few **universality
  classes** (the renormalization-group fact already in the compressor account).

Chaos makes endlessly many; only a few canonical forms survive, and writing down that short list is
itself compression.

## 3. What "kept" means: it compresses
The kept ones are short to say and explain a lot — they capture a structure shared across many cases.
That is what a good compressor does. Schmidhuber makes this precise: a pattern is *interesting* when
noticing it lets you describe the world more briefly than before — compression progress (Schmidhuber
2010). "Resonates with many" and "compresses a shared structure" are the same statement from two
sides.

## 4. Who keeps them: the crowd, in parallel
The sifting is done by a whole population at once — in effect a machine with many processors side by
side (MIMD). Candidates spread like a contagion; Sperber calls culture an *epidemiology of
representations* settling on shared *attractors* (Sperber 1996), and Rogers mapped the S-curve of how
an innovation diffuses (Rogers 2003). This is the engine of cultural evolution — useful patterns
inherited without each person rediscovering them (Boyd & Richerson 1985; Dawkins 1976). A candidate is
"selected" when it resonates, i.e. when it compresses something many already half-hold.

## 5. The human part: the rare prior that points
As machines get better, what stays scarce? Split the work into two terms, as statisticians split a
model's error (Geman et al. 1992):

- **Variance** — how widely you explore/search. Machines are very strong here, and an entire
  programme (open-ended search, novelty search, quality-diversity) makes them generate endless novelty
  on their own (Stanley & Lehman 2015). Generation is becoming a machine commodity.
- **Bias** — the prior, the assumptions you commit to before looking: which candidate is worth keeping
  *here*. This cannot come from the data alone: averaged over all problems no method beats another, so
  all advantage comes from a prior matched to the problem (No-Free-Lunch; Wolpert 1996). Supplying it
  is the scarce contribution — cheap machine prediction makes human judgement more valuable (Agrawal,
  Gans & Goldfarb 2018).

A good prior is paid for slowly: over evolution, at birth, across history, and over a lifetime. A
cat's nervous system is a prior tuned tightly to its niche; a human's is looser and still
experimenting, which is why humans keep finding new things. The striking cases — Ramanujan *seeing*
identities later checked true; Bach and Mozart hearing structure others did not — were not blind
guesses. This is the disagreement worth stating: the standard story says creative variation is
*blind* and the human only selects afterward (Campbell 1960); these cases suggest the rare prior makes
the variation itself *non-blind*, aiming the search before any selection.

So, plainly: **innovation is a high-conviction prior that resonates with many.** A strong, committed,
often contrarian prior (Thiel's "secret" — a truth few yet share, 2014; or a Kuhnian paradigm shift,
1962) is the generative bet; resonance (spread across the parallel crowd) verifies the bet caught a
real shared structure. (Precision: "high-conviction prior" = a *strong, narrow assumption* / strong
inductive bias, **not** the textbook "high bias" that means plain error.) Machines supply cheap,
abundant, increasingly open-ended *variation*; humans supply the rare, expensive, non-blind *prior*
that points it. Together they invent what neither does alone.

## 6. The tools that help
A prior is worth having only if it can be kept and reused — stored in genes, in culture, and in
external artifacts (notes, code, archives). External storage lets a prior *compose* across people and
years, which is why human knowledge accumulates and a single animal's does not. Tools that pack
accumulated knowledge into a compact, recallable form extend the prior. **AIS** is one such tool: a
personal, plain-text store where a short key addresses a body of content, so recall by a short key is
itself a small compression (cf. Gavrilov, atree). On the machine side, the author has called for a stable, human-controlled ledger for an AI's working
knowledge (Gavrilov, *Context Renormalization*), and proposes a version built on AIS: a collection of
plain-text files, each reached by a short key. The content can sit anywhere, even on the open web,
held by reference (a URL), so one store indexes a person's files and the web alike, and serves both a
person's notes and an AI's working knowledge. What is personal and scarce is not the content but the
*index* over it — the person's own associations, a living map of concepts grown to fit present
understanding, not adopted ready-made: the bias made concrete. Control is human, not the model's:
what the ledger keeps or drops is a human decision (append-only like a lifetime archive, or curated
by hand), unlike a running context a machine compacts by algorithm. The structure is not new
(associative and graph memories exist); what is proposed is the specific human-curated, plain-text,
reference-indexed form, and the point that the index is the bias. None of
these manufacture the prior; they lower the cost of keeping the scarce thing.

## What is new here, and what is not
Every block above is established and named: combinatorial/recombinant growth (Weitzman; Kauffman);
the collapse to a few canonical forms (Thom; Arnold; Wolfram); compression as the mark of interest
(Schmidhuber); culture as parallel contagion with attractors (Sperber; Rogers); variation and
selection (Campbell; Boyd & Richerson); the bias–variance split (Geman et al.); No-Free-Lunch
(Wolpert); cheap prediction, scarce judgement (Agrawal et al.). Two things are claimed: the **assembly** —
seeing all of these as one process under a single lens — and one concrete **proposal**, the
human-controlled, plain-text, reference-indexed ledger of §6 (its structure is not new; the specific
stable, human-curated form and the index-is-bias point are). No part is a new theorem.

## Closing
To write this is to take many separate, named results and squeeze them into one short structure. That
act — replacing a lot with a little that fits — is the very thing it says innovation is. The map is an
instance of the thing it maps.

---

### References
Agrawal, Gans & Goldfarb (2018), *Prediction Machines* · Arnold (1984), *Catastrophe Theory*; (1973), *Ordinary Differential Equations* · Boyd &
Richerson (1985), *Culture and the Evolutionary Process* · Campbell (1960), *Psychological Review*
67(6) · Dawkins (1976), *The Selfish Gene* · Geman, Bienenstock & Doursat (1992), *Neural Computation*
4(1) · Kauffman (2000), *Investigations* · Kuhn (1962), *The Structure of Scientific Revolutions* ·
Rogers (2003), *Diffusion of Innovations* · Schmidhuber (2010), *IEEE TAMD* 2(3) · Sperber (1996),
*Explaining Culture* · Stanley & Lehman (2015), *Why Greatness Cannot Be Planned* · Thiel & Masters
(2014), *Zero to One* · Thom (1975), *Structural Stability and Morphogenesis* · Weitzman (1998), *QJE*
113(2) · Wolfram (2002), *A New Kind of Science* · Wolpert (1996), *Neural Computation* 8(7) · Gavrilov
(self): *Intelligence Is the Discovery of Compressors*; *Emergence Does Not Care About Substrate*;
*Machines Can Generalize. Humans Still Innovate.*; atree (Zenodo concept DOI 10.5281/zenodo.20587715, latest version); *Context
Renormalization*.
