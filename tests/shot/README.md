# shot.sh - screenshot the web GUI to a PNG

A tiny **shell** helper that renders a web page with the headless Chrome already
on the machine and saves it as a PNG. For ais, point it at the `ais --serve` GUI
so you (or an agent) can *look* at the page instead of guessing from the HTML/JS.
Nothing to install, nothing to build: just Chrome. No Python, no Java -- this is a
C project, so the only other "language" in here is `/bin/sh`.

## Usage

    ./shot.sh URL [OUT.png] [WIDTHxHEIGHT]

- `URL` - the page to capture (`http://127.0.0.1:PORT/...`, or a local `file://...`)
- `OUT.png` - where to save it (default `shot.png`)
- `WIDTHxHEIGHT` - window size (default `1280x1024`)

Viewport capture only. ais's GUI is a single short page, so that is enough; the
full-page and click-and-assert cases are covered under "CDP / UI tests" below.

## Examples

    # bring the GUI up headless on a throwaway /tmp index. AIS_NO_OPEN keeps
    # `--serve` from popping a browser window (it is the headless/agent switch):
    c/ais -f /tmp/x --init && AIS_NO_OPEN=1 c/ais -f /tmp/x --serve 8080 &

    # capture it -> /tmp/gui.png
    tests/shot/shot.sh http://127.0.0.1:8080/ /tmp/gui.png 460x820

## Why it's here: so a fresh agent can SEE the GUI

The web GUI is a C string, `PAGE[]`, in `c/serve.c`. An agent changing it normally
only reads the HTML/JS and has to *guess* at the result. With this script it can
look: run it, then open the PNG and check its own layout/CSS. Agents read PNGs
natively, so once the file exists they just look.

Flow for a fresh agent:

1. `make -C c`
2. `c/ais -f /tmp/x --init && AIS_NO_OPEN=1 c/ais -f /tmp/x --serve 8080 &`
   - a throwaway `/tmp` index, **never** the repo's own `.ais` or a personal `~/.ais`
   - `AIS_NO_OPEN=1` so `--serve` stays headless and does not pop a browser window
3. `tests/shot/shot.sh http://127.0.0.1:8080/ /tmp/gui.png`, then read the PNG.
4. To force a specific state without driving a live server, fetch the page once and
   append a `<script>` before shooting (e.g. `openSheet()` to open the Add card, or
   `render('1|aisc:...','keys',3)` to draw a results view) -- the page's own
   functions are already defined.

Things worth knowing so it doesn't get stuck:

- The server must already be up; the script only takes the picture, it does not
  start ais.
- Save to `/tmp/*.png` so no stray images land in the repo.

## CDP / UI tests (the C home, when we need them)

This is a *screenshot* helper, deliberately shell-only. If ais ever needs the
Chrome DevTools Protocol for real -- full-page capture, or click-and-assert UI
tests like a mini-Playwright -- that scaffolding belongs in **C, under `tests/`**,
driven by the C suite (a `make uiut` beside `make ut`), the same way the KUL
project keeps its CDP transport *with* its Java tests. CDP is just a websocket
carrying JSON (`Runtime.evaluate` to read the DOM, `Input.*` for real clicks); a
small C client covers it with no new runtime. Write it when there is an actual UI
test to run, not before. Until then: shell screenshots here, unit tests in
`c/tests.c` (`make ut`), CLI tests in `tests/cli.sh`.

## Notes

- Generic: it screenshots any URL, so it is unrelated to the ais engine; it just
  happens to be handy for the `--serve` GUI.
- Replaces the old Xvfb + Firefox + `xwd` + ImageMagick pipeline; modern headless
  Chrome does it all in one shot -- no virtual framebuffer, no external image tools.
