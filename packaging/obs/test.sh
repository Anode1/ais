#!/bin/sh
# Build the rpm (Fedora) and the deb (Debian) from the files here, in Docker,
# the way OBS will: the tag tarball plus ais.spec, or plus debian.* laid out as
# debian/. Needs the tarball release.sh downloads. About three minutes each.
#
#   sh packaging/obs/test.sh                 # FEDORA=fedora:43 DEBIAN=ubuntu:24.04 to vary
set -eu
cd "$(dirname "$0")"
v=$(sed -n 's/^Version: *//p' ais.spec)
test -f "ais-$v.tar.gz" || { echo "no ais-$v.tar.gz here: run release.sh $v first" >&2; exit 1; }

echo "== rpm on ${FEDORA:=fedora:44}"
docker run --rm -v "$PWD:/src:ro" "$FEDORA" bash -c '
  dnf -y -q install rpm-build gcc make desktop-file-utils rpmlint >/dev/null 2>&1
  mkdir -p ~/rpmbuild/SOURCES ~/rpmbuild/SPECS
  cp /src/ais-*.tar.gz ~/rpmbuild/SOURCES/ && cp /src/ais.spec ~/rpmbuild/SPECS/
  rpmbuild -ba ~/rpmbuild/SPECS/ais.spec > /tmp/build.log 2>&1 || { tail -30 /tmp/build.log; exit 1; }
  grep -E "passed, [0-9]+ failed" /tmp/build.log
  rpmlint ~/rpmbuild/SPECS/ais.spec ~/rpmbuild/RPMS/*/ais-[0-9]*.rpm | tail -12
  rpm -qlp ~/rpmbuild/RPMS/*/ais-[0-9]*.rpm'

echo "== deb on ${DEBIAN:=debian:12}"
docker run --rm -v "$PWD:/src:ro" "$DEBIAN" bash -c '
  apt-get -qq update >/dev/null
  DEBIAN_FRONTEND=noninteractive apt-get -qq install -y build-essential debhelper lintian >/dev/null 2>&1
  v=$(sed -n "s/^Version: *//p" /src/ais.spec)
  mkdir /b && cd /b && cp "/src/ais-$v.tar.gz" "ais_$v.orig.tar.gz" && tar xzf "ais_$v.orig.tar.gz" && cd "ais-$v"
  mkdir debian && for f in /src/debian.*; do cp "$f" "debian/${f##*/debian.}"; done
  dpkg-buildpackage -us -uc -b > /tmp/build.log 2>&1 || { tail -30 /tmp/build.log; exit 1; }
  grep -E "passed, [0-9]+ failed" /tmp/build.log
  lintian ../ais_*.deb | grep -v "root privileges" || true
  dpkg -c ../ais_*.deb | awk "{print \$6}" | grep -v "/$"'
