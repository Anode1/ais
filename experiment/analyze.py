#!/usr/bin/env python3
"""Summarize and plot the AIS measurement runner's CSV.

Handles --repeats (averages runs, reports sd) and --sweep (search-arm repo-size
sweep). Prints a per-question table, a per-arm summary, and, with a sweep, a
tokens-vs-repo-size table. With matplotlib installed, writes the chart(s).

Usage: python3 analyze.py --csv results.csv --out chart.png
"""
import argparse
import csv
import statistics
from collections import defaultdict


def num(x):
    try:
        return float(x)
    except (TypeError, ValueError):
        return 0.0


def mean(xs):
    return statistics.mean(xs) if xs else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="results.csv")
    ap.add_argument("--out", default="chart.png")
    ap.add_argument("--latex", default=None,
                    help="write a per-question LaTeX table (paper appendix) to this path")
    ap.add_argument("--sanitized-csv", dest="sanitized_csv", default=None,
                    help="write the CSV without the free-text 'answer' column, for safe deposit")
    args = ap.parse_args()

    with open(args.csv, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if not rows:
        print("no rows in", args.csv)
        return
    for r in rows:
        r.setdefault("root", "(all)")

    # Safe deposit: same rows minus the free-text answer column (which can embed
    # private project internals: paths, bucket names, runbook bodies).
    if args.sanitized_csv:
        cols = [c for c in rows[0].keys() if c != "answer"]
        with open(args.sanitized_csv, "w", newline="", encoding="utf-8") as f:
            w = csv.DictWriter(f, fieldnames=cols, extrasaction="ignore")
            w.writeheader()
            for r in rows:
                w.writerow({c: r.get(c, "") for c in cols})
        print(f"wrote sanitized CSV (no answer column): {args.sanitized_csv}")

    arms, qids = [], []
    for r in rows:
        if r["arm"] not in arms:
            arms.append(r["arm"])
        if r["qid"] not in qids:
            qids.append(r["qid"])

    sroots = {r["root"]: num(r.get("root_bytes", 0)) for r in rows if r["arm"] == "search"}
    big = max(sroots, key=sroots.get) if sroots else None
    multi = len(sroots) > 1
    paired = "ais" in arms and "search" in arms

    def toks(qid, arm, root=None):
        return [num(r["total_tokens"]) for r in rows
                if r["qid"] == qid and r["arm"] == arm and (root is None or r["root"] == root)]

    # per-question table (search shown at the largest root)
    w = max([len(q) for q in qids] + [8])
    head = f"{'question':<{w}} | " + " | ".join(f"{a + ' tok':>12}" for a in arms)
    if paired:
        head += " |  ratio"
    print(head)
    print("-" * len(head))
    ratios = []
    for q in qids:
        cell, m = [], {}
        for a in arms:
            root = big if (a == "search" and big) else None
            m[a] = mean(toks(q, a, root))
            cell.append(f"{int(m[a]):>12}")
        line = f"{q:<{w}} | " + " | ".join(cell)
        if paired and m["ais"] > 0:
            ratios.append(m["search"] / m["ais"])
            line += f" | {m['search'] / m['ais']:6.0f}x"
        print(line)

    # per-arm summary
    print("\nper-arm summary" + (f"  (search at largest root '{big}')" if multi else ""))
    for a in arms:
        root = big if (a == "search" and big) else None
        tk = [num(r["total_tokens"]) for r in rows if r["arm"] == a and (root is None or r["root"] == root)]
        lat = [num(r["latency_ms"]) for r in rows if r["arm"] == a and (root is None or r["root"] == root)]
        graded = [r for r in rows if r["arm"] == a and (root is None or r["root"] == root)
                  and r.get("correct") in ("True", "False")]
        ok = sum(1 for r in graded if r["correct"] == "True")
        sd = statistics.pstdev(tk) if len(tk) > 1 else 0.0
        med = statistics.median(tk) if tk else 0.0
        rec = [num(r["recall"]) for r in graded if r.get("recall") not in (None, "")]
        rstr = f"{mean(rec):.2f}" if rec else "n/a"
        ret = [num(r["retrieval_tokens"]) for r in rows if r["arm"] == a and (root is None or r["root"] == root)
               and r.get("retrieval_tokens") not in (None, "")]
        retstr = f"{int(mean(ret))}" if ret else "n/a"
        print(f"  {a:8} mean_tokens={int(mean(tk)):>7} (sd {int(sd):>6}) median={int(med):>7}  "
              f"retrieval_tok={retstr:>7}  mean_latency_ms={int(mean(lat)):>7}  "
              f"correct={ok}/{len(graded)}  recall={rstr}")
    if ratios:
        print(f"\n  search/ais TOTAL-token ratio: mean {mean(ratios):.0f}x, median {statistics.median(ratios):.0f}x"
              f"  (diluted by the fixed per-turn model overhead)")
    ais_ret = [num(r["retrieval_tokens"]) for r in rows if r["arm"] == "ais" and r.get("retrieval_tokens") not in (None, "")]
    se_ret = [num(r["retrieval_tokens"]) for r in rows if r["arm"] == "search"
              and (not big or r["root"] == big) and r.get("retrieval_tokens") not in (None, "")]
    if ais_ret and se_ret and mean(ais_ret) > 0:
        print(f"  search/ais RETRIEVAL-payload ratio: {mean(se_ret) / mean(ais_ret):.0f}x"
              f"  (tokens pulled into context to find the answer; isolates retrieval from the model tax)")

    # repo-size sweep
    sweep_pts = []
    ais_flat = mean([num(r["total_tokens"]) for r in rows if r["arm"] == "ais"])
    if multi:
        print("\nrepo-size sweep (search arm), mean tokens over all questions:")
        print(f"  {'root':>16} {'files':>7} {'bytes':>12} {'search tok':>11} {'ais tok':>9}")
        for root in sorted(sroots, key=sroots.get):
            st = [num(r["total_tokens"]) for r in rows if r["arm"] == "search" and r["root"] == root]
            fc = next((num(r.get("root_files", 0)) for r in rows if r["root"] == root), 0)
            print(f"  {root:>16} {int(fc):>7} {int(sroots[root]):>12} {int(mean(st)):>11} {int(ais_flat):>9}")
            sweep_pts.append((sroots[root], mean(st)))

    # LaTeX appendix table: per-question, both arms, numbers only (no answer text).
    # Generated from the CSV so the paper's numbers cannot drift from the data.
    if args.latex:
        def qmean(qid, arm, col, root=None):
            v = [num(r[col]) for r in rows if r["qid"] == qid and r["arm"] == arm
                 and (root is None or r["root"] == root) and r.get(col) not in (None, "")]
            return mean(v)

        def qok(qid, arm):
            g = [r for r in rows if r["qid"] == qid and r["arm"] == arm
                 and r.get("correct") in ("True", "False")]
            return sum(1 for r in g if r["correct"] == "True"), len(g)

        def tex_int(x):
            return "{:,}".format(int(round(x))).replace(",", "{,}")

        sr = big if big else None
        a_tot, s_tot, body = [], [], []
        for q in qids:
            ai = qmean(q, "ais", "total_tokens")
            se = qmean(q, "search", "total_tokens", sr)
            a_tot.append(ai)
            s_tot.append(se)
            ok, g = qok(q, "search")
            miss = " (search miss)" if g and ok == 0 else ""
            body.append("\\code{%s} & %s & %s & %d$\\times$%s \\\\" % (
                q.replace("_", r"\_"), tex_int(ai), tex_int(se),
                round(se / ai) if ai else 0, miss))
        tex = ("% generated by analyze.py --latex; do not edit by hand\n"
               "\\begin{tabular}{lrrr}\n\\toprule\n"
               "\\textbf{question} & \\textbf{index tok} & "
               "\\textbf{file-search tok} & \\textbf{ratio} \\\\\n\\midrule\n"
               + "\n".join(body) + "\n\\midrule\n"
               "\\textbf{mean} & \\textbf{%s} & \\textbf{%s} & \\\\\n"
               "\\bottomrule\n\\end{tabular}\n" % (tex_int(mean(a_tot)), tex_int(mean(s_tot))))
        with open(args.latex, "w", encoding="utf-8") as f:
            f.write(tex)
        print(f"wrote LaTeX appendix table: {args.latex}")

    # charts
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except ImportError:
        print("\n(matplotlib not installed; text only. `pip install matplotlib` for charts.)")
        return

    width = 0.8 / len(arms)
    xs = list(range(len(qids)))
    fig, ax = plt.subplots(figsize=(max(7, len(qids) * 1.2), 5))
    for i, a in enumerate(arms):
        root = big if (a == "search" and big) else None
        v = [mean(toks(q, a, root)) or 0.1 for q in qids]
        ax.bar([x + i * width for x in xs], v, width, label=a)
    ax.set_yscale("log")
    ax.set_xticks([x + width * (len(arms) - 1) / 2 for x in xs])
    ax.set_xticklabels(qids, rotation=30, ha="right")
    ax.set_ylabel("total tokens per query (log)")
    ax.set_title("Tokens per query: AIS vs search")
    ax.legend()
    ax.grid(axis="y", which="both", alpha=0.3)
    fig.tight_layout()
    fig.savefig(args.out, dpi=130)
    print(f"\nwrote {args.out}")

    if multi and sweep_pts:
        sweep_pts.sort()
        fig2, ax2 = plt.subplots(figsize=(7, 5))
        ax2.plot([p[0] for p in sweep_pts], [p[1] for p in sweep_pts], "o-", label="search")
        ax2.axhline(ais_flat, ls="--", color="gray", label="ais (flat)")
        ax2.set_xlabel("search-root size (bytes)")
        ax2.set_ylabel("mean tokens per query")
        ax2.set_title("Search cost grows with repo size; AIS stays flat")
        ax2.legend()
        ax2.grid(alpha=0.3)
        fig2.tight_layout()
        sweep_out = args.out.rsplit(".", 1)[0] + "_sweep.png"
        fig2.savefig(sweep_out, dpi=130)
        print(f"wrote {sweep_out}")


if __name__ == "__main__":
    main()
