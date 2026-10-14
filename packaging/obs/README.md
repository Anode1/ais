# ais on the openSUSE Build Service

One OBS project builds `.deb` and `.rpm` packages for Debian, Ubuntu, Fedora and
openSUSE from the tag tarball and publishes a repository per distribution. The
files here are the package sources; `osc` pushes them.

| File | What |
| --- | --- |
| `ais.spec` | the rpm recipe (Fedora, openSUSE) |
| `ais.dsc`, `debian.*` | the Debian source; OBS lays `debian.X` out as `debian/X` and `DEBTRANSFORM-TAR` in the `.dsc` names the one shared tarball |
| `project.xml` | the project meta: which distributions and architectures build |
| `release.sh X.Y.Z` | downloads the tag tarball, rewrites the version in the spec, the changelogs and the `.dsc` checksums |
| `test.sh` | builds the rpm and the deb in Docker before a commit |

The tarball `release.sh` downloads is ignored by git and lives here only to be
copied into the checkout.

## Once

1. Account: https://build.opensuse.org, "Sign Up". The login is `USER` below.
2. Client: `pipx install osc` (or `apt install osc`). The first command that
   needs the server, `osc ls home:USER`, asks for the password and writes
   `~/.config/osc/oscrc`.
3. Project and package:

        sed "s/USER/$USER_OBS/g" ~/ais/packaging/obs/project.xml > /tmp/prj.xml
        osc meta prj -F /tmp/prj.xml home:$USER_OBS:ais
        printf '<package name="ais" project="home:%s:ais"><title>ais</title><description>Associative index</description></package>\n' "$USER_OBS" > /tmp/pkg.xml
        osc meta pkg -F /tmp/pkg.xml home:$USER_OBS:ais ais

4. First commit, from a checkout outside this repository:

        cd ~ && osc checkout home:$USER_OBS:ais ais
        cd home:$USER_OBS:ais/ais
        cp ~/ais/packaging/obs/ais-*.tar.gz ~/ais/packaging/obs/ais.spec ~/ais/packaging/obs/ais.dsc ~/ais/packaging/obs/debian.* .
        osc add * && osc commit -m 'ais 0.3.36'

5. Watch `osc results` until every row says `succeeded`; `osc buildlog Fedora_44 x86_64`
   shows one build. The install page users get, with the per-distribution
   commands, is
   `https://software.opensuse.org/download.html?project=home:USER:ais&package=ais`.
   Put that link in the README's Download section.

## Every release

    sh ~/ais/packaging/obs/release.sh X.Y.Z
    sh ~/ais/packaging/obs/test.sh
    cd ~/home:$USER_OBS:ais/ais
    osc rm ais-OLD.tar.gz
    cp ~/ais/packaging/obs/ais-X.Y.Z.tar.gz ~/ais/packaging/obs/ais.spec ~/ais/packaging/obs/ais.dsc ~/ais/packaging/obs/debian.* .
    osc add ais-X.Y.Z.tar.gz && osc commit -m 'ais X.Y.Z'

OBS rebuilds every target on commit and the repositories update when the builds
finish, usually within the hour.
