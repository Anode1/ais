# Priority & Provenance Record

**Author:** Vasili Gavrilov -- ORCID [0009-0007-9371-5994](https://orcid.org/0009-0007-9371-5994)
**Handles:** GitHub `Anode1`, LiveJournal `siberean`, SourceForge `vgavrilov`
**Date of this record:** 2026-06-07
**Status:** living document. Intended to be deposited to Zenodo (for a DOI), anchored with OpenTimestamps (blockchain proof-of-existence), and committed to git (signed tags). References third-party-dated evidence wherever possible.

---

## 0. Purpose and scope

This document is a dated, citable record of authorship and conceptual priority for a set of ideas the author developed over ~1993-2026, together with a verifiable trail of artifacts.

**What it establishes:** intellectual and historical priority -- a credible, timestamped record that the author articulated these ideas by the stated dates.

**What it does NOT claim:** patent rights. The patent route is closed (public disclosure, decades elapsed, independent commercial implementations now exist). This record is about *credit and provenance*, not exclusivity.

**Evidence tiers** (used in the trail below):
- **(A) Independently verifiable / third-party-dated** -- strongest (e.g., a registration date on a service the author does not control).
- **(B) Self-authored dated artifact** -- a file, paper, or post the author created (date asserted by the artifact itself).
- **(C) Attested** -- supported by the author's testimony and/or named witnesses, not by an independent record.

---

## 1. Claimed ideas, with earliest dates

### 1.1 AIS -- Associative Indexing Service
An associative, key->content personal-memory store: a "personalised Google" over one's own information. Keys (keywords) index **content** that is a URI or free text, and a record may carry several links -- a *set* of URIs / a small **graph** of related concepts. Retrieval is by **union and intersection of keys**. The storage backend is intended to be pluggable (filesystem, key-value store, RDB, Lucene, SQLite). First conceived **~2001** (Tier C; see the 2004 third-party anchor in section 2). This externalized, plain-text, key-addressed approach to a person's (and an AI's) working context predates the industry's per-session instruction- and skill-file approach (Anthropic's Agent Skills / `SKILL.md`, standardized 2025); the ledger proposal is offered as a stable, human-controlled alternative, **not** a claim to have originated skill files.

### 1.2 Filesystem-as-index -- manual implementation of AIS
Before AIS was a program, the author ran it **by hand on the filesystem**: an `INDEX/` directory tree (`A/`, `B/`, ...), **filenames as keys**, file contents being a list of URIs/links forming a graph. When a directory exceeded a threshold (~100 keys) -- on early-Pentium, pre-B-tree-filesystem hardware -- he **split it into subdirectories, manually balancing the tree** for logarithmic access. The **content archive was immutable** (photos, documents, books -- write-once) and copied verbatim across **4 redundant disks (3-2-1+)**, kept in step by redundant synchronization (`rsync -avu --delete` locally); over the years **three disks failed with zero byte lost**, and only the small `INDEX/` was tended. (This independently prefigures: Git's hash-prefix object sharding; the plain-text linked-note / Zettelkasten model; and the LOCKSS "lots of copies" preservation principle.) The seed -- storing **metadata** held as **directory names** -- dates to the **1993-97** CS coursework (section 2); practised systematically from **~2001** (Tier C). Various sorted index organizations, and even relational databases, were tried for the index along the way; **immutability of the contents** (the `INDEX` design) was the choice settled on **~2001** [TODO: file mtimes may date it earlier]. The underlying problem -- long-term preservation and format compatibility of life archives -- is itself a named grand challenge in computer science (see section 6 and References).

### 1.3 Wearable "glasses + gloves" front-end to AIS
A head-worn recall surface plus a hands-free capture device feeding the personal associative memory. On the **announcement of Google Glass** (developer "Explorer Edition", 2013 -- US$1500, with an initial US$1000 promotional tier; the author considered buying one), the author immediately pictured **AIS hooked into the glasses for GET** -- recall a person's details the moment you meet them -- and, for the harder half, **PUT** (entering a name at the instant it is spoken, without opening a laptop), imagined a **gamer's-style glove** as the input device. This concrete wearable framing dates to the **Google Glass announcement (~2013)** -- i.e. *after* the ~2007 AIS pitch (section 3), not before it -- and was discussed informally and repeatedly with colleagues (L.L. and L.R., M.G., others, and the author's manager). (Tier C; see section 3.)

### 1.4 Compression-as-intelligence
The thesis that intelligence is the discovery of compressors whose inductive bias fits the data; that "the compressor function is the understanding"; substrate-independence of emergence. Developed across the author's work and formalised in the 2026 papers (section 2). (Tier B.)

### 1.5 Content-addressed, scattered-memory recovery
A scheme for surviving institutional mortality: encrypt memories, address them by content hash, disperse redundant copies across the public commons, and recover them by a compact local set of hooks/keys -- independent of any single company's survival. Content-hash addressing itself is **not** claimed as novel -- it is widely prior (content-addressable stores), and a peer independently used MD5 content-identifiers in 2004 (see section 3); the claim here is the *composition*: encrypt, disperse across the public commons, and recover by a compact local key-set. Recorded here **2026** (Tier B).

---

## 2. Verifiable artifact trail (chronological)

| Date | Artifact | Location | Tier | Establishes |
|---|---|---|---|---|
| 1986-92 | Studied Physics, Kazan University (UofT certified the study as equivalent to a BSc); first applied C, interfacing NMR/MRI instruments | [TODO: any surviving records] | C | physics + CS foundation; first C in a medical setting |
| 1993-97 | Degree in CS. Built a dBASE-like database by hand (C structs, blobs, a binary format) -- the binary experience behind AIS's deliberate plain-text choice. The initial idea to **store metadata** appeared here, held as **directory names** -- the seed of filesystem-as-index (1.2) | [TODO: any surviving coursework code] | C | root of the keys-as-directories idea; informed rejection of binary |
| 1997 | Undergraduate seminar thesis: backpropagation feed-forward neural networks (architecture-as-prior / "bionics") | `Anode1/BPFNN_Coursework` (re-rendered); [TODO: original 1997 copy / advisor Prof. Ehud Gudes attestation] | B/C | early architecture-as-inductive-bias view |
| 1997-2006 | After graduation: IT companies in the US, including Silicon Valley. Programmed genomic-data parsers, OLTP systems, and search engines / search systems | [TODO: employers, dates] | C | professional search-engine + large-data-systems work, the direct background behind AIS |
| 2001 | ANSI C key-value / "maps" PoC (`aisconfig`, a.k.a. "confi"), source headers (c) 2001 | github.com/Anode1/aisconfig | B | AIS engine begun in C |
| 2001 | SMBPANN design notes (self-modifying backprop NN) | github.com/Anode1/SMBPANN; [TODO: confirm dated copy] | B | dated design work |
| ~2001 | First venture-capital pitch: the 1997-thesis **backpropagation feed-forward neural network** (BPFFNN), on Pentium-class hardware -- *not* AIS, *not* wearables | [TODO: firm(s), date, any email/letter] | C | NN work pitched to third parties ~2001 |
| ~2007 | Second venture-capital pitch: **AIS** (the personal associative index); lacking a business plan, the author took a government post in Nov 2007. Wearables were *not* part of this pitch -- the glasses framing postdates it (see 1.3) | [TODO: firm(s), date, materials] | C | AIS pitched to third parties ~2007 |
| ~2006-18 | Regulated medical IT: high-volume data processing, and prediction/classification systems (the applied background for AIS's efficiency and compression-as-intelligence claims) | [TODO: employers, dates] | C | high-volume data + prediction/classification, in a regulated medical setting |
| **2004-11-22** | **AIS registered on SourceForge** | https://sourceforge.net/projects/ais/ | **A** | **strongest third-party date anchor** |
| **2005-01-28** | AIS shell-script implementation (`ais-scripts-0.5`): filesystem-sharded plain-text INDEX, `get`/`put`/`delete`, under CVS (handle `sourcer`) | SourceForge `files/old/ais-scripts/`; recovered to `legacy/ais-scripts/` (CVS `$Id` tags dated 2005-01-28) | A | **earliest working AIS code**; the by-hand text-index practice wrapped into scripts; predates the 2009 Java/Lucene release |
| 2009-08 | AIS source/binary releases (Lucene 2.4.1, Jetty, Swing/CLI/web) | https://sourceforge.net/projects/ais/files/ | A | working implementation, dated |
| 2012 | LiveJournal article (conditional probability; same author/handle) | https://siberean.livejournal.com/16220.html | A | dated, third-party-hosted authorship |
| [TODO] | LiveJournal article describing the AIS / indexing approach | [TODO: exact URL + date] | A | the "approach" described publicly, dated |
| [TODO] | Facebook article(s) on the indexing approach | [TODO: URLs + dates] | A/B | later public description |
| 2013 | AIS (Associative Indexing Service) personal indexing tool | per 2026 papers; [TODO: artifact] | B/C | extended-memory tool in use |
| 2025 | Context Renormalization protocol | github.com/Anode1/context-renormalization (releases) | A/B | bounded-context compression protocol |
| 2025-11/12 | Documented that machine recompression of the v1 ledger silently dropped human-marked semantic priorities (which items are *key* vs *restatable*); v1 dropped. Motivated the open-call request for reserved "strong words" to preserve content | [TODO: locate the exact note/date] | B | the human-prior-vs-machine-objective failure, observed firsthand |
| 2026 | Paper: *Emergence Does Not Care About Substrate* | currently Substack (popular but **volatile** -- not a citation anchor); formal DOI deposit planned | B | compression/emergence thesis |
| 2026 | Paper: *Intelligence Is the Discovery of Compressors* | currently Substack (volatile); to be consolidated into a formal, DOI'd paper | B | full synthesis |
| 2022-05-13 | Internet Archive snapshot of the SourceForge AIS project page (pre-existing third-party capture) | http://web.archive.org/web/20220513005530/https://sourceforge.net/projects/ais/ | A | independent archived copy of the AIS page, well predating this record |
| 2026-06-07 | Save-Page-Now captures attempted for the SF files page + LiveJournal post | not yet confirmed (Save-Page-Now lag/throttle on those hosts); retry pending | -- | (in progress) |

Supporting public repositories (GitHub `Anode1`): `ais`, `aisconfig`, `ansi_c_conversion_template`, `nid`, `SMBPANN`, `context-renormalization`, `notes`, `aisgedcom`, `ped2raw`, `aisconvert`.

---

## 3. Corroborating testimony (Tier C)

The following people were told these concepts (AIS / personal associative memory / wearable capture) at or before the dates indicated. Short signed, dated statements from any of them materially strengthen this record.

- **O.K.** -- peer; the author met him several times and discussed the AIS concept on a couple of occasions (~2004). Around the **2004-11-22** SourceForge registration (within ±2 days), O.K. independently implemented an **MD5 content-identifier for resources**; rather than contribute to AIS he pursued that idea as his own separate project. He can attest to the AIS discussions of that period. [TODO: current contact; a short signed/dated statement; URL or date of O.K.'s MD5 project, which would be a Tier-A timing anchor.]
- **L.L.** -- fellow programmer; the author told him on several occasions about the AIS work he was doing in his spare time, and (~2013) about the **glasses + gloves** wearable idea; he should recall it. [TODO: current contact; approximate years; a short signed/dated statement.]
- **L.R.** -- a second programmer-friend; among those told informally about the **glasses + gloves** idea (~2013). [TODO: current contact; a short signed/dated statement.]
- **M.G.** -- programmer-friend; among those told, informally and repeatedly, about the **glasses + gloves** wearable front-end to AIS (~2013). [TODO: current contact, a short signed/dated statement.]
- **Spouse** -- has known the concept since the project's inception (~2001).
- **Former coworkers (close colleagues)** -- saw the author's `INDEX` / personal knowledge base in active, day-to-day use at work; can attest it existed and was used. [TODO: names/initials, employer, approximate years.]
- **Former manager** -- likely to recall the AIS / knowledge base if reminded. [TODO: name/initials, employer, period.]

**Possible artifact lead.** A backup or disk image of the author's former work computer -- which held the `INDEX` in daily use -- may survive in a former (government) employer's IT archive. If retrievable, it would be a dated record of the INDEX in real workplace use; retrievability is uncertain (access, retention policy, privacy), so this is noted as a lead, not a confirmed artifact. [TODO: employer, period, feasibility of a records request.]

**Venture-capital meetings (two, distinct).** *First (~2001):* the author pitched the **1997-thesis backpropagation neural network** (BPFFNN), on Pentium-class machines -- not AIS, not wearables. *Second (~2007):* the author pitched **AIS** (the personal associative index) and was advised on the missing business case (business plan, revenue model, ROI); lacking that, he took a government position in November 2007. The **wearable "glasses + gloves" framing (section 1.3) postdates both pitches** -- it dates to the 2013 Google Glass announcement -- and was shared not with VCs but informally with the colleagues named below. Any surviving email, calendar entry, or pitch document from either meeting is Tier-A evidence and should be attached.

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

**Conceptual lineage -- the personal associative memory.** Beyond longevity, AIS descends from Vannevar Bush's **Memex** (*As We May Think*, 1945): retrieval by **association** -- trails of linked items, "as the mind works" -- set explicitly against rigid hierarchical/alphabetical indexing, which is exactly AIS's keys-and-graph model (section 1.1). Memex was a vision, never built; its ideas forked into (a) public hypertext (Nelson, Engelbart, then the **Web**) and (b) the personal store: Gordon Bell's **MyLifeBits** (Microsoft Research, ~2001-2009), the "tools for thought" / PKM tools (Zettelkasten -> Roam, Obsidian, Logseq, DEVONthink), and AI lifelogging (Microsoft **Recall**, Rewind). AIS belongs to branch (b), distinguished by **plain-text durability and user ownership**: the index is the user's own files on the user's own device -- they back it up, delete it, or move it to other storage, and no one else (not even the author) can read it, because it never leaves the machine. That ownership/privacy guarantee is the **core promise**, and the sharpest line between AIS and the cloud-lifelogging successors, which read your data by design. The associative-index *idea* is Bush's and widely prior; AIS's claim is the *implementation and approach*, not the concept.

---

## 7. Why the early pitches could not succeed -- the technology gap

The AIS concept pitched to venture capital (section 3), and the wearable "glasses + gloves" form imagined soon after (section 1.3), were sound; the enabling technology was not yet there. The early rejection was, in hindsight, a matter of timing rather than merit -- a voice-driven, eventually head-worn personal associative memory needed an entire substrate that arrived only over the following decade.

What was missing at the time of the pitch(es):

- **Reliable speech recognition.** Practical, accurate ASR is a product of deep learning and reached consumers only ~2011-2014 (Apple Siri, 2011; deep-neural-network acoustic models in Google Voice Search, ~2012). In 2001-2007 speech input was inaccurate and server-bound -- the natural interface for the idea was not yet usable.
- **A distribution channel.** The iPhone (2007) and the App Store (2008) / Android Market (2008) did not exist at the earlier pitch and were brand-new at the later one. There was no way to put a personal app in ordinary people's hands.
- **Mobile broadband.** 3G was only rolling out and mobile data was scarce and costly; always-available sync to a personal archive was impractical.
- **Wearable hardware.** Consumer head-worn capture/display did not exist; Google Glass (Explorer Edition) shipped only in 2013 -- which the author considered buying precisely because it was the first hardware approximating the idea. Battery, optics, and low-power SoCs were not ready before then.
- **Cheap durable storage and cloud.** Indexing and preserving multi-gigabyte life archives was expensive; affordable offsite durability as a service began with Amazon S3 in 2006. The author's workaround was manual -- redundant disks and `rsync` / `s3 sync` (section 1.2).
- **The business case.** On top of the missing substrate, the pitch lacked the commercial framing (business plan, revenue model, ROI) the VCs asked for (section 3); the author took a government position in late 2007 rather than pursue it.

**Why now.** By 2026 every missing piece exists: on-device speech recognition on both major phone platforms, mature app stores, ubiquitous mobile broadband, cheap cloud and local storage, and -- emerging -- consumer AI glasses. Crucially, the original architecture did not have to change to meet them: a plain-text, key-addressed index with a small portable C core (sections 1.1-1.2) drops onto a phone (a native app over the same engine) and rides a voice assistant out toward glasses, unchanged. The idea was not wrong; it preceded its substrate. The 2026 re-implementation targets that now-existing substrate directly.

> Dating note: the pitch dates are split in sections 1.3 / 2 / 3. The **~2001** pitch was the **1997-thesis backpropagation neural network** (BPFFNN); the **~2007** pitch was **AIS** (after which, lacking a business plan, the author took a government post in November 2007); the **wearable "glasses + gloves"** framing dates later still, to the **2013 Google Glass announcement**, and was shared informally with colleagues rather than pitched. [TODO: confirm firms/exact dates and attach any surviving materials.]

---

## References

*Citation policy: cite stable DOIs where already published; the author's Substack essays are popular but volatile and are not used as citation anchors -- a consolidated formal paper (with a DOI) including these ideas is planned. DOIs below are marked [verify] where recalled from memory and should be confirmed before formal deposit.*

1. Gray, J. (2003). "What Next? A Dozen Information-Technology Research Goals." *Journal of the ACM* 50(1): 41-57. Originally the 1998 ACM A. M. Turing Award Lecture; also Microsoft Research Technical Report MSR-TR-99-50 (June 1999). DOI: 10.1145/602382.602401.
2. O'Hara, K., Morris, R., Shadbolt, N., Hitch, G. J., Hall, W., & Beagrie, N. (2006). "Memories for life: a review of the science and technology." *Journal of the Royal Society Interface* 3(8): 351-365. Arising from the UKCRC "Memories for Life" Grand Challenge (2003). DOI: 10.1098/rsif.2006.0125.
3. Rothenberg, J. (1995). "Ensuring the Longevity of Digital Documents." *Scientific American* 272(1): 42-47. Expanded as a CLIR report, "Ensuring the Longevity of Digital Information" (1998/1999). DOI: 10.1038/scientificamerican0195-42.
4. Bush, V. (1945). "As We May Think." *The Atlantic Monthly* 176(1): 101-108. The Memex proposal -- associative indexing of a personal store, retrieval by trails of association -- the conceptual origin of the lineage in section 6.

---

*Prepared with assistance from an AI collaborator (Claude), 2026-06-07. Facts marked [TODO] are to be supplied or verified by the author; DOIs marked [verify] should be confirmed before formal deposit; all other entries are drawn from the author's existing public artifacts.*
