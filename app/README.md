# app/ — AIS phone web app (PWA)

A thin, installable web client for phones. It is plain files (no framework, no
build step) and talks to the **same `ais --serve` API** the desktop page uses
(`/api/get`, `/api/put`) — the C engine is the backend. Voice recall uses the
browser's own speech-to-text; typing always works where voice doesn't.

    index.html           the page (markup + the small client script)
    app.css              mobile-first styling (edit and reload)
    manifest.webmanifest home-screen install metadata
    sw.js                service worker (caches the shell; never the API)
    icon.svg             app icon

## Run / test (desktop)

    AIS_WEB=app ./ais --serve

Open http://127.0.0.1:8765/ in Chrome. `localhost` is a *secure context*, so
voice, install, and the service worker all work here for development.

## On a real phone — the secure-context rule

Browser **voice and install require HTTPS or localhost**. Plain
`http://<desktop-ip>:8765` from a phone shows the page but disables voice,
install, and the service worker. Two real paths:

- **Android (recommended):** run `ais --serve` inside *Termux* on the phone, then
  open `http://localhost:8765` in Chrome — it's localhost *on the phone*, so the
  full PWA (voice, install, offline shell) works, and the data lives on the
  phone.
- **Any phone, hosted:** serve it behind an HTTPS reverse proxy. Heavier; needs
  a cert and binding beyond loopback (`ais --serve` binds 127.0.0.1 only by
  design).
- **iOS:** Safari has no Web Speech recognition, so *voice* needs a future
  native app / Siri Shortcuts. Text recall works over HTTPS.

## Notes

- The app name stays **`ais`** (written, searchable); say it "A-I-S".
- Voice flow: speech-to-text → keys → `/api/get`. A later step can fuzzy-match
  the recognized words against your existing keys (and handle Cyrillic, which
  Soundex cannot).
