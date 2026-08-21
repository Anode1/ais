# Packaging ais for a distribution

For anyone putting `ais` into a package repository. Everything a maintainer
usually has to discover by reading the tree is written down here instead.

If something in this page is wrong or awkward for your distro's rules, that is a
bug: open an issue and it gets fixed rather than patched around downstream.

## What you are packaging

One C99 binary, `ais`, plus a man page. No daemon, no service unit, no user
account, no post-install step. Nothing runs unless the user runs it, and nothing
listens on a socket unless the user asks for it (`ais --serve` binds 127.0.0.1;
`ais --sync` is started by hand).

## Build

    make                       # builds ./c/ais and copies it to ./ais
    make static                # optional: static binary, Linux only

- No `./configure`, no build framework, and no code generation.
- No network access at build time and nothing is downloaded.
- Dependencies: a C99 compiler and libc. That is the whole list. The crypto
  primitives (Monocypher) are vendored under `c/crypto/`, so there is nothing to
  unbundle unless your distro requires it, in which case say so in an issue.
- `CC`, `CFLAGS`, `CPPFLAGS`, `LDFLAGS` and `LDLIBS` are honoured; the project's
  own flags are appended, never substituted, so hardening flags pass through.

**Version, the one gotcha.** `AIS_VERSION` comes from `git describe --tags`. A
release tarball has no `.git`, so an unpatched build reports `0.0.0-dev`. Pass the
version explicitly to every make invocation:

    make AIS_VERSION=0.3.20

The same value is stamped into the binary (`ais --version`) and into the man
page's footer at install time.

## Test

    make codeut                # engine tests, in-process, fast
    make cliut                 # black-box: the built binary through a shell
    make ut                    # both, plus the GUI layer if its deps are present

`codeut` and `cliut` need no network, no display and no fixtures beyond the tree;
they are safe in a clean chroot. `make ut` additionally tries the GUI tests, which
need a browser or Flutter, so prefer the two specific targets in a package build.

Sanitizer runs (`make codeut-asan`, `make codeut-ubsan`) exist and are used in CI,
but they are not something a packager needs.

## Install

Standard GNU variables, staged installs supported:

    make prefix=/usr DESTDIR="$pkgdir" install
    make prefix=/usr DESTDIR="$pkgdir" install-desktop     # optional

| Variable | Default | What it controls |
| --- | --- | --- |
| `prefix` | `/usr/local` | everything below |
| `bindir` | `$(prefix)/bin` | the `ais` binary |
| `mandir` | `$(prefix)/share/man/man1` | `ais.1` |
| `desktopdir` | `$(prefix)/share/applications` | `ais.desktop` (install-desktop) |
| `icondir` | `$(prefix)/share/icons/hicolor/256x256/apps` | `ais.png` (install-desktop) |
| `DESTDIR` | empty | staging root |
| `INSTALL` | `install` | override for BSD/macOS toolchains |

`install` lays down the binary and the man page and nothing else.
`install-desktop` is separate because the desktop entry launches the local web GUI
and only makes sense on a graphical system; skip it for a server or minimal
package. `install-strip` is `install` plus `strip`, for distros that do not strip
in their own pipeline.

Nothing is written outside `$(DESTDIR)$(prefix)`, and no configuration file is
installed: an index is created by the user with `ais --init`, under a directory
they choose.

## Licences

- New code (`c/`, and the tree generally): **GPL-2.0-or-later**, see `COPYING`.
- `legacy/`, an imported earlier project: **Apache-2.0** under its own headers.
- `c/crypto/monocypher.[ch]`: vendored Monocypher, dual **CC0-1.0 / BSD-2-Clause**,
  see `c/crypto/README.md`.

Both licences should be listed if your distro records them per package.

## Runtime

- Data lives in `~/.ais` by default, or in the nearest `.ais/` directory at or
  above the working directory (git-style), or wherever `-f` points. The resolution
  order is in `ais --help` under INDEX LOCATION.
- No environment variables are consulted for configuration; `-f` is the only
  override. (`AIS_TTY` and `AIS_NO_OPEN` exist for tests and CI, not for users.)
- Nothing phones home, and there is no telemetry to disable.

## Documentation worth shipping

    doc/USING.txt          everyday use, plain steps
    doc/command_line.txt   the full `ais --help`, generated and test-pinned
    doc/SYNC.md            syncing between devices
    man/ais.1              installed by `make install`

## Releases and verification

Tags are `vMAJOR.MINOR.PATCH` (see `doc/dev/VERSIONING.md`). Release artifacts are
built by GitHub Actions from the tag, and each ships a matching `.sha256`:

    https://github.com/Anode1/ais/releases/latest

For a source package, prefer the tag tarball:

    https://github.com/Anode1/ais/archive/refs/tags/v0.3.20.tar.gz

## Reference PKGBUILD

`packaging/aur/PKGBUILD` in this repo is the Arch reference, kept next to the
build system so the two stay in step. The AUR copy is the one users install; when
this one changes, push the same change there, regenerate `.SRCINFO`
(`makepkg --printsrcinfo > .SRCINFO`) and bump `pkgrel`.

Note for whoever claims the AUR name: `ais` is also an acronym in marine AIS
tooling (`gnuais`, `aisdecoder`). Check the name is free before pushing, and if it
collides, `ais-index` is the fallback with `provides=('ais')`.
