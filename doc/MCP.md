# ais --mcp: the agent tool

`ais --mcp` hands your index to a coding agent as a tool it can call. MCP is the
protocol agents use to reach tools on your machine, and there is nothing to
understand beyond one line of config: the agent starts `ais` itself and talks to
it over a pipe.

A key lookup returns the matching rows, and a wrong key returns nothing rather
than something plausible, so only what matched enters the context window. That
saving is measured in the README, [with the run and how to reproduce
it](../README.md#give-an-agent-your-index).

## Wire it up

```sh
claude mcp add ais -- ais --mcp
```

Any client that reads the usual JSON starts it the same way, in its own config
file:

```json
{ "mcpServers": { "ais": { "command": "ais", "args": ["--mcp"] } } }
```

Claude Code is the client the tests exercise. Nothing runs in the background,
there is no account and no port.

## Which index it opens

Your home index `~/.ais`, the current named index, or whatever `-f` names. The
`.ais/` found by walking up from the working directory is the one index `--mcp`
will not serve: a repository you cloned can ship one, and it would become the
agent's memory with its records reaching the model as tool output. On that one it
prints a line naming `-f` and exits 2 before serving anything. A project index is
therefore served by naming it, and that line in the client's own configuration is
the permission:

    claude mcp add ais -- ais -f /abs/path/of/project/.ais --mcp

`ais --serve` still opens a walked-up `.ais/`, because the web GUI is yours and
you opened it in that directory on purpose.

`ais --init` in a repository gives that project its own index. What an agent
files there sits beside the code as plain text, readable in a diff, and separate
from your personal index. The server names the index it opened, by its absolute
path, in the instructions it hands the client on connect, because a repo-local
`.ais/` and your personal `~/.ais` speak the same protocol and otherwise look
alike.

## The tools

| Tool | Arguments | Rows by default | Answers |
| --- | --- | --- | --- |
| `recall` | `keys`, `match` (`all`, the default, or `any`), `limit` | 200 | `id\|value` per line |
| `find` | `text`, `limit` | 50 | `id\|value` per line, values holding TEXT (case folded over ASCII) |
| `tags` | `limit` | 200 | `count\|key` per line, busiest first |
| `timeline` | `count`, or `limit` for the same thing | 20 | `id\|timestamp\|keys\|value` per line, newest first |
| `save` | `value`, `keys`, both required | | `saved as record N under KEYS`; present only with `rw` |

A row budget is a JSON integer from 1 to 1000. A string, a fraction, a zero or a
number past 1000 is a tool error naming the range: nothing is clamped, because a
model cannot tell a rewritten limit from the one it asked for. An argument no
tool has is refused naming the ones it takes, since an invented `since` on
`timeline` would otherwise be answered with the whole index and read as filtered.
`match` is `all` or `any` and nothing else. Keys fold case over ASCII only, in
recall as in find, so `café` and `CAFÉ` are two different keys.

## What comes back

Rows are `id|value`, the shape recall prints at the terminal; `timeline` adds
the id that `ais --timeline` omits and joins its fields with `|`. A document
record comes back as its `blobs/` path, where recall at the terminal prints the
body itself. An empty result is words rather than an empty block, because a
model reads an empty block as a broken tool: `no match` from `recall` and
`find`, `the index has no keys yet` from `tags`, `the index is empty` from
`timeline`, which are three different facts. A reply that spent its budget ends
with `(stopped at the limit; there may be more)`, and every tool marks it, and
only when one more row exists, so a page is never read as the whole index.

## The keys stay yours

Nobody types a tool call. People say "save this", "add it to my memory", "what
did I file about venice", and the model decides what those mean. So the server
tells it, in the `instructions` it hands every client on connect. The first line
is the whole position:

> ais is this person's own associative index: things they filed under their own
> words, on their own disk, in plain text.

The rule it spends most words on: when you have not said what to file something
under, the model asks, and offers the keys your index already uses. It does not
invent a vocabulary for you.

This is the part no automatic tagger can copy without giving up its own premise.
A model that files your things under the words it would have chosen has handed
you the average of everyone's words, which is the thing this index exists not to
be. See [`about.txt`](about.txt) for the argument in full.

## A project's or a group's index

An index named with `-f` belongs to a project or a group, and its keys are that
group's vocabulary: the people and the agents that file there, of any model, in
any session. There the agent chooses keys itself, from what `tags` lists: a key
already in use whenever one fits, a new one only for what none of them names.
The instructions also tell it to save, unasked, what a later session would
otherwise have to work out again. Measured, it mostly does not: Sonnet saved
nothing in six tasks with the server alone, and twice in six with a line in its
system prompt asking for it. It saved every time the task itself ended with
"save it" ([`experiment/memory`](../experiment/memory/README.md)). So ask for
the save, in the task or in the project's agent instructions. The
bias kept is the group's instead of one person's, and it is still no model's
average, because every reader recalls by the words the group used.

`-f` naming the home index is still one person's, and the agent asks.

Several agents serve one index at once, each client starting its own
`ais --mcp`. Reads take no lock, and writes serialize under an exclusive lock, so
two agents never collide on a record id ([`limitations.txt`](limitations.txt)).
Across machines, [`--sync-folder`](SYNC.md) carries the index.

The same index lives on laptops (Linux, macOS, Windows) and Android phones, and
an iPhone app is in progress. What an agent saves on the laptop is on the phone
after the next sync, and what you save on the phone reaches the agent.

## What the file does not show

The store is a text file you can read, and that makes the engine look like a
program that appends lines to it. Each row below is a case such a program gets
wrong. The merge rules are in [`dev/MERGE.md`](dev/MERGE.md).

| Case | What ais does |
| --- | --- |
| Two agents write at once | an exclusive lock per write, the next record id re-read under it; reads take no lock |
| A crash mid-rewrite | the new file is written beside the old and renamed over it, so a file is old or new, never half |
| Recall at a million records | a posting list per key and an id-to-offset index: a rare key in 9 ms (1.02 s by scan), a key on 270,360 records in 2.2 s (hours by scan), [`performance.txt`](performance.txt) |
| The same value saved twice | one record, the keys merged |
| Two devices edit and delete while apart | timestamped, content-addressed tombstones: a delete propagates, the merge is order-independent, and re-saving a deleted value restamps it so the old delete does not remove it again |
| Two devices name a document alike | the name carries a timestamp and a random tag; a clash resolves to a name derived from the body, so every device picks the same one |
| A damaged line | skipped as one corrupt line, named by its byte offset; the rest reads |
| Moving it between devices | LAN sync by a code, encrypted with XChaCha20-Poly1305; a shared folder, where no two devices need be online at once |

998 engine tests, 569 CLI tests, and sync, mesh and UI suites on Linux, macOS,
Windows and Android hold these in place.

## What it refuses

Writes are off unless you start it as `ais --mcp rw`. An agent gets recall by
default and has to be handed the write bit on purpose. Read-only is not a save
that fails: the instructions then carry no save paragraph at all, and say that
restarting as `ais --mcp rw` turns one on.

There is no delete, no re-tag, no edit, at any setting. Those are the operations
whose damage you cannot see happening, and the CLI is two keystrokes away.

`save` itself refuses seven things, each in the words the model gets back:

| The refusal | Because |
| --- | --- |
| `save needs at least one key: ask which keys to file it under, several separated by spaces, and offer the keys tags already lists` | a record filed under nothing cannot be recalled by any key, ever |
| `save needs a value with something in it: this one is blank space only` | blanks are trimmed, and what is left of such a value is nothing |
| `reserved prefix; ais writes these itself` | a value starting with `aisc:` or `blobs/` is the index writing to itself, a secret marker and a document path, and a model that could plant either could make a value the front ends read as a file of ais's own making |
| `the index would file that key under another name: a key cannot hold '/', '\', '\|', a tab or any other control byte, or begin with '.': KEY` | the key is both a field of a line-oriented store and the name of its posting file, so the index would have to file it under another spelling |
| `a key cannot begin with '-': ais reads that as detaching the key, and files nothing under it: KEY` | the CLI's token walk reads a leading `-` as a detach, so that key would file nothing at all |
| `a key is at most 255 bytes; that one is 256: KEY` | the key is the posting's filename, and 255 is POSIX `NAME_MAX` |
| `at most 64 keys in one call` | the engine's width for one record |

Keys arrive as an array or as one string with blanks between them, and blanks
split either way, so a space inside an array element makes two keys rather than
a refusal.

A request line is at most 65,535 bytes. A longer one is dropped whole and
answered `request too large` with `"id": null`, since the id was never read, so
a document past roughly 64 KB is filed with `ais --doc` at the terminal rather
than through `save`.

Three saves succeed in a shape worth knowing. A one-line value the index already
holds is not stored twice: the keys are added to the record that holds it, and
the reply says `added KEYS to existing record N, now under ALLKEYS`. A value
already filed under exactly those keys adds nothing, and the reply says so:
`record N already holds this value under KEYS; nothing added`. A value with a
newline in it becomes a document file, and the reply names it, `as document
blobs/NAME (find does not search inside documents)`; that file is written afresh
each time, so saving the same three lines twice leaves two records and two
identical documents.

## What a model never sees

An encrypted value is never handed over. Every tool emits it as `aisc:
(encrypted, hidden)`, `find` included, which still matches on the stored
ciphertext and still hides it. Cleartext appears only at a terminal and never on
a pipe, and this server is a pipe, so there is no unlocked vault behind it for an
agent to drain.

A value that names a file is a path, and the agent can read that file if it
should. A `--doc` body sits at `blobs/...` under the index directory the
instructions named; anything else you indexed is the path you gave. A `blobs/`
value the index would not have written is withheld: the shape it accepts is
`blobs/` and exactly one more name, which is not `.` or `..` and carries no
further `/`, no `\` and no control byte. Anything else comes back as `[document
path withheld: escapes the index]`, whether or not it would have resolved
inside, and the row keeps its id.

## What it is not

It is not a vector store and not RAG; the reason is in the README under
[Why not embeddings or a vector database](../README.md#questions).

It is not a model deciding what to remember on your behalf. You name the keys,
or the agent asks you for them.

It is not a server: no daemon, no port, no account, nothing leaves the machine.

Implementation notes, the wire format and the test recipe are in
[`dev/MCP.md`](dev/MCP.md).
