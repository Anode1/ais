#!/usr/bin/env python3
"""Shared agent memory, measured: one model works and saves, another recalls.

Writer phase: Sonnet does six orientation tasks on a copy of this repository,
one fresh session each, in five arms:
  mem      ais --mcp rw on a fresh -f index, told later sessions rely on it
  mem-bare the same server, told nothing: do the MCP instructions alone make it save?
  notes    a NOTES.md file, the CLAUDE.md pattern, told the same as mem
  mem-ask, notes-ask   the same two, and each task ends asking for the save
Reader phase: a fresh session of another model answers twelve questions whose
answers the writer had to find, in three arms:
  mem      the mem-ask writer's index, read-only, plus Read/Grep/Glob
  notes    the notes-ask writer's NOTES.md in the system prompt, plus Read/Grep/Glob
  none     Read/Grep/Glob only
Each session is `claude -p --restricted --strict-mcp-config`: no CLAUDE.md, no
auto-memory, no hooks, no MCP server but the one named here.

  python3 run.py --out DIR [--readers haiku,opus] [--jobs 4]
"""
import argparse, concurrent.futures as cf, csv, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
AIS = os.path.join(REPO, "c", "ais")

LATER = ("Later sessions on this project, possibly run by other models, start with "
         "no memory of this one. ")
WRITER_SYS = {
    "mem": LATER + "Keep what they would otherwise have to work out again in the ais memory tool.",
    "mem-bare": None,
    "notes": LATER + "Keep what they would otherwise have to work out again in NOTES.md "
                     "at the top of the working directory; they read it first.",
}
WRITER_SYS["mem-ask"], WRITER_SYS["notes-ask"] = WRITER_SYS["mem"], WRITER_SYS["notes"]
# The -ask arms end each task the way a person would: they ask for the save.
ASK = {"mem-ask": " Then save in the ais memory what a later session would otherwise have "
                  "to work out again.",
       "notes-ask": " Then write into NOTES.md what a later session would otherwise have "
                    "to work out again."}
READER_SYS = {
    "mem": "Earlier sessions on this project may have kept what they learned in the ais memory tool.",
    "none": None,
    "notes": None,           # NOTES.md itself goes in, as CLAUDE.md would
}
ANSWER = " Answer in one or two sentences."


def claude(cwd, prompt, model, tools, mcp=None, system=None, budget="1.00", add_dir=None):
    cmd = ["claude", "-p", "--restricted", "--strict-mcp-config", "--disable-slash-commands",
           "--no-session-persistence", "--output-format", "json", "--model", model,
           "--tools", tools, "--max-budget-usd", budget,
           # -p has nobody to answer a permission prompt: grant the ais tools and the file writes
           "--allowedTools", "mcp__ais,Write,Edit"]
    if mcp:
        cmd += ["--mcp-config", mcp]
    if system:
        cmd += ["--append-system-prompt", system]
    if add_dir:              # a saved document is a file inside the index
        cmd += ["--add-dir", add_dir]
    # The prompt goes on stdin: --mcp-config takes several files and would swallow it.
    p = subprocess.run(cmd, cwd=cwd, input=prompt, capture_output=True, text=True, timeout=900)
    try:
        d = json.loads(p.stdout)
    except ValueError:
        return {"error": (p.stderr or p.stdout)[-400:]}
    u = d.get("usage", {})
    return {
        "result": d.get("result", ""),
        "cost": d.get("total_cost_usd", 0.0),
        "turns": d.get("num_turns", 0),
        "tokens": sum(u.get(k, 0) for k in ("input_tokens", "cache_creation_input_tokens",
                                            "cache_read_input_tokens", "output_tokens")),
        "models": ",".join(d.get("modelUsage", {}).keys()),
        "error": "" if not d.get("is_error") else d.get("result", "")[:200],
    }


def mcp_config(path, index, rw):
    args = ["-f", index, "--mcp"] + (["rw"] if rw else [])
    with open(path, "w") as f:
        json.dump({"mcpServers": {"ais": {"command": AIS, "args": args}}}, f)
    return path


def copy_repo(dst):
    """HEAD without .claude/ (its skill would be a second door to the index)."""
    os.makedirs(dst)
    arch = subprocess.run(["git", "-C", REPO, "archive", "HEAD"], capture_output=True, check=True)
    subprocess.run(["tar", "-x", "-C", dst], input=arch.stdout, check=True)
    shutil.rmtree(os.path.join(dst, ".claude"), ignore_errors=True)
    shutil.rmtree(os.path.join(dst, "experiment", "memory"), ignore_errors=True)


def graded(answer, expect):
    return all(re.search(e, answer, re.I) for e in expect)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True)
    ap.add_argument("--writer", default="sonnet")
    ap.add_argument("--readers", default="haiku,opus")
    ap.add_argument("--jobs", type=int, default=4)
    o = ap.parse_args()
    spec = json.load(open(os.path.join(HERE, "tasks.json")))
    out = os.path.abspath(o.out)
    os.makedirs(out, exist_ok=True)

    # ---- writers: one arm per thread, its tasks in order (sessions accumulate)
    def write_arm(arm):
        wd = os.path.join(out, "w-" + arm)
        copy_repo(wd)
        index = os.path.join(out, "idx-" + arm)
        mcp, tools = None, "Read,Grep,Glob"
        if arm.startswith("mem"):
            subprocess.run([AIS, "--init", "-f", index], capture_output=True, check=True)
            mcp = mcp_config(os.path.join(out, "mcp-w-" + arm + ".json"), index, True)
        else:
            tools += ",Write,Edit"
        rows = []
        for t in spec["tasks"]:
            r = claude(wd, t["prompt"] + ASK.get(arm, ""), o.writer, tools, mcp, WRITER_SYS[arm])
            if mcp:
                r["saved"] = len(subprocess.run([AIS, "-f", index, "--timeline", "-c", "1000"],
                                                capture_output=True, text=True).stdout.splitlines())
            rows.append(dict(phase="write", arm=arm, id=t["id"], **r))
            print("write", arm, t["id"], r.get("cost"), r.get("error", ""), flush=True)
        return rows

    rows = []
    with cf.ThreadPoolExecutor(5) as ex:
        for rs in ex.map(write_arm, ["mem", "mem-bare", "notes", "mem-ask", "notes-ask"]):
            rows += rs
    notes_path = os.path.join(out, "w-notes-ask", "NOTES.md")
    notes = open(notes_path).read() if os.path.exists(notes_path) else ""

    # ---- readers: a clean copy each arm, no NOTES.md in the tree
    rd = os.path.join(out, "r-tree")
    copy_repo(rd)
    rmcp = mcp_config(os.path.join(out, "mcp-r.json"), os.path.join(out, "idx-mem-ask"), False)

    def read_one(job):
        model, arm, q = job
        system, mcp, add = READER_SYS[arm], None, None
        if arm == "mem":
            mcp, add = rmcp, os.path.join(out, "idx-mem-ask")
        if arm == "notes":
            system = "Notes kept by earlier sessions on this project (NOTES.md):\n\n" + notes
        r = claude(rd, q["q"] + ANSWER, model, "Read,Grep,Glob", mcp, system, add_dir=add)
        ok = graded(r.get("result", ""), q["expect"])
        print("read", model, arm, q["id"], ok, r.get("cost"), flush=True)
        return dict(phase="read", arm=arm, id=q["id"], reader=model, correct=int(ok), **r)

    jobs = [(m, a, q) for m in o.readers.split(",") for a in ("mem", "notes", "none")
            for q in spec["questions"]]
    with cf.ThreadPoolExecutor(o.jobs) as ex:
        rows += list(ex.map(read_one, jobs))

    keys = ["phase", "arm", "id", "reader", "correct", "saved", "tokens", "cost", "turns", "models",
            "error", "result"]
    with open(os.path.join(out, "results.csv"), "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=keys, extrasaction="ignore")
        w.writeheader()
        w.writerows(rows)
    print("wrote", os.path.join(out, "results.csv"))


if __name__ == "__main__":
    main()
