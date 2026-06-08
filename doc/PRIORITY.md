# Priority & Provenance Record

**Author:** Vasili Gavrilov -- ORCID [0009-0007-9371-5994](https://orcid.org/0009-0007-9371-5994)
**Handles:** GitHub `Anode1`, LiveJournal `siberean`, SourceForge `vgavrilov`
**Date of this record:** 2026-06-07
**Status:** living document. Intended to be deposited to Zenodo (for a DOI), anchored with OpenTimestamps (blockchain proof-of-existence), and committed to git (signed tags). References third-party-dated evidence wherever possible.

---

## 0. Purpose and scope

This document is a dated, citable record of authorship and conceptual priority for a set of ideas the author developed over ~1997-2026, together with a verifiable trail of artifacts.

**What it establishes:** intellectual and historical priority -- a credible, timestamped record that the author articulated these ideas by the stated dates.

**What it does NOT claim:** patent rights. The patent route is closed (public disclosure, decades elapsed, independent commercial implementations now exist). This record is about *credit and provenance*, not exclusivity.

**Evidence tiers** (used in the trail below):
- **(A) Independently verifiable / third-party-dated** -- strongest (e.g., a registration date on a service the author does not control).
- **(B) Self-authored dated artifact** -- a file, paper, or post the author created (date asserted by the artifact itself).
- **(C) Attested** -- supported by the author's testimony and/or named witnesses, not by an independent record.

---

## 1. Claimed ideas, with earliest dates

### 1.1 AIS -- Associative Indexing Service
An associative, key->content personal-memory store: a "personalised Google" over one's own information. Keys (keywords) index **content** that is a URI or free text, and a record may carry several links -- a *set* of URIs / a small **graph** of related concepts. Retrieval is by **union and intersection of keys**. The storage backend is intended to be pluggable (filesystem, key-value store, RDB, Lucene, SQLite). First conceived **~2001** (Tier C; see the 2004 third-party anchor in section 2).

### 1.2 Filesystem-as-index -- manual implementation of AIS
Before AIS was a program, the author ran it **by hand on the filesystem**: an `INDEX/` directory tree (`A/`, `B/`, ...), **filenames as keys**, file contents being a list of URIs/links forming a graph. When a directory exceeded a threshold (~100 keys) -- on early-Pentium, pre-B-tree-filesystem hardware -- he **split it into subdirectories, manually balancing the tree** for logarithmic access. The **content archive was immutable** (photos, documents, books -- write-once) and copied verbatim across **4 redundant disks (3-2-1+)**; only the small `INDEX/` was tended. (This independently prefigures: Git's hash-prefix object sharding; the plain-text linked-note / Zettelkasten model; and the LOCKSS "lots of copies" preservation principle.) Practised from **~2001** (Tier C). The underlying problem -- long-term preservation and format compatibility of life archives -- is itself a named grand challenge in computer science (see section 6 and References).

### 1.3 Wearable / "glasses" ambient personal memory
A head-worn capture-and-recall device feeding a personal associative memory -- conceived **~2001-2006**, years before Google Glass (developer "Explorer Edition", 2013, which the author considered purchasing). (Tier C; corroborated by witnesses and the 2001 VC discussions, section 3.)

### 1.4 Compression-as-intelligence
The thesis that intelligence is the discovery of compressors whose inductive bias fits the data; that "the compressor function is the understanding"; substrate-independence of emergence. Developed across the author's work and formalised in the 2026 papers (section 2). (Tier B.)

### 1.5 Content-addressed, scattered-memory recovery
A scheme for surviving institutional mortality: encrypt memories, address them by content hash, disperse redundant copies across the public commons, and recover them by a compact local set of hooks/keys -- independent of any single company's survival. Recorded here **2026** (Tier B).

---

## 2. Verifiable artifact trail (chronological)

| Date | Artifact | Location | Tier | Establishes |
|---|---|---|---|---|
| 1997 | Undergraduate seminar thesis: backpropagation feed-forward neural networks (architecture-as-prior / "bionics") | `Anode1/BPFNN_Coursework` (re-rendered); [TODO: original 1997 copy / advisor Prof. Ehud Gudes attestation] | B/C | early architecture-as-inductive-bias view |
| 2001 | ANSI C key-value / "maps" PoC (`aisconfig`, a.k.a. "confi"), source headers (c) 2001 | github.com/Anode1/aisconfig | B | AIS engine begun in C |
| 2001 | SMBPANN design notes (self-modifying backprop NN) | github.com/Anode1/SMBPANN; [TODO: confirm dated copy] | B | dated design work |
| ~2001 | Two venture-capital pitch meetings for the personal-memory / wearable concept | [TODO: firm(s), dates, any email/letter] | C | idea pitched to third parties in 2001 |
| **2004-11-22** | **AIS registered on SourceForge** | https://sourceforge.net/projects/ais/ | **A** | **strongest third-party date anchor** |
| **2005-01-28** | AIS shell-script implementation (`ais-scripts-0.5`): filesystem-sharded plain-text INDEX, `get`/`put`/`delete`, under CVS (handle `sourcer`) | SourceForge `files/old/ais-scripts/`; recovered to `legacy/ais-scripts/` (CVS `$Id` tags dated 2005-01-28) | A | **earliest working AIS code**; the by-hand text-index practice wrapped into scripts; predates the 2009 Java/Lucene release |
| 2009-08 | AIS source/binary releases (Lucene 2.4.1, Jetty, Swing/CLI/web) | https://sourceforge.net/projects/ais/files/ | A | working implementation, dated |
| 2012 | LiveJournal article (conditional probability; same author/handle) | https://siberean.livejournal.com/16220.html | A | dated, third-party-hosted authorship |
| [TODO] | LiveJournal article describing the AIS / indexing approach | [TODO: exact URL + date] | A | the "approach" described publicly, dated |
| [TODO] | Facebook article(s) on the indexing approach | [TODO: URLs + dates] | A/B | later public description |
| 2013 | AIS (Associative Indexing Service) personal indexing tool | per 2026 papers; [TODO: artifact] | B/C | extended-memory tool in use |
| 2025 | Context Renormalization protocol | github.com/Anode1/context-renormalization (releases) | A/B | bounded-context compression protocol |
| 2026 | Paper: *Emergence Does Not Care About Substrate* | currently Substack (popular but **volatile** -- not a citation anchor); formal DOI deposit planned | B | compression/emergence thesis |
| 2026 | Paper: *Intelligence Is the Discovery of Compressors* | currently Substack (volatile); to be consolidated into a formal, DOI'd paper | B | full synthesis |
| 2022-05-13 | Internet Archive snapshot of the SourceForge AIS project page (pre-existing third-party capture) | http://web.archive.org/web/20220513005530/https://sourceforge.net/projects/ais/ | A | independent archived copy of the AIS page, well predating this record |
| 2026-06-07 | Save-Page-Now captures attempted for the SF files page + LiveJournal post | not yet confirmed (Save-Page-Now lag/throttle on those hosts); retry pending | -- | (in progress) |

Supporting public repositories (GitHub `Anode1`): `ais`, `aisconfig`, `ansi_c_conversion_template`, `nid`, `SMBPANN`, `context-renormalization`, `notes`, `aisgedcom`, `ped2raw`, `aisconvert`.

---

## 3. Corroborating testimony (Tier C)

The following people were told these concepts (AIS / personal associative memory / wearable capture) at or before the dates indicated. Short signed, dated statements from any of them materially strengthen this record.

- [TODO: Witness 1 -- name/initials, role, approx. year first told]
- [TODO: Witness 2 -- ...]
- [TODO: Witness 3 -- ...]
- [TODO: Witness 4 -- ...]
- Spouse -- has known the concept since [TODO: year].

**2001 venture-capital meetings.** The author presented the personal-memory / wearable concept to [TODO: VC firm(s) / individuals] in [TODO: dates], over two meetings, and was advised on the missing business case (business plan, revenue model, return on investment). Any surviving email, calendar entry, or pitch document from that period is Tier-A evidence and should be attached.

---

## 4. How this record is being made durable

1. **Internet Archive (Save Page Now)** -- a pre-existing third-party snapshot of the SourceForge AIS project page is **confirmed** (2022-05-13; see section 2). Fresh captures of the SourceForge *files* page and the LiveJournal post were attempted 2026-06-07 but are **not yet confirmed** (Save-Page-Now lag/throttling on those hosts); retry pending.
2. **Zenodo deposit** -- this document + a zip of the cited posts/screenshots/code, to obtain a **DOI** and a fixed deposit timestamp. (A submission packet already exists in `articles/intelligence_compressors/zenodo_submission_packet.txt`.)
3. **OpenTimestamps** -- anchor the SHA-256 of this document into the Bitcoin blockchain: an institution-independent proof that it existed by its stamp date (survives the death of any single service).
4. **git** -- commit and **GPG-signed tags** in the `ais` repository; push to multiple remotes.

> Integrity note: when finalised, record this file's hash here ->
> `SHA-256: [TODO: sha256sum PRIORITY.md]`

---

## 5. Honest limitations

- This establishes **priority of authorship and a citable provenance record**, not patent or other exclusive rights.
- Pre-2004 conception dates (sections 1.1-1.3) currently rest on **author testimony and (pending) witness statements** (Tier C); the **2004-11-22 SourceForge registration** is the earliest **independent** anchor for AIS specifically.
- Some artifacts exist today only as **re-rendered or re-hosted** copies; original-date corroboration is marked [TODO] above.
- Self-authored dated files (Tier B) are weaker than third-party-dated records (Tier A); the durability steps in section 4 are intended to convert this record itself into a Tier-A, citable, permanent artifact.

---

## 6. Context -- the named research problem (prior art)

The problem the author was addressing by hand in section 1.2 -- long-term preservation of personal/life archives and the compatibility of formats over decades -- was independently identified as a grand challenge in computer science. The author encountered at least two of the three formulations below (which one(s) is no longer recalled; all three are cited as the recognized framing of the problem). This situates the manual practice and the AIS design against an acknowledged, still-unsolved problem, and shows the work was aimed at it deliberately rather than incidentally.

- **Gray (1999/2003)** posed long-term storage and a personal "Memex"-like memory among a dozen long-range IT grand challenges.
- **The UKCRC "Memories for Life" Grand Challenge (2003; reviewed 2006)** named exactly the management of a lifetime of personal digital memories across format change.
- **Rothenberg (1995)** framed the canonical "the bits survive but nothing can read them" longevity / format-obsolescence problem.

---

## References

*Citation policy: cite stable DOIs where already published; the author's Substack essays are popular but volatile and are not used as citation anchors -- a consolidated formal paper (with a DOI) including these ideas is planned. DOIs below are marked [verify] where recalled from memory and should be confirmed before formal deposit.*

1. Gray, J. (2003). "What Next? A Dozen Information-Technology Research Goals." *Journal of the ACM* 50(1): 41-57. Originally the 1998 ACM A. M. Turing Award Lecture; also Microsoft Research Technical Report MSR-TR-99-50 (June 1999). DOI: 10.1145/602382.602401 [verify].
2. O'Hara, K., Morris, R., Shadbolt, N., Hitch, G. J., Hall, W., & Beagrie, N. (2006). "Memories for life: a review of the science and technology." *Journal of the Royal Society Interface* 3(8): 351-365. Arising from the UKCRC "Memories for Life" Grand Challenge (2003). DOI: 10.1098/rsif.2006.0125 [verify].
3. Rothenberg, J. (1995). "Ensuring the Longevity of Digital Documents." *Scientific American* 272(1): 42-47. Expanded as a CLIR report, "Ensuring the Longevity of Digital Information" (1998/1999). DOI: 10.1038/scientificamerican0195-42 [verify].

---

*Prepared with assistance from an AI collaborator (Claude), 2026-06-07. Facts marked [TODO] are to be supplied or verified by the author; DOIs marked [verify] should be confirmed before formal deposit; all other entries are drawn from the author's existing public artifacts.*
