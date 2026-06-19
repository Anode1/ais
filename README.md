# AIS: Associative Indexing Service

**Your memory, yours to keep.**\
*Models average everyone. Keep what's only yours.*

An extension of your associative memory: a memo that is always with you and always yours, that brings things back the way your mind does, by association. Underneath, an index: file anything under keys, recall it by keys, plain text on your own disk.

## Download

The latest stable build for every platform. The link below always points at the current release, never an old one:

> **<https://github.com/Anode1/ais/releases/latest>**

- **Windows**: unzip `…-windows-x86_64.zip` and double-click **`ais-gui.exe`** (the native desktop app). Nothing is installed; to remove it, delete the folder. Prefer a Start-Menu entry? Run `…-installer.exe` instead (per-user, no admin).

  *First run:* this build is not yet code-signed, so Windows may show a blue **"Windows protected your PC"** screen the first time you run the installer or `ais-gui.exe`. That is SmartScreen flagging a new, unrecognized download, not a virus warning. Click **More info**, then **Run anyway** (once per file). To confirm the download is genuine first, see ["Unknown publisher" — and how to verify a download](#unknown-publisher--and-how-to-verify-a-download); building from source is never flagged.

- **macOS / Linux**: unzip the `…-<os>-<arch>.zip`, then `./ais --serve` opens the GUI in your browser (or use the `ais` CLI; add it to your PATH to use it anywhere).

  *macOS first run:* the binaries are not yet notarized by Apple, so a downloaded copy is quarantined and Gatekeeper says *"Apple could not verify 'ais' is free of malware."* Clear the quarantine flag once, in Terminal from the unzipped folder:

  ```sh
  xattr -dr com.apple.quarantine .
  ```

  (or **System Settings ▸ Privacy & Security ▸ Open Anyway**). Locally built copies aren't affected.

## "Unknown publisher" — and how to verify a download

The Windows and macOS binaries are not code-signed, so a fresh download is flagged as coming from an unidentified developer: Windows SmartScreen calls it an **"unknown publisher"**, macOS Gatekeeper says it **"could not verify"** the app. This means only that the file is new and unsigned — it is *not* a malware finding, and the OS has not inspected the code. Signing is a paid trust service, not a safety check; AIS skips it and gives you a way to check the bytes yourself instead.

Two ways to be sure a download is the genuine, untampered release:

**1. Check the SHA-256.** Every release file ships beside a matching `…zip.sha256` holding its hash. Download both, then:

- **Windows** (PowerShell), compare the printed hash against the `.sha256` file:
  ```powershell
  Get-FileHash ais-*-windows-x86_64.zip -Algorithm SHA256
  ```
- **macOS / Linux**, with both files in the same folder (prints `OK` on a match):
  ```sh
  shasum -a 256 -c ais-*-*.zip.sha256
  ```

**2. Or build from source.** The releases are built in the open by GitHub Actions, not on anyone's personal machine (workflow: `.github/workflows/release.yml`), and a copy you compile yourself is never flagged — see *Quick start (from source)* below.

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

**Tip:** `alias is='ais'` gives you two-character recall: `is venice italy` reads like the question it answers.

## Why

A model trained on everyone gives you the average. What makes your thinking *yours* is your prior (your own associations, the order you would read things in, the links only you would draw), the systematic lens the average cancels out. AIS keeps that, unaveraged, as plain text you control: a personal index you can read by hand, back up, fork, and feed to an AI on your terms (your bias layered on the model's base, not replaced by the crowd's). A short key standing in for a body of content is itself a small act of compression.

And AIS never takes your files hostage. Short notes can live inside the index, but your documents stay where they already are, in your own folders: AIS stores only a reference (a path or a URL), never copying, moving, or reading inside them. Your storage stays immutable and organized your way; you lay whatever index you want over it, even several indexes over the same untouched files. The index is a view; your data is never touched. That separation, immutable content under independent indexes you add yourself, is the core design idea.

Your work should also outlive the software that made it, and its own versions. Keeping data readable across decades is a long-recognized, still-unsolved corner of computing: the format-obsolescence problem ([doi.org/10.1038/scientificamerican0195-42](https://doi.org/10.1038/scientificamerican0195-42), 1995). AIS's stance is that the data is the asset and the software, this version or the next, is secondary and replaceable: the index is plain text with no engine to depend on, so any future version reads it, older formats keep working (you can watch the format evolve across versions in one file and still read every line), and even if AIS is gone your memory is just lines that you, or any unix tool, can read and carry forward. The author has kept his own index this way for roughly a quarter-century, across several rewrites of the program.

This is an old idea, finally built. In 1945 Vannevar Bush described the *memex*, a personal device that extends your memory by association, the way the mind works. He never built it; in 1998 Jim Gray named it again (a personal Memex that records what you see and hear and returns any item on request) as one of a dozen long-term goals he posed for computing, in the spirit of Hilbert's problems for mathematics (ACM Turing Award Lecture; *Journal of the ACM* 50(1), 2003). The author was already keeping such an index by hand; reading of that challenge, he set out to automate it. AIS is a working memex on the durable, private side: your associations as plain text you own, that no service can read or lock you out of. The idea is Bush's and widely prior; the contribution is the durable, user-owned implementation.

Background: *Intelligence Is the Discovery of Compressors* (Zenodo [doi.org/10.5281/zenodo.20440111](https://doi.org/10.5281/zenodo.20440111); lighter version at [gavr144.substack.com](https://gavr144.substack.com)).

## Questions

**Why not SQLite, or a database?**
A database is the right tool for an *app*; this is for a *person*. SQLite is a binary file one program understands; AIS is line-oriented plain text you can read, grep, diff, and recover by hand in fifty years. A damaged byte costs a line, not the store, and the index rebuilds from the text. You trade query power you do not need for durability and transparency you do.

**Why not an embedded engine (BerkeleyDB, LMDB, gdbm)?**
Because a bundled engine is a dependency you do not control. An early AIS version actually ran on BerkeleyDB (both the Java and the C editions) right as it was acquired and relicensed; this plain-text design is that lesson, learned firsthand. A format only one library version can open is a bet that the library, its license, and its on-disk layout outlive your data; they rarely do. AIS has no engine to depend on: the store is plain text, so any future AIS, any unix tool, or any format you migrate to can read it. Moving your data out is `cut`/`sed` and `ais --dump`; repairing it (after a crash, a bad sector, or your own edit) is opening a file in an editor. The store outlives the program on purpose.

**Is keys-only search not limiting?**
On purpose. The keys you assign *are* the point: they are your prior, your ordering of the world. Full-text search finds words; keys find the meaning you committed to. (`ais --find` still searches values and paths.) To search a document's contents, keep it as a file and index its path.

**Is the built-in web server not a toy?**
It is deliberately minimal, and it is not the main interface. The command line is the contract, and the primary way to use AIS; every GUI is just a thin wrapper over it. `ais --serve` is one such wrapper: a single-user, localhost-only loop that opens a GUI in any browser with no framework, no daemon, and no account (it binds 127.0.0.1 and nothing else). The native Windows app (`win32/`) is another wrapper, and a Flutter app covers mobile. (An earlier Tk desktop wrapper was dropped once `ais --serve` covered the same ground.) The engine depends on none of them, so a wrapper can be swapped or added without touching the core.

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
