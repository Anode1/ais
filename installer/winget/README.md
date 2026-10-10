# winget manifests

The submission payload for the Windows Package Manager community repository
([microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs)), kept here
per version, so users can `winget install Anode1.AIS`. The build does not use
these files.

The package is the release zip, `ais-v<x.y.z>-windows-x86_64.zip`, declared as
`InstallerType: zip` with `NestedInstallerType: portable`: winget unpacks the
whole zip under its packages folder and puts `ais` and `ais-gui` on the PATH
through the two `PortableCommandAlias` entries. `ais-web.bat`, `USING.txt` and
the man page land in the same folder. No installer runs, so there is no
product code and nothing to uninstall but the folder, which `winget uninstall`
removes.

`0.2.3/` is the Inno Setup installer manifest from before the Windows build was
dropped and recovered; it is kept as history only. Its `InstallerUrl` is dead.

## Regenerate for a new version

Copy the newest directory to `<x.y.z>/` and change, in all three files,
`PackageVersion`; in the installer file, the two `RelativeFilePath` entries (the
folder inside the zip carries the version), `ReleaseDate`, `ReleaseNotesUrl`,
`InstallerUrl` and `InstallerSha256` (the value in the release's `.zip.sha256`
asset, uppercased); in the locale file, `ReleaseNotesUrl`.

## Validate (on Windows, with winget installed)

```
winget validate --manifest installer\winget\<x.y.z>
winget install  --manifest installer\winget\<x.y.z>
```

## Submit

A pull request to microsoft/winget-pkgs that adds the three files at
`manifests/a/Anode1/AIS/<x.y.z>/`. `wingetcreate` does the fork, the branch
and the PR:

```
wingetcreate update Anode1.AIS --version <x.y.z> --urls <InstallerUrl> --submit
```

The first version of a package goes through `wingetcreate new` or a PR by hand.
Microsoft's CI downloads the zip, checks the hash and the nested paths, and a
moderator merges. Later versions of the same package are checked by the
automation alone.

The binaries are not code-signed, so SmartScreen can still warn on the first
run of `ais-gui.exe`; the roadmap's SignPath item covers that.
