# Driving the GUIs automatically (web, Flutter, native)

How to develop and test a front-end here without a human clicking anything, and
without a window ever opening on the developer's screen. `GUI.md` says what the
surfaces must look like; this says how to drive them and prove they work.

Every layer below exits **0 pass / 1 fail / 77 SKIP** and is one `layer` line in
`tests/run.sh`, so `make ut` reports it and an absent toolchain is a SKIP rather
than a failure. That convention is what lets this file grow without making the
suite unrunnable on a machine that lacks a browser, a Dart SDK or a device.

---

## The rule that comes before everything

**Never open a window on the real display.** The rule, and the exact invocation
every recipe below uses, is in `../../AGENTS.md`. A `PreToolUse` hook blocks the
unsafe forms; treat a block as correct and fix the command.

---

## Web: `ais --serve` and the PWA

Two pages over one `/api` (see `GUI.md`), driven by ONE driver because their
element ids are deliberately identical. Which layer covers what, and how to run
each: `../../tests/README.md`.

`tests/gui/cdp.c` is a Chrome DevTools Protocol client in C99 -- no chromedriver,
no Puppeteer, no dependency beyond libc and POSIX sockets. It speaks the same
wire protocol those tools do. `cdptest.c` uses it to navigate, focus, type, press
Enter and read the live DOM back. `AIS_CDP_DEBUG=1` traces the frames.

Start the server with `AIS_NO_OPEN=1` so `--serve` does not launch a browser.

### Seeing the page

An agent cannot review a layout it has not looked at. Render it to a PNG and
open the file:

    c/ais -f /tmp/x --init && AIS_NO_OPEN=1 c/ais -f /tmp/x --serve 8080 &
    tests/shot/shot.sh http://127.0.0.1:8080/ /tmp/gui.png

Guessing at layout from source is how a control ends up off-screen. Look at it.

### The trap this layer already caught

`inter.sh` failed about half the time on a cold run, and the cause was not the
test: the page's async loaders painted into `#out` **after** their `await`, so a
response that landed once the user had moved on repainted the old view over the
new one. The fix was a `viewGen` counter each loader captures before its fetch
and re-checks after. A flaky UI test is worth reading twice before it is
rerun -- it was reporting a real race.

---

## Flutter: three different jobs, three different layers

Do not conflate them. Analysis is not a widget test, and a widget test is not
proof that the app works on a device; the three layers are listed in
`../../tests/README.md`.

Keep logic that can be tested without a device OUT of widget code -- the
`test/` directory covers `saveOutcomeMessage`, `tagsUpdateMessage` and the like
because they are plain functions. That is the cheapest layer; use it first.

### Why Flutter cannot be driven like the web

Flutter paints into one Skia surface. There is no DOM and no accessibility tree
for an external tool to query, so nothing here can ask "where is the Sync
button?". Drivers fall back to **coordinates**, which is the whole difficulty:
coordinates rot the moment a layout changes, and a rotted driver keeps passing
while testing the wrong screen.

Two ways out, in order of preference:

1. **Give the app a stable hook that is not a pixel.** A keyboard shortcut
   (desktop) or a deep link (mobile) does not move when the layout does. Both
   exist here and both are real features, not test scaffolding.
2. **Derive coordinates from the display**, never hardcode them, and put them on
   *dialogs* (which are centred) rather than page furniture (which reflows).

### Linux desktop (`flutter-sync.sh` -> `app/flutter/uitest/run.sh`)

Builds the desktop app, drives it under Xvfb with `xdotool`, asserts a record
crosses BOTH ways against a CLI peer. It opens the Sync sheet with
**Ctrl+Shift+S** rather than clicking, precisely so a layout change cannot
silently disconnect it, and it asserts on the two stores, never on the pixels.

The app has no `-f` flag, so its index is isolated by launching it with a CWD
that holds a `.ais/`: the engine resolves the nearest one, git-style, and never
touches `~/.ais`.

Needs `clang`, `ninja` and `libgtk-3-dev` for the build, plus `Xvfb`, `xdotool`,
ImageMagick `import` (per-step screenshots into `shots/`) and Mesa software GL
(`libgl1-mesa-dri`: Flutter draws through EGL/OpenGL, Xvfb has no GPU, and
without llvmpipe the surface renders solid black). On Pop!_OS the desktop
toolchain cannot be installed without downgrading the running session's Wayland
libraries, so **there it SKIPs** and the Android layer is the one that runs.

`run.sh` takes `HEADED=1` to watch it on a real display (deliberate, and not
during automated work) and `KEEP=1` to leave the throwaway stores and the
screenshots behind. Its five click coordinates are pre-tuned for a pinned
1280x720 window and a fixed store path; re-tune by reading `shots/NN-*.png` after
a failing run. If the layout starts churning, graduate the drive step to
`integration_test`, which taps widgets by `Key` and does not drift, and keep this
harness as the renderer-agnostic outer loop.

### Android, against the real APK (`flutter-sync-android.sh`)

The better target anyway: an APK is what users install. Full recipe, because
every step below cost something to learn.

**Boot headlessly.** `-no-window` is genuinely headless -- no X server involved:

    $ANDROID_SDK_ROOT/emulator/emulator -avd <name> \
        -no-window -no-audio -no-boot-anim -no-snapshot -gpu swiftshader_indirect

Then wait for `getprop sys.boot_completed` to be `1`; `adb wait-for-device`
returns long before the system is usable. KVM makes this bearable -- check
`/dev/kvm` is readable (an ACL may grant it even when your groups do not).

**Match the ABI or you will chase a phantom bug.** A `--target-platform` that
does not match `getprop ro.product.cpu.abi` produces an APK whose
`libflutter.so` is missing, and the app dies at launch with `UnsatisfiedLinkError`
-- which reads exactly like a product crash. Derive it:

    abi=$(adb shell getprop ro.product.cpu.abi | tr -d '\r')   # x86_64 -> android-x64

**Reach the host.** From the emulator, the host is `10.0.2.2`. So a CLI peer
started on the developer's machine with `ais --sync --serve PORT` is reachable at
`http://10.0.2.2:PORT` -- which is how one end of the sync can be a normal
`c/ais` process the test fully controls.

**Drive by deep link, not by hunting for buttons.** The app registers
`ais://sync?host=..&token=..` -- the same link the QR carries -- so the entire
scan-to-pair path can be triggered with:

    adb shell am start -a android.intent.action.VIEW \
        -d "ais://sync?host=10.0.2.2%3A$PORT\&token=$tok"

That lands on a **prefilled** Join dialog and reduces the whole flow to one
confirming tap. Scale that tap from the real display so it survives a different
device size:

    size=$(adb shell wm size | tr -d '\r' | awk '{print $3}')   # e.g. 1080x2400
    adb shell input tap $((${size%x*} * 748 / 1000)) $((${size#*x} * 647 / 1000))

**Assert on data, never on pixels.** The app's index is app-private, and a debug
build can read it back:

    adb shell run-as com.aisindex.ais cat app_flutter/ais/store

Check that the peer's record is in the app's store AND that the app's records
reached the peer's index. A screenshot is for diagnosing a failure, not for
deciding one.

**Screenshot when it fails**, and look at it:

    adb exec-out screencap -p > /tmp/fail.png

**Be careful whose data you are on.** A device may hold a real index. The layer
is additive by default -- it merges one proof record and asserts that record
crossed. `AIS_ANDROID_CLEAR=1` wipes the app's data for a clean-room run, and is
opt-in for that reason. It also SKIPs unless a device is already attached, so a
normal `make ut` stays fast; `AIS_ANDROID_BOOT=1` lets it start one.

---

## Making a GUI test worth having

- **Prove it can fail.** A test that has never gone red is a claim, not a test.
  Break the thing on purpose -- feed a wrong token, re-add a buffer you removed --
  and confirm the failure, then restore. Two of the layers here were tuned only
  because a deliberate regression walked straight past the first version.
- **Wire it into `tests/run.sh` the day you write it.** The Flutter sync harness
  existed for weeks, wired into nothing, and stopped testing sync entirely when a
  control moved. Nobody noticed, because nothing ran it.
- **Assert on the store, not the screen.** Every front-end here is a view over one
  engine; the durable assertion is what ended up in the index.
- **A throwaway index, always.** `mktemp -d`, never the repo's `.ais` and never a
  personal `~/.ais`. A subagent once wrote junk records into a real index by
  running `ais` with no `-f`; scope agents to a disposable one explicitly.
- **A SKIP names what is missing.** A layer that cannot run must exit 77 with a reason naming
  the missing tool. A SKIP that reads as a PASS is worse than a failure.
