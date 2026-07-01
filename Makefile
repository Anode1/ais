# Top-level convenience. The C engine lives in c/; root delegates the build
# profiles and adds install/uninstall using the standard GNU variables, so
# distro maintainers and Homebrew can package it with no patching:
#
#   make            build c/ais     (optimized, dynamic; the portable artifact on macOS/BSD)
#   make static     build a static, dependency-free c/ais   (Linux only)
#   make ut         build and run all the tests (codeut / cliut / uiut are the layers)
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

# Version stamped into the installed man page, from the git tag (same single
# source as c/Makefile / `ais --version`). Override: make AIS_VERSION=x.y.z
AIS_VERSION := $(shell git describe --tags --always --dirty 2>/dev/null | sed 's/^v//')
ifeq ($(strip $(AIS_VERSION)),)
AIS_VERSION := 0.0.0-dev
endif

.PHONY: all codeut cliut uiut ut codeut-asan codeut-ubsan hooks clean static install install-strip install-desktop uninstall distclean dist

# Build the engine in c/, then copy the binary up to the repo root as ./ais, so
# from a source checkout you can run ./ais instead of ./c/ais.
all:
	$(MAKE) -C c all
	@cp -f c/ais ais && echo "built ./ais"

static:
	$(MAKE) -C c static
	@cp -f c/ais ais && echo "built ./ais (static)"

# codeut = the C engine tests (c/tests.c, in-process) -- the fast inner loop.
codeut:
	$(MAKE) -C c ut

# codeut-asan / codeut-ubsan = the engine tests under AddressSanitizer / UBSan:
# memory and UB errors abort with a file:line report instead of passing silently.
# Run before tagging; also the backstop in .github/workflows/sanitizers.yml.
codeut-asan:
	$(MAKE) -C c ut-asan
codeut-ubsan:
	$(MAKE) -C c ut-ubsan

# Enable the git pre-push hook so the sanitizers run before every push -- a
# memory/UB bug can't reach the remote or turn CI red. Points core.hooksPath at
# the committed scripts/hooks. Undo: git config --unset core.hooksPath.
hooks:
	@git config core.hooksPath scripts/hooks
	@echo "git hooks -> scripts/hooks  (pre-push runs make codeut-asan + codeut-ubsan)"

clean:
	$(MAKE) -C c clean
	-rm -f ais

# cliut = the CLI black-box: the real binary through the shell (argv, pipes, -v -).
cliut: all
	@sh tests/cli.sh "$(CURDIR)/c/ais"

# uiut = the web GUI: `ais --serve`'s HTTP API + the rendered page in headless
# Chrome (SKIPs without curl / Chrome). Runs both web-layer scripts.
uiut: all
	@sh tests/gui/serve.sh "$(CURDIR)/c/ais"
	@sh tests/gui/ui.sh "$(CURDIR)/c/ais"

# ut = the whole suite: codeut + cliut + uiut + the wrapper build-checks, each
# layer PASS / FAIL / SKIP. The one command to run before committing -- a green
# core with a skipped GUI layer is still committable.
ut: all
	@sh tests/run.sh

# dist = a release bundle for THIS platform into releases/ (kept across runs;
# md5 sidecar included). Run it on each platform; see scripts/dist.sh.
dist:
	@sh scripts/dist.sh

install:
	@test -f c/ais || { echo "build first: run 'make' or 'make static'"; exit 1; }
	$(INSTALL) -d "$(DESTDIR)$(bindir)" "$(DESTDIR)$(mandir)"
	$(INSTALL_PROGRAM) c/ais "$(DESTDIR)$(bindir)/ais"
	sed 's/@VERSION@/$(AIS_VERSION)/' man/ais.1 > ais.1.stamped
	$(INSTALL_DATA) ais.1.stamped "$(DESTDIR)$(mandir)/ais.1"
	@rm -f ais.1.stamped
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
