# Priority & Provenance Record

**Author:** Vasili Gavrilov -- ORCID [0009-0007-9371-5994](https://orcid.org/0009-0007-9371-5994)
**Handles:** GitHub `Anode1`, LiveJournal `siberean`, SourceForge `vgavrilov`
**Date of this record:** 2026-06-11
**DOI:** [10.5281/zenodo.20647048](https://doi.org/10.5281/zenodo.20647048) (this record, on Zenodo)
**Status:** living document, deposited to Zenodo (DOI above); anchored with OpenTimestamps (blockchain proof-of-existence) and committed to git (signed tags). References third-party-dated evidence wherever possible.

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

**Conceptual origins (Tier C).** The **search-by-keys** retrieval has a concrete root: in **1998** the author built a viewer for an **LDAP / X.500 directory**, where the idea of *fast search by namespace* came from. He deliberately did **not** clone that proprietary directory GUI (an ethical choice) -- rather than re-implement its hierarchy, he conceived a *different view over immutable data*. The companion idea -- **separating immutable data/structure from its view(s)**, so the `INDEX` is just one view over a write-once content store -- comes from his **1997-2006** contract work, predominantly relational databases and views, where model/view separation was routine.

### 1.2 Filesystem-as-index -- manual implementation of AIS
Before AIS was a program, the author ran it **by hand on the filesystem**: an `INDEX/` directory tree (`A/`, `B/`, ...), **filenames as keys**, file contents being a list of URIs/links forming a graph. When a directory exceeded a threshold (~100 keys) -- on early-Pentium, pre-B-tree-filesystem hardware -- he **split it into subdirectories, manually balancing the tree** for logarithmic access. The **content archive was immutable** (photos, documents, books -- write-once) and copied verbatim across **4 redundant disks (3-2-1+)**, kept in step by redundant synchronization (`rsync -avu --delete` locally); over the years **three disks failed with zero byte lost**, and only the small `INDEX/` was tended. (This independently prefigures: Git's hash-prefix object sharding; the plain-text linked-note / Zettelkasten model; and the LOCKSS "lots of copies" preservation principle.) The seed -- storing **metadata** held as **directory names** -- dates to the **1993-97** CS coursework (section 2); practised systematically from **~2001** (Tier C). Various sorted index organizations, and even relational databases, were tried for the index along the way; **immutability of the contents** (the `INDEX` design) was the choice settled on **~2001** (the recovered archive's `INDEX/` mtimes are reset to a single `2004-12-31` restore stamp -- an *upper bound* only; the dated coursework artifacts in section 2 place the tree-structure practice in 1995-96. See the archive-forensics note, section 2.1). The underlying problem -- long-term preservation and format compatibility of life archives -- is itself a named grand challenge in computer science (see section 6 and References).

**Idea vs. practice vs. software (kept distinct).** The author ran this **by hand** from ~2001 through 2004; the indexing was automated as **software only in 2005, as AIS** (the Associative Indexing Service) -- the shell `ais-scripts`, then the C and Java implementations (section 2) -- when he had time, motivated by his **2003-04 work with commercial (private-company) search engines** and the wish to automate and organize the hand-maintained, sharded `ISROOT`. So the claims do not blur: the *idea* and the *manual practice* are ~2001-2004; the **AIS** indexing software dates to **2005** (its SourceForge project `ais` was registered 2004-11-22, section 2). The 2001 `org.is`/`is` namespace is a separate, earlier **name**, first used for the unrelated LJMS messaging library (section 2) -- not indexing code; `is` is today merely an alias of `ais`.

### 1.3 Wearable "glasses + gloves" front-end to AIS
A head-worn recall surface plus a hands-free capture device feeding the personal associative memory. On the **announcement of Google Glass** (developer "Explorer Edition", 2013 -- US$1500, with an initial US$1000 promotional tier; the author considered buying one), the author immediately pictured **AIS hooked into the glasses for GET** -- recall a person's details the moment you meet them -- and, for the harder half, **PUT** (entering a name at the instant it is spoken, without opening a laptop), imagined a **gamer's-style glove** as the input device. This concrete wearable framing dates to the **Google Glass announcement (~2013)** -- i.e. *after* the ~2007 AIS pitch (section 3), not before it -- and was discussed informally and repeatedly with colleagues (L.L. and L.R., M.G., others, and the author's manager). (Tier C; see section 3.)

### 1.4 Compression-as-intelligence
The thesis that intelligence is the discovery of compressors whose inductive bias fits the data; that "the compressor function is the understanding"; substrate-independence of emergence. Developed across the author's work and formalised in the 2026 papers (section 2). (Tier B.)

**Concrete seed (per the author, Tier C).** A small **autocomplete** JavaScript project used in chats (**~2002-3**) led to the realization that the **dictionary of keys is compact** -- so one can **index *by* the dictionary** (retrieve through the small key-set) instead of searching across the records: indexing as a form of **compression** (compress the *access path* to a compact key-set rather than scan content). That move -- a compact dictionary/compressor that *addresses* content instead of a search over it -- is the bridge from AIS (§1.1) to this thesis, the same idea at two scales. The ~2002-3 autocomplete dictionary and chat use are corroborated by `Dictionary/DIC` (first-letter-sharded) and the `gtchat` chat project (§2.1); the *insight* and its lineage to compression-as-intelligence are the author's attested intellectual history, not provable from the files.

### 1.5 Content-addressed, scattered-memory recovery
A scheme for surviving institutional mortality: encrypt memories, address them by content hash, disperse redundant copies across the public commons, and recover them by a compact local set of hooks/keys -- independent of any single company's survival. Content-hash addressing itself is **not** claimed as novel -- it is widely prior (content-addressable stores), and a peer independently used MD5 content-identifiers in 2004 (see section 3); the claim here is the *composition*: encrypt, disperse across the public commons, and recover by a compact local key-set. Recorded here **2026** (Tier B).

The decentralizing disposition is older than this 2026 framing: **LJMS** (2001, §2) was conceived as **personalized, "biased" peer-to-peer** information sharing -- deliberately *not* the centralized, normalized data held by the companies that own it. That "keep what is only yours, not the averaged center" stance is the same one running through §1.4 and the AIS design. (Tier C for the motive.)

---

## 2. Verifiable artifact trail (chronological)

| Date | Artifact | Location | Tier | Establishes |
|---|---|---|---|---|
| 1986-92 | Studied Physics, Kazan University (UofT certified the study as equivalent to a BSc); first applied C, interfacing NMR/MRI instruments | [TODO: any surviving records] | C | physics + CS foundation; first C in a medical setting |
| 1993-97 | Degree in CS. Built a dBASE-like database by hand (C structs, blobs, a binary format) -- the binary experience behind AIS's deliberate plain-text choice. The initial idea to **store metadata** appeared here, held as **directory names** -- the seed of filesystem-as-index (1.2) | partly recovered: Open University of Israel coursework archive (see next row) | C | root of the keys-as-directories idea; informed rejection of binary |
| **1995-12 / 1996-01** | Open University of Israel CS coursework, recovered: a keyed multi-entity records DB in Turbo Pascal (`PTUHA.PAS` -- students keyed by `zeut`, a courses chain, per-student course lists, binary-search retrieval, `SC`/`SA`/`CA`/`MAS` queries) and a binary-search-tree records DB in Ada 83 (`MAMAN13/EX2`); also a second binary tree (`BIN_TREE.ADB`), queues/ADTs/generics, minimax checkers, UNIX IPC/semaphores/shell, Prolog; dated scaling runs (`OUT10..250`) | recovered archive (drive #2): Pascal mtimes 1995-12-25->31 (genuine spread); Ada GNAT `.ali` source stamp 1996-01-08; [TODO: deposit a copy] | B | **surviving, dated CS coursework** -- the tree-structure & keyed-DB training behind 1.2; resolves the 1993-97 'surviving code' TODO |
| 1997 | Undergraduate seminar thesis: backpropagation feed-forward neural networks (architecture-as-prior / "bionics") | `Anode1/BPFNN_Coursework` (re-rendered); [TODO: original 1997 copy / advisor Prof. Ehud Gudes attestation] | B/C | early architecture-as-inductive-bias view |
| 1997-2006 | Contract developer: **relational databases and views** (model/view separation -- §1.1) and **production search engines** for several companies -- the professional background directly behind AIS's search/index and its model-vs-view design. (Other projects exist; omitted here as they don't corroborate a specific AIS claim -- this is a priority record, not a CV.) | [TODO: any corroborating record on file] | C | search-engine + model/view background behind AIS |
| 2001 | ANSI C key-value / "maps" PoC (`aisconfig`, a.k.a. "confi"), source headers (c) 2001 | github.com/Anode1/aisconfig | B | AIS engine begun in C || 2001-04 | `LJMS` ("Lightweight JMS") -- a spare-time Java messaging library (an independent `javax.jms` implementation with an `org.is.jms.broker` over HTTP/UDP/socket transports, and P2P-style examples: `gnutella`, `relay`, `swingP2P`), all under the **`org.is.*`** namespace, atop a reusable foundation lib (`LRUCache`, `OrderedHashtable`, `Queue`, `ThreadPool`, `logmanager`) | recovered archive (drive #2): earliest authored source **2001-04-13** (`NutClient.java`); v0.2 release 2001-04-23; `org.is` classes by 2001-04-26; CVS `Entries` 2001-04-28; the checkout's `CVS/Root` = `:ext:vgavrilov@cvs.ljms.sourceforge.net:/cvsroot/ljms` -- so **LJMS was a registered SourceForge project** (`ljms`, owner `vgavrilov`; project page now 404 -- SF retired legacy CVS and purged/reorganized inactive early-2000s projects over the years, and the author may have deleted it; the later absence does not bear on its 2001 existence, which the `CVS/Root` attests) (all spread, escaped the reset; a reused 1997 stock image `foo.jpg` excluded; [TODO: recover the SF `ljms` registration date via Wayback / SF attic -> Tier A]) | B | the **`is` name dated to April 2001** (the `org.is` namespace); part of the ~2001 infrastructure burst. The code (earliest source 2001-04-13; v0.2 2001-04-23) predates **Project JXTA's public launch (2001-04-25)** -- evidence the work was **independent of / not derived from JXTA**. This is a *non-derivation* point only: **no novelty claimed** over JMS (Sun spec, 1998), JXTA (2001), or P2P/Gnutella (2000), all prior |
| ~2001 | First venture-capital pitch: the 1997-thesis **backpropagation feed-forward neural network** (BPFFNN), on Pentium-class hardware -- *not* AIS, *not* wearables | [TODO: firm(s), date, any email/letter] | C | NN work pitched to third parties ~2001 |
| ~2007 | Second venture-capital pitch: **AIS** (the personal associative index); lacking a business plan, the author took a government post in Nov 2007. Wearables were *not* part of this pitch -- the glasses framing postdates it (see 1.3) | [TODO: firm(s), date, materials] | C | AIS pitched to third parties ~2007 |
| ~2006-18 | Regulated medical IT: high-volume data processing, and prediction/classification systems (the applied background for AIS's efficiency and compression-as-intelligence claims) | [TODO: employers, dates] | C | high-volume data + prediction/classification, in a regulated medical setting |
| 2003 | Backend research -- evaluating a store to replace the hand-maintained `INDEX`: `db2java.jar` (IBM DB2 JDBC driver) present/dated 2003, plus a collected documentation folder on Oracle, MySQL, PostgreSQL, Berkeley DB, DB2, and deadlocks | recovered archive (drive #2; `db2java.jar` 2003) | B/C | dated corroboration that **relational and other backends were evaluated** (§1.1 "pluggable backend"; §1.2 "RDBs were tried") -- the deliberation behind keeping plain text. (Dates the research activity, not a decision.) |
| 2003-10 -> 2004 | Personal content archive (`records/`) -- the immutable "blobs" the `INDEX` referred to: saved web pages/articles, source, books, configs (Gentoo/fluxbox, Linux 2.6, AMD64, J2EE/Tomcat -- all period-consistent, no anachronisms) | recovered archive (drive #2; mtimes from 2003-10-02, spread -- escaped the reset) | B | the immutable content-archive practice (1.2) in active use **by 2003**; the key->blob two-layer design the present AIS keeps (`idx` + `store` + `blobs/`). (Dates the *content*; indexing over it is upper-bounded by the INDEX reset.) |
| 2004-04 | First *program* form of AIS: a JavaScript web client named `is` (`is/www/{common,jslib,layers}.js`; `black.css`, 2004-05) | recovered archive (drive #2; mtimes 2004-04-14, spread -- escaped the reset) | B | the idea became software in 2004, ~7 months before the SourceForge anchor; the name `is` recurs to the present tool |
| **2004-11-22** | **AIS registered on SourceForge** | https://sourceforge.net/projects/ais/ | **A** | **strongest third-party date anchor** |
| **2005-01-28** | AIS shell-script implementation (`ais-scripts-0.5`): filesystem-sharded plain-text INDEX, `get`/`put`/`delete`/`common`, **automated re-sharding** (`pump.sh` -- splits a bucket into letter sub-dirs at `MAX_FILES_IN_DIR=100`), `files2db.sh`, unit tests; under CVS (handle `sourcer`) | SourceForge `files/old/ais-scripts/`; recovered to `legacy/ais-scripts/` (CVS `$Id` tags 2005-01-28; archive mtimes 2005-01-20->02-02, `pump.sh` 2005-02-01) | A | **earliest working AIS code**; automation of the tree-balancing dates to **early 2005** (the 2011 post below is a late publication of it); predates the 2009 Java/Lucene release |
| 2005-11 | C implementation named `is`, v0.0.1: autotools, a Gentoo `ais-0.0.1.ebuild`, Doxygen, gettext (`src/is.c`, `is.h`, `List.c`, `common.c`) | recovered archive (drive #2; mtimes 2005-11-02->29) | B | a **C AIS existed in 2005** -- the 2026 engine is a *re-implementation*, not the first C |
| 2005-11-06 | First **Java** copy of AIS, under CVS (`$Id: TODO,v 1.1 2005/11/06 03:52:44 sourcer`); the `TODO` lists engine concerns -- Unicode/i18n, sentence-passing (vs tokenizing), proper escapes (the same key-encoding/tokenizing problems the present engine answers). The `$Id` UTC time equals the C tree's local mtime to the second | recovered archive; CVS `$Id` 2005-11-06 (handle `sourcer`) | A | the **Java line began in 2005**, not 2009 -- the 2009 Lucene/Jetty release is a later milestone of it |
| 2007-11/12 | CVS + Eclipse `.project` activity on the C `is` tree, at the ~2007 AIS pitch / Nov-2007 government-post juncture (section 3) | recovered archive (mtimes + CVS, 2007-11-30->12-01) | B | active development at the 2007 pitch period |
| 2009-08 | AIS source/binary releases (Lucene 2.4.1, Jetty, Swing/CLI/web) | https://sourceforge.net/projects/ais/files/ | A | working implementation, dated -- a later milestone of the Java line begun 2005-11 (see above) |
| 2011-08-10 | LiveJournal "Bourne shell scripting": publishes the shell INDEX manager -- `get`/`put`/`delete`/`common`/`pump.sh` (auto letter-sharding at `MAX_FILES_IN_DIR=100`), `files2db.sh` -- i.e. the 2005 design disclosed publicly | https://siberean.livejournal.com/13367.html | A | the indexing approach, incl. automated re-sharding, described publicly and third-party-dated |
| 2012 | LiveJournal article (conditional probability; same author/handle) | https://siberean.livejournal.com/16220.html | A | dated, third-party-hosted authorship |
| [TODO] | Facebook article(s) on the indexing approach | [TODO: URLs + dates] | A/B | later public description |
| 2013 | AIS (Associative Indexing Service) personal indexing tool | per 2026 papers; [TODO: artifact] | B/C | extended-memory tool in use |
| 2025 | Context Renormalization protocol | github.com/Anode1/context-renormalization (releases) | A/B | bounded-context compression protocol |
| 2025-11/12 | Documented that machine recompression of the v1 ledger silently dropped human-marked semantic priorities (which items are *key* vs *restatable*); v1 dropped. Motivated the open-call request for reserved "strong words" to preserve content | [TODO: locate the exact note/date] | B | the human-prior-vs-machine-objective failure, observed firsthand |
| 2026 | Paper: *Emergence Does Not Care About Substrate* | currently Substack (popular but **volatile** -- not a citation anchor); formal DOI deposit planned | B | compression/emergence thesis |
| 2026 | Paper: *Intelligence Is the Discovery of Compressors* | currently Substack (volatile); to be consolidated into a formal, DOI'd paper | B | full synthesis |
| 2026 | **AIS re-implemented from scratch in ANSI C / C99** -- the current tool (`ais`): append-only plain-text store, keys with set-algebra (union/intersection), one engine under a CLI, a native desktop app, and a local web GUI; references content by path/URI and never mutates it (the immutable-store-plus-rebuildable-index design, §1.1). The **re-implementation of the C line begun 2005-11** -- C was the original target (the (c) 2001 `aisconfig` engine PoC, row above), prototyped faster in shell/Java and finished in C in 2026 | github.com/Anode1/ais (source + tagged releases) | A | the present implementation, dated and public; **closes the lineage: 2001 idea -> 2005 C -> 2026 C** |
| 2022-05-13 | Internet Archive snapshot of the SourceForge AIS project page (pre-existing third-party capture) | http://web.archive.org/web/20220513005530/https://sourceforge.net/projects/ais/ | A | independent archived copy of the AIS page, well predating this record |
| 2026-06-07 | Save-Page-Now captures attempted for the SF files page + LiveJournal post | not yet confirmed (Save-Page-Now lag/throttle on those hosts); retry pending | -- | (in progress) |

Supporting public repositories (GitHub `Anode1`): `ais`, `aisconfig`, `ansi_c_conversion_template`, `nid`, `context-renormalization`, `notes`, `aisgedcom`, `ped2raw`, `aisconvert`.

### 2.1 Recovered archive -- dating method, and the 2004-12-31 mtime reset

A personal archive survives on an external drive (disk **#2** of the author's 1-4 redundant-disk set, section 1.2); it is the source of several rows above. Two facts govern how its dates are read:

- **The `INDEX/` data sub-tree bears a single mtime, `2004-12-31T04:51:10`, uniform to the second.** That uniformity is the fingerprint of one bulk restore -- a `cp` / pre-`rsync -a` copy after a disk failure (the "three disks failed, zero byte lost" event of section 1.2), which overwrites every file's mtime with the copy instant. So for `INDEX/` the mtime is only an **upper bound** (the keys existed *no later than* that date), not a creation date. (Tier-C reconstruction of the mechanism; the pattern is *consistent* with it, not proof of which disk failed when.)
- **The *source* sub-trees sat on a disk that did not fail and kept real, spread mtimes**, and several carry dates *embedded inside* build/VCS artifacts -- GNAT `.ali` dependency stamps, CVS `$Id`/`Entries` -- independent of the filesystem mtime and surviving the restore. These are the firmer dates (Tier B): e.g. the Pascal coursework's 1995-12-25->31 editing spread, the Ada `.ali`'s 1996-01-08, the shell scripts' CVS tags. The saved `records/` content is independently self-dating in the same spirit -- embedded software versions (Linux kernel `2.6.0`/`2.6.5-gentoo-r1`; `dvgrab-1.7`, `phpBB-2.0.8a`, `nmap-2.07`, `tripwire-1.2`) and a Java-epoch-stamped Eclipse workspace (`v2Config1071534854411.xml` = 2003-12-16) -- which over-determines the 2003-04 era well beyond any single filesystem mtime, and would be impractical to fabricate across thousands of internally-consistent files.

**Excluded as non-evidence of a year:** the `1988-03-30` mtime on the bundled Ada-83 *Rationale* manual (a third-party document the author downloaded to study Ada), and the `2026` build artifacts of `coursework_1997_backprop_ffnn.*` (a recent LaTeX *re-rendering* of the 1997 NN work, not a 1997-dated original).

The program runs under the name **`is`** from the **2001 `org.is.*` Java namespace** (the LJMS project), through the 2004 JavaScript client and the 2005 C v0.0.1, to the present tool (shipped as `ais`, alias `is`). Per the author, `is` was his chosen identifier for the **indexing** service (the AIS idea, §1.1), adopted as a cross-project umbrella namespace -- which is why even the 2001 *messaging* library carries `org.is`. Tiers kept separate: the namespace's 2001-04 existence is **Tier B** (dated code on disk); that `is` *denoted indexing* is the author's **Tier C** attested intent, corroborated by the unbroken later use of `is`/`ais` for the indexing tool -- not provable from `org.is` alone, and LJMS itself contains no index code. (The ~1 TB archive is kept mtime-preserving via `rsync -a` to three further copies -- the §1.2 redundant-sync practice, still in use in 2026 -- so the dated evidence is durably and redundantly protected.)

### 2.2 AIS genesis and publish order (Tier C, author's account)

Per the author: around 2004-05 he **researched and chose the name "AIS"** and created the project; an experienced SourceForge/CVS user already (handle `vgavrilov`, from LJMS in 2001), he registered SF `ais` (2004-11-22) and committed code in 2005. He recalls **three implementations** -- a **Java** version, a **shell** version, and a **C** version he **deliberately withheld until it was stable** -- which is why the *published* artifacts were Java and shell first, with C developed in parallel (or earlier) but held back (consistent with the local-only C `is` v0.0.1, 2005-11).

*Why the publish order is mixed (and why it does not matter):* this was a **strategy, not a clean sequence**. The author **started in C** (the performant target) but found it slower to build; meanwhile **shell** served as the quick proof-of-concept and **Java** as the fast-to-ship version. The plan: **publish Java first** -- to secure the record and the name without losing momentum -- then continue C (kept private until stable). That is why the dated artifacts show shell (`$Id` 2005-01-28) and Java (2005-11) published while only **pieces of an incomplete C** version survive from 2005. The server-side bias was period-rational: with no smartphones (Motorola-class phones could neither hold nor view the index), a **server was assumed necessary**, and Java was the server-side standard. For the C backend he first considered the **GNU Berkeley DB** (cf. the 2003 backend research); he **discovered Lucene in 2007** (adopted in the 2009 Java release). All three are 2005 -- the publish order reflects strategy, not conception order.

### 2.3 Engineering practice (context)

A sustained automated **regression-testing** discipline runs across the corpus: Ada `TEST*.ADB` (1996 coursework), LJMS `TestInvoker`/`*Test`/`Impl2ServerTest` (2001), `unit_test.sh` + `testwords` in the 2005 shell `ais-scripts`, and the present C engine's `tests.c` (111 checks) + `tests/cli.sh` (53). The author was an early proponent of **test-first / TDD**, dating the practice to ~2003 -- i.e. early adoption of the Beck/XP method, **no novelty claimed**. (Tier B for the dated test artifacts; Tier C for "test-first since 2003." Context, not a priority claim.)

---

## 3. Corroborating testimony (Tier C)

The following people were told these concepts (AIS / personal associative memory / wearable capture) at or before the dates indicated. Short signed, dated statements from any of them materially strengthen this record.

- **O.K.** -- peer; the author met him several times and discussed the AIS concept on a couple of occasions (~2004). Around the **2004-11-22** SourceForge registration (within ±2 days), O.K. independently implemented an **MD5 content-identifier for resources**; rather than contribute to AIS he pursued that idea as his own separate project. He can attest to the AIS discussions of that period. [TODO: current contact; a short signed/dated statement; URL or date of O.K.'s MD5 project, which would be a Tier-A timing anchor.]
- **L.L.** -- fellow programmer; the author told him on several occasions about the AIS work he was doing in his spare time, and (~2013) about the **glasses + gloves** wearable idea; he should recall it. [TODO: current contact; approximate years; a short signed/dated statement.]
- **L.R.** -- a second programmer-friend; among those told informally about the **glasses + gloves** idea (~2013). [TODO: current contact; a short signed/dated statement.]
- **M.G.** -- the author's **spouse** and a fellow programmer; has known the concept since the project's **inception (~2001)**, and was among those told, informally and repeatedly, about the **glasses + gloves** wearable front-end to AIS (~2013). [TODO: a short signed/dated statement.]
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
