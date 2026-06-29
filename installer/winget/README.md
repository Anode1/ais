# winget manifests

Source for submitting AIS to the Windows Package Manager community repo
([microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs)), so users can
`winget install Anode1.AIS`. These files are NOT used by the build, they are the
submission payload, kept here per version.

`PackageIdentifier` is `Anode1.AIS` (Publisher.Package). Change it if you want a
different publisher token (it must match across all three files and the repo path).

## Validate locally (on Windows, with winget installed)

```
winget validate --manifest installer\winget\0.2.3
winget install  --manifest installer\winget\0.2.3   # installs from these files
```

## Submit

Easiest is `wingetcreate` (it fills the hash and opens the PR for you):

```
wingetcreate update Anode1.AIS --version 0.2.3 ^
  --urls https://github.com/Anode1/ais/releases/download/v0.2.3/ais-v0.2.3-windows-x86_64-installer.exe ^
  --submit
```

Or by hand: copy the three YAML files into a fork of winget-pkgs at
`manifests/a/Anode1/AIS/0.2.3/` and open a PR. Microsoft's CI validates the
installer download, the SHA256, and silent-install, then merges.

(Reputation note: winget's own install flow is a less alarming path than a raw
browser download, but a SmartScreen prompt can still appear until the installer
is code-signed, see the SignPath track on the roadmap.)

## Regenerate for a new version

For each release, copy `0.2.3/` to the new `<x.y.z>/` and update:

- `PackageVersion` (all three files) -> the new version
- `InstallerUrl` -> `.../releases/download/v<x.y.z>/ais-v<x.y.z>-windows-x86_64-installer.exe`
- `InstallerSha256` -> the value in that release's
  `ais-v<x.y.z>-windows-x86_64-installer.exe.sha256` asset, uppercased
- `ReleaseDate` -> the release date

The `ProductCode` (`{BE2750EB-72A2-4016-AFD0-98818CBB51E7}_is1`) is the Inno
`AppId` + `_is1`; it stays constant across versions (don't change the AppId in
`ais.iss`, or winget upgrade detection breaks).
