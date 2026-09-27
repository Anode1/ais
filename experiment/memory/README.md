# experiment/memory: one model saves, another recalls

Does an agent keep what a later session needs, and does a session of another
model find it? Run 2026-09-27 with Claude Code 2.1.283 in `claude -p
--restricted --strict-mcp-config`: no CLAUDE.md, no auto-memory, no hooks, no MCP
server but the one named.

**Writers.** Sonnet did six orientation tasks on a copy of this repository
(`tasks.json`), a fresh session each, in five arms:

| Arm | Told | Kept after six tasks |
| --- | --- | --- |
| `mem-bare` | nothing; the MCP server's own instructions only | 0 records |
| `mem` | one system-prompt line: keep what later sessions need in ais | 2 records |
| `notes` | the same line, naming NOTES.md | 2,408 bytes |
| `mem-ask` | the line, and each task ends "then save it" | 6 records, 39 keys, 4 used twice or more |
| `notes-ask` | the line, and each task ends "then write it down" | 10,640 bytes |

**Readers.** A fresh Haiku or Opus session answered twelve questions whose
answers the writer had to find, with Read/Grep/Glob and one of: the `mem-ask`
index read-only (`mem`), the `notes-ask` NOTES.md in the system prompt as a
CLAUDE.md would be (`notes`), or nothing (`none`). Graded by regex (`expect` in
`tasks.json`).

| Reader | Arm | Correct | Median tokens | Mean cost |
| --- | --- | --- | --- | --- |
| Haiku | mem | 5/12 | 8,297 | $0.019 |
| Haiku | notes | 10/12 | 10,641 | $0.007 |
| Haiku | none | 7/12 | 28,828 | $0.022 |
| Opus | mem | 12/12 | 23,561 | $0.047 |
| Opus | notes | 12/12 | 14,636 | $0.026 |
| Opus | none | 12/12 | 17,803 | $0.039 |

Total $4.60 at list prices.

**What holds.** An agent saves when the task asks for it, and rarely otherwise:
0 of 6 with the server alone, 2 of 6 with a system-prompt line. The server's
"save unasked" instruction does not do it by itself.

**What is void.** The reader numbers do not measure ais as memory. The corpus is
the ais repository, whose questions are about ais, and the MCP server's
instructions describe ais: Haiku answered 7 of its 12 `mem` questions in one
turn from the tool description, without calling a tool, and got them wrong.
Rerun on a project unrelated to ais before quoting any reader figure.

**What else it shows.** At six notes, a NOTES.md loaded whole is the cheapest
reader for both models: 10 KB costs less than a lookup. Half the `mem-ask`
records were multi-line and became documents, so a recall returns a path and
the reader spends a turn reading it. The facts sit one grep away in this
repository's docs, which makes `none` cheap too. Where memory should pay (notes
too large to load every session, facts not written down anywhere) is untested.

## Run it

```sh
python3 run.py --out /tmp/memrun            # --readers haiku,opus --jobs 4
```

`results.csv` is this run, answers included.
