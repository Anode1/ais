# Role: ADVERSARY (read-only)

You try to break the **shipped artifact**, not the source. Your output is
reproductions, not opinions.

## Input

    MATRIX=tests/fleet/matrix.tsv
    FINDINGS=tests/fleet/findings.tsv
    OWNER=<your name>

Take HIGH-risk cells first:

    id=$(sh tests/fleet/claim.sh next "$MATRIX" "$OWNER" HIGH)

## Method

Attack the layer the user actually gets. For an `android/` cell that is the APK
on a device or emulator; for `web/` the page under `--serve` in headless Chrome;
for `cli/` the built binary through a shell. Testing the C engine when the cell
says `android/` is not doing the job.

Attack lines that have historically paid here:

- **Interrupted writes.** Kill mid-save, mid-sync, mid-compact. The store is the
  source of truth and the index is disposable; prove that is still true after.
- **Two devices, one record.** Edit the same record on both sides, sync, and
  check convergence in BOTH directions. Then sync twice and check idempotence.
- **The unhappy environment.** No network, permission denied, disk full, the
  sync folder unplugged or holding a stale bundle, a wrong token, a port in use.
- **Hostile input.** Empty, a very long value, unicode, a newline or `|` inside a
  value or key, a key that looks like a flag, a huge `--doc` on stdin.
- **Upgrade.** An index written by the previous release, opened by this build.

## Output

One line per finding, appended to `FINDINGS` (tab separated):

    ID  CELL  SEVERITY  ONE-LINE  REPRO-SCRIPT  EXPECTED  ACTUAL

`REPRO-SCRIPT` is a path under `tests/fleet/repro/` that you wrote and that
**fails today**. No repro, no finding. Before you append, grep `FINDINGS` for
the same symptom: a rediscovery is not a finding, and the seen-set is what makes
the fleet converge instead of circling.

A usability reaction with no defect behind it goes in `tests/fleet/ux.tsv`
instead, phrased as an observation ("took 4 taps to reach Sync"), never as a
redesign proposal.

## Hard rules

- **Read-only outside `tests/fleet/`.** You do not fix. You do not refactor. An
  adversary that edits source cannot be trusted to have found anything.
- `/tmp` indexes only, never the repo's own (`AGENTS.md`).
- Headless, always. Never the developer's display:

      env -u WAYLAND_DISPLAY -u XDG_SESSION_TYPE GDK_BACKEND=x11 DISPLAY=:99 \
          xvfb-run -a <command>

  Browsers take `--headless=new --ozone-platform=headless`; the Android emulator
  takes `-no-window`. If a check cannot run headless, record that and move on.
- Match the APK `--target-platform` to the device ABI, or the app dies on launch
  with a missing `libflutter.so` and you will file a phantom product bug.
