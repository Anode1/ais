ais web GUI -- a browser front-end with NO web framework.
=========================================================
A ~50-line Python-stdlib bridge (ais-serve.py) plus a static page (index.html).
The `ais` CLI IS the backend; the bridge only shells out to it. No Jetty, no
build step, no database, no dependencies beyond Python's standard library.

(Contrast the kul project, which needs a real servlet stack because it has
server-side logic, a DB, and multi-user auth. AIS's logic lives in the binary,
so the web layer is a thin shim, not an application.)

Run
  AIS_INDEX="$HOME/.local/share/ais" python3 ais-serve.py
  # prints the URL and opens http://127.0.0.1:8765/ in your browser

Security
  Binds 127.0.0.1 only (localhost): single user, not on the network, no auth.
  Do NOT bind 0.0.0.0 or expose it without adding authentication.

The whole API (each just runs `ais`)
  GET  /api/get?keys=k1+k2      -> { out, err }   ais get k1 k2
  POST /api/put  {keys,values}  -> { out, err }   ais put -   (one value/line)
  POST /api/doc  {keys,text}    -> { out, err }   ais doc     (a blob document)

To add a feature: add an endpoint that runs another `ais` verb, and a button in
index.html that calls it. That is the entire extension model.
