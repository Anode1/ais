# Top-level convenience. The C engine lives in c/; root delegates the build
# profiles and adds install/uninstall using the standard GNU variables, so
# distro maintainers and Homebrew can package it with no patching:
#
#   make            build c/ais     (optimized, dynamic; the portable artifact on macOS/BSD)
#   make static     build a static, dependency-free c/ais   (Linux only)
#   make check      build and run the unit tests
#   sudo make install                 -> /usr/local/bin (on PATH)
#   make install prefix=$HOME/.local  per-user, no sudo
#   make install DESTDIR=/tmp/pkg     stage for a package (deb/rpm/ebuild/brew)
#
# No ./configure -- configuration is just these make variables.

prefix ?= /usr/local
bindir  = $(prefix)/bin
datadir = $(prefix)/share
mandir  = $(datadir)/man/man1
desktopdir = $(datadir)/applications
icondir    = $(datadir)/icons/hicolor/256x256/apps

INSTALL         ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 755
INSTALL_DATA    ?= $(INSTALL) -m 644

.PHONY: all ut clean static install install-strip install-desktop uninstall distclean check checkcli dist

# Build the engine in c/, then copy the binary up to the repo root as ./ais, so
# from a source checkout you can run ./ais instead of ./c/ais.
all:
	$(MAKE) -C c all
	@cp -f c/ais ais && echo "built ./ais"

static:
	$(MAKE) -C c static
	@cp -f c/ais ais && echo "built ./ais (static)"

ut:
	$(MAKE) -C c ut

clean:
	$(MAKE) -C c clean
	-rm -f ais

# check = C unit tests (the ais.h API) + CLI/streaming tests (the binary).
check: all
	$(MAKE) -C c check
	@sh tests/cli.sh "$(CURDIR)/c/ais"

# checkcli = just the end-to-end binary tests (assumes ais is built).
checkcli: all
	@sh tests/cli.sh "$(CURDIR)/c/ais"

# dist = a release bundle for THIS platform into releases/ (kept across runs;
# md5 sidecar included). Run it on each platform; see scripts/dist.sh.
dist:
	@sh scripts/dist.sh

install:
	@test -f c/ais || { echo "build first: run 'make' or 'make static'"; exit 1; }
	$(INSTALL) -d "$(DESTDIR)$(bindir)" "$(DESTDIR)$(mandir)"
	$(INSTALL_PROGRAM) c/ais "$(DESTDIR)$(bindir)/ais"
	$(INSTALL_DATA) man/ais.1 "$(DESTDIR)$(mandir)/ais.1"
	@echo "installed $(DESTDIR)$(bindir)/ais"

install-strip: install
	strip "$(DESTDIR)$(bindir)/ais"

# install-desktop: GUI menu integration (optional; packagers may add it). Installs
# the freedesktop .desktop entry + the scalable icon, so `ais --serve` shows up in
# the application menu. Independent of `install` so it can be run on its own.
install-desktop:
	$(INSTALL) -d "$(DESTDIR)$(desktopdir)" "$(DESTDIR)$(icondir)"
	$(INSTALL_DATA) gui/ais-web.desktop "$(DESTDIR)$(desktopdir)/ais.desktop"
	$(INSTALL_DATA) icons/ais-256.png "$(DESTDIR)$(icondir)/ais.png"
	@echo "installed desktop entry + icon (run 'install' too for the binary)"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/ais" "$(DESTDIR)$(mandir)/ais.1" \
	      "$(DESTDIR)$(desktopdir)/ais.desktop" "$(DESTDIR)$(icondir)/ais.png"
	@echo "removed ais from $(DESTDIR)$(bindir)"

# distclean = the full reset: clean + remove everything generated, including the
# root ./ais and the releases/ that `dist` deliberately keeps across runs.
distclean:
	$(MAKE) -C c clean
	-rm -rf releases ais
