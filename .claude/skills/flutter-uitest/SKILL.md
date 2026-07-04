---
name: flutter-uitest
description: Run or extend the headless UI test for the Flutter desktop app's sync flow. Drives the real Host/Join UI under Xvfb with xdotool and asserts a record crosses to/from a CLI peer. Use when asked to UI-test the native Flutter app, verify the LAN sync UI end to end, or add a native (non-web) UI check. This is the renderer-agnostic counterpart to the web CDP suite; use it, not uiautomator, for Flutter (its single Skia surface has no DOM/accessibility tree).
---

# Flutter native UI test (Xvfb)

The harness lives at `app/flutter/uitest/run.sh` with a `README.md` beside it.
It runs the Flutter **linux** build (same widgets as Android), so no emulator is
needed. Pixels in, X events out, so it works where DOM/tree drivers are blind.

## When to use
- Verify the app's Host/Join LAN sync UI actually works, end to end, against a
  real `ais --sync --serve` peer.
- Add or adjust a native UI check for the Flutter desktop app.
- Prefer this over uiautomator/Appium/CDP for the Flutter app: those see nothing
  inside its single Skia surface. Use CDP only for the web GUI (`serve.c`).

## Run
```bash
cd app/flutter/uitest
./run.sh            # headless Xvfb, one pass, assert both directions, tear down
HEADED=1 ./run.sh   # watch on your own $DISPLAY
KEEP=1 ./run.sh     # keep throwaway stores + per-step screenshots
```
Exit `0` + `PASS` means records converged both ways. Non-zero + `FAIL` prints the
host log and leaves screenshots for triage.

## Requirements
- `Xvfb`, `xdotool`, ImageMagick `import`, and a working `flutter build linux`.
- Install the X tools: `sudo apt install -y xvfb xdotool imagemagick`.

## Extending it
- Click targets are constants near the top of `run.sh`, for the pinned 1280x720
  window. Tune once from `shots/NN-*.png`, then they are stable run to run.
- For churn-proof CI, move the drive step to Flutter `integration_test` (taps by
  `Key`, no coordinate drift) and keep this as the outer renderer-agnostic loop.
- Store isolation is by CWD: the app finds the nearest `.ais/`, so launch it from
  a temp dir. Never point a UI test at `~/.ais`.

## Notes
- The two truly native paths, the `ais://` deep-link intent and the camera QR
  scan, are out of scope here (platform plugins, not UI); test those on a device.
- Keep the harness dependency-light and one script, matching the project style.
