#!/usr/bin/env python3
"""AIS measurement runner.

For each question, run a minimal agent loop via the Anthropic API in two arms and
record token usage, latency, tool calls, correctness, and recall:

  arm "ais"    : recall paths from the AIS index by key, then read the recalled
                 file(s)  (ais_recall, ais_find, ais_keys, read_file)
  arm "search" : find the answer by grep/read across the project, no index
                 (grep, read_file, list_files); the .ais/ dir is excluded

Both arms can read files; the only difference is retrieval (key lookup vs grep).

Options:
  --repeats N    : runs per question per arm (stable means / error bars)
  --sweep A,B,C  : repo-size sweep for the SEARCH arm (the ais arm is size-independent)

Writes a CSV incrementally (a mid-run failure keeps partial results). REAL API calls.

Usage:
  export ANTHROPIC_API_KEY=...
  python3 run.py --ais ../ais --index index --search-root library \
      --questions questions.json --ais-no-read --repeats 5
"""
import argparse
import csv
import glob
import json
import os
import subprocess
import time

import anthropic

EXCLUDE = (".ais", ".git", "node_modules", "__pycache__")
CAP = 6000

READ_TOOL = {"name": "read_file",
             "description": "Read a project file by relative path (e.g. a path recalled from the index).",
             "input_schema": {"type": "object", "properties": {"path": {"type": "string"}}, "required": ["path"]}}

AIS_TOOLS = [
    {"name": "ais_recall",
     "description": "Recall record paths from the local index by keyword. Intersection (records under ALL keys) unless match_any is true.",
     "input_schema": {"type": "object",
                      "properties": {"keys": {"type": "array", "items": {"type": "string"}},
                                     "match_any": {"type": "boolean"}},
                      "required": ["keys"]}},
    {"name": "ais_keys",
     "description": "List the exact keys in use in the index. Use this to pick the correct key.",
     "input_schema": {"type": "object", "properties": {}}},
    READ_TOOL,
]

SEARCH_TOOLS = [
    {"name": "grep",
     "description": "Search file contents in the project for a substring or regex; returns path:line matches.",
     "input_schema": {"type": "object", "properties": {"pattern": {"type": "string"}}, "required": ["pattern"]}},
    READ_TOOL,
    {"name": "list_files",
     "description": "List project files matching a glob.",
     "input_schema": {"type": "object", "properties": {"glob": {"type": "string"}}, "required": ["glob"]}},
]


def _cap(s):
    return s if len(s) <= CAP else s[:CAP] + "\n...[truncated]"


def _excluded(path):
    return any(("/" + e + "/") in (path + "/") for e in EXCLUDE)


def _read(root, relpath):
    root = os.path.abspath(root)
    full = os.path.abspath(os.path.join(root, relpath))
    if not full.startswith(root) or _excluded(full):
        return "path outside project or excluded"
    try:
        with open(full, encoding="utf-8", errors="replace") as f:
            return _cap(f.read())
    except OSError as e:
        return f"read error: {e}"


def ais_executor(ais_bin, index, read_root):
    def _ais(args):
        try:
            p = subprocess.run([ais_bin, "-f", index] + args, capture_output=True, text=True)
            return (p.stdout or p.stderr or "").strip()
        except FileNotFoundError:
            return f"ais not found: {ais_bin}"

    def run(name, inp):
        if name == "ais_recall":
            cli = (["-o"] if inp.get("match_any") else []) + list(inp.get("keys", []))
            return _cap(_ais(cli) or "(no records)")
        if name == "ais_find":
            return _cap(_ais(["--find", inp.get("text", "")]) or "(no records)")
        if name == "ais_keys":
            return _cap(_ais(["--keys"]))
        if name == "read_file":
            return _read(read_root, inp.get("path", ""))
        return f"unknown tool: {name}"

    return run


def search_executor(root):
    root = os.path.abspath(root)

    def run(name, inp):
        if name == "grep":
            try:
                p = subprocess.run(["grep", "-rIn", "--", inp.get("pattern", ""), root],
                                   capture_output=True, text=True)
                lines = [ln for ln in p.stdout.splitlines() if not _excluded(ln)]
                return _cap("\n".join(lines[:200]) or "(no matches)")
            except Exception as e:  # noqa: BLE001
                return f"grep error: {e}"
        if name == "read_file":
            return _read(root, inp.get("path", ""))
        if name == "list_files":
            hits = [os.path.relpath(p, root)
                    for p in glob.glob(os.path.join(root, inp.get("glob", "**/*")), recursive=True)
                    if os.path.isfile(p) and not _excluded(p)]
            return _cap("\n".join(sorted(hits)[:300]) or "(no files)")
        return f"unknown tool: {name}"

    return run


SYS_AIS = ("Answer the question from the index using EXACT keys. Keys are exact words, not fuzzy. "
           "Call ais_keys to see the exact key vocabulary, then ais_recall with the exact key(s) "
           "(intersection). If a recall returns nothing, that empty result is the signal that the key is "
           "wrong: re-check ais_keys and use a correct key. Do NOT guess, paraphrase keys, or keep "
           "retrying variants. Then call read_file only on the specific path(s) recalled, to read the "
           "document's contents. Give the final answer directly and concisely.")
SYS_AIS_NOREAD = ("Answer from the index using EXACT keys, and do NOT read any files. "
                  "Call ais_keys to see the vocabulary, then ais_recall with the exact key(s). "
                  "The recalled note or reference IS the answer; return it directly and concisely. "
                  "An empty result means the key is wrong: re-check ais_keys, do not guess.")
SYS_SEARCH = ("Answer the question by searching the project files with grep, read_file, and list_files. "
              "Do not guess; find it in the files. Give the final answer directly and concisely.")


def run_agent(client, model, system, tools, executor, question, max_turns=14):
    messages = [{"role": "user", "content": question}]
    in_tok = out_tok = calls = retrieval_bytes = 0
    final = ""
    t0 = time.monotonic()
    for _ in range(max_turns):
        resp = client.messages.create(model=model, max_tokens=1024, system=system,
                                       tools=tools, messages=messages)
        in_tok += resp.usage.input_tokens
        out_tok += resp.usage.output_tokens
        texts = [b.text for b in resp.content if b.type == "text"]
        if texts:
            final = "\n".join(t.strip() for t in texts if t.strip())
        messages.append({"role": "assistant", "content": resp.content})
        tool_uses = [b for b in resp.content if b.type == "tool_use"]
        if resp.stop_reason != "tool_use" or not tool_uses:
            break
        results = []
        for tu in tool_uses:
            content = executor(tu.name, tu.input)
            retrieval_bytes += len(content.encode("utf-8"))  # tokens pulled into context to find the answer
            results.append({"type": "tool_result", "tool_use_id": tu.id, "content": content})
        calls += len(tool_uses)
        messages.append({"role": "user", "content": results})
    return {"answer": final, "in_tokens": in_tok, "out_tokens": out_tok,
            "total_tokens": in_tok + out_tok, "tool_calls": calls,
            "retrieval_tokens": retrieval_bytes // 4,
            "latency_ms": round((time.monotonic() - t0) * 1000, 1)}


def grade(answer, expect):
    """Return (correct, recall). correct = all expected substrings present;
    recall = fraction present (completeness, the key metric for document-set queries)."""
    if not expect:
        return "", ""
    needles = [expect] if isinstance(expect, str) else expect
    hits = sum(1 for n in needles if n.lower() in answer.lower())
    return hits == len(needles), round(hits / len(needles), 3)


def count_root(path):
    """Count files and total bytes under path, excluding vcs/index/build dirs."""
    files = nbytes = 0
    for dirpath, dirnames, filenames in os.walk(path):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE]
        for fn in filenames:
            try:
                nbytes += os.path.getsize(os.path.join(dirpath, fn))
                files += 1
            except OSError:
                pass
    return files, nbytes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--index", required=True, help="AIS index dir for the ais arm")
    ap.add_argument("--search-root", required=True, help="project dir both arms read; search arm greps it")
    ap.add_argument("--questions", required=True, help="JSON list of {id, question, expect}")
    ap.add_argument("--out", default="results.csv")
    ap.add_argument("--ais", default="ais", help="path to the ais binary")
    ap.add_argument("--model", default="claude-sonnet-4-6", help="Anthropic model id")
    ap.add_argument("--arms", default="ais,search")
    ap.add_argument("--repeats", type=int, default=1, help="runs per question per arm (stable means)")
    ap.add_argument("--sweep", default=None,
                    help="comma-separated dirs of increasing size for the SEARCH arm (repo-size sweep); "
                         "default: just --search-root")
    ap.add_argument("--ais-no-read", action="store_true",
                    help="AIS arm recalls only (no read_file); the recalled note/URL is the answer")
    ap.add_argument("--api-key", default=None, help="Anthropic API key (else ANTHROPIC_API_KEY env var)")
    a = ap.parse_args()

    key = a.api_key or os.environ.get("ANTHROPIC_API_KEY")
    if not key:
        raise SystemExit(
            "No API key found. Set it in this shell:\n"
            "    export ANTHROPIC_API_KEY=sk-ant-...\n"
            "or pass --api-key sk-ant-...  (get one at https://console.anthropic.com/).")
    client = anthropic.Anthropic(api_key=key)
    with open(a.questions, encoding="utf-8") as f:
        questions = json.load(f)
    arms = a.arms.split(",")

    sweep_paths = [p.strip() for p in a.sweep.split(",")] if a.sweep else [a.search_root]
    roots = []
    for p in sweep_paths:
        full = os.path.abspath(os.path.expanduser(p))
        fc, bc = count_root(full)
        roots.append({"label": os.path.basename(os.path.normpath(full)) or full,
                      "path": full, "files": fc, "bytes": bc})
    roots.sort(key=lambda r: r["bytes"])
    if len(roots) > 1:
        print("repo-size sweep (search arm):")
        for r in roots:
            print(f"  {r['label']:>16}  {r['files']:>6} files  {r['bytes']:>12} bytes")
        print()

    ais_ex = ais_executor(a.ais, a.index, os.path.abspath(os.path.expanduser(a.search_root)))
    n_runs = len(questions) * a.repeats * (("ais" in arms) + (len(roots) if "search" in arms else 0))
    print(f"running {n_runs} agent loops "
          f"({len(questions)} questions x {a.repeats} repeats, arms={arms}, "
          f"{len(roots)} search root(s)); this spends API tokens.\n")

    cols = ["qid", "arm", "root", "root_files", "root_bytes", "repeat", "total_tokens",
            "in_tokens", "out_tokens", "retrieval_tokens", "tool_calls", "latency_ms", "correct", "recall", "answer"]
    fout = open(a.out, "w", newline="", encoding="utf-8")
    writer = csv.DictWriter(fout, fieldnames=cols, extrasaction="ignore")
    writer.writeheader()
    fout.flush()

    rows = []
    try:
        for q in questions:
            for arm in arms:
                if arm == "ais":
                    targets = [{"label": "(index)", "files": 0, "bytes": 0, "exec": ais_ex}]
                    tools = [t for t in AIS_TOOLS if not (a.ais_no_read and t["name"] == "read_file")]
                    system = SYS_AIS_NOREAD if a.ais_no_read else SYS_AIS
                else:
                    targets = [dict(r, **{"exec": search_executor(r["path"])}) for r in roots]
                    tools, system = SEARCH_TOOLS, SYS_SEARCH
                for t in targets:
                    for rep in range(a.repeats):
                        r = run_agent(client, a.model, system, tools, t["exec"], q["question"])
                        correct, recall = grade(r["answer"], q.get("expect", ""))
                        r.update(qid=q.get("id", ""), arm=arm, root=t["label"],
                                 root_files=t["files"], root_bytes=t["bytes"], repeat=rep,
                                 correct=correct, recall=recall)
                        rows.append(r)
                        writer.writerow(r)
                        fout.flush()  # persist each run; a mid-run failure (e.g. out of credit) keeps partial results
                        mark = "" if q.get("expect") is None else ("OK" if r["correct"] else "MISS")
                        print(f"[{arm:6}|{t['label']:>10}|r{rep}] {q.get('id',''):16} "
                              f"tok={r['total_tokens']:6} {mark}")
    except KeyboardInterrupt:
        print("\ninterrupted; partial results saved.")
    except Exception as e:  # noqa: BLE001
        print(f"\nstopped on error ({type(e).__name__}: {e}); partial results saved.")
    finally:
        fout.close()

    print("\n--- mean total_tokens by arm ---")
    for arm in arms:
        ts = [r["total_tokens"] for r in rows if r["arm"] == arm]
        if ts:
            print(f"  {arm:6}: {sum(ts) // len(ts):6} tokens   (n={len(ts)})")
    print(f"\nwrote {a.out}   (analyze: python3 analyze.py --csv {a.out})")


if __name__ == "__main__":
    main()
