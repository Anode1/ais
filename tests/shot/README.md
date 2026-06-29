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

The web GUI is a C string, `PAGE[]`, in `c/serve.c`; an agent changing it would
otherwise only read the HTML/JS and guess. The server must already be up (this
script only takes the picture); use a throwaway `/tmp` index, never the repo's
own `.ais` or a personal `~/.ais`, and save to `/tmp/*.png`. To force a state
without a live server, fetch the page once and append a `<script>` before
shooting (e.g. `openSheet()`, or `render('1|aisc:...','keys',3)`); the page's own
functions are already defined.

## CDP / UI tests

Real click-and-assert UI tests belong in C under `tests/` (a `make uiut`), not
here; see tests/README.md.

## Notes

- Generic: it screenshots any URL, so it is unrelated to the ais engine; it just
  happens to be handy for the `--serve` GUI.
- Replaces the old Xvfb + Firefox + `xwd` + ImageMagick pipeline; modern headless
  Chrome does it all in one shot -- no virtual framebuffer, no external image tools.
