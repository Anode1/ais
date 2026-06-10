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
mandir  = $(prefix)/share/man/man1

INSTALL         ?= install
INSTALL_PROGRAM ?= $(INSTALL) -m 755
INSTALL_DATA    ?= $(INSTALL) -m 644

.PHONY: all ut clean static install install-strip uninstall distclean check checkcli
all ut clean static:
	$(MAKE) -C c $@

# check = C unit tests (the ais.h API) + CLI/streaming tests (the binary).
check: all
	$(MAKE) -C c check
	@sh tests/cli.sh "$(CURDIR)/c/ais"

# checkcli = just the end-to-end binary tests (assumes ais is built).
checkcli: all
	@sh tests/cli.sh "$(CURDIR)/c/ais"

install:
	@test -f c/ais || { echo "build first: run 'make' or 'make static'"; exit 1; }
	$(INSTALL) -d "$(DESTDIR)$(bindir)" "$(DESTDIR)$(mandir)"
	$(INSTALL_PROGRAM) c/ais "$(DESTDIR)$(bindir)/ais"
	$(INSTALL_DATA) man/ais.1 "$(DESTDIR)$(mandir)/ais.1"
	@echo "installed $(DESTDIR)$(bindir)/ais"

install-strip: install
	strip "$(DESTDIR)$(bindir)/ais"

uninstall:
	rm -f "$(DESTDIR)$(bindir)/ais" "$(DESTDIR)$(mandir)/ais.1"
	@echo "removed ais from $(DESTDIR)$(bindir)"

distclean:
	$(MAKE) -C c clean
