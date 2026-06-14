# AIS — Associative Indexing Service

**Your memory, yours to keep.**\
*Models average everyone. Keep what's only yours.*

A personal associative index: file anything under keys, recall it by keys — plain text on your own disk.

## Download

The latest stable build for every platform — this link always points at the current release, never an old one:

> **<https://github.com/Anode1/ais/releases/latest>**

- **Windows** — run the `…-installer.exe` (per-user, no admin).
- **macOS / Linux** — unzip the `…-<os>-<arch>.zip` and run `ais` (add it to your PATH to use it anywhere).

Then `ais --serve` opens the GUI in your browser.

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

## Learn more

| Read | For |
|------|-----|
| [`doc/USING.txt`](doc/USING.txt) | How to use it, GUI on every OS (plain steps, no jargon). |
| [`doc/about.txt`](doc/about.txt) | What AIS is, and what it is not. |
| [`doc/OVERVIEW.md`](doc/OVERVIEW.md) | Design philosophy, status, provenance. |
| [`doc/dev/LAYOUT.md`](doc/dev/LAYOUT.md) | On-disk format and module map. |
| `man ais` | Full command reference. |

## License

New code (`c/`): GNU GPL v2 or later (per source headers). Legacy material (`legacy/`) under its original Apache License 2.0. Author: Vasili Gavrilov (GitHub [Anode1](https://github.com/Anode1)).
