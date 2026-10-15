# Reference spec for the openSUSE Build Service project, kept in the repo so it
# moves with the build system. packaging/obs/release.sh rewrites Version.
# See packaging/obs/README.md and doc/dev/PACKAGING.md.

Name:           ais
Version:        0.3.37
Release:        1%{?dist}
Summary:        Associative index: file anything under your own keys, recall it by keys
# The binary links the vendored Monocypher (c/crypto/), hence the second clause.
License:        (GPL-2.0-or-later OR MIT) AND (BSD-2-Clause OR CC0-1.0)
URL:            https://github.com/Anode1/ais
Source0:        %{url}/archive/refs/tags/v%{version}.tar.gz#/%{name}-%{version}.tar.gz
BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  desktop-file-utils

%description
A plain-text associative index. File a note, link, path or command under any
keys you choose and recall it later by any subset of them. One C99 binary with
no daemon and no dependencies; the store is text files you can grep and repair.
`ais --serve` opens a local web page on 127.0.0.1 for the same index.

%prep
%autosetup -n %{name}-%{version}

# AIS_VERSION normally comes from `git describe`, absent in a tarball; without it
# the binary reports 0.0.0-dev. It is passed to every make invocation.
%build
%make_build AIS_VERSION=%{version} CFLAGS="%{build_cflags}" LDFLAGS="%{build_ldflags}"

# The test binary is not shipped. LDFLAGS is emptied for it because the 0.3.36
# Makefile links it with the hardening -pie but compiles it without -fPIE;
# from the next release it takes CFLAGS too and the override can go.
%check
make AIS_VERSION=%{version} LDFLAGS= codeut
make AIS_VERSION=%{version} LDFLAGS= cliut
desktop-file-validate %{buildroot}%{_datadir}/applications/ais.desktop || :

%install
make AIS_VERSION=%{version} prefix=%{_prefix} DESTDIR=%{buildroot} install
make AIS_VERSION=%{version} prefix=%{_prefix} DESTDIR=%{buildroot} install-desktop
desktop-file-validate %{buildroot}%{_datadir}/applications/ais.desktop

%files
%license COPYING LICENSE-MIT
%doc README.md doc/USING.txt doc/command_line.txt doc/SYNC.md
%{_bindir}/ais
%{_mandir}/man1/ais.1*
%{_datadir}/applications/ais.desktop
%{_datadir}/icons/hicolor/256x256/apps/ais.png

%changelog
* Wed Oct 14 2026 Vasili Gavrilov <gavr144@gmail.com> - 0.3.37-1
- Release 0.3.37
