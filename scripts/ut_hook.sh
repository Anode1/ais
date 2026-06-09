#!/bin/sh
# PostToolUse hook: when an AIS C source (c/*.c or c/*.h) is written/edited,
# run `make ut` and report the result. Reads the tool-event JSON on stdin and
# prints a JSON {"systemMessage": ...}. Inert (exit 0) for any other file.
# Registered in .claude/settings.json. No jq dependency (uses python3).
python3 -c '
import json, sys, os, subprocess
try:
    d = json.load(sys.stdin)
except Exception:
    sys.exit(0)
f = (d.get("tool_input", {}) or {}).get("file_path") \
    or (d.get("tool_response", {}) or {}).get("filePath") or ""
cdir = os.path.join(os.environ.get("CLAUDE_PROJECT_DIR") or "/home/vas/ais", "c")
if not (f and os.path.dirname(f) == cdir and f.endswith((".c", ".h"))):
    sys.exit(0)
try:
    r = subprocess.run(["make", "-C", cdir, "ut"],
                       capture_output=True, text=True, timeout=120)
    out = (r.stdout + r.stderr).splitlines()
    line = next((l for l in reversed(out)
                 if "passed" in l or "failed" in l or "Error" in l or "error" in l),
                out[-1] if out else "")
    status = "green" if r.returncode == 0 else "RED"
    print(json.dumps({"systemMessage":
        "make ut (%s) after %s: %s" % (status, os.path.basename(f), line.strip())}))
except Exception as e:
    print(json.dumps({"systemMessage": "make ut hook error: %s" % e}))
'
