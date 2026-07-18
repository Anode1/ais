# AIS: Associative Indexing Service

**Your memory, yours to keep.**\
*Models average everyone. Keep what's only yours.*

An extension of your associative memory: a memo that is always with you and always yours, that brings things back the way your mind does, by association. Underneath, an index: file anything under keys, recall it by keys, plain text on your own disk.

And because it is plain text, it outlives its own tools: your index survives decades of archiving, still opens in fifty years, and exports into anything, no lock-in. Keeping data readable that long is computing's unsolved *digital dark age*, where file formats and the apps that open them die faster than the data. Plain text, readable since the 1960s on any machine with no special program, is the oldest and safest answer.

<p align="center">
  <img src="screenshots/android-timeline.png" width="30%" alt="Everything you saved: links, file paths, and encrypted secrets">
  <img src="screenshots/android-search.png" width="30%" alt="Search returns clickable links">
  <img src="screenshots/android-tags.png" width="30%" alt="Browse everything by tag">
</p>
<p align="center"><em>The phone app: file links, file paths and notes, recall them by tag; passwords stay encrypted (&#128274;). The same index from the command line:</em></p>
<p align="center">
  <img src="screenshots/cli.png" width="78%" alt="Store links and an encrypted password, then recall by key">
</p>

## Download

The latest stable build for every platform. The link below always points at the current release, never an old one:

> **<https://github.com/Anode1/ais/releases/latest>**

- **Windows**: _(temporarily unavailable while the desktop GUI is reworked; use `ais --serve` or the mobile app meanwhile.)_ unzip `…-windows-x86_64.zip` and double-click **`ais-gui.exe`** (the native desktop app). Nothing is installed; to remove it, delete the folder. Prefer a Start-Menu entry? Run `…-installer.exe` instead (per-user, no admin).
- **macOS / Linux**: unzip the `…-<os>-<arch>.zip`, then `./ais --serve` opens the GUI in your browser (or use the `ais` CLI; add it to your PATH to use it anywhere).

The binaries are not code-signed, so the first run is flagged as an unrecognized download (Windows SmartScreen "unknown publisher"; macOS Gatekeeper "could not verify"). That is a new-and-unsigned notice, not a malware finding. On Windows click **More info ▸ Run anyway** (once per file); on macOS run `xattr -dr com.apple.quarantine .` in the unzipped folder. A copy you build yourself is never flagged.

## Verify a download

Each release file ships beside a matching `…zip.sha256`. Download both, then check the hash (prints `OK` on a match):

```sh
shasum -a 256 -c ais-*-*.zip.sha256          # macOS / Linux
```
```powershell
Get-FileHash ais-*-windows-x86_64.zip -Algorithm SHA256   # Windows, compare against the .sha256
```

Releases are built in the open by GitHub Actions (`.github/workflows/release.yml`), not on anyone's machine.

## Quick start (from source)

```sh
make                 # build ./ais
./ais --init           # create an index here (a .ais/ directory, git-style)
./ais --serve          # open the web GUI in your browser
```

`ais --help` lists every command; [`doc/USING.txt`](doc/USING.txt) has the everyday CLI cheat-sheet (recall, add, edit) and where your data lives.

**Tip:** `alias is='ais'` gives you two-character recall: `is venice italy` reads like the question it answers.

## Why

A model trained on everyone gives you the average; your prior (your own associations and ordering) is the systematic lens the average cancels out. AIS keeps that unaveraged, as plain text you control, never taking your files hostage: it stores only a reference, so the index is a view and your data is never touched. See [`about.txt`](doc/about.txt) for the pitch and the memex origin, and [`foundation.md`](doc/foundation.md) for the prior/compression argument behind it.

## Questions

**Why not SQLite, or a database?**
A database is the right tool for an *app*; this is for a *person*. SQLite is a binary file one program understands; AIS is line-oriented plain text you can read, grep, diff, and recover by hand. You trade query power you do not need for the durability and transparency of plain text (see [`about.txt`](doc/about.txt)).

**Why not an embedded engine (BerkeleyDB, LMDB, gdbm)?**
Because a bundled engine is a dependency you do not control. An early AIS version actually ran on BerkeleyDB (both the Java and the C editions) right as it was acquired and relicensed; this plain-text design is that lesson, learned firsthand. A format only one library version can open is a bet that the library, its license, and its on-disk layout outlive your data; they rarely do. AIS has no engine to depend on: any future AIS, any unix tool, or any format you migrate to can read the store.

**Is keys-only search not limiting?**
On purpose. The keys you assign *are* the point: they are your prior, your ordering of the world. Full-text search finds words; keys find the meaning you committed to. (`ais --find` still searches values and paths.) To search a document's contents, keep it as a file and index its path.

**Is the built-in web server not a toy?**
It is deliberately minimal and not the main interface. The CLI is the contract; `ais --serve` is one thin wrapper over it, a single-user loop that binds 127.0.0.1 only. The native Win32 app and the Flutter mobile app are other wrappers; the engine depends on none of them. See [`OVERVIEW.md`](doc/OVERVIEW.md) for the full front-end map.

**Is this not just a bookmark manager / recoll / org-mode?**
It overlaps all three and copies none. Not a bookmark manager: it files *anything* under *any* keys, not URLs in a browser. Not full-text (recoll): it indexes the keys you choose, not document bodies. Not org-mode: no single tree, no app lock-in, no markup to learn, just keys with set algebra (AND / OR) over plain files. The distinctive part is that the index *is your bias*, kept unaveraged and portable.

**Does it replace my photo library or files?**
No, it points *into* them. For files, photos and pages AIS is an index of pointers, not a store of copies: a photo stays in Immich, a file on disk, a page at its URL. You file the *reference* under your own keys and recall it by association; the silo keeps the bytes. It does not compete with Immich or the filesystem, it sits across them as the one associative layer that remembers where a thing is and why it mattered. An index of pointers, not another silo to fill. (Secrets are the one exception: those it stores inline, encrypted, see below.)

**Can it hold passwords? Is it a password manager?**
Yes. A secret is stored encrypted inline (`-e`), so a login lives right next to the context it belongs to, and two things set it apart from a built-in manager. It is **cross-platform**: Apple Keychain and Google Password Manager are locked to one ecosystem, while AIS is the same plain-text index on Windows, macOS, Linux, Android and the CLI, so your secrets travel with you. And it is **agent-safe**: decryption is interactive (a passphrase you supply at a terminal or in the app), so an agent reading your index sees an opaque `aisc:` marker, not the secret, with no master key or unlocked vault to drain. What it is *not* is a bulk web-login manager: no autofill, no generation, no shared vaults, so for hundreds of site logins a dedicated cross-platform manager is still more convenient. See [`about.txt`](doc/about.txt).

## Learn more

| Read | For |
|------|-----|
| [`doc/USING.txt`](doc/USING.txt) | How to use it, GUI on every OS (plain steps, no jargon). |
| [`doc/about.txt`](doc/about.txt) | What AIS is, and what it is not. |
| [`doc/SYNC.md`](doc/SYNC.md) | Sync your index between devices: encrypted LAN sync (`--sync`), or through a shared folder a tool like Syncthing keeps in sync (`--sync-folder`). |
| [`doc/OVERVIEW.md`](doc/OVERVIEW.md) | Design philosophy, status, provenance. |
| [`doc/ROADMAP.md`](doc/ROADMAP.md) | What's planned, and where to help. |
| [`doc/dev/LAYOUT.md`](doc/dev/LAYOUT.md) | On-disk format and module map. |
| `man ais` | Full command reference. |

## Claude Code skill

This repo ships a Claude Code skill at [`.claude/skills/ais/SKILL.md`](.claude/skills/ais/SKILL.md): it teaches a coding agent to recall from and store to your ais index by keyword, near-zero-token recall instead of re-searching. Copy it into your own project's `.claude/skills/` to give your agent the same.

## See also

[agent-recipes](https://github.com/Anode1/agent-recipes) - short prompts for working with coding agents; ais is one of them (store and recall procedures instead of re-deriving them).

## License

New code (`c/`): GNU GPL v2 or later (per source headers). Legacy material (`legacy/`) under its original Apache License 2.0. Author: Vasili Gavrilov (GitHub [Anode1](https://github.com/Anode1)).
