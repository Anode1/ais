#!/usr/bin/env python3
# ais-serve.py -- a tiny LOCAL bridge so a browser can drive the `ais` CLI.
#
# No web framework, no build step, no database: Python stdlib http.server plus
# the `ais` binary, which IS the backend. Binds to 127.0.0.1 only (single user,
# local). Serves index.html and a few /api/* endpoints that shell out to `ais`.
#
# Run:  python3 gui/web/ais-serve.py     (set AIS_INDEX first to pick an index)
import http.server, json, os, shutil, subprocess, urllib.parse, webbrowser

AIS  = shutil.which("ais") or "/home/vas/ais/c/ais"
HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 8765


def run_ais(args, stdin=None):
    r = subprocess.run([AIS, *args], input=stdin, capture_output=True, text=True)
    return {"out": r.stdout, "err": r.stderr}


class Handler(http.server.BaseHTTPRequestHandler):
    def _send(self, code, body, ctype="application/json"):
        b = body if isinstance(body, bytes) else body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(b)))
        self.end_headers()
        self.wfile.write(b)

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        if u.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                self._send(200, f.read(), "text/html")
        elif u.path == "/api/get":
            keys = urllib.parse.parse_qs(u.query).get("keys", [""])[0].split()
            self._send(200, json.dumps(run_ais(keys) if keys else {"out": "", "err": ""}))
        else:
            self._send(404, json.dumps({"err": "not found"}))

    def do_POST(self):
        u = urllib.parse.urlparse(self.path)
        n = int(self.headers.get("Content-Length", 0))
        data = json.loads(self.rfile.read(n) or b"{}")
        keys = (data.get("keys") or "").split()
        if u.path == "/api/put":      # values box: one per line, under the keys
            self._send(200, json.dumps(run_ais(["put", "-", *keys], data.get("values", ""))))
        elif u.path == "/api/doc":    # a multi-line document -> blob, under the keys
            self._send(200, json.dumps(run_ais(["doc", *keys], data.get("text", ""))))
        else:
            self._send(404, json.dumps({"err": "not found"}))

    def log_message(self, *a):        # keep the console quiet
        pass


if __name__ == "__main__":
    httpd = http.server.HTTPServer(("127.0.0.1", PORT), Handler)
    url = f"http://127.0.0.1:{PORT}/"
    print(f"ais web bridge on {url}  (Ctrl-C to stop)")
    try:
        webbrowser.open(url)
    except Exception:
        pass
    httpd.serve_forever()
