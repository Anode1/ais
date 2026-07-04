# Native UI test: Flutter sync flow under Xvfb

`run.sh` drives the **real** Host/Join sync UI of the Flutter desktop app in a
headless X server and asserts a record crosses to and from a CLI peer. It is the
native-surface counterpart to the web CDP suite (`ant uiut`): where CDP speaks to
a DOM, this speaks pixels and X events, so it works on Flutter's single Skia
surface, which has no DOM and no accessibility tree for uiautomator to query.

## Why this shape

A Flutter app paints everything to one composited surface. Tree-based drivers
(uiautomator, Appium, CDP) see nothing inside it. The renderer-agnostic answer is
the old Xvfb one: run the app on a virtual display, capture the framebuffer as an
image, and inject input as X events. That is exactly what this does, and it is why
it also works for GTK, Qt, SDL, or any other toolkit on the same display.

The app's UI is plain Flutter widgets, so the **linux** build renders the same
Sync sheet and Host/Join dialogs as Android. Testing it on desktop needs no
emulator: faster, lighter, and CI-friendly. Only the two genuinely native bits
fall out (the `ais://` deep-link intent and the camera QR scan); those are
platform-plugin code, not UI.

## Run

```bash
# headless (default): Xvfb on :99, one pass, assert, tear down
./run.sh

# watch it on your own display instead of Xvfb
HEADED=1 ./run.sh

# keep the throwaway stores + per-step screenshots for inspection
KEEP=1 ./run.sh
```

## Requirements

- `Xvfb` and `xdotool` (X server + input injection).
- ImageMagick `import` (per-step screenshots).
- Mesa software GL (`libgl1-mesa-dri`): Flutter draws through EGL/OpenGL and Xvfb
  has no GPU, so the script forces `LIBGL_ALWAYS_SOFTWARE=1` (llvmpipe). Without
  the driver the surface renders solid black.
- A Flutter linux toolchain (`flutter build linux` must work).

```bash
# Debian / Ubuntu
sudo apt install -y xvfb xdotool imagemagick libgl1-mesa-dri
```

On a **Wayland** session the script forces `GDK_BACKEND=x11` and unsets
`WAYLAND_DISPLAY`, so the app uses Xvfb (headless) instead of rendering on your
real screen where `xdotool` cannot reach it. Both are handled automatically.

## How it works

1. Build `c/ais` and the Flutter linux bundle if missing.
2. Start Xvfb (unless `HEADED=1`).
3. Create two throwaway indexes and seed one record in each. The app has no
   `-f` flag, so its store is isolated by launching it with a CWD that contains a
   `.ais/`; the engine resolves the nearest one, git-style, never touching `~/.ais`.
4. The CLI peer hosts (`ais --sync --serve`) and prints a one-time token; the
   script scrapes it.
5. `xdotool` drives the app: open Sync, choose Join, type the peer address and
   token, confirm. One action per step, a screenshot after each into `shots/`.
6. Assert both stores converged: the peer's record reached the app and the app's
   record reached the peer. Deterministic, off the store files, no OCR.

## Tuning the click targets

The five click coordinates near the top of `run.sh` are **pre-tuned** for the
pinned 1280x720 window and the fixed store path (the "sync" link follows the
store-path text, which is why the path is fixed, not `mktemp`). Re-tune only if
the UI layout changes: run it, read `shots/NN-*.png`, set the constants, rerun.
If the layout churns often, graduate the drive step to `integration_test`, which
taps widgets by `Key` and does not drift; keep this harness as the
renderer-agnostic outer loop.

## Exit

`0` and `PASS` when records converge both ways; non-zero and `FAIL` otherwise,
with the host log and screenshots for triage.
