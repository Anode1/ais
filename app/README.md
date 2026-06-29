# app/: AIS phone web app (PWA)

An installable web client for phones. See gui/README.md for why front-ends stay
thin; here it is plain files (no framework, no build step) talking to the **same
`ais --serve` API** the desktop page uses (`/api/get`, `/api/put`), with the C
engine as the backend. Voice recall uses the browser's own speech-to-text;
typing always works where voice doesn't.

    index.html           the page (markup + the small client script)
    app.css              mobile-first styling (edit and reload)
    manifest.webmanifest home-screen install metadata
    sw.js                service worker (caches the shell; never the API)
    icon.png             app icon (512px; icon-192.png for installs)

## Run / test (desktop)

    AIS_WEB=app ./ais --serve

Open http://127.0.0.1:8765/ in Chrome. `localhost` is a *secure context*, so
voice, install, and the service worker all work here for development.

## On a real phone

Browser voice/install need a secure context (HTTPS or localhost); on Android use
Termux + `http://localhost:8765`. Full rule and the route choices:
see doc/android-install.md.

- **iOS:** Safari has no Web Speech recognition, so *voice* needs a future
  native app / Siri Shortcuts. Text recall works over HTTPS.

## Notes

- The app name stays **`ais`** (written, searchable); say it "A-I-S".
- Voice flow: speech-to-text → keys → `/api/get`. A later step can fuzzy-match
  the recognized words against your existing keys (and handle Cyrillic, which
  Soundex cannot).
