# AIS — Associative Indexing Service

**Your memory, yours to keep.**\
*Models average everyone. Keep what's only yours.*

An extension of your associative memory: a memo that is always with you and always yours, that brings things back the way your mind does — by association. Underneath, an index: file anything under keys, recall it by keys, plain text on your own disk.

## Download

The latest stable build for every platform — this link always points at the current release, never an old one:

> **<https://github.com/Anode1/ais/releases/latest>**

- **Windows** — run the `…-installer.exe` (per-user, no admin).
- **macOS / Linux** — unzip the `…-<os>-<arch>.zip` and run `ais` (add it to your PATH to use it anywhere).

Then `ais --serve` opens the GUI in your browser.

**macOS first run.** The binaries are not yet notarized by Apple, so a downloaded
copy is quarantined and Gatekeeper says *"Apple could not verify 'ais' is free of
malware."* Clear the quarantine flag once — in Terminal, from the unzipped folder:

```sh
xattr -dr com.apple.quarantine .
```

(or **System Settings ▸ Privacy & Security ▸ Open Anyway**). Locally built copies
aren't affected. Notarization is on the [roadmap](doc/ROADMAP.md).

## Quick start (from source)

```sh
make                 # build ./ais
./ais --init           # create an index here (a .ais/ directory, git-style)

./ais -v https://example.org/paper physics todo    # store a value under keys
./ais physics todo                                  # recall: records under BOTH keys
./ais -o physics math                               # recall: records under EITHER key
./ais --serve                                         # open the web GUI in your browser
```

`ais --help` lists every command.

**Tip:** `alias is='ais'` gives you two-character recall — `is venice italy` reads like the question it answers.

## Why

A model trained on everyone gives you the average. What makes your thinking *yours* is the variance the average throws away: your own associations, the order you would read things in, the links only you would draw. AIS keeps that, unaveraged, as plain text you control: a personal index you can read by hand, back up, fork, and feed to an AI on your terms (your bias layered on the model's base, not replaced by the crowd's). A short key standing in for a body of content is itself a small act of compression.

And AIS never takes your files hostage. Short notes can live inside the index, but your documents stay where they already are, in your own folders: AIS stores only a reference (a path or a URL), never copying, moving, or reading inside them. Your storage stays immutable and organized your way; you lay whatever index you want over it, even several indexes over the same untouched files. The index is a view; your data is never touched. That separation — immutable content under independent indexes you add yourself — is the core design idea.

This is an old idea, finally built. In 1945 Vannevar Bush described the *memex* — a personal device that extends your memory by association, the way the mind works. He never built it; the idea later forked into the Web on one side and personal-knowledge tools on the other. AIS is a working memex on the durable, private side: your associations as plain text you own, that no service can read or lock you out of. The idea is Bush's and widely prior; the contribution is the durable, user-owned implementation.

Background: *Intelligence Is the Discovery of Compressors* (Zenodo [doi.org/10.5281/zenodo.20440111](https://doi.org/10.5281/zenodo.20440111); lighter version at [gavr144.substack.com](https://gavr144.substack.com)).

## Questions

**Why not SQLite, or a database?**
A database is the right tool for an *app*; this is for a *person*. SQLite is a binary file one program understands; AIS is line-oriented plain text you can read, grep, diff, and recover by hand in fifty years. A damaged byte costs a line, not the store, and the index rebuilds from the text. You trade query power you do not need for durability and transparency you do.

**Is keys-only search not limiting?**
On purpose. The keys you assign *are* the point: they are your prior, your ordering of the world. Full-text search finds words; keys find the meaning you committed to. (`ais --find` still searches values and paths.) To search a document's contents, keep it as a file and index its path.

**Is the built-in web server not a toy?**
Yes, deliberately. `ais --serve` is a single-user, localhost-only loop so the binary can show a GUI with no framework, no daemon, no account. It binds 127.0.0.1 and nothing else. The command line is the contract; every GUI is a thin wrapper over it.

**Is this not just a bookmark manager / recoll / org-mode?**
It overlaps all three and copies none. Not a bookmark manager: it files *anything* under *any* keys, not URLs in a browser. Not full-text (recoll): it indexes the keys you choose, not document bodies. Not org-mode: no single tree, no app lock-in, no markup to learn, just keys with set algebra (AND / OR) over plain files. The distinctive part is that the index *is your bias*, kept unaveraged and portable.

## Learn more

| Read | For |
|------|-----|
| [`doc/USING.txt`](doc/USING.txt) | How to use it, GUI on every OS (plain steps, no jargon). |
| [`doc/about.txt`](doc/about.txt) | What AIS is, and what it is not. |
| [`doc/OVERVIEW.md`](doc/OVERVIEW.md) | Design philosophy, status, provenance. |
| [`doc/ROADMAP.md`](doc/ROADMAP.md) | What's planned, and where to help. |
| [`doc/dev/LAYOUT.md`](doc/dev/LAYOUT.md) | On-disk format and module map. |
| `man ais` | Full command reference. |

## License

New code (`c/`): GNU GPL v2 or later (per source headers). Legacy material (`legacy/`) under its original Apache License 2.0. Author: Vasili Gavrilov (GitHub [Anode1](https://github.com/Anode1)).
