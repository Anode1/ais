#!/usr/bin/env python3
"""AIS MCP server.

Exposes a local AIS index (recall, store, list) as Model Context Protocol tools
by wrapping the `ais` CLI. Retrieval is a local lookup and key intersection on
this machine; the model receives only the returned slice, never a search over
your files.

Optional per-call logging (latency, bytes, approximate tokens) lets us test the
conjecture that a local lookup costs far fewer tokens than an in-project search.

Run: python3 server.py [--ais PATH] [--index DIR] [--log FILE]
"""
import argparse
import json
import shutil
import subprocess
import sys
import time

from mcp.server.fastmcp import FastMCP

_p = argparse.ArgumentParser(add_help=False)
_p.add_argument("--ais", default="ais", help="path to the ais binary (default: ais on PATH)")
_p.add_argument("--index", default=None, help="index directory, forwarded as `ais -f DIR` (default: ais resolves it)")
_p.add_argument("--log", default=None, help="append per-call stats as JSONL to this file")
OPTS, _ = _p.parse_known_args()

mcp = FastMCP("ais")


def _run(cli_args):
    """Run the ais CLI; return (output, latency_ms, return_code)."""
    cmd = [OPTS.ais]
    if OPTS.index:
        cmd += ["-f", OPTS.index]
    cmd += cli_args
    t0 = time.monotonic()
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True)
        out, rc = proc.stdout, proc.returncode
        if rc != 0 and proc.stderr:
            out = (out + proc.stderr).strip()
    except FileNotFoundError:
        out, rc = f"ais binary not found: {OPTS.ais}", 127
    return out, (time.monotonic() - t0) * 1000.0, rc


def _log(tool, in_text, out_text, dt_ms, rc):
    """Append one measurement record per call when --log is set."""
    if not OPTS.log:
        return
    rec = {
        "tool": tool,
        "in_bytes": len(in_text.encode("utf-8")),
        "out_bytes": len(out_text.encode("utf-8")),
        "approx_tokens_out": max(1, len(out_text) // 4),
        "latency_ms": round(dt_ms, 2),
        "rc": rc,
    }
    try:
        with open(OPTS.log, "a", encoding="utf-8") as f:
            f.write(json.dumps(rec) + "\n")
    except OSError:
        pass


@mcp.tool()
def ais_recall(keys: list[str], match_any: bool = False) -> str:
    """Recall records from the local AIS index by keyword.

    Intersection by default (records filed under ALL keys); set match_any=True
    for union (records under ANY key). Returns matching records as `id|value`
    lines. This is a local lookup, not a search.
    """
    cli = (["-o"] if match_any else []) + list(keys)
    out, dt, rc = _run(cli)
    _log("ais_recall", " ".join(keys), out, dt, rc)
    return out or "(no records)"


@mcp.tool()
def ais_find(text: str) -> str:
    """Substring search of values and paths (`ais --find`). Use only when the keys are unknown."""
    out, dt, rc = _run(["--find", text])
    _log("ais_find", text, out, dt, rc)
    return out or "(no records)"


@mcp.tool()
def ais_store(value: str, keys: list[str]) -> str:
    """Save a short reference (URL, path, note, or command) under one or more keys."""
    out, dt, rc = _run(["-v", value] + list(keys))
    _log("ais_store", value + " " + " ".join(keys), out, dt, rc)
    return out or "stored"


@mcp.tool()
def ais_keys() -> str:
    """List the keys in use in the index (`ais --keys`). Call this before inventing a new key."""
    out, dt, rc = _run(["--keys"])
    _log("ais_keys", "", out, dt, rc)
    return out


@mcp.tool()
def ais_tags() -> str:
    """List every key with how many records use it, busiest first (`ais --tags`)."""
    out, dt, rc = _run(["--tags"])
    _log("ais_tags", "", out, dt, rc)
    return out


@mcp.tool()
def ais_where() -> str:
    """Print the resolved index directory (`ais --where`)."""
    out, dt, rc = _run(["--where"])
    _log("ais_where", "", out, dt, rc)
    return out


if __name__ == "__main__":
    if OPTS.ais == "ais" and shutil.which("ais") is None:
        print("warning: `ais` not found on PATH; build from source or pass --ais PATH", file=sys.stderr)
    mcp.run()
