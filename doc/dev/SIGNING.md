# Windows code signing (SignPath, free for open source)

Unsigned Windows downloads trigger SmartScreen's "Windows protected your PC /
Unknown publisher". The release workflow can sign the installer with
**SignPath.io**, which offers **free code signing for OSS projects** -- a good
fit for AIS (GPL, on GitHub).

Signing is **optional and additive**: the `sign-windows` job in `release.yml`
runs only when SignPath is configured (the repo variable
`SIGNPATH_ORGANIZATION_ID` is set). Until then, releases ship unsigned and the
job is skipped -- nothing breaks.

## One-time setup

1. Apply for the **open-source plan** at https://signpath.io and create an
   **organization**.
2. Install the **SignPath GitHub app** and connect this repository (so SignPath
   can fetch the build artifact to sign).
3. In SignPath create:
   - a **project** (e.g. slug `ais`),
   - an **artifact configuration** that signs the `*-windows-x86_64-installer.exe`
     inside the `ais-windows-x86_64` artifact (Authenticode),
   - a **signing policy** (e.g. slug `release-signing`).
4. Create a SignPath **API token** for a CI user.

## Repository configuration (GitHub -> Settings)

Secret:
- `SIGNPATH_API_TOKEN` -- the SignPath API token.

Variables (Settings -> Secrets and variables -> Actions -> Variables):
- `SIGNPATH_ORGANIZATION_ID`   -- enables the signing job when set
- `SIGNPATH_PROJECT_SLUG`        (e.g. `ais`)
- `SIGNPATH_POLICY_SLUG`         (e.g. `release-signing`)
- `SIGNPATH_ARTIFACT_CONFIG_SLUG` (the artifact configuration slug)

## How it flows in release.yml

1. `build-windows` builds `…-installer.exe` and uploads it as `ais-windows-x86_64`
   (exposing the artifact id).
2. `sign-windows` submits that artifact to SignPath, downloads the **signed**
   installer, refreshes its `.sha256`, and uploads `ais-windows-signed`.
3. `publish` assembles the release, **overlaying the signed installer** over the
   unsigned one, then attaches everything.

## Notes

- SmartScreen reputation still builds over time with a standard (OV-style)
  certificate; an EV certificate clears the warning immediately. SignPath's OSS
  certificate is the former.
- Signing covers the **installer** here. To also sign the bundled `ais.exe`
  (so running it directly never warns), add it to the SignPath artifact
  configuration.
- Independent of Cygwin vs the native Windows build -- both produce an unsigned
  `.exe` that this same job can sign.
