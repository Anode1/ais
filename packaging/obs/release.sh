#!/bin/sh
# Move the OBS packaging to a released tag: download its tarball, rewrite the
# version in ais.spec and debian.changelog, and regenerate ais.dsc with the
# tarball's checksums. Run from anywhere; files land in packaging/obs/.
#
#   sh packaging/obs/release.sh 0.3.37
#
# Then copy packaging/obs/* into the osc checkout and commit (README.md).
set -eu
v=${1:?usage: release.sh X.Y.Z}
cd "$(dirname "$0")"
tar="ais-$v.tar.gz"
curl -fsSL -o "$tar" "https://github.com/Anode1/ais/archive/refs/tags/v$v.tar.gz"
sed -i "s/^Version:.*/Version:        $v/" ais.spec
sed -i "/^%changelog/,\$d" ais.spec
cat >> ais.spec <<SPEC
%changelog
* $(LC_ALL=C date '+%a %b %d %Y') Vasili Gavrilov <gavr144@gmail.com> - $v-1
- Release $v
SPEC
cat > debian.changelog <<CL
ais ($v-1) unstable; urgency=medium

  * New upstream release.

 -- Vasili Gavrilov <gavr144@gmail.com>  $(LC_ALL=C date -R)
CL
cat > ais.dsc <<DSC
Format: 1.0
Source: ais
Binary: ais
Architecture: any
Version: $v-1
Maintainer: Vasili Gavrilov <gavr144@gmail.com>
Homepage: https://github.com/Anode1/ais
Standards-Version: 4.6.2
Build-Depends: debhelper-compat (= 13)
DEBTRANSFORM-TAR: $tar
Checksums-Sha256:
 $(sha256sum "$tar" | cut -d' ' -f1) $(wc -c < "$tar") $tar
Files:
 $(md5sum "$tar" | cut -d' ' -f1) $(wc -c < "$tar") $tar
DSC
echo "packaging/obs at $v: $tar, ais.spec, ais.dsc, debian.changelog"
